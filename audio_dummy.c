/*
 * dummy output driver. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * All rights reserved.
 *
 * Modifications for audio synchronisation
 * and related work, copyright (c) Mike Brady 2014--2025
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
#include <inttypes.h> // for PRId64 and friends
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

static int init(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) { return 0; }

static void deinit(void) {}

static void start(int sample_rate, __attribute__((unused)) int sample_format) {
  debug(1, "dummy audio output started at %d frames per second.", sample_rate);
}
// clang-format off
// Here is a brief explanation of the parameters:
// sample_type is 'play_samples_are_untimed' or 'play_samples_are_timed' defined in audio.h.
//   untimed samples are typically silence used by Shairport Sync itself to set up synchronisation or to fill for missing packets of audio -- the timestamp and playtime are zero.
//   timed samples come from the client.
// timestamp is the RTP timestamp given to the first frame by the client
// playtime is when the first frame should to be played. It is given in "local time".
// local time is CLOCK_MONOTONIC_RAW if available, or CLOCK_MONOTONIC otherwise, expressed in nanoseconds as an unsigned 64-bit integer (See get_absolute_time_in_ns() in common.c)
// clang-format on

static int play(__attribute__((unused)) void *buf, __attribute__((unused)) int samples,
                int sample_type, uint32_t timestamp, uint64_t playtime) {

  // sample code using some of the extra information
  if (sample_type == play_samples_are_timed) {
    int64_t lead_time = playtime - get_absolute_time_in_ns();
    debug(2, "leadtime for frame %u is %" PRId64 " nanoseconds, i.e. %f seconds.", timestamp,
          lead_time, 0.000000001 * lead_time);
  }

  return 0;
}

static void stop(void) { debug(1, "dummy audio stopped\n"); }

audio_output audio_dummy = {.name = "dummy",
                            .help = NULL,
                            .init = &init,
                            .deinit = &deinit,
                            .configure = NULL,
                            .start = &start,
                            .stop = &stop,
                            .is_running = NULL,
                            .flush = NULL,
                            .delay = NULL,
                            .stats = NULL,
                            .play = &play,
                            .volume = NULL,
                            .parameters = NULL,
                            .mute = NULL};
