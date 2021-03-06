/* Console example — various system commands

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/
#define LOG_LOCAL_LEVEL ESP_LOG_INFO
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "driver/rtc_io.h"
#include "driver/uart.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "soc/rtc_cntl_reg.h"
#include "esp32/rom/uart.h"
#include "cmd_system.h"
#include "sdkconfig.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"
#include "platform_esp32.h"
#include "config.h"
#include "esp_sleep.h"
#include "driver/uart.h"            // for the uart driver access

#ifdef CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS
#define WITH_TASKS_INFO 1
#endif


static const char * TAG = "cmd_system";

static void register_free();
static void register_heap();
static void register_version();
static void register_restart();
static void register_deep_sleep();
static void register_light_sleep();
static void register_factory_boot();
static void register_restart_ota();
#if WITH_TASKS_INFO
static void register_tasks();
#endif

void register_system()
{
    register_free();
    register_heap();
    register_version();
    register_restart();
    register_deep_sleep();
    register_light_sleep();
    register_factory_boot();
    register_restart_ota();
#if WITH_TASKS_INFO
    register_tasks();
#endif
}

/* 'version' command */
static int get_version(int argc, char **argv)
{
    esp_chip_info_t info;
    esp_chip_info(&info);
    printf("IDF Version:%s\r\n", esp_get_idf_version());
    printf("Chip info:\r\n");
    printf("\tmodel:%s\r\n", info.model == CHIP_ESP32 ? "ESP32" : "Unknow");
    printf("\tcores:%d\r\n", info.cores);
    printf("\tfeature:%s%s%s%s%d%s\r\n",
           info.features & CHIP_FEATURE_WIFI_BGN ? "/802.11bgn" : "",
           info.features & CHIP_FEATURE_BLE ? "/BLE" : "",
           info.features & CHIP_FEATURE_BT ? "/BT" : "",
           info.features & CHIP_FEATURE_EMB_FLASH ? "/Embedded-Flash:" : "/External-Flash:",
           spi_flash_get_chip_size() / (1024 * 1024), " MB");
    printf("\trevision number:%d\r\n", info.revision);
    return 0;
}

