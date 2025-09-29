

#include "convolver.h"
#include "FFTConvolver.h"
#include "Utilities.h"
#include <pthread.h>
#include <sndfile.h>

extern "C" void _warn(const char *filename, const int linenumber, const char *format, ...);
extern "C" void _debug(const char *filename, const int linenumber, int level, const char *format,
                       ...);

#define warn(...) _warn(__FILE__, __LINE__, __VA_ARGS__)
#define debug(...) _debug(__FILE__, __LINE__, __VA_ARGS__)

fftconvolver::FFTConvolver convolver_l;
fftconvolver::FFTConvolver convolver_r;

// always lock use this when accessing the playing conn value
pthread_mutex_t convolver_lock = PTHREAD_MUTEX_INITIALIZER;

int convolver_init(const char *filename, int max_length) {
  int success = 0;
  SF_INFO info;
  if (filename) {
    SNDFILE *file = sf_open(filename, SFM_READ, &info);
    if (file) {

      if (info.samplerate == 44100) {
        if ((info.channels == 1) || (info.channels == 2)) {
          const size_t size = info.frames > max_length ? max_length : info.frames;
          float buffer[size * info.channels];

          size_t l = sf_readf_float(file, buffer, size);
          if (l != 0) {
            pthread_mutex_lock(&convolver_lock);
            convolver_l.reset(); // it is possible that init could be called more than once
            convolver_r.reset(); // so it could be necessary to remove all previous settings

            if (info.channels == 1) {
              convolver_l.init(352, buffer, size);
              convolver_r.init(352, buffer, size);
            } else {
              // deinterleave
              float buffer_l[size];
              float buffer_r[size];

              unsigned int i;
              for (i = 0; i < size; ++i) {
                buffer_l[i] = buffer[2 * i + 0];
                buffer_r[i] = buffer[2 * i + 1];
              }

              convolver_l.init(352, buffer_l, size);
              convolver_r.init(352, buffer_r, size);
            }
            pthread_mutex_unlock(&convolver_lock);
            success = 1;
          }
          debug(1,
                "Convolution impulse response filter initialized from \"%s\" with %d channel%s and "
                "%d samples",
                filename, info.channels, info.channels == 1 ? "" : "s", size);
        } else {
          warn("Convolution impulse response filter file \"%s\" contains %d channels. Only 1 or 2 "
               "is supported.",
               filename, info.channels);
        }
      } else {
        warn("Convolution impulse response filter file \"%s\" sample rate is %d Hz. Only 44100 Hz "
             "is supported.",
             filename, info.samplerate);
      }
      sf_close(file);
    } else {
      warn("Convolution impulse response filter file \"%s\" can not be opened. Please check that "
           "it exists and has appropriate permissions.",
           filename);
    }
  }
  return success;
}

void convolver_process_l(float *data, int length) {
  pthread_mutex_lock(&convolver_lock);
  convolver_l.process(data, data, length);
  pthread_mutex_unlock(&convolver_lock);
}

void convolver_process_r(float *data, int length) {
  pthread_mutex_lock(&convolver_lock);
  convolver_r.process(data, data, length);
  pthread_mutex_unlock(&convolver_lock);
}
