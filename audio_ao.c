/*
 * libao output driver. This file is part of Shairport.
 * Copyright (c) James Laird 2013
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

#include "audio.h"
#include "common.h"
#include <ao/ao.h>
#include <memory.h>
#include <stdio.h>
#include <unistd.h>

typedef struct {
  sps_format_t sps_format;
  int bits_per_sample;
  int byte_format;
} sps_ao_t;

// these are the only formats that audio_pw will ever allow itself to be configured with
static sps_ao_t format_lookup[] = {{SPS_FORMAT_S16_LE, 16, AO_FMT_LITTLE},
                                   {SPS_FORMAT_S16_BE, 16, AO_FMT_BIG},
                                   {SPS_FORMAT_S32_LE, 32, AO_FMT_LITTLE},
                                   {SPS_FORMAT_S32_BE, 32, AO_FMT_BIG}};

// use an SPS_FORMAT_... to find an entry in the format_lookup table or return NULL
static sps_ao_t *sps_format_lookup(sps_format_t to_find) {
  sps_ao_t *response = NULL;
  unsigned int i = 0;
  while ((response == NULL) && (i < sizeof(format_lookup) / sizeof(sps_ao_t))) {
    if (format_lookup[i].sps_format == to_find)
      response = &format_lookup[i];
    else
      i++;
  }
  return response;
}

ao_device *dev = NULL;
ao_option *ao_opts = NULL;
ao_sample_format ao_output_format;
int driver = 0;
static int32_t current_encoded_output_format = 0;

static int check_settings(sps_format_t sample_format, unsigned int sample_rate,
                          unsigned int channel_count) {

  // debug(1, "ao check_settings: configuration: %u/%s/%u.", sample_rate,
  //       sps_format_description_string(sample_format), channel_count);

  int response = EINVAL;

  sps_ao_t *format_info = sps_format_lookup(sample_format);
  if (format_info != NULL) {

    ao_sample_format check_fmt;
    memset(&check_fmt, 0, sizeof(check_fmt));
    check_fmt.bits = format_info->bits_per_sample;
    check_fmt.rate = sample_rate;
    check_fmt.channels = channel_count;
    check_fmt.byte_format = format_info->byte_format;
    // fmt.matrix = strdup("L,R");
    ao_device *check_dev = ao_open_live(driver, &check_fmt, ao_opts);
    if (check_dev != NULL) {
      debug(3, "ao: check_settings %u/%s/%u works!", sample_rate,
            sps_format_description_string(sample_format), channel_count);
      if (ao_close(check_dev) == 0)
        debug(1, "ao check_settings: error at ao_close");
      response = 0;
    } else {
      debug(3, "ao: check_settings %u/%s/%u is not available!", sample_rate,
            sps_format_description_string(sample_format), channel_count);
    }

    // response = 0;
  }

  // debug(1, "pa check_settings: configuration: %u/%s/%u %s.", sample_rate,
  //       sps_format_description_string(sample_format), channel_count,
  //       response == 0 ? "is okay" : "can not be configured");
  return response;
}

static int check_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  return check_settings(format, rate, channels);
}

static int32_t get_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  uint64_t start_time = get_absolute_time_in_ns();
  int32_t response =
      search_for_suitable_configuration(channels, rate, format, &check_configuration);
  int64_t elapsed_time = get_absolute_time_in_ns() - start_time;
  debug(1, "ao: get_configuration took %0.3f mS.", elapsed_time * 0.000001);
  return response;
}

static int configure(int32_t requested_encoded_format, char **channel_map) {
  debug(3, "a0: configure %s.", short_format_description(requested_encoded_format));
  int response = EINVAL;
  if (current_encoded_output_format != requested_encoded_format) {
    uint64_t start_time = get_absolute_time_in_ns();
    if (current_encoded_output_format == 0)
      debug(1, "a0: setting output configuration to %s.",
            short_format_description(requested_encoded_format));
    else
      // note -- can't use short_format_description twice in one call because it returns the same
      // string buffer each time
      debug(1, "a0: changing output configuration to %s.",
            short_format_description(requested_encoded_format));
    current_encoded_output_format = requested_encoded_format;
    sps_ao_t *format_info =
        sps_format_lookup(FORMAT_FROM_ENCODED_FORMAT(current_encoded_output_format));

    if (format_info == NULL)
      die("ao: can't find format information!");

    if (dev != NULL) {
      if (ao_close(dev) == 0) {
        debug(1, "ao configure: error closing existing device ao_close");
      }
    }
    dev = NULL;
    memset(&ao_output_format, 0, sizeof(ao_output_format));
    ao_output_format.bits = format_info->bits_per_sample;
    ao_output_format.rate = RATE_FROM_ENCODED_FORMAT(current_encoded_output_format);
    ao_output_format.channels = CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format);
    ao_output_format.byte_format = format_info->byte_format;
    switch (CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format)) {
    case 2:
      ao_output_format.matrix = strdup("L,R");
      break;
    case 6:
      ao_output_format.matrix = strdup("L,R,C,LFE,BR,BL");
      break;
    case 8:
      ao_output_format.matrix = strdup("L,R,C,LFE,BR,BL,SL,SR");
      break;
    default:
      break;
    }
    // fmt.matrix = strdup("L,R");
    /*
    dev = ao_open_live(driver, &ao_output_format, ao_opts);
    if (dev != NULL) {
      debug(
          1, "ao: configure %u/%s/%u opened!",
          RATE_FROM_ENCODED_FORMAT(current_encoded_output_format),
          sps_format_description_string(FORMAT_FROM_ENCODED_FORMAT(current_encoded_output_format)),
          CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format));
      response = 0;
    } else {
      debug(
          1, "ao: configure %u/%s/%u is not available!",
          RATE_FROM_ENCODED_FORMAT(current_encoded_output_format),
          sps_format_description_string(FORMAT_FROM_ENCODED_FORMAT(current_encoded_output_format)),
          CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format));
    }
    */
    int64_t elapsed_time = get_absolute_time_in_ns() - start_time;
    debug(3, "pa: configure took %0.3f mS.", elapsed_time * 0.000001);
  } else {
    debug(3, "pa: setting output configuration  -- configuration unchanged, so nothing done.");
  }
  if ((response == 0) && (channel_map != NULL)) {
    *channel_map = NULL; // nothing back here
  }
  return response;
}

