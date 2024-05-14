/*
 * Asynchronous PulseAudio Backend. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2017-2023
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

// Based (distantly, with thanks) on
// http://stackoverflow.com/questions/29977651/how-can-the-pulseaudio-asynchronous-library-be-used-to-play-raw-pcm-data

#include "audio.h"
#include "common.h"
#include <errno.h>
#include <pthread.h>
#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// note -- these are hacked and hardwired into this code.
#define FORMAT PA_SAMPLE_S16NE
#define RATE 44100

// Four seconds buffer -- should be plenty
#define buffer_allocation RATE * 4 * 2 * 2

static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

pa_threaded_mainloop *mainloop;
pa_mainloop_api *mainloop_api;
pa_context *context;
pa_stream *stream;
static char *audio_lmb, *audio_umb, *audio_toq, *audio_eoq;
static size_t audio_size = buffer_allocation;
static size_t audio_occupancy;

void context_state_cb(pa_context *context, void *mainloop);
void stream_state_cb(pa_stream *s, void *mainloop);
void stream_success_cb(pa_stream *stream, int success, void *userdata);
void stream_write_cb(pa_stream *stream, size_t requested_bytes, void *userdata);

int status_error_notifications = 0;
static void check_pa_stream_status(pa_stream *p, const char *message) {
  if (status_error_notifications < 10) {
    if (p == NULL) {
      warn("%s No pulseaudio stream!", message);
      status_error_notifications++;
    } else {
      status_error_notifications++; // assume an error
      switch (pa_stream_get_state(p)) {
      case PA_STREAM_UNCONNECTED:
        warn("%s Pulseaudio stream unconnected!", message);
        break;
      case PA_STREAM_CREATING:
        warn("%s Pulseaudio stream being created!", message);
        break;
      case PA_STREAM_READY:
        status_error_notifications--; // no error
        break;
      case PA_STREAM_FAILED:
        warn("%s Pulseaudio stream failed!", message);
        break;
      case PA_STREAM_TERMINATED:
        warn("%s Pulseaudio stream unexpectedly terminated!", message);
        break;
      default:
        warn("%s Pulseaudio stream in unexpected state %d!", message, pa_stream_get_state(p));
        break;
      }
    }
  }
}

static void connect_stream() {
  // debug(1, "connect_stream");
  uint32_t buffer_size_in_bytes = (uint32_t)2 * 2 * RATE * 0.1; // hard wired in here
  // debug(1, "pa_buffer size is %u bytes.", buffer_size_in_bytes);

  pa_threaded_mainloop_lock(mainloop);
  // Create a playback stream
  pa_sample_spec sample_specifications;
  sample_specifications.format = FORMAT;
  sample_specifications.rate = RATE;
  sample_specifications.channels = 2;

  pa_channel_map map;
  pa_channel_map_init_stereo(&map);

  stream = pa_stream_new(context, "Playback", &sample_specifications, &map);
  pa_stream_set_state_callback(stream, stream_state_cb, mainloop);
  pa_stream_set_write_callback(stream, stream_write_cb, mainloop);
  //    pa_stream_set_latency_update_callback(stream, stream_latency_cb, mainloop);

  // recommended settings, i.e. server uses sensible values
  pa_buffer_attr buffer_attr;
  buffer_attr.maxlength = (uint32_t)-1;
  buffer_attr.tlength = buffer_size_in_bytes;
  buffer_attr.prebuf = (uint32_t)0;
  buffer_attr.minreq = (uint32_t)-1;

  pa_stream_flags_t stream_flags;
  stream_flags = PA_STREAM_START_CORKED | PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_NOT_MONOTONIC |
                 PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_ADJUST_LATENCY;

  int connect_result;

  if (config.pa_sink) {
    // Connect stream to the sink specified in the config
    connect_result =
        pa_stream_connect_playback(stream, config.pa_sink, &buffer_attr, stream_flags, NULL, NULL);
  } else {
    // Connect stream to the default audio output sink
    connect_result =
        pa_stream_connect_playback(stream, NULL, &buffer_attr, stream_flags, NULL, NULL);
  }

  if (connect_result != 0)
    die("could not connect to the pulseaudio playback stream -- the error message is \"%s\".",
        pa_strerror(pa_context_errno(context)));

  // Wait for the stream to be ready
  for (;;) {
    pa_stream_state_t stream_state = pa_stream_get_state(stream);
    if (!PA_STREAM_IS_GOOD(stream_state))
      die("stream state is no longer good while waiting for stream to become ready -- the error "
          "message is \"%s\".",
          pa_strerror(pa_context_errno(context)));
    if (stream_state == PA_STREAM_READY)
      break;
    pa_threaded_mainloop_wait(mainloop);
  }

  pa_threaded_mainloop_unlock(mainloop);
}

static int init(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) {
  // debug(1, "pa_init");
  // set up default values first
  config.audio_backend_buffer_desired_length = 0.35;
  config.audio_backend_buffer_interpolation_threshold_in_seconds =
      0.02; // below this, soxr interpolation will not occur -- it'll be basic interpolation
            // instead.

  config.audio_backend_latency_offset = 0;

  // get settings from settings file

  // do the "general" audio  options. Note, these options are in the "general" stanza!
  parse_general_audio_options();

  // now the specific options
  if (config.cfg != NULL) {
    const char *str;

    /* Get the PulseAudio server name. */
    if (config_lookup_string(config.cfg, "pa.server", &str)) {
      config.pa_server = (char *)str;
    }

    /* Get the Application Name. */
    if (config_lookup_string(config.cfg, "pa.application_name", &str)) {
      config.pa_application_name = (char *)str;
    }

    /* Get the PulseAudio sink name. */
    if (config_lookup_string(config.cfg, "pa.sink", &str)) {
      config.pa_sink = (char *)str;
    }
  }

  // finish collecting settings

  // allocate space for the audio buffer
  audio_lmb = malloc(audio_size);
  if (audio_lmb == NULL)
    die("Can't allocate %d bytes for pulseaudio buffer.", audio_size);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;

  // Get a mainloop and its context
  mainloop = pa_threaded_mainloop_new();
  if (mainloop == NULL)
    die("could not create a pa_threaded_mainloop.");
  mainloop_api = pa_threaded_mainloop_get_api(mainloop);
  if (config.pa_application_name)
    context = pa_context_new(mainloop_api, config.pa_application_name);
  else
    context = pa_context_new(mainloop_api, "Shairport Sync");
  if (context == NULL)
    die("could not create a new context for pulseaudio.");
  // Set a callback so we can wait for the context to be ready
  pa_context_set_state_callback(context, &context_state_cb, mainloop);

  // Lock the mainloop so that it does not run and crash before the context is ready
  pa_threaded_mainloop_lock(mainloop);

  // Start the mainloop
  if (pa_threaded_mainloop_start(mainloop) != 0)
    die("could not start the pulseaudio threaded mainloop");

  if (pa_context_connect(context, config.pa_server, 0, NULL) != 0)
    die("failed to connect to the pulseaudio context -- the error message is \"%s\".",
        pa_strerror(pa_context_errno(context)));

  // Wait for the context to be ready
  for (;;) {
    pa_context_state_t context_state = pa_context_get_state(context);
    if (!PA_CONTEXT_IS_GOOD(context_state))
      die("pa context is not good -- the error message \"%s\".",
          pa_strerror(pa_context_errno(context)));
    if (context_state == PA_CONTEXT_READY)
      break;
    pa_threaded_mainloop_wait(mainloop);
  }

  pa_threaded_mainloop_unlock(mainloop);
  connect_stream();
  check_pa_stream_status(stream, "audio_pa initialisation.");
  return 0;
}

