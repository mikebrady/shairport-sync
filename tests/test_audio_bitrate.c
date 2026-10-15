/* Keep meter operations and checks active with release build flags. */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include "audio_bitrate.h"

int main(void) {
  audio_bitrate_meter meter = {0};
  unsigned reports = 0;
  for (unsigned i = 0; i < 141; i++)
    reports += audio_bitrate_add(&meter, 680 + i % 3, 1024, 48000, i, i * 1024);
  assert(reports == 2);
  assert(meter.samples == 141 * 1024 && meter.bytes == 141 * 681);
  uint64_t bytes = meter.bytes, samples = meter.samples;
  assert(!audio_bitrate_add(&meter, 1000, 1024, 48000, 140, 140 * 1024));
  assert(meter.bytes == bytes && meter.samples == samples);
  assert(!audio_bitrate_add(&meter, 1000, 0, 48000, 141, 141 * 1024));
  assert(!audio_bitrate_add(&meter, 1000, 1024, 0, 141, 141 * 1024));
  assert(meter.bytes == bytes && meter.samples == samples);
  assert(audio_bitrate_reset(&meter) && !audio_bitrate_reset(&meter));
  assert(audio_bitrate_add(&meter, 64000, 96000, 48000, 0, 0));
  assert(meter.bytes * 8 * meter.rate / meter.samples == 256000);
  assert(audio_bitrate_add(&meter, 64000, 88200, 44100, 1, 0));
  assert(meter.bytes == 64000 && meter.samples == 88200);
  assert(meter.bytes * 8 * meter.rate / meter.samples == 256000);
  audio_bitrate_reset(&meter);
  assert(audio_bitrate_add(&meter, 32000, 48000, 48000, 0, 0) == 0);
  assert(audio_bitrate_add(&meter, 48000, 48000, 48000, 1, 48000));
  assert(meter.bytes * 8 * meter.rate / meter.samples == 320000);
  /* The meter accumulates the payload byte counts supplied by its caller. */
  assert(meter.bytes == 80000);
  meter.bytes = UINT64_MAX - 10;
  assert(audio_bitrate_add(&meter, 1000, 1024, 48000, 2, 96000) == AUDIO_BITRATE_RESET);
  assert(meter.bytes == 1000 && meter.samples == 1024);

  /* ALAC UDP recovery is out of order. Neither recovery nor duplicate resends
   * should erase a window or inflate compressed bytes and input frames. */
  audio_bitrate_reset(&meter);
  assert(!audio_bitrate_add(&meter, 700, 352, 44100, 65534, 0xfffffd40));
  assert(!audio_bitrate_add(&meter, 900, 352, 44100, 0, 0));
  assert(!audio_bitrate_add(&meter, 800, 352, 44100, 65535, 0xfffffea0));
  assert(!audio_bitrate_add(&meter, 700, 352, 44100, 65534, 0xfffffd40));
  assert(meter.bytes == 2400 && meter.samples == 1056);
  assert(!audio_bitrate_add(&meter, 1000, 352, 44100, 1, 352));
  assert(!audio_bitrate_add(&meter, 800, 352, 44100, 65535, 0xfffffea0));
  assert(meter.bytes == 3400 && meter.samples == 1408);

  /* Every slot in the player window can be retransmitted. */
  audio_bitrate_reset(&meter);
  for (unsigned i = 0; i < AUDIO_BITRATE_HISTORY_SIZE; i++)
    audio_bitrate_add(&meter, 1000 + i, 352, 44100, i, i * 352);
  bytes = meter.bytes;
  samples = meter.samples;
  for (unsigned i = AUDIO_BITRATE_HISTORY_SIZE; i-- > 0;)
    assert(!audio_bitrate_add(&meter, 1000 + i, 352, 44100, i, i * 352));
  assert(meter.bytes == bytes && meter.samples == samples);
  /* Reusing a slot or wrapping a sequence with a new timestamp is new audio. */
  audio_bitrate_add(&meter, 2000, 352, 44100, AUDIO_BITRATE_HISTORY_SIZE, samples);
  audio_bitrate_add(&meter, 3000, 352, 44100, 0, samples + 352);
  assert(meter.bytes == bytes + 5000 && meter.samples == samples + 704);

  /* Duplicate retransmission must not trigger an overflow reset. */
  meter.bytes = UINT64_MAX - 1;
  assert(!audio_bitrate_add(&meter, 3000, 352, 44100, 0, samples + 352));
  assert(meter.bytes == UINT64_MAX - 1);
  assert(audio_bitrate_add(&meter, 4000, 352, 44100, 1, samples + 704) == AUDIO_BITRATE_RESET);
  meter.samples = UINT64_MAX - 100;
  assert(audio_bitrate_add(&meter, 4000, 352, 44100, 2, samples + 1056) == AUDIO_BITRATE_RESET);
  assert(meter.samples == 352);
  /* Reserve enough headroom for the next reporting threshold too. */
  meter.samples = UINT64_MAX - 44100;
  assert(audio_bitrate_add(&meter, 4000, 352, 44100, 3, samples + 1408) == AUDIO_BITRATE_RESET);
  assert(meter.samples == 352);

  audio_bitrate_reset(&meter);
  assert(audio_bitrate_add(&meter, 220500, 88200, 44100, 0, 0) == AUDIO_BITRATE_REPORT);
  assert(meter.bytes * 8 * meter.rate / meter.samples == 882000);
  assert(audio_bitrate_add(&meter, 360000, 96000, 48000, 0, 0) ==
         (AUDIO_BITRATE_RESET | AUDIO_BITRATE_REPORT));
  assert(meter.bytes * 8 * meter.rate / meter.samples == 1440000);
  printf("Audio bitrate tests passed; persistent state=%zu bytes\n", sizeof(meter));
}
