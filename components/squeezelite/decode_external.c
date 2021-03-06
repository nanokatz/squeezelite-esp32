/* 
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include "config.h" 
#include "squeezelite.h"
#include "bt_app_sink.h"
#include "raop_sink.h"
#include <math.h>

#define LOCK_O   mutex_lock(outputbuf->mutex)
#define UNLOCK_O mutex_unlock(outputbuf->mutex)
#define LOCK_D   mutex_lock(decode.mutex);
#define UNLOCK_D mutex_unlock(decode.mutex);

enum { DECODE_BT = 1, DECODE_RAOP };

extern struct outputstate output;
extern struct decodestate decode;
extern struct buffer *outputbuf;
// this is the only system-wide loglevel variable
extern log_level loglevel;


static bool enable_bt_sink;
static bool enable_airplay;

#define RAOP_OUTPUT_SIZE 	(RAOP_SAMPLE_RATE * 2 * 2 * 2 * 1.2)
#define SYNC_WIN_RUN	32
#define SYNC_WIN_CHECK	8
#define SYNC_WIN_START	2

static raop_event_t	raop_state;

static EXT_RAM_ATTR struct {
	bool enabled;
	int sum, count, win, errors[SYNC_WIN_RUN];
	s32_t len;
	u32_t start_time, playtime;
} raop_sync;

/****************************************************************************************
 * Common sink data handler
 */
static void sink_data_handler(const uint8_t *data, uint32_t len)
{
    size_t bytes, space;
	int wait = 5;
		
	// would be better to lock output, but really, it does not matter
	if (!output.external) {
		LOG_SDEBUG("Cannot use external sink while LMS is controlling player");
		return;
	} 
	
	// there will always be room at some point
	while (len) {
		LOCK_O;

		bytes = min(_buf_space(outputbuf), _buf_cont_write(outputbuf));
		bytes = min(len, bytes);
#if BYTES_PER_FRAME == 4
		memcpy(outputbuf->writep, data, bytes);
#else
		{
			s16_t *iptr = (s16_t*) data;
			ISAMPLE_T *optr = (ISAMPLE_T*) outputbuf->writep;
			size_t n = bytes / BYTES_PER_FRAME * 2;
			while (n--) *optr++ = *iptr++ << 16;
		}
#endif	
		_buf_inc_writep(outputbuf, bytes);
		space = _buf_space(outputbuf);
		
		len -= bytes;
		data += bytes;
				
		UNLOCK_O;
		
		// allow i2s to empty the buffer if needed
		if (len && !space && wait--) usleep(20000);
	}	
	
	if (!wait) {
		LOG_WARN("Waited too long, dropping frames");
	}
}

/****************************************************************************************
 * BT sink command handler
 */

static bool bt_sink_cmd_handler(bt_sink_cmd_t cmd, va_list args) 
{
	// don't LOCK_O as there is always a chance that LMS takes control later anyway
	if (output.external != DECODE_BT && output.state > OUTPUT_STOPPED) {
		LOG_WARN("Cannot use BT sink while LMS/AirPlay is controlling player");
		return false;
	} 	

	LOCK_D;

	if (cmd != BT_SINK_VOLUME) LOCK_O;
		
	switch(cmd) {
	case BT_SINK_AUDIO_STARTED:
		output.next_sample_rate = output.current_sample_rate = va_arg(args, u32_t);
		output.external = DECODE_BT;
		output.state = OUTPUT_STOPPED;
		output.frames_played = 0;
		_buf_flush(outputbuf);
		if (decode.state != DECODE_STOPPED) decode.state = DECODE_ERROR;
		LOG_INFO("BT sink started");
		break;
	case BT_SINK_AUDIO_STOPPED:	
		if (output.external == DECODE_BT) {
			if (output.state > OUTPUT_STOPPED) output.state = OUTPUT_STOPPED;
			output.stop_time = gettime_ms();
			LOG_INFO("BT sink stopped");
		}	
		break;
	case BT_SINK_PLAY:
		output.state = OUTPUT_RUNNING;
		LOG_INFO("BT playing");
		break;
	case BT_SINK_STOP:		
		_buf_flush(outputbuf);
		output.state = OUTPUT_STOPPED;
		output.stop_time = gettime_ms();
		LOG_INFO("BT stopped");
		break;
	case BT_SINK_PAUSE:		
		output.stop_time = gettime_ms();
		LOG_INFO("BT paused, just silence");
		break;
	case BT_SINK_RATE:
		output.next_sample_rate = output.current_sample_rate = va_arg(args, u32_t);
		LOG_INFO("Setting BT sample rate %u", output.next_sample_rate);
		break;
	case BT_SINK_VOLUME: {
		u16_t volume = (u16_t) va_arg(args, u32_t);
		volume = 65536 * powf(volume / 128.0f, 3);
		set_volume(volume, volume);
		break;
	default:
		break;
	}
	}
	
	if (cmd != BT_SINK_VOLUME) UNLOCK_O;
	UNLOCK_D;

	return true;
}

