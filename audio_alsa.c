/*
 * libalsa output driver. This file is part of Shairport.
 * Copyright (c) Muffinman, Skaman 2013
 * Copyright (c) Mike Brady 2014--2025
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

#include "activity_monitor.h"
#include "audio.h"
#include "common.h"

enum alsa_backend_mode {
  abm_disconnected,
  abm_connected,
  abm_playing
} alsa_backend_state; // under the control of alsa_mutex

typedef struct {
  snd_pcm_format_t alsa_code;
  int sample_size;
} format_record;

// This array is of all the formats known to Shairport Sync, in order of the SPS_FORMAT definitions,
// with their equivalent alsa codes and their frame sizes.
// If just one format is requested, then its entry is searched for in the array and checked on the
// device
// If auto format is requested, then each entry in turn is tried until a working format is found.
// So, it should be in the search order.

format_record fr[] = {
    {SND_PCM_FORMAT_UNKNOWN, 0}, // unknown
    {SND_PCM_FORMAT_S8, 1},      {SND_PCM_FORMAT_U8, 1},      {SND_PCM_FORMAT_S16_LE, 2},
    {SND_PCM_FORMAT_S16_BE, 2},  {SND_PCM_FORMAT_S24_LE, 4},  {SND_PCM_FORMAT_S24_BE, 4},
    {SND_PCM_FORMAT_S24_3LE, 3}, {SND_PCM_FORMAT_S24_3BE, 3}, {SND_PCM_FORMAT_S32_LE, 4},
    {SND_PCM_FORMAT_S32_BE, 4},  {SND_PCM_FORMAT_UNKNOWN, 0}, // auto
    {SND_PCM_FORMAT_UNKNOWN, 0},                              // illegal
};

int output_method_signalled = 0; // for reporting whether it's using mmap or not
int delay_type_notified = -1; // for controlling the reporting of whether the output device can do
                              // precision delays (e.g. alsa->pulsaudio virtual devices can't)
int use_monotonic_clock = 0;  // this value will be set when the hardware is initialised

static int32_t current_encoded_output_format; // ms 8 bits: channels; next 16 bits: rate/100;
                                              // rightmost 8 bits: sps_format

static char public_channel_map[128] = "";

// static output_configuration_t alsa_configuration; // sample
// static output_configuration_t *current_alsa_configuration;

static volume_range_t volume_range = {0, 0};
static output_parameters_t output_parameters = {NULL};

static void help(void);
static int init(int argc, char **argv);
static void deinit(void);
static int prepare(void);
static int32_t get_configuration(unsigned int channels, unsigned int rate, unsigned int sps_format);
static int configure(int32_t requested_encoded_format, char **channel_map);
static void start(int i_sample_rate, int i_sample_format);
static int play(void *buf, int samples, __attribute__((unused)) int sample_type,
                __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime);
static void stop(void);
static void flush(void);
static int delay(long *the_delay);
static int stats(uint64_t *raw_measurement_time, uint64_t *corrected_measurement_time,
                 uint64_t *the_delay, uint64_t *frames_sent_to_dac);

static void *alsa_buffer_monitor_thread_code(void *arg);

static void volume(double vol);
static void do_volume(double vol);
static int do_play(void *buf, int samples);

static output_parameters_t *parameters();
static int mute(int do_mute); // returns true if it actually is allowed to use the mute
static double set_volume;
audio_output audio_alsa = {.name = "alsa",
                           .help = &help,
                           .init = &init,
                           .deinit = &deinit,
                           .prepare = &prepare,
                           .get_configuration = &get_configuration,
                           .configure = &configure,
                           .start = &start,
                           .stop = &stop,
                           .is_running = NULL,
                           .flush = &flush,
                           .delay = &delay,
                           .play = &play,
                           .stats = &stats, // will also include frames of silence sent to stop
                                            // standby mode
                                            //    .rate_info = NULL,
                           .mute = NULL,    // a function will be provided if it can, and is allowed
                                            // to, do hardware mute
                           .volume =
                               NULL, // a function will be provided if it can do hardware volume
                           .parameters = &parameters};
static int do_open();
static int do_close();

pthread_mutex_t alsa_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t alsa_mixer_mutex = PTHREAD_MUTEX_INITIALIZER;

pthread_t alsa_buffer_monitor_thread;

// for deciding when to activate mute
// there are two sources of requests to mute -- the backend itself, e.g. when it
// is flushing
// and the player, e.g. when volume goes down to -144, i.e. mute.

// we may not be allowed to use hardware mute, so we must reflect that too.

int mute_requested_externally = 0;
int mute_requested_internally = 0;

// for tracking if the output device has stalled
uint64_t stall_monitor_new_frame_count_time; // when the delay was last measured
long stall_monitor_new_frame_count;     // the delay when measured last plus all subsequently added
                                        // frames
uint64_t stall_monitor_error_threshold; // if no frames have been output in a time longer than this,
                                        // it's an error

snd_output_t *output = NULL;

int alsa_device_initialised; // boolean to ensure the initialisation is only
                             // done once

yndk_type precision_delay_available_status =
    YNDK_DONT_KNOW; // initially, we don't know if the device can do precision delay

snd_pcm_t *alsa_handle = NULL;
int alsa_handle_status =
    -ENODEV; // if alsa_handle is NULL, this should say why with a unix error code
snd_pcm_hw_params_t *alsa_params = NULL;
snd_pcm_sw_params_t *alsa_swparams = NULL;
snd_ctl_t *ctl = NULL;
snd_ctl_elem_id_t *elem_id = NULL;
snd_mixer_t *alsa_mix_handle = NULL;
snd_mixer_elem_t *alsa_mix_elem = NULL;
snd_mixer_selem_id_t *alsa_mix_sid = NULL;
long alsa_mix_minv, alsa_mix_maxv;
long alsa_mix_mindb, alsa_mix_maxdb;

char *alsa_out_dev = "default";
char *hw_alsa_out_dev = NULL;
char *alsa_mix_dev = NULL;
char *alsa_mix_ctrl = NULL;
int alsa_mix_index = 0;
int has_softvol = 0;

int64_t dither_random_number_store = 0;

int volume_set_request = 0; // set when an external request is made to set the volume.

int mixer_volume_setting_gives_mute = 0; // set when it is discovered that
                                         // particular mixer volume setting
                                         // causes a mute.
long alsa_mix_mute;                      // setting the volume to this value mutes output, if
                                         // mixer_volume_setting_gives_mute is true
int volume_based_mute_is_active =
    0; // set when muting is being done by a setting the volume to a magic value

sps_format_t disable_standby_mode_default_format;
int disable_standby_mode_default_rate;
int disable_standby_mode_default_channels;

// use this to allow the use of snd_pcm_writei or snd_pcm_mmap_writei
snd_pcm_sframes_t (*alsa_pcm_write)(snd_pcm_t *, const void *, snd_pcm_uframes_t) = snd_pcm_writei;

void handle_unfixable_error(int errorCode) {
  if (config.unfixable_error_reported == 0) {
    config.unfixable_error_reported = 1;
    char messageString[1024];
    messageString[0] = '\0';
    snprintf(messageString, sizeof(messageString), "output_device_error_%d", errorCode);
    if (config.cmd_unfixable) {
      command_execute(config.cmd_unfixable, messageString, 1);
    } else {
      die("An unrecoverable error, \"output_device_error_%d\", has been "
          "detected. Doing an emergency exit, as no run_this_if_an_unfixable_error_is_detected "
          "program.",
          errorCode);
    }
  }
}

static char *device_types[] = {
    "SND_PCM_TYPE_HW",       "SND_PCM_TYPE_HOOKS",    "SND_PCM_TYPE_MULTI",
    "SND_PCM_TYPE_FILE",     "SND_PCM_TYPE_NULL",     "SND_PCM_TYPE_SHM",
    "SND_PCM_TYPE_INET",     "SND_PCM_TYPE_COPY",     "SND_PCM_TYPE_LINEAR",
    "SND_PCM_TYPE_ALAW",     "SND_PCM_TYPE_MULAW",    "SND_PCM_TYPE_ADPCM",
    "SND_PCM_TYPE_RATE",     "SND_PCM_TYPE_ROUTE",    "SND_PCM_TYPE_PLUG",
    "SND_PCM_TYPE_SHARE",    "SND_PCM_TYPE_METER",    "SND_PCM_TYPE_MIX",
    "SND_PCM_TYPE_DROUTE",   "SND_PCM_TYPE_LBSERVER", "SND_PCM_TYPE_LINEAR_FLOAT",
    "SND_PCM_TYPE_LADSPA",   "SND_PCM_TYPE_DMIX",     "SND_PCM_TYPE_JACK",
    "SND_PCM_TYPE_DSNOOP",   "SND_PCM_TYPE_DSHARE",   "SND_PCM_TYPE_IEC958",
    "SND_PCM_TYPE_SOFTVOL",  "SND_PCM_TYPE_IOPLUG",   "SND_PCM_TYPE_EXTPLUG",
    "SND_PCM_TYPE_MMAP_EMUL"};

static int permissible_configuration_check_done = 0;

static uint16_t permissible_configurations[SPS_RATE_HIGHEST + 1][SPS_FORMAT_HIGHEST_NATIVE + 1]
                                          [8 + 1];

static int get_permissible_configuration_settings() {
  int ret = 0;
  if (permissible_configuration_check_done == 0) {
    uint64_t hto = get_absolute_time_in_ns();
    snd_pcm_hw_params_t *local_alsa_params = NULL;
    snd_pcm_hw_params_alloca(&local_alsa_params);
    snd_pcm_info_t *local_alsa_info;
    snd_pcm_info_alloca(&local_alsa_info);
    pthread_cleanup_debug_mutex_lock(&alsa_mutex, 50000, 0);
    snd_pcm_t *temporary_alsa_handle = NULL;
    ret = snd_pcm_open(&temporary_alsa_handle, alsa_out_dev, SND_PCM_STREAM_PLAYBACK, 0);
    if (ret == 0) {
      snd_pcm_type_t device_type = snd_pcm_type(temporary_alsa_handle);
      ret = snd_pcm_info(temporary_alsa_handle, local_alsa_info);
      if (ret == 0) {
        int card_number = snd_pcm_info_get_card(local_alsa_info);

        if (card_number >= 0) {
          debug(2, "output device is card %d.", card_number);
          char device_name[64] = "";
          snprintf(device_name, sizeof(device_name) - 1, "hw:%d", card_number);
          snd_ctl_t *handle;
          int err = snd_ctl_open(&handle, device_name, 0);

          if (err == 0) {
            snd_ctl_card_info_t *info;
            snd_ctl_card_info_alloca(&info);
            err = snd_ctl_card_info(handle, info);
            if (err == 0) {
              debug(2, "card name: \"%s\", long name: \"%s\".", snd_ctl_card_info_get_name(info),
                    snd_ctl_card_info_get_longname(info));
            }
            snd_ctl_close(handle);
          }
        }

        debug(
            2, "device: \"%s\", name: \"%s\", type: \"%s\", id: \"%s\", CARD=%d,DEV=%u,SUBDEV=%u.",
            alsa_out_dev, snd_pcm_info_get_name(local_alsa_info), device_types[device_type],
            snd_pcm_info_get_id(local_alsa_info), snd_pcm_info_get_card(local_alsa_info),
            snd_pcm_info_get_device(local_alsa_info), snd_pcm_info_get_subdevice(local_alsa_info));

        // check what numbers of channels the device can provide...
        unsigned int c;
        // The Raspberry Pi built-in audio jack advertises 8-channel ability,
        // but it is not actually capable of doing it.
        // It can handle one- and two-channel stuff.
        if (strcmp("bcm2835 Headphones", snd_pcm_info_get_name(local_alsa_info)) == 0) {
          debug(1, "the output is to the Raspberry Pi built-in jack -- no more than two channels "
                   "will be used.");
          for (c = 3; c <= 8; c++)
            config.channel_set &=
                ~(1 << c); // the pi built-in jack DAC can't accommodate this number of channels
        }
        for (c = 1; c <= 8; c++) {
          // if it's in the channel set -- either due to a setting in the configuration file or by
          // default, check it...
          if ((config.channel_set & (1 << c)) != 0) {
            snd_pcm_hw_free(temporary_alsa_handle); // remove any previous configurations
            ret = snd_pcm_hw_params_any(temporary_alsa_handle, local_alsa_params);
            if (ret < 0)
              debug(1, "Broken configuration for \"%s\": no configurations available: %s\n",
                    alsa_out_dev, snd_strerror(ret));
            ret = snd_pcm_hw_params_set_rate_resample(temporary_alsa_handle, local_alsa_params, 0);
            ret = snd_pcm_hw_params_test_channels(temporary_alsa_handle, local_alsa_params, c);
            if (ret == 0) {
              debug(3, "\"%s\" can handle %u channels.", alsa_out_dev, c);
            } else {
              // the device can't handle this number of channels
              debug(3, "\"%s\" can not handle %u channels.", alsa_out_dev, c);
              config.channel_set &=
                  ~(1 << c); // the alsa device can't accommodate this number of channels
            }
          }
        }

        // check what speeds the device can handle
        sps_rate_t r;
        for (r = SPS_RATE_LOWEST; r <= SPS_RATE_HIGHEST; r++) {
          // if it's in the rate set -- either due to a setting in the configuration file or by
          // default, check it...
          if ((config.rate_set & (1 << r)) != 0) {
            snd_pcm_hw_free(temporary_alsa_handle); // remove any previous configurations
            ret = snd_pcm_hw_params_any(temporary_alsa_handle, local_alsa_params);
            if (ret < 0)
              debug(1, "Broken configuration for \"%s\": no configurations available: %s\n",
                    alsa_out_dev, snd_strerror(ret));
            ret = snd_pcm_hw_params_set_rate_resample(temporary_alsa_handle, local_alsa_params, 0);
            ret = snd_pcm_hw_params_test_rate(temporary_alsa_handle, local_alsa_params,
                                              sps_rate_actual_rate(r),
                                              0); // 0 means exact rate only
            if (ret == 0) {
              debug(3, "\"%s\" can handle a rate of %u fps.", alsa_out_dev,
                    sps_rate_actual_rate(r));
            } else {
              debug(3, "\"%s\" can not handle a rate of %u fps.", alsa_out_dev,
                    sps_rate_actual_rate(r));
              config.rate_set &= ~(1 << r); // the alsa device doesn't do this rate
            }
          }
        }

        // check what formats the device can handle
        sps_format_t f;
        for (f = SPS_FORMAT_LOWEST; f <= SPS_FORMAT_HIGHEST_NATIVE; f++) {
          // if it's in the format set -- either due to a setting in the configuration file or by
          // default, check it...
          if ((config.format_set & (1 << f)) != 0) {
            snd_pcm_hw_free(temporary_alsa_handle); // remove any previous configurations
            ret = snd_pcm_hw_params_any(temporary_alsa_handle, local_alsa_params);
            if (ret < 0)
              debug(1, "Broken configuration for \"%s\": no configurations available: %s\n",
                    alsa_out_dev, snd_strerror(ret));
            ret = snd_pcm_hw_params_set_rate_resample(temporary_alsa_handle, local_alsa_params, 0);
            ret = snd_pcm_hw_params_test_format(temporary_alsa_handle, local_alsa_params,
                                                fr[f].alsa_code);
            if (ret == 0) {
              debug(3, "\"%s\" can handle the %s format.", alsa_out_dev,
                    sps_format_description_string(f));
            } else {
              debug(3, "\"%s\" can not handle the %s format.", alsa_out_dev,
                    sps_format_description_string(f));
              config.format_set &= ~(1 << f); // the alsa device doesn't do this format
            }
          }
        }

        // now we have the channels, rates and formats, but we need to check each combination
        // set the permissible_configurations array (r/f/c) to EINVAL
        for (r = SPS_RATE_LOWEST; r <= SPS_RATE_HIGHEST; r++)
          for (f = SPS_FORMAT_LOWEST; f <= SPS_FORMAT_HIGHEST_NATIVE; f++)
            for (c = 0; c <= 8; c++) {
              permissible_configurations[r][f][c] = EINVAL;
            }
        // now check each combination of permitted rate/format/channel and see if it's really
        // allowed
        for (r = SPS_RATE_LOWEST; r <= SPS_RATE_HIGHEST; r++) {
          if ((config.rate_set & (1 << r)) != 0) {
            for (f = SPS_FORMAT_LOWEST; f <= SPS_FORMAT_HIGHEST_NATIVE; f++) {
              if ((config.format_set & (1 << f)) != 0) {
                for (c = 0; c <= 8; c++) {
                  if ((config.channel_set & (1 << c)) != 0) {
                    // debug(1, "check %u/%s/%u.", sps_rate_actual_rate(r),
                    // sps_format_description_string(f), c); note: only do the check if it's not a
                    // plug-in, i.e. not of type SND_PCM_TYPE_PLUG, or a  If it is a plugin, this
                    // check may take way too long and will likely be unnecessary anyway. Similarly
                    // for a NULL or an IOPLUG device.

                    if ((device_type == SND_PCM_TYPE_PLUG) || (device_type == SND_PCM_TYPE_NULL) ||
                        (device_type == SND_PCM_TYPE_IOPLUG)) {
                      permissible_configurations[r][f][c] = 0;
                    } else {
                      snd_pcm_hw_free(temporary_alsa_handle); // remove any previous configurations
                      ret = snd_pcm_hw_params_any(temporary_alsa_handle, local_alsa_params);
                      if (ret == 0) {
                        ret = snd_pcm_hw_params_set_rate_resample(temporary_alsa_handle,
                                                                  local_alsa_params, 0);
                        ret = snd_pcm_hw_params_test_channels(temporary_alsa_handle,
                                                              local_alsa_params, c);
                        if (ret == 0) {
                          ret = snd_pcm_hw_params_test_rate(
                              temporary_alsa_handle, local_alsa_params, sps_rate_actual_rate(r),
                              0); // 0 means exact rate only
                          if (ret == 0) {
                            ret = snd_pcm_hw_params_test_format(temporary_alsa_handle,
                                                                local_alsa_params, fr[f].alsa_code);
                            if (ret == 0) {
                              // debug(1, "passed: \"%s\", format: %s, rate: %u channels: %u.",
                              // alsa_out_dev, sps_format_description_string(f),
                              // sps_rate_actual_rate(r), c);
                              permissible_configurations[r][f][c] =
                                  0; // i.e. no error, so remove the EINVAL
                            } else {
                              debug(1, "Can't set format %s for \"%s\": %s.",
                                    sps_format_description_string(f), alsa_out_dev,
                                    snd_strerror(ret));
                            }
                          } else {
                            debug(1, "Can't set rate  of %u for \"%s\": %s.",
                                  sps_rate_actual_rate(r), alsa_out_dev, snd_strerror(ret));
                          }
                        } else {
                          debug(1, "Can't set channel count of %u for \"%s\": %s.", c, alsa_out_dev,
                                snd_strerror(ret));
                        }
                      } else {
                        debug(1, "Broken configuration for \"%s\": %s.", alsa_out_dev,
                              snd_strerror(ret));
                      }
                    }
                  }
                }
              }
            }
          }
        }
        // close the device
        snd_pcm_close(temporary_alsa_handle);
        permissible_configuration_check_done = 1;
        ret = 0; // all good here, even if the last ret was an error
      }
    }
    pthread_cleanup_pop(1); // unlock the mutex
    if (ret != 0) {
      char errorstring[1024];
      strerror_r(-ret, (char *)errorstring, sizeof(errorstring));
      debug(1, "get_permissible_configuration_settings: error %d (\"%s\").", ret, errorstring);
    }
    int64_t hot = get_absolute_time_in_ns() - hto;
    if (hot > 200000000)
      debug(1,
            "get_permissible_configuration_settings: permissible configurations check took %f ms.",
            0.000001 * hot);
  }
  return ret;
}

static int precision_delay_and_status(snd_pcm_state_t *state, snd_pcm_sframes_t *delay,
                                      yndk_type *using_update_timestamps);
static int standard_delay_and_status(snd_pcm_state_t *state, snd_pcm_sframes_t *delay,
                                     yndk_type *using_update_timestamps);

// use this to allow the use of standard or precision delay calculations, with standard the, uh,
// standard.
int (*delay_and_status)(snd_pcm_state_t *state, snd_pcm_sframes_t *delay,
                        yndk_type *using_update_timestamps) = standard_delay_and_status;

// this will return true if the DAC can return precision delay information and false if not
// if it is not yet known, it will test the output device to find out

// note -- once it has done the test, it decides -- even if the delay comes back with
// "don't know", it will take that as a "No" and remember it.
// If you want it to check again, set precision_delay_available_status to YNDK_DONT_KNOW
// first.

static int precision_delay_available() {
  if (precision_delay_available_status == YNDK_DONT_KNOW) {
    // this is very crude -- if the device is a hardware device, then it's assumed the delay is
    // precise
    const char *output_device_name = snd_pcm_name(alsa_handle);
    int is_a_real_hardware_device = 0;
    if (output_device_name != NULL)
      is_a_real_hardware_device = (strstr(output_device_name, "hw:") == output_device_name);

    // The criteria as to whether precision delay is available
    // is whether the device driver returns non-zero update timestamps
    // If it does, and the device is a hardware device (i.e. its name begins with "hw:"),
    // it is considered that precision delay is available. Otherwise, it's considered to be
    // unavailable.

    // To test, we play a silence buffer (fairly large to avoid underflow)
    // and then we check the delay return. It will tell us if it
    // was able to use the (non-zero) update timestamps

    int frames_of_silence = 4410;
    size_t size_of_silence_buffer =
        frames_of_silence *
        fr[FORMAT_FROM_ENCODED_FORMAT(current_encoded_output_format)].sample_size *
        CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format);
    void *silence = malloc(size_of_silence_buffer);
    if (silence == NULL) {
      debug(1, "alsa: precision_delay_available -- failed to "
               "allocate memory for a "
               "silent frame buffer.");
    } else {
      pthread_cleanup_push(malloc_cleanup, &silence);
      int use_dither = 0;
      if ((alsa_mix_ctrl == NULL) && (config.ignore_volume_control == 0) &&
          (config.airplay_volume != 0.0))
        use_dither = 1;
      dither_random_number_store =
          generate_zero_frames(silence, frames_of_silence,
                               use_dither, // i.e. with dither
                               dither_random_number_store, current_encoded_output_format);
      do_play(silence, frames_of_silence);
      pthread_cleanup_pop(1);
      // now we can get the delay, and we'll note if it uses update timestamps
      yndk_type uses_update_timestamps;
      snd_pcm_state_t state;
      snd_pcm_sframes_t delay;
      int ret = precision_delay_and_status(&state, &delay, &uses_update_timestamps);
      // debug(3,"alsa: precision_delay_available asking for delay and status with a return status
      // of %d, a delay of %ld and a uses_update_timestamps of %d.", ret, delay,
      // uses_update_timestamps);
      if (ret == 0) {
        if ((uses_update_timestamps == YNDK_YES) && (is_a_real_hardware_device)) {
          precision_delay_available_status = YNDK_YES;
          debug(2, "alsa: precision delay timing is available.");
        } else {
          if ((uses_update_timestamps == YNDK_YES) && (!is_a_real_hardware_device)) {
            debug(2, "alsa: precision delay timing is not available because it's not definitely a "
                     "hardware device.");
          } else {
            debug(2, "alsa: precision delay timing is not available.");
          }
          precision_delay_available_status = YNDK_NO;
        }
      }
    }
  }
  return (precision_delay_available_status == YNDK_YES);
}

int alsa_characteristics_already_listed = 0;

snd_pcm_uframes_t period_size_requested, buffer_size_requested;
int set_period_size_request, set_buffer_size_request;

uint64_t frames_sent_for_playing;

// set to true if there has been a discontinuity between the last reported frames_sent_for_playing
// and the present reported frames_sent_for_playing
// Note that it will be set when the device is opened, as any previous figures for
// frames_sent_for_playing (which Shairport Sync might hold) would be invalid.
int frames_sent_break_occurred;

// if a device name ends in ",DEV=0", drop it. Then if it also begins with "CARD=", drop that too.
static void simplify_and_printf_device_name(char *device_name) {
  if (strstr(device_name, ",DEV=0") == device_name + strlen(device_name) - strlen(",DEV=0")) {
    char *shortened_device_name = str_replace(device_name, ",DEV=0", "");
    char *simplified_device_name = str_replace(shortened_device_name, "CARD=", "");
    printf("      \"%s\"\n", simplified_device_name);
    free(simplified_device_name);
    free(shortened_device_name);
  } else {
    printf("      \"%s\"\n", device_name);
  }
}

static void help(void) {
  printf("    -d output-device    set the output device, default is \"default\".\n"
         "    -c mixer-control    set the mixer control name, default is to use no mixer.\n"
         "    -m mixer-device     set the mixer device, default is the output device.\n"
         "    -i mixer-index      set the mixer index, default is 0.\n");
  // look for devices with a name prefix of hw: or hdmi:
  int card_number = -1;
  snd_card_next(&card_number);

  if (card_number < 0) {
    printf("      no hardware output devices found.\n");
  }

  int at_least_one_device_found = 0;
  while (card_number >= 0) {
    void **hints;
    char *hdmi_str = NULL;
    char *hw_str = NULL;
    if (snd_device_name_hint(card_number, "pcm", &hints) == 0) {
      void **device_on_card_hints = hints;
      while (*device_on_card_hints != NULL) {
        char *device_on_card_name = snd_device_name_get_hint(*device_on_card_hints, "NAME");
        if ((strstr(device_on_card_name, "hw:") == device_on_card_name) && (hw_str == NULL))
          hw_str = strdup(device_on_card_name);
        if ((strstr(device_on_card_name, "hdmi:") == device_on_card_name) && (hdmi_str == NULL))
          hdmi_str = strdup(device_on_card_name);
        free(device_on_card_name);
        device_on_card_hints++;
      }
      snd_device_name_free_hint(hints);
      if ((hdmi_str != NULL) || (hw_str != NULL)) {
        if (at_least_one_device_found == 0) {
          printf("    hardware output devices:\n");
          at_least_one_device_found = 1;
        }
      }
      if (hdmi_str != NULL) {
        simplify_and_printf_device_name(hdmi_str);
      } else if (hw_str != NULL) {
        simplify_and_printf_device_name(hw_str);
      }
      if (hdmi_str != NULL)
        free(hdmi_str);
      if (hw_str != NULL)
        free(hw_str);
    }
    snd_card_next(&card_number);
  }
  if (at_least_one_device_found == 0)
    printf("    no hardware output devices found.\n");
}

void set_alsa_out_dev(char *dev) {
  alsa_out_dev = dev;
  if (hw_alsa_out_dev != NULL)
    free(hw_alsa_out_dev);
  hw_alsa_out_dev = str_replace(alsa_out_dev, "hdmi:", "hw:");
}

// assuming pthread cancellation is disabled
// returns zero of all is okay, a Unx error code if there's a problem
static int open_mixer() {
  int response = 0;
  if (alsa_mix_ctrl != NULL) {
    debug(3, "Open Mixer");
    snd_mixer_selem_id_alloca(&alsa_mix_sid);
    snd_mixer_selem_id_set_index(alsa_mix_sid, alsa_mix_index);
    snd_mixer_selem_id_set_name(alsa_mix_sid, alsa_mix_ctrl);

    if ((response = snd_mixer_open(&alsa_mix_handle, 0)) < 0) {
      debug(1, "Failed to open mixer");
    } else {
      debug(3, "Mixer device name is \"%s\".", alsa_mix_dev);
      if ((response = snd_mixer_attach(alsa_mix_handle, alsa_mix_dev)) < 0) {
        debug(1, "Failed to attach mixer");
      } else {
        if ((response = snd_mixer_selem_register(alsa_mix_handle, NULL, NULL)) < 0) {
          debug(1, "Failed to register mixer element");
        } else {
          if ((response = snd_mixer_load(alsa_mix_handle)) < 0) {
            debug(1, "Failed to load mixer element");
          } else {
            debug(3, "Mixer control is \"%s\",%d.", alsa_mix_ctrl, alsa_mix_index);
            alsa_mix_elem = snd_mixer_find_selem(alsa_mix_handle, alsa_mix_sid);
            if (!alsa_mix_elem) {
              warn("failed to find mixer control \"%s\",%d.", alsa_mix_ctrl, alsa_mix_index);
              response = -ENXIO; // don't let this be ENODEV!
            }
          }
        }
      }
    }
  }
  return response;
}

// assuming pthread cancellation is disabled
static int close_mixer() {
  int ret = 0;
  if (alsa_mix_handle) {
    ret = snd_mixer_close(alsa_mix_handle);
    alsa_mix_handle = NULL;
  }
  return ret;
}

// assuming pthread cancellation is disabled
static int do_snd_mixer_selem_set_playback_dB_all(snd_mixer_elem_t *mix_elem, double vol) {
  int response = 0;
  if ((response = snd_mixer_selem_set_playback_dB_all(mix_elem, vol, 0)) != 0) {
    debug(1, "Can't set playback volume accurately to %f dB.", vol);
    if ((response = snd_mixer_selem_set_playback_dB_all(mix_elem, vol, -1)) != 0)
      if ((response = snd_mixer_selem_set_playback_dB_all(mix_elem, vol, 1)) != 0)
        debug(1, "Could not set playback dB volume on the mixer.");
  }
  return response;
}

// assuming pthread cancellation is disabled
static int actual_open_alsa_device() {
  // the alsa mutex is already acquired when this is called
  int result = 0;
  if (current_encoded_output_format != 0) {

    unsigned int rate = RATE_FROM_ENCODED_FORMAT(current_encoded_output_format);
    sps_format_t format = (sps_format_t)FORMAT_FROM_ENCODED_FORMAT(current_encoded_output_format);
    unsigned int channels = CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format);

    int ret, dir = 0;
    // unsigned int actual_sample_rate; // this will be given the rate requested and will be given
    // the actual rate snd_pcm_uframes_t frames = 441 * 10;
    snd_pcm_uframes_t actual_buffer_length_in_frames;
    snd_pcm_access_t access;

    ret = snd_pcm_open(&alsa_handle, alsa_out_dev, SND_PCM_STREAM_PLAYBACK, 0);
    // EHOSTDOWN seems to signify that it's a PipeWire pseudo device that can't be accessed by this
    // user. So, try the first device ALSA device and log it.
    if ((ret == -EHOSTDOWN) && (strcmp(alsa_out_dev, "default") == 0)) {
      ret = snd_pcm_open(&alsa_handle, "hw:0", SND_PCM_STREAM_PLAYBACK, 0);
      if ((ret == 0) || (ret == -EBUSY)) {
        // being busy should be okay
        inform("the default ALSA device is inaccessible -- \"hw:0\" used instead.");
        set_alsa_out_dev("hw:0");
      }
    }
    if (ret == 0) {
      if (alsa_handle_status == -EBUSY)
        warn("The output device \"%s\" is no longer busy and will be used by Shairport Sync.",
             alsa_out_dev);
      alsa_handle_status = ret; // all cool
    } else {
      alsa_handle = NULL; // to be sure to be sure
      if (ret == -EBUSY) {
        if (alsa_handle_status != -EBUSY)
          warn("The output device \"%s\" is busy and can't be used by Shairport Sync at present.",
               alsa_out_dev);
        debug(2, "the alsa output_device \"%s\" is busy.", alsa_out_dev);
      }
      alsa_handle_status = ret;
      frames_sent_break_occurred = 1;
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
      die("alsa: access type not available for device \"%s\": %s", alsa_out_dev, snd_strerror(ret));
      return ret;
    }

    ret = snd_pcm_hw_params_set_channels(alsa_handle, alsa_params, channels);
    if (ret < 0) {
      die("alsa: %u channels not available for device \"%s\": %s", channels, alsa_out_dev,
          snd_strerror(ret));
      return ret;
    }

    snd_pcm_format_t sf = fr[format].alsa_code;
    ret = snd_pcm_hw_params_set_format(alsa_handle, alsa_params, sf);
    if (ret < 0) {
      die("alsa: sample format %s not available for device \"%s\": %s",
          sps_format_description_string(format), alsa_out_dev, snd_strerror(ret));
      return ret;
    }

    // on some devices, it seems that setting the rate directly doesn't work.
    dir = 0;
    unsigned int actual_sample_rate = rate;
    ret = snd_pcm_hw_params_set_rate_near(alsa_handle, alsa_params, &actual_sample_rate, &dir);

    if ((ret < 0) || (actual_sample_rate != rate)) {
      die("alsa: The frame rate of %i frames per second is not available for playback: %s", rate,
          snd_strerror(ret));
      return ret;
    }

    if (set_period_size_request != 0) {
      debug(1, "Attempting to set the period size to %lu", period_size_requested);
      ret = snd_pcm_hw_params_set_period_size_near(alsa_handle, alsa_params, &period_size_requested,
                                                   &dir);
      if (ret < 0) {
        warn("alsa: cannot set period size of %lu: %s", period_size_requested, snd_strerror(ret));
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
      ret =
          snd_pcm_hw_params_set_buffer_size_near(alsa_handle, alsa_params, &buffer_size_requested);
      if (ret < 0) {
        warn("alsa: cannot set buffer size of %lu: %s", buffer_size_requested, snd_strerror(ret));
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
      die("alsa: Unable to set hw parameters for device \"%s\": %s.", alsa_out_dev,
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

    use_monotonic_clock = snd_pcm_hw_params_is_monotonic(alsa_params);

    ret = snd_pcm_hw_params_get_buffer_size(alsa_params, &actual_buffer_length_in_frames);
    if (ret < 0) {
      warn("alsa: unable to get hw buffer length for device \"%s\": %s.", alsa_out_dev,
           snd_strerror(ret));
      return ret;
    }

    ret = snd_pcm_sw_params_current(alsa_handle, alsa_swparams);
    if (ret < 0) {
      warn("alsa: unable to get current sw parameters for device \"%s\": "
           "%s.",
           alsa_out_dev, snd_strerror(ret));
      return ret;
    }

    ret = snd_pcm_sw_params_set_tstamp_mode(alsa_handle, alsa_swparams, SND_PCM_TSTAMP_ENABLE);
    if (ret < 0) {
      warn("alsa: can't enable timestamp mode of device: \"%s\": %s.", alsa_out_dev,
           snd_strerror(ret));
      return ret;
    }

    /* write the sw parameters */
    ret = snd_pcm_sw_params(alsa_handle, alsa_swparams);
    if (ret < 0) {
      warn("alsa: unable to set software parameters of device: \"%s\": %s.", alsa_out_dev,
           snd_strerror(ret));
      return ret;
    }

    ret = snd_pcm_prepare(alsa_handle);
    if (ret < 0) {
      warn("alsa: unable to prepare the device: \"%s\": %s.", alsa_out_dev, snd_strerror(ret));
      return ret;
    }

    if (config.use_precision_timing == YNA_YES)
      delay_and_status = precision_delay_and_status;
    else if (config.use_precision_timing == YNA_AUTO) {
      if (precision_delay_available()) {
        delay_and_status = precision_delay_and_status;
        debug(2, "alsa: precision timing selected for \"auto\" mode");
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
      int direction;
      snd_pcm_uframes_t frames;

      debug(log_level, "PCM handle name = '%s'", snd_pcm_name(alsa_handle));

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

      snd_pcm_hw_params_get_rate(alsa_params, &uval, &direction);
      switch (direction) {
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

      if ((snd_pcm_hw_params_get_rate_numden(alsa_params, &uval, &uval2) == 0) && (uval2 != 0))
        // watch for a divide by zero too!
        debug(log_level, "  precise (rational) rate = %.3f frames per second (i.e. %u/%u).", (1.0 * uval) / uval2, uval,
              uval2);
      else
        debug(log_level, "  precise (rational) rate information unavailable.");

      snd_pcm_hw_params_get_period_time(alsa_params, &uval, &direction);
      switch (direction) {
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

      snd_pcm_hw_params_get_period_size(alsa_params, &frames, &direction);
      switch (direction) {
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

      snd_pcm_hw_params_get_buffer_time(alsa_params, &uval, &direction);
      switch (direction) {
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
      switch (direction) {
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

      snd_pcm_hw_params_get_periods(alsa_params, &uval, &direction);
      switch (direction) {
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
    stall_monitor_new_frame_count = 0;
    stall_monitor_new_frame_count_time = 0;
  } else {
    debug(1, "no current_alsa_configuration");
    result = -1;
  }
  return result;
}

static int open_alsa_device() {
  int result;
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  result = actual_open_alsa_device();
  if (result != 0)
    debug(1, "Error in open_alsa_device: %d.", result);
  pthread_setcancelstate(oldState, NULL);
  return result;
}

static int prepare_mixer() {
  int response = 0;
  // do any alsa device initialisation (general case)
  // at present, this is only needed if a hardware mixer is being used
  // if there's a hardware mixer, it needs to be initialised before use
  if (alsa_mix_ctrl == NULL) {
    audio_alsa.volume = NULL;
    audio_alsa.mute = NULL;
    output_parameters.volume_range = NULL; // until we know, we won't offer a volume range
  } else {
    debug(2, "alsa: hardware mixer prepare");
    int oldState;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable

    if (alsa_mix_dev == NULL)
      alsa_mix_dev = hw_alsa_out_dev;

    // Now, start trying to initialise the alsa device with the settings
    // obtained
    pthread_cleanup_debug_mutex_lock(&alsa_mixer_mutex, 1000, 1);
    if (open_mixer() == 0) {
      if (snd_mixer_selem_get_playback_volume_range(alsa_mix_elem, &alsa_mix_minv, &alsa_mix_maxv) <
          0) {
        debug(1, "Can't read mixer's [linear] min and max volumes.");
      } else {
        if (snd_mixer_selem_get_playback_dB_range(alsa_mix_elem, &alsa_mix_mindb,
                                                  &alsa_mix_maxdb) == 0) {

          if (alsa_mix_mindb == SND_CTL_TLV_DB_GAIN_MUTE) {
            // For instance, the Raspberry Pi does this
            debug(2, "Lowest dB value is a mute");
            mixer_volume_setting_gives_mute = 1;
            alsa_mix_mute = SND_CTL_TLV_DB_GAIN_MUTE; // this may not be
                                                      // necessary -- it's
                                                      // always
            // going to be SND_CTL_TLV_DB_GAIN_MUTE, right?
            // debug(1, "Try minimum volume + 1 as lowest true attenuation
            // value");

            // now we need to find a lowest dB value that isn't a mute
            // so we'll work from alsa_mix_minv upwards until we get a db value
            // that is not SND_CTL_TLV_DB_GAIN_MUTE

            long cv;
            for (cv = alsa_mix_minv;
                 cv <= alsa_mix_maxv && (alsa_mix_mindb == SND_CTL_TLV_DB_GAIN_MUTE); cv++) {
              if (snd_mixer_selem_ask_playback_vol_dB(alsa_mix_elem, cv, &alsa_mix_mindb) != 0)
                debug(1, "Can't get dB value corresponding to a minimum volume "
                         "+ 1.");
            }
          }
          debug(3, "Hardware mixer has dB volume from %f to %f.", (1.0 * alsa_mix_mindb) / 100.0,
                (1.0 * alsa_mix_maxdb) / 100.0);

          audio_alsa.volume = &volume; // insert the volume function now we
                                       // know it can do dB stuff
          volume_range.minimum_volume_dB = alsa_mix_mindb;
          volume_range.maximum_volume_dB = alsa_mix_maxdb;
          output_parameters.volume_range = &volume_range;

        } else {
          // use the linear scale and do the db conversion ourselves
          warn("The hardware mixer specified -- \"%s\" -- does not have "
               "a dB volume scale.",
               alsa_mix_ctrl);

          if ((response = snd_ctl_open(&ctl, alsa_mix_dev, 0)) < 0) {
            warn("Cannot open control \"%s\"", alsa_mix_dev);
          }
          if ((response = snd_ctl_elem_id_malloc(&elem_id)) < 0) {
            debug(1, "Cannot allocate memory for control \"%s\"", alsa_mix_dev);
            elem_id = NULL;
          } else {
            snd_ctl_elem_id_set_interface(elem_id, SND_CTL_ELEM_IFACE_MIXER);
            snd_ctl_elem_id_set_name(elem_id, alsa_mix_ctrl);

            if (snd_ctl_get_dB_range(ctl, elem_id, &alsa_mix_mindb, &alsa_mix_maxdb) == 0) {
              debug(1,
                    "alsa: hardware mixer \"%s\" selected, with dB volume "
                    "from %f to %f.",
                    alsa_mix_ctrl, (1.0 * alsa_mix_mindb) / 100.0, (1.0 * alsa_mix_maxdb) / 100.0);
              has_softvol = 1;
              audio_alsa.volume = &volume; // insert the volume function now we
                                           // know it can do dB stuff
              volume_range.minimum_volume_dB = alsa_mix_mindb;
              volume_range.maximum_volume_dB = alsa_mix_maxdb;
              output_parameters.volume_range = &volume_range;
            } else {
              debug(1, "Cannot get the dB range from the volume control \"%s\"", alsa_mix_ctrl);
            }
          }
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
      if (response == 0)
        response = close_mixer();
    }
    debug_mutex_unlock(&alsa_mixer_mutex, 3); // release the mutex
    pthread_cleanup_pop(0);
    pthread_setcancelstate(oldState, NULL);
  }
  return response;
}

static int alsa_device_init() { return prepare_mixer(); }

static void
snd_error_quiet(__attribute__((unused)) const char *file, __attribute__((unused)) int line,
                __attribute__((unused)) const char *func, __attribute__((unused)) int err,
                __attribute__((unused)) const char *fmt, __attribute__((unused)) va_list arg) {
  // return NULL;
}

static int init(int argc, char **argv) {
  snd_lib_error_set_handler((snd_lib_error_handler_t)snd_error_quiet);
  current_encoded_output_format = 0;
  // for debugging
  snd_output_stdio_attach(&output, stdout, 0);
  // debug(2,"audio_alsa init called.");
  // int response = 0; // this will be what we return to the caller.
  alsa_device_initialised = 0;
  const char *str;
  int value;
  // double dvalue;

  // set up default values first

  config.no_mmap = 1; // some devices don't implement this properly and crash with data is dropped
  alsa_backend_state = abm_disconnected; // startup state
  debug(2, "alsa: init() -- alsa_backend_state => abm_disconnected.");
  set_period_size_request = 0;
  set_buffer_size_request = 0;
  config.alsa_use_hardware_mute = 0; // don't use it by default

  config.audio_backend_latency_offset = 0;
  config.audio_backend_buffer_desired_length = 0.200;
  config.audio_backend_buffer_interpolation_threshold_in_seconds =
      0.060; // below this, basic interpolation will be used to save time.
  config.alsa_maximum_stall_time = 0.200; // 200 milliseconds -- if it takes longer, it's a problem
  config.disable_standby_mode_silence_threshold =
      0.040; // start sending silent frames if the delay goes below this time

  // on slower single-core machines, it doesn't make sense to make this much less than about 40 ms,
  // as, whatever the setting, the scheduler may let it sleep for much longer -- up to 80
  // milliseconds.
  config.disable_standby_mode_silence_scan_interval = 0.030; // check silence threshold this often

  config.disable_standby_mode = disable_standby_off;
  config.keep_dac_busy = 0;
  config.use_precision_timing = YNA_NO;

  disable_standby_mode_default_format = SPS_FORMAT_S32_LE;
#ifdef CONFIG_AIRPLAY_2
  disable_standby_mode_default_rate = 48000;
#else
  disable_standby_mode_default_rate = 44100;
#endif
  disable_standby_mode_default_channels = 2;

  // get settings from settings file first, allow them to be overridden by
  // command line options

  // get settings from settings file, passing in defaults for format_set, rate_set and channel_set
  // Note, these options may be in the "general" stanza or the named stanza
#ifdef CONFIG_FFMPEG
  parse_audio_options("alsa", SPS_FORMAT_SET, SPS_RATE_SET, SPS_CHANNEL_SET);
#else
  parse_audio_options("alsa", SPS_FORMAT_NON_FFMPEG_SET, SPS_RATE_NON_FFMPEG_SET,
                      SPS_CHANNNEL_NON_FFMPEG_SET);
#endif

  if (config.cfg != NULL) {
    double dvalue;

    /* Get the Output Device Name. */
    if (config_lookup_non_empty_string(config.cfg, "alsa.output_device", &str)) {
      alsa_out_dev = (char *)str;
    }

    /* Get the Mixer Type setting. */

    if (config_lookup_non_empty_string(config.cfg, "alsa.mixer_type", &str)) {
      inform("The alsa mixer_type setting is deprecated and has been ignored. "
             "FYI, using the \"mixer_control_name\" setting automatically "
             "chooses a hardware mixer.");
    }

    /* Get the Mixer Device Name. */
    if (config_lookup_non_empty_string(config.cfg, "alsa.mixer_device", &str)) {
      alsa_mix_dev = (char *)str;
    }

    /* Get the Mixer Control Name. */
    if (config_lookup_non_empty_string(config.cfg, "alsa.mixer_control_name", &str)) {
      alsa_mix_ctrl = (char *)str;
    }

    // Get the Mixer Control Index
    if (config_lookup_int(config.cfg, "alsa.mixer_control_index", &value)) {
      alsa_mix_index = value;
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
             "\"no\". It is set to \"no\".", str);
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
             "\"no\". It is set to \"no\".", str);
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
             "\"no\". It is set to \"no\".", str);
        config.alsa_use_hardware_mute = 0;
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
             "It remains set to \"yes\".", str);
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

    /* Get the optional disable_standby_mode_silence_threshold setting. */
    if (config_lookup_float(config.cfg, "alsa.disable_standby_mode_silence_threshold", &dvalue)) {
      if (dvalue < 0.0) {
        warn("Invalid alsa disable_standby_mode_silence_threshold setting \"%f\". It "
             "must be greater than 0. Default is \"%f\". No setting is made.",
             dvalue, config.disable_standby_mode_silence_threshold);
      } else {
        config.disable_standby_mode_silence_threshold = dvalue;
      }
    }

    /* Get the optional disable_standby_mode_silence_scan_interval setting. */
    if (config_lookup_float(config.cfg, "alsa.disable_standby_mode_silence_scan_interval",
                            &dvalue)) {
      if (dvalue < 0.0) {
        warn("Invalid alsa disable_standby_mode_silence_scan_interval setting \"%f\". It "
             "must be greater than 0. Default is \"%f\". No setting is made.",
             dvalue, config.disable_standby_mode_silence_scan_interval);
      } else {
        config.disable_standby_mode_silence_scan_interval = dvalue;
      }
    }

    /* Get the optional disable_standby_mode setting. */
    if (config_lookup_string(config.cfg, "alsa.disable_standby_mode", &str)) {
      if ((strcasecmp(str, "no") == 0) || (strcasecmp(str, "off") == 0) ||
          (strcasecmp(str, "never") == 0))
        config.disable_standby_mode = disable_standby_off;
      else if ((strcasecmp(str, "yes") == 0) || (strcasecmp(str, "on") == 0) ||
               (strcasecmp(str, "always") == 0)) {
        config.disable_standby_mode = disable_standby_always;
        config.keep_dac_busy = 1;
      } else if (strcasecmp(str, "auto") == 0)
        config.disable_standby_mode = disable_standby_auto;
      else {
        warn("Invalid disable_standby_mode option choice \"%s\". It should be "
             "\"always\", \"auto\" or \"never\". "
             "It remains set to \"never\".",
             str);
      }
    }

    /* Get the optional disable_standby_mode_default_rate. */
    if (config_lookup_int(config.cfg, "alsa.disable_standby_mode_default_rate", &value)) {
      if (value < 0) {
        warn("Invalid alsa disable_standby_mode_default_rate setting %d. It "
             "must be greater than 0. Default is %d. No setting is made.",
             value, disable_standby_mode_default_rate);
      } else {
        disable_standby_mode_default_rate = value;
      }
    }

    /* Get the optional disable_standby_mode_default_channels. */
    if (config_lookup_int(config.cfg, "alsa.disable_standby_mode_default_channels", &value)) {
      if (value < 0) {
        warn("Invalid alsa disable_standby_mode_default_channels setting %d. It "
             "must be greater than 0. Default is %d. No setting is made.",
             value, disable_standby_mode_default_channels);
      } else {
        disable_standby_mode_default_channels = value;
      }
    }

    if (config_lookup_string(config.cfg, "alsa.use_precision_timing", &str)) {
      if ((strcasecmp(str, "no") == 0) || (strcasecmp(str, "off") == 0) ||
          (strcasecmp(str, "never") == 0))
        config.use_precision_timing = YNA_NO;
      else if ((strcasecmp(str, "yes") == 0) || (strcasecmp(str, "on") == 0) ||
               (strcasecmp(str, "always") == 0)) {
        config.use_precision_timing = YNA_YES;
        config.keep_dac_busy = 1;
      } else if (strcasecmp(str, "auto") == 0)
        config.use_precision_timing = YNA_AUTO;
      else {
        warn("Invalid use_precision_timing option choice \"%s\". It should be "
             "\"yes\", \"auto\" or \"no\". "
             "It remains set to \"%s\".", str,
             config.use_precision_timing == YNA_NO     ? "no"
             : config.use_precision_timing == YNA_AUTO ? "auto"
                                                       : "yes");
      }
      inform("Note: the \"alsa\" setting \"use_precision_timing\" is deprecated and will be "
             "removed in a future update.");
    }
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

  debug(2, "alsa: output device name is \"%s\".", alsa_out_dev);

  // now, we need a version of the alsa_out_dev that substitutes "hw:" for "hdmi" if it's
  // there. It seems hw:1 would be a valid devcie name where hdmi:1 would not

  if (alsa_out_dev != NULL)
    hw_alsa_out_dev = str_replace(alsa_out_dev, "hdmi:", "hw:");

  debug(2, "alsa: disable_standby_mode is \"%s\".",
        config.disable_standby_mode == disable_standby_off      ? "never"
        : config.disable_standby_mode == disable_standby_always ? "always"
                                                                : "auto");
  debug(2, "alsa: disable_standby_mode_silence_threshold is %f seconds.",
        config.disable_standby_mode_silence_threshold);
  debug(2, "alsa: disable_standby_mode_silence_scan_interval is %f seconds.",
        config.disable_standby_mode_silence_scan_interval);

  stall_monitor_error_threshold =
      (uint64_t)(config.alsa_maximum_stall_time * 1000000000); // stall time max to nanoseconds;

  // so, now, if the option to keep the DAC running has been selected, start a
  // thread to monitor the
  // length of the queue
  // if the queue gets too short, stuff it with silence

  return named_pthread_create_with_priority(&alsa_buffer_monitor_thread, 4,
                                            &alsa_buffer_monitor_thread_code, NULL, "alsa_buf_mon");

  return 0;
}

static int check_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  // we know that the format/rate/channel count are legitimate but the combination
  // may not be permitted.
  // now see if the individual formats, rates and channel count are permissible
  sps_rate_t r = SPS_RATE_LOWEST;
  int found = 0;
  // we know the rate is there, we just have to find it.
  while ((r <= SPS_RATE_HIGHEST) && (found == 0)) {
    if ((sps_rate_actual_rate(r) == rate) && ((config.rate_set & (1 << r)) != 0))
      found = 1;
    else
      r++;
  }

  int response = permissible_configurations[r][format][channels];

  if (response != 0)
    debug(3, "check %u/%s/%u returns %d.", rate, sps_format_description_string(format), channels,
          response);
  return response;
}

/*
static int check_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  int response = response = check_settings(format, rate, channels);
  debug(3, "alsa: configuration: %u/%s/%u %s.", rate, sps_format_description_string(format),
        channels, response == 0 ? "is okay" : "can not be configured");
  return response;
}
*/

static char *get_channel_map_str() {
  if (alsa_handle != NULL) {

    snd_pcm_chmap_t *channel_map = snd_pcm_get_chmap(alsa_handle);
    if (channel_map) {

      int j, k;
      // check for duplicates and replace subsequent duplicates with
      // SND_CHMAP_UNKNOWN

      for (j = 0; j < (int)channel_map->channels; j++) {
        for (k = 0; k < j; k++) {
          if ((channel_map->pos[k] != SND_CHMAP_UNKNOWN) &&
              (channel_map->pos[k] == channel_map->pos[j])) {
            debug(3,
                  "alsa: There is an error in the built-in %u channel map of the "
                  "output device:"
                  " channel %d has the same name as channel "
                  "%d (\"%s\"). Both names have been changed to \"UNKNOWN\".",
                  channel_map->channels, j, k, snd_pcm_chmap_name(channel_map->pos[k]));
            channel_map->pos[j] = SND_CHMAP_UNKNOWN;
            channel_map->pos[k] = SND_CHMAP_UNKNOWN;
          }
        }
      }

      unsigned int i;
      for (i = 0; i < channel_map->channels; i++) {
        debug(3, "channel %d is %d, name: \"%s\", long name: \"%s\".", i, channel_map->pos[i],
              snd_pcm_chmap_name(channel_map->pos[i]),
              snd_pcm_chmap_long_name(channel_map->pos[i]));
      }
      if (snd_pcm_chmap_print(channel_map, sizeof(public_channel_map), public_channel_map) < 0)
        public_channel_map[0] = '\0'; // if there's any problem
      debug(3,
            "channel count: %d, channel name list: "
            "\"%s\".",
            channel_map->channels, public_channel_map);
      free(channel_map);
    } else {
      debug(2, "no channel map -- if it's two-channel, assume standard stereo.");
      if (CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format) == 2) {
        // if it's a two-channel configuration, assume it's stereo and return
        // a standard mapping
        snprintf(public_channel_map, sizeof(public_channel_map) - 1, "FL FR");
      }
    }
  }
  return public_channel_map;
}

static int configure(int32_t requested_encoded_format, char **channel_map) {
  int response = 0;
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  pthread_cleanup_debug_mutex_lock(&alsa_mutex, 200000, 0);
  if (current_encoded_output_format != requested_encoded_format) {
    if (current_encoded_output_format == 0)
      debug(2, "alsa: setting output configuration to %s.",
            short_format_description(requested_encoded_format));
    else
      // note -- can't use short_format_description twice in one call because it returns the same
      // string buffer each time
      debug(3, "alsa: changing output configuration to %s.",
            short_format_description(requested_encoded_format));
    do_close();
    current_encoded_output_format = requested_encoded_format;
    response = do_open();
  }
  if ((response == 0) && (channel_map != NULL)) {
    *channel_map = get_channel_map_str();
  }
  debug_mutex_unlock(&alsa_mutex, 0);
  pthread_cleanup_pop(0);
  pthread_setcancelstate(oldState, NULL);
  if (response != 0)
    debug(1, "alsa: could not open the output device with configuration %s",
          short_format_description(requested_encoded_format));
  return response;
}

static void deinit(void) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  debug(2, "audio_alsa deinit called.");
  pthread_cleanup_debug_mutex_lock(&alsa_mutex, 10000, 1);
  if (alsa_handle != NULL) {
    debug(3, "alsa: closing the output device.");
    do_close();
  } else {
    debug(3, "alsa: output device already closed.");
  }
  pthread_cleanup_pop(1); // release the mutex
  debug(2, "Cancel buffer monitor thread.");
  pthread_cancel(alsa_buffer_monitor_thread);
  debug(2, "Join buffer monitor thread.");
  pthread_join(alsa_buffer_monitor_thread, NULL);
  debug(2, "Joined buffer monitor thread.");
  pthread_setcancelstate(oldState, NULL);
  if (hw_alsa_out_dev != NULL)
    free(hw_alsa_out_dev);
}

static int prepare() { return get_permissible_configuration_settings(); }

static int set_mute_state() {
  int response = 1; // some problem expected, e.g. no mixer or not allowed to use it or disconnected
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  pthread_cleanup_debug_mutex_lock(&alsa_mixer_mutex, 10000, 0);
  if ((alsa_backend_state != abm_disconnected) && (config.alsa_use_hardware_mute == 1) &&
      (open_mixer() == 0)) {
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
  pthread_cleanup_pop(0);                   // release the mutex
  pthread_setcancelstate(oldState, NULL);
  return response;
}

static output_parameters_t *parameters() { return &output_parameters; }

static void start(__attribute__((unused)) int i_sample_rate,
                  __attribute__((unused)) int i_sample_format) {

  if (alsa_device_initialised == 0) {
    debug(2, "alsa: start() calling alsa_device_init.");
    alsa_device_init();
    alsa_device_initialised = 1;
  }
}

static int standard_delay_and_status(snd_pcm_state_t *state, snd_pcm_sframes_t *delay,
                                     yndk_type *using_update_timestamps) {
  int ret = alsa_handle_status;
  if (using_update_timestamps)
    *using_update_timestamps = YNDK_NO;

  snd_pcm_state_t state_temp = SND_PCM_STATE_DISCONNECTED;
  snd_pcm_sframes_t delay_temp = 0;
  if (alsa_handle != NULL) {
    state_temp = snd_pcm_state(alsa_handle);
    if ((state_temp == SND_PCM_STATE_RUNNING) || (state_temp == SND_PCM_STATE_DRAINING)) {
      ret = snd_pcm_delay(alsa_handle, &delay_temp);
    } else {
      // not running, thus no delay information, thus can't check for frame
      // rates
      // frame_index = 0; // we'll be starting over...
      // measurement_data_is_valid = 0;
      // delay_temp = 0;
      ret = 0;
    }
  }

  if (delay != NULL)
    *delay = delay_temp;
  if (state != NULL)
    *state = state_temp;
  return ret;
}

static int precision_delay_and_status(snd_pcm_state_t *state, snd_pcm_sframes_t *delay,
                                      yndk_type *using_update_timestamps) {
  snd_pcm_state_t state_temp = SND_PCM_STATE_DISCONNECTED;
  snd_pcm_sframes_t delay_temp = 0;
  if (using_update_timestamps)
    *using_update_timestamps = YNDK_DONT_KNOW;
  int ret = alsa_handle_status;

  snd_pcm_status_t *alsa_snd_pcm_status;
  snd_pcm_status_alloca(&alsa_snd_pcm_status);

  struct timespec tn;                // time now
  snd_htimestamp_t update_timestamp; // actually a struct timespec
  if (alsa_handle != NULL) {
    ret = snd_pcm_status(alsa_handle, alsa_snd_pcm_status);
    if (ret == 0) {
      snd_pcm_status_get_htstamp(alsa_snd_pcm_status, &update_timestamp);

      /*
      // must be 1.1 or later to use snd_pcm_status_get_driver_htstamp
      #if SND_LIB_MINOR != 0
          snd_htimestamp_t driver_htstamp;
          snd_pcm_status_get_driver_htstamp(alsa_snd_pcm_status, &driver_htstamp);
          uint64_t driver_htstamp_ns = driver_htstamp.tv_sec;
          driver_htstamp_ns = driver_htstamp_ns * 1000000000;
          driver_htstamp_ns = driver_htstamp_ns + driver_htstamp.tv_nsec;
          debug(1,"driver_htstamp: %f.", driver_htstamp_ns * 0.000000001);
      #endif
      */

      state_temp = snd_pcm_status_get_state(alsa_snd_pcm_status);

      if ((state_temp == SND_PCM_STATE_RUNNING) || (state_temp == SND_PCM_STATE_DRAINING)) {

        uint64_t update_timestamp_ns = update_timestamp.tv_sec;
        update_timestamp_ns = update_timestamp_ns * 1000000000;
        update_timestamp_ns = update_timestamp_ns + update_timestamp.tv_nsec;

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
            debug(2, "alsa: update timestamps unavailable");
            delay_type_notified = 1;
          }
        } else {
          // diagnostic
          if (delay_type_notified != 0) {
            debug(2, "alsa: update timestamps available");
            delay_type_notified = 0;
          }
        }

        if (update_timestamp_ns == 0) {
          ret = snd_pcm_delay(alsa_handle, &delay_temp);
        } else {
          delay_temp = snd_pcm_status_get_delay(alsa_snd_pcm_status);

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

          // uint64_t time_now_ns = tn.tv_sec * (uint64_t)1000000000 + tn.tv_nsec;
          uint64_t time_now_ns = tn.tv_sec;
          time_now_ns = time_now_ns * 1000000000;
          time_now_ns = time_now_ns + tn.tv_nsec;

          // if the delay is not zero and if stall_monitor_new_frame_count_time is non-zero, then
          // if the delay is longer than the stall threshold
          // and the delay is the same as it was, a stall has occurred.

          if ((stall_monitor_new_frame_count_time != 0) && (delay_temp != 0)) {

            uint64_t time_since_last_measurement = time_now_ns - stall_monitor_new_frame_count_time;
            if ((time_since_last_measurement > stall_monitor_error_threshold) &&
                (stall_monitor_new_frame_count == delay_temp)) {
              debug(1, "DAC has stalled for %f seconds with a frame count of %ld.",
                    time_since_last_measurement * 1E-9, delay_temp);
              debug(1, "time_now_ns: %" PRIu64 ", stall_monitor_new_frame_count_time: %" PRIu64 ".",
                    time_now_ns, stall_monitor_new_frame_count_time);
            }
          }

          // for the next time...
          stall_monitor_new_frame_count = delay_temp;
          stall_monitor_new_frame_count_time = time_now_ns;

          /*
          // see if it's stalled

          if ((stall_monitor_start_time != 0) && (stall_monitor_frame_count == delay_temp)) {
            // hasn't outputted anything since the last call to delay()

            if (((update_timestamp_ns - stall_monitor_start_time) >
                 stall_monitor_error_threshold) ||
                ((time_now_ns - stall_monitor_start_time) > stall_monitor_error_threshold)) {
              debug(1,
                    "DAC seems to have stalled with time_now_ns: %" PRIu64
                    ", update_timestamp_ns: %" PRIu64 ", stall_monitor_start_time %" PRIu64
                    ", stall_monitor_error_threshold %" PRIu64 ", delay_temp %u.",
                    time_now_ns, update_timestamp_ns, stall_monitor_start_time,
                    stall_monitor_error_threshold, delay_temp);
              // ret = sps_extra_code_output_stalled;
            }
          } // else {
            stall_monitor_start_time = update_timestamp_ns;
            stall_monitor_frame_count = delay_temp;
          // }
          */

          if (ret == 0) {
            uint64_t delta = time_now_ns - update_timestamp_ns;

            uint64_t frames_played_since_last_interrupt =
                RATE_FROM_ENCODED_FORMAT(current_encoded_output_format);
            frames_played_since_last_interrupt = frames_played_since_last_interrupt * delta;
            frames_played_since_last_interrupt = frames_played_since_last_interrupt / 1000000000;

            snd_pcm_sframes_t frames_played_since_last_interrupt_sized =
                frames_played_since_last_interrupt;
            if ((frames_played_since_last_interrupt_sized < 0) ||
                ((uint64_t)frames_played_since_last_interrupt_sized !=
                 frames_played_since_last_interrupt))
              debug(1,
                    "overflow resizing frames_played_since_last_interrupt %" PRIx64
                    " to frames_played_since_last_interrupt %lx.",
                    frames_played_since_last_interrupt, frames_played_since_last_interrupt_sized);
            delay_temp = delay_temp - frames_played_since_last_interrupt_sized;
          }
        }
      } else { // not running, thus no delay information, thus can't check for
               // stall
        delay_temp = 0;
        stall_monitor_new_frame_count_time = 0;
      }
    } else {
      debug(1, "alsa: can't get device's status -- error %d.", ret);
    }

  } else {
    debug(2, "alsa_handle is NULL in precision_delay_and_status!");
  }
  if (delay != NULL)
    *delay = delay_temp;
  if (state != NULL)
    *state = state_temp;
  debug(3, "precision_delay_and_status returning state: %d and delay %ld.", state_temp, delay_temp);
  return ret;
}

static int delay(long *the_delay) {
  // returns 0 if the device is in a valid state -- SND_PCM_STATE_RUNNING or
  // SND_PCM_STATE_PREPARED
  // or SND_PCM_STATE_DRAINING
  // and returns the actual delay if running or 0 if prepared in *the_delay

  // otherwise return an error code
  // the error code could be a Unix errno code or a snderror code, or
  // the sps_extra_code_output_stalled or the
  // sps_extra_code_output_state_cannot_make_ready codes
  int ret = 0;
  snd_pcm_sframes_t my_delay = 0;

  int oldState;

  snd_pcm_state_t state;

  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  pthread_cleanup_debug_mutex_lock(&alsa_mutex, 10000, 0);

  ret = delay_and_status(&state, &my_delay, NULL);

  debug_mutex_unlock(&alsa_mutex, 0);
  pthread_cleanup_pop(0);
  pthread_setcancelstate(oldState, NULL);

  if (the_delay != NULL)   // can't imagine why this might happen
    *the_delay = my_delay; // note: snd_pcm_sframes_t is a long

  return ret;
}

static int stats(uint64_t *raw_measurement_time, uint64_t *corrected_measurement_time,
                 uint64_t *the_delay, uint64_t *frames_sent_to_dac) {
  // returns 0 if the device is in a valid state -- SND_PCM_STATE_RUNNING or
  // SND_PCM_STATE_PREPARED
  // or SND_PCM_STATE_DRAINING.
  // returns the actual delay if running or 0 if prepared in *the_delay
  // returns the present value of frames_sent_for_playing
  // otherwise return a non-zero value
  int ret = 0;
  *the_delay = 0;

  int oldState;

  snd_pcm_state_t state;
  snd_pcm_sframes_t my_delay = 0; // this initialisation is to silence a clang warning

  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  pthread_cleanup_debug_mutex_lock(&alsa_mutex, 10000, 0);

  if (alsa_handle == NULL) {
    ret = alsa_handle_status;
  } else {
    *raw_measurement_time =
        get_absolute_time_in_ns(); // this is not conditioned ("disciplined") by NTP
    *corrected_measurement_time = get_monotonic_time_in_ns(); // this is ("disciplined") by NTP
    ret = delay_and_status(&state, &my_delay, NULL);
  }
  if (ret == 0)
    ret = frames_sent_break_occurred; // will be zero unless an error like an underrun occurred
  else
    ret = 1;                      // just indicate there was some kind of a break
  frames_sent_break_occurred = 0; // reset it.
  if (frames_sent_to_dac != NULL)
    *frames_sent_to_dac = frames_sent_for_playing;
  debug_mutex_unlock(&alsa_mutex, 0);
  pthread_cleanup_pop(0);
  pthread_setcancelstate(oldState, NULL);
  uint64_t hd = my_delay; // note: snd_pcm_sframes_t is a long
  *the_delay = hd;
  // if (ret != 0)
  //   debug(1, "frames_sent_break_occurred? value is %d.", ret);
  return ret;
}

static int do_play(void *buf, int samples) {
  // assuming the alsa_mutex has been acquired
  int ret = 0;
  if ((samples != 0) && (buf != NULL)) {

    int oldState;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable

    snd_pcm_state_t state;
    snd_pcm_sframes_t my_delay;
    ret = delay_and_status(&state, &my_delay, NULL);

    if (ret == 0) { // will be non-zero if an error or a stall
      // just check the state of the DAC

      if ((state != SND_PCM_STATE_PREPARED) && (state != SND_PCM_STATE_RUNNING) &&
          (state != SND_PCM_STATE_XRUN)) {
        debug(1, "alsa: DAC in odd SND_PCM_STATE_* %d prior to writing.", state);
      }
      if (state == SND_PCM_STATE_XRUN) {
        debug(1, "alsa: DAC in SND_PCM_STATE_XRUN prior to writing.");
        ret = snd_pcm_recover(alsa_handle, ret, 1);
      }

      snd_pcm_state_t prior_state = state; // keep this for afterwards....
      debug(3, "alsa: write %d frames.", samples);
      ret = alsa_pcm_write(alsa_handle, buf, samples);
      if (ret == -EIO) {
        debug(1, "alsa: I/O Error.");
        usleep(20000); // give it a breather...
      }
      if (ret > 0)
        frames_sent_for_playing += ret; // this is the number of frames accepted
      if (ret == samples) {
        stall_monitor_new_frame_count += samples;
      } else {
        frames_sent_break_occurred = 1; // note than an output error has occurred
        if (ret == -EPIPE) {            /* underrun */

          // It could be that the DAC was in the SND_PCM_STATE_XRUN state before
          // sending the samples to be output. If so, it will still be in
          // the SND_PCM_STATE_XRUN state after the call and it needs to be recovered.

          // The underrun occurred in the past, so flagging an
          // error at this point is misleading.

          // In fact, having put samples in the buffer, we are about to fix it by now
          // issuing a snd_pcm_recover().

          // So, if state is SND_PCM_STATE_XRUN now, only report it if the state was
          // not SND_PCM_STATE_XRUN prior to the call, i.e. report it only
          // if we are not trying to recover from a previous underrun.

          if (prior_state == SND_PCM_STATE_XRUN)
            debug(1, "alsa: recovering from a previous underrun.");
          else
            debug(1, "alsa: underrun while writing %d samples to alsa device.", samples);
          ret = snd_pcm_recover(alsa_handle, ret, 1);
        } else if (ret == -ESTRPIPE) { /* suspended */
          if (state != prior_state)
            debug(1, "alsa: suspended while writing %d samples to alsa device.", samples);
          if ((ret = snd_pcm_resume(alsa_handle)) == -ENOSYS)
            ret = snd_pcm_prepare(alsa_handle);
        } else if (ret >= 0) {
          debug(1, "alsa: only %d of %d samples output.", ret, samples);
        }
      }
    }
    pthread_setcancelstate(oldState, NULL);
    if (ret < 0) {
      char errorstring[1024];
      strerror_r(-ret, (char *)errorstring, sizeof(errorstring));
      debug(1, "alsa: SND_PCM_STATE_* %d, error %d (\"%s\") writing %d samples to alsa device.",
            state, ret, (char *)errorstring, samples);
    }
    if ((ret == -ENOENT) || (ret == -ENODEV)) // if the device isn't there...
      handle_unfixable_error(-ret);
  }
  return ret;
}

static int do_open() {
  int ret = 0;
  if (alsa_backend_state != abm_disconnected)
    debug(1, "alsa: do_open() -- asking to open the output device when it is already "
             "connected");
  if (alsa_handle == NULL) {
    debug(3, "alsa: do_open() -- opening the output device");
    ret = open_alsa_device();
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
      frames_sent_break_occurred = 1; // there is a discontinuity with
      // any previously-reported frame count
      frames_sent_for_playing = 0;
      debug(3, "alsa: do_open() -- alsa_backend_state => abm_connected");
      alsa_backend_state = abm_connected; // only do this if it really opened it.
    } else {
      if ((ret == -ENOENT) || (ret == -ENODEV)) // if the device isn't there...
        handle_unfixable_error(-ret);
    }
  } else {
    debug(1, "alsa: do_open() -- output device already open.");
  }
  return ret;
}

static int do_close() {
  if (alsa_backend_state == abm_disconnected)
    debug(3, "alsa: do_close() -- output device is already disconnected");
  int derr = 0;
  if (alsa_handle) {
    debug(3, "alsa: do_close() -- closing the output device");
    if ((derr = snd_pcm_drop(alsa_handle)))
      debug(1, "Error %d (\"%s\") dropping output device.", derr, snd_strerror(derr));
    usleep(20000); // wait for the hardware to do its trick. BTW, this make the function pthread
                   // cancellable
    if ((derr = snd_pcm_hw_free(alsa_handle)))
      debug(1, "Error %d (\"%s\") freeing the output device hardware.", derr, snd_strerror(derr));
    debug(3, "alsa: do_close() -- closing alsa handle");
    if ((derr = snd_pcm_close(alsa_handle)))
      debug(1, "Error %d (\"%s\") closing the output device.", derr, snd_strerror(derr));
    alsa_handle = NULL;
    alsa_handle_status = -ENODEV; // no device open
  } else {
    debug(3, "alsa: do_close() -- output device is already closed.");
  }
  debug(3, "alsa: do_close() -- alsa_backend_state => abm_disconnected.");
  alsa_backend_state = abm_disconnected;
  return derr;
}

static int sub_flush() {
  if (alsa_backend_state == abm_disconnected)
    debug(1, "alsa: do_flush() -- asking to flush the output device when it is already "
             "disconnected");
  int derr = 0;
  if (alsa_handle) {
    debug(3, "alsa: do_flush() -- flushing the output device");
    frames_sent_break_occurred = 1;
    if ((derr = snd_pcm_drop(alsa_handle)))
      debug(1, "Error %d (\"%s\") dropping output device.", derr, snd_strerror(derr));
    if ((derr = snd_pcm_prepare(alsa_handle)))
      debug(1, "Error %d (\"%s\") preparing output device after flush.", derr, snd_strerror(derr));
    stall_monitor_new_frame_count = 0;
    stall_monitor_new_frame_count_time = 0;
    if (alsa_backend_state != abm_connected)
      debug(2, "alsa: sub_flush() -- alsa_backend_state => abm_connected.");
    alsa_backend_state = abm_connected;
  } else {
    debug(1, "alsa: do_flush() -- output device already closed.");
  }
  return derr;
}

static int play(void *buf, int samples, __attribute__((unused)) int sample_type,
                __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {

  // play() will change the state of the alsa_backend_mode to abm_playing
  // also, if the present alsa_backend_state is abm_disconnected, then first the
  // DAC must be
  // connected

  int ret = 0;

  pthread_cleanup_debug_mutex_lock(&alsa_mutex, 50000, 0);

  if (alsa_backend_state == abm_disconnected) {
    ret = do_open();
    if (ret == 0)
      debug(2, "alsa: play() -- opened output device");
  }

  if (ret == 0) {
    if (alsa_backend_state != abm_playing) {
      debug(2, "alsa: play() -- alsa_backend_state => abm_playing");
      alsa_backend_state = abm_playing;

      // mute_requested_internally = 0; // stop requesting a mute for backend's own
      // reasons, which might have been a flush
      // debug(2, "play() set_mute_state");
      // set_mute_state(); // try to action the request and return a status
      // do_mute(0); // unmute for backend's reason
    }
    ret = do_play(buf, samples);
  }

  debug_mutex_unlock(&alsa_mutex, 0);
  pthread_cleanup_pop(0); // release the mutex
  return ret;
}

static void flush(void) {
  pthread_cleanup_debug_mutex_lock(&alsa_mutex, 10000, 1);
  if (alsa_backend_state != abm_disconnected) { // must be playing or connected...
    // do nothing for a flush if config.keep_dac_busy is true
    if (config.keep_dac_busy == 0) {
      sub_flush();
    }
  } else {
    debug(3, "alsa: flush() -- called on a disconnected alsa backend");
  }
  debug_mutex_unlock(&alsa_mutex, 3);
  pthread_cleanup_pop(0); // release the mutex
}

static void stop(void) {
  pthread_cleanup_debug_mutex_lock(&alsa_mutex, 10000, 1);
  if (alsa_backend_state != abm_disconnected) { // must be playing or connected...
    if (config.keep_dac_busy == 0) {
      do_close();
    }
  } else
    debug(3, "alsa: stop() -- called on a disconnected alsa backend");
  debug_mutex_unlock(&alsa_mutex, 3);
  pthread_cleanup_pop(0); // release the mutex
}

static void do_volume(double vol) { // caller is assumed to have the alsa_mutex when
                                    // using this function
  debug(3, "Setting volume db to %f.", vol);
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  set_volume = vol;
  pthread_cleanup_debug_mutex_lock(&alsa_mixer_mutex, 1000, 1);
  if (volume_set_request && (open_mixer() == 0)) {
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

static void volume(double vol) {
  volume_set_request = 1; // an external request has been made to set the volume
  do_volume(vol);
}

static int mute(int mute_state_requested) {         // these would be for external reasons, not
                                                    // because of the
                                                    // state of the backend.
  mute_requested_externally = mute_state_requested; // request a mute for external reasons
  debug(2, "mute(%d) set_mute_state", mute_state_requested);
  return set_mute_state();
}
/*
static void alsa_buffer_monitor_thread_cleanup_function(__attribute__((unused)) void
*arg) {
  debug(1, "alsa: alsa_buffer_monitor_thread_cleanup_function called.");
}
*/

static void *alsa_buffer_monitor_thread_code(__attribute__((unused)) void *arg) {
  //  #include <syscall.h>
  //  debug(1, "alsa_buffer_monitor_thread_code PID %d", syscall(SYS_gettid));
  // Wait until the output configuration has been set by the main program
  debug(2, "alsa: alsa_buffer_monitor_thread_code started.");
  int frame_count = 0;
  int error_count = 0;
  int error_detected = 0;
  int64_t sleep_time_actual_ns = 0; // actual sleep time since last check, or zero
  int okb = -1;

  while (config.keep_dac_busy == 0)
    usleep(10000);

  debug(1, "alsa: get initial disable standby parameters for rate/channels: %u/%u.",
        disable_standby_mode_default_rate, disable_standby_mode_default_channels);
  while (get_permissible_configuration_settings() != 0) {
    debug(1, "wait 50 ms to check again for success");
    usleep(50000);
  }

  current_encoded_output_format =
      get_configuration(disable_standby_mode_default_channels, disable_standby_mode_default_rate,
                        disable_standby_mode_default_format);

  debug(1, "alsa: disable standby initial parameters: %s.",
        short_format_description(current_encoded_output_format));

  // if too many play errors occur early on, we will turn off the disable standby mode
  while (error_detected == 0) {
    int keep_dac_busy_has_just_gone_off = 0;
    if (okb != config.keep_dac_busy) {
      if ((okb != 0) && (config.keep_dac_busy == 0)) {
        keep_dac_busy_has_just_gone_off = 1;
      }
      debug(2, "keep_dac_busy is now \"%s\"", config.keep_dac_busy == 0 ? "no" : "yes");
      okb = config.keep_dac_busy;
    }
    if ((config.keep_dac_busy != 0) && (alsa_device_initialised == 0)) {
      debug(2, "alsa: alsa_buffer_monitor_thread_code() preparing for use and initialising.");
      alsa_device_init();
      alsa_device_initialised = 1;
    }
    int sleep_time_us = (int)(config.disable_standby_mode_silence_scan_interval * 1000000);
    if (sleep_time_actual_ns > ((8 * sleep_time_us * 1000) / 4))
      debug(1,
            "alsa_buffer_monitor_thread_code sleep was %.6f sec but request was for %.6f sec. "
            "Disabling standby may not work properly!",
            sleep_time_actual_ns * 0.000000001, config.disable_standby_mode_silence_scan_interval);
    pthread_cleanup_debug_mutex_lock(&alsa_mutex, 200000, 0);
    // check possible state transitions here
    if ((alsa_backend_state == abm_disconnected) && (config.keep_dac_busy != 0)) {
      // open the dac and move to abm_connected mode
      if (do_open() == 0) {
        debug(2,
              "alsa: alsa_buffer_monitor_thread_code() -- output device opened; "
              "alsa_backend_state from abm_disconnected => abm_connected. error_detected = %d",
              error_detected);
      } else {
        debug(1, "alsa_buffer_monitor_thread_code: can't open output device -- terminating");
        error_detected = 1;
      }
    } else if ((alsa_backend_state != abm_disconnected) && (keep_dac_busy_has_just_gone_off != 0)) {
      debug(2, "alsa: alsa_buffer_monitor_thread_code() -- closing the output "
               "device");
      do_close();
    }
    // now, if the backend is not in the abm_disconnected state
    // and config.keep_dac_busy is true (at the present, this has to be the case
    // to be in the
    // abm_connected state in the first place...) then do the silence-filling
    // thing, if needed /* only if the output device is capable of precision delay */.
    if ((alsa_backend_state != abm_disconnected) && (config.keep_dac_busy != 0) &&
        (error_detected == 0) /* && precision_delay_available() */) {
      int reply;
      long buffer_size = 0;
      snd_pcm_state_t state;
      reply = delay_and_status(&state, &buffer_size, NULL);
      if (reply != 0) {
        buffer_size = 0;
        char errorstring[1024];
        strerror_r(-reply, (char *)errorstring, sizeof(errorstring));
        debug(1, "alsa: alsa_buffer_monitor_thread_code delay error %d: \"%s\".", reply,
              (char *)errorstring);
      }
      uint64_t current_delay = 0;
      if (buffer_size < 0) {
        debug(1, "delay of less than 0: %ld.", buffer_size);
        current_delay = 0;
      } else {
        current_delay = buffer_size;
      }
      if (current_delay < minimum_dac_queue_size) {
        minimum_dac_queue_size = current_delay; // update for display later
      }

      long buffer_size_threshold = (long)(config.disable_standby_mode_silence_threshold *
                                          RATE_FROM_ENCODED_FORMAT(current_encoded_output_format));
      // debug(1, "current_delay: %" PRIu64 ", buffer_size: %ld, buffer_size_threshold %ld,
      // frames_of_silence: %d.", current_delay, buffer_size, buffer_size_threshold,
      // buffer_size_threshold - buffer_size + current_alsa_configuration->rate / 10);

      size_t size_of_silence_buffer;
      // debug(1, "buffer_size %d, buffer_size_threshold %d.", buffer_size,
      // buffer_size_threshold);
      if (buffer_size < buffer_size_threshold) {
        int frames_of_silence = buffer_size_threshold - buffer_size +
                                RATE_FROM_ENCODED_FORMAT(current_encoded_output_format) / 10;
        size_of_silence_buffer =
            frames_of_silence *
            fr[FORMAT_FROM_ENCODED_FORMAT(current_encoded_output_format)].sample_size *
            CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format);
        void *silence = calloc(size_of_silence_buffer, 1);
        if (silence == NULL) {
          warn("disable_standby_mode has been turned off because a memory allocation error "
               "occurred.");
          error_detected = 1;
        } else {
          int ret;
          pthread_cleanup_push(malloc_cleanup, &silence);
          int use_dither = 0;
          if ((alsa_mix_ctrl == NULL) &&
              (((config.ignore_volume_control == 0) && (config.airplay_volume != 0.0)) ||
               (config.playback_mode == ST_mono)))
            use_dither = 1;

          dither_random_number_store =
              generate_zero_frames(silence, frames_of_silence,
                                   use_dither, // i.e. with dither
                                   dither_random_number_store, current_encoded_output_format);

          ret = do_play(silence, frames_of_silence);
          debug(3, "Played %u frames of silence on %u channels, equal to %zu bytes.",
                frames_of_silence, CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format),
                size_of_silence_buffer);
          frame_count++;
          pthread_cleanup_pop(1); // free malloced buffer
          if (ret < 0) {
            error_count++;
            char errorstring[1024];
            strerror_r(-ret, (char *)errorstring, sizeof(errorstring));
            debug(2,
                  "alsa: alsa_buffer_monitor_thread_code error %d (\"%s\") writing %d samples "
                  "to alsa device -- %d errors in %d trials.",
                  ret, (char *)errorstring, frames_of_silence, error_count, frame_count);
            if ((error_count > 40) && (frame_count < 100)) {
              warn("disable_standby_mode has been turned off because too many underruns "
                   "occurred. Is Shairport Sync outputting to a virtual device or running in a "
                   "virtual machine?");
              error_detected = 1;
            }
          }
        }
      }
    }
    debug_mutex_unlock(&alsa_mutex, 0);
    pthread_cleanup_pop(0); // release the mutex
    uint64_t tsb = get_absolute_time_in_ns();
    usleep(sleep_time_us); // has a cancellation point in it
    sleep_time_actual_ns = get_absolute_time_in_ns() - tsb;
  }
  pthread_exit(NULL);
}

static int32_t get_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  // pass in SPS_FORMAT_AUTO because we want the best (deepest) format.

  // first, check that the device is there!
  snd_pcm_t *temp_alsa_handle = NULL;
  int response = snd_pcm_open(&temp_alsa_handle, alsa_out_dev, SND_PCM_STREAM_PLAYBACK, 0);
  ;
  if ((response == 0) && (temp_alsa_handle != NULL)) {
    response = snd_pcm_close(temp_alsa_handle);
    if (response != 0) {
      char errorstring[1024];
      strerror_r(-response, (char *)errorstring, sizeof(errorstring));
      debug(1, "error %d closing probed alsa output device \"%s\".", -response, alsa_out_dev);
    }
  } else if (response == -EBUSY) {
    response = 0; // busy is okay -- it means the device exists
  } else {
    char errorstring[1024];
    strerror_r(-response, (char *)errorstring, sizeof(errorstring));
    debug(3,
          "the alsa output device called \"%s\" can not be accessed. Error %d (\"%s\"). Maybe it "
          "doesn't exist or is not ready yet...",
          alsa_out_dev, -response, errorstring);
  }
  // if we can access the device, then search for configurations
  if (response == 0)
    response = search_for_suitable_configuration(channels, rate, format, &check_configuration);
  return response;
}
