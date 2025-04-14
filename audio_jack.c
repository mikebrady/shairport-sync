/*
 * jack output driver. This file is part of Shairport Sync.
 * Copyright (c) 2019--2025 Mike Brady <4265913+mikebrady@users.noreply.github.com>,
 *                          Jörn Nettingsmeier <nettings@luchtbeweging.nl>
 *
 * All rights reserved.
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "audio.h"
#include "common.h"
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include <jack/jack.h>
#include <jack/ringbuffer.h>

#ifdef CONFIG_SOXR
#include <soxr.h>
#endif

#define NPORTS 2

typedef jack_default_audio_sample_t sample_t;

#define jack_sample_size sizeof(sample_t)

// Two-channel, 32bit audio:
const int bytes_per_frame = NPORTS * jack_sample_size;

pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t client_mutex = PTHREAD_MUTEX_INITIALIZER;

// This also affects deinterlacing.
// So make it exactly the number of incoming audio channels!
jack_port_t *port[NPORTS];
const char *port_name[NPORTS] = {"out_L", "out_R"};

int sps_sample_rate;

jack_client_t *client;
jack_nframes_t jack_sample_rate;
jack_nframes_t jack_latency;

jack_ringbuffer_t *jackbuf;
int flush_please = 0;

jack_latency_range_t latest_latency_range[NPORTS];
int64_t time_of_latest_transfer;

#ifdef CONFIG_SOXR
typedef struct soxr_quality {
  int quality;
  const char *name;
} soxr_quality_t;

soxr_quality_t soxr_quality_table[] = {{SOXR_VHQ, "very high"}, {SOXR_HQ, "high"},
                                       {SOXR_MQ, "medium"},     {SOXR_LQ, "low"},
                                       {SOXR_QQ, "quick"},      {-1, NULL}};

static int parse_soxr_quality_name(const char *name) {
  for (soxr_quality_t *s = soxr_quality_table; s->name != NULL; ++s) {
    if (!strcmp(s->name, name)) {
      return s->quality;
    }
  }
  return -1;
}

soxr_t soxr = NULL;
soxr_quality_spec_t quality_spec;
soxr_io_spec_t io_spec;
#endif

static inline sample_t sample_conv(short sample) {
  // signed 16-bit int to float
  return ((sample < 0) ? (-1.0 * sample / SHRT_MIN) : (1.0 * sample / SHRT_MAX));
}

static void deinterleave(const char *interleaved_input_buffer, sample_t *jack_output_buffer[],
                         jack_nframes_t offset, jack_nframes_t nframes) {
  jack_nframes_t f;
  // We're dealing with 16bit audio here:
  sample_t *ifp = (sample_t *)interleaved_input_buffer;
  // Zero-copy, we're working directly on the target and destination buffers,
  // so deal with an offset for the second part of the input ringbuffer
  for (f = offset; f < (nframes + offset); f++) {
    for (int i = 0; i < NPORTS; i++) {
      jack_output_buffer[i][f] = *ifp++;
    }
  }
}

// This is the JACK process callback. We don't decide when it runs.
// It must be hard-realtime safe (i.e. fully deterministic, with constant CPU
// usage. No calls to anything that could ever block: no syscalls, no screen
// output, no file access, no mutexes...
// The JACK ringbuffer we use to get the data in here is explicitly lock-free.
static int process(jack_nframes_t nframes, __attribute__((unused)) void *arg) {
  sample_t *buffer[NPORTS];
  // Expect an array of two elements because of possible ringbuffer wrap-around:
  jack_ringbuffer_data_t v[2] = {0};
  jack_nframes_t i, thisbuf;
  int frames_written = 0;
  int frames_required = 0;

  for (i = 0; i < NPORTS; i++) {
    buffer[i] = (sample_t *)jack_port_get_buffer(port[i], nframes);
  }
  if (flush_please) {
    // We just move the read pointer ahead without doing anything with the data.
    jack_ringbuffer_read_advance(jackbuf, jack_ringbuffer_read_space(jackbuf));
    flush_please = 0;
    // Since we don't change nframes, the whole buffer will be zeroed later.
  } else {
    jack_ringbuffer_get_read_vector(jackbuf, v);
    for (i = 0; i < 2; i++) {
      thisbuf = v[i].len / bytes_per_frame;
      if (thisbuf > nframes) {
        frames_required = nframes;
      } else {
        frames_required = thisbuf;
      }
      deinterleave(v[i].buf, buffer, frames_written, frames_required);
      frames_written += frames_required;
      nframes -= frames_required;
    }
    jack_ringbuffer_read_advance(jackbuf, frames_written * bytes_per_frame);
  }
  // If there are any more frames to put into the buffer, fill them with
  // silence. This is a critical underflow situation. Let's at least keep the JACK
  // graph humming along while preventing the motorboat sound of a repeating buffer.
  while (nframes > 0) {
    for (i = 0; i < NPORTS; i++) {
      buffer[i][frames_written] = 0.0;
    }
    frames_written++;
    nframes--;
  }
  return 0; // Tell JACK that all is well.
}

// This is the JACK graph reorder callback. Now we know some JACK connections
// have changed, so we recompute the latency.
static int graph(__attribute__((unused)) void *arg) {
  int latency = 0;
  debug(2, "JACK graph reorder callback called.");
  for (int i = 0; i < NPORTS; i++) {
    jack_port_get_latency_range(port[i], JackPlaybackLatency, &latest_latency_range[i]);
    debug(2, "JACK latency for port %s\tmin: %d\t max: %d", port_name[i],
          latest_latency_range[i].min, latest_latency_range[i].max);
    latency += latest_latency_range[i].max;
  }
  latency /= NPORTS;
  jack_latency = latency;
  debug(1, "Average maximum JACK latency across all ports: %d", jack_latency);
  return 0;
}

// This the function JACK will call in case of an error in the library.
static void error(const char *desc) { warn("JACK error: \"%s\"", desc); }

// This is the function JACK will call in case of a non-critical event in the library.
static void info(const char *desc) { inform("JACK information: \"%s\"", desc); }

static int jack_init(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) {
  int i;
  int bufsz = -1;
  config.audio_backend_latency_offset = 0;
  config.audio_backend_buffer_desired_length = 0.500;
  // Below this, soxr interpolation will not occur -- it'll be basic interpolation
  // instead.
  config.audio_backend_buffer_interpolation_threshold_in_seconds = 0.25;

  // this ensures that the Shairport Sync system will provide only 44100/S16/2 to this backend.
  parse_audio_options(NULL, (1 << SPS_FORMAT_S16_LE), (1 << SPS_RATE_44100), (1 << 2));

#ifdef CONFIG_SOXR
  config.jack_soxr_resample_quality = -1; // don't resample by default
#endif

  // Now the options specific to the backend, from the "jack" stanza:
  if (config.cfg != NULL) {
    const char *str;
    if (config_lookup_non_empty_string(config.cfg, "jack.client_name", &str)) {
      config.jack_client_name = (char *)str;
    }
    if (config_lookup_non_empty_string(config.cfg, "jack.autoconnect_pattern", &str)) {
      config.jack_autoconnect_pattern = (char *)str;
    }
#ifdef CONFIG_SOXR
    if (config_lookup_non_empty_string(config.cfg, "jack.soxr_resample_quality", &str)) {
      debug(1, "SOXR quality %s", str);
      config.jack_soxr_resample_quality = parse_soxr_quality_name(str);
    }
#endif
    if (config_lookup_int(config.cfg, "jack.bufsz", &bufsz) && bufsz <= 0)
      die("jack: bufsz must be > 0");
  }
  if (config.jack_client_name == NULL)
    config.jack_client_name = strdup("shairport-sync");

  // by default a buffer that can hold up to 4 seconds of 48kHz samples
  if (bufsz <= 0)
    bufsz = 48000 * 4 * bytes_per_frame;

  jackbuf = jack_ringbuffer_create((size_t)bufsz);
  if (jackbuf == NULL)
    die("Can't allocate %d bytes for the JACK ringbuffer.", bufsz);
  // Lock the ringbuffer into memory so that it never gets paged out, which would
  // break realtime constraints.
  jack_ringbuffer_mlock(jackbuf);
  // This mutex should not be necessary, but removing it causes segfaults on
  // shutdown. Apparently, there are multiple threads in the main program trying
  // to do stuff. FIXME: Try to consolidate into one thread and get rid of this lock.
  pthread_mutex_lock(&client_mutex);
  jack_status_t status;
  client = jack_client_open(config.jack_client_name, JackNoStartServer, &status);
  if (!client) {
    die("Could not start JACK server. JackStatus is %x", status);
  }
  jack_sample_rate = jack_get_sample_rate(client);
#ifdef CONFIG_SOXR
  if (config.jack_soxr_resample_quality >= SOXR_QQ) {
    quality_spec = soxr_quality_spec(config.jack_soxr_resample_quality, 0);
    io_spec = soxr_io_spec(SOXR_INT16_I, SOXR_FLOAT32_I);
  } else
#endif
      if (jack_sample_rate != 44100) {
    die("The JACK server is running at the wrong sample rate (%d) for Shairport Sync."
        " Must be 44100 Hz.",
        jack_sample_rate);
  }
  jack_set_process_callback(client, &process, NULL);
  jack_set_graph_order_callback(client, &graph, NULL);
  jack_set_error_function(&error);
  jack_set_info_function(&info);
  for (i = 0; i < NPORTS; i++) {
    port[i] =
        jack_port_register(client, port_name[i], JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
  }
  if (jack_activate(client)) {
    die("Could not activate %s JACK client.", config.jack_client_name);
  } else {
    debug(2, "JACK client %s activated successfully.", config.jack_client_name);
  }
  if (config.jack_autoconnect_pattern != NULL) {
    inform("config.jack_autoconnect_pattern is %s. If you see the program die after this,"
           "you made a syntax error.",
           config.jack_autoconnect_pattern);
    // Sadly, this will throw a segfault if the user provides a syntactically incorrect regex.
    // I've reported it to the jack-devel mailing list, they're in a better place to fix it.
    const char **port_list = jack_get_ports(client, config.jack_autoconnect_pattern,
                                            JACK_DEFAULT_AUDIO_TYPE, JackPortIsInput);
    if (port_list != NULL) {
      for (i = 0; i < NPORTS; i++) {
        char *full_port_name[NPORTS];
        full_port_name[i] = malloc(sizeof(char) * jack_port_name_size());
        sprintf(full_port_name[i], "%s:%s", config.jack_client_name, port_name[i]);
        if (port_list[i] != NULL) {
          int err;
          debug(2, "Connecting %s to %s.", full_port_name[i], port_list[i]);
          err = jack_connect(client, full_port_name[i], port_list[i]);
          switch (err) {
          case EEXIST:
            inform("The requested connection from %s to %s already exists.", full_port_name[i],
                   port_list[i]);
            break;
          case 0:
            // success
            break;
          default:
            warn("JACK error no. %d occurred while trying to connect %s to %s.", err,
                 full_port_name[i], port_list[i]);
            break;
          }
        } else {
          inform("No matching port found in %s to connect %s to. You may not hear audio.",
                 config.jack_autoconnect_pattern, full_port_name[i]);
        }
        free(full_port_name[i]);
      }
      while (port_list[i++] != NULL) {
        inform(
            "Additional matching port %s found. Check that the connections are what you intended.",
            port_list[i - 1]);
      }
      jack_free(port_list);
    }
  }
  pthread_mutex_unlock(&client_mutex);

  return 0;
}

static void jack_deinit() {
  pthread_mutex_lock(&client_mutex);
  if (jack_deactivate(client))
    warn("Error deactivating jack client");
  if (jack_client_close(client))
    warn("Error closing jack client");
  pthread_mutex_unlock(&client_mutex);
  jack_ringbuffer_free(jackbuf);
#ifdef CONFIG_SOXR
  if (soxr) {
    soxr_delete(soxr);
    soxr = NULL;
  }
#endif
}

static void jack_start(int i_sample_rate, __attribute__((unused)) int i_sample_format) {
  // Nothing to do, JACK client has already been set up at jack_init().
  // Also, we have no say over the sample rate or sample format of JACK,
  // We convert the 16bit samples to float, and die if the sample rate is != 44k1 without soxr.
  sps_sample_rate = i_sample_rate;
#ifdef CONFIG_SOXR
  if (config.jack_soxr_resample_quality >= SOXR_QQ) {
    // we might improve a bit with soxr_clear if the sample_rate doesn't change
    if (soxr) {
      soxr_delete(soxr);
    }
    soxr_error_t e = NULL;
    soxr =
        soxr_create(sps_sample_rate, jack_sample_rate, NPORTS, &e, &io_spec, &quality_spec, NULL);
    if (!soxr) {
      die("Unable to create soxr resampler for JACK: %s", e);
    }
  }
#endif
}

static void jack_flush() {
  debug(2, "Only the consumer can safely flush a lock-free ringbuffer. Asking the"
           " process callback to do it...");
  flush_please = 1;
}

static int jack_delay(long *the_delay) {
  // Semantics change: we now look at the last transfer into the lock-free
  // ringbuffer, not into the jack buffers directly (because locking those would
  // violate real-time constraints). On average, that should lead to  just a
  // constant additional latency.
  // Without the mutex, we could get the time of what is the last transfer of data
  // to a jack buffer, but then a transfer could occur and we would get the buffer
  // occupancy after another transfer had occurred, so we could "lose" a full transfer
  // (e.g. 1024 frames @ 44,100 fps ~ 23.2 milliseconds)
  pthread_mutex_lock(&buffer_mutex);
  int64_t time_now = get_absolute_time_in_ns();
  int64_t delta = time_now - time_of_latest_transfer; // nanoseconds
  size_t audio_occupancy_now = jack_ringbuffer_read_space(jackbuf) / bytes_per_frame;
  debug(2, "audio_occupancy_now is %d.", audio_occupancy_now);
  pthread_mutex_unlock(&buffer_mutex);

  int64_t frames_processed_since_latest_latency_check = (delta * jack_sample_rate) / 1000000000;
  // debug(1,"delta: %" PRId64 " frames.",frames_processed_since_latest_latency_check);
  // jack_latency is set by the graph() callback, it's the average of the maximum
  // latencies of all our output ports. Adjust this constant baseline delay according
  // to the buffer fill level:
  int64_t the_delay_in_jack_frames =
      jack_latency + audio_occupancy_now - frames_processed_since_latest_latency_check;
  int64_t the_delay_in_sps_frames = (the_delay_in_jack_frames * sps_sample_rate) / jack_sample_rate;
  *the_delay = the_delay_in_sps_frames;
  // debug(2, "reporting a delay of %ld frames at Shairport Sync's rate of %d FPS.",*the_delay,
  // sps_sample_rate);
  return 0;
}

static int play(void *buf, int samples, __attribute__((unused)) int sample_type,
                __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {
  jack_ringbuffer_data_t v[2] = {0};
  size_t i, j, c;
  jack_nframes_t thisbuf;
  // It's ok to lock here since we're not in the realtime callback:
  pthread_mutex_lock(&buffer_mutex);
  jack_ringbuffer_get_write_vector(jackbuf, v);
  short *in = (short *)buf;
  sample_t *out;
  for (i = 0; i < 2; ++i) {
    thisbuf = v[i].len / (jack_sample_size * NPORTS); // #samples per channel
    out = (sample_t *)v[i].buf;
#ifdef CONFIG_SOXR
    if (soxr) {
      size_t i_done, o_done;
      soxr_error_t e;
      while (samples > 0 && thisbuf > 0) {
        e = soxr_process(soxr, (soxr_in_t)in, samples, &i_done, (soxr_out_t)out, thisbuf, &o_done);
        if (e)
          die("Error during soxr process: %s", e);

        in += i_done * NPORTS; // advance our input buffer
        samples -= i_done;
        thisbuf -= o_done;
        jack_ringbuffer_write_advance(jackbuf, o_done * jack_sample_size * NPORTS);
      }
    } else {
#endif
      j = 0;
      for (j = 0; j < thisbuf && samples > 0; ++j) {
        for (c = 0; c < NPORTS; ++c)
          out[j * NPORTS + c] = sample_conv(*in++);
        --samples;
      }
      jack_ringbuffer_write_advance(jackbuf, j * jack_sample_size * NPORTS);
#ifdef CONFIG_SOXR
    }
#endif
  }
  time_of_latest_transfer = get_absolute_time_in_ns();
  pthread_mutex_unlock(&buffer_mutex);
  if (samples) {
    warn("JACK ringbuffer overrun. Dropped %d samples.", samples);
  }
  return 0;
}

audio_output audio_jack = {.name = "jack",
                           .help = NULL,
                           .init = &jack_init,
                           .deinit = &jack_deinit,
                           .configure = NULL,
                           .start = &jack_start,
                           .stop = NULL,
                           .is_running = NULL,
                           .flush = &jack_flush,
                           .delay = &jack_delay,
                           .stats = NULL,
                           .play = &play,
                           .volume = NULL,
                           .parameters = NULL,
                           .mute = NULL};
