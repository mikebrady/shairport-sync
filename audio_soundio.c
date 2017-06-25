#include "audio.h"
#include "common.h"
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <soundio/soundio.h>

int Fs;
long long starttime, samples_played;

struct SoundIoOutStream *outstream;
struct SoundIo *soundio;
struct SoundIoDevice *device;
struct SoundIoRingBuffer *ring_buffer = NULL;

static int min_int(int a, int b) { return (a < b) ? a : b; }

static void write_callback(struct SoundIoOutStream *outstream, int frame_count_min,
                           int frame_count_max) {
  struct SoundIoChannelArea *areas;
  int frame_count;
  int err;

  char *read_ptr = soundio_ring_buffer_read_ptr(ring_buffer);
  int fill_bytes = soundio_ring_buffer_fill_count(ring_buffer);
  int fill_count = fill_bytes / outstream->bytes_per_frame;

  debug(3, "[--->>] frame_count_min: %d , frame_count_max: %d , fill_bytes: %d , fill_count: %d , "
           "outstream->bytes_per_frame: %d",
        frame_count_min, frame_count_max, fill_bytes, fill_count, outstream->bytes_per_frame);

  if (frame_count_min > fill_count) {
    int frame_count = frame_count_min;
    if ((err = soundio_outstream_begin_write(outstream, &areas, &frame_count))) {
      debug(0, "[--->>] begin write error: %s", soundio_strerror(err));
    }
    for (int frame = 0; frame < frame_count; frame += 1) {
      for (int ch = 0; ch < outstream->layout.channel_count; ch += 1) {
        memset(areas[ch].ptr, 0, outstream->bytes_per_sample);
        areas[ch].ptr += areas[ch].step;
      }
    }
    if ((err = soundio_outstream_end_write(outstream)))
      debug(0, "[--->>] end write error: %s", soundio_strerror(err));
    return;
  }

  int read_count = min_int(frame_count_max, fill_count);
  int frames_left = read_count;

  while (frames_left > 0) {
    int frame_count = frames_left;

    if ((err = soundio_outstream_begin_write(outstream, &areas, &frame_count)))
      debug(0, "[--->>] begin write error: %s", soundio_strerror(err));

    if (frame_count <= 0)
      break;

    for (int frame = 0; frame < frame_count; frame += 1) {
      for (int ch = 0; ch < outstream->layout.channel_count; ch += 1) {
        memcpy(areas[ch].ptr, read_ptr, outstream->bytes_per_sample);
        areas[ch].ptr += areas[ch].step;
        read_ptr += outstream->bytes_per_sample;
      }
    }

    if ((err = soundio_outstream_end_write(outstream)))
      debug(0, "[--->>] end write error: %s", soundio_strerror(err));

    frames_left -= frame_count;
  }

  debug(3, "[--->>]  Wrote: %d", read_count * outstream->bytes_per_frame);
  soundio_ring_buffer_advance_read_ptr(ring_buffer, read_count * outstream->bytes_per_frame);
}

static void underflow_callback(struct SoundIoOutStream *outstream) {
  static int count = 0;
  debug(0, "underflow %d\n", ++count);
}

static int init(int argc, char **argv) {
  int err;
  config.audio_backend_buffer_desired_length = 2.0;

  soundio = soundio_create();
  if (!soundio) {
    debug(0, "out of memory\n");
    return 1;
  }
  if ((err = soundio_connect_backend(soundio, SoundIoBackendCoreAudio))) {
    debug(0, "error connecting: %s", soundio_strerror(err));
    return 1;
  }
  soundio_flush_events(soundio);

  int default_out_device_index = soundio_default_output_device_index(soundio);
  if (default_out_device_index < 0) {
    debug(0, "no output device found");
    return 1;
  }

  device = soundio_get_output_device(soundio, default_out_device_index);
  if (!device) {
    debug(0, "out of memory");
    return 1;
  }
  debug(0, "Output device: %s\n", device->name);
  return 0;
}

