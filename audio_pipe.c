/*
 * pipe output driver. This file is part of Shairport.
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
#include <fcntl.h>
#include <memory.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include "common.h"
#include "audio.h"

static int fd = -1;

char *pipename = NULL;

static void start(int sample_rate) {
  // this will leave fd as -1 if a reader hasn't been attached
  fd = open(pipename, O_WRONLY | O_NONBLOCK);
}

static void play(short buf[], int samples) {
  // if the file is not open, try to open it.
  if (fd == -1) {
     fd = open(pipename, O_WRONLY | O_NONBLOCK); 
  }
  // if it's got a reader, write to it.
  if (fd != -1) {
    int ignore = non_blocking_write(fd, buf, samples * 4);
  }
}

static void stop(void) {
// Don't close the pipe just because a play session has stopped.
//  if (fd > 0)
//    close(fd);
}

static int init(int argc, char **argv) {
  debug(1, "pipe init");
  const char *str;
  int value;
  
  config.audio_backend_buffer_desired_length = 44100; // one second.
  config.audio_backend_latency_offset = 0;

  if (config.cfg != NULL) {
    /* Get the Output Pipename. */
    const char *str;
    if (config_lookup_string(config.cfg, "pipe.name", &str)) {
      pipename = (char *)str;
    }
 
    if ((pipename) && (strcasecmp(pipename, "STDOUT") == 0))
      die("Can't use \"pipe\" backend for STDOUT. Use the \"stdout\" backend instead.");

    /* Get the desired buffer size setting. */
    if (config_lookup_int(config.cfg, "pipe.audio_backend_buffer_desired_length", &value)) {
      if ((value < 0) || (value > 132300))
        die("Invalid pipe audio backend buffer desired length \"%d\". It should be between 0 and 132300, default is 44100",
            value);
      else
        config.audio_backend_buffer_desired_length = value;
    }

    /* Get the latency offset. */
    if (config_lookup_int(config.cfg, "pipe.audio_backend_latency_offset", &value)) {
      if ((value < -66150) || (value > 66150))
        die("Invalid pipe audio backend buffer latency offset \"%d\". It should be between -66150 and +66150, default is 0",
            value);
      else
        config.audio_backend_latency_offset = value;
    }
  }
  
  if ((pipename == NULL) && (argc != 1))
    die("bad or missing argument(s) to pipe");

  if (argc == 1)
    pipename = strdup(argv[0]);
  
  
  // here, create the pipe
  if (mkfifo(pipename, 0644) && errno != EEXIST)
    die("Could not create output pipe \"%s\"", pipename);

  debug(1, "Pipename is \"%s\"", pipename);

  return 0;
}

static void deinit(void) {
   if (fd > 0)
    close(fd);
}

static void help(void) {
  printf("    pipe takes 1 argument: the name of the FIFO to write to.\n");
}

audio_output audio_pipe = {.name = "pipe",
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
