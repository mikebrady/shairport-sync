/*
 * CoreAudio output driver (macOS). This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2014--2025
 * Modifications and additions (c) 2026
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

#ifdef COMPILE_FOR_OSX

#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
  uint8_t *data;
  size_t capacity_bytes;
  size_t read_pos;
  size_t write_pos;
  size_t used_bytes;
  pthread_mutex_t mutex;
  pthread_cond_t can_read;
  pthread_cond_t can_write;
} byte_fifo_t;

static void fifo_init(byte_fifo_t *f, size_t capacity_bytes) {
  memset(f, 0, sizeof(*f));
  f->data = malloc(capacity_bytes);
  if (!f->data)
    die("coreaudio: out of memory allocating %zu bytes", capacity_bytes);
  f->capacity_bytes = capacity_bytes;
  pthread_mutex_init(&f->mutex, NULL);
  pthread_cond_init(&f->can_read, NULL);
  pthread_cond_init(&f->can_write, NULL);
}

static void fifo_deinit(byte_fifo_t *f) {
  if (!f || !f->data)
    return;
  pthread_mutex_lock(&f->mutex);
  pthread_cond_broadcast(&f->can_read);
  pthread_cond_broadcast(&f->can_write);
  pthread_mutex_unlock(&f->mutex);
  free(f->data);
  f->data = NULL;
  pthread_cond_destroy(&f->can_read);
  pthread_cond_destroy(&f->can_write);
  pthread_mutex_destroy(&f->mutex);
  memset(f, 0, sizeof(*f));
}

static void fifo_clear(byte_fifo_t *f) {
  pthread_mutex_lock(&f->mutex);
  f->read_pos = 0;
  f->write_pos = 0;
  f->used_bytes = 0;
  pthread_cond_broadcast(&f->can_write);
  pthread_mutex_unlock(&f->mutex);
}

static size_t fifo_write(byte_fifo_t *f, const uint8_t *src, size_t n, int block) {
  size_t written = 0;
  pthread_mutex_lock(&f->mutex);
  while (written < n) {
    size_t free_bytes = f->capacity_bytes - f->used_bytes;
    if (free_bytes == 0) {
      if (!block)
        break;
      pthread_cond_wait(&f->can_write, &f->mutex);
      continue;
    }
    size_t chunk = n - written;
    if (chunk > free_bytes)
      chunk = free_bytes;
    size_t to_end = f->capacity_bytes - f->write_pos;
    size_t first = chunk < to_end ? chunk : to_end;
    memcpy(f->data + f->write_pos, src + written, first);
    f->write_pos = (f->write_pos + first) % f->capacity_bytes;
    f->used_bytes += first;
    written += first;
    if (first < chunk) {
      size_t second = chunk - first;
      memcpy(f->data + f->write_pos, src + written, second);
      f->write_pos = (f->write_pos + second) % f->capacity_bytes;
      f->used_bytes += second;
      written += second;
    }
    pthread_cond_signal(&f->can_read);
  }
  pthread_mutex_unlock(&f->mutex);
  return written;
}

static size_t fifo_read(byte_fifo_t *f, uint8_t *dst, size_t n) {
  size_t read = 0;
  pthread_mutex_lock(&f->mutex);
  while (read < n && f->used_bytes > 0) {
    size_t chunk = n - read;
    if (chunk > f->used_bytes)
      chunk = f->used_bytes;
    size_t to_end = f->capacity_bytes - f->read_pos;
    size_t first = chunk < to_end ? chunk : to_end;
    memcpy(dst + read, f->data + f->read_pos, first);
    f->read_pos = (f->read_pos + first) % f->capacity_bytes;
    f->used_bytes -= first;
    read += first;
    if (first < chunk) {
      size_t second = chunk - first;
      memcpy(dst + read, f->data + f->read_pos, second);
      f->read_pos = (f->read_pos + second) % f->capacity_bytes;
      f->used_bytes -= second;
      read += second;
    }
    pthread_cond_signal(&f->can_write);
  }
  pthread_mutex_unlock(&f->mutex);
  return read;
}

static size_t fifo_used_bytes(byte_fifo_t *f) {
  size_t used = 0;
  pthread_mutex_lock(&f->mutex);
  used = f->used_bytes;
  pthread_mutex_unlock(&f->mutex);
  return used;
}

static AudioUnit output_unit = NULL;
static byte_fifo_t fifo;
static atomic_int unit_running = 0;

static int32_t current_encoded_output_format = 0;
static unsigned int current_sample_rate = 0;
static unsigned int current_channel_count = 0;
static unsigned int current_bytes_per_sample = 0;

static OSStatus coreaudio_render(void *inRefCon, AudioUnitRenderActionFlags *ioActionFlags,
                                 const AudioTimeStamp *inTimeStamp, UInt32 inBusNumber,
                                 UInt32 inNumberFrames, AudioBufferList *ioData) {
  (void)inRefCon;
  (void)ioActionFlags;
  (void)inTimeStamp;
  (void)inBusNumber;

  // We configure for interleaved PCM, so expect one buffer.
  if (!ioData || ioData->mNumberBuffers == 0)
    return noErr;

  AudioBuffer *b = &ioData->mBuffers[0];
  size_t wanted = (size_t)inNumberFrames * current_channel_count * current_bytes_per_sample;
  if (b->mDataByteSize < wanted) {
    wanted = b->mDataByteSize;
  } else {
    b->mDataByteSize = (UInt32)wanted;
  }

  size_t got = 0;
  if (b->mData && wanted > 0) {
    got = fifo_read(&fifo, (uint8_t *)b->mData, wanted);
    if (got < wanted) {
      memset(((uint8_t *)b->mData) + got, 0, wanted - got);
    }
  }
  return noErr;
}

static int check_settings(sps_format_t sample_format, unsigned int sample_rate,
                          unsigned int channel_count) {
  // Conservative: accept only little-endian packed signed integer formats we can set up.
  if (sample_rate != 44100 && sample_rate != 48000)
    return EINVAL;
  if (channel_count < 1 || channel_count > 8)
    return EINVAL;
  if (sample_format != SPS_FORMAT_S16_LE && sample_format != SPS_FORMAT_S32_LE)
    return EINVAL;
  return 0;
}

static int check_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  return check_settings((sps_format_t)format, rate, channels);
}

static int32_t get_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  uint64_t start_time = get_absolute_time_in_ns();
  int32_t response = search_for_suitable_configuration(channels, rate, format, &check_configuration);
  int64_t elapsed_time = get_absolute_time_in_ns() - start_time;
  debug(1, "coreaudio: get_configuration took %0.3f mS.", elapsed_time * 0.000001);
  return response;
}

static void coreaudio_destroy_unit(void) {
  if (output_unit) {
    if (atomic_load(&unit_running)) {
      AudioOutputUnitStop(output_unit);
      atomic_store(&unit_running, 0);
    }
    AudioUnitUninitialize(output_unit);
    AudioComponentInstanceDispose(output_unit);
    output_unit = NULL;
  }
}

static int coreaudio_create_unit(void) {
  AudioComponentDescription desc;
  memset(&desc, 0, sizeof(desc));
  desc.componentType = kAudioUnitType_Output;
  desc.componentSubType = kAudioUnitSubType_DefaultOutput;
  desc.componentManufacturer = kAudioUnitManufacturer_Apple;

  AudioComponent comp = AudioComponentFindNext(NULL, &desc);
  if (!comp) {
    warn("coreaudio: failed to find default output audio component");
    return -1;
  }

  OSStatus st = AudioComponentInstanceNew(comp, &output_unit);
  if (st != noErr) {
    warn("coreaudio: AudioComponentInstanceNew failed: %d", (int)st);
    output_unit = NULL;
    return -1;
  }

  AURenderCallbackStruct cb;
  cb.inputProc = coreaudio_render;
  cb.inputProcRefCon = NULL;
  st = AudioUnitSetProperty(output_unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
                            0, &cb, sizeof(cb));
  if (st != noErr) {
    warn("coreaudio: AudioUnitSetProperty(SetRenderCallback) failed: %d", (int)st);
    coreaudio_destroy_unit();
    return -1;
  }

  AudioStreamBasicDescription asbd;
  memset(&asbd, 0, sizeof(asbd));
  asbd.mSampleRate = (Float64)current_sample_rate;
  asbd.mFormatID = kAudioFormatLinearPCM;
  asbd.mFormatFlags = kAudioFormatFlagIsPacked | kAudioFormatFlagIsSignedInteger;
  asbd.mBitsPerChannel = (UInt32)(current_bytes_per_sample * 8);
  asbd.mChannelsPerFrame = (UInt32)current_channel_count;
  asbd.mFramesPerPacket = 1;
  asbd.mBytesPerFrame = (UInt32)(current_channel_count * current_bytes_per_sample);
  asbd.mBytesPerPacket = asbd.mBytesPerFrame;

  st = AudioUnitSetProperty(output_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
                            &asbd, sizeof(asbd));
  if (st != noErr) {
    warn("coreaudio: AudioUnitSetProperty(StreamFormat) failed: %d", (int)st);
    coreaudio_destroy_unit();
    return -1;
  }

  st = AudioUnitInitialize(output_unit);
  if (st != noErr) {
    warn("coreaudio: AudioUnitInitialize failed: %d", (int)st);
    coreaudio_destroy_unit();
    return -1;
  }

  return 0;
}

/* Player calls output->start() before the first configure(); we create the unit in
 * configure() from setup_software_resampler(), so we must start here (and in play()). */
