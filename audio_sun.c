/*
 * sun output driver. This file is part of Shairport Sync.
 * Copyright (c) 2013 Dimitri Sokolyuk <demon@dim13.org>
 * Copyright (c) 2017 Tobias Kortkamp <t@tobik.me>
 * Copyright (c) 2021 Nia Alarie <nia@NetBSD.org>
 *
 * Modifications for audio synchronisation
 * and related work, copyright (c) Mike Brady 2014 -- 2017
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
#include <pthread.h>
#include <sys/audioio.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#ifdef __sun
#include <sys/conf.h>
#include <stropts.h>
#endif
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

static void help(void);
static int init(int, char **);
static void deinit(void);
static void start(int, int);
static int play(void *, int);
static void stop(void);
static int delay(long *);
static void flush(void);

audio_output audio_sun = {.name = "sun",
                            .help = &help,
                            .init = &init,
                            .deinit = &deinit,
                            .prepare = NULL,
                            .start = &start,
                            .stop = &stop,
                            .is_running = NULL,
                            .flush = &flush,
                            .delay = &delay,
                            .play = &play,
                            .volume = NULL,
                            .parameters = NULL,
                            .mute = NULL};

static pthread_mutex_t sun_mutex = PTHREAD_MUTEX_INITIALIZER;
static int framesize;
static int fd;

struct sun_format {
  const char *name;
  sps_format_t fmt;
  unsigned int encoding;
  unsigned int precision;
};

/* SunOS compatibility defines for NetBSD extensions. */

#ifndef AUDIO_ENCODING_SLINEAR
#define AUDIO_ENCODING_SLINEAR AUDIO_ENCODING_LINEAR
#endif

#ifndef AUDIO_ENCODING_ULINEAR
#define AUDIO_ENCODING_ULINEAR AUDIO_ENCODING_LINEAR8
#endif

#ifndef AUDIO_ENCODING_SLINEAR_LE
#define AUDIO_ENCODING_SLINEAR_LE AUDIO_ENCODING_LINEAR
#endif

#ifndef AUDIO_ENCODING_SLINEAR_BE
#define AUDIO_ENCODING_SLINEAR_BE AUDIO_ENCODING_LINEAR
#endif

#ifndef AUDIO_GETBUFINFO
#define AUDIO_GETBUFINFO AUDIO_GETINFO
#endif

#ifndef DEFAULT_DEV
#define DEFAULT_DEV "/dev/audio"
#endif

static struct sun_format formats[] = {{"U8", SPS_FORMAT_U8, AUDIO_ENCODING_ULINEAR, 8},
                                         {"S8", SPS_FORMAT_S8, AUDIO_ENCODING_SLINEAR, 16},
                                         {"S16", SPS_FORMAT_S16, AUDIO_ENCODING_SLINEAR, 16},
                                         {"S16_LE", SPS_FORMAT_S16_LE, AUDIO_ENCODING_SLINEAR_LE, 16},
                                         {"S16_BE", SPS_FORMAT_S16_BE, AUDIO_ENCODING_SLINEAR_BE, 16},
                                         {"S32", SPS_FORMAT_S16, AUDIO_ENCODING_SLINEAR, 32},
                                         {"S32_LE", SPS_FORMAT_S32_LE, AUDIO_ENCODING_SLINEAR_LE, 32},
                                         {"S32_BE", SPS_FORMAT_S32_BE, AUDIO_ENCODING_SLINEAR_BE, 32}};

static void help() { printf("    -d output-device    set the output device [default*|...]\n"); }

