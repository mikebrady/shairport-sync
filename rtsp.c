/*
 * RTSP protocol handler. This file is part of Shairport Sync
 * Copyright (c) James Laird 2013

 * Modifications associated with audio synchronization, mutithreading and
 * metadata handling copyright (c) Mike Brady 2014-2020
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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <memory.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "config.h"

#ifdef CONFIG_OPENSSL
#include <openssl/md5.h>
#endif

#ifdef CONFIG_MBEDTLS
#include <mbedtls/md5.h>
#include <mbedtls/version.h>
#endif

#ifdef CONFIG_POLARSSL
#include <polarssl/md5.h>
#endif

#include "common.h"
#include "player.h"
#include "rtp.h"
#include "rtsp.h"

#ifdef CONFIG_METADATA_HUB
#include "metadata_hub.h"
#endif

#ifdef CONFIG_MQTT
#include "mqtt.h"
#endif

#ifdef AF_INET6
#define INETx_ADDRSTRLEN INET6_ADDRSTRLEN
#else
#define INETx_ADDRSTRLEN INET_ADDRSTRLEN
#endif

#define METADATA_SNDBUF (4 * 1024 * 1024)

enum rtsp_read_request_response {
  rtsp_read_request_response_ok,
  rtsp_read_request_response_immediate_shutdown_requested,
  rtsp_read_request_response_bad_packet,
  rtsp_read_request_response_channel_closed,
  rtsp_read_request_response_read_error,
  rtsp_read_request_response_error
};

rtsp_conn_info *playing_conn;
rtsp_conn_info **conns;

int metadata_running = 0;

// always lock use this when accessing the playing conn value
static pthread_mutex_t playing_conn_lock = PTHREAD_MUTEX_INITIALIZER;

// every time we want to retain or release a reference count, lock it with this
// if a reference count is read as zero, it means the it's being deallocated.
static pthread_mutex_t reference_counter_lock = PTHREAD_MUTEX_INITIALIZER;

// only one thread is allowed to use the player at once.
// it monitors the request variable (at least when interrupted)
// static pthread_mutex_t playing_mutex = PTHREAD_MUTEX_INITIALIZER;
// static int please_shutdown = 0;
// static pthread_t playing_thread = 0;

int RTSP_connection_index = 1;

#ifdef CONFIG_METADATA
typedef struct {
  pthread_mutex_t pc_queue_lock;
  pthread_cond_t pc_queue_item_added_signal;
  pthread_cond_t pc_queue_item_removed_signal;
  char *name;
  size_t item_size;  // number of bytes in each item
  uint32_t count;    // number of items in the queue
  uint32_t capacity; // maximum number of items
  uint32_t toq;      // first item to take
  uint32_t eoq;      // free space at end of queue
  void *items;       // a pointer to where the items are actually stored
} pc_queue;          // producer-consumer queue
#endif

static int msg_indexes = 1;

typedef struct {
  int index_number;
  uint32_t referenceCount; // we might start using this...
  unsigned int nheaders;
  char *name[16];
  char *value[16];

  int contentlength;
  char *content;

  // for requests
  char method[16];

  // for responses
  int respcode;
} rtsp_message;

#ifdef CONFIG_METADATA
typedef struct {
  uint32_t type;
  uint32_t code;
  char *data;
  uint32_t length;
  rtsp_message *carrier;
} metadata_package;

void pc_queue_init(pc_queue *the_queue, char *items, size_t item_size, uint32_t number_of_items,
                   const char *name) {
  if (name)
    debug(2, "Creating metadata queue \"%s\".", name);
  else
    debug(1, "Creating an unnamed metadata queue.");
  pthread_mutex_init(&the_queue->pc_queue_lock, NULL);
  pthread_cond_init(&the_queue->pc_queue_item_added_signal, NULL);
  pthread_cond_init(&the_queue->pc_queue_item_removed_signal, NULL);
  the_queue->item_size = item_size;
  the_queue->items = items;
  the_queue->count = 0;
  the_queue->capacity = number_of_items;
  the_queue->toq = 0;
  the_queue->eoq = 0;
  if (name == NULL)
    the_queue->name = NULL;
  else
    the_queue->name = strdup(name);
}

void pc_queue_delete(pc_queue *the_queue) {
  if (the_queue->name)
    debug(2, "Deleting metadata queue \"%s\".", the_queue->name);
  else
    debug(1, "Deleting an unnamed metadata queue.");
  if (the_queue->name != NULL)
    free(the_queue->name);
  // debug(2, "destroying pc_queue_item_removed_signal");
  pthread_cond_destroy(&the_queue->pc_queue_item_removed_signal);
  // debug(2, "destroying pc_queue_item_added_signal");
  pthread_cond_destroy(&the_queue->pc_queue_item_added_signal);
  // debug(2, "destroying pc_queue_lock");
  pthread_mutex_destroy(&the_queue->pc_queue_lock);
  // debug(2, "destroying signals and locks done");
}

int send_metadata(uint32_t type, uint32_t code, char *data, uint32_t length, rtsp_message *carrier,
                  int block);

int send_ssnc_metadata(uint32_t code, char *data, uint32_t length, int block) {
  return send_metadata('ssnc', code, data, length, NULL, block);
}

void pc_queue_cleanup_handler(void *arg) {
  // debug(1, "pc_queue_cleanup_handler called.");
  pc_queue *the_queue = (pc_queue *)arg;
  int rc = pthread_mutex_unlock(&the_queue->pc_queue_lock);
  if (rc)
    debug(1, "Error unlocking for pc_queue_add_item or pc_queue_get_item.");
}

int pc_queue_add_item(pc_queue *the_queue, const void *the_stuff, int block) {
  int response = 0;
  int rc;
  if (the_queue) {
    if (block == 0) {
      rc = debug_mutex_lock(&the_queue->pc_queue_lock, 10000, 2);
      if (rc == EBUSY)
        return EBUSY;
    } else
      rc = pthread_mutex_lock(&the_queue->pc_queue_lock);
    if (rc)
      debug(1, "Error locking for pc_queue_add_item");
    pthread_cleanup_push(pc_queue_cleanup_handler, (void *)the_queue);
    // leave this out if you want this to return if the queue is already full
    // irrespective of the block flag.
    /*
                while (the_queue->count == the_queue->capacity) {
                        rc = pthread_cond_wait(&the_queue->pc_queue_item_removed_signal,
       &the_queue->pc_queue_lock); if (rc) debug(1, "Error waiting for item to be removed");
                }
                */
    if (the_queue->count < the_queue->capacity) {
      uint32_t i = the_queue->eoq;
      void *p = the_queue->items + the_queue->item_size * i;
      //    void * p = &the_queue->qbase + the_queue->item_size*the_queue->eoq;
      memcpy(p, the_stuff, the_queue->item_size);

      // update the pointer
      i++;
      if (i == the_queue->capacity)
        // fold pointer if necessary
        i = 0;
      the_queue->eoq = i;
      the_queue->count++;
      // debug(2,"metadata queue+ \"%s\" %d/%d.", the_queue->name, the_queue->count,
      // the_queue->capacity);
      if (the_queue->count == the_queue->capacity)
        debug(3, "metadata queue \"%s\": is now full with %d items in it!", the_queue->name,
              the_queue->count);
      rc = pthread_cond_signal(&the_queue->pc_queue_item_added_signal);
      if (rc)
        debug(1, "metadata queue \"%s\": error signalling after pc_queue_add_item",
              the_queue->name);
    } else {
      response = EWOULDBLOCK; // a bit arbitrary, this.
      debug(3,
            "metadata queue \"%s\": is already full with %d items in it. Not adding this item to "
            "the queue.",
            the_queue->name, the_queue->count);
    }
    pthread_cleanup_pop(1); // unlock the queue lock.
  } else {
    debug(1, "Adding an item to a NULL queue");
  }
  return response;
}

int pc_queue_get_item(pc_queue *the_queue, void *the_stuff) {
  int rc;
  if (the_queue) {
    rc = pthread_mutex_lock(&the_queue->pc_queue_lock);
    if (rc)
      debug(1, "metadata queue \"%s\": error locking for pc_queue_get_item", the_queue->name);
    pthread_cleanup_push(pc_queue_cleanup_handler, (void *)the_queue);
    while (the_queue->count == 0) {
      rc = pthread_cond_wait(&the_queue->pc_queue_item_added_signal, &the_queue->pc_queue_lock);
      if (rc)
        debug(1, "metadata queue \"%s\": error waiting for item to be added", the_queue->name);
    }
    uint32_t i = the_queue->toq;
    //    void * p = &the_queue->qbase + the_queue->item_size*the_queue->toq;
    void *p = the_queue->items + the_queue->item_size * i;
    memcpy(the_stuff, p, the_queue->item_size);

    // update the pointer
    i++;
    if (i == the_queue->capacity)
      // fold pointer if necessary
      i = 0;
    the_queue->toq = i;
    the_queue->count--;
    debug(3, "metadata queue- \"%s\" %d/%d.", the_queue->name, the_queue->count,
          the_queue->capacity);
    rc = pthread_cond_signal(&the_queue->pc_queue_item_removed_signal);
    if (rc)
      debug(1, "metadata queue \"%s\": error signalling after pc_queue_get_item", the_queue->name);
    pthread_cleanup_pop(1); // unlock the queue lock.
  } else {
    debug(1, "Removing an item from a NULL queue");
  }
  return 0;
}

#endif

int have_player(rtsp_conn_info *conn) {
  int response = 0;
  debug_mutex_lock(&playing_conn_lock, 1000000, 3);
  if (playing_conn == conn) // this connection definitely has the play lock
    response = 1;
  debug_mutex_unlock(&playing_conn_lock, 3);
  return response;
}

void player_watchdog_thread_cleanup_handler(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  debug(3, "Connection %d: Watchdog Exit.", conn->connection_number);
}

void *player_watchdog_thread_code(void *arg) {
  pthread_cleanup_push(player_watchdog_thread_cleanup_handler, arg);
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  do {
    usleep(2000000); // check every two seconds
    // debug(3, "Connection %d: Check the thread is doing something...", conn->connection_number);
    if ((config.dont_check_timeout == 0) && (config.timeout != 0)) {
      debug_mutex_lock(&conn->watchdog_mutex, 1000, 0);
      uint64_t last_watchdog_bark_time = conn->watchdog_bark_time;
      debug_mutex_unlock(&conn->watchdog_mutex, 0);
      if (last_watchdog_bark_time != 0) {
        uint64_t time_since_last_bark =
            (get_absolute_time_in_ns() - last_watchdog_bark_time) / 1000000000;
        uint64_t ct = config.timeout; // go from int to 64-bit int

        if (time_since_last_bark >= ct) {
          conn->watchdog_barks++;
          if (conn->watchdog_barks == 1) {
            // debuglev = 3; // tell us everything.
            debug(1,
                  "Connection %d: As Yeats almost said, \"Too long a silence / can make a stone "
                  "of the heart\".",
                  conn->connection_number);
            conn->stop = 1;
            pthread_cancel(conn->thread);
          } else if (conn->watchdog_barks == 3) {
            if ((config.cmd_unfixable) && (conn->unfixable_error_reported == 0)) {
              conn->unfixable_error_reported = 1;
              command_execute(config.cmd_unfixable, "unable_to_cancel_play_session", 1);
            } else {
              die("an unrecoverable error, \"unable_to_cancel_play_session\", has been detected.",
                   conn->connection_number);
            }
          }
        }
      }
    }
  } while (1);
  pthread_cleanup_pop(0); // should never happen
  pthread_exit(NULL);
}

void ask_other_rtsp_conversation_threads_to_stop(pthread_t except_this_thread);

void rtsp_request_shutdown_stream(void) {
  debug(1, "Request to shut down all rtsp conversation threads");
  ask_other_rtsp_conversation_threads_to_stop(0); // i.e. ask all playing threads to stop
}

// keep track of the threads we have spawned so we can join() them
static int nconns = 0;
static void track_thread(rtsp_conn_info *conn) {
  conns = realloc(conns, sizeof(rtsp_conn_info *) * (nconns + 1));
  if (conns) {
    conns[nconns] = conn;
    nconns++;
  } else {
    die("could not reallocate memnory for \"conns\" in rtsp.c.");
  }
}

void cancel_all_RTSP_threads(void) {
  int i;
  for (i = 0; i < nconns; i++) {
    debug(2, "Connection %d: cancelling.", conns[i]->connection_number);
    pthread_cancel(conns[i]->thread);
  }
  for (i = 0; i < nconns; i++) {
    debug(2, "Connection %d: joining.", conns[i]->connection_number);
    pthread_join(conns[i]->thread, NULL);
    free(conns[i]);
  }
}

void cleanup_threads(void) {
  void *retval;
  int i;
  // debug(2, "culling threads.");
  for (i = 0; i < nconns;) {
    if (conns[i]->running == 0) {
      debug(3, "found RTSP connection thread %d in a non-running state.",
            conns[i]->connection_number);
      pthread_join(conns[i]->thread, &retval);
      debug(3, "RTSP connection thread %d deleted...", conns[i]->connection_number);
      free(conns[i]);
      nconns--;
      if (nconns)
        conns[i] = conns[nconns];
    } else {
      i++;
    }
  }
}

// ask all rtsp_conversation threads to stop -- there should be at most one, but
// ya never know.

void ask_other_rtsp_conversation_threads_to_stop(pthread_t except_this_thread) {
  int i;
  debug(1, "asking playing threads to stop");
  for (i = 0; i < nconns; i++) {
    if (((except_this_thread == 0) || (pthread_equal(conns[i]->thread, except_this_thread) == 0)) &&
        (conns[i]->running != 0)) {
      pthread_cancel(conns[i]->thread);
      pthread_join(conns[i]->thread, NULL);
      debug(1, "Connection %d: asked to stop.", conns[i]->connection_number);
      // conns[i]->stop = 1;
      // pthread_kill(conns[i]->thread, SIGUSR1);
    }
  }
}

