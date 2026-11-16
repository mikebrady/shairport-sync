/*
 * blockaudio output driver. This file is part of Shairport Sync.
 * Copyright (c) Marius DKM 2026
 * All rights reserved.
 *
 * Writes the audio to a named pipe as a sequence of records, each a header followed by a block of
 * audio. The header gives the block's format, its RTP timestamp and the time at which its first
 * frame should be played. The format is documented in "ADVANCED TOPICS/BlockAudio.md".
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
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "utilities/network_utilities.h"

// header version 1, in the host's byte order
#define BLOCKAUDIO_MAGIC "SSBA"
#define BLOCKAUDIO_VERSION 1
#define BLOCKAUDIO_HEADER_LENGTH 40
#define OFFSET_MAGIC 0
#define OFFSET_VERSION 4
#define OFFSET_HEADER_LENGTH 6
#define OFFSET_PAYLOAD_LENGTH 8
#define OFFSET_SEQUENCE 12
#define OFFSET_HEADER_CRC32 16
#define OFFSET_FLAGS 20
#define OFFSET_RESERVED 22
#define OFFSET_FORMAT 24
#define OFFSET_RTP 28
#define OFFSET_PLAY_TIME 32

enum {
  BLOCKAUDIO_FLAG_TIMED = 1, // rtp and play_time are valid
  BLOCKAUDIO_FLAG_FLUSH = 2, // discard audio received but not yet played
  BLOCKAUDIO_FLAG_STOP = 4,  // the play session has ended
};

// a write to a pipe of up to PIPE_BUF bytes is all or nothing, so a marker is never cut short
#if BLOCKAUDIO_HEADER_LENGTH > PIPE_BUF
#error "a blockaudio header must not be longer than PIPE_BUF"
#endif

static int fd = -1;
static int32_t encoded_format = 0;
static uint32_t sequence = 0;
// guards the variables above, and makes sure that only one thread writes to the pipe at a time
static pthread_mutex_t record_lock = PTHREAD_MUTEX_INITIALIZER;

// set by configure() and used by play(), which both run on the player thread
static unsigned int bytes_per_frame = 0;

// a flush or stop marker that can't be written at once, because record_lock is held or the pipe
// is full, is left here for the next unlock_record() or record. Never held across a write.
static pthread_mutex_t pending_lock = PTHREAD_MUTEX_INITIALIZER;
static uint16_t pending_flags = 0;

static char *pipename = NULL;
static char *default_pipe_name = "/tmp/shairport-sync-blockaudio";

// compatible with zlib crc32().
static uint32_t crc32_of(const uint8_t *p, size_t length) {
  uint32_t crc = 0xFFFFFFFF;
  // computed bit by bit, as it only ever covers a header.
  for (size_t i = 0; i < length; i++) {
    crc ^= p[i];
    for (int bit = 0; bit < 8; bit++)
      crc = (crc >> 1) ^ (0xEDB88320 & (0 - (crc & 1)));
  }
  return crc ^ 0xFFFFFFFF;
}

// needs record_lock held
static void put_header(uint8_t *p, uint16_t flags, uint32_t payload_length, uint32_t rtp,
                       uint64_t play_time) {
  uint16_t version = BLOCKAUDIO_VERSION;
  uint16_t header_length = BLOCKAUDIO_HEADER_LENGTH;
  uint32_t format = (uint32_t)encoded_format;
  memset(p, 0, BLOCKAUDIO_HEADER_LENGTH); // reserved, and the CRC field while the CRC is computed
  memcpy(p + OFFSET_MAGIC, BLOCKAUDIO_MAGIC, 4);
  memcpy(p + OFFSET_VERSION, &version, sizeof(version));
  memcpy(p + OFFSET_HEADER_LENGTH, &header_length, sizeof(header_length));
  memcpy(p + OFFSET_PAYLOAD_LENGTH, &payload_length, sizeof(payload_length));
  memcpy(p + OFFSET_SEQUENCE, &sequence, sizeof(sequence));
  memcpy(p + OFFSET_FLAGS, &flags, sizeof(flags));
  memcpy(p + OFFSET_FORMAT, &format, sizeof(format));
  memcpy(p + OFFSET_RTP, &rtp, sizeof(rtp));
  memcpy(p + OFFSET_PLAY_TIME, &play_time, sizeof(play_time));
  uint32_t crc = crc32_of(p, BLOCKAUDIO_HEADER_LENGTH);
  memcpy(p + OFFSET_HEADER_CRC32, &crc, sizeof(crc));
  sequence++;
}

// needs record_lock held
static void open_pipe(void) {
  static int last_error = 0;
  // O_NONBLOCK: open() fails with ENXIO rather than waiting for a reader, and a write to a full
  // pipe fails with EAGAIN rather than blocking, so that flush() and stop() never wait.
  fd = open(pipename, O_WRONLY | O_NONBLOCK);
  int error = (fd >= 0) ? 0 : errno;
  if ((error != 0) && (error != ENXIO) && (error != last_error)) { // ENXIO: no reader yet
    char errorstring[1024];
    strerror_r(error, (char *)errorstring, sizeof(errorstring));
    warn("blockaudio: can not open the pipe \"%s\" -- error %d (\"%s\").", pipename, error,
         (char *)errorstring);
  }
  last_error = error;
}

// if the pipe is full, waits for room, or with wait = 0 returns EAGAIN without writing anything.
// on failure, the pipe is closed rather than resumed mid-record, so that a reader never sees the
// rest of a record without its start. The next play() reopens it.
// needs record_lock held
static int write_to_pipe(const uint8_t *buf, size_t size, int wait) {
  for (size_t done = 0; done < size;) {
    ssize_t n = write(fd, buf + done, size - done);
    if (n >= 0) {
      done += n;
    } else if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
      if (!wait)
        return EAGAIN;
      struct pollfd pfd = {.fd = fd, .events = POLLOUT};
      poll(&pfd, 1, -1); // a cancellation point
    } else if (errno != EINTR) {
      if (errno != EPIPE) { // EPIPE just means the reader has gone
        char errorstring[1024];
        strerror_r(errno, (char *)errorstring, sizeof(errorstring));
        debug(1, "blockaudio: error %d writing to the pipe \"%s\": \"%s\".", errno, pipename,
              errorstring);
      }
      safe_socket_close(&fd);
      return -1;
    }
  }
  return 0;
}

static uint16_t get_pending_flags(void) {
  pthread_mutex_lock(&pending_lock);
  uint16_t flags = pending_flags;
  pthread_mutex_unlock(&pending_lock);
  return flags;
}

// writes the pending flush and stop markers as one header-only record.
// if wait is 0 and the pipe is full, they stay pending and EAGAIN is returned.
// needs record_lock held
static int write_pending_markers(int wait) {
  uint16_t flags = get_pending_flags();
  if (flags == 0)
    return 0;
  if (fd >= 0) { // without a reader, there is nothing to mark
    uint8_t header[BLOCKAUDIO_HEADER_LENGTH];
    uint32_t saved_sequence = sequence;
    put_header(header, flags, 0, 0, 0);
    if (write_to_pipe(header, sizeof(header), wait) == EAGAIN) {
      sequence = saved_sequence; // the next record writes the markers
      return EAGAIN;
    }
  }
  pthread_mutex_lock(&pending_lock);
  pending_flags &= ~flags;
  pthread_mutex_unlock(&pending_lock);
  return 0;
}

// writes the pending markers without waiting,
// unless record_lock is held, in which case its holder writes them in unlock_record().
static void try_write_pending_markers(void) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  while ((get_pending_flags() != 0) && (pthread_mutex_trylock(&record_lock) == 0)) {
    int full = (write_pending_markers(0) == EAGAIN);
    pthread_mutex_unlock(&record_lock);
    if (full)
      break; // the next record writes them
  }
  pthread_setcancelstate(oldState, NULL);
}

static void unlock_record(void) {
  pthread_mutex_unlock(&record_lock);
  try_write_pending_markers();
}

// queues the marker, then writes it at once if record_lock and the pipe are free.
static void write_marker(uint16_t flags) {
  pthread_mutex_lock(&pending_lock);
  pending_flags |= flags;
  pthread_mutex_unlock(&pending_lock);
  try_write_pending_markers();
}

// needs record_lock held
static void write_record(const void *buf, uint32_t payload_length, uint16_t flags, uint32_t rtp,
                         uint64_t play_time) {
  if (fd == -1)
    open_pipe();
  if (fd == -1)
    return;
  write_pending_markers(1); // wait: the markers belong before this record
  if (fd == -1)
    return; // a failed write closes the pipe
  uint8_t header[BLOCKAUDIO_HEADER_LENGTH];
  put_header(header, flags, payload_length, rtp, play_time);
  // header and audio are two writes; record_lock keeps other writers out in between
  if (write_to_pipe(header, sizeof(header), 1) == 0)
    write_to_pipe(buf, payload_length, 1);
  // a flush or stop that came in while the record was being written goes out now, rather than
  // waiting for a next record that may not come, as when playback is paused.
  write_pending_markers(1);
}

// cleanup handler for play(), run only if the player thread is cancelled:
// close the pipe, so the reader can't get half a record.
static void unlock_on_cancel(__attribute__((unused)) void *arg) {
  if (fd >= 0)
    safe_socket_close(&fd);
  pthread_mutex_unlock(&record_lock);
}

static int play(void *buf, int samples, int sample_type, uint32_t timestamp, uint64_t playtime) {
  if ((bytes_per_frame == 0) || (samples <= 0))
    return 0;
  uint64_t payload_length = (uint64_t)samples * bytes_per_frame; // 64 bits, so it can't wrap
  if (payload_length > UINT32_MAX) {
    debug(1, "blockaudio: a block of %d frames is too long for a record.", samples);
    return 0;
  }

  int timed = (sample_type == play_samples_are_timed);

  pthread_mutex_lock(&record_lock);
  pthread_cleanup_push(unlock_on_cancel, NULL);
  write_record(buf, (uint32_t)payload_length, timed ? BLOCKAUDIO_FLAG_TIMED : 0,
               timed ? timestamp : 0, timed ? playtime : 0);
  pthread_cleanup_pop(0);
  unlock_record();
  return 0;
}

static void flush(void) { write_marker(BLOCKAUDIO_FLAG_FLUSH); }

static void stop(void) {
  // don't close the pipe just because a play session has stopped.
  write_marker(BLOCKAUDIO_FLAG_STOP);
}

static int init(int argc, char **argv) {
  config.audio_backend_buffer_desired_length = 1.0;
  config.audio_backend_latency_offset = 0;

#ifdef CONFIG_AIRPLAY_2
  parse_audio_options("blockaudio", (1 << SPS_FORMAT_S32_LE), (1 << SPS_RATE_48000), (1 << 2));
#else
  parse_audio_options("blockaudio", (1 << SPS_FORMAT_S16_LE), (1 << SPS_RATE_44100), (1 << 2));
#endif
  if (config.cfg != NULL) {
    const char *str;
    if (config_lookup_non_empty_string(config.cfg, "blockaudio.name", &str))
      pipename = (char *)str;
  }

  if (argc > 1)
    die("too many command-line arguments to blockaudio");

  if (argc == 1)
    pipename = argv[0]; // command line argument has priority

  if ((pipename) && (strcasecmp(pipename, "STDOUT") == 0))
    die("Can't use the \"blockaudio\" backend for STDOUT.");

  if (pipename == NULL)
    pipename = default_pipe_name;

  mode_t oldumask = umask(000);
  if (mkfifo(pipename, 0666) && errno != EEXIST)
    die("Could not create the blockaudio pipe \"%s\"", pipename);
  umask(oldumask);

  debug(1, "blockaudio pipe name is \"%s\"", pipename);
  return 0;
}

static void deinit(void) {
  pthread_mutex_lock(&record_lock);
  if (fd >= 0)
    safe_socket_close(&fd);
  pthread_mutex_unlock(&record_lock);
}

static void help(void) {
  printf("    Provide the pipe's pathname. The default is \"%s\".\n", default_pipe_name);
}

static int32_t get_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  // every record carries its format, so any configuration the settings allow will do
  return search_for_suitable_configuration(channels, rate, format, NULL);
}

static int configure(int32_t requested_encoded_format, __attribute__((unused)) char **channel_map) {
  int response = 0;
  unsigned int bytes_per_sample =
      sps_format_sample_size(FORMAT_FROM_ENCODED_FORMAT(requested_encoded_format));
  unsigned int channels = CHANNELS_FROM_ENCODED_FORMAT(requested_encoded_format);
  pthread_mutex_lock(&record_lock);
  if ((bytes_per_sample == 0) || (channels == 0)) {
    debug(1, "blockaudio: unsupported output format %s.",
          short_format_description(requested_encoded_format));
    encoded_format = 0;
    bytes_per_frame = 0; // play() won't write anything until a valid format is configured
    response = EINVAL;
  } else {
    encoded_format = requested_encoded_format;
    bytes_per_frame = bytes_per_sample * channels;
  }
  unlock_record();
  return response;
}

audio_output audio_blockaudio = {.name = "blockaudio",
                                 .help = &help,
                                 .init = &init,
                                 .deinit = &deinit,
                                 .prepare = NULL,
                                 .get_configuration = &get_configuration,
                                 .configure = &configure,
                                 .stop = &stop,
                                 .is_running = NULL,
                                 .flush = &flush,
                                 .delay = NULL,
                                 .stats = NULL,
                                 .play = &play,
                                 .volume = NULL,
                                 .parameters = NULL,
                                 .mute = NULL};