/****************************************************************************************
 * raop sink data handler
 */
static void raop_sink_data_handler(const uint8_t *data, uint32_t len, u32_t playtime) {
	
	raop_sync.playtime = playtime;
	raop_sync.len = len;

	sink_data_handler(data, len);
}	

/****************************************************************************************
 * AirPlay sink command handler
 */
static bool raop_sink_cmd_handler(raop_event_t event, va_list args)
{
	// don't LOCK_O as there is always a chance that LMS takes control later anyway
	if (output.external != DECODE_RAOP && output.state > OUTPUT_STOPPED) {
		LOG_WARN("Cannot use Airplay sink while LMS/BT is controlling player");
		return false;
	} 	

	LOCK_D;
	
	if (event != RAOP_VOLUME) LOCK_O;
	
	// this is async, so player might have been deleted
	switch (event) {
		case RAOP_TIMING: {
			u32_t ms, now = gettime_ms();
			int error;
									
			if (!raop_sync.enabled || output.state < OUTPUT_RUNNING || output.frames_played_dmp < output.device_frames) break;
			
			// first must make sure we started on time
			if (raop_sync.win == SYNC_WIN_START) {
				// how many ms have we really played
				ms = now - output.updated + ((output.frames_played_dmp - output.device_frames) * 10) / (RAOP_SAMPLE_RATE / 100);
				error = ms - (now - raop_sync.start_time);
				
				LOG_INFO("backend played %u, desired %u, (delta:%d)", ms, now - raop_sync.start_time, error);
			} else {	
				u32_t level = _buf_used(outputbuf);
				
				// in how many ms will the most recent block play 
				ms = (((s32_t)(level - raop_sync.len) / BYTES_PER_FRAME + output.device_frames + output.frames_in_process) * 10) / (RAOP_SAMPLE_RATE / 100) - (s32_t) (now - output.updated);
				
				// when outputbuf is empty, it means we have a network black-out or something
				error = level ? (raop_sync.playtime - now) - ms : 0;
				
				if (loglevel == lDEBUG || !level) {
					LOG_INFO("head local:%d, remote:%d (delta:%d)", ms, raop_sync.playtime - now, error);
					LOG_INFO("obuf:%u, sync_len:%u, devframes:%u, inproc:%u", _buf_used(outputbuf), raop_sync.len, output.device_frames, output.frames_in_process);
				}	
			}	
			
			// calculate sum, error and update sliding window
			raop_sync.errors[raop_sync.count++ % raop_sync.win] = error;
			raop_sync.sum += error;
			error = raop_sync.sum / min(raop_sync.count, raop_sync.win);
			
			// move to normal mode if possible
			if (raop_sync.win == SYNC_WIN_START && raop_sync.count >= SYNC_WIN_START && abs(error) < 10) {
				raop_sync.win = SYNC_WIN_RUN;
				LOG_INFO("switching to slow sync mode %u", raop_sync.win);
			}	

			// wait till e have enough data or there is a strong deviation
			if ((raop_sync.count >= raop_sync.win && abs(error) > 10) || (raop_sync.count >= SYNC_WIN_CHECK && abs(error) > 100)) { 
			
				// correct if needed
				if (error < 0) {
					output.skip_frames = -(error * RAOP_SAMPLE_RATE) / 1000;
					output.state = OUTPUT_SKIP_FRAMES;					
					LOG_INFO("skipping %u frames (count:%d)", output.skip_frames, raop_sync.count);
				} else {
					output.pause_frames = (error * RAOP_SAMPLE_RATE) / 1000;
					output.state = OUTPUT_PAUSE_FRAMES;
					LOG_INFO("pausing for %u frames (count: %d)", output.pause_frames, raop_sync.count);
				}

				// reset sliding window		
				raop_sync.sum = raop_sync.count = 0;
				memset(raop_sync.errors, 0, sizeof(raop_sync.errors));
											
			}	

			break;
		}
		case RAOP_SETUP:
			// we need a fair bit of space for RTP process
			_buf_resize(outputbuf, RAOP_OUTPUT_SIZE);
			output.frames_played = 0;
			output.external = DECODE_RAOP;
			output.state = OUTPUT_STOPPED;
			if (decode.state != DECODE_STOPPED) decode.state = DECODE_ERROR;
			LOG_INFO("resizing buffer %u", outputbuf->size);
			break;
		case RAOP_STREAM:
			LOG_INFO("Stream", NULL);
			raop_state = event;
			raop_sync.win = SYNC_WIN_START;
			raop_sync.sum = raop_sync.count = 0 ;
			memset(raop_sync.errors, 0, sizeof(raop_sync.errors));
			raop_sync.enabled = !strcasestr(output.device, "BT");
			output.next_sample_rate = output.current_sample_rate = RAOP_SAMPLE_RATE;
			break;
		case RAOP_STOP:
		case RAOP_FLUSH:
			if (event == RAOP_FLUSH) { LOG_INFO("Flush", NULL); }
			else { LOG_INFO("Stop", NULL); }
			raop_state = event;
			_buf_flush(outputbuf);		
			if (output.state > OUTPUT_STOPPED) output.state = OUTPUT_STOPPED;
			output.frames_played = 0;
			output.stop_time = gettime_ms();
			break;
		case RAOP_PLAY: {
			LOG_INFO("Play", NULL);
			if (raop_state != RAOP_PLAY) {
				output.state = OUTPUT_START_AT;
				output.start_at = va_arg(args, u32_t);
				raop_sync.start_time = output.start_at;
				LOG_INFO("Starting at %u (in %d ms)", output.start_at, output.start_at - gettime_ms());
			}
			raop_state = event;
			break;
		}
		case RAOP_VOLUME: {
			float volume = va_arg(args, double);
			LOG_INFO("Volume[0..1] %0.4f", volume);
			volume = 65536 * powf(volume, 3);
			set_volume((u16_t) volume, (u16_t) volume);
			break;
		}
		default:
			break;
	}
	
	if (event != RAOP_VOLUME) UNLOCK_O;
	
	UNLOCK_D;
	return true;
}