// park a null at the line ending, and return the next line pointer
// accept \r, \n, or \r\n
static char *nextline(char *in, int inbuf) {
  char *out = NULL;
  while (inbuf) {
    if (*in == '\r') {
      *in++ = 0;
      out = in;
      inbuf--;
    }
    if ((*in == '\n') && (inbuf)) {
      *in++ = 0;
      out = in;
    }

    if (out)
      break;

    in++;
    inbuf--;
  }
  return out;
}

void msg_retain(rtsp_message *msg) {
  int rc = pthread_mutex_lock(&reference_counter_lock);
  if (rc)
    debug(1, "Error %d locking reference counter lock");
  if (msg > (rtsp_message *)0x00010000) {
    msg->referenceCount++;
    debug(3, "msg_free increment reference counter message %d to %d.", msg->index_number,
          msg->referenceCount);
    // debug(1,"msg_retain -- item %d reference count %d.", msg->index_number, msg->referenceCount);
    rc = pthread_mutex_unlock(&reference_counter_lock);
    if (rc)
      debug(1, "Error %d unlocking reference counter lock");
  } else {
    debug(1, "invalid rtsp_message pointer 0x%x passed to retain", (uintptr_t)msg);
  }
}

rtsp_message *msg_init(void) {
  rtsp_message *msg = malloc(sizeof(rtsp_message));
  if (msg) {
    memset(msg, 0, sizeof(rtsp_message));
    msg->referenceCount = 1; // from now on, any access to this must be protected with the lock
    msg->index_number = msg_indexes++;
    debug(3, "msg_init message %d", msg->index_number);
  } else {
    die("msg_init -- can not allocate memory for rtsp_message %d.", msg_indexes);
  }
  // debug(1,"msg_init -- create item %d.", msg->index_number);
  return msg;
}

int msg_add_header(rtsp_message *msg, char *name, char *value) {
  if (msg->nheaders >= sizeof(msg->name) / sizeof(char *)) {
    warn("too many headers?!");
    return 1;
  }

  msg->name[msg->nheaders] = strdup(name);
  msg->value[msg->nheaders] = strdup(value);
  msg->nheaders++;

  return 0;
}

char *msg_get_header(rtsp_message *msg, char *name) {
  unsigned int i;
  for (i = 0; i < msg->nheaders; i++)
    if (!strcasecmp(msg->name[i], name))
      return msg->value[i];
  return NULL;
}

void debug_print_msg_headers(int level, rtsp_message *msg) {
  unsigned int i;
  for (i = 0; i < msg->nheaders; i++) {
    debug(level, "  Type: \"%s\", content: \"%s\"", msg->name[i], msg->value[i]);
  }
}

/*
static void debug_print_msg_content(int level, rtsp_message *msg) {
  if (msg->contentlength) {
    char *obf = malloc(msg->contentlength * 2 + 1);
    if (obf) {
      char *obfp = obf;
      int obfc;
      for (obfc = 0; obfc < msg->contentlength; obfc++) {
        snprintf(obfp, 3, "%02X", msg->content[obfc]);
        obfp += 2;
      };
      *obfp = 0;
      debug(level, "Content (hex): \"%s\"", obf);
      free(obf);
    } else {
      debug(level, "Can't allocate space for debug buffer");
    }
  } else {
    debug(level, "No content");
  }
}
*/

void msg_free(rtsp_message **msgh) {
  debug_mutex_lock(&reference_counter_lock, 1000, 0);
  if (*msgh > (rtsp_message *)0x00010000) {
    rtsp_message *msg = *msgh;
    msg->referenceCount--;
    if (msg->referenceCount)
      debug(3, "msg_free decrement reference counter message %d to %d", msg->index_number,
            msg->referenceCount);
    if (msg->referenceCount == 0) {
      unsigned int i;
      for (i = 0; i < msg->nheaders; i++) {
        free(msg->name[i]);
        free(msg->value[i]);
      }
      if (msg->content)
        free(msg->content);
      // debug(1,"msg_free item %d -- free.",msg->index_number);
      uintptr_t index = (msg->index_number) & 0xFFFF;
      if (index == 0)
        index = 0x10000; // ensure it doesn't fold to zero.
      *msgh =
          (rtsp_message *)(index); // put a version of the index number of the freed message in here
      debug(3, "msg_free freed message %d", msg->index_number);
      free(msg);
    } else {
      // debug(1,"msg_free item %d -- decrement reference to
      // %d.",msg->index_number,msg->referenceCount);
    }
  } else if (*msgh != NULL) {
    debug(1,
          "msg_free: error attempting to free an allocated but already-freed rtsp_message, number "
          "%d.",
          (uintptr_t)*msgh);
  }
  debug_mutex_unlock(&reference_counter_lock, 0);
}

int msg_handle_line(rtsp_message **pmsg, char *line) {
  rtsp_message *msg = *pmsg;

  if (!msg) {
    msg = msg_init();
    *pmsg = msg;
    char *sp, *p;
    sp = NULL; // this is to quieten a compiler warning

    debug(3, "RTSP Message Received: \"%s\".", line);

    p = strtok_r(line, " ", &sp);
    if (!p)
      goto fail;
    strncpy(msg->method, p, sizeof(msg->method) - 1);

    p = strtok_r(NULL, " ", &sp);
    if (!p)
      goto fail;

    p = strtok_r(NULL, " ", &sp);
    if (!p)
      goto fail;
    if (strcmp(p, "RTSP/1.0"))
      goto fail;

    return -1;
  }

  if (strlen(line)) {
    char *p;
    p = strstr(line, ": ");
    if (!p) {
      warn("bad header: >>%s<<", line);
      goto fail;
    }
    *p = 0;
    p += 2;
    msg_add_header(msg, line, p);
    debug(3, "    %s: %s.", line, p);
    return -1;
  } else {
    char *cl = msg_get_header(msg, "Content-Length");
    if (cl)
      return atoi(cl);
    else
      return 0;
  }

fail:
  debug(3, "msg_handle_line fail");
  msg_free(pmsg);
  *pmsg = NULL;
  return 0;
}

enum rtsp_read_request_response rtsp_read_request(rtsp_conn_info *conn, rtsp_message **the_packet) {

  *the_packet = NULL; // need this for error handling

  enum rtsp_read_request_response reply = rtsp_read_request_response_ok;
  ssize_t buflen = 4096;
  int release_buffer = 0;         // on exit, don't deallocate the buffer if everything was okay
  char *buf = malloc(buflen + 1); // add a NUL at the end
  if (!buf) {
    warn("Connection %d: rtsp_read_request: can't get a buffer.", conn->connection_number);
    return (rtsp_read_request_response_error);
  }
  pthread_cleanup_push(malloc_cleanup, buf);
  ssize_t nread;
  ssize_t inbuf = 0;
  int msg_size = -1;

  while (msg_size < 0) {
    if (conn->stop != 0) {
      debug(3, "Connection %d: shutdown requested.", conn->connection_number);
      reply = rtsp_read_request_response_immediate_shutdown_requested;
      goto shutdown;
    }

    nread = read(conn->fd, buf + inbuf, buflen - inbuf);

    if (nread == 0) {
      // a blocking read that returns zero means eof -- implies connection closed
      debug(3, "Connection %d: -- connection closed.", conn->connection_number);
      reply = rtsp_read_request_response_channel_closed;
      goto shutdown;
    }

    if (nread < 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN) {
        debug(1, "Connection %d: getting Error 11 -- EAGAIN from a blocking read!",
              conn->connection_number);
        continue;
      }
      if (errno != ECONNRESET) {
        char errorstring[1024];
        strerror_r(errno, (char *)errorstring, sizeof(errorstring));
        debug(1, "Connection %d: rtsp_read_request_response_read_error %d: \"%s\".",
              conn->connection_number, errno, (char *)errorstring);
      }
      reply = rtsp_read_request_response_read_error;
      goto shutdown;
    }

    /* // this outputs the message received
        {
        void *pt = malloc(nread+1);
        memset(pt, 0, nread+1);
        memcpy(pt, buf + inbuf, nread);
        debug(1, "Incoming string on port: \"%s\"",pt);
        free(pt);
        }
    */

    inbuf += nread;

    char *next;
    while (msg_size < 0 && (next = nextline(buf, inbuf))) {
      msg_size = msg_handle_line(the_packet, buf);

      if (!(*the_packet)) {
        debug(1, "Connection %d: rtsp_read_request can't find an RTSP header.",
              conn->connection_number);
        reply = rtsp_read_request_response_bad_packet;
        goto shutdown;
      }

      inbuf -= next - buf;
      if (inbuf)
        memmove(buf, next, inbuf);
    }
  }

  if (msg_size > buflen) {
    buf = realloc(buf, msg_size + 1);
    if (!buf) {
      warn("Connection %d: too much content.", conn->connection_number);
      reply = rtsp_read_request_response_error;
      goto shutdown;
    }
    buflen = msg_size;
  }

  uint64_t threshold_time =
      get_absolute_time_in_ns() + ((uint64_t)15000000000); // i.e. fifteen seconds from now
  int warning_message_sent = 0;

  const size_t max_read_chunk = 1024 * 1024 / 16;
  while (inbuf < msg_size) {

    // we are going to read the stream in chunks and time how long it takes to
    // do so.
    // If it's taking too long, (and we find out about it), we will send an
    // error message as
    // metadata

    if (warning_message_sent == 0) {
      uint64_t time_now = get_absolute_time_in_ns();
      if (time_now > threshold_time) { // it's taking too long
        debug(1, "Error receiving metadata from source -- transmission seems "
                 "to be stalled.");
#ifdef CONFIG_METADATA
        send_ssnc_metadata('stal', NULL, 0, 1);
#endif
        warning_message_sent = 1;
      }
    }

    if (conn->stop != 0) {
      debug(1, "RTSP shutdown requested.");
      reply = rtsp_read_request_response_immediate_shutdown_requested;
      goto shutdown;
    }
    size_t read_chunk = msg_size - inbuf;
    if (read_chunk > max_read_chunk)
      read_chunk = max_read_chunk;
    usleep(80000); // wait about 80 milliseconds between reads of up to about 64 kB
    nread = read(conn->fd, buf + inbuf, read_chunk);
    if (!nread) {
      reply = rtsp_read_request_response_error;
      goto shutdown;
    }
    if (nread < 0) {
      if (errno == EINTR)
        continue;
      if (errno == EAGAIN) {
        debug(1, "Getting Error 11 -- EAGAIN from a blocking read!");
        continue;
      }
      if (errno != ECONNRESET) {
        char errorstring[1024];
        strerror_r(errno, (char *)errorstring, sizeof(errorstring));
        debug(1, "Connection %d: rtsp_read_request_response_read_error %d: \"%s\".",
              conn->connection_number, errno, (char *)errorstring);
      }
      reply = rtsp_read_request_response_read_error;
      goto shutdown;
    }
    inbuf += nread;
  }

  rtsp_message *msg = *the_packet;
  msg->contentlength = inbuf;
  msg->content = buf;
  char *jp = inbuf + buf;
  *jp = '\0';
  *the_packet = msg;
shutdown:
  if (reply != rtsp_read_request_response_ok) {
    msg_free(the_packet);
    release_buffer = 1; // allow the buffer to be released
  }
  pthread_cleanup_pop(release_buffer);
  return reply;
}

int msg_write_response(int fd, rtsp_message *resp) {
  char pkt[2048];
  int pktfree = sizeof(pkt);
  char *p = pkt;
  int n;
  unsigned int i;

  n = snprintf(p, pktfree, "RTSP/1.0 %d %s\r\n", resp->respcode,
               resp->respcode == 200 ? "OK" : "Unauthorized");
  // debug(1, "sending response: %s", pkt);
  pktfree -= n;
  p += n;

  for (i = 0; i < resp->nheaders; i++) {
    //    debug(3, "    %s: %s.", resp->name[i], resp->value[i]);
    n = snprintf(p, pktfree, "%s: %s\r\n", resp->name[i], resp->value[i]);
    pktfree -= n;
    p += n;
    if (pktfree <= 1024) {
      debug(1, "Attempted to write overlong RTSP packet 1");
      return -1;
    }
  }

  // Here, if there's content, write the Content-Length header ...

  if (resp->contentlength) {
    debug(1, "Responding with content of length %d", resp->contentlength);
    n = snprintf(p, pktfree, "Content-Length: %d\r\n", resp->contentlength);
    pktfree -= n;
    p += n;
    if (pktfree <= 1024) {
      debug(1, "Attempted to write overlong RTSP packet 2");
      return -2;
    }
    debug(1, "Content is \"%s\"", resp->content);
    memcpy(p, resp->content, resp->contentlength);
    pktfree -= resp->contentlength;
    p += resp->contentlength;
  }

  n = snprintf(p, pktfree, "\r\n");
  pktfree -= n;
  p += n;

  if (pktfree <= 1024) {
    debug(1, "Attempted to write overlong RTSP packet 3");
    return -3;
  }
  ssize_t reply = write(fd, pkt, p - pkt);
  if (reply == -1) {
    char errorstring[1024];
    strerror_r(errno, (char *)errorstring, sizeof(errorstring));
    debug(1, "msg_write_response error %d: \"%s\".", errno, (char *)errorstring);
    return -4;
  }
  if (reply != p - pkt) {
    debug(1, "msg_write_response error -- requested bytes: %d not fully written: %d.", p - pkt,
          reply);
    return -5;
  }
  return 0;
}

