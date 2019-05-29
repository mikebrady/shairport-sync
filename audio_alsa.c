/*
 * libalsa output driver. This file is part of Shairport.
 * Copyright (c) Muffinman, Skaman 2013
 * Copyright (c) Mike Brady 2014 -- 2019
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#define ALSA_PCM_NEW_HW_PARAMS_API

#include <alsa/asoundlib.h>
#include <inttypes.h>
#include <math.h>
#include <memory.h>
#include <pthread.h>
#include <stdio.h>
#include <unistd.h>

#include "config.h"

#include "common.h"
#include "activity_monitor.h"
#include "audio.h"

enum alsa_backend_mode {
  abm_disconnected,
  abm_connected,
  abm_playing
} alsa_backend_state; // under the control of alsa_mutex

typedef struct {
	snd_pcm_format_t alsa_code;
	int frame_size;
} format_record;

static void help(void);
static int init(int argc, char **argv);
static void deinit(void);
static void start(int i_sample_rate, int i_sample_format);
static int play(void *buf, int samples);
static void stop(void);
static void flush(void);
int delay(long *the_delay);
int get_rate_information(uint64_t *elapsed_time, uint64_t *frames_played);
void *alsa_buffer_monitor_thread_code(void *arg);

static void volume(double vol);
void do_volume(double vol);
int prepare(void);

static void parameters(audio_parameters *info);
int mute(int do_mute); // returns true if it actually is allowed to use the mute
static double set_volume;
static int output_method_signalled = 0; // for reporting whether it's using mmap or not
int delay_type_notified = -1; // for controlling the reporting of whether the output device can do precison delays (e.g. alsa->pulsaudio virtual devices can't)
int use_monotonic_clock = 0; // this value will be set when the hardware is initialised

audio_output audio_alsa = {
    .name = "alsa",
    .help = &help,
    .init = &init,
    .deinit = &deinit,
    .prepare = &prepare,
    .start = &start,
    .stop = &stop,
    .is_running = NULL,
    .flush = &flush,
    .delay = &delay,
    .play = &play,
    .rate_info = &get_rate_information,
    .mute = NULL,        // a function will be provided if it can, and is allowed to,
                         // do hardware mute
    .volume = NULL,      // a function will be provided if it can do hardware volume
    .parameters = NULL}; // a function will be provided if it can do hardware volume

static pthread_mutex_t alsa_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t alsa_mixer_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_t alsa_buffer_monitor_thread;

// for deciding when to activate mute
// there are two sources of requests to mute -- the backend itself, e.g. when it
// is flushing
// and the player, e.g. when volume goes down to -144, i.e. mute.

// we may not be allowed to use hardware mute, so we must reflect that too.

int mute_requested_externally = 0;
int mute_requested_internally = 0;

// for tracking how long the output device has stalled
uint64_t stall_monitor_start_time;      // zero if not initialised / not started /
                                        // zeroed by flush
long stall_monitor_frame_count;         // set to delay at start of time, incremented by
                                        // any writes
uint64_t stall_monitor_error_threshold; // if the time is longer than this, it's
                                        // an error

static snd_output_t *output = NULL;
int frame_size; // in bytes for interleaved stereo

int alsa_device_initialised; // boolean to ensure the initialisation is only
                             // done once
         
snd_pcm_t *alsa_handle = NULL;
static snd_pcm_hw_params_t *alsa_params = NULL;
static snd_pcm_sw_params_t *alsa_swparams = NULL;
static snd_ctl_t *ctl = NULL;
static snd_ctl_elem_id_t *elem_id = NULL;
static snd_mixer_t *alsa_mix_handle = NULL;
static snd_mixer_elem_t *alsa_mix_elem = NULL;
static snd_mixer_selem_id_t *alsa_mix_sid = NULL;
static long alsa_mix_minv, alsa_mix_maxv;
static long alsa_mix_mindb, alsa_mix_maxdb;

static char *alsa_out_dev = "default";
static char *alsa_mix_dev = NULL;
static char *alsa_mix_ctrl = "Master";
static int alsa_mix_index = 0;
static int hardware_mixer = 0;
static int has_softvol = 0;

int64_t dither_random_number_store = 0;

static int volume_set_request = 0; // set when an external request is made to set the volume.

int mixer_volume_setting_gives_mute = 0; // set when it is discovered that
                                         // particular mixer volume setting
                                         // causes a mute.
long alsa_mix_mute;                      // setting the volume to this value mutes output, if
                                         // mixer_volume_setting_gives_mute is true
int volume_based_mute_is_active =
    0; // set when muting is being done by a setting the volume to a magic value

// use this to allow the use of snd_pcm_writei or snd_pcm_mmap_writei
snd_pcm_sframes_t (*alsa_pcm_write)(snd_pcm_t *, const void *,
                                           snd_pcm_uframes_t) = snd_pcm_writei;


int precision_delay_and_status(snd_pcm_state_t *state, snd_pcm_sframes_t *delay, enum yndk_type *using_update_timestamps);
int standard_delay_and_status(snd_pcm_state_t *state, snd_pcm_sframes_t *delay, enum yndk_type *using_update_timestamps);

// use this to allow the use of standard or precision delay calculations, with standard the, uh, standard.
int (*delay_and_status)(snd_pcm_state_t *state, snd_pcm_sframes_t *delay, enum yndk_type *using_update_timestamps) = standard_delay_and_status;

int precision_delay_available() {
  // this is very crude -- if the device is a hardware device, then it's assumed the delay is precise
  const char *output_device_name = snd_pcm_name(alsa_handle);
  return (strstr(output_device_name,"hw:") == output_device_name);
}

// static int play_number;
// static int64_t accumulated_delay, accumulated_da_delay;
int alsa_characteristics_already_listed = 0;

static snd_pcm_uframes_t period_size_requested, buffer_size_requested;
static int set_period_size_request, set_buffer_size_request;

static uint64_t measurement_start_time;
static uint64_t frames_played_at_measurement_start_time;

static uint64_t measurement_time;
static uint64_t frames_played_at_measurement_time;

volatile uint64_t most_recent_write_time;

static uint64_t frames_sent_for_playing;
static uint64_t frame_index;
static int measurement_data_is_valid;

static void help(void) {
  printf("    -d output-device    set the output device, default is \"default\".\n"
         "    -c mixer-control    set the mixer control name, default is to use no mixer.\n"
         "    -m mixer-device     set the mixer device, default is the output device.\n"
         "    -i mixer-index      set the mixer index, default is 0.\n");
  system("if [ -d /proc/asound ] ; then echo \"    hardware output devices:\" ; ls -al /proc/asound/ 2>/dev/null | grep '\\->' | tr -s ' ' | cut -d ' ' -f 9 | while read line; do echo \"      \\\"hw:$line\\\"\" ; done ; fi");
}

void set_alsa_out_dev(char *dev) { alsa_out_dev = dev; }

// assuming pthread cancellation is disabled
int open_mixer() {
  int response = 0;
  if (hardware_mixer) {
    debug(3, "Open Mixer");
    int ret = 0;
    snd_mixer_selem_id_alloca(&alsa_mix_sid);
    snd_mixer_selem_id_set_index(alsa_mix_sid, alsa_mix_index);
    snd_mixer_selem_id_set_name(alsa_mix_sid, alsa_mix_ctrl);

    if ((snd_mixer_open(&alsa_mix_handle, 0)) < 0) {
      debug(1, "Failed to open mixer");
      response = -1;
    } else {
      debug(3, "Mixer device name is \"%s\".", alsa_mix_dev);
      if ((snd_mixer_attach(alsa_mix_handle, alsa_mix_dev)) < 0) {
        debug(1, "Failed to attach mixer");
        response = -2;
      } else {
        if ((snd_mixer_selem_register(alsa_mix_handle, NULL, NULL)) < 0) {
          debug(1, "Failed to register mixer element");
          response = -3;
        } else {
          ret = snd_mixer_load(alsa_mix_handle);
          if (ret < 0) {
            debug(1, "Failed to load mixer element");
            response = -4;
          } else {
            debug(3, "Mixer Control name is \"%s\".", alsa_mix_ctrl);
            alsa_mix_elem = snd_mixer_find_selem(alsa_mix_handle, alsa_mix_sid);
            if (!alsa_mix_elem) {
              warn("failed to find mixer control \"%s\".", alsa_mix_ctrl);
              response = -5;
            } else {
              response = 1; // we found a hardware mixer and successfully opened it
            }
          }
        }
      }
    }
  }
  return response;
}

// assuming pthread cancellation is disabled
void close_mixer() {
  if (alsa_mix_handle) {
    snd_mixer_close(alsa_mix_handle);
    alsa_mix_handle = NULL;
  }
}

// assuming pthread cancellation is disabled
void do_snd_mixer_selem_set_playback_dB_all(snd_mixer_elem_t *mix_elem, double vol) {
  if (snd_mixer_selem_set_playback_dB_all(mix_elem, vol, 0) != 0) {
    debug(1, "Can't set playback volume accurately to %f dB.", vol);
    if (snd_mixer_selem_set_playback_dB_all(mix_elem, vol, -1) != 0)
      if (snd_mixer_selem_set_playback_dB_all(mix_elem, vol, 1) != 0)
        debug(1, "Could not set playback dB volume on the mixer.");
  }
}

void actual_close_alsa_device() {
  debug(1, "actual close");
  if (alsa_handle) {
    int derr;
    if ((derr = snd_pcm_hw_free(alsa_handle)))
      debug(1, "Error %d (\"%s\") freeing the output device hardware while "
               "closing it.",
            derr, snd_strerror(derr));

    if ((derr = snd_pcm_close(alsa_handle)))
      debug(1, "Error %d (\"%s\") closing the output device.", derr, snd_strerror(derr));
    alsa_handle = NULL;
  }
}

// This array is a sequence of the output rates to be tried if automatic speed selection is requested.
// There is no benefit to upconverting the frame rate, other than for compatibility.
// The lowest rate that the DAC is capable of is chosen.

unsigned int auto_speed_output_rates[] = {
	44100,
	88200,
	176400,
	352800,
};

// This array is of all the formats known to Shairport Sync, in order of the SPS_FORMAT definitions, with their equivalent alsa codes and their frame sizes.
// If just one format is requested, then its entry is searched for in the array and checked on the device
// If auto format is requested, then each entry in turn is tried until a working format is found.
// So, it should be in the search order.
  
  format_record fr[] = {
    {SND_PCM_FORMAT_UNKNOWN,0}, // unknown
    {SND_PCM_FORMAT_S8,2},
    {SND_PCM_FORMAT_U8,2},
    {SND_PCM_FORMAT_S16,4},
    {SND_PCM_FORMAT_S16_LE,4},
    {SND_PCM_FORMAT_S16_BE,4},
    {SND_PCM_FORMAT_S24,4},
    {SND_PCM_FORMAT_S24_LE,8},
    {SND_PCM_FORMAT_S24_BE,8},
    {SND_PCM_FORMAT_S24_3LE,6},
    {SND_PCM_FORMAT_S24_3BE,6},
    {SND_PCM_FORMAT_S32,8},
    {SND_PCM_FORMAT_S32_LE,8},
    {SND_PCM_FORMAT_S32_BE,8},
    {SND_PCM_FORMAT_UNKNOWN,0}, // auto
    {SND_PCM_FORMAT_UNKNOWN,0}, // illegal
  };
  
  // This array is the sequence of formats to be tried if automatic selection of the format is requested.
  // Ideally, audio should pass through Shairport Sync unaltered, apart from occasional interpolation.
  // If the user chooses a hardware mixer, then audio could go straight through, unaltered, as signed 16 bit stereo.
  // However, the user might, at any point, select an option that requires modification, such as stereo to mono mixing,
  // additional volume attenuation, convolution, and so on. For this reason,
  // we look for the greatest depth the DAC is capable of, since upconverting it is completely lossless.
  // If audio processing is required, then the dither that must be added will
  // be added at the lowest possible level.
  // Hence, selecting the greatest bit depth is always either beneficial or neutral.
  
  enum sps_format_t auto_format_check_sequence[] = {
    SPS_FORMAT_S32,
    SPS_FORMAT_S32_LE,
    SPS_FORMAT_S32_BE,
    SPS_FORMAT_S24,
    SPS_FORMAT_S24_LE,
    SPS_FORMAT_S24_BE,  
    SPS_FORMAT_S24_3LE,
    SPS_FORMAT_S24_3BE,
    SPS_FORMAT_S16,
    SPS_FORMAT_S16_LE,
    SPS_FORMAT_S16_BE,
    SPS_FORMAT_S8,
    SPS_FORMAT_U8,
  };

// assuming pthread cancellation is disabled
// if do_auto_setting is true and auto format or auto speed has been requested,
// select the settings as appropriate and store them
int actual_open_alsa_device(int do_auto_setup) {
  // the alsa mutex is already acquired when this is called
  const snd_pcm_uframes_t minimal_buffer_headroom =
      352 * 2; // we accept this much headroom in the hardware buffer, but we'll
               // accept less
               /*
                 const snd_pcm_uframes_t requested_buffer_headroom =
                     minimal_buffer_headroom + 2048; // we ask for this much headroom in the
                                                     // hardware buffer, but we'll accept
                 less
               */

  int ret, dir = 0;
  unsigned int actual_sample_rate; // this will be given the rate requested and will be given the actual rate
  // snd_pcm_uframes_t frames = 441 * 10;
  snd_pcm_uframes_t actual_buffer_length;
  snd_pcm_access_t access;

  // ensure no calls are made to the alsa device enquiring about the buffer
  // length if
  // synchronisation is disabled.
  if (config.no_sync != 0)
    audio_alsa.delay = NULL;

  // ensure no calls are made to the alsa device enquiring about the buffer
  // length if
  // synchronisation is disabled.
  if (config.no_sync != 0)
    audio_alsa.delay = NULL;

  ret = snd_pcm_open(&alsa_handle, alsa_out_dev, SND_PCM_STREAM_PLAYBACK, 0);
  if (ret < 0) {
    if (ret == -ENOENT) {
      warn("the alsa output_device \"%s\" can not be found.", alsa_out_dev);
    } else {
      char errorstring[1024];
      strerror_r(-ret, (char *)errorstring, sizeof(errorstring));
      warn("alsa: error %d (\"%s\") opening alsa device \"%s\".", ret, (char *)errorstring, alsa_out_dev);
    }
    return ret;
  }

  snd_pcm_hw_params_alloca(&alsa_params);
  snd_pcm_sw_params_alloca(&alsa_swparams);

  ret = snd_pcm_hw_params_any(alsa_handle, alsa_params);
  if (ret < 0) {
    die("audio_alsa: Broken configuration for device \"%s\": no configurations "
         "available",
         alsa_out_dev);
    return ret;
  }

  if ((config.no_mmap == 0) &&
      (snd_pcm_hw_params_set_access(alsa_handle, alsa_params, SND_PCM_ACCESS_MMAP_INTERLEAVED) >=
       0)) {
    if (output_method_signalled == 0) {
      debug(3, "Output written using MMAP");
      output_method_signalled = 1;
    }
    access = SND_PCM_ACCESS_MMAP_INTERLEAVED;
    alsa_pcm_write = snd_pcm_mmap_writei;
  } else {
    if (output_method_signalled == 0) {
      debug(3, "Output written with RW");
      output_method_signalled = 1;
    }
    access = SND_PCM_ACCESS_RW_INTERLEAVED;
    alsa_pcm_write = snd_pcm_writei;
  }

  ret = snd_pcm_hw_params_set_access(alsa_handle, alsa_params, access);
  if (ret < 0) {
    warn("audio_alsa: Access type not available for device \"%s\": %s", alsa_out_dev,
         snd_strerror(ret));
    return ret;
  }
  

  ret = snd_pcm_hw_params_set_channels(alsa_handle, alsa_params, 2);
  if (ret < 0) {
    warn("audio_alsa: Channels count (2) not available for device \"%s\": %s", alsa_out_dev,
         snd_strerror(ret));
    return ret;
  }

  snd_pcm_format_t sf;
 
  if ((do_auto_setup == 0) || (config.output_format_auto_requested == 0)) { // no auto format
  	if ((config.output_format > SPS_FORMAT_UNKNOWN) && (config.output_format < SPS_FORMAT_AUTO)) {
  		sf = fr[config.output_format].alsa_code;
  		frame_size = fr[config.output_format].frame_size;
  	} else {
  		warn("alsa: unexpected output format %d. Set to S16_LE.",config.output_format);
  		config.output_format = SPS_FORMAT_S16_LE;
  		sf = fr[config.output_format].alsa_code;
  		frame_size = fr[config.output_format].frame_size;
  	} 
		ret = snd_pcm_hw_params_set_format(alsa_handle, alsa_params, sf);
		if (ret < 0) {
			warn("audio_alsa: Alsa sample format %d not available for device \"%s\": %s", sf,
					 alsa_out_dev, snd_strerror(ret));
			return ret;
		}
  } else { // auto format
  	int number_of_formats_to_try;
  	enum sps_format_t *formats;
  		formats = auto_format_check_sequence;
  		number_of_formats_to_try = sizeof(auto_format_check_sequence)/sizeof(sps_format_t);  	
  	int i = 0;
  	int format_found = 0;
  	enum sps_format_t trial_format = SPS_FORMAT_UNKNOWN;
  	while ((i < number_of_formats_to_try) && (format_found == 0)) {
  		trial_format = formats[i];
  		sf = fr[trial_format].alsa_code;
  		frame_size = fr[trial_format].frame_size;
  		ret = snd_pcm_hw_params_set_format(alsa_handle, alsa_params, sf);
  		if (ret == 0)
  			format_found = 1;
  		else
  			i++;
  	}
		if (ret == 0) {
			config.output_format = trial_format;
			debug(1,"alsa: output format chosen is \"%s\".",sps_format_description_string(config.output_format));
		} else {
			warn("audio_alsa: Could not automatically set the output format for device \"%s\": %s",
					 alsa_out_dev, snd_strerror(ret));
			return ret;
		} 	  
  }
  
  if ((do_auto_setup == 0) || (config.output_rate_auto_requested == 0)) { // no auto format
    actual_sample_rate = config.output_rate; // this is the requested rate -- it'll be changed to the actual rate
		ret = snd_pcm_hw_params_set_rate_near(alsa_handle, alsa_params, &actual_sample_rate, &dir);
		if (ret < 0) {
			warn("audio_alsa: Rate %iHz not available for playback: %s", config.output_rate,
					 snd_strerror(ret));
			return ret;
		}
	} else {
		int number_of_speeds_to_try;
		unsigned int *speeds;
		
		speeds = auto_speed_output_rates;
		number_of_speeds_to_try = sizeof(auto_speed_output_rates)/sizeof(int);
		
		int i = 0;
		int speed_found = 0;
		
		while ((i < number_of_speeds_to_try) && (speed_found == 0)) {
  		actual_sample_rate = speeds[i];
  		ret = snd_pcm_hw_params_set_rate_near(alsa_handle, alsa_params, &actual_sample_rate, &dir);
  		if (ret == 0) {
  			speed_found = 1;
  			if (actual_sample_rate != speeds[i])
  				warn("Speed requested: %d. Speed available: %d.",speeds[i],actual_sample_rate);
  		} else {
  			i++;
  		}
  	}
		if (ret == 0) {
			config.output_rate = actual_sample_rate;
			debug(1,"alsa: output speed chosen is %d.",config.output_rate);
		} else {
			warn("audio_alsa: Could not automatically set the output rate for device \"%s\": %s",
					 alsa_out_dev, snd_strerror(ret));
			return ret;
		}	
	}
  
  if (set_period_size_request != 0) {
    debug(1, "Attempting to set the period size");
    ret = snd_pcm_hw_params_set_period_size_near(alsa_handle, alsa_params, &period_size_requested,
                                                 &dir);
    if (ret < 0) {
      warn("audio_alsa: cannot set period size of %lu: %s", period_size_requested,
           snd_strerror(ret));
      return ret;
    } else {
      snd_pcm_uframes_t actual_period_size;
      snd_pcm_hw_params_get_period_size(alsa_params, &actual_period_size, &dir);
      if (actual_period_size != period_size_requested)
        inform("Actual period size set to a different value than requested. "
               "Requested: %lu, actual "
               "setting: %lu",
               period_size_requested, actual_period_size);
    }
  }

  if (set_buffer_size_request != 0) {
    debug(1, "Attempting to set the buffer size to %lu", buffer_size_requested);
    ret = snd_pcm_hw_params_set_buffer_size_near(alsa_handle, alsa_params, &buffer_size_requested);
    if (ret < 0) {
      warn("audio_alsa: cannot set buffer size of %lu: %s", buffer_size_requested,
           snd_strerror(ret));
      return ret;
    } else {
      snd_pcm_uframes_t actual_buffer_size;
      snd_pcm_hw_params_get_buffer_size(alsa_params, &actual_buffer_size);
      if (actual_buffer_size != buffer_size_requested)
        inform("Actual period size set to a different value than requested. "
               "Requested: %lu, actual "
               "setting: %lu",
               buffer_size_requested, actual_buffer_size);
    }
  }

  ret = snd_pcm_hw_params(alsa_handle, alsa_params);
  if (ret < 0) {
    warn("audio_alsa: Unable to set hw parameters for device \"%s\": %s.", alsa_out_dev,
         snd_strerror(ret));
    return ret;
  }

  // check parameters after attempting to set them

  if (set_period_size_request != 0) {
    snd_pcm_uframes_t actual_period_size;
    snd_pcm_hw_params_get_period_size(alsa_params, &actual_period_size, &dir);
    if (actual_period_size != period_size_requested)
      inform("Actual period size set to a different value than requested. "
             "Requested: %lu, actual "
             "setting: %lu",
             period_size_requested, actual_period_size);
  }

  if (set_buffer_size_request != 0) {
    snd_pcm_uframes_t actual_buffer_size;
    snd_pcm_hw_params_get_buffer_size(alsa_params, &actual_buffer_size);
    if (actual_buffer_size != buffer_size_requested)
      inform("Actual period size set to a different value than requested. "
             "Requested: %lu, actual "
             "setting: %lu",
             buffer_size_requested, actual_buffer_size);
  }

  if (actual_sample_rate != config.output_rate) {
    warn("Can't set the D/A converter to sample rate %d.", config.output_rate);
    return -EINVAL;
  }
  
  use_monotonic_clock =  snd_pcm_hw_params_is_monotonic(alsa_params);

  ret = snd_pcm_hw_params_get_buffer_size(alsa_params, &actual_buffer_length);
  if (ret < 0) {
    warn("audio_alsa: Unable to get hw buffer length for device \"%s\": %s.", alsa_out_dev,
         snd_strerror(ret));
    return ret;
  }

  ret = snd_pcm_sw_params_current(alsa_handle, alsa_swparams);
  if (ret < 0) {
    warn("audio_alsa: Unable to get current sw parameters for device \"%s\": "
         "%s.",
         alsa_out_dev, snd_strerror(ret));
    return ret;
  }

  ret = snd_pcm_sw_params_set_tstamp_mode(alsa_handle, alsa_swparams, SND_PCM_TSTAMP_ENABLE);
  if (ret < 0) {
    warn("audio_alsa: Can't enable timestamp mode of device: \"%s\": %s.", alsa_out_dev,
         snd_strerror(ret));
    return ret;
  }

  /* write the sw parameters */
  ret = snd_pcm_sw_params(alsa_handle, alsa_swparams);
  if (ret < 0) {
    warn("audio_alsa: Unable to set software parameters of device: \"%s\": %s.", alsa_out_dev,
         snd_strerror(ret));
    return ret;
  }
  
  ret = snd_pcm_prepare(alsa_handle);
  if (ret < 0) {
    warn("audio_alsa: Unable to prepare the device: \"%s\": %s.", alsa_out_dev,
         snd_strerror(ret));
    return ret;
  }

  if (actual_buffer_length < config.audio_backend_buffer_desired_length + minimal_buffer_headroom) {
    /*
    // the dac buffer is too small, so let's try to set it
    buffer_size =
        config.audio_backend_buffer_desired_length + requested_buffer_headroom;
    ret = snd_pcm_hw_params_set_buffer_size_near(alsa_handle, alsa_params,
                                                 &buffer_size);
    if (ret < 0)
      die("audio_alsa: Unable to set hw buffer size to %lu for device \"%s\": "
          "%s.",
          config.audio_backend_buffer_desired_length +
              requested_buffer_headroom,
          alsa_out_dev, snd_strerror(ret));
    if (config.audio_backend_buffer_desired_length + minimal_buffer_headroom >
        buffer_size) {
      die("audio_alsa: Can't set hw buffer size to %lu or more for device "
          "\"%s\". Requested size: %lu, granted size: %lu.",
          config.audio_backend_buffer_desired_length + minimal_buffer_headroom,
          alsa_out_dev, config.audio_backend_buffer_desired_length +
                            requested_buffer_headroom,
          buffer_size);
    }
    */
    debug(1, "The alsa buffer is smaller (%lu bytes) than the desired backend "
             "buffer "
             "length (%ld) you have chosen.",
          actual_buffer_length, config.audio_backend_buffer_desired_length);
  }
  
  
  if (config.use_precision_timing == YNA_YES) 
    delay_and_status = precision_delay_and_status;
  else if (config.use_precision_timing == YNA_AUTO) {
    if (precision_delay_available()) {
      delay_and_status = precision_delay_and_status;
      debug(2,"alsa: precision timing selected for \"auto\" mode");
    }
  }

  if (alsa_characteristics_already_listed == 0) {
    alsa_characteristics_already_listed = 1;
    int log_level = 2; // the level at which debug information should be output
                       //    int rc;
    snd_pcm_access_t access_type;
    snd_pcm_format_t format_type;
    snd_pcm_subformat_t subformat_type;
    //    unsigned int val, val2;
    unsigned int uval, uval2;
    int sval;
    int dir;
    snd_pcm_uframes_t frames;

    debug(log_level, "PCM handle name = '%s'", snd_pcm_name(alsa_handle));

    //      ret = snd_pcm_hw_params_any(alsa_handle, alsa_params);
    //      if (ret < 0) {
    //        die("audio_alsa: Cannpot get configuration for
    // device
    //\"%s\":
    // no
    // configurations
    //"
    //            "available",
    //            alsa_out_dev);
    //      }

    debug(log_level, "alsa device parameters:");

    snd_pcm_hw_params_get_access(alsa_params, &access_type);
    debug(log_level, "  access type = %s", snd_pcm_access_name(access_type));

    snd_pcm_hw_params_get_format(alsa_params, &format_type);
    debug(log_level, "  format = '%s' (%s)", snd_pcm_format_name(format_type),
          snd_pcm_format_description(format_type));

    snd_pcm_hw_params_get_subformat(alsa_params, &subformat_type);
    debug(log_level, "  subformat = '%s' (%s)", snd_pcm_subformat_name(subformat_type),
          snd_pcm_subformat_description(subformat_type));

    snd_pcm_hw_params_get_channels(alsa_params, &uval);
    debug(log_level, "  number of channels = %u", uval);

    sval = snd_pcm_hw_params_get_sbits(alsa_params);
    debug(log_level, "  number of significant bits = %d", sval);

    snd_pcm_hw_params_get_rate(alsa_params, &uval, &dir);
    switch (dir) {
    case -1:
      debug(log_level, "  rate = %u frames per second (<).", uval);
      break;
    case 0:
      debug(log_level, "  rate = %u frames per second (precisely).", uval);
      break;
    case 1:
      debug(log_level, "  rate = %u frames per second (>).", uval);
      break;
    }

    if (snd_pcm_hw_params_get_rate_numden(alsa_params, &uval, &uval2) == 0)
      debug(log_level, "  precise (rational) rate = %.3f frames per second (i.e. %u/%u).", uval,
            uval2, ((double)uval) / uval2);
    else
      debug(log_level, "  precise (rational) rate information unavailable.");

    snd_pcm_hw_params_get_period_time(alsa_params, &uval, &dir);
    switch (dir) {
    case -1:
      debug(log_level, "  period_time = %u us (<).", uval);
      break;
    case 0:
      debug(log_level, "  period_time = %u us (precisely).", uval);
      break;
    case 1:
      debug(log_level, "  period_time = %u us (>).", uval);
      break;
    }

    snd_pcm_hw_params_get_period_size(alsa_params, &frames, &dir);
    switch (dir) {
    case -1:
      debug(log_level, "  period_size = %lu frames (<).", frames);
      break;
    case 0:
      debug(log_level, "  period_size = %lu frames (precisely).", frames);
      break;
    case 1:
      debug(log_level, "  period_size = %lu frames (>).", frames);
      break;
    }

    snd_pcm_hw_params_get_buffer_time(alsa_params, &uval, &dir);
    switch (dir) {
    case -1:
      debug(log_level, "  buffer_time = %u us (<).", uval);
      break;
    case 0:
      debug(log_level, "  buffer_time = %u us (precisely).", uval);
      break;
    case 1:
      debug(log_level, "  buffer_time = %u us (>).", uval);
      break;
    }

    snd_pcm_hw_params_get_buffer_size(alsa_params, &frames);
    switch (dir) {
    case -1:
      debug(log_level, "  buffer_size = %lu frames (<).", frames);
      break;
    case 0:
      debug(log_level, "  buffer_size = %lu frames (precisely).", frames);
      break;
    case 1:
      debug(log_level, "  buffer_size = %lu frames (>).", frames);
      break;
    }

    snd_pcm_hw_params_get_periods(alsa_params, &uval, &dir);
    switch (dir) {
    case -1:
      debug(log_level, "  periods_per_buffer = %u (<).", uval);
      break;
    case 0:
      debug(log_level, "  periods_per_buffer = %u (precisely).", uval);
      break;
    case 1:
      debug(log_level, "  periods_per_buffer = %u (>).", uval);
      break;
    }
  }
  return 0;
}

