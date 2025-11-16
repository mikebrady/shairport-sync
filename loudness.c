#include "loudness.h"
#include "common.h"
#include <math.h>

#define MAXCHANNELS 8

// loudness_processor_dynamic loudness_r;
// loudness_processor_dynamic loudness_l;

loudness_processor_dynamic loudness_filters[MAXCHANNELS];

// if the rate or the loudness_reference_volume_db change, recalculate the
// loudness volume parameters

static int loudness_fix_volume_parameter = 0;
static unsigned int loudness_rate_parameter = 0;
static float loudness_volume_reference_parameter = 0.0;
static loudness_processor_static lps = {0.0, 0.0, 0.0, 0.0, 0.0};

void _loudness_set_volume(loudness_processor_static *p, float volume, unsigned int sample_rate) {
  float gain = -(volume - config.loudness_reference_volume_db) * 0.5;
  if (gain < 0) {
    gain = 0;
  }
  debug(2, "Volume: %.1f dB - Loudness gain @10Hz: %.1f dB", volume, gain);

  float Fc = 10.0;
  float Q = 0.5;

  // Formula from http://www.earlevel.com/main/2011/01/02/biquad-formulas/
  float Fs = sample_rate * 1.0;

  float K = tan(M_PI * Fc / Fs);
  float V = pow(10.0, gain / 20.0);

  float norm = 1 / (1 + 1 / Q * K + K * K);
  p->a0 = (1 + V / Q * K + K * K) * norm;
  p->a1 = 2 * (K * K - 1) * norm;
  p->a2 = (1 - V / Q * K + K * K) * norm;
  p->b1 = p->a1;
  p->b2 = (1 - 1 / Q * K + K * K) * norm;
}

float loudness_process(loudness_processor_dynamic *p, float i0) {
  float o0 = lps.a0 * i0 + lps.a1 * p->i1 + lps.a2 * p->i2 - lps.b1 * p->o1 - lps.b2 * p->o2;

  p->o2 = p->o1;
  p->o1 = o0;

  p->i2 = p->i1;
  p->i1 = i0;

  return o0;
}

void loudness_update(rtsp_conn_info *conn) {
  // first, see if loudness can be enabled
  int do_loudness = config.loudness_enabled;
  if ((config.output->parameters != NULL) && (config.output->parameters()->volume_range != NULL)) {
    do_loudness = 0; // if we are using external (usually hardware) volume controls.
  }

  if (do_loudness) {
    // check the volume parameters
    if ((conn->fix_volume != loudness_fix_volume_parameter) ||
        (conn->input_rate != loudness_rate_parameter) ||
        (config.loudness_reference_volume_db != loudness_volume_reference_parameter)) {
      debug(1, "update loudness parameters");
      float new_volume = 20 * log10((double)conn->fix_volume / 65536);
      _loudness_set_volume(&lps, new_volume, conn->input_rate);
      // _loudness_set_volume(&loudness_r, new_volume, conn->input_rate);
      loudness_fix_volume_parameter = conn->fix_volume;
      loudness_rate_parameter = conn->input_rate;
      loudness_volume_reference_parameter = config.loudness_reference_volume_db;
    }
  }
  conn->do_loudness = do_loudness;
}

void loudness_process_blocks(float *fbufs, unsigned int channel_length,
                             unsigned int number_of_channels, float gain) {
  unsigned int channel_number, sample_index;
  float *sample_pointer = fbufs;
  for (channel_number = 0; channel_number < number_of_channels; channel_number++) {
    for (sample_index = 0; sample_index < channel_length; sample_index++) {
      *sample_pointer = loudness_process(&loudness_filters[channel_number], *sample_pointer * gain);
      sample_pointer++;
    }
  }
}

void loudness_reset() {
  unsigned int i;
  for (i = 0; i < MAXCHANNELS; i++) {
    loudness_filters[i].i1 = 0.0;
    loudness_filters[i].i2 = 0.0;
    loudness_filters[i].o1 = 0.0;
    loudness_filters[i].o2 = 0.0;
  }
}