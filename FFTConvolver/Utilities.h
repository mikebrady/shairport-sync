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

#ifndef _FFTCONVOLVER_UTILITIES_H
#define _FFTCONVOLVER_UTILITIES_H

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstring>
#include <new>


namespace fftconvolver
{

#if defined(__SSE__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
  #if !defined(FFTCONVOLVER_USE_SSE) && !defined(FFTCONVOLVER_DONT_USE_SSE)
    #define FFTCONVOLVER_USE_SSE
  #endif
#endif


#if defined (FFTCONVOLVER_USE_SSE)
  #include <xmmintrin.h>
#endif


#if defined(__GNUC__)
  #define FFTCONVOLVER_RESTRICT __restrict__
#else
  #define FFTCONVOLVER_RESTRICT
#endif


/**
* @brief Returns whether SSE optimization for the convolver is enabled
* @return true: Enabled - false: Disabled
*/
bool SSEEnabled();


/**
* @class Buffer
* @brief Simple buffer implementation (uses 16-byte alignment if SSE optimization is enabled)
*/
template<typename T>
class Buffer
{
public:  
  explicit Buffer(size_t initialSize = 0) :
    _data(0),
    _size(0)
  {
    resize(initialSize);
  }

  virtual ~Buffer()
  {
    clear();
  }

  void clear()
  {
    deallocate(_data);
    _data = 0;
    _size = 0;
  }

  void resize(size_t size)
  {
    if (_size != size)
    {
      clear();

      if (size > 0)
      {
        assert(!_data && _size == 0);
        _data = allocate(size);
        _size = size;
      }
    }
    setZero();
  }

  size_t size() const
  {
    return _size;
  }

  void setZero()
  {
    ::memset(_data, 0, _size * sizeof(T));
  }

  void copyFrom(const Buffer<T>& other)
  {
    assert(_size == other._size);
    if (this != &other)
    {
      ::memcpy(_data, other._data, _size * sizeof(T));
    }
  }

  T& operator[](size_t index)
  {
    assert(_data && index < _size);
    return _data[index];
  }

  const T& operator[](size_t index) const
  {
    assert(_data && index < _size);
    return _data[index];
  }

  operator bool() const
  {
    return (_data != 0 && _size > 0);
  }

  T* data()
  {
    return _data;
  }

  const T* data() const
  {
    return _data;
  }

  static void Swap(Buffer<T>& a, Buffer<T>& b)
  {
    std::swap(a._data, b._data);
    std::swap(a._size, b._size);
  }

private:
  T* allocate(size_t size)
  {
#if defined(FFTCONVOLVER_USE_SSE)
    return static_cast<T*>(_mm_malloc(size * sizeof(T), 16));
#else
    return new T[size];
#endif
  }
  
  void deallocate(T* ptr)
  {
#if defined(FFTCONVOLVER_USE_SSE)
    _mm_free(ptr);
#else
    delete [] ptr;
#endif
  }

  T* _data;
  size_t _size;

  // Prevent uncontrolled usage
  Buffer(const Buffer&);
  Buffer& operator=(const Buffer&);
};


/**
* @brief Type of one sample
*/
typedef float Sample;


/**
* @brief Buffer for samples
*/
typedef Buffer<Sample> SampleBuffer;


/**
* @class SplitComplex
* @brief Buffer for split-complex representation of FFT results
*
* The split-complex representation stores the real and imaginary parts
* of FFT results in two different memory buffers which is useful e.g. for
* SIMD optimizations.
*/
class SplitComplex
{
public:
  explicit SplitComplex(size_t initialSize = 0) : 
    _size(0),
    _re(),
    _im()
  {
    resize(initialSize);
  }

  ~SplitComplex()
  {
    clear();
  }

  void clear()
  {
    _re.clear();
    _im.clear();
    _size = 0;
  }

  void resize(size_t newSize)
  {
    _re.resize(newSize);
    _im.resize(newSize);
    _size = newSize;
  }

  void setZero()
  {
    _re.setZero();
    _im.setZero();
  }

  void copyFrom(const SplitComplex& other)
  {
    _re.copyFrom(other._re);
    _im.copyFrom(other._im);
  }

  Sample* re()
  {
    return _re.data();
  }

  const Sample* re() const
  {
    return _re.data();
  }

  Sample* im()
  {
    return _im.data();
  }

  const Sample* im() const
  {
    return _im.data();
  }

  size_t size() const
  {
    return _size;
  }

private:
  size_t _size;
  SampleBuffer _re;
  SampleBuffer _im;

  // Prevent uncontrolled usage
  SplitComplex(const SplitComplex&);
  SplitComplex& operator=(const SplitComplex&);
};


/**
* @brief Returns the next power of 2 of a given number
* @param val The number
* @return The next power of 2
*/
template<typename T>
T NextPowerOf2(const T& val)
{
  T nextPowerOf2 = 1;
  while (nextPowerOf2 < val)
  {
    nextPowerOf2 *= 2;
  }
  return nextPowerOf2;
}
  

/**
* @brief Sums two given sample arrays
* @param result The result array
* @param a The 1st array
* @param b The 2nd array
* @param len The length of the arrays
*/
void Sum(Sample* FFTCONVOLVER_RESTRICT result,
         const Sample* FFTCONVOLVER_RESTRICT a,
         const Sample* FFTCONVOLVER_RESTRICT b,
         size_t len);


/**
* @brief Copies a source array into a destination buffer and pads the destination buffer with zeros
* @param dest The destination buffer
* @param src The source array
* @param srcSize The size of the source array
*/
template<typename T>
void CopyAndPad(Buffer<T>& dest, const T* src, size_t srcSize)
{
  assert(dest.size() >= srcSize);
  ::memcpy(dest.data(), src, srcSize * sizeof(T));
  ::memset(dest.data() + srcSize, 0, (dest.size()-srcSize) * sizeof(T)); 
}


/**
* @brief Adds the complex product of two split-complex buffers to a result buffer
* @param result The result buffer
* @param a The 1st factor of the complex product
* @param b The 2nd factor of the complex product
*/
void ComplexMultiplyAccumulate(SplitComplex& result, const SplitComplex& a, const SplitComplex& b);


/**
* @brief Adds the complex product of two split-complex arrays to a result array
* @param re The real part of the result buffer
* @param im The imaginary part of the result buffer
* @param reA The real part of the 1st factor of the complex product
* @param imA The imaginary part of the 1st factor of the complex product
* @param reB The real part of the 2nd factor of the complex product
* @param imB The imaginary part of the 2nd factor of the complex product
*/
void ComplexMultiplyAccumulate(Sample* FFTCONVOLVER_RESTRICT re, 
                               Sample* FFTCONVOLVER_RESTRICT im,
                               const Sample* FFTCONVOLVER_RESTRICT reA,
                               const Sample* FFTCONVOLVER_RESTRICT imA,
                               const Sample* FFTCONVOLVER_RESTRICT reB,
                               const Sample* FFTCONVOLVER_RESTRICT imB,
                               const size_t len);
  
} // End of namespace fftconvolver

#endif // Header guard
