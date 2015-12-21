/*
 * libao output driver. This file is part of Shairport.
 * Copyright (c) James Laird 2013
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

#include <stdio.h>
#include <unistd.h>
#include <memory.h>
#include <ao/ao.h>
#include "common.h"
#include "audio.h"

ao_device *dev = NULL;

static void help(void) {
  printf("    -d driver           set the output driver\n"
         "    -o name=value       set an arbitrary ao option\n"
         "    -i id               shorthand for -o id=<id>\n"
         "    -n name             shorthand for -o dev=<name> -o dsp=<name>\n");
}

static int init(int argc, char **argv) {
  const char *str;
  int value;
  ao_initialize();
  int driver = ao_default_driver_id();
  ao_option *ao_opts = NULL;

  config.audio_backend_buffer_desired_length = 44100; // one second.
  config.audio_backend_latency_offset = 0;
  
  // get settings from settings file first, allow them to be overridden by command line options

  if (config.cfg != NULL) {
    /* Get the desired buffer size setting. */
    if (config_lookup_int(config.cfg, "ao.audio_backend_buffer_desired_length_software", &value)) {
      if ((value < 0) || (value > 66150))
        die("Invalid a0 audio backend buffer desired length \"%d\". It should be between 0 and "
            "66150, default is 44100",
            value);
      else {
        config.audio_backend_buffer_desired_length = value;
      }
    }
    
    /* Get the latency offset. */
    if (config_lookup_int(config.cfg, "ao.audio_backend_latency_offset", &value)) {
      if ((value < -66150) || (value > 66150))
        die("Invalid ao audio backend buffer latency offset \"%d\". It should be between -66150 and +66150, default is 0",
            value);
      else
        config.audio_backend_latency_offset = value;
    }
  }


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

  ao_sample_format fmt;
  memset(&fmt, 0, sizeof(fmt));

  fmt.bits = 16;
  fmt.rate = 44100;
  fmt.channels = 2;
  fmt.byte_format = AO_FMT_NATIVE;

  dev = ao_open_live(driver, &fmt, ao_opts);

  return dev ? 0 : 1;
}

static void deinit(void) {
  if (dev)
    ao_close(dev);
  dev = NULL;
  ao_shutdown();
}

static void start(int sample_rate) {
  if (sample_rate != 44100)
    die("unexpected sample rate!");
}

static void play(short buf[], int samples) { ao_play(dev, (char *)buf, samples * 4); }

static void stop(void) {}

audio_output audio_ao = {.name = "ao",
                         .help = &help,
                         .init = &init,
                         .deinit = &deinit,
                         .start = &start,
                         .stop = &stop,
                         .flush = NULL,
                         .delay = NULL,
                         .play = &play,
                         .volume = NULL,
                         .parameters = NULL,
                         .mute = NULL};