static void register_version()
{
    const esp_console_cmd_t cmd = {
        .command = "version",
        .help = "Get version of chip and SDK",
        .hint = NULL,
        .func = &get_version,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/** 'restart' command restarts the program */


esp_err_t guided_boot(esp_partition_subtype_t partition_subtype)
{
#if RECOVERY_APPLICATION
	if(partition_subtype ==ESP_PARTITION_SUBTYPE_APP_FACTORY){
		ESP_LOGW(TAG,"RECOVERY application is already active");
		if(!wait_for_commit()){
			ESP_LOGW(TAG,"Unable to commit configuration. ");
		}
		ESP_LOGW(TAG, "Restarting after tx complete");
		uart_wait_tx_done(UART_NUM_1, 500 / portTICK_RATE_MS);
		esp_restart();
		return ESP_OK;
	}
#else
	if(partition_subtype !=ESP_PARTITION_SUBTYPE_APP_FACTORY){
		ESP_LOGW(TAG,"SQUEEZELITE application is already active");
		if(!wait_for_commit()){
			ESP_LOGW(TAG,"Unable to commit configuration. ");
		}
		ESP_LOGW(TAG, "Restarting after tx complete");
		uart_wait_tx_done(UART_NUM_1, 500 / portTICK_RATE_MS);
		esp_restart();
		return ESP_OK;
	}
#endif
	esp_err_t err = ESP_OK;
	bool bFound=false;
    ESP_LOGI(TAG, "Looking for partition type %u",partition_subtype);
    const esp_partition_t *partition;
	esp_partition_iterator_t it = esp_partition_find(ESP_PARTITION_TYPE_APP, partition_subtype, NULL);

	if(it == NULL){
		ESP_LOGE(TAG,"Unable initialize partition iterator!");
		set_status_message(ERROR, "Reboot failed. Cannot iterate through partitions");
	}
	else
	{
		ESP_LOGD(TAG, "Found partition. Getting info.");
		partition = (esp_partition_t *) esp_partition_get(it);
		ESP_LOGD(TAG, "Releasing partition iterator");
		esp_partition_iterator_release(it);
		if(partition != NULL){
			ESP_LOGI(TAG, "Found application partition %s sub type %u", partition->label,partition_subtype);
			err=esp_ota_set_boot_partition(partition);
			if(err!=ESP_OK){
				ESP_LOGE(TAG,"Unable to set partition as active for next boot. %s",esp_err_to_name(err));
				bFound=false;
				set_status_message(ERROR, "Unable to select partition for reboot.");
			}
			else{
				ESP_LOGW(TAG, "Application partition %s sub type %u is selected for boot", partition->label,partition_subtype);
				bFound=true;
				set_status_message(WARNING, "Rebooting!");
			}
		}
		else
		{
			ESP_LOGE(TAG,"partition type %u not found!  Unable to reboot to recovery.",partition_subtype);
			set_status_message(ERROR, "Partition not found.");
		}
		ESP_LOGD(TAG, "Yielding to other processes");
		taskYIELD();
		if(bFound) {
			ESP_LOGW(TAG,"Configuration %s changes. ",config_has_changes()?"has":"does not have");
			if(!wait_for_commit()){
				ESP_LOGW(TAG,"Unable to commit configuration. ");
			}
			ESP_LOGW(TAG, "Restarting after tx complete");
			uart_wait_tx_done(UART_NUM_1, 500 / portTICK_RATE_MS);
			esp_restart();
		}
	}

	return ESP_OK;
}

static int restart(int argc, char **argv)
{
	ESP_LOGW(TAG, "\n\nPerforming a simple restart to the currently active partition.");
	if(!wait_for_commit()){
		ESP_LOGW(TAG,"Unable to commit configuration. ");
	}
	ESP_LOGW(TAG, "Restarting after tx complete");
    uart_wait_tx_done(UART_NUM_1, 500 / portTICK_RATE_MS);
    esp_restart();
    return 0;
}

void simple_restart()
{
	ESP_LOGW(TAG,"\n\n Called to perform a simple system reboot.");
	if(!wait_for_commit()){
		ESP_LOGW(TAG,"Unable to commit configuration. ");
	}

	ESP_LOGW(TAG, "Restarting after tx complete");
	uart_wait_tx_done(UART_NUM_1, 500 / portTICK_RATE_MS);
    esp_restart();
}

esp_err_t guided_restart_ota(){
	ESP_LOGW(TAG,"\n\nCalled for a reboot to OTA Application");
    guided_boot(ESP_PARTITION_SUBTYPE_APP_OTA_0);
	return ESP_FAIL; // return fail.  This should never return... we're rebooting!
}
esp_err_t guided_factory(){
	ESP_LOGW(TAG,"\n\nCalled for a reboot to recovery application");
	guided_boot(ESP_PARTITION_SUBTYPE_APP_FACTORY);
	return ESP_FAIL; // return fail.  This should never return... we're rebooting!
}
static int restart_factory(int argc, char **argv)
{
	ESP_LOGW(TAG, "Executing guided boot into recovery");
	guided_boot(ESP_PARTITION_SUBTYPE_APP_FACTORY);
	return 0; // return fail.  This should never return... we're rebooting!
}
static int restart_ota(int argc, char **argv)
{
	ESP_LOGW(TAG, "Executing guided boot into ota app 0");
	guided_boot(ESP_PARTITION_SUBTYPE_APP_OTA_0);
	return 0; // return fail.  This should never return... we're rebooting!
}
static void register_restart()
{
    const esp_console_cmd_t cmd = {
        .command = "restart",
        .help = "Software reset of the chip",
        .hint = NULL,
        .func = &restart,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}
static void register_restart_ota()
{
    const esp_console_cmd_t cmd = {
        .command = "restart_ota",
        .help = "Selects the ota app partition to boot from and performa a software reset of the chip",
        .hint = NULL,
        .func = &restart_ota,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

static void register_factory_boot()
{
    const esp_console_cmd_t cmd = {
        .command = "recovery",
        .help = "Resets and boot to recovery (if available)",
        .hint = NULL,
        .func = &restart_factory,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}
/** 'free' command prints available heap memory */

static int free_mem(int argc, char **argv)
{
    printf("%d\n", esp_get_free_heap_size());
    return 0;
}

static void register_free()
{
    const esp_console_cmd_t cmd = {
        .command = "free",
        .help = "Get the current size of free heap memory",
        .hint = NULL,
        .func = &free_mem,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/* 'heap' command prints minumum heap size */
static int heap_size(int argc, char **argv)
{
    uint32_t heap_size = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    ESP_LOGI(TAG, "min heap size: %u", heap_size);
    return 0;
}

static void register_heap()
{
    const esp_console_cmd_t heap_cmd = {
        .command = "heap",
        .help = "Get minimum size of free heap memory that was available during program execution",
        .hint = NULL,
        .func = &heap_size,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&heap_cmd) );

}

/** 'tasks' command prints the list of tasks and related information */
#if WITH_TASKS_INFO

static int tasks_info(int argc, char **argv)
{
    const size_t bytes_per_task = 40; /* see vTaskList description */
    char *task_list_buffer = malloc(uxTaskGetNumberOfTasks() * bytes_per_task);
    if (task_list_buffer == NULL) {
        ESP_LOGE(TAG, "failed to allocate buffer for vTaskList output");
        return 1;
    }
    fputs("Task Name\tStatus\tPrio\tHWM\tTask#", stdout);
#ifdef CONFIG_FREERTOS_VTASKLIST_INCLUDE_COREID
    fputs("\tAffinity", stdout);
#endif
    fputs("\n", stdout);
    vTaskList(task_list_buffer);
    fputs(task_list_buffer, stdout);
    free(task_list_buffer);
    return 0;
}

static void register_tasks()
{
    const esp_console_cmd_t cmd = {
        .command = "tasks",
        .help = "Get information about running tasks",
        .hint = NULL,
        .func = &tasks_info,
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

#endif // WITH_TASKS_INFO

/** 'deep_sleep' command puts the chip into deep sleep mode */

static struct {
    struct arg_int *wakeup_time;
    struct arg_int *wakeup_gpio_num;
    struct arg_int *wakeup_gpio_level;
    struct arg_end *end;
} deep_sleep_args;


static int deep_sleep(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &deep_sleep_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, deep_sleep_args.end, argv[0]);
        return 1;
    }
    if (deep_sleep_args.wakeup_time->count) {
        uint64_t timeout = 1000ULL * deep_sleep_args.wakeup_time->ival[0];
        ESP_LOGI(TAG, "Enabling timer wakeup, timeout=%lluus", timeout);
        ESP_ERROR_CHECK( esp_sleep_enable_timer_wakeup(timeout) );
    }
    if (deep_sleep_args.wakeup_gpio_num->count) {
        int io_num = deep_sleep_args.wakeup_gpio_num->ival[0];
        if (!rtc_gpio_is_valid_gpio(io_num)) {
            ESP_LOGE(TAG, "GPIO %d is not an RTC IO", io_num);
            return 1;
        }
        int level = 0;
        if (deep_sleep_args.wakeup_gpio_level->count) {
            level = deep_sleep_args.wakeup_gpio_level->ival[0];
            if (level != 0 && level != 1) {
                ESP_LOGE(TAG, "Invalid wakeup level: %d", level);
                return 1;
            }
        }
        ESP_LOGI(TAG, "Enabling wakeup on GPIO%d, wakeup on %s level",
                 io_num, level ? "HIGH" : "LOW");

        ESP_ERROR_CHECK( esp_sleep_enable_ext1_wakeup(1ULL << io_num, level) );
    }
    rtc_gpio_isolate(GPIO_NUM_12);
    esp_deep_sleep_start();
}

static void register_deep_sleep()
{
    deep_sleep_args.wakeup_time =
        arg_int0("t", "time", "<t>", "Wake up time, ms");
    deep_sleep_args.wakeup_gpio_num =
        arg_int0(NULL, "io", "<n>",
                 "If specified, wakeup using GPIO with given number");
    deep_sleep_args.wakeup_gpio_level =
        arg_int0(NULL, "io_level", "<0|1>", "GPIO level to trigger wakeup");
    deep_sleep_args.end = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command = "deep_sleep",
        .help = "Enter deep sleep mode. "
        "Two wakeup modes are supported: timer and GPIO. "
        "If no wakeup option is specified, will sleep indefinitely.",
        .hint = NULL,
        .func = &deep_sleep,
        .argtable = &deep_sleep_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

/** 'light_sleep' command puts the chip into light sleep mode */

static struct {
    struct arg_int *wakeup_time;
    struct arg_int *wakeup_gpio_num;
    struct arg_int *wakeup_gpio_level;
    struct arg_end *end;
} light_sleep_args;

static int light_sleep(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **) &light_sleep_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, light_sleep_args.end, argv[0]);
        return 1;
    }
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    if (light_sleep_args.wakeup_time->count) {
        uint64_t timeout = 1000ULL * light_sleep_args.wakeup_time->ival[0];
        ESP_LOGI(TAG, "Enabling timer wakeup, timeout=%lluus", timeout);
        ESP_ERROR_CHECK( esp_sleep_enable_timer_wakeup(timeout) );
    }
    int io_count = light_sleep_args.wakeup_gpio_num->count;
    if (io_count != light_sleep_args.wakeup_gpio_level->count) {
        ESP_LOGE(TAG, "Should have same number of 'io' and 'io_level' arguments");
        return 1;
    }
    for (int i = 0; i < io_count; ++i) {
        int io_num = light_sleep_args.wakeup_gpio_num->ival[i];
        int level = light_sleep_args.wakeup_gpio_level->ival[i];
        if (level != 0 && level != 1) {
            ESP_LOGE(TAG, "Invalid wakeup level: %d", level);
            return 1;
        }
        ESP_LOGI(TAG, "Enabling wakeup on GPIO%d, wakeup on %s level",
                 io_num, level ? "HIGH" : "LOW");

        ESP_ERROR_CHECK( gpio_wakeup_enable(io_num, level ? GPIO_INTR_HIGH_LEVEL : GPIO_INTR_LOW_LEVEL) );
    }
    if (io_count > 0) {
        ESP_ERROR_CHECK( esp_sleep_enable_gpio_wakeup() );
    }
    if (CONFIG_CONSOLE_UART_NUM <= UART_NUM_1) {
        ESP_LOGI(TAG, "Enabling UART wakeup (press ENTER to exit light sleep)");
        ESP_ERROR_CHECK( uart_set_wakeup_threshold(CONFIG_CONSOLE_UART_NUM, 3) );
        ESP_ERROR_CHECK( esp_sleep_enable_uart_wakeup(CONFIG_CONSOLE_UART_NUM) );
    }
    fflush(stdout);
    uart_tx_wait_idle(CONFIG_CONSOLE_UART_NUM);
    esp_light_sleep_start();
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    const char *cause_str;
    switch (cause) {
    case ESP_SLEEP_WAKEUP_GPIO:
        cause_str = "GPIO";
        break;
    case ESP_SLEEP_WAKEUP_UART:
        cause_str = "UART";
        break;
    case ESP_SLEEP_WAKEUP_TIMER:
        cause_str = "timer";
        break;
    default:
        cause_str = "unknown";
        printf("%d\n", cause);
    }
    ESP_LOGI(TAG, "Woke up from: %s", cause_str);
    return 0;
}

static void register_light_sleep()
{
    light_sleep_args.wakeup_time =
        arg_int0("t", "time", "<t>", "Wake up time, ms");
    light_sleep_args.wakeup_gpio_num =
        arg_intn(NULL, "io", "<n>", 0, 8,
                 "If specified, wakeup using GPIO with given number");
    light_sleep_args.wakeup_gpio_level =
        arg_intn(NULL, "io_level", "<0|1>", 0, 8, "GPIO level to trigger wakeup");
    light_sleep_args.end = arg_end(3);

    const esp_console_cmd_t cmd = {
        .command = "light_sleep",
        .help = "Enter light sleep mode. "
        "Two wakeup modes are supported: timer and GPIO. "
        "Multiple GPIO pins can be specified using pairs of "
        "'io' and 'io_level' arguments. "
        "Will also wake up on UART input.",
        .hint = NULL,
        .func = &light_sleep,
        .argtable = &light_sleep_args
    };
    ESP_ERROR_CHECK( esp_console_cmd_register(&cmd) );
}