int open_alsa_device(int do_auto_setup) {
  int result;
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  result = actual_open_alsa_device(do_auto_setup);
  pthread_setcancelstate(oldState, NULL);
  return result;
}

int do_alsa_device_init_if_needed() {
  int response = 0;
  // do any alsa device initialisation (general case) if needed
  // at present, this is only needed if a hardware mixer is being used
  // if there's a hardware mixer, it needs to be initialised before first use
  if (alsa_device_initialised == 0) {
    alsa_device_initialised = 1;
    if (hardware_mixer) {
      debug(2, "alsa: hardware mixer init");
      int oldState;
      pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable

      if (alsa_mix_dev == NULL)
        alsa_mix_dev = alsa_out_dev;

      // Now, start trying to initialise the alsa device with the settings
      // obtained
      pthread_cleanup_debug_mutex_lock(&alsa_mixer_mutex, 1000, 1);
      if (open_mixer() == 1) {
        if (snd_mixer_selem_get_playback_volume_range(alsa_mix_elem, &alsa_mix_minv,
                                                      &alsa_mix_maxv) < 0)
          debug(1, "Can't read mixer's [linear] min and max volumes.");
        else {
          if (snd_mixer_selem_get_playback_dB_range(alsa_mix_elem, &alsa_mix_mindb,
                                                    &alsa_mix_maxdb) == 0) {

            audio_alsa.volume = &volume;         // insert the volume function now we
                                                 // know it can do dB stuff
            audio_alsa.parameters = &parameters; // likewise the parameters stuff
            if (alsa_mix_mindb == SND_CTL_TLV_DB_GAIN_MUTE) {
              // For instance, the Raspberry Pi does this
              debug(1, "Lowest dB value is a mute");
              mixer_volume_setting_gives_mute = 1;
              alsa_mix_mute = SND_CTL_TLV_DB_GAIN_MUTE; // this may not be
                                                        // necessary -- it's
                                                        // always
              // going to be SND_CTL_TLV_DB_GAIN_MUTE, right?
              // debug(1, "Try minimum volume + 1 as lowest true attenuation
              // value");
              if (snd_mixer_selem_ask_playback_vol_dB(alsa_mix_elem, alsa_mix_minv + 1,
                                                      &alsa_mix_mindb) != 0)
                debug(1, "Can't get dB value corresponding to a minimum volume "
                         "+ 1.");
            }
            debug(3, "Hardware mixer has dB volume from %f to %f.", (1.0 * alsa_mix_mindb) / 100.0,
                  (1.0 * alsa_mix_maxdb) / 100.0);
          } else {
            // use the linear scale and do the db conversion ourselves
            warn("The hardware mixer specified -- \"%s\" -- does not have "
                 "a dB volume scale.",
                 alsa_mix_ctrl);

            if (snd_ctl_open(&ctl, alsa_mix_dev, 0) < 0) {
              warn("Cannot open control \"%s\"", alsa_mix_dev);
              response = -1;
            }
            if (snd_ctl_elem_id_malloc(&elem_id) < 0) {
              debug(1, "Cannot allocate memory for control \"%s\"", alsa_mix_dev);
              elem_id = NULL;
              response = -2;
            } else {
              snd_ctl_elem_id_set_interface(elem_id, SND_CTL_ELEM_IFACE_MIXER);
              snd_ctl_elem_id_set_name(elem_id, alsa_mix_ctrl);

              if (snd_ctl_get_dB_range(ctl, elem_id, &alsa_mix_mindb, &alsa_mix_maxdb) == 0) {
                debug(1, "alsa: hardware mixer \"%s\" selected, with dB volume "
                         "from %f to %f.",
                      alsa_mix_ctrl, (1.0 * alsa_mix_mindb) / 100.0,
                      (1.0 * alsa_mix_maxdb) / 100.0);
                has_softvol = 1;
                audio_alsa.volume = &volume;         // insert the volume function now
                                                     // we know it can do dB stuff
                audio_alsa.parameters = &parameters; // likewise the parameters stuff
              } else {
                debug(1, "Cannot get the dB range from the volume control \"%s\"", alsa_mix_ctrl);
              }
            }
            /*
            debug(1, "Min and max volumes are %d and
            %d.",alsa_mix_minv,alsa_mix_maxv);
            alsa_mix_maxdb = 0;
            if ((alsa_mix_maxv!=0) && (alsa_mix_minv!=0))
              alsa_mix_mindb =
            -20*100*(log10(alsa_mix_maxv*1.0)-log10(alsa_mix_minv*1.0));
            else if (alsa_mix_maxv!=0)
              alsa_mix_mindb = -20*100*log10(alsa_mix_maxv*1.0);
            audio_alsa.volume = &linear_volume; // insert the linear volume
            function
            audio_alsa.parameters = &parameters; // likewise the parameters
            stuff
            debug(1,"Max and min dB calculated are %d and
            %d.",alsa_mix_maxdb,alsa_mix_mindb);
            */
          }
        }
        if (((config.alsa_use_hardware_mute == 1) &&
             (snd_mixer_selem_has_playback_switch(alsa_mix_elem))) ||
            mixer_volume_setting_gives_mute) {
          audio_alsa.mute = &mute; // insert the mute function now we know it
                                   // can do muting stuff
          // debug(1, "Has mixer and mute ability we will use.");
        } else {
          // debug(1, "Has mixer but not using hardware mute.");
        }
        close_mixer();
      }
      debug_mutex_unlock(&alsa_mixer_mutex, 3); // release the mutex
      pthread_cleanup_pop(0);
      pthread_setcancelstate(oldState, NULL);
    }
  }
  return response;
}

