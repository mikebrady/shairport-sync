#pragma once

#include <stdint.h>

double airplayVolumeToUnitVolume(double airplayVolume);

int parse_prlg(const char *str, uint32_t *a, uint32_t *b, uint32_t *c);