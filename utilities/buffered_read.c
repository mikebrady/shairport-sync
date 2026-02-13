/*
 * Buffered Read. This file is part of Shairport Sync
 * Copyright (c) Mike Brady 2025

 * Modifications, including those associated with audio synchronization, multithreading and
 * metadata handling copyright (c) Mike Brady 2014--2025
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
 
 #include <sys/types.h>
 #include "buffered_read.h"
 #include "common.h"

ssize_t buffered_read(buffered_tcp_desc *descriptor, void *buf, size_t count,
                      size_t *bytes_remaining) {
  ssize_t response = -1;
  if (debug_mutex_lock(&descriptor->mutex, 50000, 1) != 0)
    debug(1, "problem with mutex");
  pthread_cleanup_push(mutex_unlock, (void *)&descriptor->mutex);
  // wipe the slate dlean before reading...
  descriptor->error_code = 0;
  descriptor->closed = 0;

  if (descriptor->buffer_occupancy == 0) {
    debug(2, "buffered_read: buffer empty -- waiting for %zu bytes.", count);
  }

  while ((descriptor->buffer_occupancy == 0) && (descriptor->error_code == 0) &&
         (descriptor->closed == 0)) {
    if (pthread_cond_wait(&descriptor->not_empty_cv, &descriptor->mutex))
      debug(1, "Error waiting for buffered read");
    else
      debug(2, "buffered_read: signalled with %zu bytes after waiting.",
            descriptor->buffer_occupancy);
  }

  if (descriptor->error_code) {
    errno = descriptor->error_code;
    debug(1, "buffered_read: error %d.", errno);
    response = -1;
  } else if (descriptor->closed != 0) {
    debug(2, "buffered_read: connection closed.");
    errno = 0; // no error -- just closed
    response = 0;
  } else if (descriptor->buffer_occupancy != 0) {
    ssize_t bytes_to_move = count;

    if (descriptor->buffer_occupancy < count) {
      bytes_to_move = descriptor->buffer_occupancy;
    }

    ssize_t top_gap = descriptor->buffer + descriptor->buffer_max_size - descriptor->toq;
    if (top_gap < bytes_to_move)
      bytes_to_move = top_gap;

    memcpy(buf, descriptor->toq, bytes_to_move);
    descriptor->toq += bytes_to_move;
    if (descriptor->toq == descriptor->buffer + descriptor->buffer_max_size)
      descriptor->toq = descriptor->buffer;
    descriptor->buffer_occupancy -= bytes_to_move;
    if (bytes_remaining != NULL)
      *bytes_remaining = descriptor->buffer_occupancy;
    response = bytes_to_move;
    if (pthread_cond_signal(&descriptor->not_full_cv))
      debug(1, "Error signalling");
  }

  pthread_cleanup_pop(1); // release the mutex
  return response;
}

#define STANDARD_PACKET_SIZE 4096

void buffered_tcp_reader_cleanup_handler(__attribute__((unused)) void *arg) {
  debug(2, "Buffered TCP Reader Thread Exit via Cleanup.");
}

void *buffered_tcp_reader(void *arg) {
  //  #include <syscall.h>
  //  debug(1, "buffered_tcp_reader PID %d", syscall(SYS_gettid));
  pthread_cleanup_push(buffered_tcp_reader_cleanup_handler, NULL);
  buffered_tcp_desc *descriptor = (buffered_tcp_desc *)arg;

  // listen(descriptor->sock_fd, 5); // this is done in the handle_setup_2 code to ensure it's open
  // when the client hears about it...
  ssize_t nread;
  SOCKADDR remote_addr;
  memset(&remote_addr, 0, sizeof(remote_addr));
  socklen_t addr_size = sizeof(remote_addr);
  int finished = 0;
  int fd = accept(descriptor->sock_fd, (struct sockaddr *)&remote_addr, &addr_size);
  // debug(1, "buffered_tcp_reader: the client has opened a buffered audio link.");
  intptr_t pfd = fd;
  pthread_cleanup_push(socket_cleanup, (void *)pfd);

  do {
    int have_time_to_sleep = 0;
    if (debug_mutex_lock(&descriptor->mutex, 500000, 1) != 0)
      debug(1, "problem with mutex");
    pthread_cleanup_push(mutex_unlock, (void *)&descriptor->mutex);
    while (descriptor->buffer_occupancy == descriptor->buffer_max_size) {
      if (pthread_cond_wait(&descriptor->not_full_cv, &descriptor->mutex))
        debug(1, "Error waiting for not_full_cv");
    }
    pthread_cleanup_pop(1); // release the mutex

    // now we know it is not full, so go ahead and try to read some more into it

    // wrap
    if ((size_t)(descriptor->eoq - descriptor->buffer) == descriptor->buffer_max_size)
      descriptor->eoq = descriptor->buffer;

    // figure out how much to ask for
    size_t bytes_to_request = STANDARD_PACKET_SIZE;
    size_t free_space = descriptor->buffer_max_size - descriptor->buffer_occupancy;
    if (bytes_to_request > free_space)
      bytes_to_request = free_space; // don't ask for more than will fit

    size_t gap_to_end_of_buffer =
        descriptor->buffer + descriptor->buffer_max_size - descriptor->eoq;
    if (gap_to_end_of_buffer < bytes_to_request)
      bytes_to_request =
          gap_to_end_of_buffer; // only ask for what will fill to the top of the buffer

    // do the read
    if (descriptor->buffer_occupancy == 0)
      debug(2, "recv of up to %zd bytes with an buffer empty.", bytes_to_request);
    nread = recv(fd, descriptor->eoq, bytes_to_request, 0);
    // debug(1, "Received %d bytes for a buffer size of %d bytes.",nread,
    // descriptor->buffer_occupancy + nread);
    if (debug_mutex_lock(&descriptor->mutex, 50000, 1) != 0)
      debug(1, "problem with not empty mutex");
    pthread_cleanup_push(mutex_unlock, (void *)&descriptor->mutex);
    if (nread < 0) {
      char errorstring[1024];
      strerror_r(errno, (char *)errorstring, sizeof(errorstring));
      debug(1, "error in buffered_tcp_reader %d: \"%s\". Could not recv a packet.", errno,
            errorstring);
      descriptor->error_code = errno;
    } else if (nread == 0) {
      descriptor->closed = 1;
      debug(
          2,
          "buffered audio port closed by remote end. Terminating the buffered_tcp_reader thread.");
      finished = 1;
    } else if (nread > 0) {
      descriptor->eoq += nread;
      descriptor->buffer_occupancy += nread;
    }

    // signal if we got data or an error or the file closed
    if (pthread_cond_signal(&descriptor->not_empty_cv))
      debug(1, "Error signalling");
    if (descriptor->buffer_occupancy > 16384)
      have_time_to_sleep = 1;
    pthread_cleanup_pop(1); // release the mutex
    if (have_time_to_sleep)
      usleep(10000); // give other threads a chance to run...
  } while (finished == 0);

  debug(2, "Buffered TCP Reader Thread Exit \"Normal\" Exit Begin.");
  pthread_cleanup_pop(1); // close the socket
  pthread_cleanup_pop(1); // cleanup
  debug(2, "Buffered TCP Reader Thread Exit \"Normal\" Exit.");
  pthread_exit(NULL);
}

// this will read a block of the size specified to the buffer
// and will return either with the block or on error
ssize_t read_sized_block(buffered_tcp_desc *descriptor, void *buf, size_t count,
                          size_t *bytes_remaining) {
  ssize_t response, nread;
  size_t inbuf = 0; // bytes already in the buffer
  int keep_trying = 1;

  do {
    nread = buffered_read(descriptor, buf + inbuf, count - inbuf, bytes_remaining);
    if (nread == 0) {
      // a blocking read that returns zero means eof -- implies connection closed
      debug(2, "read_sized_block connection closed.");
      keep_trying = 0;
    } else if (nread < 0) {
      if (errno == EAGAIN) {
        debug(1, "read_sized_block getting Error 11 -- EAGAIN from a blocking read!");
      }
      if ((errno != EAGAIN) && (errno != EINTR)) {
        char errorstring[1024];
        strerror_r(errno, (char *)errorstring, sizeof(errorstring));
        debug(1, "read_sized_block read error %d: \"%s\".", errno, (char *)errorstring);
        keep_trying = 0;
      }
    } else {
      inbuf += (size_t)nread;
    }
  } while ((keep_trying != 0) && (inbuf < count));
  if (nread <= 0)
    response = nread;
  else
    response = inbuf;
  return response;
}
