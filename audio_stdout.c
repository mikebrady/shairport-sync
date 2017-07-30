/*
 * stdout output driver. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2015
 *
 * Based on pipe output driver
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

#include "audio.h"
#include "common.h"
#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int fd = -1;

static void start(int sample_rate, int sample_format) { fd = STDOUT_FILENO; }

static void play(short buf[], int samples) {
  char errorstring[1024];
  int warned = 0;
  int rc = write(fd, buf, samples * 4); 
  if ((rc<0) && (warned==0)) {
    strerror_r(errno,(char*)errorstring,1024);
    warn("Error %d writing to stdout: \"%s\".",errno,errorstring);
    warned = 1;
  }
}

static void stop(void) {
  // don't close stdout
}

static int init(int argc, char **argv) {
  // set up default values first
  config.audio_backend_buffer_desired_length = 1.0;
  config.audio_backend_latency_offset = 0;
  
  // get settings from settings file  
  // do the "general" audio  options. Note, these options are in the "general" stanza!
  parse_general_audio_options();
  return 0;
}

static void deinit(void) {
  // don't close stdout
}

static void help(void) { printf("    stdout takes no arguments\n"); }

audio_output audio_stdout = {.name = "stdout",
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
                             .mute = NULL,
                             .change_output_device = NULL};
