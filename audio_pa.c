// Based on
// http://stackoverflow.com/questions/29977651/how-can-the-pulseaudio-asynchronous-library-be-used-to-play-raw-pcm-data

#include "audio.h"
#include "common.h"
#include <assert.h>
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
#define buffer_allocation 44100 * 4 * 2 * 2

static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

static struct {
  char *server;
  char *sink;
  char *service_name;
} pulse_options = {.server = NULL, .sink = NULL, .service_name = NULL};

pa_threaded_mainloop *mainloop;
pa_mainloop_api *mainloop_api;
pa_context *context;
pa_stream *stream;
char *audio_lmb, *audio_umb, *audio_toq, *audio_eoq;
size_t audio_size = buffer_allocation;
size_t audio_occupancy;

void context_state_cb(pa_context *context, void *mainloop);
void stream_state_cb(pa_stream *s, void *mainloop);
void stream_success_cb(pa_stream *stream, int success, void *userdata);
void stream_write_cb(pa_stream *stream, size_t requested_bytes, void *userdata);


static int init(int argc, char **argv) {

  // default values
  config.audio_backend_buffer_desired_length = 0.35;
  config.audio_backend_latency_offset = 0;
  
  // allocate space for the audio buffer
  audio_lmb = malloc(audio_size);
  if (audio_lmb == NULL)
    die("Can't allocate %d bytes for pulseaudio buffer.", audio_size);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;
  
  // Get a mainloop and its context
  mainloop = pa_threaded_mainloop_new();
  assert(mainloop);
  mainloop_api = pa_threaded_mainloop_get_api(mainloop);
  context = pa_context_new(mainloop_api, "Shairport Sync");
  assert(context);

  // Set a callback so we can wait for the context to be ready
  pa_context_set_state_callback(context, &context_state_cb, mainloop);

  // Lock the mainloop so that it does not run and crash before the context is ready
  pa_threaded_mainloop_lock(mainloop);

  // Start the mainloop
  assert(pa_threaded_mainloop_start(mainloop) == 0);
  assert(pa_context_connect(context, NULL, PA_CONTEXT_NOAUTOSPAWN, NULL) == 0);

  // Wait for the context to be ready
  for (;;) {
    pa_context_state_t context_state = pa_context_get_state(context);
    assert(PA_CONTEXT_IS_GOOD(context_state));
    if (context_state == PA_CONTEXT_READY)
      break;
    pa_threaded_mainloop_wait(mainloop);
  }

  pa_threaded_mainloop_unlock(mainloop);

  return 0;
}

static void deinit(void) {
  // debug(1, "pa deinit start");
  pa_threaded_mainloop_stop(mainloop);
  pa_threaded_mainloop_free(mainloop);
  // debug(1, "pa deinit done");
}

static void start(int sample_rate, int sample_format) {

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
  buffer_attr.maxlength = (uint32_t) -1;
  buffer_attr.tlength = buffer_size_in_bytes;
  buffer_attr.prebuf = (uint32_t) 0;
  buffer_attr.minreq = (uint32_t)-1;

  // Settings copied as per the chromium browser source
  pa_stream_flags_t stream_flags;
  stream_flags = PA_STREAM_START_CORKED | PA_STREAM_INTERPOLATE_TIMING | PA_STREAM_NOT_MONOTONIC |
                 //        PA_STREAM_AUTO_TIMING_UPDATE;
                 PA_STREAM_AUTO_TIMING_UPDATE | PA_STREAM_ADJUST_LATENCY;
 
  // Connect stream to the default audio output sink
  assert(pa_stream_connect_playback(stream, NULL, &buffer_attr, stream_flags, NULL, NULL) == 0);

  // Wait for the stream to be ready
  for (;;) {
    pa_stream_state_t stream_state = pa_stream_get_state(stream);
    assert(PA_STREAM_IS_GOOD(stream_state));
    if (stream_state == PA_STREAM_READY)
      break;
    pa_threaded_mainloop_wait(mainloop);
  }

  pa_threaded_mainloop_unlock(mainloop);
}