void handle_record(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(2, "Connection %d: RECORD", conn->connection_number);
  if (have_player(conn)) {
    if (conn->player_thread)
      warn("Connection %d: RECORD: Duplicate RECORD message -- ignored", conn->connection_number);
    else
      player_play(conn); // the thread better be 0

    resp->respcode = 200;
    // I think this is for telling the client what the absolute minimum latency
    // actually is,
    // and when the client specifies a latency, it should be added to this figure.

    // Thus, [the old version of] AirPlay's latency figure of 77175, when added to 11025 gives you
    // exactly 88200
    // and iTunes' latency figure of 88553, when added to 11025 gives you 99578,
    // pretty close to the 99400 we guessed.

    msg_add_header(resp, "Audio-Latency", "11025");

    char *p;
    uint32_t rtptime = 0;
    char *hdr = msg_get_header(req, "RTP-Info");

    if (hdr) {
      // debug(1,"FLUSH message received: \"%s\".",hdr);
      // get the rtp timestamp
      p = strstr(hdr, "rtptime=");
      if (p) {
        p = strchr(p, '=');
        if (p) {
          rtptime = uatoi(p + 1); // unsigned integer -- up to 2^32-1
          // rtptime--;
          // debug(1,"RTSP Flush Requested by handle_record: %u.",rtptime);
          player_flush(rtptime, conn);
        }
      }
    }
  } else {
    warn("Connection %d RECORD received without having the player (no ANNOUNCE?)",
         conn->connection_number);
    resp->respcode = 451;
  }
}

void handle_options(rtsp_conn_info *conn, __attribute__((unused)) rtsp_message *req,
                    rtsp_message *resp) {
  debug(3, "Connection %d: OPTIONS", conn->connection_number);
  resp->respcode = 200;
  msg_add_header(resp, "Public",
                 "ANNOUNCE, SETUP, RECORD, "
                 "PAUSE, FLUSH, TEARDOWN, "
                 "OPTIONS, GET_PARAMETER, SET_PARAMETER");
}

void handle_teardown(rtsp_conn_info *conn, __attribute__((unused)) rtsp_message *req,
                     rtsp_message *resp) {
  debug(2, "Connection %d: TEARDOWN", conn->connection_number);
  if (have_player(conn)) {
    resp->respcode = 200;
    msg_add_header(resp, "Connection", "close");
    debug(
        3,
        "TEARDOWN: synchronously terminating the player thread of RTSP conversation thread %d (2).",
        conn->connection_number);
    player_stop(conn);
    debug(3, "TEARDOWN: successful termination of playing thread of RTSP conversation thread %d.",
          conn->connection_number);
  } else {
    warn("Connection %d TEARDOWN received without having the player (no ANNOUNCE?)",
         conn->connection_number);
    resp->respcode = 451;
  }
}

void handle_flush(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(3, "Connection %d: FLUSH", conn->connection_number);
  if (have_player(conn)) {
    char *p = NULL;
    uint32_t rtptime = 0;
    char *hdr = msg_get_header(req, "RTP-Info");

    if (hdr) {
      // debug(1,"FLUSH message received: \"%s\".",hdr);
      // get the rtp timestamp
      p = strstr(hdr, "rtptime=");
      if (p) {
        p = strchr(p, '=');
        if (p)
          rtptime = uatoi(p + 1); // unsigned integer -- up to 2^32-1
      }
    }
    // debug(1,"RTSP Flush Requested: %u.",rtptime);

    // the following is now done better by the player_flush routine as a 'pfls'
    /*
    #ifdef CONFIG_METADATA
        if (p)
          send_metadata('ssnc', 'flsr', p + 1, strlen(p + 1), req, 1);
        else
          send_metadata('ssnc', 'flsr', NULL, 0, NULL, 0);
    #endif
    */

    player_flush(rtptime, conn); // will not crash even it there is no player thread.
    resp->respcode = 200;

  } else {
    warn("Connection %d FLUSH received without having the player (no ANNOUNCE?)",
         conn->connection_number);
    resp->respcode = 451;
  }
}

void handle_setup(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(3, "Connection %d: SETUP", conn->connection_number);
  resp->respcode = 451; // invalid arguments -- expect them
  if (have_player(conn)) {
    uint16_t cport, tport;
    char *ar = msg_get_header(req, "Active-Remote");
    if (ar) {
      debug(2, "Connection %d: SETUP -- Active-Remote string seen: \"%s\".",
            conn->connection_number, ar);
      // get the active remote
      if (conn->dacp_active_remote) // this is in case SETUP was previously called
        free(conn->dacp_active_remote);
      conn->dacp_active_remote = strdup(ar);
#ifdef CONFIG_METADATA
      send_metadata('ssnc', 'acre', ar, strlen(ar), req, 1);
#endif
    } else {
      debug(2, "Connection %d: SETUP -- Note: no Active-Remote information  the SETUP Record.",
            conn->connection_number);
      if (conn->dacp_active_remote) { // this is in case SETUP was previously called
        free(conn->dacp_active_remote);
        conn->dacp_active_remote = NULL;
      }
    }

    ar = msg_get_header(req, "DACP-ID");
    if (ar) {
      debug(2, "Connection %d: SETUP -- DACP-ID string seen: \"%s\".", conn->connection_number, ar);
      if (conn->dacp_id) // this is in case SETUP was previously called
        free(conn->dacp_id);
      conn->dacp_id = strdup(ar);
#ifdef CONFIG_METADATA
      send_metadata('ssnc', 'daid', ar, strlen(ar), req, 1);
#endif
    } else {
      debug(2, "Connection %d: SETUP doesn't include DACP-ID string information.",
            conn->connection_number);
      if (conn->dacp_id) { // this is in case SETUP was previously called
        free(conn->dacp_id);
        conn->dacp_id = NULL;
      }
    }

    char *hdr = msg_get_header(req, "Transport");
    if (hdr) {
      char *p;
      p = strstr(hdr, "control_port=");
      if (p) {
        p = strchr(p, '=') + 1;
        cport = atoi(p);

        p = strstr(hdr, "timing_port=");
        if (p) {
          p = strchr(p, '=') + 1;
          tport = atoi(p);

          if (conn->rtp_running) {
            if ((conn->remote_control_port != cport) || (conn->remote_timing_port != tport)) {
              warn("Connection %d: Duplicate SETUP message with different control (old %u, new %u) "
                   "or "
                   "timing (old %u, new "
                   "%u) ports! This is probably fatal!",
                   conn->connection_number, conn->remote_control_port, cport,
                   conn->remote_timing_port, tport);
            } else {
              warn("Connection %d: Duplicate SETUP message with the same control (%u) and timing "
                   "(%u) "
                   "ports. This is "
                   "probably not fatal.",
                   conn->connection_number, conn->remote_control_port, conn->remote_timing_port);
            }
          } else {
            rtp_setup(&conn->local, &conn->remote, cport, tport, conn);
          }
          if (conn->local_audio_port != 0) {

            char resphdr[256] = "";
            snprintf(resphdr, sizeof(resphdr),
                     "RTP/AVP/"
                     "UDP;unicast;interleaved=0-1;mode=record;control_port=%d;"
                     "timing_port=%d;server_"
                     "port=%d",
                     conn->local_control_port, conn->local_timing_port, conn->local_audio_port);

            msg_add_header(resp, "Transport", resphdr);

            msg_add_header(resp, "Session", "1");

            resp->respcode = 200; // it all worked out okay
            debug(1,
                  "Connection %d: SETUP DACP-ID \"%s\" from %s to %s with UDP ports Control: "
                  "%d, Timing: %d and Audio: %d.",
                  conn->connection_number, conn->dacp_id, &conn->client_ip_string,
                  &conn->self_ip_string, conn->local_control_port, conn->local_timing_port,
                  conn->local_audio_port);

          } else {
            debug(1, "Connection %d: SETUP seems to specify a null audio port.",
                  conn->connection_number);
          }
        } else {
          debug(1, "Connection %d: SETUP doesn't specify a timing_port.", conn->connection_number);
        }
      } else {
        debug(1, "Connection %d: SETUP doesn't specify a control_port.", conn->connection_number);
      }
    } else {
      debug(1, "Connection %d: SETUP doesn't contain a Transport header.", conn->connection_number);
    }
    if (resp->respcode != 200) {
      debug(1, "Connection %d: SETUP error -- releasing the player lock.", conn->connection_number);
      debug_mutex_lock(&playing_conn_lock, 1000000, 3);
      if (playing_conn == conn) // if we have the player
        playing_conn = NULL;    // let it go
      debug_mutex_unlock(&playing_conn_lock, 3);
    }

  } else {
    warn("Connection %d SETUP received without having the player (no ANNOUNCE?)",
         conn->connection_number);
  }
}

/*
static void handle_ignore(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(1, "Connection thread %d: IGNORE", conn->connection_number);
  resp->respcode = 200;
}
*/

void handle_set_parameter_parameter(rtsp_conn_info *conn, rtsp_message *req,
                                    __attribute__((unused)) rtsp_message *resp) {

  char *cp = req->content;
  int cp_left = req->contentlength;
  /*
  int k = cp_left;
  if (k>max_bytes)
    k = max_bytes;
  for (i = 0; i < k; i++)
    snprintf((char *)buf + 2 * i, 3, "%02x", cp[i]);
  debug(1, "handle_set_parameter_parameter: \"%s\".",buf);
  */

  char *next;
  while (cp_left && cp) {
    next = nextline(cp, cp_left);
    // note: "next" will return NULL if there is no \r or \n or \r\n at the end of this
    // but we are always guaranteed that if cp is not null, it will be pointing to something
    // NUL-terminated

    if (next)
      cp_left -= (next - cp);
    else
      cp_left = 0;

    if (!strncmp(cp, "volume: ", strlen("volume: "))) {
      float volume = atof(cp + strlen("volume: "));
      // debug(2, "AirPlay request to set volume to: %f.", volume);
      player_volume(volume, conn);
    } else
#ifdef CONFIG_METADATA
        if (!strncmp(cp, "progress: ", strlen("progress: "))) {
      char *progress = cp + strlen("progress: ");
      // debug(2, "progress: \"%s\"",progress); // rtpstampstart/rtpstampnow/rtpstampend 44100 per
      // second
      send_ssnc_metadata('prgr', progress, strlen(progress), 1);

    } else
#endif
    {
      debug(1, "unrecognised parameter: \"%s\" (%d)\n", cp, strlen(cp));
    }
    cp = next;
  }
}

#ifdef CONFIG_METADATA
// Metadata is not used by shairport-sync.
// Instead we send all metadata to a fifo pipe, so that other apps can listen to
// the pipe and use the metadata.

// We use two 4-character codes to identify each piece of data and we send the
// data itself, if any,
// in base64 form.

// The first 4-character code, called the "type", is either:
//    'core' for all the regular metadadata coming from iTunes, etc., or
//    'ssnc' (for 'shairport-sync') for all metadata coming from Shairport Sync
//    itself, such as
//    start/end delimiters, etc.

// For 'core' metadata, the second 4-character code is the 4-character metadata
// code coming from
// iTunes etc.
// For 'ssnc' metadata, the second 4-character code is used to distinguish the
// messages.

// Cover art is not tagged in the same way as other metadata, it seems, so is
// sent as an 'ssnc' type
// metadata message with the code 'PICT'
// Here are the 'ssnc' codes defined so far:
//    'PICT' -- the payload is a picture, either a JPEG or a PNG. Check the
//    first few bytes to see
//    which.
//    'abeg' -- active mode entered. No arguments
//    'aend' -- active mode exited. No arguments
//    'pbeg' -- play stream begin. No arguments
//    'pend' -- play stream end. No arguments
//    'pfls' -- play stream flush. The argument is an unsigned 32-bit
//               frame number. It seems that all frames up to but not
//               including this frame are to be flushed.
//
//    'prsm' -- play stream resume. No arguments
//		`pffr` -- the first frame of a play session has been received and has been validly
// timed.
//    'pvol' -- play volume. The volume is sent as a string --
//    "airplay_volume,volume,lowest_volume,highest_volume"
//              volume, lowest_volume and highest_volume are given in dB.
//              The "airplay_volume" is what's sent to the player, and is from
//              0.00 down to -30.00,
//              with -144.00 meaning mute.
//              This is linear on the volume control slider of iTunes or iOS
//              AirPlay.
//    'prgr' -- progress -- this is metadata from AirPlay consisting of RTP
//    timestamps for the start
//    of the current play sequence, the current play point and the end of the
//    play sequence.
//              I guess the timestamps wrap at 2^32.
//    'mdst' -- a sequence of metadata is about to start; will have, as data,
//    the rtptime associated with the metadata, if available
//    'mden' -- a sequence of metadata has ended; will have, as data, the
//    rtptime associated with the metadata, if available
//    'pcst' -- a picture is about to be sent; will have, as data, the rtptime
//    associated with the picture, if available
//    'pcen' -- a picture has been sent; will have, as data, the rtptime
//    associated with the metadata, if available
//    'snam' -- A device -- e.g. "Joe's iPhone" -- has opened a play session.
//    Specifically, it's the "X-Apple-Client-Name" string
//    'snua' -- A "user agent" -- e.g. "iTunes/12..." -- has opened a play
//    session. Specifically, it's the "User-Agent" string
//    The next two two tokens are to facilitate remote control of the source.
//    There is some information at http://nto.github.io/AirPlay.html about
//    remote control of the source.
//
//    'daid' -- this is the source's DACP-ID (if it has one -- it's not
//    guaranteed), useful if you want to remotely control the source. Use this
//    string to identify the source's remote control on the network.
//    'acre' -- this is the source's Active-Remote token, necessary if you want
//    to send commands to the source's remote control (if it has one).
//		`clip` -- the payload is the IP number of the client, i.e. the sender of audio.
//		Can be an IPv4 or an IPv6 number.
//		`svip` -- the payload is the IP number of the server, i.e. the player itself.
//		Can be an IPv4 or an IPv6 number.
//		`dapo` -- the payload is the port number (as text) on the server to which remote
// control commands should be sent. It is 3689 for iTunes but varies for iOS devices.

