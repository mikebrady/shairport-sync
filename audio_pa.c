/*
 * Asynchronous PulseAudio Backend. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2017
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

// updated implementation based on kodi pulseaudio sink
// https://github.com/xbmc/xbmc/blob/Matrix/xbmc/cores/AudioEngine/Sinks/AESinkPULSE.cpp

#include "audio.h"
#include "common.h"
#include <errno.h>
#include <pthread.h>
#include <pulse/pulseaudio.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

struct pa_struct {
  pa_threaded_mainloop *mainloop;
  pa_mainloop_api *mainloop_api;
  pa_context *context;
  pa_stream *stream;
  pa_sample_spec sample_spec;
  pa_buffer_attr buffer_attr;
  int requested_bytes;
  int locked;
} data;

static void context_state_cb(__attribute__((unused)) pa_context *context, void *userdata) {
  struct pa_struct* pulseaudio = (struct pa_struct*)userdata;

  pa_threaded_mainloop_signal(pulseaudio->mainloop, 0);
}

static void stream_state_cb(__attribute__((unused)) pa_stream *s, void *userdata) {
  struct pa_struct* pulseaudio = (struct pa_struct*)userdata;

  pa_threaded_mainloop_signal(pulseaudio->mainloop, 0);
}

static void stream_latency_cb(__attribute__((unused)) pa_stream *s, void *userdata) {
  struct pa_struct* pulseaudio = (struct pa_struct*)userdata;

  pa_threaded_mainloop_signal(pulseaudio->mainloop, 0);
}

static void stream_write_cb(__attribute__((unused)) pa_stream *s, size_t requested_bytes, void *userdata) {
  struct pa_struct* pulseaudio = (struct pa_struct*)userdata;

  pulseaudio->requested_bytes = requested_bytes;
  pa_threaded_mainloop_signal(pulseaudio->mainloop, 0);
}

static void stream_success_cb(__attribute__((unused)) pa_stream *s, __attribute__((unused)) int success, void *userdata) {
  struct pa_struct* pulseaudio = (struct pa_struct*)userdata;

  pa_threaded_mainloop_signal(pulseaudio->mainloop, 0);
}

static void wait_for_operation(pa_operation *op, pa_threaded_mainloop *mainloop, const char *log_entry)
{
  if (op == NULL)
    return;

  debug(1, "pa: running operation %s", log_entry);

  while (pa_operation_get_state(op) == PA_OPERATION_RUNNING) {
    debug(1, "operation waiting for signal");
    pa_threaded_mainloop_wait(mainloop);
  }

  if (pa_operation_get_state(op) != PA_OPERATION_DONE)
  {
    warn("pa: %s, operation failed", log_entry);
    return;
  }

  debug(1, "pa: operation successful %s", log_entry);

  pa_operation_unref(op);
}

static void pa_try_lock() {
  if (!data.locked) {
    pa_threaded_mainloop_lock(data.mainloop);
    data.locked = 1;
  }
}

static void pa_try_unlock() {
  if (data.locked) {
    pa_threaded_mainloop_unlock(data.mainloop);
    data.locked = 0;
  }
}

static int init(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) {

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

  // Get a mainloop and its context
  data.mainloop = pa_threaded_mainloop_new();
  if (data.mainloop == NULL)
    die("could not create a pa_threaded_mainloop.");
  data.mainloop_api = pa_threaded_mainloop_get_api(data.mainloop);
  if (config.pa_application_name)
    data.context = pa_context_new(data.mainloop_api, config.pa_application_name);
  else
    data.context = pa_context_new(data.mainloop_api, "Shairport Sync");
  if (data.context == NULL)
    die("could not create a new context for pulseaudio.");
  // Set a callback so we can wait for the context to be ready
  pa_context_set_state_callback(data.context, &context_state_cb, (void*)&data);

  // Lock the mainloop so that it does not run and crash before the context is ready
  pa_try_lock();

  // Start the mainloop
  if (pa_threaded_mainloop_start(data.mainloop) != 0)
    die("could not start the pulseaudio threaded mainloop");

  if (pa_context_connect(data.context, config.pa_server, 0, NULL) != 0)
    die("failed to connect to the pulseaudio context -- the error message is \"%s\".",
        pa_strerror(pa_context_errno(data.context)));

  // Wait for the context to be ready
  for (;;) {
    pa_context_state_t context_state = pa_context_get_state(data.context);
    if (!PA_CONTEXT_IS_GOOD(context_state))
      die("pa context is not good -- the error message \"%s\".",
          pa_strerror(pa_context_errno(data.context)));
    if (context_state == PA_CONTEXT_READY)
      break;
    pa_threaded_mainloop_wait(data.mainloop);
  }

  pa_try_unlock();

  return 0;
}

static void deinit(void) {
  pa_threaded_mainloop_stop(data.mainloop);

  if (data.stream != NULL) {
    pa_stream_disconnect(data.stream);
    pa_stream_unref(data.stream);
    data.stream = NULL;
  }

  if (data.context != NULL) {
    pa_context_disconnect(data.context);
    pa_context_unref(data.context);
    data.context = NULL;
  }

  pa_threaded_mainloop_free(data.mainloop);
  data.mainloop = NULL;
}

static int sps_format_to_pa_format(sps_format_t sps_format) {
  switch (sps_format) {
    case SPS_FORMAT_U8:
      return PA_SAMPLE_U8;
    case SPS_FORMAT_S16:
      return PA_SAMPLE_S16NE;
    case SPS_FORMAT_S16_LE:
      return PA_SAMPLE_S16LE;
    case SPS_FORMAT_S16_BE:
      return PA_SAMPLE_S16BE;
    case SPS_FORMAT_S24:
      return PA_SAMPLE_S24_32NE;
    case SPS_FORMAT_S24_LE:
      return PA_SAMPLE_S24_32LE;
    case SPS_FORMAT_S24_BE:
      return PA_SAMPLE_S24_32BE;
    case SPS_FORMAT_S24_3LE:
      return PA_SAMPLE_S24LE;
    case SPS_FORMAT_S24_3BE:
      return PA_SAMPLE_S24BE;
    case SPS_FORMAT_S32:
      return PA_SAMPLE_S32NE;
    case SPS_FORMAT_S32_LE:
      return PA_SAMPLE_S32LE;
    case SPS_FORMAT_S32_BE:
      return PA_SAMPLE_S32BE;

    case SPS_FORMAT_S8:
    case SPS_FORMAT_UNKNOWN:
    case SPS_FORMAT_AUTO:
    case SPS_FORMAT_INVALID:
    default:
      return PA_SAMPLE_S16NE;
  }
}

static void start(int sample_rate, int sample_format) {
  pa_try_lock();

  data.sample_spec.channels = 2;
  data.sample_spec.rate = sample_rate;
  data.sample_spec.format = sps_format_to_pa_format(sample_format);

  debug(1, "pa: sample rate: %d", data.sample_spec.rate);
  debug(1, "pa: format: %s", pa_sample_format_to_string(data.sample_spec.format));
  debug(1, "pa: format size: %d", pa_sample_size_of_format(data.sample_spec.format));

  pa_channel_map map;
  pa_channel_map_init_stereo(&map);

  if (data.stream != NULL) {
    debug(1, "pa: cleanup old stream");
    pa_stream_disconnect(data.stream);
    pa_stream_unref(data.stream);
    data.stream = NULL;
  }

  data.stream = pa_stream_new(data.context, "Playback", &data.sample_spec, &map);
  pa_stream_set_state_callback(data.stream, stream_state_cb, (void*)&data);
  pa_stream_set_write_callback(data.stream, stream_write_cb, (void*)&data);
  pa_stream_set_latency_update_callback(data.stream, stream_latency_cb, (void*)&data);

  pa_usec_t latency = 20000;

  data.buffer_attr.fragsize = pa_usec_to_bytes(latency, &data.sample_spec);
  data.buffer_attr.maxlength = pa_usec_to_bytes(latency, &data.sample_spec);
  data.buffer_attr.minreq = pa_usec_to_bytes(0, &data.sample_spec);
  data.buffer_attr.prebuf = (uint32_t)-1;
  data.buffer_attr.tlength = pa_usec_to_bytes(latency, &data.sample_spec);

  pa_stream_flags_t stream_flags;
  stream_flags = PA_STREAM_START_CORKED | PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_NOT_MONOTONIC |
                 PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_ADJUST_LATENCY;

  int connect_result;

  if (config.pa_sink) {
    // Connect stream to the sink specified in the config
    connect_result =
        pa_stream_connect_playback(data.stream, config.pa_sink, &data.buffer_attr, stream_flags, NULL, NULL);
  } else {
    // Connect stream to the default audio output sink
    connect_result =
        pa_stream_connect_playback(data.stream, NULL, &data.buffer_attr, stream_flags, NULL, NULL);
  }

  if (connect_result != 0)
    die("could not connect to the pulseaudio playback stream -- the error message is \"%s\".",
        pa_strerror(pa_context_errno(data.context)));

  // Wait for the stream to be ready
  for (;;) {
    pa_stream_state_t stream_state = pa_stream_get_state(data.stream);
    if (!PA_STREAM_IS_GOOD(stream_state))
      die("stream state is no longer good while waiting for stream to become ready -- the error "
          "message is \"%s\".",
          pa_strerror(pa_context_errno(data.context)));
    if (stream_state == PA_STREAM_READY)
      break;
    pa_threaded_mainloop_wait(data.mainloop);
  }

  pa_try_unlock();
}

static int play(void *buf, int samples) {
  size_t bytes_to_transfer = samples * data.sample_spec.channels * pa_sample_size_of_format(data.sample_spec.format);
  int ret;

  pa_try_lock();

  if (pa_stream_is_corked(data.stream) == 1) {
    wait_for_operation(pa_stream_cork(data.stream, 0, stream_success_cb, (void*)&data), data.mainloop, "uncork");
  }

  while (data.requested_bytes < 0) {
    pa_threaded_mainloop_wait(data.mainloop);
  }

  ret = pa_stream_write(data.stream, buf, bytes_to_transfer, NULL, 0, PA_SEEK_RELATIVE);
  data.requested_bytes -= bytes_to_transfer;

  pa_try_unlock();

  if (ret) {
    warn("pa: pa_stream_write failed: %d", ret);
    return ret;
  }

  return 0;
}

static void flush() {
  pa_try_lock();

  if (pa_stream_is_corked(data.stream) == 0) {
    debug(1,"pa: flush and cork for flush");
    wait_for_operation(pa_stream_flush(data.stream, stream_success_cb, (void*)&data), data.mainloop, "flush");
    wait_for_operation(pa_stream_cork(data.stream, 1, stream_success_cb, (void*)&data), data.mainloop, "cork");
  }

  pa_try_unlock();
}

static void stop() {
  pa_try_lock();

  debug(1,"pa: drain and cork for stop");
  wait_for_operation(pa_stream_drain(data.stream, stream_success_cb, (void*)&data), data.mainloop, "drain");
  wait_for_operation(pa_stream_cork(data.stream, 1, stream_success_cb, (void*)&data), data.mainloop, "cork");

  pa_try_unlock();
}

audio_output audio_pa = {
  .name = "pa",
  .help = NULL,
  .init = &init,
  .deinit = &deinit,
  .prepare = NULL,
  .start = &start,
  .stop = &stop,
  .is_running = NULL,
  .flush = &flush,
  .delay = NULL,
  .play = &play,
  .volume = NULL,
  .parameters = NULL,
  .mute = NULL
};
