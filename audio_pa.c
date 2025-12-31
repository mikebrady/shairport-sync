/*
 * PulseAudio Backend. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2017--2025
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

typedef struct {
  pa_sample_format_t pa_format;
  sps_format_t sps_format;
  unsigned int bytes_per_sample;
} pa_sps_t;

// these are the only formats that audio_pw will ever allow itself to be configured with
static pa_sps_t format_lookup[] = {{PA_SAMPLE_S16LE, SPS_FORMAT_S16_LE, 2},
                                   {PA_SAMPLE_S16BE, SPS_FORMAT_S16_BE, 2},
                                   {PA_SAMPLE_S32LE, SPS_FORMAT_S32_LE, 4},
                                   {PA_SAMPLE_S32BE, SPS_FORMAT_S32_BE, 4}};

#define CHANNEL_MAP_SIZE 1024
static char channel_map[CHANNEL_MAP_SIZE + 1];

static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

pa_threaded_mainloop *mainloop;
pa_mainloop_api *mainloop_api;
pa_context *context;
pa_stream *stream;

static int32_t current_encoded_output_format = 0;
const char *default_channel_layouts = NULL;
static char *audio_lmb, *audio_umb, *audio_toq, *audio_eoq;
static size_t audio_size, audio_occupancy;

// use an SPS_FORMAT_... to find an entry in the format_lookup table or return NULL
static pa_sps_t *sps_format_lookup(sps_format_t to_find) {
  pa_sps_t *response = NULL;
  unsigned int i = 0;
  while ((response == NULL) && (i < sizeof(format_lookup) / sizeof(pa_sps_t))) {
    if (format_lookup[i].sps_format == to_find)
      response = &format_lookup[i];
    else
      i++;
  }
  return response;
}

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

static int check_settings(sps_format_t sample_format, unsigned int sample_rate,
                          unsigned int channel_count) {

  // debug(1, "pa check_settings: configuration: %u/%s/%u.", sample_rate,
  //       sps_format_description_string(sample_format), channel_count);

  int response = EINVAL;

  pa_sps_t *format_info = sps_format_lookup(sample_format);
  if (format_info != NULL) {
    // here, try to create a stream of the given format.

    pa_threaded_mainloop_lock(mainloop);
    // Create a playback stream
    pa_sample_spec sample_specifications;
    sample_specifications.format = format_info->pa_format;
    sample_specifications.rate = sample_rate;
    sample_specifications.channels = channel_count;

    pa_channel_map map;
    pa_channel_map_init_auto(&map, sample_specifications.channels, PA_CHANNEL_MAP_DEFAULT);

    pa_stream *check_settings_stream =
        pa_stream_new(context, "Playback", &sample_specifications, &map);

    if (check_settings_stream != NULL) {
      response = 0; // success
      pa_stream_unref(check_settings_stream);
    }
    pa_threaded_mainloop_unlock(mainloop);

    // response = 0;
  }

  // debug(1, "pa check_settings: configuration: %u/%s/%u %s.", sample_rate,
  //       sps_format_description_string(sample_format), channel_count,
  //       response == 0 ? "is okay" : "can not be configured");
  return response;
}

static int check_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  return check_settings(format, rate, channels);
}

static int32_t get_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  uint64_t start_time = get_absolute_time_in_ns();
  int32_t response =
      search_for_suitable_configuration(channels, rate, format, &check_configuration);
  int64_t elapsed_time = get_absolute_time_in_ns() - start_time;
  debug(3, "pa: get_configuration took %0.3f mS.", elapsed_time * 0.000001);
  return response;
}

static int configure(int32_t requested_encoded_format, char **resulting_channel_map) {
  debug(3, "pa: configure %s.", short_format_description(requested_encoded_format));
  int response = 0;
  if (current_encoded_output_format != requested_encoded_format) {
    uint64_t start_time = get_absolute_time_in_ns();
    if (current_encoded_output_format == 0)
      debug(3, "pa: setting output configuration to %s.",
            short_format_description(requested_encoded_format));
    else
      // note -- can't use short_format_description twice in one call because it returns the same
      // string buffer each time
      debug(3, "pa: changing output configuration to %s.",
            short_format_description(requested_encoded_format));
    current_encoded_output_format = requested_encoded_format;
    pa_sps_t *format_info =
        sps_format_lookup(FORMAT_FROM_ENCODED_FORMAT(current_encoded_output_format));

    if (format_info == NULL)
      die("pa: can't find format information!");

    if (audio_lmb != NULL) {
      free(audio_lmb); // release previous buffer
    }

    if (stream != NULL) {
      // debug(1, "pa: stopping and releasing the current stream...");
      pa_threaded_mainloop_lock(mainloop);
      if (pa_stream_is_corked(stream) == 0) {
        // debug(1,"Flush and cork for flush.");
        pa_stream_flush(stream, stream_success_cb, NULL);
        pa_stream_cork(stream, 1, stream_success_cb, mainloop);
      }
      pa_stream_disconnect(stream);
      pa_stream_unref(stream);
      pa_threaded_mainloop_unlock(mainloop);
      stream = NULL;
    }

    audio_size = RATE_FROM_ENCODED_FORMAT(current_encoded_output_format) *
                 format_info->bytes_per_sample *
                 CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format) * 1; // one seconds
    audio_lmb = malloc(audio_size);
    if (audio_lmb == NULL)
      die("Can't allocate %zd bytes for pulseaudio buffer.", audio_size);
    audio_toq = audio_eoq = audio_lmb;
    audio_umb = audio_lmb + audio_size;
    audio_occupancy = 0;

    pa_threaded_mainloop_lock(mainloop);
    // Create a playback stream
    pa_sample_spec sample_specifications;
    sample_specifications.format = format_info->pa_format;
    sample_specifications.rate = RATE_FROM_ENCODED_FORMAT(current_encoded_output_format);
    sample_specifications.channels = CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format);

    uint32_t buffer_size_in_bytes =
        (uint32_t)CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format) *
        format_info->bytes_per_sample * RATE_FROM_ENCODED_FORMAT(current_encoded_output_format) /
        10;

    pa_channel_map map;

    pa_channel_map *pacm = NULL;

    // if we've not asked specifically for native formats, ask for the alsa format...
    if ((default_channel_layouts == NULL) || (strcasecmp(default_channel_layouts, "alsa") == 0)) {
      pacm = pa_channel_map_init_auto(&map, sample_specifications.channels, PA_CHANNEL_MAP_ALSA);
    }

    // ask for the native format...
    if (pacm == NULL) {
      pacm = pa_channel_map_init_auto(&map, sample_specifications.channels, PA_CHANNEL_MAP_DEFAULT);
    }

    // ask for some format...
    if (pacm == NULL) {
      pacm =
          pa_channel_map_init_extend(&map, sample_specifications.channels, PA_CHANNEL_MAP_DEFAULT);
    }

    if (resulting_channel_map != NULL) { // if needed...
      // PA_CHANNEL_MAP_ALSA gives default channel maps that correspond to the FFmpeg defaults.
      // make up a channel map
      channel_map[0] = '\0';
      int c;
      for (c = 0; c < map.channels; c++) {
        switch (map.map[c]) {

        case PA_CHANNEL_POSITION_MONO:
          strncat(channel_map, "FC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_FRONT_LEFT:
          strncat(channel_map, "FL", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_FRONT_RIGHT:
          strncat(channel_map, "FR", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_FRONT_CENTER:
          strncat(channel_map, "FC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_REAR_CENTER:
          strncat(channel_map, "BC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_REAR_LEFT:
          strncat(channel_map, "BL", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_REAR_RIGHT:
          strncat(channel_map, "BR", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_LFE:
          strncat(channel_map, "LFE", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_FRONT_LEFT_OF_CENTER:
          strncat(channel_map, "FLC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_FRONT_RIGHT_OF_CENTER:
          strncat(channel_map, "FRC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_SIDE_LEFT:
          strncat(channel_map, "SL", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_SIDE_RIGHT:
          strncat(channel_map, "SR", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX0:
          strncat(channel_map, "AUX0", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX1:
          strncat(channel_map, "AUX1", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX2:
          strncat(channel_map, "AUX2", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX3:
          strncat(channel_map, "AUX3", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX4:
          strncat(channel_map, "AUX4", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX5:
          strncat(channel_map, "AUX5", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX6:
          strncat(channel_map, "AUX6", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX7:
          strncat(channel_map, "AUX7", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX8:
          strncat(channel_map, "AUX8", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX9:
          strncat(channel_map, "AUX9", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX10:
          strncat(channel_map, "AUX10", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX11:
          strncat(channel_map, "AUX11", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX12:
          strncat(channel_map, "AUX12", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX13:
          strncat(channel_map, "AUX13", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX14:
          strncat(channel_map, "AUX14", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX15:
          strncat(channel_map, "AUX15", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX16:
          strncat(channel_map, "AUX16", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX17:
          strncat(channel_map, "AUX17", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX18:
          strncat(channel_map, "AUX18", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX19:
          strncat(channel_map, "AUX19", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX20:
          strncat(channel_map, "AUX20", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX21:
          strncat(channel_map, "AUX21", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX22:
          strncat(channel_map, "AUX22", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX23:
          strncat(channel_map, "AUX23", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX24:
          strncat(channel_map, "AUX24", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX25:
          strncat(channel_map, "AUX25", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX26:
          strncat(channel_map, "AUX26", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX27:
          strncat(channel_map, "AUX27", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX28:
          strncat(channel_map, "AUX28", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX29:
          strncat(channel_map, "AUX29", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX30:
          strncat(channel_map, "AUX30", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_AUX31:
          strncat(channel_map, "AUX31", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_TOP_CENTER:
          strncat(channel_map, "TC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_TOP_FRONT_LEFT:
          strncat(channel_map, "TFL", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_TOP_FRONT_RIGHT:
          strncat(channel_map, "TFR", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_TOP_FRONT_CENTER:
          strncat(channel_map, "TFC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_TOP_REAR_LEFT:
          strncat(channel_map, "TBL", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_TOP_REAR_RIGHT:
          strncat(channel_map, "TBR", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        case PA_CHANNEL_POSITION_TOP_REAR_CENTER:
          strncat(channel_map, "TBC", sizeof(channel_map) - 1 - strlen(channel_map));
          break;
        default:
          break;
        }
        if (c != (map.channels - 1))
          strncat(channel_map, " ", sizeof(channel_map) - 1 - strlen(channel_map));
      }

      debug(1, "audio_pa: channel map for %d channels is \"%s\".", sample_specifications.channels,
            channel_map);
    }

    stream = pa_stream_new(context, "Playback", &sample_specifications, &map);
    pa_stream_set_state_callback(stream, stream_state_cb, mainloop);
    pa_stream_set_write_callback(stream, stream_write_cb, mainloop);

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
      connect_result = pa_stream_connect_playback(stream, config.pa_sink, &buffer_attr,
                                                  stream_flags, NULL, NULL);
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

    // to here

    int64_t elapsed_time = get_absolute_time_in_ns() - start_time;
    debug(3, "pa: configuration took %0.3f mS.", elapsed_time * 0.000001);
  } else {
    debug(3, "pa: setting output configuration  -- configuration unchanged, so nothing done.");
  }
  if ((response == 0) && (resulting_channel_map != NULL)) {
    *resulting_channel_map = channel_map;
  }
  return response;
}

static int init(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) {
  // debug(1, "pa_init");
  // set up default values first
  config.audio_backend_buffer_desired_length = 0.35;
  config.audio_backend_buffer_interpolation_threshold_in_seconds =
      0.02; // below this, soxr interpolation will not occur -- it'll be basic interpolation
            // instead.

  config.audio_backend_latency_offset = 0;

  // get settings from settings file, passing in defaults for format_set, rate_set and channel_set
  // Note, these options may be in the "general" stanza or the named stanza
#ifdef CONFIG_FFMPEG
  parse_audio_options("pulseaudio", SPS_FORMAT_SET, SPS_RATE_SET, SPS_CHANNEL_SET);
#else
  parse_audio_options("pulseaudio", SPS_FORMAT_NON_FFMPEG_SET, SPS_RATE_NON_FFMPEG_SET,
                      SPS_CHANNNEL_NON_FFMPEG_SET);
#endif

  // now the specific options
  if (config.cfg != NULL) {
    const char *str;

    /* Get the PulseAudio server name. */
    if (config_lookup_non_empty_string(config.cfg, "pulseaudio.server", &str)) {
      config.pa_server = (char *)str;
    }

    // get the default channel mapping setting basis -- native ("no") or pa ("yes").

    if (config_lookup_non_empty_string(config.cfg, "pulseaudio", &default_channel_layouts)) {
      if ((strcasecmp(default_channel_layouts, "alsa") == 0) ||
          (strcasecmp(default_channel_layouts, "pulseaudio") == 0)) {
        debug(1, "pulseaudio default_channel_layouts setting: \"%s\".", default_channel_layouts);
      } else {
        debug(1, "Invalid pulseaudio default_channel_layouts setting. Must be \"alsa\" or "
                 "\"pulseaudio\".");
        default_channel_layouts = NULL;
      }
    };

    /* Get the Application Name. */
    if (config_lookup_non_empty_string(config.cfg, "pulseaudio.application_name", &str)) {
      config.pa_application_name = (char *)str;
    }

    /* Get the PulseAudio sink name. */
    if (config_lookup_non_empty_string(config.cfg, "pulseaudio.sink", &str)) {
      config.pa_sink = (char *)str;
    }
  }

  // finish collecting settings

  stream = NULL;    // no stream
  audio_lmb = NULL; // no buffer

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
  return 0;
}

