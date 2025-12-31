/*
 * sndio output driver. This file is part of Shairport Sync.
 * Copyright (c) 2013 Dimitri Sokolyuk <demon@dim13.org>
 * Copyright (c) 2017 Tobias Kortkamp <t@tobik.me>
 * Copyright (c) 2014--2025 Mike Brady
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "audio.h"
#include "common.h"
#include <errno.h>
#include <pthread.h>
#include <sndio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// see https://sndio.org/tips.html#section_2_1, section 11 Glossary
// don't have any information for 7 and 8 channels
static char channel_map_1[] = "FL";
static char channel_map_2[] = "FL FR";
static char channel_map_3[] = "FL FR BL";
static char channel_map_4[] = "FL FR BL BR";
static char channel_map_5[] = "FL FR BL BR FC";
static char channel_map_6[] = "FL FR BL BR FC LFE";

static pthread_mutex_t sndio_mutex = PTHREAD_MUTEX_INITIALIZER;

static int current_encoded_output_format;
static struct sio_hdl *hdl;
static const char *output_device_name;
static unsigned int output_device_driver_bufsiz; // parameters for opening the output device
static unsigned int output_device_driver_round;  // parameters for opening the output device
static int is_running;

static int framesize;
static size_t played;
static size_t written;
uint64_t time_of_last_onmove_cb;
int at_least_one_onmove_cb_seen;

struct sio_par par;

struct sndio_formats {
  sps_format_t sps_format;
  unsigned int bits; // bits per sample
  unsigned int sig;  // signed = 1,
  unsigned int le;   // is little endian
};

static struct sndio_formats format_lookup[] = {{SPS_FORMAT_S16_LE, 16, 1, 1},
                                               {SPS_FORMAT_S16_BE, 16, 1, 0},
                                               {SPS_FORMAT_S32_LE, 32, 1, 1},
                                               {SPS_FORMAT_S32_BE, 32, 1, 0}};

// use an SPS_FORMAT_... to find an entry in the format_lookup table or return NULL
static struct sndio_formats *sps_format_lookup(sps_format_t to_find) {
  struct sndio_formats *response = NULL;
  unsigned int i = 0;
  while ((response == NULL) && (i < sizeof(format_lookup) / sizeof(struct sndio_formats))) {
    if (format_lookup[i].sps_format == to_find)
      response = &format_lookup[i];
    else
      i++;
  }
  return response;
}

static uint16_t permissible_configurations[SPS_RATE_HIGHEST + 1][SPS_FORMAT_HIGHEST_NATIVE + 1]
                                          [8 + 1];

static int get_permissible_configuration_settings() {
  int ret = 0;
  uint64_t hto = get_absolute_time_in_ns();
  pthread_cleanup_debug_mutex_lock(&sndio_mutex, 1000, 1);
  struct sio_hdl *hdl = sio_open(output_device_name, SIO_PLAY, 0);
  if (hdl != NULL) {
    struct sio_cap cap;
    // memset(&cap, 0, sizeof(struct sio_cap));
    ret = sio_getcap(hdl, &cap);
    if (ret == 1) {
      sps_format_t f;
      sps_rate_t r;
      unsigned int c;
      // check what numbers of channels the device can provide...
      for (c = 1; c <= 8; c++) {
        // if it's in the channel set -- either due to a setting in the configuration file or by
        // default, check it...
        if ((config.channel_set & (1 << c)) != 0) {

          unsigned int s = 0;
          int fo = 0;
          while ((s < SIO_NCHAN) && (fo == 0)) {
            if (cap.pchan[s] == c)
              fo = 1;
            else
              s++;
          }
          if (fo == 0) {
            debug(3, "sndio: output device can't deal with %u channels.", c);
            config.channel_set &= ~(1 << c); // remove this channel count
          } else {
            debug(3, "sndio: output device can have %u channels.", c);
          }
        }
      }

      // check what speeds the device can handle
      for (r = SPS_RATE_LOWEST; r <= SPS_RATE_HIGHEST; r++) {
        // if it's in the rate set -- either due to a setting in the configuration file or by
        // default, check it...
        if ((config.rate_set & (1 << r)) != 0) {
          unsigned int s = 0;
          int fo = 0;
          while ((s < SIO_NRATE) && (fo == 0)) {
            if (cap.rate[s] == sps_rate_actual_rate(r))
              fo = 1;
            else
              s++;
          }
          if (fo == 0) {
            debug(3, "sndio: output device can't be set to %u fps.", sps_rate_actual_rate(r));
            config.rate_set &= ~(1 << r); // remove this rate
          } else {
            debug(3, "sndio: output device can be set to %u fps.", sps_rate_actual_rate(r));
          }
        }
      }

      // check what formats the device can handle
      for (f = SPS_FORMAT_LOWEST; f <= SPS_FORMAT_HIGHEST_NATIVE; f++) {
        // if it's in the format set -- either due to a setting in the configuration file or by
        // default, check it...
        if ((config.format_set & (1 << f)) != 0) {
          struct sndio_formats *format = sps_format_lookup(f);
          unsigned int s = 0;
          int found = 0;
          if (format != NULL) {
            while ((s < SIO_NENC) && (found == 0)) {
              if ((cap.enc[s].bits == format->bits) && (cap.enc[s].le == format->le) &&
                  (cap.enc[s].sig == format->sig)) {
                // debug(1, "bits: %u, %u. le: %u, %u. sig: %u, %u.", cap.enc[s].bits, format->bits,
                // cap.enc[s].le, format->le, cap.enc[s].sig, format->sig);
                found = 1;
              } else {
                s++;
              }
            }
          } else {
            debug(3, "sndio: no entry for format %s.", sps_format_description_string(f));
          }
          if (found == 0) {
            if (format != 0)
              debug(3, "sndio: output device can't be set to format %s.",
                    sps_format_description_string(f));
            config.format_set &= ~(1 << f); // remove this format
          } else {
            debug(3, "sndio: output device can be set to format %s.",
                  sps_format_description_string(f));
          }
        }
      }
      //      for (f = SPS_FORMAT_LOWEST; f <= SPS_FORMAT_HIGHEST_NATIVE; f++) {
      //        if ((config.format_set & (1 << f)) != 0)
      //          debug(1, "format %s okay.", sps_format_description_string(f));
      //      }
      // now we have the channels, rates and formats, but we need to check each combination
      // set the permissible_configurations array (r/f/c) to EINVAL
      for (r = SPS_RATE_LOWEST; r <= SPS_RATE_HIGHEST; r++)
        for (f = SPS_FORMAT_LOWEST; f <= SPS_FORMAT_HIGHEST_NATIVE; f++)
          for (c = 0; c <= 8; c++) {
            permissible_configurations[r][f][c] = EINVAL;
          }
      // now check each combination of permitted rate/format/channel and see if it's really allowed
      for (r = SPS_RATE_LOWEST; r <= SPS_RATE_HIGHEST; r++) {
        if ((config.rate_set & (1 << r)) != 0) {
          for (f = SPS_FORMAT_LOWEST; f <= SPS_FORMAT_HIGHEST_NATIVE; f++) {
            if ((config.format_set & (1 << f)) != 0) {
              for (c = 0; c <= 8; c++) {
                if ((config.channel_set & (1 << c)) != 0) {
                  // debug(1, "check %u/%s/%u.", sps_rate_actual_rate(r),
                  // sps_format_description_string(f), c);
                  struct sio_par proposed_par, actual_par;
                  struct sndio_formats *format_info = sps_format_lookup(f);
                  sio_initpar(&proposed_par);
                  proposed_par.rate = sps_rate_actual_rate(r);
                  proposed_par.pchan = c;
                  proposed_par.bits = format_info->bits;
                  proposed_par.bps = SIO_BPS(par.bits);
                  proposed_par.le = format_info->le;
                  proposed_par.sig = format_info->sig;
                  if (sio_setpar(hdl, &proposed_par) == 1) {
                    if (sio_getpar(hdl, &actual_par) == 1) {
                      if ((actual_par.rate == proposed_par.rate) &&
                          (actual_par.pchan == proposed_par.pchan) &&
                          (actual_par.bits == proposed_par.bits) &&
                          (actual_par.le == proposed_par.le) &&
                          (actual_par.sig == proposed_par.sig)) {
                        permissible_configurations[r][f][c] =
                            0; // i.e. no error, so remove the EINVAL
                      } else {
                        debug(3, "sndio: check_setting: could not set format exactly");
                      }
                    } else {
                      debug(1,
                            "sndio: check_setting: could not get response for a proposed format");
                    }
                  }
                }
              }
            }
          }
        }
      }
    }
  }
  pthread_cleanup_pop(1); // let the handle go...
  ret = 0;                // all good here, even if the last ret was an error
  int64_t hot = get_absolute_time_in_ns() - hto;
  debug(2, "sndio: get_permissible_configuration_settings took %f ms.", 0.000001 * hot);
  return ret;
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

static int32_t get_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  return search_for_suitable_configuration(channels, rate, format, &check_configuration);
}

static int configure(int32_t requested_encoded_format, char **channel_map) {
  int response = 0;
  debug(3, "sndio: configure %s.", short_format_description(requested_encoded_format));

  // if (1) {
  if (current_encoded_output_format != requested_encoded_format) {
    if (current_encoded_output_format == 0)
      debug(2, "sndio: setting output configuration to %s.",
            short_format_description(requested_encoded_format));
    else
      // note -- can't use short_format_description twice in one call because it returns the same
      // string buffer each time
      debug(2, "sndio: changing output configuration to %s.",
            short_format_description(requested_encoded_format));
    current_encoded_output_format = requested_encoded_format;

    struct sndio_formats *format_info =
        sps_format_lookup(FORMAT_FROM_ENCODED_FORMAT(requested_encoded_format));

    if (format_info == NULL)
      die("sndio: can't find format information!");

    if (is_running != 0) {
      debug(1, "sndio: the output device is running while changing configuration");
      if (sio_flush(hdl) != 1)
        debug(1, "sndio: unable to flush");
      written = played = is_running = 0;
      time_of_last_onmove_cb = 0;
      at_least_one_onmove_cb_seen = 0;
    }

    struct sio_par par;
    memset(&par, 0, sizeof(struct sio_par));
    sio_initpar(&par);
    par.rate = RATE_FROM_ENCODED_FORMAT(requested_encoded_format);
    par.pchan = CHANNELS_FROM_ENCODED_FORMAT(requested_encoded_format);
    par.bits = format_info->bits;
    par.bps = SIO_BPS(par.bits);
    par.le = format_info->le;
    par.sig = format_info->sig;
    debug(3, "Requested %u/%u/%u/%u/%u (rate/bits/signed/le/channels)", par.rate, par.bits, par.sig,
          par.le, par.pchan);
    if (sio_setpar(hdl, &par) == 1) {
      struct sio_par apar;
      if (sio_getpar(hdl, &apar) == 1) {
        debug(3, "Got %u/%u/%u/%u/%u (rate/bits/signed/le/channels)", apar.rate, apar.bits,
              apar.sig, apar.le, apar.pchan);
        if ((apar.rate == par.rate) && (apar.pchan == par.pchan) && (apar.bits == par.bits) &&
            (apar.le == par.le) && (apar.sig == par.sig)) {
          framesize = apar.bps * apar.pchan;
          // config.audio_backend_buffer_desired_length = 1.0 * par.bufsz / par.rate;
          debug(
              2,
              "bufsiz is %u, rate is %u: computed buffer length is %f, actual buffer length is %f.",
              apar.bufsz, apar.rate, (1.0 * apar.bufsz) / apar.rate,
              config.audio_backend_buffer_desired_length);
        } else {
          debug(1, "sndio: configure: could not set format exactly");
          response = 1;
        }
      } else {
        debug(1, "sndio: configure: could not set format");
        response = 1;
      }
    }
  }
  if ((response == 0) && (channel_map != NULL)) {
    switch (CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format)) {
    case 1:
      *channel_map = channel_map_1;
      break;
    case 2:
      *channel_map = channel_map_2;
      break;
    case 3:
      *channel_map = channel_map_3;
      break;
    case 4:
      *channel_map = channel_map_4;
      break;
    case 5:
      *channel_map = channel_map_5;
      break;
    case 6:
      *channel_map = channel_map_6;
      break;
    case 7:
      *channel_map = channel_map_6;
      break;
    case 8:
      *channel_map = channel_map_6;
      break;
    default:
      *channel_map = NULL;
      break;
    }
  }
  return response;
}

static void help() {
  printf("    -d output-device    set the output device [default|rsnd/0|rsnd/1...]\n");
}

static void onmove_cb(__attribute__((unused)) void *arg, int delta) {
  time_of_last_onmove_cb = get_absolute_time_in_ns();
  at_least_one_onmove_cb_seen = 1;
  played += delta;
}

static int init(int argc, char **argv) {
  // debug(1, "sndio: init");
  current_encoded_output_format = 0;
  is_running = 0;

  // defaults
  config.audio_backend_buffer_desired_length = 1.0;
  config.audio_backend_buffer_interpolation_threshold_in_seconds =
      0.25; // below this, soxr interpolation will not occur -- it'll be basic interpolation
            // instead.
  config.audio_backend_latency_offset = 0;
  output_device_name = SIO_DEVANY;
  output_device_driver_bufsiz = 0; // hmm, not sure about this...
  output_device_driver_round = 0;
  int opt;

  // get settings from settings file, passing in defaults for format_set, rate_set and channel_set
  // Note, these options may be in the "general" stanza or the named stanza
#ifdef CONFIG_FFMPEG
  parse_audio_options("sndio", SPS_FORMAT_SET, SPS_RATE_SET, SPS_CHANNEL_SET);
#else
  parse_audio_options("sndio", SPS_FORMAT_NON_FFMPEG_SET, SPS_RATE_NON_FFMPEG_SET,
                      SPS_CHANNNEL_NON_FFMPEG_SET);
#endif

  // get specific settings
  int value;
  if (config.cfg != NULL) {
    if (!config_lookup_non_empty_string(config.cfg, "sndio.device", &output_device_name))
      output_device_name = SIO_DEVANY;

    if (config_lookup_int(config.cfg, "sndio.bufsz", &value)) {
      if (value > 0) {
        output_device_driver_bufsiz = value;
      } else {
        die("sndio: bufsz must be > 0");
      }
    }

    if (config_lookup_int(config.cfg, "sndio.round", &value)) {
      if (value > 0) {
        output_device_driver_round = value;
      } else {
        die("sndio: round must be > 0");
      }
    }
  }

  optind = 1; // optind=0 is equivalent to optind=1 plus special behaviour
  argv--;     // so we shift the arguments to satisfy getopt()
  argc++;
  while ((opt = getopt(argc, argv, "d:")) > 0) {
    switch (opt) {
    case 'd':
      output_device_name = optarg;
      break;
    default:
      help();
      die("Invalid audio option -%c specified", opt);
    }
  }
  if (optind < argc)
    die("Invalid audio argument: %s", argv[optind]);
  /*
    written = played = 0;
    time_of_last_onmove_cb = 0;
    at_least_one_onmove_cb_seen = 0;
  */
  get_permissible_configuration_settings();
  debug(2, "sndio: output device name is \"%s\".", output_device_name);
  hdl = sio_open(output_device_name, SIO_PLAY, 0);
  if (!hdl)
    die("sndio: cannot open audio device");
  sio_onmove(hdl, onmove_cb, NULL);
  // debug(1, "sndio: init done");
  return 0;
}