static int init(int argc, char **argv) {
  // for debugging
  snd_output_stdio_attach(&output, stdout, 0);

  // debug(2,"audio_alsa init called.");
  int response = 0; // this will be what we return to the caller.
  alsa_device_initialised = 0;
  const char *str;
  int value;
  // double dvalue;

  // set up default values first

  alsa_backend_state = abm_disconnected; // startup state
  debug(2, "alsa: init() -- alsa_backend_state => abm_disconnected.");
  set_period_size_request = 0;
  set_buffer_size_request = 0;
  config.alsa_use_hardware_mute = 0; // don't use it by default

  config.audio_backend_latency_offset = 0;
  config.audio_backend_buffer_desired_length = 0.200;
  config.audio_backend_buffer_interpolation_threshold_in_seconds =
      0.120; // below this, basic interpolation will be used to save time.
  config.alsa_maximum_stall_time = 0.200; // 200 milliseconds -- if it takes longer, it's a problem
  config.audio_backend_silence_threshold =
      0.040; // start sending silent frames if the delay goes below this time
  config.audio_backend_silence_scan_interval = 0.004; // check silence threshold this often

  stall_monitor_error_threshold =
      (uint64_t)1000000 * config.alsa_maximum_stall_time; // stall time max to microseconds;
  stall_monitor_error_threshold = (stall_monitor_error_threshold << 32) / 1000000; // now in fp form
  debug(1,
        "stall_monitor_error_threshold is 0x%" PRIx64 ", with alsa_maximum_stall_time of %f sec.",
        stall_monitor_error_threshold, config.alsa_maximum_stall_time);

  stall_monitor_start_time = 0;
  stall_monitor_frame_count = 0;
  
  config.disable_standby_mode = disable_standby_off;
  config.keep_dac_busy = 0;
  config.use_precision_timing = YNA_AUTO;

  // get settings from settings file first, allow them to be overridden by
  // command line options

  // do the "general" audio  options. Note, these options are in the "general"
  // stanza!
  parse_general_audio_options();

  if (config.cfg != NULL) {
    double dvalue;

    /* Get the Output Device Name. */
    if (config_lookup_string(config.cfg, "alsa.output_device", &str)) {
      alsa_out_dev = (char *)str;
    }

    /* Get the Mixer Type setting. */

    if (config_lookup_string(config.cfg, "alsa.mixer_type", &str)) {
      inform("The alsa mixer_type setting is deprecated and has been ignored. "
             "FYI, using the \"mixer_control_name\" setting automatically "
             "chooses a hardware mixer.");
    }

    /* Get the Mixer Device Name. */
    if (config_lookup_string(config.cfg, "alsa.mixer_device", &str)) {
      alsa_mix_dev = (char *)str;
    }

    /* Get the Mixer Control Name. */
    if (config_lookup_string(config.cfg, "alsa.mixer_control_name", &str)) {
      alsa_mix_ctrl = (char *)str;
      hardware_mixer = 1;
    }

    /* Get the disable_synchronization setting. */
    if (config_lookup_string(config.cfg, "alsa.disable_synchronization", &str)) {
      if (strcasecmp(str, "no") == 0)
        config.no_sync = 0;
      else if (strcasecmp(str, "yes") == 0)
        config.no_sync = 1;
      else {
        warn("Invalid disable_synchronization option choice \"%s\". It should "
             "be \"yes\" or "
             "\"no\". It is set to \"no\".");
        config.no_sync = 0;
      }
    }

    /* Get the mute_using_playback_switch setting. */
    if (config_lookup_string(config.cfg, "alsa.mute_using_playback_switch", &str)) {
      inform("The alsa \"mute_using_playback_switch\" setting is deprecated. "
             "Please use the \"use_hardware_mute_if_available\" setting instead.");
      if (strcasecmp(str, "no") == 0)
        config.alsa_use_hardware_mute = 0;
      else if (strcasecmp(str, "yes") == 0)
        config.alsa_use_hardware_mute = 1;
      else {
        warn("Invalid mute_using_playback_switch option choice \"%s\". It "
             "should be \"yes\" or "
             "\"no\". It is set to \"no\".");
        config.alsa_use_hardware_mute = 0;
      }
    }

    /* Get the use_hardware_mute_if_available setting. */
    if (config_lookup_string(config.cfg, "alsa.use_hardware_mute_if_available", &str)) {
      if (strcasecmp(str, "no") == 0)
        config.alsa_use_hardware_mute = 0;
      else if (strcasecmp(str, "yes") == 0)
        config.alsa_use_hardware_mute = 1;
      else {
        warn("Invalid use_hardware_mute_if_available option choice \"%s\". It "
             "should be \"yes\" or "
             "\"no\". It is set to \"no\".");
        config.alsa_use_hardware_mute = 0;
      }
    }
        

    /* Get the output format, using the same names as aplay does*/
    if (config_lookup_string(config.cfg, "alsa.output_format", &str)) {
      if (strcasecmp(str, "auto") == 0)
        config.output_format_auto_requested = 1;
      else {
        config.output_format_auto_requested = 0;
        if (strcasecmp(str, "S16") == 0)
          config.output_format = SPS_FORMAT_S16;
        else if (strcasecmp(str, "S16_LE") == 0)
          config.output_format = SPS_FORMAT_S16_LE;
        else if (strcasecmp(str, "S16_BE") == 0)
          config.output_format = SPS_FORMAT_S16_BE;
        else if (strcasecmp(str, "S24") == 0)
          config.output_format = SPS_FORMAT_S24;
        else if (strcasecmp(str, "S24_LE") == 0)
          config.output_format = SPS_FORMAT_S24_LE;
        else if (strcasecmp(str, "S24_BE") == 0)
          config.output_format = SPS_FORMAT_S24_BE;
        else if (strcasecmp(str, "S24_3LE") == 0)
          config.output_format = SPS_FORMAT_S24_3LE;
        else if (strcasecmp(str, "S24_3BE") == 0)
          config.output_format = SPS_FORMAT_S24_3BE;
        else if (strcasecmp(str, "S32") == 0)
          config.output_format = SPS_FORMAT_S32;
        else if (strcasecmp(str, "S32_LE") == 0)
          config.output_format = SPS_FORMAT_S32_LE;
        else if (strcasecmp(str, "S32_BE") == 0)
          config.output_format = SPS_FORMAT_S32_BE;
        else if (strcasecmp(str, "U8") == 0)
          config.output_format = SPS_FORMAT_U8;
        else if (strcasecmp(str, "S8") == 0)
          config.output_format = SPS_FORMAT_S8;
        else {
          warn("Invalid output format \"%s\". It should be \"U8\", \"S8\", "
               "\"S16\", \"S24\", \"S24_LE\", \"S24_BE\", "
               "\"S24_3LE\", \"S24_3BE\" or "
               "\"S32\", \"S32_LE\", \"S32_BE\". It is set to \"%s\".",
               sps_format_description_string(config.output_format));
        }
      }
    }

    if (config_lookup_string(config.cfg, "alsa.output_rate", &str)) {
			if (strcasecmp(str, "auto") == 0) {
				config.output_rate_auto_requested = 1;
			} else {
				config.output_rate_auto_requested = 0;
				/* Get the output rate, which must be a multiple of 44,100*/
				if (config_lookup_int(config.cfg, "alsa.output_rate", &value)) {
					debug(1, "alsa output rate is %d frames per second", value);
					switch (value) {
					case 44100:
					case 88200:
					case 176400:
					case 352800:
						config.output_rate = value;
						break;
					default:
						warn("Invalid output rate \"%d\". It should be \"auto\" or a multiple of 44,100 up "
								 "to 352,800. It is "
								 "set to %d.",
								 value,config.output_rate);
					}
				}
			}
    }

    /* Get the use_mmap_if_available setting. */
    if (config_lookup_string(config.cfg, "alsa.use_mmap_if_available", &str)) {
      if (strcasecmp(str, "no") == 0)
        config.no_mmap = 1;
      else if (strcasecmp(str, "yes") == 0)
        config.no_mmap = 0;
      else {
        warn("Invalid use_mmap_if_available option choice \"%s\". It should be "
             "\"yes\" or \"no\". "
             "It is set to \"yes\".");
        config.no_mmap = 0;
      }
    }
    /* Get the optional period size value */
    if (config_lookup_int(config.cfg, "alsa.period_size", &value)) {
      set_period_size_request = 1;
      debug(1, "Value read for period size is %d.", value);
      if (value < 0) {
        warn("Invalid alsa period size setting \"%d\". It "
             "must be greater than 0. No setting is made.",
             value);
        set_period_size_request = 0;
      } else {
        period_size_requested = value;
      }
    }

    /* Get the optional buffer size value */
    if (config_lookup_int(config.cfg, "alsa.buffer_size", &value)) {
      set_buffer_size_request = 1;
      debug(1, "Value read for buffer size is %d.", value);
      if (value < 0) {
        warn("Invalid alsa buffer size setting \"%d\". It "
             "must be greater than 0. No setting is made.",
             value);
        set_buffer_size_request = 0;
      } else {
        buffer_size_requested = value;
      }
    }

    /* Get the optional alsa_maximum_stall_time setting. */
    if (config_lookup_float(config.cfg, "alsa.maximum_stall_time", &dvalue)) {
      if (dvalue < 0.0) {
        warn("Invalid alsa maximum write time setting \"%f\". It "
             "must be greater than 0. Default is \"%f\". No setting is made.",
             dvalue, config.alsa_maximum_stall_time);
      } else {
        config.alsa_maximum_stall_time = dvalue;
      }
    }


    /* Get the optional disable_standby_mode setting. */
    if (config_lookup_string(config.cfg, "alsa.disable_standby_mode", &str)) {
      if ((strcasecmp(str, "no") == 0) || (strcasecmp(str, "off") == 0) || (strcasecmp(str, "never") == 0))
        config.disable_standby_mode = disable_standby_off;
      else if ((strcasecmp(str, "yes") == 0) || (strcasecmp(str, "on") == 0) || (strcasecmp(str, "always") == 0)) {
        config.disable_standby_mode = disable_standby_always;
        config.keep_dac_busy = 1;
      } else if (strcasecmp(str, "auto") == 0)
        config.disable_standby_mode = disable_standby_auto;
      else {
        warn("Invalid disable_standby_mode option choice \"%s\". It should be "
             "\"always\", \"auto\" or \"never\". "
             "It is set to \"never\".");
      }
    }

    
    if (config_lookup_string(config.cfg, "alsa.use_precision_timing", &str)) {
      if ((strcasecmp(str, "no") == 0) || (strcasecmp(str, "off") == 0) || (strcasecmp(str, "never") == 0))
        config.use_precision_timing = YNA_NO;
      else if ((strcasecmp(str, "yes") == 0) || (strcasecmp(str, "on") == 0) || (strcasecmp(str, "always") == 0)) {
        config.use_precision_timing = YNA_YES;
        config.keep_dac_busy = 1;
      } else if (strcasecmp(str, "auto") == 0)
        config.use_precision_timing = YNA_AUTO;
      else {
        warn("Invalid use_precision_timing option choice \"%s\". It should be "
             "\"yes\", \"auto\" or \"no\". "
             "It is set to \"auto\".");
      }
    }

    debug(1, "alsa: disable_standby_mode is \"%s\".", config.disable_standby_mode == disable_standby_off ? "never" : config.disable_standby_mode == disable_standby_always ? "always" : "auto");
  }

  optind = 1; // optind=0 is equivalent to optind=1 plus special behaviour
  argv--;     // so we shift the arguments to satisfy getopt()
  argc++;
  // some platforms apparently require optreset = 1; - which?
  int opt;
  while ((opt = getopt(argc, argv, "d:t:m:c:i:")) > 0) {
    switch (opt) {
    case 'd':
      alsa_out_dev = optarg;
      break;

    case 't':
      inform("The alsa backend -t option is deprecated and has been ignored. "
             "FYI, using the -c option automatically chooses a hardware "
             "mixer.");
      break;

    case 'm':
      alsa_mix_dev = optarg;
      break;
    case 'c':
      alsa_mix_ctrl = optarg;
      hardware_mixer = 1;
      break;
    case 'i':
      alsa_mix_index = strtol(optarg, NULL, 10);
      break;
    default:
      warn("Invalid audio option \"-%c\" specified -- ignored.", opt);
      help();
    }
  }

  if (optind < argc) {
    warn("Invalid audio argument: \"%s\" -- ignored", argv[optind]);
  }

  debug(1, "alsa: output device name is \"%s\".", alsa_out_dev);

  // so, now, if the option to keep the DAC running has been selected, start a
  // thread to monitor the
  // length of the queue
  // if the queue gets too short, stuff it with silence

  most_recent_write_time = 0; // could be used by the alsa_buffer_monitor_thread_code
  pthread_create(&alsa_buffer_monitor_thread, NULL, &alsa_buffer_monitor_thread_code, NULL);

  return response;
}