static int init(int argc, char **argv) {
  struct audio_info info;
  int found, opt;
  unsigned int i;
  const char *devname, *tmp;

  // set up default values first

  AUDIO_INITINFO(&info);
  info.play.encoding = AUDIO_ENCODING_SLINEAR_LE;
  info.play.sample_rate = 44100;
  info.play.channels = 2;
  info.play.precision = 16;

  devname = DEFAULT_DEV;

  config.audio_backend_buffer_desired_length = 1.0;
  config.audio_backend_buffer_interpolation_threshold_in_seconds =
      0.25; // below this, soxr interpolation will not occur -- it'll be basic interpolation
            // instead.
  config.audio_backend_latency_offset = 0;

  // get settings from settings file

  // do the "general" audio  options. Note, these options are in the "general" stanza!
  parse_general_audio_options();

  // get the specific settings

  if (config.cfg != NULL) {
    if (!config_lookup_string(config.cfg, "sun.device", &devname))
      devname = DEFAULT_DEV;
    if (config_lookup_string(config.cfg, "sun.format", &tmp)) {
      for (i = 0, found = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        if (strcasecmp(formats[i].name, tmp) == 0) {
          config.output_format = formats[i].fmt;
          info.play.encoding = formats[i].encoding;
          info.play.precision = formats[i].precision;
          found = 1;
          break;
        }
      }
      if (!found)
        die("Invalid output format \"%s\". Should be one of: U8, S8, S16, S16_LE, S16_BE, S32, S32_LE, S32_BE",
            tmp);
    }
  }
  optind = 1; // optind=0 is equivalent to optind=1 plus special behaviour
  argv--;     // so we shift the arguments to satisfy getopt()
  argc++;
  while ((opt = getopt(argc, argv, "d:")) > 0) {
    switch (opt) {
    case 'd':
      devname = optarg;
      break;
    default:
      help();
      die("Invalid audio option -%c specified", opt);
    }
  }
  if (optind < argc)
    die("Invalid audio argument: %s", argv[optind]);

  pthread_mutex_lock(&sun_mutex);
  debug(1, "Output device name is \"%s\".", devname);
  fd = open(devname, O_WRONLY);
  if (fd < 0)
    die("sun: cannot open audio device");

  if (ioctl(fd, AUDIO_SETINFO, &info) < 0)
    die("sun: failed to set audio parameters");
  if (ioctl(fd, AUDIO_GETINFO, &info) < 0)
    die("sun: failed to get audio parameters");

  framesize = (info.play.precision / 8) * info.play.channels;
  config.output_rate = info.play.sample_rate;
  config.audio_backend_buffer_desired_length = 1.0 * info.play.buffer_size / info.play.sample_rate;
  config.audio_backend_latency_offset = 0;

  pthread_mutex_unlock(&sun_mutex);
  return 0;
}

static void deinit() {
  pthread_mutex_lock(&sun_mutex);
  close(fd);
  pthread_mutex_unlock(&sun_mutex);
}

static void start(__attribute__((unused)) int sample_rate,
                  __attribute__((unused)) int sample_format) {
  struct audio_info tmpinfo;

  AUDIO_INITINFO(&tmpinfo);
  tmpinfo.play.pause = false;

  pthread_mutex_lock(&sun_mutex);
  if (ioctl(fd, AUDIO_SETINFO, &tmpinfo) < 0)
      die("sun: unable to start");
  pthread_mutex_unlock(&sun_mutex);
}

static int play(void *buf, int frames) {
  if (frames > 0) {
    pthread_mutex_lock(&sun_mutex);
    if (write(fd, buf, frames * framesize) < 0)
      die("sun: unable to write");
    pthread_mutex_unlock(&sun_mutex);
  }
  return 0;
}

static void stop() {
  struct audio_info tmpinfo;

  AUDIO_INITINFO(&tmpinfo);
  tmpinfo.play.pause = true;

  pthread_mutex_lock(&sun_mutex);
  (void)ioctl(fd, AUDIO_DRAIN, NULL);
  if (ioctl(fd, AUDIO_SETINFO, &tmpinfo) < 0)
      die("sun: unable to stop");
  pthread_mutex_unlock(&sun_mutex);
}

static int delay(long *_delay) {
  struct audio_info tmpinfo;

  pthread_mutex_lock(&sun_mutex);
  if (ioctl(fd, AUDIO_GETBUFINFO, &tmpinfo) < 0)
    die("sun: unable to get audio buffer info");
  *_delay = tmpinfo.play.seek / framesize;
  pthread_mutex_unlock(&sun_mutex);
  return 0;
}

static void flush() {
  pthread_mutex_lock(&sun_mutex);
#ifdef AUDIO_FLUSH
  if (ioctl(fd, AUDIO_FLUSH, NULL) < 0)
    die("sun: unable to flush");
#else
  if (ioctl(fd, I_FLUSH, FLUSHW) < 0)
    die("sun: unable to flush");
#endif
  pthread_mutex_unlock(&sun_mutex);
}
