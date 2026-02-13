/*
 * Asynchronous PipeWire Backend. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2024--2025
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

// This uses ideas from the tone generator sample code at:
// https://github.com/PipeWire/pipewire/blob/master/src/examples/audio-src.c
// Thanks to Wim Taymans.

#include "audio.h"
#include "common.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>

static char channel_map_mono[] = "FC";
static char channel_map_stereo[] = "FL FR";
static char channel_map_2p1[] = "FL FR LFE";
static char channel_map_4p0[] = "FL FR FC BC";
static char channel_map_5p0[] = "FL FR FC BL BR";
static char channel_map_5p1[] = "FL FR FC LFE BL BR";
static char channel_map_6p1[] = "FL FR FC LFE BC SL SR";
static char channel_map_7p1[] = "FL FR FC LFE BL BR SL SR";

typedef struct {
  enum spa_audio_format spa_format;
  sps_format_t sps_format;
  unsigned int bytes_per_sample;
} spa_sps_t;

// these are the only formats that audio_pw will ever allow itself to be configured with
static spa_sps_t format_lookup[] = {{SPA_AUDIO_FORMAT_S16_LE, SPS_FORMAT_S16_LE, 2},
                                    {SPA_AUDIO_FORMAT_S16_BE, SPS_FORMAT_S16_BE, 2},
                                    {SPA_AUDIO_FORMAT_S32_LE, SPS_FORMAT_S32_LE, 4},
                                    {SPA_AUDIO_FORMAT_S32_BE, SPS_FORMAT_S32_BE, 4}};

#define BUFFER_SIZE_IN_SECONDS 1

static uint8_t buffer[1024];
static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

static int32_t current_encoded_output_format = 0;
static char *audio_lmb, *audio_umb, *audio_toq, *audio_eoq;
static size_t audio_size = 0;
static size_t audio_occupancy;
static int enable_fill;
static int stream_is_active;
static int on_process_is_running = 0;

struct data {
  struct pw_thread_loop *loop;
  struct pw_stream *stream;
  unsigned int rate;
  unsigned int bytes_per_sample;
  unsigned int channels;
};

// the pipewire global data structure
struct data data = {NULL, NULL, 0, 0, 0};

// use an SPS_FORMAT_... to find an entry in the format_lookup table or return NULL
static spa_sps_t *sps_format_lookup(sps_format_t to_find) {
  spa_sps_t *response = NULL;
  unsigned int i = 0;
  while ((response == NULL) && (i < sizeof(format_lookup) / sizeof(spa_sps_t))) {
    if (format_lookup[i].sps_format == to_find)
      response = &format_lookup[i];
    else
      i++;
  }
  return response;
}

static void on_process(void *userdata) {

  struct data *local_data = userdata;
  int n_frames = 0;

  pthread_mutex_lock(&buffer_mutex);

  // debug(1, "on_process called.");

  if (stream_is_active == 0)
    debug(1, "on_process called while stream inactive!");

  on_process_is_running = 1;
  if ((audio_occupancy > 0) || (enable_fill)) {

    // get a buffer to see how big it can be
    struct pw_buffer *b = pw_stream_dequeue_buffer(local_data->stream);
    if (b == NULL) {
      pw_log_warn("out of buffers: %m");
      die("PipeWire failure -- out of buffers!");
    }
    struct spa_buffer *buf = b->buffer;
    uint8_t *dest = buf->datas[0].data;
    if (dest != NULL) {
      int stride = local_data->bytes_per_sample * local_data->channels;

      // note: the requested field is the number of frames, not bytes, requested
      int max_possible_frames = SPA_MIN(b->requested, buf->datas[0].maxsize / stride);

      size_t bytes_we_can_transfer = max_possible_frames * stride;

      if (audio_occupancy > 0) {
        // if (enable_fill == 1)) {
        //   debug(1, "got audio -- disable_fill");
        // }
        enable_fill = 0;

        if (bytes_we_can_transfer > audio_occupancy)
          bytes_we_can_transfer = audio_occupancy;

        n_frames = bytes_we_can_transfer / stride;

        size_t bytes_to_end_of_buffer = (size_t)(audio_umb - audio_toq); // must be zero or positive
        if (bytes_we_can_transfer <= bytes_to_end_of_buffer) {
          // the bytes are all in a row in the audio buffer
          memcpy(dest, audio_toq, bytes_we_can_transfer);
          audio_toq += bytes_we_can_transfer;
        } else {
          // the bytes are in two places in the audio buffer
          size_t first_portion_to_write = audio_umb - audio_toq;
          if (first_portion_to_write != 0)
            memcpy(dest, audio_toq, first_portion_to_write);
          uint8_t *new_dest = dest + first_portion_to_write;
          memcpy(new_dest, audio_lmb, bytes_we_can_transfer - first_portion_to_write);
          audio_toq = audio_lmb + bytes_we_can_transfer - first_portion_to_write;
        }
        audio_occupancy -= bytes_we_can_transfer;

      } else {
        debug(3, "send silence");
        // this should really be dithered silence
        memset(dest, 0, bytes_we_can_transfer);
        n_frames = max_possible_frames;
      }

      buf->datas[0].chunk->offset = 0;
      buf->datas[0].chunk->stride = stride;
      buf->datas[0].chunk->size = n_frames * stride;
      pw_stream_queue_buffer(local_data->stream, b);

    } // (else the first data block does not contain a data pointer)
  }
  pthread_mutex_unlock(&buffer_mutex);
}

static const struct pw_stream_events stream_events = {PW_VERSION_STREAM_EVENTS,
                                                      .process = on_process};

static void deinit(void) {
  pw_thread_loop_stop(data.loop);
  if (data.stream != NULL)
    pw_stream_destroy(data.stream);
  pw_thread_loop_destroy(data.loop);
  pw_deinit();
  on_process_is_running = 0;
  if (audio_lmb != NULL)
    free(audio_lmb); // deallocate that buffer
}

static int init(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) {
  // set up default values first
  config.audio_backend_buffer_desired_length = 0.5;
  config.audio_backend_buffer_interpolation_threshold_in_seconds =
      0.02; // below this, soxr interpolation will not occur -- it'll be basic interpolation
            // instead.

  config.audio_backend_latency_offset = 0;

  // get settings from settings file, passing in defaults for format_set, rate_set and channel_set
  // Note, these options may be in the "general" stanza or the named stanza
#ifdef CONFIG_FFMPEG
  parse_audio_options("pipewire", SPS_FORMAT_SET, SPS_RATE_SET, SPS_CHANNEL_SET);
#else
  parse_audio_options("pipewire", SPS_FORMAT_NON_FFMPEG_SET, SPS_RATE_NON_FFMPEG_SET,
                      SPS_CHANNNEL_NON_FFMPEG_SET);
#endif

  // now any PipeWire-specific options
  if (config.cfg != NULL) {
    const char *str;

    // Get the optional Application Name, if provided.
    if (config_lookup_non_empty_string(config.cfg, "pipewire.application_name", &str)) {
      config.pw_application_name = (char *)str;
    }

    // Get the optional PipeWire node name, if provided.
    if (config_lookup_non_empty_string(config.cfg, "pipewire.node_name", &str)) {
      config.pw_node_name = (char *)str;
    }

    // Get the optional PipeWire sink target name, if provided.
    if (config_lookup_non_empty_string(config.cfg, "pipewire.sink_target", &str)) {
      config.pw_sink_target = (char *)str;
    }
  }

  // finished collecting settings

  audio_lmb = NULL;
  audio_size = 0;
  current_encoded_output_format = 0;
  enable_fill = 1;

  int largc = 0;
  pw_init(&largc, NULL);

  /* make a threaded loop. */
  data.loop = pw_thread_loop_new("shairport-sync", NULL);

  pw_thread_loop_lock(data.loop);

  char *appname = config.pw_application_name;
  if (appname == NULL)
    appname = "Shairport Sync";

  char *nodename = config.pw_node_name;
  if (nodename == NULL)
    nodename = "Shairport Sync";

  struct pw_properties *props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE, "Music",
      PW_KEY_APP_NAME, appname, PW_KEY_NODE_NAME, nodename, NULL);

  if (config.pw_sink_target != NULL) {
    debug(3, "setting sink target to \"%s\".", config.pw_sink_target);
    pw_properties_set(props, PW_KEY_TARGET_OBJECT, config.pw_sink_target);
  }

  data.stream = pw_stream_new_simple(pw_thread_loop_get_loop(data.loop), config.appName, props,
                                     &stream_events, &data);
  pw_thread_loop_start(data.loop);

  on_process_is_running = 0;

  pw_thread_loop_unlock(data.loop);
  return 0;
}

