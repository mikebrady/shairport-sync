/*
 * dummy output driver. This file is part of Shairport.
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
#include <sys/time.h>
#include "common.h"
#include "audio.h"

int Fs;
long long starttime, samples_played;

static int init(int argc, char **argv) { return 0; }

static void deinit(void) {}

static void start(int sample_rate) {
  Fs = sample_rate;
  starttime = 0;
  samples_played = 0;
  debug(1, "dummy audio output started at Fs=%d Hz\n", sample_rate);
}

static void play(short buf[], int samples) {}

static void stop(void) { debug(1, "dummy audio stopped\n"); }

static void help(void) { printf("    There are no options for dummy audio.\n"); }

audio_output audio_dummy = {.name = "dummy",
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
