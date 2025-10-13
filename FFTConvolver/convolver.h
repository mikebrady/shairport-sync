// C wrapper to C++ FFTConvolver
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

  #include <stddef.h>
  
// int convolver_init(const char* file, unsigned char channel_count, double max_length_in_seconds, size_t block_size);
void convolver_reset();
//void convolver_clear_state();
// void convolver_process(unsigned int channel, float *data, int length);
// void convolver_process_l(float* data, int length);
// void convolver_process_r(float* data, int length);

void convolver_pool_init(size_t numThreads, size_t numConvolvers);
void convolver_pool_closedown();
int convolver_init(const char* file, unsigned char channel_count, double max_length_in_seconds, size_t block_size);
void convolver_process(unsigned int channel, float *data, int length);
void convolver_clear_state();
void convolver_wait_for_all();
  
#ifdef __cplusplus
}
#endif
