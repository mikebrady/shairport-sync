#pragma once

#include "player.h"
#include <stdio.h>

typedef struct {
  float a0, a1, a2, b1, b2;
} loudness_processor_static;

typedef struct {
  float i1, i2, o1, o2;
} loudness_processor_dynamic;

// extern loudness_processor_dynamic loudness_r;
// extern loudness_processor_dynamic loudness_l;

void loudness_set_volume(float volume);
float loudness_process(loudness_processor_dynamic *p, float sample);
void loudness_update(rtsp_conn_info *conn);

void loudness_reset();
void loudness_process_blocks(float *fbufs, unsigned int channel_length,
                             unsigned int number_of_channels, float gain);