//		A special sub-protocol is used for sending large data items over UDP
//    If the payload exceeded 4 MB, it is chunked using the following format:
//    "ssnc", "chnk", packet_ix, packet_counts, packet_tag, packet_type, chunked_data.
//    Notice that the number of items is different to the standard

// including a simple base64 encoder to minimise malloc/free activity

// From Stack Overflow, with thanks:
// http://stackoverflow.com/questions/342409/how-do-i-base64-encode-decode-in-c
// minor mods to make independent of C99.
// more significant changes make it not malloc memory
// needs to initialise the docoding table first

// add _so to end of name to avoid confusion with polarssl's implementation

static char encoding_table[] = {'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
                                'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
                                'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
                                'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
                                '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};

static size_t mod_table[] = {0, 2, 1};

// pass in a pointer to the data, its length, a pointer to the output buffer and
// a pointer to an int
// containing its maximum length
// the actual length will be returned.

char *base64_encode_so(const unsigned char *data, size_t input_length, char *encoded_data,
                       size_t *output_length) {

  size_t calculated_output_length = 4 * ((input_length + 2) / 3);
  if (calculated_output_length > *output_length)
    return (NULL);
  *output_length = calculated_output_length;

  size_t i, j;
  for (i = 0, j = 0; i < input_length;) {

    uint32_t octet_a = i < input_length ? (unsigned char)data[i++] : 0;
    uint32_t octet_b = i < input_length ? (unsigned char)data[i++] : 0;
    uint32_t octet_c = i < input_length ? (unsigned char)data[i++] : 0;

    uint32_t triple = (octet_a << 0x10) + (octet_b << 0x08) + octet_c;

    encoded_data[j++] = encoding_table[(triple >> 3 * 6) & 0x3F];
    encoded_data[j++] = encoding_table[(triple >> 2 * 6) & 0x3F];
    encoded_data[j++] = encoding_table[(triple >> 1 * 6) & 0x3F];
    encoded_data[j++] = encoding_table[(triple >> 0 * 6) & 0x3F];
  }

  for (i = 0; i < mod_table[input_length % 3]; i++)
    encoded_data[*output_length - 1 - i] = '=';

  return encoded_data;
}

// with thanks!
//

static int fd = -1;
// static int dirty = 0;

pc_queue metadata_queue;
#define metadata_queue_size 500
metadata_package metadata_queue_items[metadata_queue_size];
pthread_t metadata_thread;

#ifdef CONFIG_METADATA_HUB
pc_queue metadata_hub_queue;
#define metadata_hub_queue_size 500
metadata_package metadata_hub_queue_items[metadata_hub_queue_size];
pthread_t metadata_hub_thread;
#endif

#ifdef CONFIG_MQTT
pc_queue metadata_mqtt_queue;
#define metadata_mqtt_queue_size 500
metadata_package metadata_mqtt_queue_items[metadata_mqtt_queue_size];
pthread_t metadata_mqtt_thread;
#endif

static int metadata_sock = -1;
static struct sockaddr_in metadata_sockaddr;
static char *metadata_sockmsg;
pc_queue metadata_multicast_queue;
#define metadata_multicast_queue_size 500
metadata_package metadata_multicast_queue_items[metadata_queue_size];
pthread_t metadata_multicast_thread;

void metadata_create_multicast_socket(void) {
  if (config.metadata_enabled == 0)
    return;

  // Unlike metadata pipe, socket is opened once and stays open,
  // so we can call it in create
  if (config.metadata_sockaddr && config.metadata_sockport) {
    metadata_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (metadata_sock < 0) {
      debug(1, "Could not open metadata socket");
    } else {
      int buffer_size = METADATA_SNDBUF;
      setsockopt(metadata_sock, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
      bzero((char *)&metadata_sockaddr, sizeof(metadata_sockaddr));
      metadata_sockaddr.sin_family = AF_INET;
      metadata_sockaddr.sin_addr.s_addr = inet_addr(config.metadata_sockaddr);
      metadata_sockaddr.sin_port = htons(config.metadata_sockport);
      metadata_sockmsg = malloc(config.metadata_sockmsglength);
      if (metadata_sockmsg) {
        memset(metadata_sockmsg, 0, config.metadata_sockmsglength);
      } else {
        die("Could not malloc metadata multicast socket buffer");
      }
    }
  }
}

void metadata_delete_multicast_socket(void) {
  if (config.metadata_enabled == 0)
    return;
  shutdown(metadata_sock, SHUT_RDWR); // we want to immediately deallocate the buffer
  close(metadata_sock);
  if (metadata_sockmsg)
    free(metadata_sockmsg);
}

void metadata_open(void) {
  if (config.metadata_enabled == 0)
    return;

  size_t pl = strlen(config.metadata_pipename) + 1;

  char *path = malloc(pl + 1);
  snprintf(path, pl + 1, "%s", config.metadata_pipename);

  fd = try_to_open_pipe_for_writing(path);
  free(path);
}

static void metadata_close(void) {
  if (fd < 0)
    return;
  close(fd);
  fd = -1;
}

void metadata_multicast_process(uint32_t type, uint32_t code, char *data, uint32_t length) {
  // debug(1, "Process multicast metadata with type %x, code %x and length %u.", type, code,
  // length);
  if (metadata_sock >= 0 && length < config.metadata_sockmsglength - 8) {
    char *ptr = metadata_sockmsg;
    uint32_t v;
    v = htonl(type);
    memcpy(ptr, &v, 4);
    ptr += 4;
    v = htonl(code);
    memcpy(ptr, &v, 4);
    ptr += 4;
    memcpy(ptr, data, length);
    sendto(metadata_sock, metadata_sockmsg, length + 8, 0, (struct sockaddr *)&metadata_sockaddr,
           sizeof(metadata_sockaddr));
  } else if (metadata_sock >= 0) {
    // send metadata in numbered chunks using the protocol:
    // ("ssnc", "chnk", packet_ix, packet_counts, packet_tag, packet_type, chunked_data)

    uint32_t chunk_ix = 0;
    uint32_t chunk_total = length / (config.metadata_sockmsglength - 24);
    if (chunk_total * (config.metadata_sockmsglength - 24) < length) {
      chunk_total++;
    }
    uint32_t remaining = length;
    uint32_t v;
    char *data_crsr = data;
    do {
      char *ptr = metadata_sockmsg;
      memcpy(ptr, "ssncchnk", 8);
      ptr += 8;
      v = htonl(chunk_ix);
      memcpy(ptr, &v, 4);
      ptr += 4;
      v = htonl(chunk_total);
      memcpy(ptr, &v, 4);
      ptr += 4;
      v = htonl(type);
      memcpy(ptr, &v, 4);
      ptr += 4;
      v = htonl(code);
      memcpy(ptr, &v, 4);
      ptr += 4;
      size_t datalen = remaining;
      if (datalen > config.metadata_sockmsglength - 24) {
        datalen = config.metadata_sockmsglength - 24;
      }
      memcpy(ptr, data_crsr, datalen);
      data_crsr += datalen;
      sendto(metadata_sock, metadata_sockmsg, datalen + 24, 0,
             (struct sockaddr *)&metadata_sockaddr, sizeof(metadata_sockaddr));
      chunk_ix++;
      remaining -= datalen;
      if (remaining == 0)
        break;
    } while (1);
  }
}

void metadata_process(uint32_t type, uint32_t code, char *data, uint32_t length) {
  // debug(1, "Process metadata with type %x, code %x and length %u.", type, code, length);
  int ret = 0;
  // readers may go away and come back

  if (fd < 0)
    metadata_open();
  if (fd < 0)
    return;
  char thestring[1024];
  snprintf(thestring, 1024, "<item><type>%x</type><code>%x</code><length>%u</length>", type, code,
           length);
  // ret = non_blocking_write(fd, thestring, strlen(thestring));
  ret = write(fd, thestring, strlen(thestring));
  if (ret < 0) {
    // debug(1,"metadata_process error %d exit 1",ret);
    return;
  }
  if ((data != NULL) && (length > 0)) {
    snprintf(thestring, 1024, "\n<data encoding=\"base64\">\n");
    // ret = non_blocking_write(fd, thestring, strlen(thestring));
    ret = write(fd, thestring, strlen(thestring));
    if (ret < 0) {
      // debug(1,"metadata_process error %d exit 2",ret);
      return;
    }
    // here, we write the data in base64 form using our nice base64 encoder
    // but, we break it into lines of 76 output characters, except for the last
    // one.
    // thus, we send groups of (76/4)*3 =  57 bytes to the encoder at a time
    size_t remaining_count = length;
    char *remaining_data = data;
    // size_t towrite_count;
    char outbuf[76];
    while ((remaining_count) && (ret >= 0)) {
      size_t towrite_count = remaining_count;
      if (towrite_count > 57)
        towrite_count = 57;
      size_t outbuf_size = 76; // size of output buffer on entry, length of result on exit
      if (base64_encode_so((unsigned char *)remaining_data, towrite_count, outbuf, &outbuf_size) ==
          NULL)
        debug(1, "Error encoding base64 data.");
      // debug(1,"Remaining count: %d ret: %d, outbuf_size:
      // %d.",remaining_count,ret,outbuf_size);
      // ret = non_blocking_write(fd, outbuf, outbuf_size);
      ret = write(fd, outbuf, outbuf_size);
      if (ret < 0) {
        // debug(1,"metadata_process error %d exit 3",ret);
        return;
      }
      remaining_data += towrite_count;
      remaining_count -= towrite_count;
    }
    snprintf(thestring, 1024, "</data>");
    // ret = non_blocking_write(fd, thestring, strlen(thestring));
    ret = write(fd, thestring, strlen(thestring));
    if (ret < 0) {
      // debug(1,"metadata_process error %d exit 4",ret);
      return;
    }
  }
  snprintf(thestring, 1024, "</item>\n");
  // ret = non_blocking_write(fd, thestring, strlen(thestring));
  ret = write(fd, thestring, strlen(thestring));
  if (ret < 0) {
    // debug(1,"metadata_process error %d exit 5",ret);
    return;
  }
}

void metadata_pack_cleanup_function(void *arg) {
  // debug(1, "metadata_pack_cleanup_function called");
  metadata_package *pack = (metadata_package *)arg;
  if (pack->carrier)
    msg_free(&pack->carrier); // release the message
  else if (pack->data)
    free(pack->data);
  // debug(1, "metadata_pack_cleanup_function exit");
}

void metadata_thread_cleanup_function(__attribute__((unused)) void *arg) {
  // debug(2, "metadata_thread_cleanup_function called");
  metadata_close();
  pc_queue_delete(&metadata_queue);
}

void *metadata_thread_function(__attribute__((unused)) void *ignore) {
  // create a pc_queue for passing information to a threaded metadata handler
  pc_queue_init(&metadata_queue, (char *)&metadata_queue_items, sizeof(metadata_package),
                metadata_queue_size, "pipe");
  metadata_create_multicast_socket();
  metadata_package pack;
  pthread_cleanup_push(metadata_thread_cleanup_function, NULL);
  while (1) {
    pc_queue_get_item(&metadata_queue, &pack);
    pthread_cleanup_push(metadata_pack_cleanup_function, (void *)&pack);
    if (config.metadata_enabled) {
      if (pack.carrier) {
        debug(3, "     pipe: type %x, code %x, length %u, message %d.", pack.type, pack.code,
              pack.length, pack.carrier->index_number);
      } else {
        debug(3, "     pipe: type %x, code %x, length %u.", pack.type, pack.code, pack.length);
      }
      metadata_process(pack.type, pack.code, pack.data, pack.length);
      debug(3, "     pipe: done.");
    }
    pthread_cleanup_pop(1);
  }
  pthread_cleanup_pop(1); // will never happen
  pthread_exit(NULL);
}

void metadata_multicast_thread_cleanup_function(__attribute__((unused)) void *arg) {
  // debug(2, "metadata_multicast_thread_cleanup_function called");
  metadata_delete_multicast_socket();
  pc_queue_delete(&metadata_multicast_queue);
}

void *metadata_multicast_thread_function(__attribute__((unused)) void *ignore) {
  // create a pc_queue for passing information to a threaded metadata handler
  pc_queue_init(&metadata_multicast_queue, (char *)&metadata_multicast_queue_items,
                sizeof(metadata_package), metadata_multicast_queue_size, "multicast");
  metadata_create_multicast_socket();
  metadata_package pack;
  pthread_cleanup_push(metadata_multicast_thread_cleanup_function, NULL);
  while (1) {
    pc_queue_get_item(&metadata_multicast_queue, &pack);
    pthread_cleanup_push(metadata_pack_cleanup_function, (void *)&pack);
    if (config.metadata_enabled) {
      if (pack.carrier) {
        debug(3,
              "                                                                    multicast: type "
              "%x, code %x, length %u, message %d.",
              pack.type, pack.code, pack.length, pack.carrier->index_number);
      } else {
        debug(3,
              "                                                                    multicast: type "
              "%x, code %x, length %u.",
              pack.type, pack.code, pack.length);
      }
      metadata_multicast_process(pack.type, pack.code, pack.data, pack.length);
      debug(3,
            "                                                                    multicast: done.");
    }
    pthread_cleanup_pop(1);
  }
  pthread_cleanup_pop(1); // will never happen
  pthread_exit(NULL);
}

#ifdef CONFIG_METADATA_HUB
void metadata_hub_close(void) {}

void metadata_hub_thread_cleanup_function(__attribute__((unused)) void *arg) {
  // debug(2, "metadata_hub_thread_cleanup_function called");
  metadata_hub_close();
  pc_queue_delete(&metadata_hub_queue);
}

void *metadata_hub_thread_function(__attribute__((unused)) void *ignore) {
  // create a pc_queue for passing information to a threaded metadata handler
  pc_queue_init(&metadata_hub_queue, (char *)&metadata_hub_queue_items, sizeof(metadata_package),
                metadata_hub_queue_size, "hub");
  metadata_package pack;
  pthread_cleanup_push(metadata_hub_thread_cleanup_function, NULL);
  while (1) {
    pc_queue_get_item(&metadata_hub_queue, &pack);
    pthread_cleanup_push(metadata_pack_cleanup_function, (void *)&pack);
    if (pack.carrier) {
      debug(3, "                    hub: type %x, code %x, length %u, message %d.", pack.type,
            pack.code, pack.length, pack.carrier->index_number);
    } else {
      debug(3, "                    hub: type %x, code %x, length %u.", pack.type, pack.code,
            pack.length);
    }
    metadata_hub_process_metadata(pack.type, pack.code, pack.data, pack.length);
    debug(3, "                    hub: done.");
    pthread_cleanup_pop(1);
  }
  pthread_cleanup_pop(1); // will never happen
  pthread_exit(NULL);
}
#endif

#ifdef CONFIG_MQTT
void metadata_mqtt_close(void) {}

void metadata_mqtt_thread_cleanup_function(__attribute__((unused)) void *arg) {
  // debug(2, "metadata_mqtt_thread_cleanup_function called");
  metadata_mqtt_close();
  pc_queue_delete(&metadata_mqtt_queue);
  // debug(2, "metadata_mqtt_thread_cleanup_function done");
}

void *metadata_mqtt_thread_function(__attribute__((unused)) void *ignore) {
  // create a pc_queue for passing information to a threaded metadata handler
  pc_queue_init(&metadata_mqtt_queue, (char *)&metadata_mqtt_queue_items, sizeof(metadata_package),
                metadata_mqtt_queue_size, "mqtt");
  metadata_package pack;
  pthread_cleanup_push(metadata_mqtt_thread_cleanup_function, NULL);
  while (1) {
    pc_queue_get_item(&metadata_mqtt_queue, &pack);
    pthread_cleanup_push(metadata_pack_cleanup_function, (void *)&pack);
    if (config.mqtt_enabled) {
      if (pack.carrier) {
        debug(3,
              "                                        mqtt: type %x, code %x, length %u, message "
              "%d.",
              pack.type, pack.code, pack.length, pack.carrier->index_number);
      } else {
        debug(3, "                                        mqtt: type %x, code %x, length %u.",
              pack.type, pack.code, pack.length);
      }
      mqtt_process_metadata(pack.type, pack.code, pack.data, pack.length);
      debug(3, "                                        mqtt: done.");
    }

    pthread_cleanup_pop(1);
  }
  pthread_cleanup_pop(1); // will never happen
  pthread_exit(NULL);
}
#endif

void metadata_init(void) {
  int ret;
  if (config.metadata_enabled) {
    // create the metadata pipe, if necessary
    size_t pl = strlen(config.metadata_pipename) + 1;
    char *path = malloc(pl + 1);
    snprintf(path, pl + 1, "%s", config.metadata_pipename);
    mode_t oldumask = umask(000);
    if (mkfifo(path, 0666) && errno != EEXIST)
      die("Could not create metadata pipe \"%s\".", path);
    umask(oldumask);
    debug(1, "metadata pipe name is \"%s\".", path);

    // try to open it
    fd = try_to_open_pipe_for_writing(path);
    // we check that it's not a "real" error. From the "man 2 open" page:
    // "ENXIO  O_NONBLOCK | O_WRONLY is set, the named file is a FIFO, and no process has the FIFO
    // open for reading." Which is okay.
    if ((fd == -1) && (errno != ENXIO)) {
      char errorstring[1024];
      strerror_r(errno, (char *)errorstring, sizeof(errorstring));
      debug(1, "metadata_hub_thread_function -- error %d (\"%s\") opening pipe: \"%s\".", errno,
            (char *)errorstring, path);
      warn("can not open metadata pipe -- error %d (\"%s\") opening pipe: \"%s\".", errno,
           (char *)errorstring, path);
    }
    free(path);
    int ret;
    ret = pthread_create(&metadata_thread, NULL, metadata_thread_function, NULL);
    if (ret)
      debug(1, "Failed to create metadata thread!");

    ret =
        pthread_create(&metadata_multicast_thread, NULL, metadata_multicast_thread_function, NULL);
    if (ret)
      debug(1, "Failed to create metadata multicast thread!");
  }
#ifdef CONFIG_METADATA_HUB
  ret = pthread_create(&metadata_hub_thread, NULL, metadata_hub_thread_function, NULL);
  if (ret)
    debug(1, "Failed to create metadata hub thread!");
#endif
#ifdef CONFIG_MQTT
  ret = pthread_create(&metadata_mqtt_thread, NULL, metadata_mqtt_thread_function, NULL);
  if (ret)
    debug(1, "Failed to create metadata mqtt thread!");
#endif
  metadata_running = 1;
}

void metadata_stop(void) {
  if (metadata_running) {
    debug(2, "metadata_stop called.");
#ifdef CONFIG_MQTT
    // debug(2, "metadata stop mqtt thread.");
    pthread_cancel(metadata_mqtt_thread);
    pthread_join(metadata_mqtt_thread, NULL);
    // debug(2, "metadata stop mqtt done.");
#endif
#ifdef CONFIG_METADATA_HUB
    // debug(2, "metadata stop hub thread.");
    pthread_cancel(metadata_hub_thread);
    pthread_join(metadata_hub_thread, NULL);
    // debug(2, "metadata stop hub done.");
#endif
    if (config.metadata_enabled) {
      // debug(2, "metadata stop multicast thread.");
      if (metadata_multicast_thread) {
        pthread_cancel(metadata_multicast_thread);
        pthread_join(metadata_multicast_thread, NULL);
        // debug(2, "metadata stop multicast done.");
      }
      if (metadata_thread) {
        // debug(2, "metadata stop metadata_thread thread.");
        pthread_cancel(metadata_thread);
        pthread_join(metadata_thread, NULL);
        // debug(2, "metadata_stop finished successfully.");
      }
    }
  }
}

int send_metadata_to_queue(pc_queue *queue, uint32_t type, uint32_t code, char *data,
                           uint32_t length, rtsp_message *carrier, int block) {

  // parameters: type, code, pointer to data or NULL, length of data or NULL,
  // the rtsp_message or
  // NULL
  // the rtsp_message is sent for 'core' messages, because it contains the data
  // and must not be
  // freed until the data has been read. So, it is passed to send_metadata to be
  // retained,
  // sent to the thread where metadata is processed and released (and probably
  // freed).

  // The rtsp_message is also sent for certain non-'core' messages.

  // The reading of the parameters is a bit complex
  // If the rtsp_message field is non-null, then it represents an rtsp_message
  // and the data pointer is assumed to point to something within it.
  // The reference counter of the rtsp_message is incremented here and
  // should be decremented by the metadata handler when finished.
  // If the reference count reduces to zero, the message will be freed.

  // If the rtsp_message is NULL, then if the pointer is non-null then the data it
  // points to, of the length specified, is memcpy'd and passed to the metadata
  // handler. The handler should free it when done.
  // If the rtsp_message is NULL and the pointer is also NULL, nothing further
  // is done.

  metadata_package pack;
  pack.type = type;
  pack.code = code;
  pack.length = length;
  pack.carrier = carrier;
  pack.data = data;
  if (pack.carrier) {
    msg_retain(pack.carrier);
  } else {
    if (data)
      pack.data = memdup(data, length); // only if it's not a null
  }
  int rc = pc_queue_add_item(queue, &pack, block);
  if (rc != 0) {
    if (pack.carrier) {
      if (rc == EWOULDBLOCK)
        debug(2,
              "metadata queue \"%s\" full, dropping message item: type %x, code %x, data %x, "
              "length %u, message %d.",
              queue->name, pack.type, pack.code, pack.data, pack.length,
              pack.carrier->index_number);
      msg_free(&pack.carrier);
    } else {
      if (rc == EWOULDBLOCK)
        debug(
            2,
            "metadata queue \"%s\" full, dropping data item: type %x, code %x, data %x, length %u.",
            queue->name, pack.type, pack.code, pack.data, pack.length);
      if (pack.data)
        free(pack.data);
    }
  }
  return rc;
}

int send_metadata(uint32_t type, uint32_t code, char *data, uint32_t length, rtsp_message *carrier,
                  int block) {
  int rc;
  if (config.metadata_enabled) {
    rc = send_metadata_to_queue(&metadata_queue, type, code, data, length, carrier, block);
    rc =
        send_metadata_to_queue(&metadata_multicast_queue, type, code, data, length, carrier, block);
  }

#ifdef CONFIG_METADATA_HUB
  rc = send_metadata_to_queue(&metadata_hub_queue, type, code, data, length, carrier, block);
#endif

#ifdef CONFIG_MQTT
  rc = send_metadata_to_queue(&metadata_mqtt_queue, type, code, data, length, carrier, block);
#endif

  return rc;
}

static void handle_set_parameter_metadata(__attribute__((unused)) rtsp_conn_info *conn,
                                          rtsp_message *req,
                                          __attribute__((unused)) rtsp_message *resp) {
  char *cp = req->content;
  unsigned int cl = req->contentlength;

  unsigned int off = 8;

  uint32_t itag, vl;
  while (off < cl) {
    // pick up the metadata tag as an unsigned longint
    memcpy(&itag, (uint32_t *)(cp + off), sizeof(uint32_t)); /* can be misaligned, thus memcpy */
    itag = ntohl(itag);
    off += sizeof(uint32_t);

    // pick up the length of the data
    memcpy(&vl, (uint32_t *)(cp + off), sizeof(uint32_t)); /* can be misaligned, thus memcpy */
    vl = ntohl(vl);
    off += sizeof(uint32_t);

    // pass the data over
    if (vl == 0)
      send_metadata('core', itag, NULL, 0, NULL, 1);
    else
      send_metadata('core', itag, (char *)(cp + off), vl, req, 1);

    // move on to the next item
    off += vl;
  }
}

#endif

static void handle_get_parameter(__attribute__((unused)) rtsp_conn_info *conn, rtsp_message *req,
                                 rtsp_message *resp) {
  // debug(1, "Connection %d: GET_PARAMETER", conn->connection_number);
  // debug_print_msg_headers(1,req);
  // debug_print_msg_content(1,req);

  if ((req->content) && (req->contentlength == strlen("volume\r\n")) &&
      strstr(req->content, "volume") == req->content) {
    // debug(1,"Current volume sought");
    char *p = malloc(128); // will be automatically deallocated with the response is deleted
    if (p) {
      resp->content = p;
      resp->contentlength = snprintf(p, 128, "\r\nvolume: %.6f\r\n", config.airplay_volume);
    } else {
      debug(1, "Couldn't allocate space for a response.");
    }
  }
  resp->respcode = 200;
}

static void handle_set_parameter(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(3, "Connection %d: SET_PARAMETER", conn->connection_number);
  // if (!req->contentlength)
  //    debug(1, "received empty SET_PARAMETER request.");

  // debug_print_msg_headers(1,req);

  char *ct = msg_get_header(req, "Content-Type");

  if (ct) {
    // debug(2, "SET_PARAMETER Content-Type:\"%s\".", ct);

#ifdef CONFIG_METADATA
    // It seems that the rtptime of the message is used as a kind of an ID that
    // can be used
    // to link items of metadata, including pictures, that refer to the same
    // entity.
    // If they refer to the same item, they have the same rtptime.
    // So we send the rtptime before and after both the metadata items and the
    // picture item
    // get the rtptime
    char *p = NULL;
    char *hdr = msg_get_header(req, "RTP-Info");

    if (hdr) {
      p = strstr(hdr, "rtptime=");
      if (p) {
        p = strchr(p, '=');
      }
    }

    // not all items have RTP-time stuff in them, which is okay

    if (!strncmp(ct, "application/x-dmap-tagged", 25)) {
      debug(3, "received metadata tags in SET_PARAMETER request.");
      if (p == NULL)
        debug(1, "Missing RTP-Time info for metadata");
      if (p)
        send_metadata('ssnc', 'mdst', p + 1, strlen(p + 1), req, 1); // metadata starting
      else
        send_metadata('ssnc', 'mdst', NULL, 0, NULL,
                      0); // metadata starting, if rtptime is not available

      handle_set_parameter_metadata(conn, req, resp);

      if (p)
        send_metadata('ssnc', 'mden', p + 1, strlen(p + 1), req, 1); // metadata ending
      else
        send_metadata('ssnc', 'mden', NULL, 0, NULL,
                      0); // metadata starting, if rtptime is not available

    } else if (!strncmp(ct, "image", 5)) {
      // Some server simply ignore the md field from the TXT record. If The
      // config says 'please, do not include any cover art', we are polite and
      // do not write them to the pipe.
      if (config.get_coverart) {
        // debug(1, "received image in SET_PARAMETER request.");
        // note: the image/type tag isn't reliable, so it's not being sent
        // -- best look at the first few bytes of the image
        if (p == NULL)
          debug(1, "Missing RTP-Time info for picture item");
        if (p)
          send_metadata('ssnc', 'pcst', p + 1, strlen(p + 1), req, 1); // picture starting
        else
          send_metadata('ssnc', 'pcst', NULL, 0, NULL,
                        0); // picture starting, if rtptime is not available

        send_metadata('ssnc', 'PICT', req->content, req->contentlength, req, 1);

        if (p)
          send_metadata('ssnc', 'pcen', p + 1, strlen(p + 1), req, 1); // picture ending
        else
          send_metadata('ssnc', 'pcen', NULL, 0, NULL,
                        0); // picture ending, if rtptime is not available
      } else {
        debug(1, "Ignore received picture item (include_cover_art = no).");
      }
    } else
#endif
        if (!strncmp(ct, "text/parameters", 15)) {
      // debug(2, "received parameters in SET_PARAMETER request.");
      handle_set_parameter_parameter(conn, req, resp); // this could be volume or progress
    } else {
      debug(1, "received unknown Content-Type \"%s\" in SET_PARAMETER request.", ct);
    }
  } else {
    debug(1, "missing Content-Type header in SET_PARAMETER request.");
  }
  resp->respcode = 200;
}

static void handle_announce(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(3, "Connection %d: ANNOUNCE", conn->connection_number);

  int have_the_player = 0;
  int should_wait = 0; // this will be true if you're trying to break in to the current session
  int interrupting_current_session = 0;

  // try to become the current playing_conn

  debug_mutex_lock(&playing_conn_lock, 1000000, 3); // get it

  if (playing_conn == NULL) {
    playing_conn = conn;
    have_the_player = 1;
  } else if (playing_conn == conn) {
    have_the_player = 1;
    warn("Duplicate ANNOUNCE, by the look of it!");
  } else if (playing_conn->stop) {
    debug(1, "Connection %d ANNOUNCE is waiting for connection %d to shut down.",
          conn->connection_number, playing_conn->connection_number);
    should_wait = 1;
  } else if (config.allow_session_interruption == 1) {
    debug(2, "Connection %d: ANNOUNCE: asking playing connection %d to shut down.",
          conn->connection_number, playing_conn->connection_number);
    playing_conn->stop = 1;
    interrupting_current_session = 1;
    should_wait = 1;
    pthread_cancel(playing_conn->thread); // asking the RTSP thread to exit
  }
  debug_mutex_unlock(&playing_conn_lock, 3);

  if (should_wait) {
    int time_remaining = 3000000; // must be signed, as it could go negative...

    while ((time_remaining > 0) && (have_the_player == 0)) {
      debug_mutex_lock(&playing_conn_lock, 1000000, 3); // get it
      if (playing_conn == NULL) {
        playing_conn = conn;
        have_the_player = 1;
      }
      debug_mutex_unlock(&playing_conn_lock, 3);

      if (have_the_player == 0) {
        usleep(100000);
        time_remaining -= 100000;
      }
    }

    if ((have_the_player == 1) && (interrupting_current_session == 1)) {
      debug(2, "Connection %d: ANNOUNCE got the player", conn->connection_number);
    } else {
      debug(2, "Connection %d: ANNOUNCE failed to get the player", conn->connection_number);
    }
  }

  if (have_the_player) {
    debug(3, "Connection %d: ANNOUNCE has acquired play lock.", conn->connection_number);

    // now, if this new session did not break in, then it's okay to reset the next UDP ports
    // to the start of the range

    if (interrupting_current_session == 0) { // will be zero if it wasn't waiting to break in
      resetFreeUDPPort();
    }

    /*
    {
      char *cp = req->content;
      int cp_left = req->contentlength;
      while (cp_left > 1) {
        if (strlen(cp) != 0)
          debug(1,">>>>>> %s", cp);
        cp += strlen(cp) + 1;
        cp_left -= strlen(cp) + 1;
      }
    }
*/

    conn->stream.type = ast_unknown;
    resp->respcode = 456; // 456 - Header Field Not Valid for Resource
    char *pssid = NULL;
    char *paesiv = NULL;
    char *prsaaeskey = NULL;
    char *pfmtp = NULL;
    char *pminlatency = NULL;
    char *pmaxlatency = NULL;
    //    char *pAudioMediaInfo = NULL;
    char *pUncompressedCDAudio = NULL;
    char *cp = req->content;
    int cp_left = req->contentlength;
    char *next;
    while (cp_left && cp) {
      next = nextline(cp, cp_left);
      cp_left -= next - cp;

      if (!strncmp(cp, "a=rtpmap:96 L16/44100/2", strlen("a=rtpmap:96 L16/44100/2")))
        pUncompressedCDAudio = cp + strlen("a=rtpmap:96 L16/44100/2");

      //      if (!strncmp(cp, "m=audio", strlen("m=audio")))
      //        pAudioMediaInfo = cp + strlen("m=audio");

      if (!strncmp(cp, "o=iTunes", strlen("o=iTunes")))
        pssid = cp + strlen("o=iTunes");

      if (!strncmp(cp, "a=fmtp:", strlen("a=fmtp:")))
        pfmtp = cp + strlen("a=fmtp:");

      if (!strncmp(cp, "a=aesiv:", strlen("a=aesiv:")))
        paesiv = cp + strlen("a=aesiv:");

      if (!strncmp(cp, "a=rsaaeskey:", strlen("a=rsaaeskey:")))
        prsaaeskey = cp + strlen("a=rsaaeskey:");

      if (!strncmp(cp, "a=min-latency:", strlen("a=min-latency:")))
        pminlatency = cp + strlen("a=min-latency:");

      if (!strncmp(cp, "a=max-latency:", strlen("a=max-latency:")))
        pmaxlatency = cp + strlen("a=max-latency:");

      cp = next;
    }

    if (pUncompressedCDAudio) {
      debug(2, "An uncompressed PCM stream has been detected.");
      conn->stream.type = ast_uncompressed;
      conn->max_frames_per_packet = 352; // number of audio frames per packet.
      conn->input_rate = 44100;
      conn->input_num_channels = 2;
      conn->input_bit_depth = 16;
      conn->input_bytes_per_frame = conn->input_num_channels * ((conn->input_bit_depth + 7) / 8);

      /*
      int y = strlen(pAudioMediaInfo);
      if (y > 0) {
        char obf[4096];
        if (y > 4096)
          y = 4096;
        char *p = pAudioMediaInfo;
        char *obfp = obf;
        int obfc;
        for (obfc = 0; obfc < y; obfc++) {
          snprintf(obfp, 3, "%02X", (unsigned int)*p);
          p++;
          obfp += 2;
        };
        *obfp = 0;
        debug(1, "AudioMediaInfo: \"%s\".", obf);
      }
      */
    }

    if (pssid) {
      uint32_t ssid = uatoi(pssid);
      debug(3, "Synchronisation Source Identifier: %08X,%u", ssid, ssid);
    }

    if (pminlatency) {
      conn->minimum_latency = atoi(pminlatency);
      debug(3, "Minimum latency %d specified", conn->minimum_latency);
    }

    if (pmaxlatency) {
      conn->maximum_latency = atoi(pmaxlatency);
      debug(3, "Maximum latency %d specified", conn->maximum_latency);
    }

    if ((paesiv == NULL) && (prsaaeskey == NULL)) {
      // debug(1,"Unencrypted session requested?");
      conn->stream.encrypted = 0;
    } else {
      conn->stream.encrypted = 1;
      // debug(1,"Encrypted session requested");
    }

    if (conn->stream.encrypted) {
      int len, keylen;
      uint8_t *aesiv = base64_dec(paesiv, &len);
      if (len != 16) {
        warn("client announced aeskey of %d bytes, wanted 16", len);
        free(aesiv);
        goto out;
      }
      memcpy(conn->stream.aesiv, aesiv, 16);
      free(aesiv);

      uint8_t *rsaaeskey = base64_dec(prsaaeskey, &len);
      uint8_t *aeskey = rsa_apply(rsaaeskey, len, &keylen, RSA_MODE_KEY);
      free(rsaaeskey);
      if (keylen != 16) {
        warn("client announced rsaaeskey of %d bytes, wanted 16", keylen);
        free(aeskey);
        goto out;
      }
      memcpy(conn->stream.aeskey, aeskey, 16);
      free(aeskey);
    }

    if (pfmtp) {
      conn->stream.type = ast_apple_lossless;
      debug(3, "An ALAC stream has been detected.");

      // Set reasonable connection defaults
      conn->stream.fmtp[0] = 96;
      conn->stream.fmtp[1] = 352;
      conn->stream.fmtp[2] = 0;
      conn->stream.fmtp[3] = 16;
      conn->stream.fmtp[4] = 40;
      conn->stream.fmtp[5] = 10;
      conn->stream.fmtp[6] = 14;
      conn->stream.fmtp[7] = 2;
      conn->stream.fmtp[8] = 255;
      conn->stream.fmtp[9] = 0;
      conn->stream.fmtp[10] = 0;
      conn->stream.fmtp[11] = 44100;

      unsigned int i = 0;
      unsigned int max_param = sizeof(conn->stream.fmtp) / sizeof(conn->stream.fmtp[0]);
      char *found;
      while ((found = strsep(&pfmtp, " \t")) != NULL && i < max_param) {
        conn->stream.fmtp[i++] = atoi(found);
      }
      // here we should check the sanity of the fmtp values
      // for (i = 0; i < sizeof(conn->stream.fmtp) / sizeof(conn->stream.fmtp[0]); i++)
      //  debug(1,"  fmtp[%2d] is: %10d",i,conn->stream.fmtp[i]);

      // set the parameters of the player (as distinct from the parameters of the decoder -- that's
      // done later).
      conn->max_frames_per_packet = conn->stream.fmtp[1]; // number of audio frames per packet.
      conn->input_rate = conn->stream.fmtp[11];
      conn->input_num_channels = conn->stream.fmtp[7];
      conn->input_bit_depth = conn->stream.fmtp[3];
      conn->input_bytes_per_frame = conn->input_num_channels * ((conn->input_bit_depth + 7) / 8);
    }

    if (conn->stream.type == ast_unknown) {
      warn("Can not process the following ANNOUNCE message:");
      // print each line of the request content
      // the problem is that nextline has replace all returns, newlines, etc. by
      // NULLs
      char *cp = req->content;
      int cp_left = req->contentlength;
      while (cp_left > 1) {
        if (strlen(cp) != 0)
          warn("    %s", cp);
        cp += strlen(cp) + 1;
        cp_left -= strlen(cp) + 1;
      }
      goto out;
    }

    char *hdr = msg_get_header(req, "X-Apple-Client-Name");
    if (hdr) {
      debug(1, "Play connection from device named \"%s\" on RTSP conversation thread %d.", hdr,
            conn->connection_number);
#ifdef CONFIG_METADATA
      send_metadata('ssnc', 'snam', hdr, strlen(hdr), req, 1);
#endif
    }
    hdr = msg_get_header(req, "User-Agent");
    if (hdr) {
      conn->UserAgent = strdup(hdr);
      debug(2, "Play connection from user agent \"%s\" on RTSP conversation thread %d.", hdr,
            conn->connection_number);
      // if the user agent is AirPlay and has a version number of 353 or less (from iOS 11.1,2)
      // use the older way of calculating the latency

      char *p = strstr(hdr, "AirPlay");
      if (p) {
        p = strchr(p, '/');
        if (p) {
          conn->AirPlayVersion = atoi(p + 1);
          debug(2, "AirPlay version %d detected.", conn->AirPlayVersion);
        }
      }

#ifdef CONFIG_METADATA
      send_metadata('ssnc', 'snua', hdr, strlen(hdr), req, 1);
#endif
    }
    resp->respcode = 200;
  } else {
    resp->respcode = 453;
    debug(1, "Connection %d: ANNOUNCE failed because another connection is already playing.",
          conn->connection_number);
  }

out:
  if (resp->respcode != 200 && resp->respcode != 453) {
    debug(1, "Connection %d: Error in handling ANNOUNCE. Unlocking the play lock.",
          conn->connection_number);
    debug_mutex_lock(&playing_conn_lock, 1000000, 3); // get it
    if (playing_conn == conn)                         // if we managed to acquire it
      playing_conn = NULL;                            // let it go
    debug_mutex_unlock(&playing_conn_lock, 3);
  }
}

static struct method_handler {
  char *method;
  void (*handler)(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp);
} method_handlers[] = {{"OPTIONS", handle_options},
                       {"ANNOUNCE", handle_announce},
                       {"FLUSH", handle_flush},
                       {"TEARDOWN", handle_teardown},
                       {"SETUP", handle_setup},
                       {"GET_PARAMETER", handle_get_parameter},
                       {"SET_PARAMETER", handle_set_parameter},
                       {"RECORD", handle_record},
                       {NULL, NULL}};

static void apple_challenge(int fd, rtsp_message *req, rtsp_message *resp) {
  char *hdr = msg_get_header(req, "Apple-Challenge");
  if (!hdr)
    return;

  SOCKADDR fdsa;
  socklen_t sa_len = sizeof(fdsa);
  getsockname(fd, (struct sockaddr *)&fdsa, &sa_len);

  int chall_len;
  uint8_t *chall = base64_dec(hdr, &chall_len);
  if (chall == NULL)
    die("null chall in apple_challenge");
  uint8_t buf[48], *bp = buf;
  int i;
  memset(buf, 0, sizeof(buf));

  if (chall_len > 16) {
    warn("oversized Apple-Challenge!");
    free(chall);
    return;
  }
  memcpy(bp, chall, chall_len);
  free(chall);
  bp += chall_len;

#ifdef AF_INET6
  if (fdsa.SAFAMILY == AF_INET6) {
    struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)(&fdsa);
    memcpy(bp, sa6->sin6_addr.s6_addr, 16);
    bp += 16;
  } else
#endif
  {
    struct sockaddr_in *sa = (struct sockaddr_in *)(&fdsa);
    memcpy(bp, &sa->sin_addr.s_addr, 4);
    bp += 4;
  }

  for (i = 0; i < 6; i++)
    *bp++ = config.hw_addr[i];

  int buflen, resplen;
  buflen = bp - buf;
  if (buflen < 0x20)
    buflen = 0x20;

  uint8_t *challresp = rsa_apply(buf, buflen, &resplen, RSA_MODE_AUTH);
  char *encoded = base64_enc(challresp, resplen);
  if (encoded == NULL)
    die("could not allocate memory for \"encoded\"");
  // strip the padding.
  char *padding = strchr(encoded, '=');
  if (padding)
    *padding = 0;

  msg_add_header(resp, "Apple-Response", encoded); // will be freed when the response is freed.
  free(challresp);
  free(encoded);
}

static char *make_nonce(void) {
  uint8_t random[8];
  int fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
    die("could not open /dev/urandom!");
  // int ignore =
  if (read(fd, random, sizeof(random)) != sizeof(random))
    debug(1, "Error reading /dev/urandom");
  close(fd);
  return base64_enc(random, 8); // returns a pointer to malloc'ed memory
}

static int rtsp_auth(char **nonce, rtsp_message *req, rtsp_message *resp) {

  if (!config.password)
    return 0;
  if (!*nonce) {
    *nonce = make_nonce();
    goto authenticate;
  }

  char *hdr = msg_get_header(req, "Authorization");
  if (!hdr || strncmp(hdr, "Digest ", 7))
    goto authenticate;

  char *realm = strstr(hdr, "realm=\"");
  char *username = strstr(hdr, "username=\"");
  char *response = strstr(hdr, "response=\"");
  char *uri = strstr(hdr, "uri=\"");

  if (!realm || !username || !response || !uri)
    goto authenticate;

  char *quote;
  realm = strchr(realm, '"') + 1;
  if (!(quote = strchr(realm, '"')))
    goto authenticate;
  *quote = 0;
  username = strchr(username, '"') + 1;
  if (!(quote = strchr(username, '"')))
    goto authenticate;
  *quote = 0;
  response = strchr(response, '"') + 1;
  if (!(quote = strchr(response, '"')))
    goto authenticate;
  *quote = 0;
  uri = strchr(uri, '"') + 1;
  if (!(quote = strchr(uri, '"')))
    goto authenticate;
  *quote = 0;

  uint8_t digest_urp[16], digest_mu[16], digest_total[16];

#ifdef CONFIG_OPENSSL
  MD5_CTX ctx;

  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  MD5_Init(&ctx);
  MD5_Update(&ctx, username, strlen(username));
  MD5_Update(&ctx, ":", 1);
  MD5_Update(&ctx, realm, strlen(realm));
  MD5_Update(&ctx, ":", 1);
  MD5_Update(&ctx, config.password, strlen(config.password));
  MD5_Final(digest_urp, &ctx);
  MD5_Init(&ctx);
  MD5_Update(&ctx, req->method, strlen(req->method));
  MD5_Update(&ctx, ":", 1);
  MD5_Update(&ctx, uri, strlen(uri));
  MD5_Final(digest_mu, &ctx);
  pthread_setcancelstate(oldState, NULL);
#endif

#ifdef CONFIG_MBEDTLS
#if MBEDTLS_VERSION_MINOR >= 7
  mbedtls_md5_context tctx;
  mbedtls_md5_starts_ret(&tctx);
  mbedtls_md5_update_ret(&tctx, (const unsigned char *)username, strlen(username));
  mbedtls_md5_update_ret(&tctx, (unsigned char *)":", 1);
  mbedtls_md5_update_ret(&tctx, (const unsigned char *)realm, strlen(realm));
  mbedtls_md5_update_ret(&tctx, (unsigned char *)":", 1);
  mbedtls_md5_update_ret(&tctx, (const unsigned char *)config.password, strlen(config.password));
  mbedtls_md5_finish_ret(&tctx, digest_urp);
  mbedtls_md5_starts_ret(&tctx);
  mbedtls_md5_update_ret(&tctx, (const unsigned char *)req->method, strlen(req->method));
  mbedtls_md5_update_ret(&tctx, (unsigned char *)":", 1);
  mbedtls_md5_update_ret(&tctx, (const unsigned char *)uri, strlen(uri));
  mbedtls_md5_finish_ret(&tctx, digest_mu);
#else
  mbedtls_md5_context tctx;
  mbedtls_md5_starts(&tctx);
  mbedtls_md5_update(&tctx, (const unsigned char *)username, strlen(username));
  mbedtls_md5_update(&tctx, (unsigned char *)":", 1);
  mbedtls_md5_update(&tctx, (const unsigned char *)realm, strlen(realm));
  mbedtls_md5_update(&tctx, (unsigned char *)":", 1);
  mbedtls_md5_update(&tctx, (const unsigned char *)config.password, strlen(config.password));
  mbedtls_md5_finish(&tctx, digest_urp);
  mbedtls_md5_starts(&tctx);
  mbedtls_md5_update(&tctx, (const unsigned char *)req->method, strlen(req->method));
  mbedtls_md5_update(&tctx, (unsigned char *)":", 1);
  mbedtls_md5_update(&tctx, (const unsigned char *)uri, strlen(uri));
  mbedtls_md5_finish(&tctx, digest_mu);
#endif
#endif

#ifdef CONFIG_POLARSSL
  md5_context tctx;
  md5_starts(&tctx);
  md5_update(&tctx, (const unsigned char *)username, strlen(username));
  md5_update(&tctx, (unsigned char *)":", 1);
  md5_update(&tctx, (const unsigned char *)realm, strlen(realm));
  md5_update(&tctx, (unsigned char *)":", 1);
  md5_update(&tctx, (const unsigned char *)config.password, strlen(config.password));
  md5_finish(&tctx, digest_urp);
  md5_starts(&tctx);
  md5_update(&tctx, (const unsigned char *)req->method, strlen(req->method));
  md5_update(&tctx, (unsigned char *)":", 1);
  md5_update(&tctx, (const unsigned char *)uri, strlen(uri));
  md5_finish(&tctx, digest_mu);
#endif

  int i;
  unsigned char buf[33];
  for (i = 0; i < 16; i++)
    snprintf((char *)buf + 2 * i, 3, "%02x", digest_urp[i]);

#ifdef CONFIG_OPENSSL
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  MD5_Init(&ctx);
  MD5_Update(&ctx, buf, 32);
  MD5_Update(&ctx, ":", 1);
  MD5_Update(&ctx, *nonce, strlen(*nonce));
  MD5_Update(&ctx, ":", 1);
  for (i = 0; i < 16; i++)
    snprintf((char *)buf + 2 * i, 3, "%02x", digest_mu[i]);
  MD5_Update(&ctx, buf, 32);
  MD5_Final(digest_total, &ctx);
  pthread_setcancelstate(oldState, NULL);
#endif

#ifdef CONFIG_MBEDTLS
#if MBEDTLS_VERSION_MINOR >= 7
  mbedtls_md5_starts_ret(&tctx);
  mbedtls_md5_update_ret(&tctx, buf, 32);
  mbedtls_md5_update_ret(&tctx, (unsigned char *)":", 1);
  mbedtls_md5_update_ret(&tctx, (const unsigned char *)*nonce, strlen(*nonce));
  mbedtls_md5_update_ret(&tctx, (unsigned char *)":", 1);
  for (i = 0; i < 16; i++)
    snprintf((char *)buf + 2 * i, 3, "%02x", digest_mu[i]);
  mbedtls_md5_update_ret(&tctx, buf, 32);
  mbedtls_md5_finish_ret(&tctx, digest_total);
#else
  mbedtls_md5_starts(&tctx);
  mbedtls_md5_update(&tctx, buf, 32);
  mbedtls_md5_update(&tctx, (unsigned char *)":", 1);
  mbedtls_md5_update(&tctx, (const unsigned char *)*nonce, strlen(*nonce));
  mbedtls_md5_update(&tctx, (unsigned char *)":", 1);
  for (i = 0; i < 16; i++)
    snprintf((char *)buf + 2 * i, 3, "%02x", digest_mu[i]);
  mbedtls_md5_update(&tctx, buf, 32);
  mbedtls_md5_finish(&tctx, digest_total);
#endif
#endif

#ifdef CONFIG_POLARSSL
  md5_starts(&tctx);
  md5_update(&tctx, buf, 32);
  md5_update(&tctx, (unsigned char *)":", 1);
  md5_update(&tctx, (const unsigned char *)*nonce, strlen(*nonce));
  md5_update(&tctx, (unsigned char *)":", 1);
  for (i = 0; i < 16; i++)
    snprintf((char *)buf + 2 * i, 3, "%02x", digest_mu[i]);
  md5_update(&tctx, buf, 32);
  md5_finish(&tctx, digest_total);
#endif

  for (i = 0; i < 16; i++)
    snprintf((char *)buf + 2 * i, 3, "%02x", digest_total[i]);

  if (!strcmp(response, (const char *)buf))
    return 0;
  warn("Password authorization failed.");

authenticate:
  resp->respcode = 401;
  int hdrlen = strlen(*nonce) + 40;
  char *authhdr = malloc(hdrlen);
  snprintf(authhdr, hdrlen, "Digest realm=\"raop\", nonce=\"%s\"", *nonce);
  msg_add_header(resp, "WWW-Authenticate", authhdr);
  free(authhdr);
  return 1;
}

void rtsp_conversation_thread_cleanup_function(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);

  debug(3, "Connection %d: rtsp_conversation_thread_func_cleanup_function called.",
        conn->connection_number);
  if (conn->player_thread)
    player_stop(conn);

  debug(3, "Closing timing, control and audio sockets...");
  if (conn->control_socket)
    close(conn->control_socket);
  if (conn->timing_socket)
    close(conn->timing_socket);
  if (conn->audio_socket)
    close(conn->audio_socket);

  if (conn->fd > 0) {
    debug(3, "Connection %d: closing fd %d.", conn->connection_number, conn->fd);
    close(conn->fd);
    debug(3, "Connection %d: closed fd %d.", conn->connection_number, conn->fd);
  }
  if (conn->auth_nonce) {
    free(conn->auth_nonce);
    conn->auth_nonce = NULL;
  }
  rtp_terminate(conn);

  if (conn->dacp_id) {
    free(conn->dacp_id);
    conn->dacp_id = NULL;
  }

  if (conn->UserAgent) {
    free(conn->UserAgent);
    conn->UserAgent = NULL;
  }

  // remove flow control and mutexes
  int rc = pthread_mutex_destroy(&conn->volume_control_mutex);
  if (rc)
    debug(1, "Connection %d: error %d destroying volume_control_mutex.", conn->connection_number,
          rc);
  rc = pthread_cond_destroy(&conn->flowcontrol);
  if (rc)
    debug(1, "Connection %d: error %d destroying flow control condition variable.",
          conn->connection_number, rc);
  rc = pthread_mutex_destroy(&conn->ab_mutex);
  if (rc)
    debug(1, "Connection %d: error %d destroying ab_mutex.", conn->connection_number, rc);
  rc = pthread_mutex_destroy(&conn->flush_mutex);
  if (rc)
    debug(1, "Connection %d: error %d destroying flush_mutex.", conn->connection_number, rc);

  debug(3, "Cancel watchdog thread.");
  pthread_cancel(conn->player_watchdog_thread);
  debug(3, "Join watchdog thread.");
  pthread_join(conn->player_watchdog_thread, NULL);
  debug(3, "Delete watchdog mutex.");
  pthread_mutex_destroy(&conn->watchdog_mutex);

  debug(3, "Connection %d: Checking play lock.", conn->connection_number);
  debug_mutex_lock(&playing_conn_lock, 1000000, 3); // get it
  if (playing_conn == conn) {                       // if it's ours
    debug(3, "Connection %d: Unlocking play lock.", conn->connection_number);
    playing_conn = NULL; // let it go
  }
  debug_mutex_unlock(&playing_conn_lock, 3);

  debug(2, "Connection %d: terminated.", conn->connection_number);
  conn->running = 0;
  pthread_setcancelstate(oldState, NULL);
}

