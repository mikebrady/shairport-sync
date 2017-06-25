/*
 * sndio output driver. This file is part of Shairport.
 * Copyright (c) 2013 Dimitri Sokolyuk <demon@dim13.org>
 * Copyright (c) 2017 Tobias Kortkamp <t@tobik.me>
 *
 * Modifications for audio synchronisation
 * and related work, copyright (c) Mike Brady 2014
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
#include <sndio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void help(void);
static int init(int, char **);
static void onmove_cb(void *, int);
static void deinit(void);
static void start(int, int);
static void play(short[], int);
static void stop(void);
static void onmove_cb(void *, int);
static int delay(long *);
static void flush(void);

audio_output audio_sndio = {.name = "sndio",
                            .help = &help,
                            .init = &init,
                            .deinit = &deinit,
                            .start = &start,
                            .stop = &stop,
                            .flush = &flush,
                            .delay = &delay,
                            .play = &play,
                            .volume = NULL,
                            .parameters = NULL,
                            .mute = NULL,
                            .change_output_device = NULL};

static pthread_mutex_t sndio_mutex = PTHREAD_MUTEX_INITIALIZER;
static struct sio_hdl *hdl;
static int framesize;
static size_t played;
static size_t written;
int64_t time_of_last_onmove_cb;
int at_least_one_onmove_cb_seen;

struct sndio_formats {
  const char *name;
  enum sps_format_t fmt;

  unsigned int bits;
  unsigned int bps;
  unsigned int sig;
  unsigned int le;
};

static struct sndio_formats formats[] = {
    {"s8", SPS_FORMAT_S8, 8, 1, 1, SIO_LE_NATIVE},
    {"u8", SPS_FORMAT_U8, 8, 1, 0, SIO_LE_NATIVE},
    {"s16", SPS_FORMAT_S16, 16, 2, 1, SIO_LE_NATIVE},
    {"s24", SPS_FORMAT_S24, 24, 4, 1, SIO_LE_NATIVE},
    {"s24le3", SPS_FORMAT_S24_3LE, 24, 3, 1, 1},
    {"s24be3", SPS_FORMAT_S24_3BE, 24, 3, 1, 0},
    {"s32", SPS_FORMAT_S32, 24, 4, 1, SIO_LE_NATIVE}};

static void help() {
  printf("    -d output-device    set the output device [default*|...]\n");
}

static int init(int argc, char **argv) {
  struct sio_par par;
  int i, found, opt, round, rate, bufsz;
  const char *devname, *tmp;
  

  sio_initpar(&par);
  par.rate = 44100;
  par.pchan = 2;
  par.bits = 16;
  par.bps = SIO_BPS(par.bits);
  par.le = 1;
  par.sig = 1;
  devname = SIO_DEVANY;
  
  if (config.cfg != NULL) {
    if (!config_lookup_string(config.cfg, "sndio.device", &devname))
      devname = SIO_DEVANY;
    if (config_lookup_int(config.cfg, "sndio.rate", &rate)) {
      if (rate % 44100 == 0 && rate >= 44100 && rate <= 352800) {
        par.rate = rate;
      } else {
        die("sndio: output rate must be a multiple of 44100 and 44100 <= rate <= "
            "352800");
      }
    }
    if (config_lookup_int(config.cfg, "sndio.bufsz", &bufsz)) {
      if (bufsz > 0) {
        par.appbufsz = bufsz;
      } else {
        die("sndio: bufsz must be > 0");
      }
    }
    if (config_lookup_int(config.cfg, "sndio.round", &round)) {
      if (round > 0) {
        par.round = round;
      } else {
        die("sndio: round must be > 0");
      }
    }
    if (config_lookup_string(config.cfg, "sndio.format", &tmp)) {
      for (i = 0, found = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
        if (strcmp(formats[i].name, tmp) == 0) {
          config.output_format = formats[i].fmt;
          found = 1;
          break;
        }
      }
      if (!found)
        die("Invalid output format \"%s\". Should be one of: s8, u8, s16, s24, "
            "s24le3, s24be3, s32",
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
  pthread_mutex_lock(&sndio_mutex);
  debug(1, "Output device name is \"%s\".", devname);
  hdl = sio_open(devname, SIO_PLAY, 0);
  if (!hdl)
    die("sndio: cannot open audio device");

  written = played = 0;
  time_of_last_onmove_cb = 0;
  at_least_one_onmove_cb_seen = 0;

  for (i = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
    if (formats[i].fmt == config.output_format) {
      par.bits = formats[i].bits;
      par.bps = formats[i].bps;
      par.sig = formats[i].sig;
      par.le = formats[i].le;
      break;
    }
  }

  if (!sio_setpar(hdl, &par) || !sio_getpar(hdl, &par))
    die("sndio: failed to set audio parameters");
  for (i = 0, found = 0; i < sizeof(formats) / sizeof(formats[0]); i++) {
    if (formats[i].bits == par.bits && formats[i].bps == par.bps &&
        formats[i].sig == par.sig && formats[i].le == par.le) {
      config.output_format = formats[i].fmt;
      found = 1;
      break;
    }
  }
  if (!found)
    die("sndio: failed to negotiate audio parameters");

  framesize = par.bps * par.pchan;
  config.output_rate = par.rate;
  config.audio_backend_buffer_desired_length = 1.0 * par.bufsz / par.rate;
  config.audio_backend_latency_offset = 0;

  sio_onmove(hdl, onmove_cb, NULL);

  pthread_mutex_unlock(&sndio_mutex);
  return 0;
}

static void deinit() {
  pthread_mutex_lock(&sndio_mutex);
  sio_close(hdl);
  pthread_mutex_unlock(&sndio_mutex);
}

static void start(int sample_rate, int sample_format) {
  pthread_mutex_lock(&sndio_mutex);
  if (!sio_start(hdl))
    die("sndio: unable to start");
  written = played = 0;
  time_of_last_onmove_cb = 0;
  at_least_one_onmove_cb_seen = 0;
  pthread_mutex_unlock(&sndio_mutex);
}

static void play(short buf[], int frames) {
  if (frames > 0) {
    pthread_mutex_lock(&sndio_mutex);
    written += sio_write(hdl, buf, frames * framesize);
    pthread_mutex_unlock(&sndio_mutex);
  }
}

static void stop() {
  pthread_mutex_lock(&sndio_mutex);
  if (!sio_stop(hdl))
    die("sndio: unable to stop");
  written = played = 0;
  pthread_mutex_unlock(&sndio_mutex);
}

static void onmove_cb(void *arg, int delta) {
  pthread_mutex_lock(&sndio_mutex);
  time_of_last_onmove_cb = get_absolute_time_in_fp();
  at_least_one_onmove_cb_seen = 1;
  played += delta;
  pthread_mutex_unlock(&sndio_mutex);
}

static int delay(long *_delay) {
  pthread_mutex_lock(&sndio_mutex);
  size_t estimated_extra_frames_output = 0;
  if (at_least_one_onmove_cb_seen) { // when output starts, the onmove_cb callback will be made
    // calculate the difference in time between now and when the last callback occoured,
    // and use it to estimate the frames that would have been output
    uint64_t time_difference = get_absolute_time_in_fp() - time_of_last_onmove_cb;
    uint64_t frame_difference = time_difference * 44100;
    uint64_t frame_difference_big_integer = frame_difference>>32;
    estimated_extra_frames_output = frame_difference_big_integer;
    // debug(1,"Frames played to last cb: %d, estimated to current time: %d.",played,estimated_extra_frames_output);
  }
  *_delay = (written / framesize) - (played+estimated_extra_frames_output);
  pthread_mutex_unlock(&sndio_mutex);
  return 0;
}

static void flush() {
  pthread_mutex_lock(&sndio_mutex);
  if (!sio_stop(hdl) || !sio_start(hdl))
    die("sndio: unable to flush");
  written = played = 0;
  pthread_mutex_unlock(&sndio_mutex);
}
