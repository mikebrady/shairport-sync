#include <assert.h>
#include <stdio.h>
#include "aac_bitrate.h"

int main(void) {
  aac_bitrate_meter meter = {0};
  unsigned reports = 0;
  for (unsigned i = 0; i < 141; i++)
    reports += aac_bitrate_add(&meter, 680 + i % 3, 1024, 48000, i, i * 1024);
  assert(reports == 2);
  assert(meter.samples == 141 * 1024 && meter.bytes == 141 * 681);
  uint64_t bytes = meter.bytes, samples = meter.samples;
  assert(!aac_bitrate_add(&meter, 1000, 1024, 48000, 140, 140 * 1024));
  assert(meter.bytes == bytes && meter.samples == samples);
  assert(!aac_bitrate_add(&meter, 1000, 0, 48000, 141, 141 * 1024));
  assert(!aac_bitrate_add(&meter, 1000, 1024, 0, 141, 141 * 1024));
  assert(meter.bytes == bytes && meter.samples == samples);
  assert(aac_bitrate_reset(&meter) && !aac_bitrate_reset(&meter));
  assert(aac_bitrate_add(&meter, 64000, 96000, 48000, 0, 0));
  assert(meter.bytes * 8 * meter.rate / meter.samples == 256000);
  assert(aac_bitrate_add(&meter, 64000, 88200, 44100, 1, 0));
  assert(meter.bytes == 64000 && meter.samples == 88200);
  assert(meter.bytes * 8 * meter.rate / meter.samples == 256000);
  aac_bitrate_reset(&meter);
  assert(aac_bitrate_add(&meter, 32000, 48000, 48000, 0, 0) == 0);
  assert(aac_bitrate_add(&meter, 48000, 48000, 48000, 1, 48000));
  assert(meter.bytes * 8 * meter.rate / meter.samples == 320000);
  /* A synthetic ADTS header must not reach the meter. */
  assert(meter.bytes == 80000);
  meter.bytes = UINT64_MAX - 10;
  assert(!aac_bitrate_add(&meter, 1000, 1024, 48000, 2, 96000));
  assert(meter.bytes == 1000 && meter.samples == 1024);
  printf("AAC meter tests passed; persistent state=%zu bytes\n", sizeof(meter));
}