static void deinit(void) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  // debug(2,"audio_alsa deinit called.");
  stop();
  debug(2, "Cancel buffer monitor thread.");
  pthread_cancel(alsa_buffer_monitor_thread);
  debug(3, "Join buffer monitor thread.");
  pthread_join(alsa_buffer_monitor_thread, NULL);
  pthread_setcancelstate(oldState, NULL);
}

int set_mute_state() {
  int response = 1; // some problem expected, e.g. no mixer or not allowed to use it or disconnected
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  pthread_cleanup_debug_mutex_lock(&alsa_mixer_mutex, 10000, 0);
  if ((alsa_backend_state != abm_disconnected) && (config.alsa_use_hardware_mute == 1) &&
      (open_mixer() == 1)) {
    response = 0; // okay if actually using the mute facility
    debug(2, "alsa: actually set_mute_state");
    int mute = 0;
    if ((mute_requested_externally != 0) || (mute_requested_internally != 0))
      mute = 1;
    if (mute == 1) {
      debug(2, "alsa: hardware mute switched on");
      if (snd_mixer_selem_has_playback_switch(alsa_mix_elem))
        snd_mixer_selem_set_playback_switch_all(alsa_mix_elem, 0);
      else {
        volume_based_mute_is_active = 1;
        do_snd_mixer_selem_set_playback_dB_all(alsa_mix_elem, alsa_mix_mute);
      }
    } else {
      debug(2, "alsa: hardware mute switched off");
      if (snd_mixer_selem_has_playback_switch(alsa_mix_elem))
        snd_mixer_selem_set_playback_switch_all(alsa_mix_elem, 1);
      else {
        volume_based_mute_is_active = 0;
        do_snd_mixer_selem_set_playback_dB_all(alsa_mix_elem, set_volume);
      }
    }
    close_mixer();
  }
  debug_mutex_unlock(&alsa_mixer_mutex, 3); // release the mutex
  pthread_cleanup_pop(0); // release the mutex  
  pthread_setcancelstate(oldState, NULL);
  return response;
}