void msg_cleanup_function(void *arg) {
  // debug(3, "msg_cleanup_function called.");
  msg_free((rtsp_message **)arg);
}

static void *rtsp_conversation_thread_func(void *pconn) {
  rtsp_conn_info *conn = pconn;

  // create the watchdog mutex, initialise the watchdog time and start the watchdog thread;
  conn->watchdog_bark_time = get_absolute_time_in_ns();
  pthread_mutex_init(&conn->watchdog_mutex, NULL);
  pthread_create(&conn->player_watchdog_thread, NULL, &player_watchdog_thread_code, (void *)conn);

  int rc = pthread_mutex_init(&conn->flush_mutex, NULL);
  if (rc)
    die("Connection %d: error %d initialising flush_mutex.", conn->connection_number, rc);
  rc = pthread_mutex_init(&conn->ab_mutex, NULL);
  if (rc)
    die("Connection %d: error %d initialising ab_mutex.", conn->connection_number, rc);
// set the flowcontrol condition variable to wait on a monotonic clock
#ifdef COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN_AND_OPENBSD
  pthread_condattr_t attr;
  pthread_condattr_init(&attr);
  pthread_condattr_setclock(&attr, CLOCK_MONOTONIC); // can't do this in OS X, and don't need it.
  rc = pthread_cond_init(&conn->flowcontrol, &attr);
#endif
#ifdef COMPILE_FOR_OSX
  rc = pthread_cond_init(&conn->flowcontrol, NULL);
#endif
  if (rc)
    die("Connection %d: error %d initialising flow control condition variable.",
        conn->connection_number, rc);
  rc = pthread_mutex_init(&conn->volume_control_mutex, NULL);
  if (rc)
    die("Connection %d: error %d initialising volume_control_mutex.", conn->connection_number, rc);

  // nothing before this is cancellable
  pthread_cleanup_push(rtsp_conversation_thread_cleanup_function, (void *)conn);

  rtp_initialise(conn);
  char *hdr = NULL;

  enum rtsp_read_request_response reply;

  int rtsp_read_request_attempt_count = 1; // 1 means exit immediately
  rtsp_message *req, *resp;

  while (conn->stop == 0) {
    int debug_level = 3; // for printing the request and response
    reply = rtsp_read_request(conn, &req);
    if (reply == rtsp_read_request_response_ok) {
      pthread_cleanup_push(msg_cleanup_function, (void *)&req);
      resp = msg_init();
      pthread_cleanup_push(msg_cleanup_function, (void *)&resp);
      resp->respcode = 400;

      if (strcmp(req->method, "OPTIONS") !=
          0) // the options message is very common, so don't log it until level 3
        debug_level = 2;
      debug(debug_level,
            "Connection %d: Received an RTSP Packet of type \"%s\":", conn->connection_number,
            req->method),
          debug_print_msg_headers(debug_level, req);

      apple_challenge(conn->fd, req, resp);
      hdr = msg_get_header(req, "CSeq");
      if (hdr)
        msg_add_header(resp, "CSeq", hdr);
      //      msg_add_header(resp, "Audio-Jack-Status", "connected; type=analog");
      msg_add_header(resp, "Server", "AirTunes/105.1");

      if ((conn->authorized == 1) || (rtsp_auth(&conn->auth_nonce, req, resp)) == 0) {
        conn->authorized = 1; // it must have been authorized or didn't need a password
        struct method_handler *mh;
        int method_selected = 0;
        for (mh = method_handlers; mh->method; mh++) {
          if (!strcmp(mh->method, req->method)) {
            method_selected = 1;
            mh->handler(conn, req, resp);
            break;
          }
        }
        if (method_selected == 0) {
          debug(3, "Connection %d: Unrecognised and unhandled rtsp request \"%s\".",
                conn->connection_number, req->method);

          int y = req->contentlength;
          if (y > 0) {
            char obf[4096];
            if (y > 4096)
              y = 4096;
            char *p = req->content;
            char *obfp = obf;
            int obfc;
            for (obfc = 0; obfc < y; obfc++) {
              snprintf(obfp, 3, "%02X", (unsigned int)*p);
              p++;
              obfp += 2;
            };
            *obfp = 0;
            debug(3, "Content: \"%s\".", obf);
          }
        }
      }
      debug(debug_level, "Connection %d: RTSP Response:", conn->connection_number);
      debug_print_msg_headers(debug_level, resp);

      if (conn->stop == 0) {
        int err = msg_write_response(conn->fd, resp);
        if (err) {
          debug(1,
                "Connection %d: Unable to write an RTSP message response. Terminating the "
                "connection.",
                conn->connection_number);
          struct linger so_linger;
          so_linger.l_onoff = 1; // "true"
          so_linger.l_linger = 0;
          err = setsockopt(conn->fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof so_linger);
          if (err)
            debug(1, "Could not set the RTSP socket to abort due to a write error on closing.");
          conn->stop = 1;
          // if (debuglev >= 1)
          //  debuglev = 3; // see what happens next
        }
      }
      pthread_cleanup_pop(1);
      pthread_cleanup_pop(1);
    } else {
      int tstop = 0;
      if (reply == rtsp_read_request_response_immediate_shutdown_requested)
        tstop = 1;
      else if ((reply == rtsp_read_request_response_channel_closed) ||
               (reply == rtsp_read_request_response_read_error)) {
        if (conn->player_thread) {
          rtsp_read_request_attempt_count--;
          if (rtsp_read_request_attempt_count == 0) {
            tstop = 1;
            if (reply == rtsp_read_request_response_read_error) {
              struct linger so_linger;
              so_linger.l_onoff = 1; // "true"
              so_linger.l_linger = 0;
              int err = setsockopt(conn->fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof so_linger);
              if (err)
                debug(1, "Could not set the RTSP socket to abort due to a read error on closing.");
            }
            // debuglev = 3; // see what happens next
          } else {
            if (reply == rtsp_read_request_response_channel_closed)
              debug(2,
                    "Connection %d: RTSP channel unexpectedly closed -- will try again %d time(s).",
                    conn->connection_number, rtsp_read_request_attempt_count);
            if (reply == rtsp_read_request_response_read_error)
              debug(2, "Connection %d: RTSP channel read error -- will try again %d time(s).",
                    conn->connection_number, rtsp_read_request_attempt_count);
            usleep(20000);
          }
        } else {
          tstop = 1;
        }
      } else if (reply == rtsp_read_request_response_bad_packet) {
        char *response_text = "RTSP/1.0 400 Bad Request\r\nServer: AirTunes/105.1\r\n\r\n";
        ssize_t reply = write(conn->fd, response_text, strlen(response_text));
        if (reply == -1) {
          char errorstring[1024];
          strerror_r(errno, (char *)errorstring, sizeof(errorstring));
          debug(1, "rtsp_read_request_response_bad_packet write response error %d: \"%s\".", errno,
                (char *)errorstring);
        } else if (reply != (ssize_t)strlen(response_text)) {
          debug(1, "rtsp_read_request_response_bad_packet write %d bytes requested but %d written.",
                strlen(response_text), reply);
        }
      } else {
        debug(1, "Connection %d: rtsp_read_request error %d, packet ignored.",
              conn->connection_number, (int)reply);
      }
      if (tstop) {
        debug(3, "Connection %d: Terminate RTSP connection.", conn->connection_number);
        conn->stop = 1;
      }
    }
  }
  pthread_cleanup_pop(1);
  pthread_exit(NULL);
}

