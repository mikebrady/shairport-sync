// ==================================================================================
// Copyright (c) 2012 HiFi-LoFi
//
// This is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
// ==================================================================================

#include "FFTConvolver.h"

#include <cassert>
#include <cmath>

#if defined (FFTCONVOLVER_USE_SSE)
  #include <xmmintrin.h>
#endif


namespace fftconvolver
{  

FFTConvolver::FFTConvolver() :
  _blockSize(0),
  _segSize(0),
  _segCount(0),
  _fftComplexSize(0),
  _segments(),
  _segmentsIR(),
  _fftBuffer(),
  _fft(),
  _preMultiplied(),
  _conv(),
  _overlap(),
  _current(0),
  _inputBuffer(),
  _inputBufferFill(0)
{
}

  
FFTConvolver::~FFTConvolver()
{
  reset();
}

  
void FFTConvolver::reset()
{  
  for (size_t i=0; i<_segCount; ++i)
  {
    delete _segments[i];
    delete _segmentsIR[i];
  }
  
  _blockSize = 0;
  _segSize = 0;
  _segCount = 0;
  _fftComplexSize = 0;
  _segments.clear();
  _segmentsIR.clear();
  _fftBuffer.clear();
  _fft.init(0);
  _preMultiplied.clear();
  _conv.clear();
  _overlap.clear();
  _current = 0;
  _inputBuffer.clear();
  _inputBufferFill = 0;
}

void FFTConvolver::clearState()
{
  if (_segCount == 0)
  {
    return; // Not initialized
  }
  
  _inputBuffer.setZero();
  _inputBufferFill = 0;
  _overlap.setZero();
  
  for (size_t i = 0; i < _segCount; ++i)
  {
    _segments[i]->setZero();
  }
  
  _preMultiplied.setZero();
  _conv.setZero();
  _current = 0;
}
  
bool FFTConvolver::init(size_t blockSize, const Sample* ir, size_t irLen)
{
  reset();

  if (blockSize == 0)
  {
    return false;
  }
  
  // Ignore zeros at the end of the impulse response because they only waste computation time
  while (irLen > 0 && ::fabs(ir[irLen-1]) < 0.000001f)
  {
    --irLen;
  }

  if (irLen == 0)
  {
    return true;
  }
  
  _blockSize = NextPowerOf2(blockSize);
  _segSize = 2 * _blockSize;
  _segCount = static_cast<size_t>(::ceil(static_cast<float>(irLen) / static_cast<float>(_blockSize)));
  _fftComplexSize = audiofft::AudioFFT::ComplexSize(_segSize);
  
  // FFT
  _fft.init(_segSize);
  _fftBuffer.resize(_segSize);
  
  // Prepare segments
  for (size_t i=0; i<_segCount; ++i)
  {
    _segments.push_back(new SplitComplex(_fftComplexSize));    
  }
  
  // Prepare IR
  for (size_t i=0; i<_segCount; ++i)
  {
    SplitComplex* segment = new SplitComplex(_fftComplexSize);
    const size_t remaining = irLen - (i * _blockSize);
    const size_t sizeCopy = (remaining >= _blockSize) ? _blockSize : remaining;
    CopyAndPad(_fftBuffer, &ir[i*_blockSize], sizeCopy);
    _fft.fft(_fftBuffer.data(), segment->re(), segment->im());
    _segmentsIR.push_back(segment);
  }
  
  // Prepare convolution buffers  
  _preMultiplied.resize(_fftComplexSize);
  _conv.resize(_fftComplexSize);
  _overlap.resize(_blockSize);
  
  // Prepare input buffer
  _inputBuffer.resize(_blockSize);
  _inputBufferFill = 0;

  // Reset current position
  _current = 0;
  
  return true;
}


void FFTConvolver::process(const Sample* input, Sample* output, size_t len)
{
  if (_segCount == 0)
  {
    ::memset(output, 0, len * sizeof(Sample));
    return;
  }

  size_t processed = 0;
  while (processed < len)
  {
    const bool inputBufferWasEmpty = (_inputBufferFill == 0);
    const size_t processing = std::min(len-processed, _blockSize-_inputBufferFill);
    const size_t inputBufferPos = _inputBufferFill;
    ::memcpy(_inputBuffer.data()+inputBufferPos, input+processed, processing * sizeof(Sample));

    // Forward FFT
    CopyAndPad(_fftBuffer, &_inputBuffer[0], _blockSize); 
    _fft.fft(_fftBuffer.data(), _segments[_current]->re(), _segments[_current]->im());

    // Complex multiplication
    if (inputBufferWasEmpty)
    {
      _preMultiplied.setZero();
      for (size_t i=1; i<_segCount; ++i)
      {
        const size_t indexIr = i;
        const size_t indexAudio = (_current + i) % _segCount;
        ComplexMultiplyAccumulate(_preMultiplied, *_segmentsIR[indexIr], *_segments[indexAudio]);
      }
    }
    _conv.copyFrom(_preMultiplied);
    ComplexMultiplyAccumulate(_conv, *_segments[_current], *_segmentsIR[0]);

    // Backward FFT
    _fft.ifft(_fftBuffer.data(), _conv.re(), _conv.im());

    // Add overlap
    Sum(output+processed, _fftBuffer.data()+inputBufferPos, _overlap.data()+inputBufferPos, processing);

    // Input buffer full => Next block
    _inputBufferFill += processing;
    if (_inputBufferFill == _blockSize)
    {
      // Input buffer is empty again now
      _inputBuffer.setZero();
      _inputBufferFill = 0;

      // Save the overlap
      ::memcpy(_overlap.data(), _fftBuffer.data()+_blockSize, _blockSize * sizeof(Sample));

      // Update current segment
      _current = (_current > 0) ? (_current - 1) : (_segCount - 1);
    }

    processed += processing;
  }
}
  
} // End of namespace fftconvolver