static void start(__attribute__((unused)) int i_sample_rate, __attribute__((unused)) int i_sample_format) {
  debug(3, "audio_alsa start called.");
  
  frame_index = 0;
  measurement_data_is_valid = 0;

  stall_monitor_start_time = 0;
  stall_monitor_frame_count = 0;
  if (alsa_device_initialised == 0) {
    debug(2, "alsa: start() calling do_alsa_device_init_if_needed.");
    do_alsa_device_init_if_needed();
  }
}

int standard_delay_and_status(snd_pcm_state_t *state, snd_pcm_sframes_t *delay, enum yndk_type *using_update_timestamps) {
  int ret = 0;
  if (using_update_timestamps)
    *using_update_timestamps = YNDK_NO;
  *state = snd_pcm_state(alsa_handle);
  if ((*state == SND_PCM_STATE_RUNNING) || (*state == SND_PCM_STATE_DRAINING)) {
    ret = snd_pcm_delay(alsa_handle,delay);
  } else {
  // not running, thus no delay information, thus can't check for frame
  // rates
    frame_index = 0; // we'll be starting over...
    measurement_data_is_valid = 0;
    *delay = 0;  
  }
    
  stall_monitor_start_time = 0;  // zero if not initialised / not started / zeroed by flush
  stall_monitor_frame_count = 0; // set to delay at start of time, incremented by any writes

  return ret;
}