static void deinit(void) {
  // debug(1, "pa deinit");
  if (stream != NULL) {
    check_pa_stream_status(stream, "audio_pa deinitialisation.");
    pa_stream_disconnect(stream);
    pa_threaded_mainloop_stop(mainloop);
    pa_threaded_mainloop_free(mainloop);
    debug(1, "pa deinit done");
  }
}

/*
static void start(__attribute__((unused)) int sample_rate,
                  __attribute__((unused)) int sample_format) {
  check_pa_stream_status(stream, "audio_pa start.");
}
*/

static int play(void *buf, int samples, __attribute__((unused)) int sample_type,
                __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {
  // debug(1,"pa_play of %d samples.",samples);
  // copy the samples into the queue
  check_pa_stream_status(stream, "audio_pa play.");

  pa_sps_t *format_info =
      sps_format_lookup(FORMAT_FROM_ENCODED_FORMAT(current_encoded_output_format));
  size_t bytes_to_transfer = samples * format_info->bytes_per_sample *
                             CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format);

  pthread_mutex_lock(&buffer_mutex);
  size_t bytes_available = audio_size - audio_occupancy;
  if (bytes_available < bytes_to_transfer)
    bytes_to_transfer = bytes_available;
  if ((bytes_to_transfer > 0) && (audio_lmb != NULL)) {
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

  if ((audio_occupancy >=
       (RATE_FROM_ENCODED_FORMAT(current_encoded_output_format) * format_info->bytes_per_sample *
        CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format)) /
           4) &&
      (pa_stream_is_corked(stream))) {
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
  // debug(1, "pa delay");
  check_pa_stream_status(stream, "audio_pa delay.");
  // debug(1,"pa_delay");
  long result = 0;
  int reply = 0;
  pa_usec_t latency;
  int negative;
  pa_threaded_mainloop_lock(mainloop);
  int gl = pa_stream_get_latency(stream, &latency, &negative);
  pa_threaded_mainloop_unlock(mainloop);
  if (gl == -PA_ERR_NODATA) {
    reply = -ENODEV;
  } else if (gl != 0) {
    reply = -EIO;
  } else {
    pa_sps_t *format_info =
        sps_format_lookup(FORMAT_FROM_ENCODED_FORMAT(current_encoded_output_format));
    // convert audio_occupancy bytes to frames and latency microseconds into frames
    result = (audio_occupancy / (format_info->bytes_per_sample *
                                 CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format))) +
             (latency * RATE_FROM_ENCODED_FORMAT(current_encoded_output_format)) / 1000000;
    reply = 0;
  }
  *the_delay = result;
  return reply;
}