static void deinit(void) {
  soundio_ring_buffer_destroy(ring_buffer);
  soundio_device_unref(device);
  soundio_destroy(soundio);
  debug(0, "soundio audio deinit\n");
}

static void start(int sample_rate, int sample_format) {
  Fs = sample_rate;
  starttime = 0;
  samples_played = 0;
  int err;

  debug(1, "soundion rate: %d, format: %d", sample_rate, sample_format);

  // soundio_device_sort_channel_layouts(device);

  outstream = soundio_outstream_create(device);
  outstream->format = SoundIoFormatS16NE;
  outstream->sample_rate = sample_rate;
  outstream->layout.channel_count = 2;
  outstream->write_callback = write_callback;
  outstream->underflow_callback = underflow_callback;
  // outstream->software_latency = 0;

  if ((err = soundio_outstream_open(outstream))) {
    debug(0, "unable to open device: %s", soundio_strerror(err));
  }
  if (outstream->layout_error)
    debug(0, "unable to set channel layout: %s\n", soundio_strerror(outstream->layout_error));

  int capacity = outstream->sample_rate * outstream->bytes_per_frame;
  ring_buffer = soundio_ring_buffer_create(soundio, capacity);
  if (!ring_buffer)
    debug(0, "unable to create ring buffer: out of memory");
  char *buf = soundio_ring_buffer_write_ptr(ring_buffer);
  memset(buf, 0, capacity);
  soundio_ring_buffer_advance_write_ptr(ring_buffer, capacity);

  if ((err = soundio_outstream_start(outstream))) {
    debug(0, "unable to start outstream: %s", soundio_strerror(err));
  }

  debug(1, "libsoundio output started\n");
}

static void play(short buf[], int samples) {
  int err;
  int free_bytes = soundio_ring_buffer_free_count(ring_buffer);
  int written_bytes = 0;
  int write_bytes = 0;
  int left_bytes = samples * outstream->bytes_per_frame;
  char *write_ptr = soundio_ring_buffer_write_ptr(ring_buffer);

  debug(3, "[<<---] samples: %d , size: %d", samples, left_bytes);
  write_bytes = min_int(left_bytes, free_bytes);
  debug(3, "[<<---] left_bytes: %d, write_bytes: %d, free_bytes: %d\n", left_bytes, write_bytes,
        free_bytes);

  if (write_bytes) {
    memcpy(write_ptr, (char *)buf, write_bytes);
    written_bytes += write_bytes;
    soundio_ring_buffer_advance_write_ptr(ring_buffer, write_bytes);
    debug(3, "[<<---] Written to buffer : %d\n", written_bytes);
  }
}

static void parameters(audio_parameters *info) {
  info->minimum_volume_dB = -30.0;
  info->maximum_volume_dB = 0.0;
  debug(2, "Parameters\n");
  debug(2, "Current Volume dB: %f\n", info->current_volume_dB);
  debug(2, "Minimum Volume dB: %d\n", info->minimum_volume_dB);
  debug(2, "Maximum Volume dB: %d\n", info->maximum_volume_dB);
}

static void stop(void) {
  soundio_outstream_destroy(outstream);
  soundio_ring_buffer_clear(ring_buffer);
  debug(1, "libsoundio output stopped\n");
}

static void flush(void) {
  soundio_ring_buffer_clear(ring_buffer);
  debug(1, "libsoundio output flushed\n");
}

static void help(void) { printf(" There are no options for libsoundio.\n"); }

audio_output audio_soundio = {.name = "soundio",
                              .help = &help,
                              .init = &init,
                              .deinit = &deinit,
                              .start = &start,
                              .stop = &stop,
                              .flush = &flush,
                              .delay = NULL,
                              .play = &play,
                              .volume = NULL,
                              .parameters = &parameters,
                              .mute = NULL,
                              .change_output_device = NULL};