static int check_settings(sps_format_t sample_format, unsigned int sample_rate,
                          unsigned int channel_count) {
  // we know the formats with be big- or little-ended.
  // we will accept only S32_..., S16_...

  int response = EINVAL;

  if (sps_format_lookup(sample_format) != NULL)
    response = 0;

  debug(3, "pw: configuration: %u/%s/%u %s.", sample_rate,
        sps_format_description_string(sample_format), channel_count,
        response == 0 ? "is okay" : "can not be configured");
  return response;
}

static int check_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  return check_settings(format, rate, channels);
}

static int32_t get_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  return search_for_suitable_configuration(channels, rate, format, &check_configuration);
}

static int configure(int32_t requested_encoded_format, char **resulting_channel_map) {
  // debug(2, "pw: configure %s.", short_format_description(requested_encoded_format));
  int response = 0;
  char *channel_map = NULL;
  // if (1) {
  if (current_encoded_output_format != requested_encoded_format) {
    uint64_t start_time = get_absolute_time_in_ns();
    if (current_encoded_output_format == 0)
      debug(2, "pw: setting output configuration to %s.",
            short_format_description(requested_encoded_format));
    else
      // note -- can't use short_format_description twice in one call because it returns the same
      // string buffer each time
      debug(2, "pw: changing output configuration to %s.",
            short_format_description(requested_encoded_format));
    current_encoded_output_format = requested_encoded_format;
    spa_sps_t *format_info =
        sps_format_lookup(FORMAT_FROM_ENCODED_FORMAT(current_encoded_output_format));

    if (format_info == NULL)
      die("Can't find format information!");
    // enum spa_audio_format spa_format = format_info->spa_format;
    data.bytes_per_sample = format_info->bytes_per_sample;
    data.channels = CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format);
    data.rate = RATE_FROM_ENCODED_FORMAT(current_encoded_output_format);

    pw_thread_loop_lock(data.loop);
    enable_fill = 0;

    if (pw_stream_get_state(data.stream, NULL) != PW_STREAM_STATE_UNCONNECTED) {
      response = pw_stream_disconnect(data.stream);
      if (response != 0) {
        debug(1, "error %d disconnecting stream.", response);
      }
    }

    if (audio_lmb != NULL) {
      // debug(3, "deallocating existing audio_pw.c buffer.");
      free(audio_lmb);
    }

    audio_size = data.rate * BUFFER_SIZE_IN_SECONDS * data.bytes_per_sample * data.channels;
    // allocate space for the audio buffer
    audio_lmb = malloc(audio_size);
    if (audio_lmb == NULL)
      die("Can't allocate %zd bytes for PipeWire buffer.", audio_size);
    audio_toq = audio_eoq = audio_lmb;
    audio_umb = audio_lmb + audio_size;
    audio_occupancy = 0;

    // Make one parameter with the supported formats. The SPA_PARAM_EnumFormat
    // id means that this is a format enumeration (of 1 value).
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    const struct spa_pod *params[1];
    // create a stream with the default channel layout corresponding to
    // the number of channels
    switch (CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format)) {
    case 1:
      channel_map = channel_map_mono;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          // we are giving the position of 8 channels here, even if we need less than that.
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate, .position = {SPA_AUDIO_CHANNEL_FC}));
      break;
    case 2:
      channel_map = channel_map_stereo;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          // we are giving the position of 8 channels here, even if we need less than that.
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR}));
      break;
    case 3:
      channel_map = channel_map_2p1;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          // we are giving the position of 8 channels here, even if we need less than that.
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                                                SPA_AUDIO_CHANNEL_LFE}));
      break;
    case 4:
      channel_map = channel_map_4p0;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          // we are giving the position of 8 channels here, even if we need less than that.
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                                                SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_BC}));
      break;
    case 5:
      channel_map = channel_map_5p0;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          // we are giving the position of 8 channels here, even if we need less than that.
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                                                SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_RL,
                                                SPA_AUDIO_CHANNEL_RR}));
      break;
    case 6:
      channel_map = channel_map_5p1;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                                                SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
                                                SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR}));
      break;
    case 7:
      channel_map = channel_map_6p1;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                                                SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
                                                SPA_AUDIO_CHANNEL_BC, SPA_AUDIO_CHANNEL_SL,
                                                SPA_AUDIO_CHANNEL_SR}));
      break;
    case 8:
      channel_map = channel_map_7p1;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                                                SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
                                                SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR,
                                                SPA_AUDIO_CHANNEL_SL, SPA_AUDIO_CHANNEL_SR}));
      break;
    default:
      channel_map = NULL;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          // we are giving the position of 8 channels here, even if we need less than that.
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                                                SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
                                                SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR,
                                                SPA_AUDIO_CHANNEL_SL, SPA_AUDIO_CHANNEL_SR}));
      break;
    }

    // Now connect this stream. We ask that our process function is
    // called in a realtime thread.
    pw_stream_connect(data.stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                      PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                          PW_STREAM_FLAG_RT_PROCESS,
                      params, 1);
    stream_is_active = 0;
    enable_fill = 1;
    pw_thread_loop_unlock(data.loop);
    int64_t elapsed_time = get_absolute_time_in_ns() - start_time;
    debug(3, "pw: configuration took %0.3f mS.", elapsed_time * 0.000001);
  } else {
    debug(2, "pw: setting output configuration  -- configuration unchanged, so nothing done.");
  }
  if ((response == 0) && (resulting_channel_map != NULL)) {
    *resulting_channel_map = channel_map;
  }
  return response;
}