/*
// this function is not thread safe.
static const char *format_address(struct sockaddr *fsa) {
  static char string[INETx_ADDRSTRLEN];
  void *addr;
#ifdef AF_INET6
  if (fsa->sa_family == AF_INET6) {
    struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)(fsa);
    addr = &(sa6->sin6_addr);
  } else
#endif
  {
    struct sockaddr_in *sa = (struct sockaddr_in *)(fsa);
    addr = &(sa->sin_addr);
  }
  return inet_ntop(fsa->sa_family, addr, string, sizeof(string));
}
*/

void rtsp_listen_loop_cleanup_handler(__attribute__((unused)) void *arg) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  debug(2, "rtsp_listen_loop_cleanup_handler called.");
  cancel_all_RTSP_threads();
  int *sockfd = (int *)arg;
  mdns_unregister();
  if (sockfd)
    free(sockfd);
  pthread_setcancelstate(oldState, NULL);
}

void rtsp_listen_loop(void) {
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  struct addrinfo hints, *info, *p;
  char portstr[6];
  int *sockfd = NULL;
  int nsock = 0;
  int i, ret;

  playing_conn = NULL; // the data structure representing the connection that has the player.

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  snprintf(portstr, 6, "%d", config.port);

  // debug(1,"listen socket port request is \"%s\".",portstr);

  ret = getaddrinfo(NULL, portstr, &hints, &info);
  if (ret) {
    die("getaddrinfo failed: %s", gai_strerror(ret));
  }

  for (p = info; p; p = p->ai_next) {
    ret = 0;
    int fd = socket(p->ai_family, p->ai_socktype, IPPROTO_TCP);
    int yes = 1;

    // Handle socket open failures if protocol unavailable (or IPV6 not handled)
    if (fd == -1) {
      // debug(1, "Failed to get socket: fam=%d, %s\n", p->ai_family,
      // strerror(errno));
      continue;
    }
    // Set the RTSP socket to close on exec() of child processes
    // otherwise background run_this_before_play_begins or run_this_after_play_ends commands
    // that are sleeping prevent the daemon from being restarted because
    // the listening RTSP port is still in use.
    // See: https://github.com/mikebrady/shairport-sync/issues/329
    fcntl(fd, F_SETFD, FD_CLOEXEC);
    ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    struct timeval tv;
    tv.tv_sec = 3; // three seconds write timeout
    tv.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv) == -1)
      debug(1, "Error %d setting send timeout for rtsp writeback.", errno);

    if ((config.dont_check_timeout == 0) && (config.timeout != 0)) {
      tv.tv_sec = config.timeout; // 120 seconds read timeout by default.
      tv.tv_usec = 0;
      if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) == -1)
        debug(1, "Error %d setting read timeout for rtsp connection.", errno);
    }