static void play(short buf[], int samples) {
  // debug(1,"pa_play of %d samples.",samples);
  char *bbuf = (char *)buf;
  // copy the samples into the queue
  size_t bytes_to_transfer = samples * 2 * 2;
  size_t space_to_end_of_buffer = audio_umb - audio_eoq;
  if (space_to_end_of_buffer >= bytes_to_transfer) {
    memcpy(audio_eoq, bbuf, bytes_to_transfer);
    audio_occupancy += bytes_to_transfer;
    pthread_mutex_lock(&buffer_mutex);
    audio_eoq += bytes_to_transfer;
    pthread_mutex_unlock(&buffer_mutex);
  } else {
    memcpy(audio_eoq, bbuf, space_to_end_of_buffer);
    bbuf += space_to_end_of_buffer;
    memcpy(audio_lmb, bbuf, bytes_to_transfer - space_to_end_of_buffer);
    pthread_mutex_lock(&buffer_mutex);
    audio_occupancy += bytes_to_transfer;
    pthread_mutex_unlock(&buffer_mutex);
    audio_eoq = audio_lmb + bytes_to_transfer - space_to_end_of_buffer;
  }
  if ((audio_occupancy >= 11025 * 2 * 2) && (pa_stream_is_corked(stream))) {
    // debug(1,"Uncorked");
    pa_threaded_mainloop_lock(mainloop);
    pa_stream_cork(stream, 0, stream_success_cb, mainloop);
    pa_threaded_mainloop_unlock(mainloop);

  }
}