static void deinit(void) {
  check_pa_stream_status(stream, "audio_pa deinitialisation.");
  pa_stream_disconnect(stream);
  pa_threaded_mainloop_stop(mainloop);
  pa_threaded_mainloop_free(mainloop);
  // debug(1, "pa deinit done");
}

static void start(__attribute__((unused)) int sample_rate,
                  __attribute__((unused)) int sample_format) {
  check_pa_stream_status(stream, "audio_pa start.");
}

static int play(void *buf, int samples, __attribute__((unused)) int sample_type,
                __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {
  // debug(1,"pa_play of %d samples.",samples);
  // copy the samples into the queue
  check_pa_stream_status(stream, "audio_pa play.");
  size_t bytes_to_transfer = samples * 2 * 2;

  pthread_mutex_lock(&buffer_mutex);
  size_t bytes_available = audio_size - audio_occupancy;
  if (bytes_available < bytes_to_transfer)
    bytes_to_transfer = bytes_available;
  if (bytes_to_transfer > 0) {
    size_t space_to_end_of_buffer = audio_umb - audio_eoq;
    if (space_to_end_of_buffer >= bytes_to_transfer) {
      memcpy(audio_eoq, buf, bytes_to_transfer);
      audio_eoq += bytes_to_transfer;
    } else {
      memcpy(audio_eoq, buf, space_to_end_of_buffer);
      buf += space_to_end_of_buffer;
      memcpy(audio_lmb, buf, bytes_to_transfer - space_to_end_of_buffer);
      audio_eoq = audio_lmb + bytes_to_transfer - space_to_end_of_buffer;
    }
    audio_occupancy += bytes_to_transfer;
  }

  if ((audio_occupancy >= 11025 * 2 * 2) && (pa_stream_is_corked(stream))) {
    // debug(1,"Uncorked");
    pthread_mutex_unlock(&buffer_mutex);
    pa_threaded_mainloop_lock(mainloop);
    pa_stream_cork(stream, 0, stream_success_cb, mainloop);
    pa_threaded_mainloop_unlock(mainloop);
  } else {
    pthread_mutex_unlock(&buffer_mutex);
  }
  return 0;
}

int pa_delay(long *the_delay) {
  check_pa_stream_status(stream, "audio_pa delay.");
  // debug(1,"pa_delay");
  long result = 0;
  int reply = 0;
  pa_usec_t latency;
  int negative;
  pa_threaded_mainloop_lock(mainloop);
  int gl = pa_stream_get_latency(stream, &latency, &negative);
  pa_threaded_mainloop_unlock(mainloop);
  if (gl == PA_ERR_NODATA) {
    // debug(1, "No latency data yet.");
    reply = -ENODEV;
  } else if (gl != 0) {
    // debug(1,"Error %d getting latency.",gl);
    reply = -EIO;
  } else {
    result = (audio_occupancy / (2 * 2)) + (latency * 44100) / 1000000;
    reply = 0;
  }
  *the_delay = result;
  return reply;
}

static void flush(void) {
  check_pa_stream_status(stream, "audio_pa flush.");
  pa_threaded_mainloop_lock(mainloop);
  if (pa_stream_is_corked(stream) == 0) {
    // debug(1,"Flush and cork for flush.");
    pa_stream_flush(stream, stream_success_cb, NULL);
    pa_stream_cork(stream, 1, stream_success_cb, mainloop);
  }
  pa_threaded_mainloop_unlock(mainloop);
  pthread_mutex_lock(&buffer_mutex);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;
  pthread_mutex_unlock(&buffer_mutex);
}

static void stop(void) {
  check_pa_stream_status(stream, "audio_pa stop.");
  // Cork the stream so it will stop playing
  pa_threaded_mainloop_lock(mainloop);
  if (pa_stream_is_corked(stream) == 0) {
    // debug(1,"Flush and cork for stop.");
    pa_stream_flush(stream, stream_success_cb, NULL);
    pa_stream_cork(stream, 1, stream_success_cb, mainloop);
  }
  pa_threaded_mainloop_unlock(mainloop);
  pthread_mutex_lock(&buffer_mutex);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;
  pthread_mutex_unlock(&buffer_mutex);
}

audio_output audio_pa = {.name = "pa",
                         .help = NULL,
                         .init = &init,
                         .deinit = &deinit,
                         .prepare = NULL,
                         .start = &start,
                         .stop = &stop,
                         .is_running = NULL,
                         .flush = &flush,
                         .delay = &pa_delay,
                         .stats = NULL,
                         .play = &play,
                         .volume = NULL,
                         .parameters = NULL,
                         .mute = NULL};

void context_state_cb(__attribute__((unused)) pa_context *context, void *mainloop) {
  // debug(1,"context_state_cb called.");
  pa_threaded_mainloop_signal(mainloop, 0);
}

void stream_state_cb(__attribute__((unused)) pa_stream *s, void *mainloop) {
  // debug(1,"stream_state_cb called.");
  pa_threaded_mainloop_signal(mainloop, 0);
}

void stream_write_cb(pa_stream *stream, size_t requested_bytes,
                     __attribute__((unused)) void *userdata) {
  check_pa_stream_status(stream, "audio_pa stream_write_cb.");
  int bytes_to_transfer = requested_bytes;
  int bytes_transferred = 0;
  uint8_t *buffer = NULL;
  int ret = 0;
  pthread_mutex_lock(&buffer_mutex);
  pthread_cleanup_push(mutex_unlock, (void *)&buffer_mutex);
  while ((bytes_to_transfer > 0) && (audio_occupancy > 0) && (ret == 0)) {
    if (pa_stream_is_suspended(stream))
      debug(1, "stream is suspended");
    size_t bytes_we_can_transfer = bytes_to_transfer;
    if (audio_occupancy < bytes_we_can_transfer) {
      // debug(1, "Underflow? We have %d bytes but we are asked for %d bytes", audio_occupancy,
      //       bytes_we_can_transfer);
      pa_stream_cork(stream, 1, stream_success_cb, mainloop);
      // debug(1, "Corked");
      bytes_we_can_transfer = audio_occupancy;
    }

    // bytes we can transfer will never be greater than the bytes available

    ret = pa_stream_begin_write(stream, (void **)&buffer, &bytes_we_can_transfer);
    if ((ret == 0) && (buffer != NULL)) {
      if (bytes_we_can_transfer <= (size_t)(audio_umb - audio_toq)) {
        // the bytes are all in a row in the audo buffer
        memcpy(buffer, audio_toq, bytes_we_can_transfer);
        audio_toq += bytes_we_can_transfer;
        ret = pa_stream_write(stream, buffer, bytes_we_can_transfer, NULL, 0LL, PA_SEEK_RELATIVE);
      } else {
        // the bytes are in two places in the audio buffer
        size_t first_portion_to_write = audio_umb - audio_toq;
        if (first_portion_to_write != 0)
          memcpy(buffer, audio_toq, first_portion_to_write);
        uint8_t *new_buffer = buffer + first_portion_to_write;
        memcpy(new_buffer, audio_lmb, bytes_we_can_transfer - first_portion_to_write);
        ret = pa_stream_write(stream, buffer, bytes_we_can_transfer, NULL, 0LL, PA_SEEK_RELATIVE);
        audio_toq = audio_lmb + bytes_we_can_transfer - first_portion_to_write;
      }
      bytes_transferred += bytes_we_can_transfer;
      audio_occupancy -= bytes_we_can_transfer;
      bytes_to_transfer -= bytes_we_can_transfer;
    }
  }
  pthread_cleanup_pop(1); // release the mutex
  if (ret != 0)
    debug(1, "error writing to pa buffer");
  // debug(1,"<<<Frames requested %d, written to pa: %d, corked status:
  // %d.",requested_bytes/4,bytes_transferred/4,pa_stream_is_corked(stream));
}

void stream_success_cb(__attribute__((unused)) pa_stream *stream,
                       __attribute__((unused)) int success,
                       __attribute__((unused)) void *userdata) {
  return;
}