static void deinit() {
  pthread_cleanup_debug_mutex_lock(&sndio_mutex, 1000, 1);
  if (hdl != NULL) {
    if (is_running != 0) {
      sio_flush(hdl);
      is_running = 0;
    }
    sio_close(hdl);
    hdl = NULL;
  }
  pthread_cleanup_pop(1); // unlock the mutex
}

static int play(void *buf, int frames, __attribute__((unused)) int sample_type,
                __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {

  if (frames > 0) {
    pthread_cleanup_debug_mutex_lock(&sndio_mutex, 1000, 1);
    if (is_running == 0) {
      if (hdl != NULL) {
        if (sio_start(hdl) != 1)
          debug(1, "sndio: unable to start");
        is_running = 1;
        written = played = 0;
        time_of_last_onmove_cb = 0;
        at_least_one_onmove_cb_seen = 0;
      } else {
        debug(1, "sndio: output device is not open for play!");
      }
    }
    written += sio_write(hdl, buf, frames * framesize);
    pthread_cleanup_pop(1); // unlock the mutex
  }
  return 0;
}

static void stop() {

  pthread_cleanup_debug_mutex_lock(&sndio_mutex, 1000, 1);
  if (hdl != NULL) {
    if (is_running != 0) {
      if (sio_flush(hdl) != 1)
        debug(1, "sndio: unable to stop");
      written = played = is_running = 0;
    } else {
      debug(1, "sndio: stop: not running.");
    }
  } else {
    debug(1, "sndio: output device is not open for stop!");
  }
  pthread_cleanup_pop(1); // unlock the mutex
}

int get_delay(long *delay) {
  int response = 0;

  size_t estimated_extra_frames_output = 0;
  if (at_least_one_onmove_cb_seen) { // when output starts, the onmove_cb callback will be made
    // calculate the difference in time between now and when the last callback occurred,
    // and use it to estimate the frames that would have been output
    uint64_t time_difference = get_absolute_time_in_ns() - time_of_last_onmove_cb;
    uint64_t frame_difference = (time_difference * par.rate) / 1000000000;
    estimated_extra_frames_output = frame_difference;
    // sanity check -- total estimate can not exceed frames written.
    if ((estimated_extra_frames_output + played) > written / framesize) {
      // debug(1,"play estimate fails sanity check, possibly due to running on a VM");
      estimated_extra_frames_output = 0; // can't make any sensible guess
    }
    // debug(1,"Frames played to last cb: %d, estimated to current time:
    // %d.",played,estimated_extra_frames_output);
  }
  if (delay != NULL)
    *delay = (written / framesize) - (played + estimated_extra_frames_output);
  return response;
}

static int delay(long *delay) {
  int result = 0;
  pthread_cleanup_debug_mutex_lock(&sndio_mutex, 1000, 1);
  if (hdl != NULL) {
    if (is_running != 0) {
      result = get_delay(delay);
    } else {
      debug(2, "sndio: output device is not open for delay!");
      if (delay != NULL)
        *delay = 0;
    }
  } else {
    debug(1, "sndio: output device is not open for delay!");
  }
  pthread_cleanup_pop(1); // unlock the mutex
  return result;
}

static void flush() {
  pthread_cleanup_debug_mutex_lock(&sndio_mutex, 1000, 1);
  if (hdl != NULL) {
    if (is_running != 0) {
      if (sio_flush(hdl) != 1)
        debug(1, "sndio: unable to flush");
      written = played = is_running = 0;
    } else {
      debug(2, "sndio: flush: not running.");
    }
  } else {
    debug(1, "sndio: output device is not open for flush!");
  }
  pthread_cleanup_pop(1); // unlock the mutex
}

audio_output audio_sndio = {.name = "sndio",
                            .help = &help,
                            .init = &init,
                            .deinit = &deinit,
                            .configure = &configure,
                            .get_configuration = &get_configuration,
                            .start = NULL,
                            .stop = &stop,
                            .is_running = NULL,
                            .flush = &flush,
                            .delay = &delay,
                            .stats = NULL,
                            .play = &play,
                            .volume = NULL,
                            .parameters = NULL,
                            .mute = NULL};