int precision_delay_and_status(snd_pcm_state_t *state, snd_pcm_sframes_t *delay, enum yndk_type *using_update_timestamps) {
  snd_pcm_status_t *alsa_snd_pcm_status;
  snd_pcm_status_alloca(&alsa_snd_pcm_status);
  
  if (using_update_timestamps)
    *using_update_timestamps = YNDK_DONT_KNOW;

  struct timespec tn;                // time now
  snd_htimestamp_t update_timestamp; // actually a struct timespec

  int ret = snd_pcm_status(alsa_handle, alsa_snd_pcm_status);
  if (ret == 0) {
 
 // must be 1.1 or later to use snd_pcm_status_get_driver_htstamp
#if SND_LIB_MINOR == 0
      snd_pcm_status_get_htstamp(alsa_snd_pcm_status, &update_timestamp);
#else
      snd_pcm_status_get_driver_htstamp(alsa_snd_pcm_status, &update_timestamp);
#endif

    *state = snd_pcm_status_get_state(alsa_snd_pcm_status);

    if ((*state == SND_PCM_STATE_RUNNING) || (*state == SND_PCM_STATE_DRAINING)) {

      uint64_t update_timestamp_ns =
          update_timestamp.tv_sec * (uint64_t)1000000000 + update_timestamp.tv_nsec;

      // if the update_timestamp is zero, we take this to mean that the device doesn't report
      // interrupt timings. (It could be that it's not a real hardware device.)
      // so we switch to getting the delay the regular way
      // i.e. using snd_pcm_delay	()
      if (using_update_timestamps) {
        if (update_timestamp_ns == 0)
          *using_update_timestamps = YNDK_NO;
        else
          *using_update_timestamps = YNDK_YES;
      }
      
// user information
      if (update_timestamp_ns == 0) {
        if (delay_type_notified != 1) {
          inform("Note: the alsa output device \"%s\" is not capable of high precision delay timing.", snd_pcm_name(alsa_handle));
          debug(1,"alsa: delay_and_status must use snd_pcm_delay() to calculate delay");
          delay_type_notified = 1;
        }
      } else {
// diagnostic
        if (delay_type_notified != 0) {
          debug(2,"alsa: delay_and_status using snd_pcm_status_get_delay() to calculate delay");
          delay_type_notified = 0;
        }
      }

      if (update_timestamp_ns == 0) {
        ret = snd_pcm_delay	(alsa_handle,delay);
      } else {
        *delay = snd_pcm_status_get_delay(alsa_snd_pcm_status);

/*
// It seems that the alsa library uses CLOCK_REALTIME before 1.0.28, even though
// the check for monotonic returns true. Might have to watch out for this.
  #if SND_LIB_MINOR == 0 && SND_LIB_SUBMINOR < 28
        clock_gettime(CLOCK_REALTIME, &tn);
  #else
        clock_gettime(CLOCK_MONOTONIC, &tn);
  #endif
*/

        if (use_monotonic_clock)
          clock_gettime(CLOCK_MONOTONIC, &tn);
        else
          clock_gettime(CLOCK_REALTIME, &tn);
  
        uint64_t time_now_ns = tn.tv_sec * (uint64_t)1000000000 + tn.tv_nsec;

        // see if it's stalled

        if ((stall_monitor_start_time != 0) && (stall_monitor_frame_count == *delay)) {
          // hasn't outputted anything since the last call to delay()

          if (((update_timestamp_ns - stall_monitor_start_time) > stall_monitor_error_threshold) ||
              ((time_now_ns - stall_monitor_start_time) > stall_monitor_error_threshold)) {
            debug(2, "DAC seems to have stalled with time_now_ns: %" PRIX64
                     ", update_timestamp_ns: %" PRIX64 ", stall_monitor_start_time %" PRIX64
                     ", stall_monitor_error_threshold %" PRIX64 ".",
                  time_now_ns, update_timestamp_ns, stall_monitor_start_time,
                  stall_monitor_error_threshold);
            debug(2, "DAC seems to have stalled with time_now: %lx,%lx"
                     ", update_timestamp: %lx,%lx, stall_monitor_start_time %" PRIX64
                     ", stall_monitor_error_threshold %" PRIX64 ".",
                  tn.tv_sec, tn.tv_nsec, update_timestamp.tv_sec, update_timestamp.tv_nsec, stall_monitor_start_time,
                  stall_monitor_error_threshold);
            ret = sps_extra_code_output_stalled;
          }
        } else {
          stall_monitor_start_time = update_timestamp_ns;
          stall_monitor_frame_count = *delay;
        }

        if (ret == 0) {
          uint64_t delta = time_now_ns - update_timestamp_ns;

          uint64_t frames_played_since_last_interrupt =
              ((uint64_t)config.output_rate * delta) / 1000000000;
          snd_pcm_sframes_t frames_played_since_last_interrupt_sized =
              frames_played_since_last_interrupt;

          *delay = *delay - frames_played_since_last_interrupt_sized;
        }
      }
    } else { // not running, thus no delay information, thus can't check for
             // stall
      *delay = 0;
      stall_monitor_start_time = 0;  // zero if not initialised / not started / zeroed by flush
      stall_monitor_frame_count = 0; // set to delay at start of time, incremented by any writes

      // not running, thus no delay information, thus can't check for frame
      // rates
      frame_index = 0; // we'll be starting over...
      measurement_data_is_valid = 0;
    }
  } else {
    debug(1, "alsa: can't get device's status.");
  }
  return ret;
}

