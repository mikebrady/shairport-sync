#pragma once

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