static void coreaudio_start_output_unit(void) {
  if (!output_unit || atomic_load(&unit_running))
    return;
  OSStatus st = AudioOutputUnitStart(output_unit);
  if (st == noErr) {
    atomic_store(&unit_running, 1);
    debug(2, "coreaudio: AudioOutputUnitStart (default system output device).");
  } else {
    warn("coreaudio: AudioOutputUnitStart failed: %d", (int)st);
  }
}

static int configure(int32_t requested_encoded_format, char **channel_map) {
  debug(3, "coreaudio: configure %s.", short_format_description(requested_encoded_format));

  int response = EINVAL;
  if (requested_encoded_format <= 0)
    return response;

  if (current_encoded_output_format == requested_encoded_format) {
    if (channel_map)
      *channel_map = NULL;
    return 0;
  }

  sps_format_t fmt = FORMAT_FROM_ENCODED_FORMAT(requested_encoded_format);
  unsigned int rate = RATE_FROM_ENCODED_FORMAT(requested_encoded_format);
  unsigned int channels = CHANNELS_FROM_ENCODED_FORMAT(requested_encoded_format);

  if (check_settings(fmt, rate, channels) != 0) {
    debug(1, "coreaudio: unsupported configuration request %s.",
          short_format_description(requested_encoded_format));
    return EINVAL;
  }

  current_encoded_output_format = requested_encoded_format;
  current_sample_rate = rate;
  current_channel_count = channels;
  current_bytes_per_sample = (fmt == SPS_FORMAT_S16_LE) ? 2 : 4;

  // Recreate unit and FIFO for the new format.
  coreaudio_destroy_unit();
  fifo_deinit(&fifo);

  // Keep ~2 seconds of audio to absorb scheduling jitter.
  size_t capacity_frames = (size_t)current_sample_rate * 2;
  size_t capacity_bytes = capacity_frames * current_channel_count * current_bytes_per_sample;
  fifo_init(&fifo, capacity_bytes);

  if (coreaudio_create_unit() != 0) {
    warn("coreaudio: failed to create output unit");
    coreaudio_destroy_unit();
    return EIO;
  }

  coreaudio_start_output_unit();

  debug(1, "coreaudio: output configured to %s.", short_format_description(current_encoded_output_format));

  if (channel_map)
    *channel_map = NULL;
  response = 0;
  return response;
}

