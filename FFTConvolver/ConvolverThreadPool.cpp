/*
 * Convolver Thread Pool. This file is part of Shairport Sync
 * Copyright (c) Mike Brady 2026
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "ConvolverThreadPool.h"
#include "FFTConvolver.h"
#include "config.h"

extern "C" void _debug(const char *filename, const int linenumber, int level, const char *format,
                       ...);
#define debug(...) _debug(__FILE__, __LINE__, __VA_ARGS__)

ConvolverThreadPool::ConvolverThreadPool()
    : _convolvers(), _threads(), _taskQueue(), _queueMutex(), _condition(), _completionCV(),
      _stop(false), _activeTasks(0) {}

ConvolverThreadPool::~ConvolverThreadPool() {
  shutdown();

  // Delete all convolver instances
  for (size_t i = 0; i < _convolvers.size(); ++i) {
    delete _convolvers[i];
  }
  _convolvers.clear();
}

bool ConvolverThreadPool::init(size_t numThreads, size_t numConvolvers) {
  // Clean up any existing threads first
  shutdown();

  // Validate parameters
  if (numThreads == 0 || numConvolvers == 0) {
    return false;
  }

  // Delete any existing convolvers
  for (size_t i = 0; i < _convolvers.size(); ++i) {
    delete _convolvers[i];
  }
  _convolvers.clear();

  // Reset state
  _stop = false;
  _activeTasks = 0;

  // Create convolver instances (but don't initialize them yet)
  _convolvers.resize(numConvolvers);
  for (size_t i = 0; i < numConvolvers; ++i) {
    _convolvers[i] = new FFTConvolver();
  }

  // Create worker threads
  _threads.reserve(numThreads);
  for (size_t i = 0; i < numThreads; i++) {
#ifndef COMPILE_FOR_OSX
    pthread_setname_np(pthread_self(), "convolver");
#endif
    _threads.emplace_back([this]() { workerThread(); });
  }

  return true;
}

// Mutex to protect FFTW plan creation (required)
std::mutex fftwMutex;

bool ConvolverThreadPool::initConvolver(size_t convolverId, size_t blockSize, const Sample *ir,
                                        size_t irLen) {
  if (convolverId >= _convolvers.size()) {
    return false;
  }

  // Make sure no tasks are using this convolver
  waitForAll();
  std::lock_guard<std::mutex> lock(fftwMutex);
  return _convolvers[convolverId]->init(blockSize, ir, irLen);
}

bool ConvolverThreadPool::initAllConvolvers(size_t blockSize, const Sample *ir, size_t irLen) {
  // Make sure no tasks are running
  waitForAll();

  for (size_t i = 0; i < _convolvers.size(); ++i) {
    if (!_convolvers[i]->init(blockSize, ir, irLen)) {
      return false;
    }
  }

  return true;
}

void ConvolverThreadPool::processAsync(size_t convolverId, const Sample *input, Sample *output,
                                       size_t len) {
  assert(convolverId < _convolvers.size());

  {
    std::lock_guard<std::mutex> lock(_queueMutex);

    // Create the task
    _taskQueue.push([this, convolverId, input, output, len]() {
      _convolvers[convolverId]->process(input, output, len);
    });

    ++_activeTasks;
  }

  // Wake up one worker thread
  _condition.notify_one();
}

void ConvolverThreadPool::waitForAll() {
  std::unique_lock<std::mutex> lock(_queueMutex);
  _completionCV.wait(lock, [this]() { return _taskQueue.empty() && _activeTasks == 0; });
}

void ConvolverThreadPool::clearState(size_t convolverId) {
  // Do the replacement assertion check first, and then wait for all tasks to stop
  if (convolverId < _convolvers.size()) {
    waitForAll();
  } else {
    debug(1, "assert(convolverId < _convolvers.size()) failed, with convolverId: %u and _convolvers.size(): %u.", convolverId, _convolvers.size());
  } 
}

/* this is the old version
void ConvolverThreadPool::clearState(size_t convolverId) {
  assert(convolverId < _convolvers.size());

  // Make sure no tasks are running before clearing state
  waitForAll();

  // _convolvers[convolverId]->clearState();
}
*/

void ConvolverThreadPool::clearAllStates() {
  // Make sure no tasks are running before clearing state
  waitForAll();

  for (size_t i = 0; i < _convolvers.size(); ++i) {
    _convolvers[i]->clearState();
  }
}

void ConvolverThreadPool::workerThread() {
  while (true) {
    std::function<void()> task;

    // Wait for a task or stop signal
    {
      std::unique_lock<std::mutex> lock(_queueMutex);
      _condition.wait(lock, [this]() { return _stop || !_taskQueue.empty(); });

      // Exit if stopping and no more tasks
      if (_stop && _taskQueue.empty()) {
        return;
      }

      // Get the next task
      task = std::move(_taskQueue.front());
      _taskQueue.pop();
    }

    // Execute the task (outside the lock for better parallelism)
    task();

    // Mark task as complete
    {
      std::lock_guard<std::mutex> lock(_queueMutex);
      --_activeTasks;
      _completionCV.notify_all();
    }
  }
}

void ConvolverThreadPool::shutdown() {
  // Signal threads to stop
  {
    std::lock_guard<std::mutex> lock(_queueMutex);
    _stop = true;
  }
  _condition.notify_all();

  // Wait for all threads to finish
  for (auto &thread : _threads) {
    if (thread.joinable()) {
      thread.join();
    }
  }

  // Clear thread vector
  _threads.clear();

  // Clear remaining tasks - properly this time
  {
    std::lock_guard<std::mutex> lock(_queueMutex);
    while (!_taskQueue.empty()) {
      _taskQueue.pop();
    }
  }

  // Reset state
  _activeTasks = 0;
  _stop = false;
}