int delay(long *the_delay) {
  // returns 0 if the device is in a valid state -- SND_PCM_STATE_RUNNING or
  // SND_PCM_STATE_PREPARED
  // or SND_PCM_STATE_DRAINING
  // and returns the actual delay if running or 0 if prepared in *the_delay

  // otherwise return an error code
  // the error code could be a Unix errno code or a snderror code, or
  // the sps_extra_code_output_stalled or the
  // sps_extra_code_output_state_cannot_make_ready codes
  int ret = 0;
  *the_delay = 0;
  if (alsa_handle == NULL)
    ret = ENODEV;
  else {
    int oldState;

    snd_pcm_state_t state;
    snd_pcm_sframes_t my_delay = 0; // this initialisation is to silence a clang warning

    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
    pthread_cleanup_debug_mutex_lock(&alsa_mutex, 10000, 0);

    ret = delay_and_status(&state, &my_delay, NULL);

    debug_mutex_unlock(&alsa_mutex, 0);
    pthread_cleanup_pop(0);
    pthread_setcancelstate(oldState, NULL);

    *the_delay = my_delay; // note: snd_pcm_sframes_t is a long
  }
  return ret;
}

int get_rate_information(uint64_t *elapsed_time, uint64_t *frames_played) {
  int response = 0; // zero means okay
  if (measurement_data_is_valid) {
    *elapsed_time = measurement_time - measurement_start_time;
    *frames_played = frames_played_at_measurement_time - frames_played_at_measurement_start_time;
  } else {
    *elapsed_time = 0;
    *frames_played = 0;
    response = -1;
  }
  return response;
}

int do_play(void *buf, int samples) {
  // assuming the alsa_mutex has been acquired
  // debug(3,"audio_alsa play called.");
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable

  snd_pcm_state_t state;
  snd_pcm_sframes_t my_delay;
  int ret = delay_and_status(&state, &my_delay, NULL);

  if (ret == 0) { // will be non-zero if an error or a stall

    if ((samples != 0) && (buf != NULL)) {

      // jut check the state of the DAC

      if ((state != SND_PCM_STATE_PREPARED) && (state != SND_PCM_STATE_RUNNING) &&
          (state != SND_PCM_STATE_XRUN)) {
        debug(1, "alsa: DAC in odd SND_PCM_STATE_* %d prior to writing.", state);
      }

      // debug(3, "write %d frames.", samples);
      ret = alsa_pcm_write(alsa_handle, buf, samples);
      if (ret == samples) {
        stall_monitor_frame_count += samples;

        if (frame_index == 0) {
          frames_sent_for_playing = samples;
        } else {
          frames_sent_for_playing += samples;
        }

        const uint64_t start_measurement_from_this_frame =
            (2 * config.output_rate) / 352; // two seconds of frames

        frame_index++;

        if ((frame_index == start_measurement_from_this_frame) ||
            ((frame_index > start_measurement_from_this_frame) && (frame_index % 32 == 0))) {

          measurement_time = get_absolute_time_in_fp();
          frames_played_at_measurement_time = frames_sent_for_playing - my_delay - samples;

          if (frame_index == start_measurement_from_this_frame) {
            // debug(1, "Start frame counting");
            frames_played_at_measurement_start_time = frames_played_at_measurement_time;
            measurement_start_time = measurement_time;
            measurement_data_is_valid = 1;
          }
        }
      } else {
        frame_index = 0;
        measurement_data_is_valid = 0;
        if (ret == -EPIPE) { /* underrun */
          debug(1, "alsa: underrun while writing %d samples to alsa device.", samples);
          ret = snd_pcm_recover(alsa_handle, ret, debuglev > 0 ? 1 : 0);
          if (ret < 0) {
            warn("alsa: can't recover from SND_PCM_STATE_XRUN: %s.", snd_strerror(ret));
          }
        } else if (ret == -ESTRPIPE) { /* suspended */
          debug(1, "alsa: suspended while writing %d samples to alsa device.", samples);
          while ((ret = snd_pcm_resume(alsa_handle)) == -EAGAIN) {
            sleep(1); /* wait until the suspend flag is released */
            if (ret < 0) {
              warn("alsa: can't recover from SND_PCM_STATE_SUSPENDED state, "
                   "snd_pcm_prepare() "
                   "failed: %s.",
                   snd_strerror(ret));
            }
          }
        } else {
          char errorstring[1024];
          strerror_r(-ret, (char *)errorstring, sizeof(errorstring));
          debug(1, "alsa: error %d (\"%s\") writing %d samples to alsa device.", ret, (char *)errorstring, samples);
        }

      }
    }
  } else {
    debug(1, "alsa: device status returns fault status %d and SND_PCM_STATE_* "
             "%d  for play.",
          ret, state);
    frame_index = 0;
    measurement_data_is_valid = 0;
  }

  pthread_setcancelstate(oldState, NULL);
  return ret;
}

int do_open(int do_auto_setup) {
  int ret = 0;
  if (alsa_backend_state != abm_disconnected)
    debug(1, "alsa: do_open() -- opening the output device when it is already "
             "connected");
  if (alsa_handle == NULL) {
    // debug(1,"alsa: do_open() -- opening the output device");
    ret = open_alsa_device(do_auto_setup);
    if (ret == 0) {
      mute_requested_internally = 0;
      if (audio_alsa.volume)
        do_volume(set_volume);
      if (audio_alsa.mute) {
        debug(2, "do_open() set_mute_state");
        set_mute_state(); // the mute_requested_externally flag will have been
                          // set accordingly
        // do_mute(0); // complete unmute
      }
      
      alsa_backend_state = abm_connected; // only do this if it really opened it.
    }
  } else {
    debug(1, "alsa: do_open() -- output device already open.");
  }
  return ret;
}

int do_close() {
  if (alsa_backend_state == abm_disconnected)
    debug(1, "alsa: do_close() -- closing the output device when it is already "
             "disconnected");
  int derr = 0;
  if (alsa_handle) {
    // debug(1,"alsa: do_close() -- closing the output device");
    if ((derr = snd_pcm_drop(alsa_handle)))
      debug(1, "Error %d (\"%s\") dropping output device.", derr, snd_strerror(derr));
    if ((derr = snd_pcm_hw_free(alsa_handle)))
      debug(1, "Error %d (\"%s\") freeing the output device hardware.", derr, snd_strerror(derr));

    // flush also closes the device
    debug(2, "alsa: do_close() -- closing alsa handle");
    if ((derr = snd_pcm_close(alsa_handle)))
      debug(1, "Error %d (\"%s\") closing the output device.", derr, snd_strerror(derr));
    alsa_handle = NULL;
  } else {
    debug(1, "alsa: do_close() -- output device already closed.");
  }
  alsa_backend_state = abm_disconnected;
  return derr;
}

int play(void *buf, int samples) {

  // play() will change the state of the alsa_backend_mode to abm_playing
  // also, if the present alsa_backend_state is abm_disconnected, then first the
  // DAC must be
  // connected

  // debug(3,"audio_alsa play called.");
  int ret = 0;

  pthread_cleanup_debug_mutex_lock(&alsa_mutex, 50000, 0);

  if (alsa_backend_state == abm_disconnected) {
    ret = do_open(0); // don't try to auto setup
    if (ret == 0)
      debug(2, "alsa: play() -- opened output device");
  }

  if (ret == 0) {
    if (alsa_backend_state != abm_playing) {
      debug(2, "alsa: play() -- alsa_backend_state => abm_playing");
      alsa_backend_state = abm_playing;
      
      // mute_requested_internally = 0; // stop requesting a mute for backend's own
                                     // reasons, which might have been a flush
      //debug(2, "play() set_mute_state");
      //set_mute_state(); // try to action the request and return a status
      // do_mute(0); // unmute for backend's reason
    }
    ret = do_play(buf, samples);
  }

  debug_mutex_unlock(&alsa_mutex, 0);
  pthread_cleanup_pop(0); // release the mutex
  return ret;
}

