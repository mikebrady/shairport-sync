/*
 * sndio output driver. This file is part of Shairport.
 * Copyright (c) 2013 Dimitri Sokolyuk <demon@dim13.org>
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

#include <stdio.h>
#include <unistd.h>
#include <sndio.h>
#include "audio.h"

static struct sio_hdl *sio;
static struct sio_par par;

static int init(int argc, char **argv) {
  sio = sio_open(SIO_DEVANY, SIO_PLAY, 0);
  if (!sio)
    die("sndio: cannot connect to sound server");

  sio_initpar(&par);

  par.bits = 16;
  par.rate = 44100;
  par.pchan = 2;
  par.le = SIO_LE_NATIVE;
  par.sig = 1;

  if (!sio_setpar(sio, &par))
    die("sndio: failed to set audio parameters");
  if (!sio_getpar(sio, &par))
    die("sndio: failed to get audio parameters");

  config.audio_backend_buffer_desired_length = 44100; // one second.
  config.audio_backend_latency_offset = 0;

  return 0;
}

static void deinit(void) { sio_close(sio); }

static void start(int sample_rate) {
  if (sample_rate != par.rate)
    die("unexpected sample rate!");
  sio_start(sio);
}

static void play(short buf[], int samples) {
  sio_write(sio, (char *)buf, samples * par.bps * par.pchan);
}

static void stop(void) { sio_stop(sio); }

static void help(void) {
  printf("    There are no options for sndio audio.\n");
  printf("    Use AUDIODEVICE environment variable.\n");
}

static void volume(double vol) {
  unsigned int v = vol * SIO_MAXVOL;
  sio_setvol(sio, v);
}

audio_output audio_sndio = {.name = "sndio",
                            .help = &help,
                            .init = &init,
                            .deinit = &deinit,
                            .start = &start,
                            .stop = &stop,
                            .flush = NULL,
                            .delay = NULL,
                            .play = &play,
                            .volume = &volume,
                            .parameters = NULL,
                            .mute= NULL};