#ifdef IPV6_V6ONLY
    // some systems don't support v4 access on v6 sockets, but some do.
    // since we need to account for two sockets we might as well
    // always.
    if (p->ai_family == AF_INET6) {
      ret |= setsockopt(fd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes));
    }
#endif

    if (!ret)
      ret = bind(fd, p->ai_addr, p->ai_addrlen);

    // one of the address families will fail on some systems that
    // report its availability. do not complain.

    if (ret) {
      char *family;
#ifdef AF_INET6
      if (p->ai_family == AF_INET6) {
        family = "IPv6";
      } else
#endif
        family = "IPv4";
      debug(1, "unable to listen on %s port %d. The error is: \"%s\".", family, config.port,
            strerror(errno));
      continue;
    }

    listen(fd, 5);
    nsock++;
    sockfd = realloc(sockfd, nsock * sizeof(int));
    sockfd[nsock - 1] = fd;
  }

  freeaddrinfo(info);

  if (nsock) {
    int maxfd = -1;
    fd_set fds;
    FD_ZERO(&fds);
    for (i = 0; i < nsock; i++) {
      if (sockfd[i] > maxfd)
        maxfd = sockfd[i];
    }

    mdns_register();

    pthread_setcancelstate(oldState, NULL);
    int acceptfd;
    struct timeval tv;
    pthread_cleanup_push(rtsp_listen_loop_cleanup_handler, (void *)sockfd);
    do {
      pthread_testcancel();
      tv.tv_sec = 60;
      tv.tv_usec = 0;

      for (i = 0; i < nsock; i++)
        FD_SET(sockfd[i], &fds);

      ret = select(maxfd + 1, &fds, 0, 0, &tv);
      if (ret < 0) {
        if (errno == EINTR)
          continue;
        break;
      }

      cleanup_threads();

      acceptfd = -1;
      for (i = 0; i < nsock; i++) {
        if (FD_ISSET(sockfd[i], &fds)) {
          acceptfd = sockfd[i];
          break;
        }
      }
      if (acceptfd < 0) // timeout
        continue;

      rtsp_conn_info *conn = malloc(sizeof(rtsp_conn_info));
      if (conn == 0)
        die("Couldn't allocate memory for an rtsp_conn_info record.");
      memset(conn, 0, sizeof(rtsp_conn_info));
      conn->connection_number = RTSP_connection_index++;
      socklen_t slen = sizeof(conn->remote);

      conn->fd = accept(acceptfd, (struct sockaddr *)&conn->remote, &slen);
      if (conn->fd < 0) {
        debug(1, "Connection %d: New connection on port %d not accepted:", conn->connection_number,
              config.port);
        perror("failed to accept connection");
        free(conn);
      } else {
        SOCKADDR *local_info = (SOCKADDR *)&conn->local;
        socklen_t size_of_reply = sizeof(*local_info);
        memset(local_info, 0, sizeof(SOCKADDR));
        if (getsockname(conn->fd, (struct sockaddr *)local_info, &size_of_reply) == 0) {

          // IPv4:
          if (local_info->SAFAMILY == AF_INET) {
            char ip4[INET_ADDRSTRLEN];        // space to hold the IPv4 string
            char remote_ip4[INET_ADDRSTRLEN]; // space to hold the IPv4 string
            struct sockaddr_in *sa = (struct sockaddr_in *)local_info;
            inet_ntop(AF_INET, &(sa->sin_addr), ip4, INET_ADDRSTRLEN);
            unsigned short int tport = ntohs(sa->sin_port);
            sa = (struct sockaddr_in *)&conn->remote;
            inet_ntop(AF_INET, &(sa->sin_addr), remote_ip4, INET_ADDRSTRLEN);
            unsigned short int rport = ntohs(sa->sin_port);
            debug(2, "Connection %d: new connection from %s:%u to self at %s:%u.",
                  conn->connection_number, remote_ip4, rport, ip4, tport);
          }
#ifdef AF_INET6
          if (local_info->SAFAMILY == AF_INET6) {
            // IPv6:

            char ip6[INET6_ADDRSTRLEN];        // space to hold the IPv6 string
            char remote_ip6[INET6_ADDRSTRLEN]; // space to hold the IPv6 string
            struct sockaddr_in6 *sa6 =
                (struct sockaddr_in6 *)local_info; // pretend this is loaded with something
            inet_ntop(AF_INET6, &(sa6->sin6_addr), ip6, INET6_ADDRSTRLEN);
            u_int16_t tport = ntohs(sa6->sin6_port);

            sa6 = (struct sockaddr_in6 *)&conn->remote; // pretend this is loaded with something
            inet_ntop(AF_INET6, &(sa6->sin6_addr), remote_ip6, INET6_ADDRSTRLEN);
            u_int16_t rport = ntohs(sa6->sin6_port);
            debug(2, "Connection %d: new connection from [%s]:%u to self at [%s]:%u.",
                  conn->connection_number, remote_ip6, rport, ip6, tport);
          }
#endif

        } else {
          debug(1, "Error figuring out Shairport Sync's own IP number.");
        }
        //      usleep(500000);
        //      pthread_t rtsp_conversation_thread;
        //      conn->thread = rtsp_conversation_thread;
        //      conn->stop = 0; // record's memory has been zeroed
        //      conn->authorized = 0; // record's memory has been zeroed
        // fcntl(conn->fd, F_SETFL, O_NONBLOCK);

        ret = pthread_create(&conn->thread, NULL, rtsp_conversation_thread_func,
                             conn); // also acts as a memory barrier
        if (ret) {
          char errorstring[1024];
          strerror_r(ret, (char *)errorstring, sizeof(errorstring));
          die("Connection %d: cannot create an RTSP conversation thread. Error %d: \"%s\".",
              conn->connection_number, ret, (char *)errorstring);
        }
        debug(3, "Successfully created RTSP receiver thread %d.", conn->connection_number);
        conn->running = 1; // this must happen before the thread is tracked
        track_thread(conn);
      }
    } while (1);
    pthread_cleanup_pop(1); // should never happen
  } else {
    die("could not establish a service on port %d -- program terminating. Is another instance of "
        "Shairport Sync running on this device?",
        config.port);
  }
  // debug(1, "Oops -- fell out of the RTSP select loop");
}
