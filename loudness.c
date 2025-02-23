#include "loudness.h"
#include "common.h"
#include <math.h>

loudness_processor loudness_r;
loudness_processor loudness_l;

void _loudness_set_volume(loudness_processor *p, float volume) {
  float gain = -(volume - config.loudness_reference_volume_db) * 0.5;
  if (gain < 0)
    gain = 0;

  float Fc = 10.0;
  float Q = 0.5;

  // Formula from http://www.earlevel.com/main/2011/01/02/biquad-formulas/
  float Fs = 44100.0;

  float K = tan(M_PI * Fc / Fs);
  float V = pow(10.0, gain / 20.0);

  float norm = 1 / (1 + 1 / Q * K + K * K);
  p->a0 = (1 + V / Q * K + K * K) * norm;
  p->a1 = 2 * (K * K - 1) * norm;
  p->a2 = (1 - V / Q * K + K * K) * norm;
  p->b1 = p->a1;
  p->b2 = (1 - 1 / Q * K + K * K) * norm;
}

float loudness_process(loudness_processor *p, float i0) {
  float o0 = p->a0 * i0 + p->a1 * p->i1 + p->a2 * p->i2 - p->b1 * p->o1 - p->b2 * p->o2;

  p->o2 = p->o1;
  p->o1 = o0;

  p->i2 = p->i1;
  p->i1 = i0;

  return o0;
}

void loudness_set_volume(float volume) {
  float gain = -(volume - config.loudness_reference_volume_db) * 0.5;
  if (gain < 0)
    gain = 0;

  debug(3, "Volume: %.1f dB - Loudness gain @10Hz: %.1f dB", volume, gain);
  _loudness_set_volume(&loudness_l, volume);
  _loudness_set_volume(&loudness_r, volume);
}
