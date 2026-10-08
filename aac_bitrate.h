#ifndef AAC_BITRATE_H
#define AAC_BITRATE_H

#include <stdint.h>

typedef struct {
  uint64_t bytes, samples, next_report;
  uint32_t rate, last_sequence, last_timestamp;
  int have_packet;
} aac_bitrate_meter;

static inline int aac_bitrate_reset(aac_bitrate_meter *meter) {
  int had_data = meter->have_packet;
  *meter = (aac_bitrate_meter){0};
  return had_data;
}

static inline int aac_bitrate_is_duplicate(const aac_bitrate_meter *meter,
                                           uint32_t sequence, uint32_t timestamp) {
  return meter->have_packet && meter->last_sequence == sequence &&
         meter->last_timestamp == timestamp;
}

/* Payload excludes transport/encryption headers and the synthetic ADTS header.
 * Samples are input sample frames, independent of channels and resampling. */
static inline int aac_bitrate_add(aac_bitrate_meter *meter, uint64_t bytes,
                                 uint32_t samples, uint32_t rate,
                                 uint32_t sequence, uint32_t timestamp) {
  if (!bytes || !samples || (rate != 44100 && rate != 48000))
    return 0;
  if (aac_bitrate_is_duplicate(meter, sequence, timestamp))
    return 0;
  if (meter->rate != rate)
    aac_bitrate_reset(meter);
  if (UINT64_MAX - meter->bytes < bytes || UINT64_MAX - meter->samples < samples)
    aac_bitrate_reset(meter);
  meter->rate = rate;
  meter->bytes += bytes;
  meter->samples += samples;
  meter->last_sequence = sequence;
  meter->last_timestamp = timestamp;
  meter->have_packet = 1;
  if (!meter->next_report)
    meter->next_report = 2 * (uint64_t)rate;
  if (meter->samples < meter->next_report)
    return 0;
  meter->next_report = meter->samples + rate;
  return 1;
}
#endif
