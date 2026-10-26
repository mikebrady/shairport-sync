/*
 * stdout output driver. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2015--2025
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
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static int fd = STDOUT_FILENO;
static int warned = 0;
static unsigned int bytes_per_frame = 0;
static unsigned int frame_rate = 0;

// The stdout backend can't see how much of what it has written is still waiting to be heard -- it might be
// buffered by whatever reads STDOUT -- so it has no delay() and can't keep in sync. If the reader can say,
// set "delay_file" in the "stdout" stanza: the reader keeps that file holding two numbers, the nanoseconds
// of audio it has read that are still to be heard, and the CLOCK_REALTIME time in nanoseconds when that was
// true, rewriting it as it reads more. delay() then adds what is still unread in the pipe.
static const char *delay_file = NULL;

static int delay(long *the_delay) {
  if (delay_file == NULL || frame_rate == 0 || bytes_per_frame == 0)
    return -ENODEV;
  int unread = 0; // bytes written but not yet read, if STDOUT is a pipe
  if (ioctl(fd, FIONREAD, &unread) != 0 || unread < 0)
    unread = 0;
  long long pending = 0, at = 0;
  int dfd = open(delay_file, O_RDONLY);
  if (dfd >= 0) {
    char b[64];
    ssize_t n = pread(dfd, b, sizeof(b) - 1, 0);
    close(dfd);
    if (n > 0) {
      b[n] = 0;
      if (sscanf(b, "%lld %lld", &pending, &at) != 2) {
        pending = 0;
        at = 0;
      }
    }
  }
  if (at > 0) { // the reader plays on after it writes, so what it said is reduced by the time since
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long long now = (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
    pending -= now - at;
  }
  if (pending < 0)
    pending = 0;
  *the_delay = unread / (int)bytes_per_frame + (long)(pending * frame_rate / 1000000000LL);
  return 0;
}

static int play(void *buf, int samples, __attribute__((unused)) int sample_type,
                __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {
  char errorstring[1024];
  if (bytes_per_frame == 0)
    debug(1, "stdout: bytes per frame not initialised before play()!");
  int rc = write(fd, buf, samples * bytes_per_frame);
  if ((rc < 0) && (warned == 0)) {
    strerror_r(errno, (char *)errorstring, 1024);
    warn("error %d writing to stdout (fd: %d): \"%s\".", errno, fd, errorstring);
    warned = 1;
  }
  return rc;
}

static int init(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) {
  // set up default values first
  config.audio_backend_buffer_desired_length = 1.0;
  config.audio_backend_latency_offset = 0;

  // get settings from settings file, passing in defaults for format_set, rate_set and channel_set
  // Note, these options may be in the "general" stanza or the named stanza
#ifdef CONFIG_AIRPLAY_2
  parse_audio_options("stdout", (1 << SPS_FORMAT_S32_LE), (1 << SPS_RATE_48000), (1 << 2));
#else
  parse_audio_options("stdout", (1 << SPS_FORMAT_S16_LE), (1 << SPS_RATE_44100), (1 << 2));
#endif
  if (config.cfg != NULL) {
    const char *str;
    if (config_lookup_non_empty_string(config.cfg, "stdout.delay_file", &str))
      delay_file = str;
  }
  return 0;
}

static int32_t get_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  // use the standard format/rate/channel search to get a suitable configuration. No
  // check_configuration() method needs to be passed to search_for_suitable_configuration() because
  // it will always return a valid choice based on any settings and the defaults
  return search_for_suitable_configuration(channels, rate, format, NULL);
}

static int configure(int32_t requested_encoded_format, __attribute__((unused)) char **channel_map) {
  int response = 0;
  unsigned int bytes_per_sample =
      sps_format_sample_size(FORMAT_FROM_ENCODED_FORMAT(requested_encoded_format));
  if (bytes_per_sample == 0) {
    debug(1, "stdout: unknown output format.");
    bytes_per_sample = 4; // emergency hack
    response = EINVAL;
  }
  bytes_per_frame = bytes_per_sample * CHANNELS_FROM_ENCODED_FORMAT(requested_encoded_format);
  frame_rate = RATE_FROM_ENCODED_FORMAT(requested_encoded_format);
  return response;
}

audio_output audio_stdout = {.name = "stdout",
                             .help = NULL,
                             .init = &init,
                             .deinit = NULL,
                             .get_configuration = &get_configuration,
                             .configure = &configure,
                             .stop = NULL,
                             .is_running = NULL,
                             .flush = NULL,
                             .delay = &delay,
                             .stats = NULL,
                             .play = &play,
                             .volume = NULL,
                             .parameters = NULL,
                             .mute = NULL};
