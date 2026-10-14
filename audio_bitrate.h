#ifndef AUDIO_BITRATE_H
#define AUDIO_BITRATE_H

#include <stdint.h>

/* Match the player's bounded packet window. No allocation on packet arrival. */
#define AUDIO_BITRATE_HISTORY_SIZE 1024
#define AUDIO_BITRATE_REPORT 1
#define AUDIO_BITRATE_RESET 2

typedef struct {
  uint64_t bytes, samples, next_report;
  uint32_t rate;
  int have_packet;
  struct {
    uint32_t sequence, timestamp;
    int valid;
  } packets[AUDIO_BITRATE_HISTORY_SIZE];
} audio_bitrate_meter;

static inline int audio_bitrate_reset(audio_bitrate_meter *meter) {
  int had_data = meter->have_packet;
  *meter = (audio_bitrate_meter){0};
  return had_data;
}

static inline int audio_bitrate_is_duplicate(const audio_bitrate_meter *meter,
                                           uint32_t sequence, uint32_t timestamp) {
  unsigned slot = sequence % AUDIO_BITRATE_HISTORY_SIZE;
  return meter->packets[slot].valid && meter->packets[slot].sequence == sequence &&
         meter->packets[slot].timestamp == timestamp;
}

/* Payload excludes transport/encryption headers and the synthetic ADTS header.
 * Samples are input sample frames, independent of channels and resampling. */
static inline int audio_bitrate_add(audio_bitrate_meter *meter, uint64_t bytes,
                                 uint32_t samples, uint32_t rate,
                                 uint32_t sequence, uint32_t timestamp) {
  if (!bytes || !samples || (rate != 44100 && rate != 48000))
    return 0;
  int result = 0;
  /* A retransmission must not trigger overflow or restart a reporting window. */
  if (meter->rate == rate && audio_bitrate_is_duplicate(meter, sequence, timestamp))
    return 0;
  if (meter->rate != rate || UINT64_MAX - meter->bytes < bytes ||
      UINT64_MAX - meter->samples < samples ||
      UINT64_MAX - meter->samples - samples < rate) {
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
  if (!meter->next_report)
    meter->next_report = 2 * (uint64_t)rate;
  if (meter->samples < meter->next_report)
    return result;
  meter->next_report = meter->samples + rate;
  return result | AUDIO_BITRATE_REPORT;
}
#endif