int prepare(void) {
	// this will leave the DAC open / connected.
  int ret = 0;

  pthread_cleanup_debug_mutex_lock(&alsa_mutex, 50000, 0);

  if (alsa_backend_state == abm_disconnected) {
    ret = do_open(1); // do auto setup 
    if (ret == 0)
      debug(2, "alsa: prepare() -- opened output device");
    
  }

  debug_mutex_unlock(&alsa_mutex, 0);
  pthread_cleanup_pop(0); // release the mutex
  return ret;
}

static void flush(void) {
  // debug(2,"audio_alsa flush called.");
  pthread_cleanup_debug_mutex_lock(&alsa_mutex, 10000, 1);
  // mute_requested_internally = 1; // request a mute for backend's reasons
  // debug(2, "flush() set_mute_state");
  // set_mute_state();
  // do_mute(1); // mute for backend's own reasons
  if (alsa_backend_state != abm_disconnected) { // must be playing or connected...
    if (config.keep_dac_busy != 0) {
      debug(2, "alsa: flush() -- alsa_backend_state => abm_connected.");
      alsa_backend_state = abm_connected;
    } else {
      debug(2, "alsa: flush() -- closing the output device");
      do_close(); // will change the state to disconnected
      debug(2, "alsa: flush() -- alsa_backend_state => abm_disconnected.");
    }
  } else
    debug(3, "alsa: flush() -- called on a disconnected alsa backend");
  debug_mutex_unlock(&alsa_mutex, 3);
  pthread_cleanup_pop(0); // release the mutex
}

static void stop(void) {
  // debug(2,"audio_alsa stop called.");
  flush(); // flush will also close the device if appropriate
}

static void parameters(audio_parameters *info) {
  info->minimum_volume_dB = alsa_mix_mindb;
  info->maximum_volume_dB = alsa_mix_maxdb;
}

void do_volume(double vol) { // caller is assumed to have the alsa_mutex when
                             // using this function
  debug(3, "Setting volume db to %f.", vol);
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  set_volume = vol;
  pthread_cleanup_debug_mutex_lock(&alsa_mixer_mutex, 1000, 1);
  if (volume_set_request && (open_mixer() == 1)) {
    if (has_softvol) {
      if (ctl && elem_id) {
        snd_ctl_elem_value_t *value;
        long raw;

        if (snd_ctl_convert_from_dB(ctl, elem_id, vol, &raw, 0) < 0)
          debug(1, "Failed converting dB gain to raw volume value for the "
                   "software volume control.");

        snd_ctl_elem_value_alloca(&value);
        snd_ctl_elem_value_set_id(value, elem_id);
        snd_ctl_elem_value_set_integer(value, 0, raw);
        snd_ctl_elem_value_set_integer(value, 1, raw);
        if (snd_ctl_elem_write(ctl, value) < 0)
          debug(1, "Failed to set playback dB volume for the software volume "
                   "control.");
      }
    } else {
      if (volume_based_mute_is_active == 0) {
        // debug(1,"Set alsa volume.");
        do_snd_mixer_selem_set_playback_dB_all(alsa_mix_elem, vol);
      } else {
        debug(2, "Not setting volume because volume-based mute is active");
      }
    }
    volume_set_request = 0; // any external request that has been made is now satisfied
    close_mixer();
  }
  debug_mutex_unlock(&alsa_mixer_mutex, 3);
  pthread_cleanup_pop(0); // release the mutex
  pthread_setcancelstate(oldState, NULL);
}

void volume(double vol) {
  volume_set_request = 1; // an external request has been made to set the volume
  do_volume(vol);
}

/*
static void linear_volume(double vol) {
  debug(2, "Setting linear volume to %f.", vol);
  set_volume = vol;
  if (hardware_mixer && alsa_mix_handle) {
    double linear_volume = pow(10, vol);
    // debug(1,"Linear volume is %f.",linear_volume);
    long int_vol = alsa_mix_minv + (alsa_mix_maxv - alsa_mix_minv) *
linear_volume;
    // debug(1,"Setting volume to %ld, for volume input of %f.",int_vol,vol);
    if (alsa_mix_handle) {
      if (snd_mixer_selem_set_playback_volume_all(alsa_mix_elem, int_vol) != 0)
        die("Failed to set playback volume");

    }
  }
}
*/

int mute(int mute_state_requested) {                // these would be for external reasons, not
                                                    // because of the
                                                    // state of the backend.
  mute_requested_externally = mute_state_requested; // request a mute for external reasons
  debug(2, "mute(%d) set_mute_state", mute_state_requested);
  return set_mute_state();
}
/*
void alsa_buffer_monitor_thread_cleanup_function(__attribute__((unused)) void
*arg) {
  debug(1, "alsa: alsa_buffer_monitor_thread_cleanup_function called.");
}
*/

void *alsa_buffer_monitor_thread_code(__attribute__((unused)) void *arg) {
  int okb = -1;
  while (1) {
    if (okb != config.keep_dac_busy) {
      debug(2,"keep_dac_busy is now \"%s\"",config.keep_dac_busy == 0 ? "no" : "yes");
      okb = config.keep_dac_busy;
    }
    if ((config.keep_dac_busy != 0) && (alsa_device_initialised == 0)) {
      debug(2, "alsa: alsa_buffer_monitor_thread_code() calling "
               "do_alsa_device_init_if_needed.");
      do_alsa_device_init_if_needed();
    }
    int sleep_time_ms = (int)(config.audio_backend_silence_scan_interval * 1000);
    pthread_cleanup_debug_mutex_lock(&alsa_mutex, 200000, 0);
    // check possible state transitions here
    if ((alsa_backend_state == abm_disconnected) && (config.keep_dac_busy != 0)) {
      // open the dac and move to abm_connected mode
      if (do_open(1) == 0) // no automatic setup of rate and speed if necessary
        debug(2, "alsa: alsa_buffer_monitor_thread_code() -- output device opened; "
                 "alsa_backend_state => abm_connected");
    } else if ((alsa_backend_state == abm_connected) && (config.keep_dac_busy == 0)) {
      stall_monitor_start_time = 0;
      frame_index = 0;
      measurement_data_is_valid = 0;
      debug(2, "alsa: alsa_buffer_monitor_thread_code() -- closing the output "
               "device");
      do_close();
      debug(2, "alsa: alsa_buffer_monitor_thread_code() -- alsa_backend_state "
               "=> abm_disconnected");
    }
    // now, if the backend is not in the abm_disconnected state
    // and config.keep_dac_busy is true (at the present, this has to be the case
    // to be in the
    // abm_connected state in the first place...) then do the silence-filling
    // thing, if needed, and if the output device is capable of precision delay.
    if ((alsa_backend_state != abm_disconnected) && (config.keep_dac_busy != 0) && precision_delay_available()) {
      int reply;
      long buffer_size = 0;
      snd_pcm_state_t state;
      uint64_t present_time = get_absolute_time_in_fp();
      if ((most_recent_write_time == 0) || (present_time > most_recent_write_time)) {
        reply = delay_and_status(&state, &buffer_size, NULL);
        if (reply != 0) {
          buffer_size = 0;
          char errorstring[1024];
          strerror_r(-reply, (char *)errorstring, sizeof(errorstring));
          debug(1, "alsa: alsa_buffer_monitor_thread_code delay error %d: \"%s\".", reply,
                (char *)errorstring);
        }
        long buffer_size_threshold =
            (long)(config.audio_backend_silence_threshold * config.output_rate);
        if (buffer_size < buffer_size_threshold) {
          uint64_t sleep_time_in_fp = sleep_time_ms;
          sleep_time_in_fp = sleep_time_in_fp << 32;
          sleep_time_in_fp = sleep_time_in_fp / 1000;
          // debug(1,"alsa: sleep_time: %d ms or 0x%" PRIx64 " in fp
          // form.",sleep_time_ms,sleep_time_in_fp); int frames_of_silence =
          // (config.output_rate *
          // sleep_time_ms * 2) / 1000;
          int frames_of_silence = 1024;
          size_t size_of_silence_buffer = frames_of_silence * frame_size;
          // debug(1, "alsa: alsa_buffer_monitor_thread_code -- silence buffer
          // length: %u bytes.",
          //      size_of_silence_buffer);
          void *silence = malloc(size_of_silence_buffer);
          if (silence == NULL) {
            debug(1, "alsa: alsa_buffer_monitor_thread_code -- failed to "
                     "allocate memory for a "
                     "silent frame buffer.");
          } else {
            pthread_cleanup_push(malloc_cleanup, silence);
            int use_dither = 0;
            if ((hardware_mixer == 0) && (config.ignore_volume_control == 0) &&
                (config.airplay_volume != 0.0))
              use_dither = 1;
            dither_random_number_store =
                generate_zero_frames(silence, frames_of_silence, config.output_format,
                                     use_dither, // i.e. with dither
                                     dither_random_number_store);
            // debug(1,"Play %d frames of silence with most_recent_write_time of
            // %" PRIx64 ".",
            //    frames_of_silence,most_recent_write_time);
            do_play(silence, frames_of_silence);
            pthread_cleanup_pop(1);
          }
        }
      }
    }
    debug_mutex_unlock(&alsa_mutex, 0);
    pthread_cleanup_pop(0);       // release the mutex
    usleep(sleep_time_ms * 1000); // has a cancellation point in it
  }
  pthread_exit(NULL);
}