static void flush(void) {
  // debug(1, "pa flush");
  if (stream != NULL) {
    check_pa_stream_status(stream, "audio_pa flush.");
    pa_threaded_mainloop_lock(mainloop);
    if (pa_stream_is_corked(stream) == 0) {
      // debug(1,"Flush and cork for flush.");
      pa_stream_flush(stream, stream_success_cb, NULL);
      pa_stream_cork(stream, 1, stream_success_cb, mainloop);
    }
    pa_threaded_mainloop_unlock(mainloop);
  }
  pthread_mutex_lock(&buffer_mutex);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;
  pthread_mutex_unlock(&buffer_mutex);
}

static void stop(void) {
  // debug(1, "pa stop");
  if (stream != NULL) {
    check_pa_stream_status(stream, "audio_pa stop.");
    // Cork the stream so it will stop playing
    pa_threaded_mainloop_lock(mainloop);
    if (pa_stream_is_corked(stream) == 0) {
      // debug(1,"Flush and cork for stop.");
      pa_stream_flush(stream, stream_success_cb, NULL);
      pa_stream_cork(stream, 1, stream_success_cb, mainloop);
    }
    pa_threaded_mainloop_unlock(mainloop);
  }
  pthread_mutex_lock(&buffer_mutex);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;
  pthread_mutex_unlock(&buffer_mutex);
}

