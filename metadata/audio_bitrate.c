/*
 * Audio bitrate calculator.
 * This file is part of Shairport Sync.
 * Copyright Leon Roy (c) 2026
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

#include "audio_bitrate.h"
#include "audio_bitrate_structs.h"
#include "common.h"
#include "core.h"
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

int audio_bitrate_reset(audio_bitrate_meter *meter) {
  int had_data = meter->have_packet;
  *meter = (audio_bitrate_meter){0};
  return had_data;
}

int audio_bitrate_is_duplicate(const audio_bitrate_meter *meter, uint32_t sequence,
                               uint32_t timestamp) {
  unsigned slot = sequence % AUDIO_BITRATE_HISTORY_SIZE;
  return meter->packets[slot].valid && meter->packets[slot].sequence == sequence &&
         meter->packets[slot].timestamp == timestamp;
}

/* Payload excludes transport/encryption headers and the synthetic ADTS header.
 * Samples are input sample frames, independent of channels and resampling. */
int audio_bitrate_add(audio_bitrate_meter *meter, uint64_t bytes, uint32_t samples, uint32_t rate,
                      uint32_t sequence, uint32_t timestamp) {
  if (!bytes || !samples || (rate != 44100 && rate != 48000))
    return 0;
  int result = 0;
  /* A retransmission must not trigger overflow or restart a reporting window. */
  if (meter->rate == rate && audio_bitrate_is_duplicate(meter, sequence, timestamp))
    return 0;
  if (meter->rate != rate || UINT64_MAX - meter->bytes < bytes ||
      UINT64_MAX - meter->samples < samples || UINT64_MAX - meter->samples - samples < rate) {
    if (audio_bitrate_reset(meter))
      result |= AUDIO_BITRATE_RESET;
  }
  meter->rate = rate;
  meter->bytes += bytes;
  meter->samples += samples;
  unsigned slot = sequence % AUDIO_BITRATE_HISTORY_SIZE;
  meter->packets[slot].sequence = sequence;
  meter->packets[slot].timestamp = timestamp;
  meter->packets[slot].valid = 1;
  meter->have_packet = 1;
  if (!meter->next_report) {
    if (config.bitrate_stats_interval > 0.0) {
      meter->next_report = (uint64_t)(config.bitrate_stats_interval * rate);
    } else {
      meter->next_report = 5 * (uint64_t)rate;
    }
  }
  if (meter->samples < meter->next_report)
    return result;
  meter->next_report = meter->samples + (uint64_t)(config.bitrate_stats_interval * rate);
  return result | AUDIO_BITRATE_REPORT;
}

/* Queue submission may take its mutex, but never waits for queue capacity. */
static void publish_audio_bitrate(const audio_bitrate_meter *meter) {
  char text[96];
  int length = snprintf(text, sizeof(text), "%" PRIu64 "/%" PRIu64 "/%u", meter ? meter->bytes : 0,
                        meter ? meter->samples : 0, meter ? meter->rate : 0);
  if (config.bitrate_stats_interval != 0.0) {
    if (length > 0 && (size_t)length < sizeof(text))
      send_ssnc_metadata('abrt', text, length, 0);
  }
}

void reset_input_bitrate(rtsp_conn_info *conn) {
  /* Caller holds flush_mutex. This also invalidates packets being decoded. */
  conn->input_bitrate_generation++;
  conn->input_bitrate_ssrc = SSRC_NONE;
  if (audio_bitrate_reset(&conn->input_bitrate))
    publish_audio_bitrate(NULL);
}

static int input_bitrate_is_playing(const rtsp_conn_info *conn) {
#ifdef CONFIG_AIRPLAY_2
  if (conn->airplay_type == ap_2 && conn->airplay_stream_type == buffered_stream)
    return conn->ap2_play_enabled;
#endif
  return !conn->flush_requested;
}

uint64_t input_bitrate_packet_generation(rtsp_conn_info *conn, uint32_t ssrc, uint32_t sequence,
                                         uint32_t timestamp, int32_t timestamp_gap) {
  uint64_t generation = 0;
  pthread_mutex_lock_and_cleanup_push(&conn->flush_mutex);
  if (input_bitrate_is_playing(conn)) {
    if (conn->input_bitrate_ssrc != ssrc ||
        (timestamp_gap && !audio_bitrate_is_duplicate(&conn->input_bitrate, sequence, timestamp))) {
      reset_input_bitrate(conn);
      conn->input_bitrate_ssrc = ssrc;
    }
  }
  generation = conn->input_bitrate_generation;
  pthread_cleanup_pop(1);
  return generation;
}

void account_input_bitrate(rtsp_conn_info *conn, uint32_t ssrc, uint64_t bytes, uint32_t samples,
                           uint32_t sequence, uint32_t timestamp, uint64_t generation) {
  pthread_mutex_lock_and_cleanup_push(&conn->flush_mutex);
  if (generation == conn->input_bitrate_generation && input_bitrate_is_playing(conn)) {
    audio_bitrate_meter *meter = &conn->input_bitrate;
    uint32_t rate = get_ssrc_rate(ssrc);
    /* Native Classic ALAC uses an artificial 44.1-kHz SSRC even when ANNOUNCE
     * negotiates 48 kHz. Buffered/FFmpeg formats retain their SSRC-derived rate. */
    if (ssrc == ALAC_44100_S16_2 &&
#ifdef CONFIG_AIRPLAY_2
        conn->airplay_type == ap_1 &&
#endif
        (config.decoder_in_use == 1 << decoder_hammerton ||
         config.decoder_in_use == 1 << decoder_apple_alac))
      rate = conn->input_rate;
    int result = audio_bitrate_add(meter, bytes, samples, rate, sequence, timestamp);
    if (result & AUDIO_BITRATE_RESET) {
      conn->input_bitrate_generation++;
      publish_audio_bitrate(NULL);
    }
    if (result & AUDIO_BITRATE_REPORT)
      publish_audio_bitrate(meter);
  }
  pthread_cleanup_pop(1);
}

void report_player_counters(rtsp_conn_info *conn) {
  if (config.bitrate_stats_interval != 0.0) {
    uint64_t stats_now = get_absolute_time_in_ns();
    if (!conn->last_receiver_stats || stats_now - conn->last_receiver_stats >= (config.bitrate_stats_interval * 1E9)) {
      uint64_t missing = 0, too_late = 0, retries = 0;
      pthread_mutex_lock_and_cleanup_push(&conn->ab_mutex);
      missing = conn->missing_packets;
      too_late = conn->too_late_packets;
      retries = conn->resend_requests;
      pthread_cleanup_pop(1);
  
      char stats[80];
      int length = snprintf(stats, sizeof(stats), "%" PRIu64 "/%" PRIu64 "/%" PRIu64, missing,
                            too_late, retries);
      
        if (length > 0 && (size_t)length < sizeof(stats))
          send_ssnc_metadata('arst', stats, length, 0);
  
      conn->last_receiver_stats = stats_now;
    }
  }
}
