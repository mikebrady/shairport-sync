// ==================================================================================
// Copyright (c) 2017 HiFi-LoFi
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is furnished
// to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
// FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
// COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
// IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// ==================================================================================

#ifndef _FFTCONVOLVER_FFTCONVOLVER_H
#define _FFTCONVOLVER_FFTCONVOLVER_H

#include "AudioFFT.h"
#include "Utilities.h"

#include <vector>


namespace fftconvolver
{ 

/**
* @class FFTConvolver
* @brief Implementation of a partitioned FFT convolution algorithm with uniform block size
*
* Some notes on how to use it:
*
* - After initialization with an impulse response, subsequent data portions of
*   arbitrary length can be convolved. The convolver internally can handle
*   this by using appropriate buffering.
*
* - The convolver works without "latency" (except for the required
*   processing time, of course), i.e. the output always is the convolved
*   input for each processing call.
*
* - The convolver is suitable for real-time processing which means that no
*   "unpredictable" operations like allocations, locking, API calls, etc. are
*   performed during processing (all necessary allocations and preparations take
*   place during initialization).
*/
class FFTConvolver
{  
public:
  FFTConvolver();  
  virtual ~FFTConvolver();
  
  /**
  * @brief Initializes the convolver
  * @param blockSize Block size internally used by the convolver (partition size)
  * @param ir The impulse response
  * @param irLen Length of the impulse response
  * @return true: Success - false: Failed
  */
  bool init(size_t blockSize, const Sample* ir, size_t irLen);

  /**
  * @brief Convolves the the given input samples and immediately outputs the result
  * @param input The input samples
  * @param output The convolution result
  * @param len Number of input/output samples
  */
  void process(const Sample* input, Sample* output, size_t len);

  /**
  * @brief Resets the convolver and discards the set impulse response
  */
  void reset();
  
  /**
  * @brief Clears audio history
  */
  
  void clearState();

private:
  size_t _blockSize;
  size_t _segSize;
  size_t _segCount;
  size_t _fftComplexSize;
  std::vector<SplitComplex*> _segments;
  std::vector<SplitComplex*> _segmentsIR;
  SampleBuffer _fftBuffer;
  audiofft::AudioFFT _fft;
  SplitComplex _preMultiplied;
  SplitComplex _conv;
  SampleBuffer _overlap;
  size_t _current;
  SampleBuffer _inputBuffer;
  size_t _inputBufferFill;

  // Prevent uncontrolled usage
  FFTConvolver(const FFTConvolver&);
  FFTConvolver& operator=(const FFTConvolver&);
};
  
} // End of namespace fftconvolver

#endif // Header guard