/****************************************************************************************
 * We provide the generic codec register option
 */
void register_external(void) {
	char *p;

	if ((p = config_alloc_get(NVS_TYPE_STR, "enable_bt_sink")) != NULL) {
		enable_bt_sink = strcmp(p,"1") == 0 || strcasecmp(p,"y") == 0;
		free(p);
	}

	if ((p = config_alloc_get(NVS_TYPE_STR, "enable_airplay")) != NULL) {
		enable_airplay = strcmp(p,"1") == 0 || strcasecmp(p,"y") == 0;
		free(p);
	}

	if (!strcasestr(output.device, "BT ") ) {
		if(enable_bt_sink){
			bt_sink_init(bt_sink_cmd_handler, sink_data_handler);
			LOG_INFO("Initializing BT sink");
		}
	} else {
		LOG_WARN("Cannot be a BT sink and source");
	}	

	if (enable_airplay){
		raop_sink_init(raop_sink_cmd_handler, raop_sink_data_handler);
		LOG_INFO("Initializing AirPlay sink");
	}
}

void deregister_external(void) {
	if (!strcasestr(output.device, "BT ") && enable_bt_sink) {
		bt_sink_deinit();
		LOG_INFO("Stopping BT sink");
	}
	if (enable_airplay){
		raop_sink_deinit();
		LOG_INFO("Stopping AirPlay sink");
	}
}

void decode_restore(int external) {
	switch (external) {
	case DECODE_BT:
		bt_disconnect();
		break;
	case DECODE_RAOP:
		raop_disconnect();
		raop_state = RAOP_STOP;
		break;
	}
}