void context_state_cb(__attribute__((unused)) pa_context *local_context, void *local_mainloop) {
  // debug(1,"context_state_cb called.");
  pa_threaded_mainloop_signal(local_mainloop, 0);
}

void stream_state_cb(__attribute__((unused)) pa_stream *s, void *local_mainloop) {
  // debug(1,"stream_state_cb called.");
  pa_threaded_mainloop_signal(local_mainloop, 0);
}

void stream_write_cb(pa_stream *local_stream, size_t requested_bytes,
                     __attribute__((unused)) void *userdata) {
  // debug(1, "pa stream_write_cb");
  check_pa_stream_status(local_stream, "audio_pa stream_write_cb.");
  int bytes_to_transfer = requested_bytes;
  int bytes_transferred = 0;
  uint8_t *buffer = NULL;
  int ret = 0;
  pthread_mutex_lock(&buffer_mutex);
  pthread_cleanup_push(mutex_unlock, (void *)&buffer_mutex);
  while ((bytes_to_transfer > 0) && (audio_occupancy > 0) && (ret == 0)) {
    if (pa_stream_is_suspended(local_stream))
      debug(1, "local_stream is suspended");
    size_t bytes_we_can_transfer = bytes_to_transfer;
    if (audio_occupancy == 0) {
      pa_stream_cork(local_stream, 1, stream_success_cb, mainloop);
      debug(1, "stream_write_cb: corked");
    }
    if (audio_occupancy < bytes_we_can_transfer) {
      // debug(1, "Underflow? We have %d bytes but we are asked for %d bytes", audio_occupancy,
      //       bytes_we_can_transfer);
      bytes_we_can_transfer = audio_occupancy;
    }

    // bytes we can transfer will never be greater than the bytes available

    ret = pa_stream_begin_write(local_stream, (void **)&buffer, &bytes_we_can_transfer);
    if ((ret == 0) && (buffer != NULL) && (audio_lmb != NULL)) {
      if (bytes_we_can_transfer <= (size_t)(audio_umb - audio_toq)) {
        // the bytes are all in a row in the audo buffer
        memcpy(buffer, audio_toq, bytes_we_can_transfer);
        audio_toq += bytes_we_can_transfer;
        ret = pa_stream_write(local_stream, buffer, bytes_we_can_transfer, NULL, 0LL,
                              PA_SEEK_RELATIVE);
      } else {
        // the bytes are in two places in the audio buffer
        size_t first_portion_to_write = audio_umb - audio_toq;
        if (first_portion_to_write != 0)
          memcpy(buffer, audio_toq, first_portion_to_write);
        uint8_t *new_buffer = buffer + first_portion_to_write;
        memcpy(new_buffer, audio_lmb, bytes_we_can_transfer - first_portion_to_write);
        ret = pa_stream_write(local_stream, buffer, bytes_we_can_transfer, NULL, 0LL,
                              PA_SEEK_RELATIVE);
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
  // %d.",requested_bytes/4,bytes_transferred/4,pa_stream_is_corked(local_stream));
}

void stream_success_cb(__attribute__((unused)) pa_stream *local_stream,
                       __attribute__((unused)) int success,
                       __attribute__((unused)) void *userdata) {
  return;
}

audio_output audio_pa = {.name = "pulseaudio",
                         .help = NULL,
                         .init = &init,
                         .deinit = &deinit,
                         .start = NULL,
                         .configure = &configure,
                         .get_configuration = &get_configuration,
                         .stop = &stop,
                         .is_running = NULL,
                         .flush = &flush,
                         .delay = &pa_delay,
                         .stats = NULL,
                         .play = &play,
                         .volume = NULL,
                         .parameters = NULL,
                         .mute = NULL};
