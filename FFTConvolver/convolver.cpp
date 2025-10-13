

#include "convolver.h"
#include "ConvolverThreadPool.h"
#include "FFTConvolver.h"
#include "Utilities.h"
#include <pthread.h>
#include <sndfile.h>
#include <unistd.h>

extern "C" void _warn(const char *filename, const int linenumber, const char *format, ...);
extern "C" void _debug(const char *filename, const int linenumber, int level, const char *format,
                       ...);

#define warn(...) _warn(__FILE__, __LINE__, __VA_ARGS__)
#define debug(...) _debug(__FILE__, __LINE__, __VA_ARGS__)

// Create and initialize the thread pool
ConvolverThreadPool pool;

void convolver_pool_init(size_t numThreads, size_t numConvolvers) {
  if (!pool.init(numThreads, numConvolvers)) {
    debug(1, "failed to initialize thread pool!");
  } else {
    debug(1, "thread pool initialized with %u threads and %u convolvers.", numThreads,
          numConvolvers);
  }
}

void convolver_pool_closedown() {
  pool.shutdown(); // Just shutdown, don't delete
  debug(3, "thread pool shut down");
}

int convolver_init(const char *filename, unsigned char channel_count,
                       double max_length_in_seconds, size_t block_size) {
  debug(3, "convolver_init");
  int success = 0;
  SF_INFO info = {}; // Zero everything, including format
  if (filename) {
    SNDFILE *file = sf_open(filename, SFM_READ, &info);
    if (file) {
      size_t max_length = (size_t)(max_length_in_seconds * info.samplerate);
      const size_t size =
          (unsigned int)info.frames > max_length ? max_length : (unsigned int)info.frames;
      float *buffer = (float*)malloc(sizeof(float) * size * info.channels);
      if (buffer != NULL) {
        // float buffer[size * info.channels];
        float *abuffer = (float*)malloc(sizeof(float) * size);
        if (abuffer != NULL) {
          size_t l = sf_readf_float(file, buffer, size);
          if (l != 0) {
            unsigned int cc;
            if (info.channels == 1) {
              for (cc = 0; cc < channel_count; cc++) {
                if (!pool.initConvolver(cc, block_size, buffer, size)) {
                  debug(1, "new convolver failed to initialize convolver %u ", cc);
                }
              }
            } else if (info.channels == channel_count) {
              // we have to deinterleave the ir file channels for each convolver
              // float abuffer[size];
              for (cc = 0; cc < channel_count; cc++) {
                unsigned int i;
                for (i = 0; i < size; ++i) {
                  abuffer[i] = buffer[channel_count * i + cc];
                }
                if (!pool.initConvolver(cc, block_size, abuffer, size)) {
                  debug(1, "new convolver failed to initialize convolver %u ", cc);
                }               
              }
            }
            success = 1;
          }
          debug(2,
                "convolution impulse response filter initialized from \"%s\" with %d channel%s and "
                "%d samples",
                filename, info.channels, info.channels == 1 ? "" : "s", size);
          sf_close(file);
          free((void*)abuffer);
        } else {
          debug(1, "failed to init convolvers because insufficient memory was available");
        }
        free((void*)buffer);
      } else {
        warn("failed to init convolvers because insufficient memory was available");
      }
    } else {
      warn("Convolution impulse response filter file \"%s\" can not be opened. Please check that "
           "it exists, is a valid sound file and has appropriate access permissions.",
           filename);
    }
  }
  return success;
}

void convolver_process(unsigned int channel, float *data, int length) {
  pool.processAsync(channel, data, data, length);
}

void convolver_wait_for_all() { pool.waitForAll(); }

void convolver_clear_state() {
  pool.clearAllStates();
}

const unsigned int max_channels = 8;
fftconvolver::FFTConvolver convolvers[max_channels];

// fftconvolver::FFTConvolver convolver_l;
// fftconvolver::FFTConvolver convolver_r;

// always lock use this when accessing the playing conn value
/*

pthread_mutex_t convolver_lock = PTHREAD_MUTEX_INITIALIZER;

int convolver_init(const char *filename, unsigned char channel_count, double max_length_in_seconds,
                   size_t block_size) {
  debug(1, "convolver_init");
  int success = 0;
  SF_INFO info;
  if (filename) {
    SNDFILE *file = sf_open(filename, SFM_READ, &info);
    if (file) {
      size_t max_length = (size_t)(max_length_in_seconds * info.samplerate);
      const size_t size =
          (unsigned int)info.frames > max_length ? max_length : (unsigned int)info.frames;
      float buffer[size * info.channels];

      size_t l = sf_readf_float(file, buffer, size);
      if (l != 0) {
        pthread_mutex_lock(&convolver_lock);

        unsigned int cc;
        for (cc = 0; cc < channel_count; cc++) {
          convolvers[cc].reset();
        }

        if (info.channels == 1) {
          for (cc = 0; cc < channel_count; cc++) {
            convolvers[cc].init(block_size, buffer, size);
          }
        } else if (info.channels == channel_count) {
          // we have to deinterleave the ir file channels for each convolver
          for (cc = 0; cc < channel_count; cc++) {
            float abuffer[size];
            unsigned int i;
            for (i = 0; i < size; ++i) {
              abuffer[i] = buffer[channel_count * i + cc];
            }
            convolvers[cc].init(block_size, abuffer, size);
          }
        }
        pthread_mutex_unlock(&convolver_lock);
        success = 1;
      }
      debug(2,
            "convolution impulse response filter initialized from \"%s\" with %d channel%s and "
            "%d samples",
            filename, info.channels, info.channels == 1 ? "" : "s", size);
      sf_close(file);
    } else {
      warn("Convolution impulse response filter file \"%s\" can not be opened. Please check that "
           "it exists, is a valid sound file and has appropriate access permissions.",
           filename);
    }
  }
  return success;
}
void convolver_reset() {
  debug(1, "convolver_reset");
  pthread_mutex_lock(&convolver_lock);
  unsigned int cc;
  for (cc = 0; cc < max_channels; cc++) {
    convolvers[cc].reset();
  }
  // convolver_l.reset(); // it is possible that init could be called more than once
  // convolver_r.reset(); // so it could be necessary to remove all previous settings
  pthread_mutex_unlock(&convolver_lock);
}

void convolver_clear_state() {
  debug(1, "convolver_clear_state");
  pthread_mutex_lock(&convolver_lock);
  unsigned int cc;
  for (cc = 0; cc < max_channels; cc++) {
    convolvers[cc].clearState();
  }
  // convolver_l.reset(); // it is possible that init could be called more than once
  // convolver_r.reset(); // so it could be necessary to remove all previous settings
  pthread_mutex_unlock(&convolver_lock);
}

void convolver_process(unsigned int channel, float *data, int length) {
  pthread_mutex_lock(&convolver_lock);
  convolvers[channel].process(data, data, length);
  pthread_mutex_unlock(&convolver_lock);
  usleep(100);
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
*/