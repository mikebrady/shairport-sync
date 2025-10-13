#ifndef CONVOLVER_THREAD_POOL_H
#define CONVOLVER_THREAD_POOL_H

#include "FFTConvolver.h"
#include <cassert>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

// Use the fftconvolver namespace
using fftconvolver::FFTConvolver;
using fftconvolver::Sample;

class ConvolverThreadPool {
private:
  std::vector<FFTConvolver *> _convolvers;
  std::vector<std::thread> _threads;
  std::queue<std::function<void()>> _taskQueue;
  std::mutex _queueMutex;
  std::condition_variable _condition;
  std::condition_variable _completionCV;
  bool _stop;
  size_t _activeTasks;

public:
  ConvolverThreadPool();
  ~ConvolverThreadPool();

  // Initialize the thread pool (creates convolvers but doesn't initialize them)
  // numThreads: number of worker threads (level of parallelism)
  // numConvolvers: total number of convolver instances
  bool init(size_t numThreads, size_t numConvolvers);

  // Initialize a specific convolver with its IR
  // convolverId: which convolver to initialize
  // blockSize: block size for convolution
  // ir: impulse response data
  // irLen: length of impulse response
  bool initConvolver(size_t convolverId, size_t blockSize, const Sample *ir, size_t irLen);

  // Initialize all convolvers with the same IR
  bool initAllConvolvers(size_t blockSize, const Sample *ir, size_t irLen);

  // Queue a convolution task (non-blocking)
  void processAsync(size_t convolverId, const Sample *input, Sample *output, size_t len);

  // Wait for all queued tasks to complete (blocking)
  void waitForAll();

  // Clear the state of a specific convolver
  void clearState(size_t convolverId);

  // Clear the state of all convolvers
  void clearAllStates();

  // Get the number of convolvers
  size_t getNumConvolvers() const { return _convolvers.size(); }

  // Get the number of threads
  size_t getNumThreads() const { return _threads.size(); }
  
  void shutdown();

private:
  void workerThread();
  // void shutdown();
};

#endif // CONVOLVER_THREAD_POOL_H