static void start(int sample_rate, int sample_format) {
  (void)sample_rate;
  (void)sample_format;
  coreaudio_start_output_unit();
}

static int play(void *buf, int samples, int sample_type, uint32_t timestamp, uint64_t playtime) {
  (void)sample_type;
  (void)timestamp;
  (void)playtime;
  if (!output_unit || current_bytes_per_sample == 0 || current_channel_count == 0)
    return EIO;

  coreaudio_start_output_unit();

  // `samples` is frames per channel.
  size_t bytes = (size_t)samples * current_channel_count * current_bytes_per_sample;
  size_t written = fifo_write(&fifo, (const uint8_t *)buf, bytes, 1);
  if (written != bytes) {
    // should not happen in blocking mode
    return EAGAIN;
  }
  return 0;
}

static void stop(void) {
  if (output_unit && atomic_load(&unit_running)) {
    AudioOutputUnitStop(output_unit);
    atomic_store(&unit_running, 0);
  }
  fifo_clear(&fifo);
}

static void flush(void) { fifo_clear(&fifo); }

static int delay(long *the_delay) {
  if (!the_delay)
    return EINVAL;
  if (current_sample_rate == 0 || current_channel_count == 0 || current_bytes_per_sample == 0) {
    *the_delay = 0;
    return 0;
  }
  size_t used = fifo_used_bytes(&fifo);
  size_t frame_bytes = (size_t)current_channel_count * current_bytes_per_sample;
  long frames = (long)(used / frame_bytes);
  *the_delay = frames;
  return 0;
}

static void help(void) {
  printf("    The coreaudio backend outputs to the system default audio device.\n"
         "    Supported formats: S16_LE and S32_LE; rates: 44100, 48000; channels: 1-8.\n");
}

static int init(int argc, char **argv) {
  (void)argc;
  (void)argv;

  config.audio_backend_buffer_desired_length = 1.0;
  config.audio_backend_latency_offset = 0;

#ifdef CONFIG_FFMPEG
  parse_audio_options("coreaudio", SPS_FORMAT_SET, SPS_RATE_SET, SPS_CHANNEL_SET);
#else
  parse_audio_options("coreaudio", SPS_FORMAT_NON_FFMPEG_SET, SPS_RATE_NON_FFMPEG_SET,
                      SPS_CHANNNEL_NON_FFMPEG_SET);
#endif

  // Configure will allocate the FIFO; keep unconfigured until first configure call.
  memset(&fifo, 0, sizeof(fifo));
  return 0;
}

static void deinit(void) {
  coreaudio_destroy_unit();
  fifo_deinit(&fifo);
  current_encoded_output_format = 0;
  current_sample_rate = 0;
  current_channel_count = 0;
  current_bytes_per_sample = 0;
}

static int prepare(void) { return 0; }

audio_output audio_coreaudio = {.help = &help,
                                .name = "coreaudio",
                                .init = &init,
                                .deinit = &deinit,
                                .prepare = &prepare,
                                .get_configuration = &get_configuration,
                                .configure = &configure,
                                .start = &start,
                                .play = &play,
                                .stop = &stop,
                                .is_running = NULL,
                                .flush = &flush,
                                .delay = &delay,
                                .stats = NULL,
                                .volume = NULL,
                                .parameters = NULL,
                                .mute = NULL};

#endif // COMPILE_FOR_OSX

