/*
   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <stdio.h>
#include "esp_log.h"
#include "driver/gpio.h"
#include <driver/i2c.h>
#include "config.h"
#include "battery.h"
#include "led.h"
#include "monitor.h"
#include "globdefs.h"
#include "accessors.h"

extern void battery_svc_init(void);
extern void monitor_svc_init(void);
extern void led_svc_init(void);

int i2c_system_port = I2C_SYSTEM_PORT;
int i2c_system_speed = 400000;
int spi_system_host = SPI_SYSTEM_HOST;
int spi_system_dc_gpio = -1;

static const char *TAG = "services";

/****************************************************************************************
 * 
 */
void set_power_gpio(int gpio, char *value) {
	bool parsed = true;
	
	if (!strcasecmp(value, "vcc") ) {
		gpio_pad_select_gpio(gpio);
		gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
		gpio_set_level(gpio, 1);
	} else if (!strcasecmp(value, "gnd")) {
		gpio_pad_select_gpio(gpio);
		gpio_set_direction(gpio, GPIO_MODE_OUTPUT);
		gpio_set_level(gpio, 0);
	} else parsed = false	;
	
	if (parsed) ESP_LOGI(TAG, "set GPIO %u to %s", gpio, value);
 }	
 

/****************************************************************************************
 * 
 */
void services_init(void) {
	gpio_install_isr_service(0);
	
#ifdef CONFIG_I2C_LOCKED
	if (i2c_system_port == 0) {
		i2c_system_port = 1;
		ESP_LOGE(TAG, "Port 0 is reserved for internal DAC use");
	}
#endif

	// set potential power GPIO
	parse_set_GPIO(set_power_gpio);

	// shared I2C bus 
	const i2c_config_t * i2c_config = config_i2c_get(&i2c_system_port);
	ESP_LOGI(TAG,"Configuring I2C sda:%d scl:%d port:%u speed:%u", i2c_config->sda_io_num, i2c_config->scl_io_num, i2c_system_port, i2c_config->master.clk_speed);

	if (i2c_config->sda_io_num != -1 && i2c_config->scl_io_num != -1) {
		i2c_param_config(i2c_system_port, i2c_config);
		i2c_driver_install(i2c_system_port, i2c_config->mode, 0, 0, 0 );
	} else {
		i2c_system_port = -1;
		ESP_LOGW(TAG, "no I2C configured");
	}	
		
	const spi_bus_config_t * spi_config = config_spi_get((spi_host_device_t*) &spi_system_host);
	ESP_LOGI(TAG,"Configuring SPI data:%d clk:%d host:%u dc:%d", spi_config->mosi_io_num, spi_config->sclk_io_num, spi_system_host, spi_system_dc_gpio);
	
	if (spi_config->mosi_io_num != -1 && spi_config->sclk_io_num != -1) {
		spi_bus_initialize( spi_system_host, spi_config, 1 );
		if (spi_system_dc_gpio != -1) {
			gpio_set_direction( spi_system_dc_gpio, GPIO_MODE_OUTPUT );
			gpio_set_level( spi_system_dc_gpio, 0 );
		} else {
			ESP_LOGW(TAG, "No DC GPIO set, SPI display will not work");
		}	
	} else {
		spi_system_host = -1;
		ESP_LOGW(TAG, "no SPI configured");
	}	

	led_svc_init();
	battery_svc_init();
	monitor_svc_init();
}