int pa_delay(long *the_delay) {
  long result = 0;
  int reply = -ENODEV;
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

void flush(void) {
  // Cork the stream so it will stop playing
  pa_threaded_mainloop_lock(mainloop);
  if (pa_stream_is_corked(stream)==0) {
    // debug(1,"Flush and cork for flush.");
    pa_stream_flush(stream, stream_success_cb, NULL);
    pa_stream_cork(stream, 1, stream_success_cb, mainloop);
  }
  pa_threaded_mainloop_unlock(mainloop);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;
}

static void stop(void) {
  // Cork the stream so it will stop playing
  pa_threaded_mainloop_lock(mainloop);
  if (pa_stream_is_corked(stream)==0) {
    // debug(1,"Flush and cork for stop.");
    pa_stream_flush(stream, stream_success_cb, NULL);
    pa_stream_cork(stream, 1, stream_success_cb, mainloop);
  }
  pa_threaded_mainloop_unlock(mainloop);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;

  // debug(1, "finish with stream");
  pa_stream_disconnect(stream);
}

static void help(void) {
  printf(" no settings.\n");
}

audio_output audio_pa = {.name = "pa",
                         .help = &help,
                         .init = &init,
                         .deinit = &deinit,
                         .start = &start,
                         .stop = &stop,
                         .flush = &flush,
                         .delay = &pa_delay,
                         .play = &play,
                         .volume = NULL,
                         .parameters = NULL,
                         .mute = NULL,
                         .change_output_device = NULL};

void context_state_cb(pa_context *context, void *mainloop) {
  pa_threaded_mainloop_signal(mainloop, 0);
}

void stream_state_cb(pa_stream *s, void *mainloop) { pa_threaded_mainloop_signal(mainloop, 0); }

void stream_write_cb(pa_stream *stream, size_t requested_bytes, void *userdata) {

/*
  // play with timing information
  const struct pa_timing_info *ti = pa_stream_get_timing_info(stream);
  if ((ti == NULL) || (ti->write_index_corrupt)) {
    debug(2, "Timing info invalid");
  } else {
    struct timeval time_now;

    pa_gettimeofday(&time_now);

    uint64_t time_now_fp = ((uint64_t)time_now.tv_sec << 32) +
                           ((uint64_t)time_now.tv_usec << 32) / 1000000; // types okay
    uint64_t time_of_ti_fp = ((uint64_t)(ti->timestamp.tv_sec) << 32) +
                             ((uint64_t)(ti->timestamp.tv_usec) << 32) / 1000000; // types okay

    if (time_now_fp >= time_of_ti_fp) {
      uint64_t estimate_age = ((time_now_fp - time_of_ti_fp) * 1000000) >> 32;
      uint64_t bytes_in_buffer = ti->write_index - ti->read_index;
      pa_usec_t microseconds_to_write_buffer = (bytes_in_buffer * 1000000) / (44100 * 2 * 2);
      pa_usec_t ea = (pa_usec_t)estimate_age;
      pa_usec_t pa_latency = ti->sink_usec + ti->transport_usec + microseconds_to_write_buffer;
      pa_usec_t estimated_latency = pa_latency - estimate_age;
      // debug(1,"Estimated latency is %d microseconds.",estimated_latency);

//    } else {
//      debug(1, "Time now is earlier than time of timing information");
    }
  }
*/
  int bytes_to_transfer = requested_bytes;
  int bytes_transferred = 0;
  uint8_t *buffer = NULL;

  while ((bytes_to_transfer > 0) && (audio_occupancy > 0)) {
    size_t bytes_we_can_transfer = bytes_to_transfer;
    if (audio_occupancy < bytes_we_can_transfer) {
      // debug(1, "Underflow? We have %d bytes but we are asked for %d bytes", audio_occupancy,
      //      bytes_we_can_transfer);
      pa_stream_cork(stream,1, stream_success_cb, mainloop);
      // debug(1, "Corked");
      bytes_we_can_transfer = audio_occupancy;
    }

    // bytes we can transfer will never be greater than the bytes available
    
    pa_stream_begin_write(stream, (void **)&buffer, &bytes_we_can_transfer);
    if (bytes_we_can_transfer <= (audio_umb - audio_toq)) {
      // the bytes are all in a row in the audo buffer
      memcpy(buffer, audio_toq, bytes_we_can_transfer);
      audio_toq += bytes_we_can_transfer;
      // lock
      pthread_mutex_lock(&buffer_mutex);
      audio_occupancy -= bytes_we_can_transfer;
      pthread_mutex_unlock(&buffer_mutex);
      // unlock
      pa_stream_write(stream, buffer, bytes_we_can_transfer, NULL, 0LL, PA_SEEK_RELATIVE);
      bytes_transferred += bytes_we_can_transfer;
    } else {
      // the bytes are in two places in the audio buffer
      size_t first_portion_to_write = audio_umb - audio_toq;
      if (first_portion_to_write != 0)
        memcpy(buffer, audio_toq, first_portion_to_write);
      char *new_buffer = buffer + first_portion_to_write;
      memcpy(new_buffer, audio_lmb, bytes_we_can_transfer - first_portion_to_write);
      pa_stream_write(stream, buffer, bytes_we_can_transfer, NULL, 0LL, PA_SEEK_RELATIVE);
      bytes_transferred += bytes_we_can_transfer;
      audio_toq = audio_lmb + bytes_we_can_transfer - first_portion_to_write;
      // lock
      pthread_mutex_lock(&buffer_mutex);
      audio_occupancy -= bytes_we_can_transfer;
      pthread_mutex_unlock(&buffer_mutex);
      // unlock
    }
    bytes_to_transfer -= bytes_we_can_transfer;
    // debug(1,"audio_toq is %llx",audio_toq);
  }

  // debug(1,"<<<Frames requested %d, written to pa: %d, corked status:
  // %d.",requested_bytes/4,bytes_transferred/4,pa_stream_is_corked(stream));
}

void alt_stream_write_cb(pa_stream *stream, size_t requested_bytes, void *userdata) {
  // debug(1, "***Bytes requested bytes %d.", requested_bytes);
  int bytes_remaining = requested_bytes;
  while (bytes_remaining > 0) {
    uint8_t *buffer = NULL;
    size_t bytes_to_fill = 44100;
    size_t i;

    if (bytes_to_fill > bytes_remaining)
      bytes_to_fill = bytes_remaining;

    pa_stream_begin_write(stream, (void **)&buffer, &bytes_to_fill);

    for (i = 0; i < bytes_to_fill; i += 2) {
      buffer[i] = (i % 100) * 40 / 100 + 44;
      buffer[i + 1] = (i % 100) * 40 / 100 + 44;
    }

    pa_stream_write(stream, buffer, bytes_to_fill, NULL, 0LL, PA_SEEK_RELATIVE);

    bytes_remaining -= bytes_to_fill;
  }
}

void stream_success_cb(pa_stream *stream, int success, void *userdata) { return; }