static void help(void) {
  printf("    -d driver           set the output driver\n"
         "    -o name=value       set an arbitrary ao option\n"
         "    -i id               shorthand for -o id=<id>\n"
         "    -n name             shorthand for -o dev=<name> -o dsp=<name>\n");
  // get a list of drivers available
  ao_initialize();
  int defaultDriver = ao_default_driver_id();
  if (defaultDriver == -1) {
    printf("    No usable drivers available.\n");
  } else {
    ao_info *defaultDriverInfo = ao_driver_info(defaultDriver);
    int driver_count;
    ao_info **driver_list = ao_driver_info_list(&driver_count);
    int i = 0;
    if (driver_count == 0) {
      printf("    Driver list unavailable.\n");
    } else {
      printf("    Drivers:\n");
      for (i = 0; i < driver_count; i++) {
        ao_info *the_driver = driver_list[i];
        if (strcmp(the_driver->short_name, defaultDriverInfo->short_name) == 0)
          printf("        \"%s\" (default)\n", the_driver->short_name);
        else
          printf("        \"%s\"\n", the_driver->short_name);
      }
    }
  }
  ao_shutdown();
}

static int init(int argc, char **argv) {
  ao_initialize();
  driver = ao_default_driver_id();
  if (driver == -1) {
    warn("the ao backend can not find a usable output device with the default driver!");
  } else {

    // set up default values first

    config.audio_backend_buffer_desired_length = 1.0;
    config.audio_backend_latency_offset = 0;

    // get settings from settings file first, allow them to be overridden by
    // command line options

    // get settings from settings file, passing in defaults for format_set, rate_set and channel_set
    // Note, these options may be in the "general" stanza or the named stanza
#ifdef CONFIG_FFMPEG
    parse_audio_options("ao", SPS_FORMAT_SET, SPS_RATE_SET, SPS_CHANNEL_SET);
#else
    parse_audio_options("ao", SPS_FORMAT_NON_FFMPEG_SET, SPS_RATE_NON_FFMPEG_SET,
                        SPS_CHANNNEL_NON_FFMPEG_SET);
#endif

    optind = 1; // optind=0 is equivalent to optind=1 plus special behaviour
    argv--;     // so we shift the arguments to satisfy getopt()
    argc++;

    // some platforms apparently require optreset = 1; - which?
    int opt;
    char *mid;
    while ((opt = getopt(argc, argv, "d:i:n:o:")) > 0) {
      switch (opt) {
      case 'd':
        driver = ao_driver_id(optarg);
        if (driver < 0)
          die("could not find ao driver %s", optarg);
        break;
      case 'i':
        ao_append_option(&ao_opts, "id", optarg);
        break;
      case 'n':
        ao_append_option(&ao_opts, "dev", optarg);
        // Old libao versions (for example, 0.8.8) only support
        // "dsp" instead of "dev".
        ao_append_option(&ao_opts, "dsp", optarg);
        break;
      case 'o':
        mid = strchr(optarg, '=');
        if (!mid)
          die("Expected an = in audio option %s", optarg);
        *mid = 0;
        ao_append_option(&ao_opts, optarg, mid + 1);
        break;
      default:
        help();
        die("Invalid audio option -%c specified", opt);
      }
    }

    if (optind < argc)
      die("Invalid audio argument: %s", argv[optind]);

    // get_permissible_configuration_settings();
  }
  return 0;
}

static void deinit(void) {
  if (dev != NULL)
    ao_close(dev);
  dev = NULL;
  ao_shutdown();
}

static int play(void *buf, int samples, __attribute__((unused)) int sample_type,
                __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {
  int response = 0;

  if (dev == NULL) {
    debug(1, "ao play(): ao_open_live to play %d samples, bytes per sample: %u, channels: %u",
          samples, ao_output_format.bits / 8, ao_output_format.channels);
    dev = ao_open_live(driver, &ao_output_format, ao_opts);
  }
  // debug(1,"ao play(): play %d samples, bytes per sample: %u, channels: %u", samples,
  // ao_output_format.bits/8, ao_output_format.channels);
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState); // make this un-cancellable
  response = ao_play(dev, buf, samples * ao_output_format.bits / 8 * ao_output_format.channels);
  pthread_setcancelstate(oldState, NULL);
  return response;
}

static void stop(void) {
  if (dev != NULL) {
    debug(1, "ao stop(): ao_close");
    ao_close(dev);
    dev = NULL;
  }
}

audio_output audio_ao = {.name = "ao",
                         .help = &help,
                         .init = &init,
                         .deinit = &deinit,
                         .configure = &configure,
                         .get_configuration = &get_configuration,
                         // .start = &start,
                         .stop = &stop,
                         .is_running = NULL,
                         .flush = NULL,
                         .delay = NULL,
                         .stats = NULL,
                         .play = &play,
                         .volume = NULL,
                         .parameters = NULL,
                         .mute = NULL};
