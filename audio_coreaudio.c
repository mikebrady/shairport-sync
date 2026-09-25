/*
 * CoreAudio Backend (macOS). This file is part of Shairport Sync.
 * Copyright (c) 2026 the Shairport Sync contributors
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

// Plays through the system default output device using an AUHAL output unit.
// Samples are queued in a ring buffer by play() and drained by the unit's render
// callback. The delay reported to the player is the audio still in the ring buffer,
// plus the audio already handed to the device but not yet presented (estimated from
// the host time stamp of the most recent render), plus the device's own latency.
// That is what lets Shairport Sync keep AirPlay 2 multi-room playback in sync.

#include "audio.h"
#include "common.h"
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>
#include <CoreAudio/HostTime.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static AudioUnit output_unit = NULL;
static int unit_initialized = 0;
static int unit_running = 0;

static int32_t current_encoded_output_format = 0;
static unsigned int bytes_per_frame = 0;
static unsigned int frame_rate = 0;
static long presentation_latency_frames = 0; // device, stream and unit latency, in output frames

static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
static uint8_t *audio_lmb = NULL, *audio_umb, *audio_toq, *audio_eoq;
static size_t audio_size, audio_occupancy;

// the most recent render, used to estimate the audio still queued in the device
static uint64_t last_render_host_time = 0; // when its first frame will be presented
static uint32_t last_render_frames = 0;

static void reset_ring_buffer(void) {
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;
  last_render_host_time = 0;
  last_render_frames = 0;
}

static OSStatus render_callback(__attribute__((unused)) void *ref,
                                __attribute__((unused)) AudioUnitRenderActionFlags *flags,
                                const AudioTimeStamp *time_stamp,
                                __attribute__((unused)) UInt32 bus, UInt32 frames,
                                AudioBufferList *io_data) {
  uint8_t *out = io_data->mBuffers[0].mData;
  size_t bytes_wanted = io_data->mBuffers[0].mDataByteSize;
  size_t bytes_copied = 0;
  // never block the real-time audio thread -- play silence if the buffer is busy
  if (pthread_mutex_trylock(&buffer_mutex) == 0) {
    if (audio_lmb != NULL) {
      size_t bytes_to_copy = bytes_wanted < audio_occupancy ? bytes_wanted : audio_occupancy;
      size_t first_portion = audio_umb - audio_toq;
      if (bytes_to_copy <= first_portion) {
        memcpy(out, audio_toq, bytes_to_copy);
        audio_toq += bytes_to_copy;
      } else {
        memcpy(out, audio_toq, first_portion);
        memcpy(out + first_portion, audio_lmb, bytes_to_copy - first_portion);
        audio_toq = audio_lmb + bytes_to_copy - first_portion;
      }
      if (audio_toq == audio_umb)
        audio_toq = audio_lmb;
      audio_occupancy -= bytes_to_copy;
      bytes_copied = bytes_to_copy;
    }
    if (time_stamp->mFlags & kAudioTimeStampHostTimeValid) {
      last_render_host_time = time_stamp->mHostTime;
      last_render_frames = frames;
    }
    pthread_mutex_unlock(&buffer_mutex);
  }
  if (bytes_copied < bytes_wanted)
    memset(out + bytes_copied, 0, bytes_wanted - bytes_copied);
  return noErr;
}

static AudioDeviceID current_output_device(void) {
  AudioDeviceID device = kAudioObjectUnknown;
  UInt32 size = sizeof(device);
  if (output_unit != NULL)
    AudioUnitGetProperty(output_unit, kAudioOutputUnitProperty_CurrentDevice,
                         kAudioUnitScope_Global, 0, &device, &size);
  return device;
}

// The latency, in seconds, between a frame being presented to the device (the time
// stamp of a render) and it becoming audible.
static double device_presentation_latency(void) {
  AudioDeviceID device = current_output_device();
  if (device == kAudioObjectUnknown)
    return 0.0;

  Float64 device_rate = 0.0;
  UInt32 size = sizeof(device_rate);
  AudioObjectPropertyAddress address = {kAudioDevicePropertyNominalSampleRate,
                                        kAudioObjectPropertyScopeGlobal,
                                        kAudioObjectPropertyElementMain};
  if ((AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &device_rate) != noErr) ||
      (device_rate <= 0.0))
    return 0.0;

  UInt32 device_latency = 0;
  size = sizeof(device_latency);
  address.mSelector = kAudioDevicePropertyLatency;
  address.mScope = kAudioObjectPropertyScopeOutput;
  AudioObjectGetPropertyData(device, &address, 0, NULL, &size, &device_latency);

  UInt32 stream_latency = 0;
  AudioStreamID streams[16];
  size = sizeof(streams);
  address.mSelector = kAudioDevicePropertyStreams;
  if ((AudioObjectGetPropertyData(device, &address, 0, NULL, &size, streams) == noErr) &&
      (size >= sizeof(AudioStreamID))) {
    UInt32 latency_size = sizeof(stream_latency);
    AudioObjectPropertyAddress stream_address = {kAudioStreamPropertyLatency,
                                                 kAudioObjectPropertyScopeGlobal,
                                                 kAudioObjectPropertyElementMain};
    AudioObjectGetPropertyData(streams[0], &stream_address, 0, NULL, &latency_size,
                               &stream_latency);
  }

  Float64 unit_latency = 0.0; // e.g. from sample rate conversion inside the unit
  size = sizeof(unit_latency);
  AudioUnitGetProperty(output_unit, kAudioUnitProperty_Latency, kAudioUnitScope_Global, 0,
                       &unit_latency, &size);

  double latency = (device_latency + stream_latency) / device_rate + unit_latency;
  debug(2,
        "coreaudio: device rate %.0f, device latency %u frames, stream latency %u frames, "
        "unit latency %.6f s, total %.6f s.",
        device_rate, device_latency, stream_latency, unit_latency, latency);
  return latency;
}

static int init(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) {
  config.audio_backend_buffer_desired_length = 0.35;
  config.audio_backend_buffer_interpolation_threshold_in_seconds = 0.02;
  config.audio_backend_latency_offset = 0;

  // get settings from settings file, passing in defaults for format_set, rate_set and channel_set
  // Note, these options may be in the "general" stanza or the named stanza
  parse_audio_options("coreaudio", (1 << SPS_FORMAT_S32_LE) | (1 << SPS_FORMAT_S16_LE),
                      (1 << SPS_RATE_44100) | (1 << SPS_RATE_48000), (1 << 2));

  AudioComponentDescription description = {.componentType = kAudioUnitType_Output,
                                           .componentSubType = kAudioUnitSubType_DefaultOutput,
                                           .componentManufacturer = kAudioUnitManufacturer_Apple};
  AudioComponent component = AudioComponentFindNext(NULL, &description);
  if (component == NULL)
    die("coreaudio: can't find the default output audio unit.");
  if (AudioComponentInstanceNew(component, &output_unit) != noErr)
    die("coreaudio: can't create the default output audio unit.");

  AURenderCallbackStruct callback = {.inputProc = render_callback, .inputProcRefCon = NULL};
  if (AudioUnitSetProperty(output_unit, kAudioUnitProperty_SetRenderCallback, kAudioUnitScope_Input,
                           0, &callback, sizeof(callback)) != noErr)
    die("coreaudio: can't set the render callback.");
  return 0;
}

static void deinit(void) {
  if (output_unit != NULL) {
    if (unit_running)
      AudioOutputUnitStop(output_unit);
    if (unit_initialized)
      AudioUnitUninitialize(output_unit);
    AudioComponentInstanceDispose(output_unit);
    output_unit = NULL;
  }
  unit_running = 0;
  unit_initialized = 0;
  free(audio_lmb);
  audio_lmb = NULL;
}

static int32_t get_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  // the output unit converts any linear PCM it is given to the device's own format, so
  // the standard search over the configured formats, rates and channels is all that's needed
  return search_for_suitable_configuration(channels, rate, format, NULL);
}

static int configure(int32_t requested_encoded_format, char **resulting_channel_map) {
  if (resulting_channel_map != NULL)
    *resulting_channel_map = NULL; // no channel map -- use the default channel order
  if (requested_encoded_format == current_encoded_output_format)
    return 0;

  sps_format_t format = FORMAT_FROM_ENCODED_FORMAT(requested_encoded_format);
  unsigned int channels = CHANNELS_FROM_ENCODED_FORMAT(requested_encoded_format);
  unsigned int rate = RATE_FROM_ENCODED_FORMAT(requested_encoded_format);
  unsigned int bits;
  if (format == SPS_FORMAT_S16_LE)
    bits = 16;
  else if (format == SPS_FORMAT_S32_LE)
    bits = 32;
  else {
    warn("coreaudio: unsupported output format %s.",
         short_format_description(requested_encoded_format));
    return EINVAL;
  }

  if (unit_running) {
    AudioOutputUnitStop(output_unit);
    unit_running = 0;
  }
  if (unit_initialized) {
    AudioUnitUninitialize(output_unit);
    unit_initialized = 0;
  }

  AudioStreamBasicDescription stream_format = {.mSampleRate = rate,
                                               .mFormatID = kAudioFormatLinearPCM,
                                               .mFormatFlags = kAudioFormatFlagIsSignedInteger |
                                                               kAudioFormatFlagIsPacked,
                                               .mBytesPerPacket = channels * bits / 8,
                                               .mFramesPerPacket = 1,
                                               .mBytesPerFrame = channels * bits / 8,
                                               .mChannelsPerFrame = channels,
                                               .mBitsPerChannel = bits};
  OSStatus status =
      AudioUnitSetProperty(output_unit, kAudioUnitProperty_StreamFormat, kAudioUnitScope_Input, 0,
                           &stream_format, sizeof(stream_format));
  if (status != noErr) {
    warn("coreaudio: can't set the stream format to %s (error %d).",
         short_format_description(requested_encoded_format), (int)status);
    return EINVAL;
  }
  status = AudioUnitInitialize(output_unit);
  if (status != noErr) {
    warn("coreaudio: can't initialise the output unit (error %d).", (int)status);
    return EIO;
  }
  unit_initialized = 1;

  pthread_mutex_lock(&buffer_mutex);
  free(audio_lmb);
  bytes_per_frame = stream_format.mBytesPerFrame;
  frame_rate = rate;
  audio_size = (size_t)rate * bytes_per_frame; // one second
  audio_lmb = malloc(audio_size);
  if (audio_lmb == NULL)
    die("coreaudio: can't allocate %zu bytes for the audio buffer.", audio_size);
  reset_ring_buffer();
  pthread_mutex_unlock(&buffer_mutex);

  presentation_latency_frames = (long)(device_presentation_latency() * rate + 0.5);
  current_encoded_output_format = requested_encoded_format;
  debug(1, "coreaudio: output configured to %s, presentation latency %ld frames.",
        short_format_description(requested_encoded_format), presentation_latency_frames);
  return 0;
}

static int play(void *buf, int samples, __attribute__((unused)) int sample_type,
                __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {
  if ((audio_lmb == NULL) || (bytes_per_frame == 0))
    return -ENODEV;
  size_t bytes_to_transfer = (size_t)samples * bytes_per_frame;
  pthread_mutex_lock(&buffer_mutex);
  size_t bytes_available = audio_size - audio_occupancy;
  if (bytes_available < bytes_to_transfer) {
    debug(1, "coreaudio: buffer overflow -- dropping %zu bytes.",
          bytes_to_transfer - bytes_available);
    bytes_to_transfer = bytes_available;
  }
  size_t space_to_end_of_buffer = audio_umb - audio_eoq;
  if (space_to_end_of_buffer >= bytes_to_transfer) {
    memcpy(audio_eoq, buf, bytes_to_transfer);
    audio_eoq += bytes_to_transfer;
  } else {
    memcpy(audio_eoq, buf, space_to_end_of_buffer);
    memcpy(audio_lmb, (uint8_t *)buf + space_to_end_of_buffer,
           bytes_to_transfer - space_to_end_of_buffer);
    audio_eoq = audio_lmb + bytes_to_transfer - space_to_end_of_buffer;
  }
  if (audio_eoq == audio_umb)
    audio_eoq = audio_lmb;
  audio_occupancy += bytes_to_transfer;
  pthread_mutex_unlock(&buffer_mutex);

  if ((!unit_running) && (unit_initialized)) {
    OSStatus status = AudioOutputUnitStart(output_unit);
    if (status == noErr)
      unit_running = 1;
    else
      warn("coreaudio: can't start the output unit (error %d).", (int)status);
  }
  return 0;
}

static int delay(long *the_delay) {
  if ((audio_lmb == NULL) || (bytes_per_frame == 0) || (frame_rate == 0))
    return -ENODEV;
  pthread_mutex_lock(&buffer_mutex);
  long frames = audio_occupancy / bytes_per_frame;
  if ((unit_running) && (last_render_host_time != 0)) {
    // frames already handed to the device that haven't been presented yet
    int64_t render_end_ns = (int64_t)AudioConvertHostTimeToNanos(last_render_host_time) +
                            (int64_t)last_render_frames * 1000000000LL / frame_rate;
    int64_t remaining_ns =
        render_end_ns - (int64_t)AudioConvertHostTimeToNanos(AudioGetCurrentHostTime());
    if (remaining_ns > 0)
      frames += (long)(remaining_ns * (int64_t)frame_rate / 1000000000LL);
  }
  pthread_mutex_unlock(&buffer_mutex);
  *the_delay = frames + presentation_latency_frames;
  return 0;
}

static void flush(void) {
  pthread_mutex_lock(&buffer_mutex);
  if (audio_lmb != NULL)
    reset_ring_buffer();
  pthread_mutex_unlock(&buffer_mutex);
}

static void stop(void) {
  if (unit_running) {
    AudioOutputUnitStop(output_unit);
    unit_running = 0;
  }
  flush();
}

audio_output audio_coreaudio = {.name = "coreaudio",
                                .help = NULL,
                                .init = &init,
                                .deinit = &deinit,
                                .prepare = NULL,
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