static int play(__attribute__((unused)) void *buf, int samples,
                __attribute__((unused)) int sample_type, __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {
  if (stream_is_active == 0) {
    pw_thread_loop_lock(data.loop);
    on_process_is_running = 0;
    pw_stream_set_active(data.stream, true);
    pw_thread_loop_unlock(data.loop);
    stream_is_active = 1;
    // debug(1, "set stream active");
  }
  // copy the samples into the queue
  // debug(3, "play %u samples; %u samples already in the buffer.", samples, audio_occupancy /
  // (data.bytes_per_sample * data.channels));
  size_t bytes_to_transfer = samples * data.channels * data.bytes_per_sample;
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
  pthread_mutex_unlock(&buffer_mutex);
  return 0;
}

static int delay(long *the_delay) {
  long result = 0;
  int reply = -ENODEV; // ENODATA is not defined in FreeBSD

  if (on_process_is_running == 0) {
    debug(3, "pw_processor not running");
  }

  if ((stream_is_active == 0) && (on_process_is_running != 0)) {
    debug(3, "stream not active but on_process_is_running is true.");
  }
  if (on_process_is_running != 0) {

    struct pw_time stream_time_info_1, stream_time_info_2;
    ssize_t audio_occupancy_now;

    // get stable pw_time info to ensure we get an audio occupancy figure
    // that relates to the pw_time we have.
    // we do this by getting a pw_time before and after getting the occupancy
    // and accepting the information if they are both the same

    int loop_count = 1;
    int non_matching;
    int stream_time_valid_if_zero;
    do {
      stream_time_valid_if_zero =
          pw_stream_get_time_n(data.stream, &stream_time_info_1, sizeof(struct pw_time));
      audio_occupancy_now = audio_occupancy;
      pw_stream_get_time_n(data.stream, &stream_time_info_2, sizeof(struct pw_time));

      non_matching = memcmp(&stream_time_info_1, &stream_time_info_2, sizeof(struct pw_time));
      if (non_matching != 0) {
        loop_count++;
      }
    } while (((non_matching != 0) || (stream_time_valid_if_zero != 0)) && (loop_count < 10));

    if (non_matching != 0) {
      debug(1, "can't get a stable pw_time!");
    }
    if (stream_time_valid_if_zero != 0) {
      debug(1, "can't get valid stream info");
    }
    if (stream_time_info_1.rate.denom == 0) {
      debug(2, "non valid stream_time_info_1");
    }

    if ((non_matching == 0) && (stream_time_valid_if_zero == 0) &&
        (stream_time_info_1.rate.denom != 0)) {
      int64_t interval_from_pw_time_to_now_ns =
          pw_stream_get_nsec(data.stream) - stream_time_info_1.now;

      uint64_t frames_possibly_played_since_measurement =
          ((interval_from_pw_time_to_now_ns * data.rate) + 500000000L) / 1000000000L;

      uint64_t net_delay_in_frames = stream_time_info_1.queued + stream_time_info_1.buffered;

      uint64_t fixed_delay_ns =
          (stream_time_info_1.delay * stream_time_info_1.rate.num * 1000000000L) /
          stream_time_info_1.rate.denom; // ns;
      uint64_t fixed_delay_in_frames = ((fixed_delay_ns * data.rate) + 500000000L) / 1000000000L;

      net_delay_in_frames = net_delay_in_frames + fixed_delay_in_frames +
                            audio_occupancy_now / (data.bytes_per_sample * data.channels) -
                            frames_possibly_played_since_measurement;

      result = net_delay_in_frames;
      reply = 0;
    }
  }

  *the_delay = result;
  return reply;
}

static void flush(void) {
  pthread_mutex_lock(&buffer_mutex);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;
  // if (enable_fill == 0) {
  //   debug(1, "flush enable_fill");
  // }
  enable_fill = 1;
  pthread_mutex_unlock(&buffer_mutex);
}

static void stop(void) {
  pthread_mutex_lock(&buffer_mutex);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;
  // if (enable_fill == 0) {
  //   debug(1, "stop enable_fill");
  // }
  pthread_mutex_unlock(&buffer_mutex);
  if (stream_is_active == 1) {
    pw_thread_loop_lock(data.loop);
    // pw_stream_flush(data.stream, true);
    pw_stream_set_active(data.stream, false);
    pw_thread_loop_unlock(data.loop);
    stream_is_active = 0;
    // debug(1, "set stream inactive");
  }
}

audio_output audio_pw = {.name = "pipewire",
                         .help = NULL,
                         .init = &init,
                         .deinit = &deinit,
                         .start = NULL,
                         .get_configuration = &get_configuration,
                         .configure = &configure,
                         .stop = &stop,
                         .is_running = NULL,
                         .flush = &flush,
                         .delay = &delay,
                         .stats = NULL,
                         .play = &play,
                         .volume = NULL,
                         .parameters = NULL,
                         .mute = NULL};
