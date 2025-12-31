/*
 * RTSP protocol handler. This file is part of Shairport Sync
 * Copyright (c) James Laird 2013

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

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <inttypes.h>
#include <limits.h>
#include <memory.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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

#include <sys/ioctl.h>

#include "activity_monitor.h"
#include "config.h"

#ifdef CONFIG_OPENSSL
#include <openssl/evp.h>
#include <openssl/md5.h>
#endif

#ifdef CONFIG_MBEDTLS
#include <mbedtls/md5.h>
#include <mbedtls/version.h>
#endif

#ifdef CONFIG_POLARSSL
#include <polarssl/md5.h>
#endif

#include "bonjour_strings.h"
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

#ifdef CONFIG_AIRPLAY_2
#include "ap2_buffered_audio_processor.h"
#include "ap2_event_receiver.h"
#include "ap2_rc_event_receiver.h"
#include "pair_ap/pair.h"
#include "plist/plist.h"
#include "plists/get_info_response.h"
#include "ptp-utilities.h"

#ifdef HAVE_LIBPLIST_GE_2_3_0
#define plist_from_memory(plist_data, length, plist)                                               \
  plist_from_memory((plist_data), (length), (plist), NULL)
#endif
#endif

#ifdef CONFIG_CONVOLUTION
#include "FFTConvolver/convolver.h"
#endif

#ifdef CONFIG_DBUS_INTERFACE
#include "dbus-service.h"
#endif

#include "mdns.h"
#include "utilities/network_utilities.h"

#define METADATA_SNDBUF (4 * 1024 * 1024)

enum rtsp_read_request_response {
  rtsp_read_request_response_ok,
  rtsp_read_request_response_pending,
  rtsp_read_request_response_immediate_shutdown_requested,
  rtsp_read_request_response_bad_packet,
  rtsp_read_request_response_channel_closed,
  rtsp_read_request_response_read_error,
  rtsp_read_request_response_error
};

rtsp_conn_info *principal_conn;
rtsp_conn_info **conns;

int metadata_running = 0;

// always lock this when accessing the principal conn value
// use a read lock when consulting and holding it
// use a write lock if you want to change it
pthread_rwlock_t principal_conn_lock = PTHREAD_RWLOCK_INITIALIZER;

// always lock this when accessing the list of connection threads
pthread_mutex_t conns_lock = PTHREAD_MUTEX_INITIALIZER;

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

  uint32_t contentlength;
  char *content;

  // for requests
  char method[16];
  char path[256];

  // for responses
  int respcode;
} rtsp_message;

#ifdef CONFIG_AIRPLAY_2

int add_pstring_to_malloc(const char *s, void **allocation, size_t *size) {
  int response = 0;
  void *p = *allocation;
  if (p == NULL) {
    p = malloc(strlen(s) + 1);
    if (p == NULL) {
      debug(1, "error allocating memory");
    } else {
      *allocation = p;
      *size = *size + strlen(s) + 1;
      uint8_t *b = (uint8_t *)p;
      *b = strlen(s);
      p = p + 1;
      memcpy(p, s, strlen(s));
      response = 1;
    }
  } else {
    p = realloc(p, *size + strlen(s) + 1);
    if (p == NULL) { // assuming we never allocate a zero byte space
      debug(1, "error reallocating memory");
    } else {
      *allocation = p;
      uint8_t *b = (uint8_t *)p + *size;
      *b = strlen(s);
      p = p + *size + 1;
      memcpy(p, s, strlen(s));
      *size = *size + strlen(s) + 1;
      response = 1;
    }
  }
  return response;
}

#endif

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

int send_metadata(const uint32_t type, const uint32_t code, const char *data, const uint32_t length,
                  rtsp_message *carrier, int block);

int send_ssnc_metadata(const uint32_t code, const char *data, const uint32_t length,
                       const int block) {
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
      rc = debug_mutex_lock(&the_queue->pc_queue_lock, 50000, 1);
    if (rc)
      debug(1, "Error %d (\"%s\") locking for pc_queue_add_item. Block is %d.", rc, strerror(rc),
            block);
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
    rc = debug_mutex_lock(&the_queue->pc_queue_lock, 50000, 1);
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

// The principal_conn variable points to the connection that
// controls the mDNS status and flags and that is potentially
// in control of the playing subsystem to output audio to a backend
// the principal_conn variable may be NULL

// the principal_conn is set by an ANNOUNCE message (Classic AirPlay) or
// by the initial SETUP (of a connection, not of a play session) message (AirPlay 2) and cleared
// when a session is terminated (AirPlay 2)

// In AirPlay 2, only one PTP connection can be live at any time, and it is the principal_conn.
// This is because, in AirPlay 2, the principal_conn connection
// also has control of the mDNS interface, and thus determines the state of the player as seen by
// other devices.

void release_play_lock(rtsp_conn_info *conn) {
  // no need thread cancellation points in here
  pthread_rwlock_wrlock(&principal_conn_lock);
  if ((principal_conn == conn) || (conn == NULL)) { // if we have the player
    if (principal_conn != NULL) {
#ifdef CONFIG_AIRPLAY_2
      config.airplay_statusflags &= (0xffffffff - (1 << 11)); // DeviceSupportsRelay
      if (principal_conn->airplay_gid) {
        free(principal_conn->airplay_gid);
        principal_conn->airplay_gid = NULL; // stop using the client's GID as our GID.
      }
      build_bonjour_strings(principal_conn);
      mdns_update(NULL, secondary_txt_records);
#endif
      debug(2, "Connection %d: %s released principal_conn.", conn->connection_number,
            get_category_string(conn->airplay_stream_category));
    }
    principal_conn = NULL; // let it go
  }
  pthread_rwlock_unlock(&principal_conn_lock);
}

// stop the current principal_conn from playing if necessary and make conn the principal_conn.

play_lock_r get_play_lock(rtsp_conn_info *conn, int allow_session_interruption) {
  play_lock_r response = play_lock_aquisition_failed;
  if (conn != NULL) {
    debug(2, "Connection %d: %s get_play_lock with allow_session_interruption of %d.",
          conn->connection_number, get_category_string(conn->airplay_stream_category),
          allow_session_interruption);

    pthread_rwlock_wrlock(&principal_conn_lock);
    pthread_cleanup_push(rwlock_unlock, (void *)&principal_conn_lock);

    if (principal_conn == conn) {
      debug(2, "Connection %d: %s already has principal_conn.", principal_conn->connection_number,
            get_category_string(conn->airplay_stream_category));
    } else {
      if (principal_conn != NULL)
        debug(2, "Connection %d: %s is requested to relinquish principal_conn.",
              principal_conn->connection_number,
              get_category_string(conn->airplay_stream_category));
      if (conn != NULL)
        debug(2, "Connection %d: %s request to acquire principal_conn.", conn->connection_number,
              get_category_string(conn->airplay_stream_category));
    }

    if (principal_conn == conn) {
      if (conn == NULL)
        response = play_lock_already_released;
      else
        response = play_lock_already_acquired;
    } else if (principal_conn == NULL) {
      // already unlocked, and principal conn not NULL
      principal_conn = conn;
#ifdef CONFIG_AIRPLAY_2
      config.airplay_statusflags |= (1 << 11); // DeviceSupportsRelay
#endif
      response = play_lock_acquired_without_breaking_in;
    } else if (allow_session_interruption != 0) { // principal conn not NULL,
      // important -- demote the principal conn before cancelling it
      if (principal_conn->fd > 0) {
        debug(2,
              "Connection %d: %s is acquiring play_lock and is forcing termination of Connection "
              "%d %s. Closing "
              "RTSP connection socket %d: "
              "from %s:%u to self at "
              "%s:%u.",
              conn->connection_number, get_category_string(conn->airplay_stream_category),
              principal_conn->connection_number,
              get_category_string(principal_conn->airplay_stream_category), principal_conn->fd,
              principal_conn->client_ip_string, principal_conn->client_rtsp_port,
              principal_conn->self_ip_string, principal_conn->self_rtsp_port);
        close(principal_conn->fd);
        // principal_conn->fd = 0;
      }
      rtsp_conn_info *previous_principal_conn = principal_conn;
      principal_conn = conn;                           // make the conn the new principal_conn
      pthread_cancel(previous_principal_conn->thread); // cancel the previous one...

      if (principal_conn == NULL) {
#ifdef CONFIG_AIRPLAY_2
        config.airplay_statusflags &= (0xffffffff - (1 << 11)); // DeviceSupportsRelay
        if (conn->airplay_gid) {
          free(conn->airplay_gid);
          conn->airplay_gid = NULL; // stop using the client's GID as our GID.
        }
        build_bonjour_strings(conn);
        mdns_update(NULL, secondary_txt_records);
#endif
        response = play_lock_released;
      } else {
#ifdef CONFIG_AIRPLAY_2
        config.airplay_statusflags |= (1 << 11); // DeviceSupportsRelay
#endif
        response = play_lock_acquired_by_breaking_in;
      }
      // usleep(1000000); // don't know why this delay is needed.
    }
    if ((principal_conn != NULL) && (response != play_lock_already_acquired))
      debug(2, "Connection %d: %s has principal_conn.", conn->connection_number,
            get_category_string(conn->airplay_stream_category));
    pthread_cleanup_pop(1); // release the principal_conn lock

  } else {
    debug(1, "Connection %d: %s get_play_lock must have a non-NULL conn.", conn->connection_number,
            get_category_string(conn->airplay_stream_category));
  }
  return response;
}

// keep track of the threads we have spawned so we can join() them
static int nconns = 0;
static void track_thread(rtsp_conn_info *conn) {
  debug_mutex_lock(&conns_lock, 1000000, 3);
  // look for an empty slot first
  int i = 0;
  int found = 0;
  while ((i < nconns) && (found == 0)) {
    if (conns[i] == NULL)
      found = 1;
    else
      i++;
  }
  if (found != 0) {
    conns[i] = conn;
  } else {
    // make space for a new element
    conns = realloc(conns, sizeof(rtsp_conn_info *) * (nconns + 1));
    if (conns) {
      conns[nconns] = conn;
      nconns++;
    } else {
      die("could not reallocate memory for conns");
    }
  }
  debug_mutex_unlock(&conns_lock, 3);
}

// note: connection numbers start at 1, so an except_this_one value of zero means "all threads"
void cancel_all_RTSP_threads(airplay_stream_c stream_category, int except_this_one) {
  // if the stream category is unspecified_stream_category
  // all categories are elegible for cancellation
  // otherwise just the category itself
  debug_mutex_lock(&conns_lock, 1000000, 3);
  int i;
  for (i = 0; i < nconns; i++) {
    if ((conns[i] != NULL) && (conns[i]->running != 0) &&
        (conns[i]->connection_number != except_this_one) &&
        ((conns[i]->airplay_stream_category == stream_category) ||
         (stream_category == unspecified_stream_category))) {
      pthread_cancel(conns[i]->thread);
      debug(2, "Connection %d: %s cancelled.", conns[i]->connection_number,
            get_category_string(conns[i]->airplay_stream_category));
    }
  }
  for (i = 0; i < nconns; i++) {
    if ((conns[i] != NULL) && (conns[i]->connection_number != except_this_one) &&
        ((conns[i]->airplay_stream_category == stream_category) ||
         (stream_category == unspecified_stream_category))) {
      debug(2, "Connection %d: %s joining....", conns[i]->connection_number,
            get_category_string(conns[i]->airplay_stream_category));
      pthread_join(conns[i]->thread, NULL);
      debug(2, "Connection %d: %s joined.", conns[i]->connection_number,
            get_category_string(conns[i]->airplay_stream_category));
      free(conns[i]);
      conns[i] = NULL;
    }
  }
  debug_mutex_unlock(&conns_lock, 3);
}

int old_connection_count = -1;

void cleanup_threads(void) {

  void *retval;
  int i;
  int connection_count = 0;
  // debug(2, "culling threads.");
  debug_mutex_lock(&conns_lock, 1000000, 3);
  for (i = 0; i < nconns; i++) {
    if ((conns[i] != NULL) && (conns[i]->running == 0)) {
      debug(3, "found RTSP connection thread %d in a non-running state.",
            conns[i]->connection_number);
      pthread_join(conns[i]->thread, &retval);
      debug(3, "Connection %d: deleted in cleanup.", conns[i]->connection_number);
      free(conns[i]);
      conns[i] = NULL;
    }
    if (conns[i] != NULL) {
      debug(3, "Airplay Volume for connection %d is %.6f.", conns[i]->connection_number,
            suggested_volume(conns[i]));
      connection_count++;
    }
  }
  debug_mutex_unlock(&conns_lock, 3);

  if (old_connection_count != connection_count) {
    if (connection_count == 0) {
      debug(3, "No active connections.");
    } else if (connection_count == 1)
      debug(3, "One active connection.");
    else
      debug(3, "%d active connections.", connection_count);
    old_connection_count = connection_count;
  }
  debug(3, "Airplay Volume for new connections is %.6f.", suggested_volume(NULL));
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
  int rc = debug_mutex_lock(&reference_counter_lock, 500000, 1);
  if (rc)
    debug(1, "Error %d locking reference counter lock", rc);
  if (msg > (rtsp_message *)0x00010000) {
    msg->referenceCount++;
    debug(3, "msg_free increment reference counter message %d to %d.", msg->index_number,
          msg->referenceCount);
    // debug(1,"msg_retain -- item %d reference count %d.", msg->index_number, msg->referenceCount);
    rc = pthread_mutex_unlock(&reference_counter_lock);
    if (rc)
      debug(1, "Error %d unlocking reference counter lock", rc);
  } else {
    debug(1, "invalid rtsp_message pointer 0x%" PRIxPTR " passed to retain", (uintptr_t)msg);
  }
}

rtsp_message *msg_init(void) {
  // no thread cancellation points here
  int rc = debug_mutex_lock(&reference_counter_lock, 500000, 1);
  if (rc)
    debug(1, "Error %d locking reference counter lock", rc);

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

  rc = pthread_mutex_unlock(&reference_counter_lock);
  if (rc)
    debug(1, "Error %d unlocking reference counter lock", rc);

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

void _debug_print_msg_headers(const char *filename, const int linenumber, int level,
                              rtsp_message *msg) {
  unsigned int i;
  if (msg->respcode != 0)
    _debug(filename, linenumber, level, "  Response Code: %d.", msg->respcode);
  for (i = 0; i < msg->nheaders; i++) {
    _debug(filename, linenumber, level, "  Type: \"%s\", content: \"%s\"", msg->name[i],
           msg->value[i]);
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
          "%" PRIxPTR ".",
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

    debug(3, "RTSP/HTTP Message Received: \"%s\".", line);

    p = strtok_r(line, " ", &sp);
    if (!p)
      goto fail;
    strncpy(msg->method, p, sizeof(msg->method) - 1);

    p = strtok_r(NULL, " ", &sp);
    if (!p)
      goto fail;
    strncpy(msg->path, p, sizeof(msg->path) - 1);

    p = strtok_r(NULL, " ", &sp);
    if (!p)
      goto fail;
    if ((strcmp(p, "RTSP/1.0") != 0) && (strcmp(p, "HTTP/1.1") != 0)) {
      debug(1, "Problem with Message: \"%s\"", p);
      goto fail;
    }

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
  debug(1, "msg_handle_line fail");
  msg_free(pmsg);
  *pmsg = NULL;
  return 0;
}

#ifdef CONFIG_AIRPLAY_2

int rtsp_message_contains_plist(rtsp_message *message) {
  int reply = 0; // assume there is no plist in the message
  if ((message->contentlength >= strlen("bplist00")) &&
      (strstr(message->content, "bplist00") == message->content))
    reply = 1;
  return reply;
}

plist_t plist_from_rtsp_content(rtsp_message *message) {
  plist_t the_plist = NULL;
  if (rtsp_message_contains_plist(message)) {
    plist_from_memory(message->content, message->contentlength, &the_plist);
  }
  return the_plist;
}

char *plist_as_xml_text(plist_t the_plist) {
  // caller must free the returned character buffer
  // convert it to xml format
  uint32_t size;
  char *plist_out = NULL;
  plist_to_xml(the_plist, &plist_out, &size);

  // put it into a NUL-terminated string
  char *reply = malloc(size + 1);
  if (reply) {
    memcpy(reply, plist_out, size);
    reply[size] = '\0';
  }
  if (plist_out)
    free(plist_out);
  return reply;
}

// caller must free the returned character buffer
char *rtsp_plist_content(rtsp_message *message) {
  char *reply = NULL;
  // first, check if it has binary plist content
  if (rtsp_message_contains_plist(message))
    reply = plist_as_xml_text(plist_from_rtsp_content(message));
  return reply;
}

#endif

void _debug_log_rtsp_message(const char *filename, const int linenumber, int level, char *prompt,
                             rtsp_message *message) {
  if (level > debug_level())
    return;
  if ((prompt) && (*prompt != '\0')) // okay to pass NULL or an empty list...
    _debug(filename, linenumber, level, "%s", prompt);
  _debug_print_msg_headers(filename, linenumber, level, message);
#ifdef CONFIG_AIRPLAY_2
  char *plist_content = rtsp_plist_content(message);
  if (plist_content) {
    _debug(filename, linenumber, level, "  Content Plist (as XML):\n--\n%s--", plist_content);
    free(plist_content);
  } else
#endif
  {
    _debug(filename, linenumber, level, "  No Content Plist. Content length: %d.",
           message->contentlength);
  }
}

#define debug_log_rtsp_message(level, prompt, message)                                             \
  _debug_log_rtsp_message(__FILE__, __LINE__, level, prompt, message)
#define debug_print_msg_headers(level, message)                                                    \
  _debug_print_msg_headers(__FILE__, __LINE__, level, message)

#ifdef CONFIG_AIRPLAY_2
static void buf_add(sized_buffer *buf, uint8_t *in, size_t in_len) {
  if (buf->length + in_len > buf->size) {
    buf->size = buf->length + in_len + 2048; // Extra headroom to avoid future memcpy's
    buf->data = realloc(buf->data, buf->size);
  }
  memcpy(buf->data + buf->length, in, in_len);
  buf->length += in_len;
}

static void buf_drain(sized_buffer *buf, ssize_t len) {
  if (len < 0 || (size_t)len >= buf->length) {
    free(buf->data);
    memset(buf, 0, sizeof(sized_buffer));
    return;
  }
  memmove(buf->data, buf->data + len, buf->length - len);
  buf->length -= len;
}

static size_t buf_remove(sized_buffer *buf, uint8_t *out, size_t out_len) {
  size_t bytes = (buf->length > out_len) ? out_len : buf->length;
  memcpy(out, buf->data, bytes);
  buf_drain(buf, bytes);
  return bytes;
}

ssize_t read_encrypted(int fd, pair_cipher_bundle *ctx, void *buf, size_t count) {
  ssize_t response = 0;
  // If there is leftover decoded content from the last pass just return that
  if (ctx->plaintext_read_buffer.length > 0) {
    response = buf_remove(&ctx->plaintext_read_buffer, buf, count);
  } else {

    // Otherwise read stuff in...
    uint8_t in[4096];
    uint8_t *plain = NULL; // may be allocated and reallocated by pair_decrypt
    pthread_cleanup_push(malloc_cleanup, &plain);
    size_t plain_len = 0;
    do {
      response = read(fd, in, sizeof(in));
      if (response > 0) {
        buf_add(&ctx->encrypted_read_buffer, in, response);
        ssize_t consumed = pair_decrypt(&plain, &plain_len, ctx->encrypted_read_buffer.data,
                                        ctx->encrypted_read_buffer.length, ctx->cipher_ctx);
        if (consumed < 0) {
          debug(1, "read_encrypted: abnormal exit from pair_decrypt: %zd.", consumed);
          response = -1;
        } else {
          buf_drain(&ctx->encrypted_read_buffer, consumed);
        }
      }
    } while ((plain_len == 0) && (response > 0));

    if (response >= 0) {
      buf_add(&ctx->plaintext_read_buffer, plain, plain_len);
      response = buf_remove(&ctx->plaintext_read_buffer, buf, count);
    }
    pthread_cleanup_pop(1);
  }

  return response;
}

ssize_t write_encrypted(int fd, pair_cipher_bundle *ctx, const void *buf, size_t count) {
  uint8_t *encrypted;
  size_t encrypted_len;

  ssize_t ret = pair_encrypt(&encrypted, &encrypted_len, buf, count, ctx->cipher_ctx);
  if (ret < 0) {
    debug(1, "%s", pair_cipher_errmsg(ctx->cipher_ctx));
    return -1;
  }

  size_t remain = encrypted_len;
  // debug(1, "write to the \"%s\" channel", ctx->description);
  // debug_print_buffer(1, (void *)buf, count);
  // debug(1, "write encrypted:");
  // debug_print_buffer(1, encrypted, encrypted_len);
  while (remain > 0) {
    ssize_t wrote = write(fd, encrypted + (encrypted_len - remain), remain);
    if (wrote <= 0) {
      free(encrypted);
      return wrote;
    }
    remain -= wrote;
  }
  free(encrypted);
  return count;
}

#endif

ssize_t read_from_rtsp_connection(rtsp_conn_info *conn, void *buf, size_t count) {
  if (count == 0)
    debug(1, "asking to read zero bytes!");

  ssize_t result = 0; // closed
  if (conn->fd > 0) {
#ifdef CONFIG_AIRPLAY_2
    if (conn->ap2_pairing_context.control_cipher_bundle.cipher_ctx) {
      conn->ap2_pairing_context.control_cipher_bundle.is_encrypted = 1;
      result =
          read_encrypted(conn->fd, &conn->ap2_pairing_context.control_cipher_bundle, buf, count);

    } else {
      result = read(conn->fd, buf, count);
    }
#else
    result = read(conn->fd, buf, count);
    // In AP1, the RTSP connection is closed in this way, so it's not unexpected
#endif
    if ((result <= 0) && (errno != 0)) {
      char errorstring[1024];
      strerror_r(errno, (char *)errorstring, sizeof(errorstring));
      debug(3, "read_from_rtsp_connection error %d \"%s\" attempting to read up to %zu bytes.",
            errno, errorstring, count);
    }
  } else {
    debug(1, "Connection %d: attempt to read from a closed RTSP connection.",
          conn->connection_number);
  }
  return result;
}

#ifdef CONFIG_AIRPLAY_2
void set_client_as_ptp_clock(rtsp_conn_info *conn) {
  char timing_list_message[4096] = "";
  strncat(timing_list_message, "T ", sizeof(timing_list_message) - 1 - strlen(timing_list_message));
  strncat(timing_list_message, (const char *)&conn->client_ip_string,
          sizeof(timing_list_message) - 1 - strlen(timing_list_message));
  ptp_send_control_message_string(timing_list_message);
}
#endif

enum rtsp_read_request_response rtsp_read_request(rtsp_conn_info *conn, rtsp_message **the_packet) {
  enum rtsp_read_request_response reply = rtsp_read_request_response_pending;
  *the_packet = NULL; // need this for error handling
  ssize_t buflen = 4096;
#ifdef CONFIG_METADATA
  if ((config.metadata_enabled != 0) && (config.get_coverart != 0))
    buflen = 1024 * 256; // big enough for typical picture data, which will be base64 encoded
#endif
  int release_buffer = 0;         // on exit, don't deallocate the buffer if everything was okay
  char *buf = malloc(buflen + 1); // add a NUL at the end
  if (buf == NULL) {
    debug(1, "Connection %d: rtsp_read_request: can't get a buffer.", conn->connection_number);
    reply = rtsp_read_request_response_error;
  } else {
    debug(3, "buf is allocated at 0x%" PRIxPTR ".", (uintptr_t)buf);
    pthread_cleanup_push(malloc_cleanup, &buf);
    ssize_t nread;
    ssize_t inbuf = 0;
    int msg_size = -1;

    while ((msg_size < 0) && (reply == rtsp_read_request_response_pending)) {
      if (conn->stop != 0) {
        debug(3, "Connection %d: shutdown requested by client.", conn->connection_number);
        reply = rtsp_read_request_response_immediate_shutdown_requested;
        // goto shutdown;
      } else {

        nread = read_from_rtsp_connection(conn, buf + inbuf, buflen - inbuf);

        if (nread <= 0) {
          // ETIMEDOUT seems to be from the keepalive having failed.
          // But it does seem as it it's not always sent, e.g. if another read() is outstanding (?)
          // EAGAIN seems to be simply from the read() request timing out.
          if (errno == ETIMEDOUT) {
            debug(1,
                  "Connection %d has disappeared. As Yeats almost said, \"Too long a "
                  "silence / can make a stone "
                  "of the heart\". ETIMEOUT",
                  conn->connection_number);
            reply = rtsp_read_request_response_immediate_shutdown_requested;
          } else if (nread == 0) {
            if (errno == 0) {
              // a blocking read that returns zero means eof -- implies connection closed by client
              debug(2, "Connection %d RTSP closed by client.", conn->connection_number);
            } else {
              char errorstring[1024];
              strerror_r(errno, (char *)errorstring, sizeof(errorstring));
              debug(2, "Connection %d RTSP port closed by client with error %d: \"%s\".",
                    conn->connection_number, errno, (char *)errorstring);
            }
            close(conn->fd); // close it from our end too...
            conn->fd = 0;
            reply = rtsp_read_request_response_channel_closed;
          } else {
            char errorstring[1024];
            strerror_r(errno, (char *)errorstring, sizeof(errorstring));
            debug(1, "Connection %d: rtsp_read_request_response_read_error %d: \"%s\".",
                  conn->connection_number, errno, (char *)errorstring);
            reply = rtsp_read_request_response_read_error;
          }
          // goto shutdown;
        } else {

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
          while ((reply == rtsp_read_request_response_pending) && (msg_size < 0) &&
                 (next = nextline(buf, inbuf))) {
            msg_size = msg_handle_line(the_packet, buf);
            if (!(*the_packet)) {
              debug(1, "Connection %d: rtsp_read_request can't find an RTSP header.",
                    conn->connection_number);
              reply = rtsp_read_request_response_bad_packet;
              // goto shutdown;
            } else {
              inbuf -= next - buf;
              if (inbuf)
                memmove(buf, next, inbuf);
            }
          }
        }
      }
    }

    if ((reply == rtsp_read_request_response_pending) && (msg_size > 0)) {
      // more input is needed...
      uint64_t threshold_time =
          get_absolute_time_in_ns() + ((uint64_t)15000000000); // i.e. fifteen seconds from now
      int warning_message_sent = 0;

      if (msg_size > buflen) {
        buf = realloc(buf, msg_size + 1);
        if (buf == NULL) {
          warn("Connection %d: too much content.", conn->connection_number);
          reply = rtsp_read_request_response_error;
          // goto shutdown;
        } else {
          debug(3, "buf is reallocated at 0x%" PRIxPTR ".", (uintptr_t)buf);
          buflen = msg_size;
        }
      }

      // const size_t max_read_chunk = 1024 * 1024 / 16;
      while ((inbuf < msg_size) && (reply == rtsp_read_request_response_pending)) {

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
          // goto shutdown;
        } else {
          size_t read_chunk = msg_size - inbuf;
          // if (read_chunk > max_read_chunk)
          //  read_chunk = max_read_chunk;
          // usleep(80000); // wait about 80 milliseconds between reads of up to max_read_chunk
          nread = read_from_rtsp_connection(conn, buf + inbuf, read_chunk);

          if (nread <= 0) {
            // ETIMEDOUT seems to be from the keepalive having failed.
            // But it does seem as it it's not always sent, e.g. if another read() is outstanding
            // (?) EAGAIN seems to be simply from the read() request timing out.
            if (errno == ETIMEDOUT) {
              debug(1,
                    "Connection %d has disappeared. As Yeats almost said, \"Too long a "
                    "silence / can make a stone "
                    "of the heart\". ETIMEOUT",
                    conn->connection_number);
              reply = rtsp_read_request_response_immediate_shutdown_requested;
              // Note: the socket will be closed when the thread exits
            } else if (nread == 0) {
              if (errno == 0) {
                // a blocking read that returns zero means eof -- implies connection closed by
                // client
                debug(1, "Connection %d closed by client.", conn->connection_number);
              } else {
                char errorstring[1024];
                strerror_r(errno, (char *)errorstring, sizeof(errorstring));
                debug(1, "Connection %d closed by client with error %d: \"%s\".",
                      conn->connection_number, errno, (char *)errorstring);
              }
              reply = rtsp_read_request_response_channel_closed;
              // Note: the socket will be closed when the thread exits
            } else {
              char errorstring[1024];
              strerror_r(errno, (char *)errorstring, sizeof(errorstring));
              debug(1, "Connection %d: rtsp_read_request_response_read_error %d: \"%s\".",
                    conn->connection_number, errno, (char *)errorstring);
              reply = rtsp_read_request_response_read_error;
            }
            // goto shutdown;
          } else {
            inbuf += nread;
          }
        }
      }
    }
    if (reply == rtsp_read_request_response_pending) {
      reply = rtsp_read_request_response_ok;
      rtsp_message *msg = *the_packet;
      msg->contentlength = inbuf;
      msg->content = buf;
      char *jp = inbuf + buf;
      *jp = '\0';
      *the_packet = msg;
    }

    // shutdown:
    if (reply != rtsp_read_request_response_ok) {
      if (*the_packet != NULL) {
        debug(3, "Freeing the_packet");
        msg_free(the_packet);
      }
      release_buffer = 1; // allow the buffer to be released
    }
    pthread_cleanup_pop(release_buffer);
  }
  return reply;
}

int msg_write_response(rtsp_conn_info *conn, rtsp_message *resp) {
  char pkt[4096];
  int pktfree = sizeof(pkt);
  char *p = pkt;
  int n;
  unsigned int i;

  struct response_t {
    int code;
    char *string;
  };

  struct response_t responses[] = {{200, "OK"},
                                   {400, "Bad Request"},
                                   {403, "Unauthorized"},
                                   {404, "Not Found"},
                                   {451, "Unavailable"},
                                   {456, "Header Field Not Valid for Resource"},
                                   {470, "Connection Authorization Required"},
                                   {500, "Internal Server Error"},
                                   {501, "Not Implemented"}};
  // 451 is really "Unavailable For Legal Reasons"!
  int found = 0;
  char *respcode_text = "Unauthorized";
  for (i = 0; i < sizeof(responses) / sizeof(struct response_t); i++) {
    if (resp->respcode == responses[i].code) {
      found = 1;
      respcode_text = responses[i].string;
    }
  }

  if (found == 0)
    debug(1, "can't find text for response code %d. Using \"%s\" instead.", resp->respcode,
          respcode_text);

  n = snprintf(p, pktfree, "RTSP/1.0 %d %s\r\n", resp->respcode, respcode_text);
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

  // if (resp->contentlength) {
  {
    // debug(2, "Responding with content of length %d", resp->contentlength);
    n = snprintf(p, pktfree, "Content-Length: %d\r\n", resp->contentlength);
    pktfree -= n;
    p += n;
    if (pktfree <= 1024) {
      debug(1, "Attempted to write overlong RTSP packet 2");
      return -2;
    }
  }

  n = snprintf(p, pktfree, "\r\n");
  pktfree -= n;
  p += n;

  if (resp->contentlength) {
    memcpy(p, resp->content, resp->contentlength);
    pktfree -= resp->contentlength;
    p += resp->contentlength;
  }

  if (pktfree <= 1024) {
    debug(1, "Attempted to write overlong RTSP packet 3");
    return -3;
  }

  // here, if the link is encrypted, better do it

#ifdef CONFIG_AIRPLAY_2
  ssize_t reply;
  if (conn->ap2_pairing_context.control_cipher_bundle.is_encrypted) {
    reply =
        write_encrypted(conn->fd, &conn->ap2_pairing_context.control_cipher_bundle, pkt, p - pkt);
  } else {
    reply = write(conn->fd, pkt, p - pkt);
  }
#else
  ssize_t reply = write(conn->fd, pkt, p - pkt);
#endif

  if (reply == -1) {
    char errorstring[1024];
    strerror_r(errno, (char *)errorstring, sizeof(errorstring));
    debug(1, "msg_write_response error %d: \"%s\".", errno, (char *)errorstring);
    return -4;
  }
  if (reply != p - pkt) {
    debug(1, "msg_write_response error -- requested bytes: %zd not fully written: %zd.", p - pkt,
          reply);
    return -5;
  }
  return 0;
}

#ifdef CONFIG_AIRPLAY_2
void handle_record_2(rtsp_conn_info *conn, __attribute((unused)) rtsp_message *req,
                     rtsp_message *resp) {
  debug(2, "Connection %d: RECORD on %s", conn->connection_number,
        get_category_string(conn->airplay_stream_category));
  debug_log_rtsp_message(3, "RECORD incoming message", req);
  msg_add_header(resp, "Audio-Latency", "0");
  resp->respcode = 200;
}
#endif

void handle_record(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(2, "Connection %d: RECORD", conn->connection_number);
  if ((conn != NULL) && (principal_conn == conn)) {
    if (conn->player_thread)
      warn("Connection %d: RECORD: Duplicate RECORD message -- ignored", conn->connection_number);
    else {
      activity_monitor_signify_activity(1);
      player_play(conn); // the thread better be 0
    }

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

#ifdef CONFIG_AIRPLAY_2

void generateTxtDataValueInfo(rtsp_conn_info *conn, void **response, size_t *responseLength) {
  void *qualifier_response_data = NULL;
  size_t qualifier_response_data_length = 0;
  char localString[256];
  if (add_pstring_to_malloc("acl=0", &qualifier_response_data, &qualifier_response_data_length) ==
      0)
    debug(1, "Problem");
  if (add_pstring_to_malloc("btaddr=00:00:00:00:00:00", &qualifier_response_data,
                            &qualifier_response_data_length) == 0)
    debug(1, "Problem");
  if (add_pstring_to_malloc(
          bnprintf(localString, sizeof(localString), "deviceid=%s", config.airplay_device_id),
          &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");
  if (add_pstring_to_malloc(
          bnprintf(localString, sizeof(localString), "fex=%s", config.airplay_fex),
          &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  uint64_t features_hi = config.airplay_features;
  features_hi = (features_hi >> 32) & 0xffffffff;
  uint64_t features_lo = config.airplay_features;
  features_lo = features_lo & 0xffffffff;

  if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString),
                                     "features=0x%" PRIX64 ",0x%" PRIX64 "", features_lo,
                                     features_hi),
                            &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");
  // if (add_pstring_to_malloc("rsf=0x0", &qualifier_response_data,
  //                             &qualifier_response_data_length) == 0)
  //   debug(1, "Problem");

  if (add_pstring_to_malloc(
          bnprintf(localString, sizeof(localString), "flags=0x%x", config.airplay_statusflags),
          &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if ((conn != NULL) && (conn->airplay_gid != 0)) {
    snprintf(localString, sizeof(localString), "gid=%s", conn->airplay_gid);
  } else {
    snprintf(localString, sizeof(localString), "gid=%s", config.airplay_pi);
  }

  if (add_pstring_to_malloc(localString, &qualifier_response_data,
                            &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  int gcgl = 0;
  if (conn != NULL)
    gcgl = conn->groupContainsGroupLeader;

  // should have igl here;
  if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString), "igl=%0"),
                            &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString), "gcgl=%d", gcgl),
                            &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc(
          bnprintf(localString, sizeof(localString), "pgid=%s", config.airplay_pgid),
          &qualifier_response_data, &qualifier_response_data_length) == 0)

    if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString), "pgcgl=%d", gcgl),
                              &qualifier_response_data, &qualifier_response_data_length) == 0)
      debug(1, "Problem");

  if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString), "model=%s", config.model),
                            &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");
  if (add_pstring_to_malloc("protovers=1.1", &qualifier_response_data,
                            &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString), "pi=%s", config.airplay_pi),
                            &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc(
          bnprintf(localString, sizeof(localString), "psi=%s", config.airplay_psi),
          &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString), "pk=%s", config.pk_string),
                            &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc(
          bnprintf(localString, sizeof(localString), "srcvers=%s", config.srcvers),
          &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc(bnprintf(localString, sizeof(localString), "osvers=%s", config.osvers),
                            &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  if (add_pstring_to_malloc("vv=2", &qualifier_response_data, &qualifier_response_data_length) == 0)
    debug(1, "Problem");

  *response = qualifier_response_data;
  *responseLength = qualifier_response_data_length;
}

plist_t generateInfoPlist(rtsp_conn_info *conn) {
  plist_t response_plist = NULL;

  plist_from_memory((const char *)get_info_response_plist, get_info_response_plist_len,
                    &response_plist);

  if (response_plist == NULL) {
    debug(1, "generateInfoPlist plist not created!");
  } else {
    pthread_rwlock_rdlock(&principal_conn_lock); // don't let the principal_conn be changed
    pthread_cleanup_push(rwlock_unlock, (void *)&principal_conn_lock);

    // debug(1,"qualifier_response_data_length: %u.", qualifier_response_data_length);

    plist_dict_set_item(response_plist, "featuresEx", plist_new_string(config.airplay_fex));

    plist_dict_set_item(response_plist, "features", plist_new_uint(config.airplay_features));
    plist_dict_set_item(response_plist, "statusFlags", plist_new_uint(config.airplay_statusflags));
    plist_dict_set_item(response_plist, "deviceID", plist_new_string(config.airplay_device_id));
    plist_dict_set_item(response_plist, "pi", plist_new_string(config.airplay_pi));
    plist_dict_set_item(response_plist, "name", plist_new_string(config.service_name));
    char *vs = get_version_string();
    plist_dict_set_item(response_plist, "model", plist_new_string(config.model));
    free(vs);
    plist_dict_set_item(response_plist, "pk",
                        plist_new_data((const char *)config.airplay_pk, sizeof(config.airplay_pk)));
    char senderAddress[256];
    snprintf(senderAddress, sizeof(senderAddress), "%s:%u", conn->client_ip_string,
             conn->client_rtsp_port);
    plist_dict_set_item(response_plist, "senderAddress", plist_new_string(senderAddress));
    plist_dict_set_item(response_plist, "initialVolume", plist_new_real(suggested_volume(conn)));
    plist_dict_set_item(response_plist, "sourceVersion", plist_new_string(config.srcvers));
    pthread_cleanup_pop(1); // release the principal_conn lock
    // Create a dictionary of supported formats for the bufferStream
    uint64_t bufferStreamFormats = 0L;
    // bufferStreamFormats = 0xF7FE000E00000000; // don't know what these do (from the HPm)
    plist_t supported_formats_plist = plist_new_dict();
    if (supported_formats_plist != NULL) {
      plist_dict_set_item(supported_formats_plist, "audioStream", plist_new_uint(21235712));
      {
        bufferStreamFormats |= 0x00000400000L; // AAC-LC/44.1K/F24/2
        bufferStreamFormats |= 0x40000;        // ALAC/44100/S16/2
      }

      {
        bufferStreamFormats |= 0x00000200000L; // ALAC/48K/F24/2
        bufferStreamFormats |= 0x00000800000L; // AAC-LC/48K/F24/2
      }
      {
        if (config.eight_channel_layout != 0)
          bufferStreamFormats |= 0x10000000000L; // AAC-LC/48K/F24/7.1
        if (config.six_channel_layout != 0)
          bufferStreamFormats |= 0x08000000000L; // AAC-LC/48K/F24/5.1
      }
      plist_dict_set_item(supported_formats_plist, "bufferStream",
                          plist_new_uint(bufferStreamFormats));
      debug(3, "bufferedStream formats: 0x%" PRIX64 ".", bufferStreamFormats);
      plist_dict_set_item(response_plist, "supportedFormats", supported_formats_plist);
    }
  }
  return response_plist;
}

void handle_get_info(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug_log_rtsp_message(3, "GET /info:", req);
  if (rtsp_message_contains_plist(req)) { // it's stage one
    // get version of AirPlay -- it might be too old. Not using it yet.
    char *hdr = msg_get_header(req, "User-Agent");
    if (hdr) {
      if (strstr(hdr, "AirPlay/") == hdr) {
        hdr = hdr + strlen("AirPlay/");
        // double airplay_version = 0.0;
        // airplay_version = atof(hdr);
        debug(3, "Connection %d: GET_INFO: Source AirPlay Version is: %s.", conn->connection_number,
              hdr);
      }
    }

    // in Stage 1, look for the DACP and Active-Remote
    char *ar = msg_get_header(req, "Active-Remote");
    if (ar) {
      debug(3, "Connection %d: GET /info -- Active-Remote string seen: \"%s\".",
            conn->connection_number, ar);
      // get the active remote
      if (conn->dacp_active_remote) // this is in case SETUP was previously called
        free(conn->dacp_active_remote);
      conn->dacp_active_remote = strdup(ar);
#ifdef CONFIG_METADATA
      send_metadata('ssnc', 'acre', ar, strlen(ar), req, 1);
#endif
    } else {
      debug(3, "Connection %d: GET /info -- doesn't include  Active-Remote information.",
            conn->connection_number);
      if (conn->dacp_active_remote) { // this is in case GET /info was previously called
        free(conn->dacp_active_remote);
        conn->dacp_active_remote = NULL;
      }
    }

    ar = msg_get_header(req, "DACP-ID");
    if (ar) {
      debug(3, "Connection %d: GET /info -- DACP-ID string seen: \"%s\".", conn->connection_number,
            ar);
      if (conn->dacp_id) // this is in case SETUP was previously called
        free(conn->dacp_id);
      conn->dacp_id = strdup(ar);
#ifdef CONFIG_METADATA
      send_metadata('ssnc', 'daid', ar, strlen(ar), req, 1);
#endif
    } else {
      debug(3, "Connection %d: GET /info -- doesn't include DACP-ID string information.",
            conn->connection_number);
      if (conn->dacp_id) { // this is in case GET /info was previously called
        free(conn->dacp_id);
        conn->dacp_id = NULL;
      }
    }

    plist_t info_plist = NULL;
    plist_from_memory(req->content, req->contentlength, &info_plist);

    plist_t qualifier = plist_dict_get_item(info_plist, "qualifier");
    if (qualifier == NULL) {
      debug(1, "GET /info Stage 1: plist->qualifier was NULL");
      goto user_fail;
    }
    if (plist_array_get_size(qualifier) < 1) {
      debug(1, "GET /info Stage 1: plist->qualifier array length < 1");
      goto user_fail;
    }
    plist_t qualifier_array_value = plist_array_get_item(qualifier, 0);
    char *qualifier_array_val_cstr;
    plist_get_string_val(qualifier_array_value, &qualifier_array_val_cstr);
    if (qualifier_array_val_cstr == NULL) {
      debug(1, "GET /info Stage 1: first item in qualifier array not a string");
      goto user_fail;
    }
    debug(3, "GET /info Stage 1: qualifier: %s", qualifier_array_val_cstr);
    plist_free(info_plist);
    free(qualifier_array_val_cstr);

    plist_t response_plist = generateInfoPlist(conn);

    if (response_plist == NULL)
      goto user_fail;

    void *txtData = NULL;
    size_t txtDataLength = 0;
    generateTxtDataValueInfo(conn, &txtData, &txtDataLength);
    plist_dict_set_item(response_plist, "txtAirPlay", plist_new_data(txtData, txtDataLength));
    free(txtData);
    plist_to_bin(response_plist, &resp->content, &resp->contentlength);
    if (resp->contentlength == 0)
      debug(1, "GET /info Stage 1: response bplist not created!");
    plist_free(response_plist);
    /*
        free(qualifier_response_data);
    */

    msg_add_header(resp, "Content-Type", "application/x-apple-binary-plist");
    resp->respcode = 200;
    debug_log_rtsp_message(3, "GET /info Stage 1 Response:", resp);
    return;

  user_fail:
    resp->respcode = 400;
    return;
  } else { // stage two
    plist_t response_plist = generateInfoPlist(conn);

    if (response_plist == NULL)
      goto user_fail;

    void *txtData = NULL;
    size_t txtDataLength = 0;
    generateTxtDataValueInfo(conn, &txtData, &txtDataLength);
    plist_dict_set_item(response_plist, "txtAirPlay", plist_new_data(txtData, txtDataLength));
    free(txtData);
    plist_to_bin(response_plist, &resp->content, &resp->contentlength);
    plist_free(response_plist);
    msg_add_header(resp, "Content-Type", "application/x-apple-binary-plist");
    resp->respcode = 200;
    debug_log_rtsp_message(3, "GET /info Stage 2 Response", resp);
    return;
  }
}

void handle_flushbuffered(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(2, "Connection %d: FLUSHBUFFERED %s : Content-Length %d", conn->connection_number,
        req->path, req->contentlength);
  debug_log_rtsp_message(3, "FLUSHBUFFERED request", req);

  uint64_t flushFromSeq = 0;
  uint64_t flushFromTS = 0;
  uint64_t flushUntilSeq = 0;
  uint64_t flushUntilTS = 0;
  int flushFromValid = 0;
  plist_t messagePlist = plist_from_rtsp_content(req);
  if (messagePlist != NULL) {
    plist_t item = plist_dict_get_item(messagePlist, "flushFromSeq");
    if (item == NULL) {
      debug(3, "Can't find a flushFromSeq");
    } else {
      flushFromValid = 1;
      plist_get_uint_val(item, &flushFromSeq);
      debug(3, "flushFromSeq is %" PRId64 ".", flushFromSeq);
    }

    item = plist_dict_get_item(messagePlist, "flushFromTS");
    if (item == NULL) {
      if (flushFromValid != 0)
        debug(1, "flushFromSeq without flushFromTS!");
      else
        debug(3, "Can't find a flushFromTS");
    } else {
      plist_get_uint_val(item, &flushFromTS);
      if (flushFromValid == 0)
        debug(1, "flushFromTS without flushFromSeq!");
      debug(3, "flushFromTS is %" PRId64 ".", flushFromTS);
    }

    item = plist_dict_get_item(messagePlist, "flushUntilSeq");
    if (item == NULL) {
      debug(1, "Can't find the flushUntilSeq");
    } else {
      plist_get_uint_val(item, &flushUntilSeq);
      debug(3, "flushUntilSeq is %" PRId64 ".", flushUntilSeq);
    }

    item = plist_dict_get_item(messagePlist, "flushUntilTS");
    if (item == NULL) {
      debug(1, "Can't find the flushUntilTS");
    } else {
      plist_get_uint_val(item, &flushUntilTS);
      debug(3, "flushUntilTS is %" PRId64 ".", flushUntilTS);
    }

    debug_mutex_lock(&conn->flush_mutex, 1000, 1);

    if (flushFromValid == 0) {
      // an immediate flush is requested
      conn->ap2_immediate_flush_requested = 1;
      conn->ap2_immediate_flush_until_sequence_number = flushUntilSeq & 0x7fffff;
      conn->ap2_immediate_flush_until_rtp_timestamp = flushUntilTS;
      debug(2,
            "Connection %d: immediate flush request created: flushUntilTS: %" PRIu64 ", flushUntilSeq: %" PRIu64 ".",
            conn->connection_number, flushUntilTS, flushUntilSeq & 0x7fffff);
      conn->ap2_play_enabled = 0; // stop trying to play audio
      ptp_send_control_message_string(
          "P"); // "P"ause signify clock no longer valid and will be restarted by a subsequent play
    } else {
      // look for a record slot that isn't in use
      unsigned int i = 0;
      unsigned int found = 0;
      while ((i < MAX_DEFERRED_FLUSH_REQUESTS) && (found == 0)) {
        if (conn->ap2_deferred_flush_requests[i].inUse == 0) {
          found = 1;
        } else {
          i++;
        }
      }
      if (found != 0) {
        conn->ap2_deferred_flush_requests[i].inUse = 1;
        conn->ap2_deferred_flush_requests[i].active = 0;
        conn->ap2_deferred_flush_requests[i].flushFromSeq = flushFromSeq & 0x7fffff;
        conn->ap2_deferred_flush_requests[i].flushFromTS = flushFromTS;
        conn->ap2_deferred_flush_requests[i].flushUntilSeq = flushUntilSeq & 0x7fffff;
        conn->ap2_deferred_flush_requests[i].flushUntilTS = flushUntilTS;
        debug(2,
              "Connection %d: deferred flush request created: flushFromSeq: %" PRIu64 ", flushUntilSeq: %" PRIu64 ".",
              conn->connection_number, flushFromSeq, flushUntilSeq);
      } else {
        debug(1, "Connection %d: no more room for deferred flush request records",
              conn->connection_number);
      }
    }

    debug_mutex_unlock(&conn->flush_mutex, 3);
    plist_free(messagePlist);
  }

  resp->respcode = 200;
}

void handle_setrate(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(1, "Connection %d: SETRATE %s : Content-Length %d", conn->connection_number, req->path,
        req->contentlength);
  debug_log_rtsp_message(1, "SETRATE request -- unimplemented", req);
  resp->respcode = 501; // Not Implemented
}

void handle_unimplemented_ap1(__attribute((unused)) rtsp_conn_info *conn, rtsp_message *req,
                              rtsp_message *resp) {
  debug_log_rtsp_message(1, "request not recognised for AirPlay 1 operation", req);
  resp->respcode = 501;
}

void handle_setrateanchori(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(2, "Connection %d: SETRATEANCHORI %s :: Content-Length %d", conn->connection_number,
        req->path, req->contentlength);
  debug_log_rtsp_message(3, "SETRATEANCHORI", req);
  plist_t messagePlist = plist_from_rtsp_content(req);

  if (messagePlist != NULL) {
    pthread_cleanup_push(plist_cleanup, (void *)messagePlist);
    plist_t item = plist_dict_get_item(messagePlist, "networkTimeSecs");
    if (item != NULL) {
      plist_t item_2 = plist_dict_get_item(messagePlist, "networkTimeTimelineID");
      if (item_2 == NULL) {
        debug(1, "Can't identify the Clock ID of the player.");
      } else {
        uint64_t nid;
        plist_get_uint_val(item_2, &nid);
        debug(3, "networkTimeTimelineID \"%" PRIx64 "\".", nid);
        conn->networkTimeTimelineID = nid;
      }
      uint64_t networkTimeSecs;
      plist_get_uint_val(item, &networkTimeSecs);
      debug(3, "anchor networkTimeSecs is %" PRIu64 ".", networkTimeSecs);

      item = plist_dict_get_item(messagePlist, "networkTimeFrac");
      uint64_t networkTimeFrac;
      plist_get_uint_val(item, &networkTimeFrac);
      debug(3, "anchor networkTimeFrac is 0%" PRIu64 ".", networkTimeFrac);
      // it looks like the networkTimeFrac is a fraction where the msb is work 1/2, the
      // next 1/4 and so on
      // now, convert the network time and fraction into nanoseconds
      networkTimeFrac = networkTimeFrac >> 32; // reduce precision to about 1/4 nanosecond
      networkTimeFrac = networkTimeFrac * 1000000000;
      networkTimeFrac = networkTimeFrac >> 32; // we should now be left with the ns

      networkTimeSecs = networkTimeSecs * 1000000000; // turn the whole seconds into ns
      uint64_t anchorTimeNanoseconds = networkTimeSecs + networkTimeFrac;

      debug(3, "anchorTimeNanoseconds looks like %" PRIu64 ".", anchorTimeNanoseconds);

      item = plist_dict_get_item(messagePlist, "rtpTime");
      uint64_t rtpTime;

      plist_get_uint_val(item, &rtpTime);
      // debug(1, "anchor rtpTime is %" PRId64 ".", rtpTime);
      uint32_t anchorRTPTime = rtpTime;

      int32_t added_latency = (int32_t)(config.audio_backend_latency_offset * conn->input_rate);
      // debug(1,"anchorRTPTime: %" PRIu32 ", added latency: %" PRId32 ".", anchorRTPTime,
      // added_latency);
      set_ptp_anchor_info(conn, conn->networkTimeTimelineID, anchorRTPTime - added_latency,
                          anchorTimeNanoseconds);
    }

    item = plist_dict_get_item(messagePlist, "rate");
    if (item != NULL) {
      uint64_t rate;
      plist_get_uint_val(item, &rate);
      debug(3, "anchor rate 0x%016" PRIx64 ".", rate);
      pthread_cleanup_debug_mutex_lock(&conn->flush_mutex, 1000, 1);
      conn->ap2_rate = rate;
      if ((rate & 1) != 0) {
        ptp_send_control_message_string(
            "B"); // signify clock dependability period is "B"eginning (or resuming)
        debug(2, "Connection %d: SETRATEANCHORI Start playing, with anchor clock %" PRIx64 ".",
              conn->connection_number, conn->networkTimeTimelineID);
        activity_monitor_signify_activity(1);

#ifdef CONFIG_METADATA
        send_ssnc_metadata('pres', NULL, 0, 1); // resume -- contains cancellation points
#endif
        conn->ap2_play_enabled = 1;
      } else {
        ptp_send_control_message_string("P"); // signify play is "P"ausing
        debug(2, "Connection %d: SETRATEANCHORI Pause playing.", conn->connection_number);
        conn->ap2_play_enabled = 0;
        activity_monitor_signify_activity(0);

#ifdef CONFIG_METADATA
        send_ssnc_metadata('paus', NULL, 0, 1); // pause -- contains cancellation points
#endif
        // if (config.output->stop) {
        //   debug(1, "Connection %d: SETRATEANCHORI would stop the output backend.",
        //   conn->connection_number); config.output->stop();
        // }
      }
      pthread_cleanup_pop(1); // unlock the conn->flush_mutex
    }
    pthread_cleanup_pop(1); // plist_free the messagePlist;
  } else {
    debug(1, "missing plist!");
  }
  resp->respcode = 200;
}

void handle_get(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(3, "Connection %d: GET %s :: Content-Length %d", conn->connection_number, req->path,
        req->contentlength);
  debug_log_rtsp_message(3, "GET request", req);
  if (strcmp(req->path, "/info") == 0) {
    handle_get_info(conn, req, resp);
  } else {
    debug(1, "Unhandled GET, path \"%s\".", req->path);
    resp->respcode = 501; // Not Implemented
  }
}

#else
void handle_get(__attribute((unused)) rtsp_conn_info *conn, __attribute((unused)) rtsp_message *req,
                __attribute((unused)) rtsp_message *resp) {
  debug(1, "Connection %d: GET %s Content-Length %d", conn->connection_number, req->path,
        req->contentlength);
  resp->respcode = 501; // 501 is not implemented
}

void handle_post(rtsp_conn_info *conn, rtsp_message *req,
                 __attribute((unused)) rtsp_message *resp) {
  debug(1, "Connection %d: AP1 POST %s Content-Length %d", conn->connection_number, req->path,
        req->contentlength);
  resp->respcode = 501; // 501 is not implemented
}
#endif

#ifdef CONFIG_AIRPLAY_2
struct pairings {
  char device_id[PAIR_AP_DEVICE_ID_LEN_MAX];
  uint8_t public_key[32];

  struct pairings *next;
} * pairings;

static struct pairings *pairing_find(const char *device_id) {
  for (struct pairings *pairing = pairings; pairing; pairing = pairing->next) {
    if (strcmp(device_id, pairing->device_id) == 0)
      return pairing;
  }
  return NULL;
}

static void pairing_add(uint8_t public_key[32], const char *device_id) {
  struct pairings *pairing = calloc(1, sizeof(struct pairings));
  snprintf(pairing->device_id, sizeof(pairing->device_id), "%s", device_id);
  memcpy(pairing->public_key, public_key, sizeof(pairing->public_key));

  pairing->next = pairings;
  pairings = pairing;
}

static void pairing_remove(struct pairings *pairing) {
  if (pairing == pairings) {
    pairings = pairing->next;
  } else {
    struct pairings *iter;
    for (iter = pairings; iter && (iter->next != pairing); iter = iter->next)
      ; /* EMPTY */

    if (iter)
      iter->next = pairing->next;
  }

  free(pairing);
}

static int pairing_add_cb(uint8_t public_key[32], const char *device_id,
                          void *cb_arg __attribute__((unused))) {
  debug(2, "pair-add cb for %s", device_id);

  struct pairings *pairing = pairing_find(device_id);
  if (pairing) {
    memcpy(pairing->public_key, public_key, sizeof(pairing->public_key));
    return 0;
  }

  pairing_add(public_key, device_id);
  return 0;
}

static int pairing_remove_cb(uint8_t public_key[32] __attribute__((unused)), const char *device_id,
                             void *cb_arg __attribute__((unused))) {
  debug(2, "pair-remove cb for %s", device_id);

  struct pairings *pairing = pairing_find(device_id);
  if (!pairing) {
    debug(1, "pair-remove callback for device \"%s\".", device_id);
    return -1;
  }

  pairing_remove(pairing);
  return 0;
}

static void pairing_list_cb(pair_cb enum_cb, void *enum_cb_arg,
                            void *cb_arg __attribute__((unused))) {
  debug(2, "pair-list cb");

  for (struct pairings *pairing = pairings; pairing; pairing = pairing->next) {
    enum_cb(pairing->public_key, pairing->device_id, enum_cb_arg);
  }
}

void handle_pair_add(rtsp_conn_info *conn __attribute__((unused)), rtsp_message *req,
                     rtsp_message *resp) {
  debug(2, "Connection %d: handle_pair_add", conn->connection_number);
  uint8_t *body = NULL;
  size_t body_len = 0;
  int ret = pair_add(PAIR_SERVER_HOMEKIT, &body, &body_len, pairing_add_cb, NULL,
                     (const uint8_t *)req->content, req->contentlength);
  if (ret < 0) {
    debug(1, "pair-add returned an error");
    resp->respcode = 451;
    return;
  }
  resp->content = (char *)body; // these will be freed when the data is sent
  resp->contentlength = body_len;
  msg_add_header(resp, "Content-Type", "application/octet-stream");
  debug_log_rtsp_message(2, "pair-add response", resp);
}

void handle_pair_list(rtsp_conn_info *conn __attribute__((unused)), rtsp_message *req,
                      rtsp_message *resp) {
  debug(2, "Connection %d: handle_pair_list", conn->connection_number);
  uint8_t *body = NULL;
  size_t body_len = 0;
  int ret = pair_list(PAIR_SERVER_HOMEKIT, &body, &body_len, pairing_list_cb, NULL,
                      (const uint8_t *)req->content, req->contentlength);
  if (ret < 0) {
    debug(1, "pair-list returned an error");
    resp->respcode = 451;
    return;
  }
  resp->content = (char *)body; // these will be freed when the data is sent
  resp->contentlength = body_len;
  msg_add_header(resp, "Content-Type", "application/octet-stream");
  debug_log_rtsp_message(2, "pair-list response", resp);
}

void handle_pair_remove(rtsp_conn_info *conn __attribute__((unused)), rtsp_message *req,
                        rtsp_message *resp) {
  debug(3, "Connection %d: handle_pair_remove", conn->connection_number);
  uint8_t *body = NULL;
  size_t body_len = 0;
  int ret = pair_remove(PAIR_SERVER_HOMEKIT, &body, &body_len, pairing_remove_cb, NULL,
                        (const uint8_t *)req->content, req->contentlength);
  if (ret < 0) {
    debug(1, "pair-remove returned an error");
    resp->respcode = 451;
    return;
  }
  resp->content = (char *)body; // these will be freed when the data is sent
  resp->contentlength = body_len;
  msg_add_header(resp, "Content-Type", "application/octet-stream");
  debug_log_rtsp_message(3, "pair-remove response", resp);
}

void handle_pair_verify(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(3, "Connection %d: handle_pair_verify Content-Length %d", conn->connection_number,
        req->contentlength);
  int ret;
  uint8_t *body = NULL;
  size_t body_len = 0;
  // struct pair_result *result;

  if (!conn->ap2_pairing_context.verify_ctx) {
    conn->ap2_pairing_context.verify_ctx =
        pair_verify_new(PAIR_SERVER_HOMEKIT, NULL, NULL, NULL, config.airplay_device_id);
    if (!conn->ap2_pairing_context.verify_ctx) {
      debug(1, "Error creating verify context");
      resp->respcode = 500; // Internal Server Error
      goto out;
    }
  }

  ret = pair_verify(&body, &body_len, conn->ap2_pairing_context.verify_ctx,
                    (const uint8_t *)req->content, req->contentlength);
  if (ret < 0) {
    debug(1, "%s", pair_verify_errmsg(conn->ap2_pairing_context.verify_ctx));
    resp->respcode = 470; // Connection Authorization Required
    goto out;
  }

  /*
    ret = pair_verify_result(&result, conn->ap2_pairing_context.verify_ctx);
    if (ret == 0 && result->shared_secret_len > 0) {
      conn->ap2_pairing_context.control_cipher_bundle.cipher_ctx =
          pair_cipher_new(PAIR_SERVER_HOMEKIT, 3, result->shared_secret, result->shared_secret_len);
      if (!conn->ap2_pairing_context.control_cipher_bundle.cipher_ctx) {
        debug(1, "Error setting up rtsp control channel ciphering\n");
        goto out;
      }
      conn->ap2_pairing_context.event_cipher_bundle.cipher_ctx =
          pair_cipher_new(PAIR_SERVER_HOMEKIT, 4, result->shared_secret, result->shared_secret_len);
      if (!conn->ap2_pairing_context.event_cipher_bundle.cipher_ctx) {
        debug(1, "Error setting up rtsp event channel ciphering\n");
        goto out;
      }
    }
  */

out:
  resp->content = (char *)body; // these will be freed when the data is sent
  resp->contentlength = body_len;
  if (body)
    msg_add_header(resp, "Content-Type", "application/octet-stream");
  debug_log_rtsp_message(3, "pair-verify response", resp);
}

void handle_pair_pin_start(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  uint8_t *body = NULL;
  size_t body_len = 0;
  debug(2, "Connection %d: pair-pin-start Content-Length %d", conn->connection_number,
        req->contentlength);
  debug_log_rtsp_message(2, "handle_pair_pin_start", req);

  resp->content = (char *)body; // these will be freed when the data is sent
  resp->contentlength = body_len;
  if (body != NULL)
    msg_add_header(resp, "Content-Type", "application/octet-stream");
  debug_log_rtsp_message(2, "pair-pin-start response", resp);
}

void handle_pair_setup(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  int ret;
  uint8_t *body = NULL;
  size_t body_len = 0;
  debug(3, "Connection %d: handle_pair_setup Content-Length %d", conn->connection_number,
        req->contentlength);
  debug_log_rtsp_message(3, "handle_pair_setup", req);

  if (!conn->ap2_pairing_context.setup_ctx) {
    conn->ap2_pairing_context.setup_ctx = pair_setup_new(PAIR_SERVER_HOMEKIT, config.airplay_pin,
                                                         NULL, NULL, config.airplay_device_id);
    if (!conn->ap2_pairing_context.setup_ctx) {
      debug(1, "Error creating setup context");
      resp->respcode = 500; // Internal Server Error
      goto out;
    }
  }

  ret = pair_setup(&body, &body_len, conn->ap2_pairing_context.setup_ctx,
                   (const uint8_t *)req->content, req->contentlength);
  if (ret < 0) {
    debug(1, "%s", pair_setup_errmsg(conn->ap2_pairing_context.setup_ctx));
    resp->respcode = 470; // Connection Authorization Required
    goto out;
  }

  ret = pair_setup_result(NULL, &conn->pair_setup_result, conn->ap2_pairing_context.setup_ctx);
  if (ret == 0 && conn->pair_setup_result->shared_secret_len > 0) {
    // Transient pairing completed (pair-setup step 2), prepare encryption, but
    // don't activate yet, the response to this request is still plaintext
    conn->ap2_pairing_context.control_cipher_bundle.cipher_ctx =
        pair_cipher_new(PAIR_SERVER_HOMEKIT, 3, conn->pair_setup_result->shared_secret,
                        conn->pair_setup_result->shared_secret_len,
                        ""); // last argument is the (possible) dynamic salt suffix
    if (!conn->ap2_pairing_context.control_cipher_bundle.cipher_ctx) {
      debug(1, "Error setting up rtsp control channel ciphering\n");
      goto out;
    }
    conn->ap2_pairing_context.control_cipher_bundle.description = strdup("Control Stream");
    conn->ap2_pairing_context.event_cipher_bundle.cipher_ctx =
        pair_cipher_new(PAIR_SERVER_HOMEKIT, 4, conn->pair_setup_result->shared_secret,
                        conn->pair_setup_result->shared_secret_len,
                        ""); // last argument is the (possible) dynamic salt suffix
    if (!conn->ap2_pairing_context.event_cipher_bundle.cipher_ctx) {
      debug(1, "Error setting up rtsp control channel ciphering\n");
      goto out;
    }
    conn->ap2_pairing_context.event_cipher_bundle.description = strdup("Event Stream");
  }

out:
  resp->content = (char *)body; // these will be freed when the data is sent
  resp->contentlength = body_len;
  if (body)
    msg_add_header(resp, "Content-Type", "application/octet-stream");
  debug_log_rtsp_message(3, "pair-setup response", resp);
}

void handle_fp_setup(__attribute__((unused)) rtsp_conn_info *conn, rtsp_message *req,
                     rtsp_message *resp) {

  /* Fairplay magic */
  static uint8_t server_fp_reply1[] =
      "\x46\x50\x4c\x59\x03\x01\x02\x00\x00\x00\x00\x82\x02\x00\x0f\x9f\x3f\x9e\x0a"
      "\x25\x21\xdb\xdf\x31\x2a\xb2\xbf\xb2\x9e\x8d\x23\x2b\x63\x76\xa8\xc8\x18\x70"
      "\x1d\x22\xae\x93\xd8\x27\x37\xfe\xaf\x9d\xb4\xfd\xf4\x1c\x2d\xba\x9d\x1f\x49"
      "\xca\xaa\xbf\x65\x91\xac\x1f\x7b\xc6\xf7\xe0\x66\x3d\x21\xaf\xe0\x15\x65\x95"
      "\x3e\xab\x81\xf4\x18\xce\xed\x09\x5a\xdb\x7c\x3d\x0e\x25\x49\x09\xa7\x98\x31"
      "\xd4\x9c\x39\x82\x97\x34\x34\xfa\xcb\x42\xc6\x3a\x1c\xd9\x11\xa6\xfe\x94\x1a"
      "\x8a\x6d\x4a\x74\x3b\x46\xc3\xa7\x64\x9e\x44\xc7\x89\x55\xe4\x9d\x81\x55\x00"
      "\x95\x49\xc4\xe2\xf7\xa3\xf6\xd5\xba";
  static uint8_t server_fp_reply2[] =
      "\x46\x50\x4c\x59\x03\x01\x02\x00\x00\x00\x00\x82\x02\x01\xcf\x32\xa2\x57\x14"
      "\xb2\x52\x4f\x8a\xa0\xad\x7a\xf1\x64\xe3\x7b\xcf\x44\x24\xe2\x00\x04\x7e\xfc"
      "\x0a\xd6\x7a\xfc\xd9\x5d\xed\x1c\x27\x30\xbb\x59\x1b\x96\x2e\xd6\x3a\x9c\x4d"
      "\xed\x88\xba\x8f\xc7\x8d\xe6\x4d\x91\xcc\xfd\x5c\x7b\x56\xda\x88\xe3\x1f\x5c"
      "\xce\xaf\xc7\x43\x19\x95\xa0\x16\x65\xa5\x4e\x19\x39\xd2\x5b\x94\xdb\x64\xb9"
      "\xe4\x5d\x8d\x06\x3e\x1e\x6a\xf0\x7e\x96\x56\x16\x2b\x0e\xfa\x40\x42\x75\xea"
      "\x5a\x44\xd9\x59\x1c\x72\x56\xb9\xfb\xe6\x51\x38\x98\xb8\x02\x27\x72\x19\x88"
      "\x57\x16\x50\x94\x2a\xd9\x46\x68\x8a";
  static uint8_t server_fp_reply3[] =
      "\x46\x50\x4c\x59\x03\x01\x02\x00\x00\x00\x00\x82\x02\x02\xc1\x69\xa3\x52\xee"
      "\xed\x35\xb1\x8c\xdd\x9c\x58\xd6\x4f\x16\xc1\x51\x9a\x89\xeb\x53\x17\xbd\x0d"
      "\x43\x36\xcd\x68\xf6\x38\xff\x9d\x01\x6a\x5b\x52\xb7\xfa\x92\x16\xb2\xb6\x54"
      "\x82\xc7\x84\x44\x11\x81\x21\xa2\xc7\xfe\xd8\x3d\xb7\x11\x9e\x91\x82\xaa\xd7"
      "\xd1\x8c\x70\x63\xe2\xa4\x57\x55\x59\x10\xaf\x9e\x0e\xfc\x76\x34\x7d\x16\x40"
      "\x43\x80\x7f\x58\x1e\xe4\xfb\xe4\x2c\xa9\xde\xdc\x1b\x5e\xb2\xa3\xaa\x3d\x2e"
      "\xcd\x59\xe7\xee\xe7\x0b\x36\x29\xf2\x2a\xfd\x16\x1d\x87\x73\x53\xdd\xb9\x9a"
      "\xdc\x8e\x07\x00\x6e\x56\xf8\x50\xce";
  static uint8_t server_fp_reply4[] =
      "\x46\x50\x4c\x59\x03\x01\x02\x00\x00\x00\x00\x82\x02\x03\x90\x01\xe1\x72\x7e"
      "\x0f\x57\xf9\xf5\x88\x0d\xb1\x04\xa6\x25\x7a\x23\xf5\xcf\xff\x1a\xbb\xe1\xe9"
      "\x30\x45\x25\x1a\xfb\x97\xeb\x9f\xc0\x01\x1e\xbe\x0f\x3a\x81\xdf\x5b\x69\x1d"
      "\x76\xac\xb2\xf7\xa5\xc7\x08\xe3\xd3\x28\xf5\x6b\xb3\x9d\xbd\xe5\xf2\x9c\x8a"
      "\x17\xf4\x81\x48\x7e\x3a\xe8\x63\xc6\x78\x32\x54\x22\xe6\xf7\x8e\x16\x6d\x18"
      "\xaa\x7f\xd6\x36\x25\x8b\xce\x28\x72\x6f\x66\x1f\x73\x88\x93\xce\x44\x31\x1e"
      "\x4b\xe6\xc0\x53\x51\x93\xe5\xef\x72\xe8\x68\x62\x33\x72\x9c\x22\x7d\x82\x0c"
      "\x99\x94\x45\xd8\x92\x46\xc8\xc3\x59";

  static uint8_t server_fp_header[] = "\x46\x50\x4c\x59\x03\x01\x04\x00\x00\x00\x00\x14";

  resp->respcode = 200; // assume it's handled

  // uint8_t *out;
  // size_t out_len;
  int version_pos = 4;
  int mode_pos = 14;
  int type_pos = 5;
  int seq_pos = 6;
  int setup_message_type = 1;
  int setup1_message_seq = 1;
  int setup2_message_seq = 3;
  int setup2_suffix_len = 20;
  // int ret;

  // response and len are dummy values and can be ignored

  // debug(1, "Version: %02x, mode: %02x, type: %02x, seq: %02x", req->content[version_pos],
  //       req->content[mode_pos], req->content[type_pos], req->content[seq_pos]);

  if (req->content[version_pos] != 3 || req->content[type_pos] != setup_message_type) {
    debug(1, "Unsupported FP version.");
  }

  char *response = NULL;
  size_t len = 0;

  if (req->content[seq_pos] == setup1_message_seq) {
    // All replies are the same length. -1 to account for the NUL byte at the end.
    len = sizeof(server_fp_reply1) - 1;

    if (req->content[mode_pos] == 0)
      response = memdup(server_fp_reply1, len);
    if (req->content[mode_pos] == 1)
      response = memdup(server_fp_reply2, len);
    if (req->content[mode_pos] == 2)
      response = memdup(server_fp_reply3, len);
    if (req->content[mode_pos] == 3)
      response = memdup(server_fp_reply4, len);

  } else if (req->content[seq_pos] == setup2_message_seq) {
    // -1 to account for the NUL byte at the end.
    len = sizeof(server_fp_header) - 1 + setup2_suffix_len;
    response = malloc(len);
    if (response) {
      memcpy(response, server_fp_header, sizeof(server_fp_header) - 1);
      memcpy(response + sizeof(server_fp_header) - 1,
             req->content + req->contentlength - setup2_suffix_len, setup2_suffix_len);
    }
  }

  if (response == NULL) {
    debug(1, "Cannot create a response.");
  }

  resp->content = response; // these will be freed when the data is sent
  resp->contentlength = len;
  msg_add_header(resp, "Content-Type", "application/octet-stream");
}

/*
        <key>Identifier</key>
        <string>21cc689d-d5de-4814-872c-71d1426b57e0</string>
        <key>Enable_HK_Access_Control</key>
        <true/>
        <key>PublicKey</key>
        <data>
        qXJDhhL5F3OACL+HO7LVLQVdy0OJtavepjpF720PaOQ=
        </data>
        <key>Device_Name</key>
        <string>MyDevice</string>
        <key>Access_Control_Level</key>
        <integer>0</integer>
*/
void handle_configure(rtsp_conn_info *conn __attribute__((unused)),
                      rtsp_message *req __attribute__((unused)), rtsp_message *resp) {
  uint8_t public_key[32];

  pair_public_key_get(PAIR_SERVER_HOMEKIT, public_key, config.airplay_device_id);

  plist_t response_plist = plist_new_dict();

  plist_dict_set_item(response_plist, "Identifier", plist_new_string(config.airplay_pi));
  plist_dict_set_item(response_plist, "Enable_HK_Access_Control", plist_new_bool(1));
  plist_dict_set_item(response_plist, "PublicKey",
                      plist_new_data((const char *)public_key, sizeof(public_key)));
  plist_dict_set_item(response_plist, "Device_Name", plist_new_string(config.service_name));
  plist_dict_set_item(response_plist, "Access_Control_Level", plist_new_uint(0));

  plist_to_bin(response_plist, &resp->content, &resp->contentlength);
  plist_free(response_plist);

  msg_add_header(resp, "Content-Type", "application/x-apple-binary-plist");
  debug_log_rtsp_message(2, "POST /configure response:", resp);
}

void handle_feedback(rtsp_conn_info *conn, __attribute__((unused)) rtsp_message *req,
                     __attribute__((unused)) rtsp_message *resp) {
  debug(3, "Connection %d: POST %s Content-Length %d", conn->connection_number, req->path,
        req->contentlength);
  debug_log_rtsp_message(3, NULL, req);

  int is_playing = 0;
  int type = 0;
  double rate = 0.0;

  // get information from the current player, if any.

  pthread_rwlock_rdlock(&principal_conn_lock); // don't let the principal_conn be changed
  pthread_cleanup_push(rwlock_unlock, (void *)&principal_conn_lock);
  if ((principal_conn != NULL) && (principal_conn->is_playing != 0)) {
    is_playing = 1;
    type = principal_conn->type;
    rate = 1.0 * principal_conn->input_rate;
  }
  pthread_cleanup_pop(1); // release the principal_conn lock

  // debug(1, "Player is%s playing.", is_playing != 0 ? "" : " not");

  if (is_playing != 0) {
    if ((type != 96) && (type != 103))
      debug(1, "Connection %d, feedback unexpected type: %u.", conn->connection_number, type);
    if ((rate != 44100.0) && (rate != 48000.0))
      debug(2, "Connection %d, feedback unexpected rate: %f.", conn->connection_number, rate);
    plist_t payload_plist = plist_new_dict();
    plist_dict_set_item(payload_plist, "type", plist_new_uint(type));
    plist_dict_set_item(payload_plist, "sr", plist_new_real(rate));

    plist_t array_plist = plist_new_array();
    plist_array_append_item(array_plist, payload_plist);

    plist_t response_plist = plist_new_dict();
    plist_dict_set_item(response_plist, "streams", array_plist);

    plist_to_bin(response_plist, &resp->content, &resp->contentlength);
    plist_free(response_plist);
    // plist_free(array_plist);
    // plist_free(payload_plist);

    msg_add_header(resp, "Content-Type", "application/x-apple-binary-plist");
    debug_log_rtsp_message(3, "FEEDBACK response:", resp);
  }
}

void handle_command(rtsp_conn_info *conn, rtsp_message *req,
                    __attribute__((unused)) rtsp_message *resp) {
  debug(3, "Connection %d: POST %s Content-Length %d", conn->connection_number, req->path,
        req->contentlength);
  debug_log_rtsp_message(3, NULL, req);
  if (rtsp_message_contains_plist(req)) {
    plist_t command_dict = NULL;
    plist_from_memory(req->content, req->contentlength, &command_dict);
    if (command_dict != NULL) {
      // we have a plist -- try to get the dict item keyed to "updateMRSupportedCommands"
      plist_t item = plist_dict_get_item(command_dict, "type");
      if (item != NULL) {
        char *typeValue = NULL;
        plist_get_string_val(item, &typeValue);
        if ((typeValue != NULL) && (strcmp(typeValue, "updateMRSupportedCommands") == 0)) {
          item = plist_dict_get_item(command_dict, "params");
          if (item != NULL) {
            // the item should be a dict
            plist_t item_array = plist_dict_get_item(item, "mrSupportedCommandsFromSender");
            if (item_array != NULL) {
              // here we have an array of data items
              uint32_t items = plist_array_get_size(item_array);
              if (items) {
                uint32_t item_number;
                for (item_number = 0; item_number < items; item_number++) {
                  plist_t the_item = plist_array_get_item(item_array, item_number);
                  char *buff = NULL;
                  uint64_t length = 0;
                  plist_get_data_val(the_item, &buff, &length);
                  // debug(1,"Item %d, length: %" PRId64 " bytes", item_number, length);
                  if ((buff != NULL) && (length >= strlen("bplist00")) &&
                      (strstr(buff, "bplist00") == buff)) {
                    // debug(1,"Contains a plist.");
                    plist_t subsidiary_plist = NULL;
                    plist_from_memory(buff, length, &subsidiary_plist);
                    if (subsidiary_plist) {
                      char *printable_plist = plist_as_xml_text(subsidiary_plist);
                      if (printable_plist) {
                        debug(3, "\n%s", printable_plist);
                        free(printable_plist);
                      } else {
                        debug(1, "Can't print the plist!");
                      }
                      plist_free(subsidiary_plist);
                    } else {
                      debug(1, "Can't access the plist!");
                    }
                  }
                  if (buff != NULL)
                    free(buff);
                }
              }
            } else {
              debug(1, "Connection %d: POST /command no mrSupportedCommandsFromSender item.",
                    conn->connection_number);
            }
          } else {
            debug(1, "Connection %d: POST /command no params dict.", conn->connection_number);
          }
          resp->respcode = 200;
        } else {
          debug(1, "Connection %d: POST /command plist type \"%s\" received.",
                conn->connection_number, typeValue);
          debug_log_rtsp_message(2, NULL, req);
        }
        if (typeValue != NULL)
          free(typeValue);
      } else {
        debug(2, "Connection %d: Could not find a \"type\" item.", conn->connection_number);
      }

      plist_free(command_dict);
    } else {
      debug(1, "Connection %d: POST /command plist cannot be inputted.", conn->connection_number);
    }
  } else {
    debug(1, "Connection %d: POST /command contains no plist", conn->connection_number);
  }
}

void handle_audio_mode(rtsp_conn_info *conn, rtsp_message *req,
                       __attribute__((unused)) rtsp_message *resp) {
  debug(2, "Connection %d: POST %s Content-Length %d", conn->connection_number, req->path,
        req->contentlength);
  debug_log_rtsp_message(3, NULL, req);
}

void handle_post(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  resp->respcode = 200;
  if (strcmp(req->path, "/pair-setup") == 0) {
    handle_pair_setup(conn, req, resp);
  } else if (strcmp(req->path, "/pair-verify") == 0) {
    handle_pair_verify(conn, req, resp);
  } else if (strcmp(req->path, "/pair-add") == 0) {
    handle_pair_add(conn, req, resp);
  } else if (strcmp(req->path, "/pair-remove") == 0) {
    handle_pair_remove(conn, req, resp);
  } else if (strcmp(req->path, "/pair-list") == 0) {
    handle_pair_list(conn, req, resp);
  } else if (strcmp(req->path, "/pair-pin-start") == 0) {
    handle_pair_pin_start(conn, req, resp);
  } else if (strcmp(req->path, "/fp-setup") == 0) {
    handle_fp_setup(conn, req, resp);
  } else if (strcmp(req->path, "/configure") == 0) {
    handle_configure(conn, req, resp);
  } else if (strcmp(req->path, "/feedback") == 0) {
    handle_feedback(conn, req, resp);
  } else if (strcmp(req->path, "/command") == 0) {
    handle_command(conn, req, resp);
  } else if (strcmp(req->path, "/audioMode") == 0) {
    handle_audio_mode(conn, req, resp);
  } else {
    debug(1, "Connection %d: Unhandled POST %s Content-Length %d", conn->connection_number,
          req->path, req->contentlength);
    debug_log_rtsp_message(2, "POST request", req);
  }
}

void handle_setpeers(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(2, "Connection %d: SETPEERS %s Content-Length %d", conn->connection_number, req->path,
        req->contentlength);
  debug_log_rtsp_message(3, "SETPEERS request", req);
  /*
    char timing_list_message[4096];
    timing_list_message[0] = 'T';
    timing_list_message[1] = 0;

    // ensure the client itself is first -- it's okay if it's duplicated later
    strncat(timing_list_message, " ", sizeof(timing_list_message) - 1 -
    strlen(timing_list_message)); strncat(timing_list_message, (const char
    *)&conn->client_ip_string, sizeof(timing_list_message) - 1 - strlen(timing_list_message));

    plist_t addresses_array = NULL;
    plist_from_memory(req->content, req->contentlength, &addresses_array);
    uint32_t items = plist_array_get_size(addresses_array);
    if (items) {
      uint32_t item;
      for (item = 0; item < items; item++) {
        plist_t n = plist_array_get_item(addresses_array, item);
        char *ip_address = NULL;
        plist_get_string_val(n, &ip_address);
        // debug(1,ip_address);
        strncat(timing_list_message, " ",
                sizeof(timing_list_message) - 1 - strlen(timing_list_message));
        strncat(timing_list_message, ip_address,
                sizeof(timing_list_message) - 1 - strlen(timing_list_message));
        if (ip_address != NULL)
          free(ip_address);
      }
      ptp_send_control_message_string(timing_list_message);
    }
    plist_free(addresses_array);
  */
  // set_client_as_ptp_clock(conn);
  resp->respcode = 200;
}
void handle_setpeersx(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(2, "Connection %d: SETPEERSX %s Content-Length %d", conn->connection_number, req->path,
        req->contentlength);
  debug_log_rtsp_message(2, "SETPEERS Xrequest", req);
  resp->respcode = 200;
}
#endif

void handle_options(rtsp_conn_info *conn, __attribute__((unused)) rtsp_message *req,
                    rtsp_message *resp) {
  debug_log_rtsp_message(2, "OPTIONS request", req);
  debug(3, "Connection %d: OPTIONS", conn->connection_number);
  resp->respcode = 200;
  msg_add_header(resp, "Public",
                 "ANNOUNCE, SETUP, RECORD, "
                 "PAUSE, FLUSH, TEARDOWN, "
                 "OPTIONS, GET_PARAMETER, SET_PARAMETER");
}

void handle_teardown(rtsp_conn_info *conn, __attribute__((unused)) rtsp_message *req,
                     rtsp_message *resp) {
  debug(2, "Connection %d: TEARDOWN (Classic AirPlay)", conn->connection_number);
  debug_log_rtsp_message(2, "TEARDOWN (Classic AirPlay) request", req);

  // most of the cleanup here is done by the exiting player_thread, if any, and by the event
  // receiver if and when it exits.

  if (conn->player_thread) {
    debug(2, "TEARDOWN is stopping a player thread before exiting...");
    player_stop(conn);                    // this nulls the player_thread and cancels the threads...
    activity_monitor_signify_activity(0); // inactive, and should be after command_stop()
  }

  resp->respcode = 200;
  msg_add_header(resp, "Connection", "close");
  // debug(1,"Bogus exit for valgrind -- remember to comment it out!.");
  // exit(EXIT_SUCCESS);
}

#ifdef CONFIG_AIRPLAY_2
void handle_options_2(rtsp_conn_info *conn, __attribute__((unused)) rtsp_message *req,
                      rtsp_message *resp) {
  debug_log_rtsp_message(2, "OPTIONS request", req);
  debug(3, "Connection %d: OPTIONS", conn->connection_number);
  resp->respcode = 200;
  msg_add_header(resp, "Public",
                 "ANNOUNCE, SETUP, RECORD, "
                 "PAUSE, FLUSH, FLUSHBUFFERED, TEARDOWN, "
                 "OPTIONS, POST, GET, PUT");
}

// TEARDOWN and TEARDOWN for AP2 look the same!

void handle_teardown_2(rtsp_conn_info *conn, __attribute__((unused)) rtsp_message *req,
                       rtsp_message *resp) {

  debug(2, "Connection %d: TEARDOWN 2 %s.", conn->connection_number,
        get_category_string(conn->airplay_stream_category));
  debug_log_rtsp_message(2, "TEARDOWN 2: ", req);

  if (conn->player_thread) {
    debug(2, "TEARDOWN 2 is stopping a player thread before exiting...");
    player_stop(conn);                    // this nulls the player_thread and cancels the threads...
    activity_monitor_signify_activity(0); // inactive, and should be after command_stop()
  }
  resp->respcode = 200;
  msg_add_header(resp, "Connection", "close");

  // debug(1,"Bogus exit for valgrind -- remember to comment it out!.");
  // sps_shutdown(TOE_normal); // ask for a normal exit
}
#endif

void handle_flush(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug_log_rtsp_message(2, "FLUSH request", req);
  debug(3, "Connection %d: FLUSH", conn->connection_number);
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
  debug(2, "RTSP Flush Requested: %u.", rtptime);
  if ((conn != NULL) && (conn == principal_conn)) {
#ifdef CONFIG_METADATA
    if (p)
      send_metadata('ssnc', 'flsr', p + 1, strlen(p + 1), req, 1);
    else
      send_metadata('ssnc', 'flsr', NULL, 0, NULL, 0);
#endif

    player_flush(rtptime, conn); // will not crash even it there is no player thread.
    resp->respcode = 200;

  } else {
    warn("Connection %d FLUSH %u received without having the player", conn->connection_number,
         rtptime);
    resp->respcode = 451;
  }
}

#ifdef CONFIG_AIRPLAY_2

#ifdef CONFIG_METADATA
static void check_and_send_plist_metadata(plist_t messagePlist, const char *plist_key,
                                          uint32_t metadata_code) {
  plist_t item = plist_dict_get_item(messagePlist, plist_key);
  if (item) {
    char *value;
    plist_get_string_val(item, &value);
    if (value != NULL) {
      send_metadata('ssnc', metadata_code, value, strlen(value), NULL, 0);
      free(value);
    }
  }
}
#endif

void handle_setup_2(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  int err;
  debug(2, "Connection %d: SETUP (AirPlay 2) on %s", conn->connection_number,
        get_category_string(conn->airplay_stream_category));
  debug_log_rtsp_message(2, "SETUP (AirPlay 2) incoming message", req);

  plist_t messagePlist = plist_from_rtsp_content(req);
  plist_t setupResponsePlist = plist_new_dict();
  resp->respcode = 501;

  // see if we can get a name for the client
  char *clientNameString = NULL;

  plist_t nameItem = plist_dict_get_item(messagePlist, "name");
  if (nameItem != NULL) {
    plist_get_string_val(nameItem, &conn->ap2_client_name); // generates a malloced string
    clientNameString = strdup(conn->ap2_client_name);
  } else {
    clientNameString = strdup("<unknown>");
  }

  // see if the incoming plist contains a "streams" array
  plist_t streams = plist_dict_get_item(messagePlist, "streams");
  if (streams == NULL) {
    // no "streams" plist, so it must (?) be an initial setup
    debug(2,
          "Connection %d SETUP: No \"streams\" array has been found -- create an event thread "
          "and open a TCP port.",
          conn->connection_number);
    conn->airplay_stream_category = unspecified_stream_category;

    // figure out what category of stream it is, by looking at the plist
    plist_t timingProtocol = plist_dict_get_item(messagePlist, "timingProtocol");
    if (timingProtocol != NULL) {
      char *timingProtocolString = NULL;
      plist_get_string_val(timingProtocol, &timingProtocolString);
      if (timingProtocolString) {
        if (strcmp(timingProtocolString, "PTP") == 0) {
          debug(2, "Connection %d: AP2 PTP connection from %s:%u (\"%s\") to self at %s:%u.",
                conn->connection_number, conn->client_ip_string, conn->client_rtsp_port,
                clientNameString, conn->self_ip_string, conn->self_rtsp_port);
          conn->airplay_stream_category = ptp_stream;
          conn->timing_type = ts_ptp;

          do_pthread_setname(&conn->thread, "ap2_ptp_%d", conn->connection_number);

        } else if (strcmp(timingProtocolString, "NTP") == 0) {
          debug(1, "Connection %d: SETUP: NTP setup from %s:%u (\"%s\") to self at %s:%u.",
                conn->connection_number, conn->client_ip_string, conn->client_rtsp_port,
                clientNameString, conn->self_ip_string, conn->self_rtsp_port);
          conn->airplay_stream_category = ntp_stream;
          conn->timing_type = ts_ntp;
          do_pthread_setname(&conn->thread, "ap2_ntp_%d", conn->connection_number);
        } else if (strcmp(timingProtocolString, "None") == 0) {
          debug(3,
                "Connection %d: SETUP: a \"None\" setup detected from %s:%u (\"%s\") to self at "
                "%s:%u.",
                conn->connection_number, conn->client_ip_string, conn->client_rtsp_port,
                clientNameString, conn->self_ip_string, conn->self_rtsp_port);
          // now check to see if it's got the "isRemoteControlOnly" item and check it's true
          plist_t isRemoteControlOnly = plist_dict_get_item(messagePlist, "isRemoteControlOnly");
          if (isRemoteControlOnly != NULL) {
            uint8_t isRemoteControlOnlyBoolean = 0;
            plist_get_bool_val(isRemoteControlOnly, &isRemoteControlOnlyBoolean);
            if (isRemoteControlOnlyBoolean != 0) {
              debug(2,
                    "Connection %d: SETUP: Remote Control Only connection from %s:%u (\"%s\") to "
                    "self at %s:%u.",
                    conn->connection_number, conn->client_ip_string, conn->client_rtsp_port,
                    clientNameString, conn->self_ip_string, conn->self_rtsp_port);
              conn->airplay_stream_category = remote_control_stream;
              do_pthread_setname(&conn->thread, "ap2_rc_%d", conn->connection_number);
            } else {
              debug(1,
                    "Connection %d: SETUP: a \"None\" setup detected, with "
                    "\"isRemoteControlOnly\" item set to \"false\".",
                    conn->connection_number);
            }
          } else {
            debug(1,
                  "Connection %d: SETUP: a \"None\" setup detected, but no "
                  "\"isRemoteControlOnly\" item detected.",
                  conn->connection_number);
          }
        }

        // here, we know it's an initial setup and we know the kind of setup being requested
        // if it's a full service PTP stream, we get groupUUID, groupContainsGroupLeader and
        // timingPeerList
        if (conn->airplay_stream_category == ptp_stream) {

          // airplay 2 always allows interruption, so should never return
          // play_lock_aquisition_failed
          if (get_play_lock(conn, 1) != play_lock_aquisition_failed) {
            debug(2, "Connection %d: %s AP2 setup -- play lock acquired.", conn->connection_number,
                  get_category_string(conn->airplay_stream_category));

#ifdef CONFIG_METADATA
            send_ssnc_metadata('conn', conn->client_ip_string, strlen(conn->client_ip_string),
                               1); // before disconnecting an existing play
            send_ssnc_metadata('clip', conn->client_ip_string, strlen(conn->client_ip_string), 1);
            send_ssnc_metadata('svip', conn->self_ip_string, strlen(conn->self_ip_string), 1);
#endif

            if (ptp_shm_interface_open() !=
                0) // it should be open already, but just in case it isn't...
              die("Can not access the NQPTP service. Has it stopped running?");
            debug_log_rtsp_message(3, "SETUP \"PTP\" message", req);
            plist_t groupUUID = plist_dict_get_item(messagePlist, "groupUUID");
            if (groupUUID) {
              char *gid = NULL;
              plist_get_string_val(groupUUID, &gid);
              if (gid) {
                if (conn->airplay_gid)
                  free(conn->airplay_gid);
                conn->airplay_gid = gid; // it'll be free'd later on...
              } else {
                debug(1, "Invalid groupUUID");
              }
            } else {
              debug(1, "No groupUUID in SETUP");
            }

            // now see if the group contains a group leader
            plist_t groupContainsGroupLeader =
                plist_dict_get_item(messagePlist, "groupContainsGroupLeader");
            if (groupContainsGroupLeader) {
              uint8_t value = 0;
              plist_get_bool_val(groupContainsGroupLeader, &value);
              conn->groupContainsGroupLeader = value;
              debug(3, "Updated groupContainsGroupLeader to %u", conn->groupContainsGroupLeader);
            } else {
              debug(1, "No groupContainsGroupLeader in SETUP");
            }

            char timing_list_message[4096];
            timing_list_message[0] = 'T';
            timing_list_message[1] = 0;

            // ensure the client itself is first -- it's okay if it's duplicated later
            strncat(timing_list_message, " ",
                    sizeof(timing_list_message) - 1 - strlen(timing_list_message));
            strncat(timing_list_message, (const char *)&conn->client_ip_string,
                    sizeof(timing_list_message) - 1 - strlen(timing_list_message));

            plist_t timing_peer_info = plist_dict_get_item(messagePlist, "timingPeerInfo");
            if (timing_peer_info) {
              // first, get the incoming plist.
              plist_t addresses_array = plist_dict_get_item(timing_peer_info, "Addresses");
              if (addresses_array) {
                // iterate through the array of items
                uint32_t items = plist_array_get_size(addresses_array);
                if (items) {
                  uint32_t item;
                  for (item = 0; item < items; item++) {
                    plist_t n = plist_array_get_item(addresses_array, item);
                    char *ip_address = NULL;
                    plist_get_string_val(n, &ip_address);
                    // debug(1, "Timing peer: %s", ip_address);
                    strncat(timing_list_message, " ",
                            sizeof(timing_list_message) - 1 - strlen(timing_list_message));
                    strncat(timing_list_message, ip_address,
                            sizeof(timing_list_message) - 1 - strlen(timing_list_message));
                    free(ip_address);
                  }
                } else {
                  debug(1, "SETUP on Connection %d: No timingPeerInfo addresses in the array.",
                        conn->connection_number);
                }
              } else {
                debug(1, "SETUP on Connection %d: Can't find timingPeerInfo addresses",
                      conn->connection_number);
              }
              // make up the timing peer info list part of the response...
              // debug(1,"Create timingPeerInfoPlist");
              plist_t timingPeerInfoPlist = plist_new_dict();
              plist_t addresses = plist_new_array(); // to hold the device's interfaces
              plist_array_append_item(addresses, plist_new_string(conn->self_ip_string));
              //            debug(1,"self ip: \"%s\"", conn->self_ip_string);

              struct ifaddrs *addrs, *iap;
              getifaddrs(&addrs);
              for (iap = addrs; iap != NULL; iap = iap->ifa_next) {
                // debug(1, "Interface index %d, name: \"%s\"",if_nametoindex(iap->ifa_name),
                // iap->ifa_name);
                if ((iap->ifa_addr) && (iap->ifa_netmask) && (iap->ifa_flags & IFF_UP) &&
                    ((iap->ifa_flags & IFF_LOOPBACK) == 0)) {
                  char buf[INET6_ADDRSTRLEN + 1]; // +1 for a NUL
                  memset(buf, 0, sizeof(buf));
                  if (iap->ifa_addr->sa_family == AF_INET6) {
                    struct sockaddr_in6 *addr6 = (struct sockaddr_in6 *)(iap->ifa_addr);
                    inet_ntop(AF_INET6, (void *)&addr6->sin6_addr, buf, sizeof(buf));
                    plist_array_append_item(addresses, plist_new_string(buf));
                    // debug(1, "Own address IPv6: %s", buf);

                    // strncat(timing_list_message, " ",
                    // sizeof(timing_list_message) - 1 - strlen(timing_list_message));
                    // strncat(timing_list_message, buf,
                    // sizeof(timing_list_message) - 1 - strlen(timing_list_message));

                  } else {
                    struct sockaddr_in *addr = (struct sockaddr_in *)(iap->ifa_addr);
                    inet_ntop(AF_INET, (void *)&addr->sin_addr, buf, sizeof(buf));
                    plist_array_append_item(addresses, plist_new_string(buf));
                    // debug(1, "Own address IPv4: %s", buf);

                    // strncat(timing_list_message, " ",
                    // sizeof(timing_list_message) - 1 - strlen(timing_list_message));
                    // strncat(timing_list_message, buf,
                    // sizeof(timing_list_message) - 1 - strlen(timing_list_message));
                  }
                }
              }
              freeifaddrs(addrs);

              // debug(1,"initial timing peer command: \"%s\".", timing_list_message);
              // ptp_send_control_message_string(timing_list_message);
              // deferring this until play is about to start
              // set_client_as_ptp_clock(conn);
              // ptp_send_control_message_string("B"); // signify clock dependability period is
              // "B"eginning (or continuing)
              plist_dict_set_item(timingPeerInfoPlist, "Addresses", addresses);
              plist_dict_set_item(timingPeerInfoPlist, "ID",
                                  plist_new_string(conn->self_ip_string));
              plist_dict_set_item(setupResponsePlist, "timingPeerInfo", timingPeerInfoPlist);
              // get a port to use as an event port
              // bind a new TCP port and get a socket
              conn->local_event_port = 0; // any port
              int lerr = bind_socket_and_port(SOCK_STREAM, conn->connection_ip_family,
                                              conn->self_ip_string, conn->self_scope_id,
                                              &conn->local_event_port, &conn->event_socket);
              if (lerr) {
                die("SETUP on Connection %d: Error %d: could not find a TCP port to use as an "
                    "event "
                    "port",
                    conn->connection_number, lerr);
              }

              listen(conn->event_socket, 128); // ensure socket is open before telling client

              debug(2, "Connection %d: TCP PTP event port opened: %u.", conn->connection_number,
                    conn->local_event_port);

              if (conn->rtp_event_thread != NULL)
                debug(1, "previous rtp_event_thread allocation not freed, it seems.");
              conn->ap2_event_receiver_exited = 0;
              conn->rtp_event_thread = malloc(sizeof(pthread_t));
              if (conn->rtp_event_thread == NULL)
                die("Couldn't allocate space for pthread_t");

              named_pthread_create(conn->rtp_event_thread, NULL, &ap2_event_receiver, (void *)conn,
                                   "ap2_ptp_evt_%d", conn->connection_number);
              plist_dict_set_item(setupResponsePlist, "eventPort",
                                  plist_new_uint(conn->local_event_port));
              plist_dict_set_item(setupResponsePlist, "timingPort", plist_new_uint(0)); // dummy
              // cancel_all_RTSP_threads(ptp_stream,
              //                         conn->connection_number); // kill all the other listeners
              resp->respcode = 200;
            } else {
              debug(1, "SETUP on Connection %d: PTP setup -- no timingPeerInfo plist.",
                    conn->connection_number);
            }

            // since the GID from the client has been acquired, update the airplay bonjour strings.
            build_bonjour_strings(conn);
            debug(2, "Connection %d: SETUP mdns_update on %s.", conn->connection_number,
                  get_category_string(conn->airplay_stream_category));
            mdns_update(NULL, secondary_txt_records);

#ifdef CONFIG_METADATA
            check_and_send_plist_metadata(messagePlist, "name", 'snam');
            check_and_send_plist_metadata(messagePlist, "deviceID", 'cdid');
            check_and_send_plist_metadata(messagePlist, "model", 'cmod');
            check_and_send_plist_metadata(messagePlist, "macAddress", 'cmac');
#endif
          } else {
            // this should never happen!
            debug(1, "SETUP on Connection %d: could not become principal conn.",
                  conn->connection_number);
            resp->respcode = 453;
          }
        } else if (conn->airplay_stream_category == ntp_stream) {
          debug(1, "SETUP on Connection %d: ntp stream handling is not implemented!",
                conn->connection_number);
          warn("Shairport Sync can not handle NTP streams.");
        } else if (conn->airplay_stream_category == remote_control_stream) {

          debug_log_rtsp_message(3, "SETUP (no stream) \"isRemoteControlOnly\" message", req);

          // get a port to use as an event port
          // bind a new TCP port and get a socket
          conn->local_event_port = 0; // any port
          int lerr = bind_socket_and_port(SOCK_STREAM, conn->connection_ip_family,
                                          conn->self_ip_string, conn->self_scope_id,
                                          &conn->local_event_port, &conn->event_socket);
          if (lerr) {
            die("SETUP on Connection %d: Error %d: could not find a TCP port to use as an event "
                "port",
                conn->connection_number, lerr);
          }

          listen(conn->event_socket, 128); // ensure socket is open before telling client

          debug(2, "Connection %d SETUP (RC): TCP Remote Control event port opened: %u.",
                conn->connection_number, conn->local_event_port);
          if (conn->rtp_event_thread != NULL)
            debug(1,
                  "Connection %d SETUP (RC): previous rtp_event_thread allocation not freed, it "
                  "seems.",
                  conn->connection_number);
          conn->ap2_event_receiver_exited = 0;
          conn->rtp_event_thread = malloc(sizeof(pthread_t));
          if (conn->rtp_event_thread == NULL)
            die("Couldn't allocate space for pthread_t");
          named_pthread_create(conn->rtp_event_thread, NULL, &ap2_rc_event_receiver, (void *)conn,
                               "ap2_rc_evt_%d", conn->connection_number);
          plist_dict_set_item(setupResponsePlist, "eventPort",
                              plist_new_uint(conn->local_event_port));
          debug(2, "SETUP on Connection %d: RemoteControl Only eventPort %u.",
                conn->connection_number, conn->local_event_port);
          // plist_dict_set_item(setupResponsePlist, "timingPort", plist_new_uint(0));
          //  cancel_all_RTSP_threads(
          //     remote_control_stream,
          //     conn->connection_number); // kill all the other remote control listeners

          resp->respcode = 200;
        } else {
          debug(1, "SETUP on Connection %d: an unrecognised \"%s\" setup detected.",
                conn->connection_number, timingProtocolString);
          warn("Shairport Sync can not handle streams of this type: \"%s\".", timingProtocolString);
        }
        free(timingProtocolString);
      } else {
        debug(1, "SETUP on Connection %d: Can't retrieve timingProtocol string in initial SETUP.",
              conn->connection_number);
      }
    } else {
      debug(1,
            "SETUP on Connection %d: Unrecognised SETUP incoming message from \"%s\": no "
            "timingProtocol or streams plist found.",
            conn->connection_number, (const char *)conn->client_ip_string);
      debug_log_rtsp_message(1, "Unrecognised SETUP incoming message.", req);
      warn("Unrecognised SETUP incoming message -- ignored.");
    }
  } else {
    debug(2, "Connection %d: SETUP on %s. A \"streams\" array has been found",
          conn->connection_number, get_category_string(conn->airplay_stream_category));
    debug_log_rtsp_message(3, "SETUP (AirPlay 2) SETUP with streams incoming message", req);
    if (conn->airplay_stream_category == ptp_stream) {

      if (conn->player_thread) {
        debug(1, "stopping a running player during setup phase 2");
        player_stop(conn); // this nulls the player_thread and cancels the threads...
        activity_monitor_signify_activity(0); // inactive, and should be after command_stop()
      }

      set_client_as_ptp_clock(conn);
      ptp_send_control_message_string(
          "B"); // signify clock dependability period is "B"eginning (or continuing)
      plist_t stream0 = plist_array_get_item(streams, 0);

      plist_t streams_array = plist_new_array(); // to hold the ports and stuff
      plist_t stream0dict = plist_new_dict();

      // get the session key -- it must have one

      plist_t item = plist_dict_get_item(stream0, "shk"); // session key
      uint64_t item_value = 0;
      if (item != NULL) {
        plist_get_data_val(item, (char **)&conn->session_key,
                           &item_value); // item_value is the session key length (?)
      } else {
        warn("No session key (shk) property in setup! This is fatal!");
      }

      // get the compression type
      // this seems to be static -- a stream's encoding can change dynamically, it seems
      item = plist_dict_get_item(stream0, "ct"); // compression type
      if (item != NULL) {
        plist_get_uint_val(item, &item_value);
        conn->compressionType = item_value;
        // see https://emanuelecozzi.net/docs/airplay2/audio/ for values
      } else {
        debug(1, "No compression type (ct) property found in setup.");
      }

      // get the max frames per packet
      item = plist_dict_get_item(stream0, "spf"); // samples per frame (?)
      if (item != NULL) {
        plist_get_uint_val(item, &item_value);
        // see https://emanuelecozzi.net/docs/airplay2/audio/ for values
        debug(3, "Frames per packet (aka spf (\"samples per frame\"?): %" PRId64 ".", item_value);
        conn->frames_per_packet = item_value;
      } else {
        warn("No frames per packet (spf) property found in setup!");
      }

      // bind a new UDP port and get a socket
      conn->local_ap2_control_port = 0; // any port
      err = bind_socket_and_port(SOCK_DGRAM, conn->connection_ip_family, conn->self_ip_string,
                                 conn->self_scope_id, &conn->local_ap2_control_port,
                                 &conn->ap2_control_socket);
      if (err) {
        die("Error %d: could not find a UDP port to use as an ap2_control port", err);
      }
      debug(2, "Connection %d: UDP control port opened: %u.", conn->connection_number,
            conn->local_ap2_control_port);

      named_pthread_create(&conn->rtp_ap2_control_thread, NULL, &rtp_ap2_control_receiver,
                           (void *)conn, "ap2_cn_%d", conn->connection_number);

      // get the DACP-ID and Active Remote for remote control stuff

      char *ar = msg_get_header(req, "Active-Remote");
      if (ar) {
        debug(3, "Connection %d: SETUP AP2 -- Active-Remote string seen: \"%s\".",
              conn->connection_number, ar);
        // get the active remote
        if (conn->dacp_active_remote) // this is in case SETUP was previously called
          free(conn->dacp_active_remote);
        conn->dacp_active_remote = strdup(ar);
#ifdef CONFIG_METADATA
        send_metadata('ssnc', 'acre', ar, strlen(ar), req, 1);
#endif
      } else {
        debug(2, "Connection %d: SETUP AP2 no Active-Remote information in the the SETUP Record.",
              conn->connection_number);
        if (conn->dacp_active_remote) { // this is in case SETUP was previously called
          free(conn->dacp_active_remote);
          conn->dacp_active_remote = NULL;
        }
      }

      ar = msg_get_header(req, "DACP-ID");
      if (ar) {
        debug(3, "Connection %d: SETUP AP2 -- DACP-ID string seen: \"%s\".",
              conn->connection_number, ar);
        if (conn->dacp_id) // this is in case SETUP was previously called
          free(conn->dacp_id);
        conn->dacp_id = strdup(ar);
#ifdef CONFIG_METADATA
        send_metadata('ssnc', 'daid', ar, strlen(ar), req, 1);
#endif
      } else {
        debug(2, "Connection %d: SETUP AP2 doesn't include DACP-ID string information.",
              conn->connection_number);
        if (conn->dacp_id) { // this is in case SETUP was previously called
          free(conn->dacp_id);
          conn->dacp_id = NULL;
        }
      }

      // now, get the type of the stream.
      item = plist_dict_get_item(stream0, "type");
      item_value = 0;
      plist_get_uint_val(item, &item_value);
      conn->type = item_value;
      switch (item_value) {
      case 96: {
        debug(1, "Connection %d. AP2 Realtime ALAC/44100/S16/2 Stream.", conn->connection_number);
        debug_log_rtsp_message(2, "Realtime Audio Stream SETUP incoming message", req);

        conn->stream.type = ast_apple_lossless;
        conn->airplay_stream_type = realtime_stream;
        // get the sample rate
        item = plist_dict_get_item(stream0, "sr"); // sample rate
        if (item != NULL) {
          plist_get_uint_val(item, &item_value);
          // see https://emanuelecozzi.net/docs/airplay2/audio/ for values
          debug(2, "Sample rate: %" PRId64 ".", item_value);
          conn->input_rate = item_value;
        } else {
          debug(1, "No sample rate (sr) property found in setup.");
        }
        conn->frames_per_packet = 352;

        // bind a new UDP port and get a socket
        conn->local_realtime_audio_port = 0; // any port
        err = bind_socket_and_port(SOCK_DGRAM, conn->connection_ip_family, conn->self_ip_string,
                                   conn->self_scope_id, &conn->local_realtime_audio_port,
                                   &conn->realtime_audio_socket);
        if (err) {
          die("Error %d: could not find a UDP port to use as a realtime_audio port", err);
        }
        debug(2, "Connection %d: UDP realtime audio port opened: %u.", conn->connection_number,
              conn->local_realtime_audio_port);

        named_pthread_create(&conn->rtp_realtime_audio_thread, NULL, &rtp_realtime_audio_receiver,
                             (void *)conn, "ap2_rat_%d", conn->connection_number);
        plist_dict_set_item(stream0dict, "type", plist_new_uint(96));
        plist_dict_set_item(stream0dict, "dataPort",
                            plist_new_uint(conn->local_realtime_audio_port));

        debug(2, "Realtime Stream Play");
        activity_monitor_signify_activity(1);
        player_play(conn);

        conn->rtp_running = 1; // hack!
      } break;
      case 103: {
        debug_log_rtsp_message(3, "Buffered Audio Stream SETUP incoming message", req);
        conn->airplay_stream_type = buffered_stream;

        // get the audio format code
        item = plist_dict_get_item(stream0, "audioFormat"); // audio format
        if (item != NULL) {
          plist_get_uint_val(item, &conn->audio_format);
          // see https://emanuelecozzi.net/docs/airplay2/audio/ for values
          // seems to be only the initial format -- it seems as if it can change dynamically
        } else {
          debug(1, "No audio format (audioFormat) property found in setup.");
        }

        // bind a new TCP port and get a socket
        conn->local_buffered_audio_port = 0; // any port
        err = bind_socket_and_port(SOCK_STREAM, conn->connection_ip_family, conn->self_ip_string,
                                   conn->self_scope_id, &conn->local_buffered_audio_port,
                                   &conn->buffered_audio_socket);
        if (err) {
          die("SETUP on Connection %d: Error %d: could not find a TCP port to use as a "
              "buffered_audio port",
              conn->connection_number, err);
        }

        listen(conn->buffered_audio_socket, 128); // ensure it's open before telling the client

        debug(2, "Connection %d: TCP Buffered Audio port opened: %u.", conn->connection_number,
              conn->local_buffered_audio_port);

        activity_monitor_signify_activity(1);

        // debug(1, "Connection %d: create rtp_buffered_audio_thread", conn->connection_number);

        named_pthread_create_with_priority(&conn->rtp_buffered_audio_thread, 2,
                                           &rtp_buffered_audio_processor, (void *)conn,
                                           "ap2_bat_%d", conn->connection_number);

        plist_dict_set_item(stream0dict, "type", plist_new_uint(103));
        plist_dict_set_item(stream0dict, "dataPort",
                            plist_new_uint(conn->local_buffered_audio_port));
        plist_dict_set_item(stream0dict, "audioBufferSize",
                            plist_new_uint(conn->ap2_audio_buffer_size));

        // this should be cancelled by an activity_monitor_signify_activity(1)
        // call in the SETRATEANCHORI handler, which should come up right away
        activity_monitor_signify_activity(0);
        player_play(conn);
        conn->rtp_running = 1; // hack!
      } break;
      case 130: {
        debug(1, "Remote Control Setup Received on a PTP connection.");
        debug_log_rtsp_message(2, "Incoming message", req);
      } break;
      default:
        debug(1, "SETUP on Connection %d: Unhandled stream type %" PRIu64 ".",
              conn->connection_number, item_value);
        debug_log_rtsp_message(1, "Unhandled stream type incoming message", req);
      }

      plist_dict_set_item(stream0dict, "controlPort", plist_new_uint(conn->local_ap2_control_port));

      plist_array_append_item(streams_array, stream0dict);
      plist_dict_set_item(setupResponsePlist, "streams", streams_array);

      resp->respcode = 200;
    } else if (conn->airplay_stream_category == remote_control_stream) {
      debug(3, "Connection %d (RC): SETUP: Remote Control Only with stream received from %s.",
            conn->connection_number, conn->client_ip_string);
      debug_log_rtsp_message(3, "Remote Control Stream SETUP incoming message", req);

      plist_t seed_item = NULL;
      // the data port and listener thread may already have been set up
      // if so, the local_data_port will be non-zero

      if (conn->local_data_port == 0) {
        // set up data channel ciphering
        plist_t dict = plist_array_get_item(streams, 0);
        if (dict != NULL) {
          // get the seed that becomes the suffix for the salt
          seed_item = plist_dict_get_item(dict, "seed"); // session key
          uint64_t seed = 0;
          if (seed_item != NULL) {
            plist_get_uint_val(seed_item, &seed);
            char salt_suffix[256] = "";
            snprintf(salt_suffix, sizeof(salt_suffix), "%" PRIu64 "", seed);
            conn->ap2_pairing_context.data_cipher_bundle.cipher_ctx =
                pair_cipher_new(PAIR_SERVER_HOMEKIT, 5, conn->pair_setup_result->shared_secret,
                                conn->pair_setup_result->shared_secret_len,
                                salt_suffix); // last argument is the (possible) dynamic salt suffix
            if (conn->ap2_pairing_context.data_cipher_bundle.cipher_ctx != NULL) {
              conn->ap2_pairing_context.data_cipher_bundle.description = strdup("DataStream");
              // get a port to use as an data port
              // bind a new TCP port and get a socket
              conn->local_data_port = 0; // any port
              int lerr = bind_socket_and_port(SOCK_STREAM, conn->connection_ip_family,
                                              conn->self_ip_string, conn->self_scope_id,
                                              &conn->local_data_port, &conn->data_socket);
              if (lerr) {
                die("SETUP on Connection %d (RC): Error %d: could not find a TCP port to use as a "
                    "data "
                    "port",
                    conn->connection_number, lerr);
              }
              listen(conn->data_socket,
                     128); // open port for listening before telling the client about it!

              debug(2, "Connection %d SETUP (RC): TCP Remote Control data port opened: %u.",
                    conn->connection_number, conn->local_data_port);
            } else {
              debug(1, "Connection %d: SETUP: Error setting up rtsp data channel ciphering.",
                    conn->connection_number);
            }
          } else {
            debug(2, "Connection %d: SETUP: No data channel encryption salt seed found.",
                  conn->connection_number);
          }
        } else {
          debug(1, "Connection %d: SETUP: Could not find the streams array",
                conn->connection_number);
        }
      } else {
        debug(1, "Connection %d SETUP (RC): data port already allocated.", conn->connection_number);
      }
      plist_t coreResponseDict = plist_new_dict();
      plist_dict_set_item(coreResponseDict, "streamID", plist_new_uint(1));
      plist_dict_set_item(coreResponseDict, "type", plist_new_uint(130));
      if (seed_item != NULL)
        plist_dict_set_item(coreResponseDict, "dataPort", plist_new_uint(conn->local_data_port));

      plist_t coreResponseArray = plist_new_array();
      plist_array_append_item(coreResponseArray, coreResponseDict);
      plist_dict_set_item(setupResponsePlist, "streams", coreResponseArray);

      resp->respcode = 200;
    } else {
      debug(1, "Connection %d: SETUP: Stream received but no airplay category set. Nothing done.",
            conn->connection_number);
    }
  }

  if (resp->respcode == 200) {
    plist_to_bin(setupResponsePlist, &resp->content, &resp->contentlength);
    plist_free(setupResponsePlist);
    msg_add_header(resp, "Content-Type", "application/x-apple-binary-plist");
  }
  plist_free(messagePlist);
  if (clientNameString != NULL)
    free(clientNameString);
}
#endif

void handle_setup(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(3, "Connection %d: SETUP", conn->connection_number);
  resp->respcode = 451; // invalid arguments -- expect them
  // check this connection has the principal_conn, obtained during a prior ANNOUNCE
  if ((conn != NULL) && (principal_conn == conn)) {
    uint16_t cport, tport;
    char *ar = msg_get_header(req, "Active-Remote");
    if (ar) {
      debug(2, "Connection %d: SETUP: Active-Remote string seen: \"%s\".", conn->connection_number,
            ar);
      // get the active remote
      if (conn->dacp_active_remote) // this is in case SETUP was previously called
        free(conn->dacp_active_remote);
      conn->dacp_active_remote = strdup(ar);
#ifdef CONFIG_METADATA
      send_metadata('ssnc', 'acre', ar, strlen(ar), req, 1);
#endif
    } else {
      debug(2, "Connection %d: SETUP: Note: no Active-Remote information seen.",
            conn->connection_number);
      if (conn->dacp_active_remote) { // this is in case SETUP was previously called
        free(conn->dacp_active_remote);
        conn->dacp_active_remote = NULL;
      }
    }

    ar = msg_get_header(req, "DACP-ID");
    if (ar) {
      debug(2, "Connection %d: SETUP: DACP-ID string seen: \"%s\".", conn->connection_number, ar);
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
            debug(2,
                  "Connection %d: SETUP DACP-ID \"%s\" from %s to %s with UDP ports Control: "
                  "%d, Timing: %d and Audio: %d.",
                  conn->connection_number, conn->dacp_id, (char *)&conn->client_ip_string,
                  (char *)&conn->self_ip_string, conn->local_control_port, conn->local_timing_port,
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
  } else {
    warn("Connection %d SETUP received without having the player (no ANNOUNCE?)",
         conn->connection_number);
  }
  if (resp->respcode == 200) {
    do_pthread_setname(&conn->thread, "rtsp_1_%d", conn->connection_number);
#ifdef CONFIG_METADATA
    send_ssnc_metadata('clip', conn->client_ip_string, strlen(conn->client_ip_string), 1);
    send_ssnc_metadata('svip', conn->self_ip_string, strlen(conn->self_ip_string), 1);
#endif
  } else {
    debug(1, "Connection %d: SETUP error -- releasing the player lock.", conn->connection_number);
    release_play_lock(conn);
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
      debug(3, "Connection %d: request to set AirPlay Volume to: %f.", conn->connection_number,
            volume);
      // if we are playing, go ahead and change the volume
#ifdef CONFIG_DBUS_INTERFACE
      if (dbus_service_is_running()) {
        shairport_sync_set_volume(shairportSyncSkeleton, volume);
      } else {
#endif
        pthread_rwlock_rdlock(&principal_conn_lock); // don't let the principal_conn be changed
        pthread_cleanup_push(rwlock_unlock, (void *)&principal_conn_lock);
        if (principal_conn == conn) {
          debug(3, "Connection %d: set player volume to %.3f.", conn->connection_number, volume);
          player_volume(volume, conn);
          debug(3, "Connection %d: set player volume to %.3f success.", conn->connection_number,
                volume);
        } else {
          if (principal_conn != NULL)
            debug(1, "Connection %d: fail to set player volume to %.3f. Principal conn is %d.",
                  conn->connection_number, volume, principal_conn->connection_number);
          else
            debug(1, "Connection %d: fail to set player volume to %.3f. Principal conn is NULL.",
                  conn->connection_number, volume);
        }
        if (conn != NULL) {
          conn->own_airplay_volume = volume;
          conn->own_airplay_volume_set = 1;
        }
        pthread_cleanup_pop(1); // release the principal_conn lock
#ifdef CONFIG_DBUS_INTERFACE
      }
#endif
    } else if (strncmp(cp, "progress: ", strlen("progress: ")) ==
               0) { // this can be sent even when metadata is not solicited

#ifdef CONFIG_METADATA
      char *progress = cp + strlen("progress: ");
      // debug(2, "progress: \"%s\"",progress); // rtpstampstart/rtpstampnow/rtpstampend 44100
      // (always?) per second
      send_ssnc_metadata('prgr', progress, strlen(progress), 1);
#endif

    } else {
      debug(1, "Connection %d, unrecognised parameter: \"%s\"\n", conn->connection_number, cp);
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
//    'prsm' -- play stream resume. No arguments. (deprecated)
//    'paus' -- buffered audio stream paused. No arguments.
//    'pres' -- buffered audio stream resumed. No arguments.
//		'pffr' -- the first frame of a play session has been received and has been validly
//              timed. The argument is
//              "<frame_number>/<time_it_should_be_played_in_64_bit_nanoseconds>"."
//		'pdis' -- a discontinuity in the timestamps of incoming frames has been detected.
//              timed. The argument is  "<frame_number>/<discontinuity>".
//              discontinuity is the actual frame number less the expected frame number
//              positive means a gap
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
//		Can be an IPv4 or an IPv6 number. In AirPlay 2 operation, it is sent as soon
//    as the client has exclusive access to the player and after any existing
//    play session has been interrupted and terminated.
//		`conn` -- the payload is the IP number of the client, i.e. the sender of audio.
//		Can be an IPv4 or an IPv6 number. This is an AirPlay-2-only message.
//    It is sent as soon as the client requests access to the player.
//    If Shairport Sync is already playing, this message is sent before the current
//    play session is stopped.
//		`svip` -- the payload is the IP number of the server, i.e. the player itself.
//		Can be an IPv4 or an IPv6 number.
//		`svna` -- the payload is the service name of the player, i.e. the name by
//		which it is seen in the AirPlay menu.
//    `disc` -- the payload is the IP number of the client, i.e. the sender of audio.
//		Can be an IPv4 or an IPv6 number. This is an AirPlay-2-only message.
//    It is sent when a client has been disconnected.
//		`dapo` -- the payload is the port number (as text) on the server to which remote
// control commands should be sent. It is 3689 for iTunes but varies for iOS devices.
//    ``

//		A special sub-protocol is used for sending large data items over UDP
//    If the payload exceeded 4 MB, it is chunked using the following format:
//    "ssnc", "chnk", packet_ix, packet_counts, packet_tag, packet_type, chunked_data.
//    Notice that the number of items is different to the standard

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
  if (metadata_sock != -1) {
    shutdown(metadata_sock, SHUT_RDWR); // we want to immediately deallocate the buffer
    close(metadata_sock);
  }
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
    if (config.metadata_sockmsglength == 24)
      die("A divide by zero almost occurred (config.metadata_sockmsglength = 24).");
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
}

void *metadata_thread_function(__attribute__((unused)) void *ignore) {
  //  #include <syscall.h>
  //  debug(1, "metadata_thread_function PID %d", syscall(SYS_gettid));
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
}

void *metadata_multicast_thread_function(__attribute__((unused)) void *ignore) {
  //  #include <syscall.h>
  //  debug(1, "metadata_multicast_thread_function PID %d", syscall(SYS_gettid));
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
}

void *metadata_hub_thread_function(__attribute__((unused)) void *ignore) {
  //  #include <syscall.h>
  //  debug(1, "metadata_hub_thread_function PID %d", syscall(SYS_gettid));
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
  // debug(2, "metadata_mqtt_thread_cleanup_function done");
}

void *metadata_mqtt_thread_function(__attribute__((unused)) void *ignore) {
  //  #include <syscall.h>
  //  debug(1, "metadata_mqtt_thread_function PID %d", syscall(SYS_gettid));
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
      debug(1, "metadata_init -- error %d (\"%s\") opening pipe: \"%s\".", errno,
            (char *)errorstring, path);
      warn("can not open metadata pipe -- error %d (\"%s\") opening pipe: \"%s\".", errno,
           (char *)errorstring, path);
    }
    free(path);

    // initialise the metadata queues first, otherwise the might be a race condition
    // create a pc_queue for the metadata pipe
    pc_queue_init(&metadata_queue, (char *)&metadata_queue_items, sizeof(metadata_package),
                  metadata_queue_size, "pipe");

    if (named_pthread_create(&metadata_thread, NULL, metadata_thread_function, NULL,
                             "metadata pipe") != 0)
      debug(1, "Failed to create metadata thread!");

    // create a pc_queue for the metadata_multicast_queue
    pc_queue_init(&metadata_multicast_queue, (char *)&metadata_multicast_queue_items,
                  sizeof(metadata_package), metadata_multicast_queue_size, "multicast");
    if (named_pthread_create(&metadata_multicast_thread, NULL, metadata_multicast_thread_function,
                             NULL, "metadata mcst") != 0)
      debug(1, "Failed to create metadata multicast thread!");
  }
#ifdef CONFIG_METADATA_HUB
  // create a pc_queue for the metadata hub
  pc_queue_init(&metadata_hub_queue, (char *)&metadata_hub_queue_items, sizeof(metadata_package),
                metadata_hub_queue_size, "hub");
  if (named_pthread_create(&metadata_hub_thread, NULL, metadata_hub_thread_function, NULL,
                           "metadata hub") != 0)
    debug(1, "Failed to create metadata hub thread!");
#endif
#ifdef CONFIG_MQTT
  // create a pc_queue for the MQTT handler
  pc_queue_init(&metadata_mqtt_queue, (char *)&metadata_mqtt_queue_items, sizeof(metadata_package),
                metadata_mqtt_queue_size, "mqtt");
  if (named_pthread_create(&metadata_mqtt_thread, NULL, metadata_mqtt_thread_function, NULL,
                           "metadata mqtt") != 0)
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
    pc_queue_delete(&metadata_mqtt_queue);
    // debug(2, "metadata stop mqtt done.");
#endif
#ifdef CONFIG_METADATA_HUB
    // debug(2, "metadata stop hub thread.");
    pthread_cancel(metadata_hub_thread);
    pthread_join(metadata_hub_thread, NULL);
    pc_queue_delete(&metadata_hub_queue);
    // debug(2, "metadata stop hub done.");
#endif
    if (config.metadata_enabled) {
      // debug(2, "metadata stop multicast thread.");
      if (metadata_multicast_thread) {
        pthread_cancel(metadata_multicast_thread);
        pthread_join(metadata_multicast_thread, NULL);
        pc_queue_delete(&metadata_multicast_queue);
        // debug(2, "metadata stop multicast done.");
      }
      if (metadata_thread) {
        // debug(2, "metadata stop metadata_thread thread.");
        pthread_cancel(metadata_thread);
        pthread_join(metadata_thread, NULL);
        pc_queue_delete(&metadata_queue);
        // debug(2, "metadata_stop finished successfully.");
      }
    }
  }
}

int send_metadata_to_queue(pc_queue *queue, const uint32_t type, const uint32_t code,
                           const char *data, const uint32_t length, rtsp_message *carrier,
                           int block) {

  // clang-format off
  // parameters:
  //   type,
  //   code,
  //   pointer to data or NULL,
  //   length of data or NULL,
  //   the rtsp_message or NULL,

  // the rtsp_message is sent for 'core' messages, because it contains the data
  // and must not be freed until the data has been read.
  // So, it is passed to send_metadata to be retained, sent to the thread where metadata
  // is processed and released (and probably freed).

  // The rtsp_message is also sent for certain non-'core' messages.

  // The reading of the parameters is a bit complex:
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
  // clang-format on

  metadata_package pack;
  pack.type = type;
  pack.code = code;
  pack.length = length;
  pack.carrier = carrier;
  pack.data = (char *)data;
  if (pack.carrier) {
    msg_retain(pack.carrier);
  } else {
    if (data)
      pack.data = memdup(data, length); // only if it's not a null
  }

  // debug(1, "send_metadata_to_queue %x/%x", type, code);
  int rc = pc_queue_add_item(queue, &pack, block);
  if (rc != 0) {
    if (pack.carrier) {
      if (rc == EWOULDBLOCK)
        debug(2,
              "metadata queue \"%s\" full, dropping message item: type %x, code %x, data %" PRIxPTR ", "
              "length %u, message %d.",
              queue->name, pack.type, pack.code, (uintptr_t)pack.data, pack.length,
              pack.carrier->index_number);
      msg_free(&pack.carrier);
    } else {
      if (rc == EWOULDBLOCK)
        debug(
            2,
            "metadata queue \"%s\" full, dropping data item: type %x, code %x, data %" PRIxPTR ", length %u.",
            queue->name, pack.type, pack.code, (uintptr_t)pack.data, pack.length);
      if (pack.data)
        free(pack.data);
    }
  }
  return rc;
}

int send_metadata(const uint32_t type, const uint32_t code, const char *data, const uint32_t length,
                  rtsp_message *carrier, int block) {
  int rc = 0;
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
    debug(2, "Connection %d: current volume (%.6f) requested", conn->connection_number,
          suggested_volume(conn));

    char *p = malloc(128); // will be automatically deallocated with the response is deleted
    if (p) {
      resp->content = p;
      resp->contentlength = snprintf(p, 128, "\r\nvolume: %.6f\r\n", suggested_volume(conn));
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
      debug(1, "Connection %d: received unknown Content-Type \"%s\" in SET_PARAMETER request.",
            conn->connection_number, ct);
      debug_print_msg_headers(1, req);
    }
  } else {
    debug(1, "Connection %d: missing Content-Type header in SET_PARAMETER request.",
          conn->connection_number);
  }
  resp->respcode = 200;
}

static void handle_announce(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp) {
  debug(2, "Connection %d: ANNOUNCE", conn->connection_number);
#ifdef CONFIG_AIRPLAY_2
  conn->airplay_stream_category = classic_airplay_stream; // already set in Classic AirPlay build
  play_lock_r get_play_status = get_play_lock(
      conn, 1); // always allow interruption in the Classic-AirPlay-in-AirPlay-2 mode (?)
#else
  play_lock_r get_play_status = get_play_lock(conn, config.allow_session_interruption);
#endif
  if (get_play_status != play_lock_aquisition_failed) {

    // this has already been checked for in Classic Airplay and would be play_lock_already_acquired
    // here. if this new session did not break in, then it's okay to reset the next UDP ports to the
    // start of the range
    if (get_play_status ==
        play_lock_acquired_without_breaking_in) { // if it's safe to re-use original UDP ports
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

#ifdef CONFIG_AIRPLAY_2
    // In AirPlay 2, an ANNOUNCE signifies the start of an AirPlay 1 session.
    debug(1, "Connection %d: %s connection from %s:%u to self at %s:%u.", conn->connection_number,
          get_category_string(conn->airplay_stream_category), conn->client_ip_string,
          conn->client_rtsp_port, conn->self_ip_string, conn->self_rtsp_port);
    conn->airplay_type = ap_1;
    conn->timing_type = ts_ntp;
    conn->type = 96; // this is the AirPlay 2 code for Realtime Audio -- not sure it's right
#endif
    conn->stream.type = ast_unknown;
    resp->respcode = 200; // presumed OK
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
      conn->frames_per_packet = 352; // number of audio frames per packet.
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
    } else if ((paesiv != NULL) && (prsaaeskey != NULL)) {
      conn->stream.encrypted = 1;
      // debug(1,"Encrypted session requested");
    } else {
      warn("Invalid Announce message -- missing paesiv or prsaaeskey.");
      resp->respcode = 456; // 456 - Header Field Not Valid for Resource
      // goto out;
    }
    if (conn->stream.encrypted) {
      int len, keylen;
      uint8_t *aesiv = base64_dec(paesiv, &len);
      if (len == 16) {
        memcpy(conn->stream.aesiv, aesiv, 16);
      } else {
        resp->respcode = 456; // 456 - Header Field Not Valid for Resource
        warn("client announced aeskey of %d bytes, wanted 16", len);
      }
      free(aesiv);

      uint8_t *rsaaeskey = base64_dec(prsaaeskey, &len);
      uint8_t *aeskey = rsa_apply(rsaaeskey, len, &keylen, RSA_MODE_KEY);
      free(rsaaeskey);
      if (keylen == 16) {
        memcpy(conn->stream.aeskey, aeskey, 16);
      } else {
        resp->respcode = 456; // 456 - Header Field Not Valid for Resource
        warn("client announced rsaaeskey of %d bytes, wanted 16", keylen);
      }
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
      conn->frames_per_packet = conn->stream.fmtp[1]; // number of audio frames per packet.
      conn->input_rate = conn->stream.fmtp[11];
      conn->input_num_channels = conn->stream.fmtp[7];
      conn->input_bit_depth = conn->stream.fmtp[3];
      conn->input_bytes_per_frame = conn->input_num_channels * ((conn->input_bit_depth + 7) / 8);
    }

    if ((resp->respcode == 200) && (conn->stream.type != ast_unknown)) {
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
    } else {
      warn("Can not process the following ANNOUNCE message:");
      // print each line of the request content
      // the problem is that nextline has replace all returns, newlines, etc. by
      // NULLs
      char *lcp = req->content;
      int lcp_left = req->contentlength;
      while (lcp_left > 1) {
        if (strlen(lcp) != 0)
          warn("    %s", lcp);
        lcp += strlen(lcp) + 1;
        lcp_left -= strlen(lcp) + 1;
      }
    }
    debug(2, "Connection %d: ANNOUNCE has completed.", conn->connection_number);
  } else {
    // can't get the principal_conn
    resp->respcode = 453;
  }
}

#ifdef CONFIG_AIRPLAY_2
static struct method_handler {
  char *method;
  void (*ap1_handler)(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp); // for AirPlay 1
  void (*ap2_handler)(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp); // for AirPlay 2
} method_handlers[] = {{"OPTIONS", handle_options, handle_options_2},
                       {"ANNOUNCE", handle_announce, handle_announce},
                       {"FLUSH", handle_flush, handle_flush},
                       {"TEARDOWN", handle_teardown, handle_teardown_2},
                       {"SETUP", handle_setup, handle_setup_2},
                       {"GET_PARAMETER", handle_get_parameter, handle_get_parameter},
                       {"SET_PARAMETER", handle_set_parameter, handle_set_parameter},
                       {"RECORD", handle_record, handle_record_2},
                       {"GET", handle_get, handle_get},
                       {"POST", handle_post, handle_post},
                       {"SETPEERS", handle_unimplemented_ap1, handle_setpeers},
                       {"SETPEERSX", handle_unimplemented_ap1, handle_setpeersx},
                       {"SETRATEANCHORTI", handle_unimplemented_ap1, handle_setrateanchori},
                       {"FLUSHBUFFERED", handle_unimplemented_ap1, handle_flushbuffered},
                       {"SETRATE", handle_unimplemented_ap1, handle_setrate},
                       {NULL, NULL, NULL}};
#else
static struct method_handler {
  char *method;
  void (*handler)(rtsp_conn_info *conn, rtsp_message *req,
                  rtsp_message *resp); // for AirPlay 1 only
} method_handlers[] = {{"OPTIONS", handle_options},
                       {"GET", handle_get},
                       {"POST", handle_post},
                       {"ANNOUNCE", handle_announce},
                       {"FLUSH", handle_flush},
                       {"TEARDOWN", handle_teardown},
                       {"SETUP", handle_setup},
                       {"GET_PARAMETER", handle_get_parameter},
                       {"SET_PARAMETER", handle_set_parameter},
                       {"RECORD", handle_record},
                       {NULL, NULL}};
#endif

static void apple_challenge(int lfd, rtsp_message *req, rtsp_message *resp) {
  char *hdr = msg_get_header(req, "Apple-Challenge");
  if (!hdr)
    return;
  SOCKADDR fdsa;
  socklen_t sa_len = sizeof(fdsa);
  getsockname(lfd, (struct sockaddr *)&fdsa, &sa_len);

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
    *bp++ = config.ap1_prefix[i];

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
  int lfd = open("/dev/urandom", O_RDONLY);
  if (lfd < 0)
    die("could not open /dev/urandom!");
  // int ignore =
  if (read(lfd, random, sizeof(random)) != sizeof(random))
    debug(1, "Error reading /dev/urandom");
  close(lfd);
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
  EVP_MD_CTX *ctx;
  unsigned int digest_urp_len = EVP_MD_size(EVP_md5());
  unsigned int digest_mu_len = EVP_MD_size(EVP_md5());
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_md5(), NULL);

  EVP_DigestUpdate(ctx, username, strlen(username));
  EVP_DigestUpdate(ctx, ":", 1);
  EVP_DigestUpdate(ctx, realm, strlen(realm));
  EVP_DigestUpdate(ctx, ":", 1);
  EVP_DigestUpdate(ctx, config.password, strlen(config.password));
  EVP_DigestFinal_ex(ctx, digest_urp, &digest_urp_len);
  EVP_MD_CTX_free(ctx);

  ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_md5(), NULL);

  EVP_DigestUpdate(ctx, req->method, strlen(req->method));
  EVP_DigestUpdate(ctx, ":", 1);
  EVP_DigestUpdate(ctx, uri, strlen(uri));

  EVP_DigestFinal_ex(ctx, digest_mu, &digest_mu_len);
  EVP_MD_CTX_free(ctx);
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
  unsigned int digest_total_len = EVP_MD_size(EVP_md5());

  ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_md5(), NULL);

  EVP_DigestUpdate(ctx, buf, 32);
  EVP_DigestUpdate(ctx, ":", 1);
  EVP_DigestUpdate(ctx, *nonce, strlen(*nonce));
  EVP_DigestUpdate(ctx, ":", 1);
  for (i = 0; i < 16; i++)
    snprintf((char *)buf + 2 * i, 3, "%02x", digest_mu[i]);
  EVP_DigestUpdate(ctx, buf, 32);
  EVP_DigestFinal_ex(ctx, digest_total, &digest_total_len);
  EVP_MD_CTX_free(ctx);
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
  if (conn != NULL) {
    int oldState;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
    debug(3, "Connection %d: %s rtsp_conversation_thread_func_cleanup_function called.",
          conn->connection_number, get_category_string(conn->airplay_stream_category));

    if (conn->player_thread) {
      player_stop(conn); // this nulls the player_thread and cancels the threads...
      activity_monitor_signify_activity(0); // inactive, and should be after command_stop()
    }

    if (conn->fd > 0) {
      debug(
          2,
          "Connection %d: terminating -- closing RTSP connection socket %d: from %s:%u to self at "
          "%s:%u.",
          conn->connection_number, conn->fd, conn->client_ip_string, conn->client_rtsp_port,
          conn->self_ip_string, conn->self_rtsp_port);
      close(conn->fd);
      conn->fd = 0;
    }

#ifdef CONFIG_AIRPLAY_2
    if (conn->session_key) {
      free(conn->session_key);
      conn->session_key = NULL;
    }

    // give the event receiver a chance to exit normally, if it exists
    if (conn->rtp_event_thread != NULL) {
      uint64_t event_receiver_start_wait_time = get_absolute_time_in_ns();
      int64_t event_receiver_wait_time = 0;
      do {
        if (conn->ap2_event_receiver_exited == 0)
          usleep(50000);
        event_receiver_wait_time = get_absolute_time_in_ns() - event_receiver_start_wait_time;
      } while ((conn->ap2_event_receiver_exited == 0) && (event_receiver_wait_time < 2000000000L));

      if (conn->ap2_event_receiver_exited == 0) {
        debug(2, "Connection %d: %s event receiver has not exited, so cancelling it.",
              conn->connection_number, get_category_string(conn->airplay_stream_category));
        pthread_cancel(*conn->rtp_event_thread);
      }
      pthread_join(*conn->rtp_event_thread, NULL);
      free(conn->rtp_event_thread);
      conn->rtp_event_thread = NULL;
    }
    conn->ap2_event_receiver_exited = 0;
    debug(3, "Connection %d: %s event thread deleted.", conn->connection_number,
          get_category_string(conn->airplay_stream_category));
#endif

    debug(3, "Connection %d: terminating  -- closing timing, control and audio sockets...",
          conn->connection_number);
    if (conn->control_socket) {
      debug(3, "Connection %d: terminating  -- closing control_socket %d.", conn->connection_number,
            conn->control_socket);
      close(conn->control_socket);
      conn->control_socket = 0;
    }
    if (conn->timing_socket) {
      debug(3, "Connection %d: terminating  -- closing timing_socket %d.", conn->connection_number,
            conn->timing_socket);
      close(conn->timing_socket);
      conn->timing_socket = 0;
    }
    if (conn->audio_socket) {
      debug(3, "Connection %d: terminating -- closing audio_socket %d.", conn->connection_number,
            conn->audio_socket);
      close(conn->audio_socket);
      conn->audio_socket = 0;
    }
    if (conn->auth_nonce) {
      free(conn->auth_nonce);
      conn->auth_nonce = NULL;
    }

#ifdef CONFIG_AIRPLAY_2
    buf_drain(&conn->ap2_pairing_context.control_cipher_bundle.plaintext_read_buffer, -1);
    buf_drain(&conn->ap2_pairing_context.control_cipher_bundle.encrypted_read_buffer, -1);
    if (conn->ap2_pairing_context.control_cipher_bundle.description != NULL)
      free(conn->ap2_pairing_context.control_cipher_bundle.description);
    pair_cipher_free(conn->ap2_pairing_context.control_cipher_bundle.cipher_ctx);

    buf_drain(&conn->ap2_pairing_context.event_cipher_bundle.plaintext_read_buffer, -1);
    buf_drain(&conn->ap2_pairing_context.event_cipher_bundle.encrypted_read_buffer, -1);
    if (conn->ap2_pairing_context.event_cipher_bundle.description != NULL)
      free(conn->ap2_pairing_context.event_cipher_bundle.description);
    pair_cipher_free(conn->ap2_pairing_context.event_cipher_bundle.cipher_ctx);

    buf_drain(&conn->ap2_pairing_context.data_cipher_bundle.plaintext_read_buffer, -1);
    buf_drain(&conn->ap2_pairing_context.data_cipher_bundle.encrypted_read_buffer, -1);
    if (conn->ap2_pairing_context.data_cipher_bundle.description != NULL)
      free(conn->ap2_pairing_context.data_cipher_bundle.description);
    pair_cipher_free(conn->ap2_pairing_context.data_cipher_bundle.cipher_ctx);

    pair_setup_free(conn->ap2_pairing_context.setup_ctx);
    pair_verify_free(conn->ap2_pairing_context.verify_ctx);
    if (conn->airplay_gid) {
      free(conn->airplay_gid);
      conn->airplay_gid = NULL;
    }
#endif

    rtp_terminate(conn);

    if (conn->dacp_id) {
      free(conn->dacp_id);
      conn->dacp_id = NULL;
    }

    if (conn->UserAgent) {
      free(conn->UserAgent);
      conn->UserAgent = NULL;
    }

#ifdef CONFIG_AIRPLAY_2
    if (conn->ap2_client_name) {
      free(conn->ap2_client_name);
      conn->ap2_client_name = NULL;
    }
#endif
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
    debug(3, "Connection %d: Closed.", conn->connection_number);
    conn->running = 0; // for the garbage collector
    release_play_lock(conn);
    pthread_setcancelstate(oldState, NULL);
  }
}

void msg_cleanup_function(void *arg) {
  debug(3, "msg_cleanup_function called 0x%" PRIxPTR ".", (uintptr_t)arg);
  msg_free((rtsp_message **)arg);
}

static void *rtsp_conversation_thread_func(void *pconn) {
  //  #include <syscall.h>
  //  debug(1, "rtsp_conversation_thread_func PID %d", syscall(SYS_gettid));
  rtsp_conn_info *conn = pconn;

  int rc = pthread_mutex_init(&conn->flush_mutex, NULL);
  if (rc)
    die("Connection %d: error %d initialising flush_mutex.", conn->connection_number, rc);
  rc = pthread_mutex_init(&conn->ab_mutex, NULL);
  if (rc)
    die("Connection %d: error %d initialising ab_mutex.", conn->connection_number, rc);
  rc = pthread_cond_init(&conn->flowcontrol, NULL);
  if (rc)
    die("Connection %d: error %d initialising flow control condition variable.",
        conn->connection_number, rc);
  rc = pthread_mutex_init(&conn->volume_control_mutex, NULL);
  if (rc)
    die("Connection %d: error %d initialising volume_control_mutex.", conn->connection_number, rc);

  rc = pthread_mutex_init(&conn->player_create_delete_mutex, NULL);
  if (rc)
    die("Connection %d: error %d initialising player_create_delete_mutex.", conn->connection_number,
        rc);

  // nothing before this is cancellable
  pthread_cleanup_push(rtsp_conversation_thread_cleanup_function, (void *)conn);

  rtp_initialise(conn);
  char *hdr = NULL;

  enum rtsp_read_request_response reply;

  // int rtsp_read_request_attempt_count = 1; // 1 means exit immediately
  rtsp_message *req = NULL, *resp = NULL;

#ifdef CONFIG_AIRPLAY_2
  conn->ap2_audio_buffer_size = 1024 * 1024 * 8;
#endif

  while (conn->stop == 0) {
    pthread_testcancel();
    int debug_level = 2; // for printing the request and response

    // check to see if a conn has been zeroed

    debug_mutex_lock(&conns_lock, 1000000, 3);
    int i;
    for (i = 0; i < nconns; i++) {
      if ((conns[i] != NULL) && (conns[i]->connection_number == 0)) {
        debug(1, "conns[%d] has a Connection Number of 0!", i);
      }
    }
    debug_mutex_unlock(&conns_lock, 3);

    reply = rtsp_read_request(conn, &req);
    if (reply == rtsp_read_request_response_ok) {
      pthread_cleanup_push(msg_cleanup_function, (void *)&req);
      resp = msg_init();
      pthread_cleanup_push(msg_cleanup_function, (void *)&resp);
      resp->respcode = 501; // Not Implemented
      int dl = debug_level;
      // if ((strcmp(req->method, "OPTIONS") == 0) ||
      //    (strcmp(req->method, "POST") ==
      //     0)) // the options message is very common, so don't log it until level 3
      //  dl = 3;
      debug(dl,
            "Connection %d: (%s) received an RTSP Packet of type \"%s\":", conn->connection_number,
            get_category_string(conn->airplay_stream_category), req->method);
      debug_log_rtsp_message(dl, NULL, req);

      apple_challenge(conn->fd, req, resp);
      hdr = msg_get_header(req, "CSeq");
      if (hdr)
        msg_add_header(resp, "CSeq", hdr);
        //      msg_add_header(resp, "Audio-Jack-Status", "connected; type=analog");
#ifdef CONFIG_AIRPLAY_2
      char server_string[128];
      snprintf(server_string, sizeof(server_string), "AirTunes/%s", config.srcvers);
      msg_add_header(resp, "Server", server_string);
#else
      msg_add_header(resp, "Server", "AirTunes/105.1");
#endif

      if ((conn->authorized == 1) || (rtsp_auth(&conn->auth_nonce, req, resp)) == 0) {
        conn->authorized = 1; // it must have been authorized or didn't need a password
        struct method_handler *mh;
        int method_selected = 0;
        for (mh = method_handlers; mh->method; mh++) {
          if (!strcmp(mh->method, req->method)) {
            method_selected = 1;
#ifdef CONFIG_AIRPLAY_2
            if (conn->airplay_type == ap_1)
              mh->ap1_handler(conn, req, resp);
            else
              mh->ap2_handler(conn, req, resp);
#else
            mh->handler(conn, req, resp);
#endif
            break;
          }
        }
        if (method_selected == 0) {
          debug(1,
                "Connection %d: (%s) unrecognised and unhandled rtsp request \"%s\". HTTP Response "
                "Code "
                "%d returned.",
                conn->connection_number, get_category_string(conn->airplay_stream_category),
                req->method, resp->respcode);
          debug_log_rtsp_message(dl, NULL, req);

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
            debug(dl, "Content: \"%s\".", obf);
          }
        }
        resp->respcode = 200; // OK
      }
      debug(dl, "Connection %d: (%s) RTSP response:", conn->connection_number,
            get_category_string(conn->airplay_stream_category));
      debug_log_rtsp_message(dl, NULL, resp);
      // if (conn->stop == 0) {
      int err = msg_write_response(conn, resp);
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
      // }
      pthread_cleanup_pop(1);
      pthread_cleanup_pop(1);
    } else { // if the response is not rtsp_read_request_response_ok
      conn->stop = 1;
      if (reply == rtsp_read_request_response_read_error) {
        debug(1, "bad packet received.");
        struct linger so_linger;
        so_linger.l_onoff = 1; // "true"
        so_linger.l_linger = 0;
        int err = setsockopt(conn->fd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof so_linger);
        if (err)
          debug(1, "Could not set the RTSP socket to abort due to a read error on closing.");
      } else if (reply == rtsp_read_request_response_bad_packet) {
        conn->stop = 0; // don't stop for a bad packet
        char *response_text = "RTSP/1.0 400 Bad Request\r\nServer: AirTunes/105.1\r\n\r\n";
        ssize_t lreply = write(conn->fd, response_text, strlen(response_text));
        if (lreply == -1) {
          char errorstring[1024];
          strerror_r(errno, (char *)errorstring, sizeof(errorstring));
          debug(1, "rtsp_read_request_response_bad_packet write response error %d: \"%s\".", errno,
                (char *)errorstring);
        } else if (lreply != (ssize_t)strlen(response_text)) {
          debug(1, "rtsp_read_request_response_bad_packet write %zd bytes requested but %d written.",
                strlen(response_text), reply);
        }
      }
    }
  }
  pthread_cleanup_pop(1);
  debug(2, "Connection %d: exit.", conn->connection_number);
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
  cancel_all_RTSP_threads(unspecified_stream_category, 0); // kill all RTSP listeners
  int *sockfd = (int *)arg;
  if (sockfd) {
    int i;
    for (i = 1; i <= sockfd[0]; i++) {
      debug(2, "closing socket %d.", sockfd[i]);
      close(sockfd[i]);
    }
    free(sockfd);
  }
  pthread_setcancelstate(oldState, NULL);
  int i;
  for (i = 0; i < nconns; i++) {
    if (conns[i] != NULL) {
      free(conns[i]);
      conns[i] = NULL;
    }
  }
}

void *rtsp_listen_loop(__attribute((unused)) void *arg) {
  //  #include <syscall.h>
  //  debug(1, "rtsp_listen_loop PID %d", syscall(SYS_gettid));
  int oldState;
  pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &oldState);
  struct addrinfo hints, *info, *p;
  char portstr[6];
  int *sockfd = NULL;
  int nsock = 0;
  int i, ret;

  principal_conn = NULL; // the data structure representing the connection that has the player.

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
    int lfd = socket(p->ai_family, p->ai_socktype, IPPROTO_TCP);
    int yes = 1;

    // Handle socket open failures if protocol unavailable (or IPV6 not handled)
    if (lfd != -1) {
      // Set the RTSP socket to close on exec() of child processes
      // otherwise background run_this_before_play_begins or run_this_after_play_ends commands
      // that are sleeping prevent the daemon from being restarted because
      // the listening RTSP port is still in use.
      // See: https://github.com/mikebrady/shairport-sync/issues/329
      fcntl(lfd, F_SETFD, FD_CLOEXEC);
      ret = setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

#ifdef IPV6_V6ONLY
      // some systems don't support v4 access on v6 sockets, but some do.
      // since we need to account for two sockets we might as well
      // always.
      if (p->ai_family == AF_INET6) {
        ret |= setsockopt(lfd, IPPROTO_IPV6, IPV6_V6ONLY, &yes, sizeof(yes));
      }
#endif

      if (!ret)
        ret = bind(lfd, p->ai_addr, p->ai_addrlen);

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
      } else {
        listen(lfd, 255);
        nsock++;
        sockfd = realloc(sockfd, (nsock + 1) * sizeof(int));
        sockfd[nsock] = lfd;
        sockfd[0] = nsock; // the first entry is the number of sockets in the array
      }
    }

    /*
        listen(lfd, 5);
        nsock++;
        sockfd = realloc(sockfd, nsock * sizeof(int));
        sockfd[nsock - 1] = lfd;
    */
  }

  freeaddrinfo(info);

  if (nsock) {
    int maxfd = -1;
    fd_set fds;
    FD_ZERO(&fds);
    // skip the first element in sockfd -- it's the count
    for (i = 1; i <= nsock; i++) {
      if (sockfd[i] > maxfd)
        maxfd = sockfd[i];
    }

    char **t1 = txt_records; // ap1 text records
    char **t2 = NULL;        // possibly two text records
#ifdef CONFIG_AIRPLAY_2
    // make up a secondary set of text records
    t2 = secondary_txt_records; // second set of text records in AirPlay 2 only
#endif
    build_bonjour_strings(NULL); // no conn yet
    // if a thread is created, e.g. Avahi, it'll inherit the name from this thread
    mdns_register(t1, t2); // note that the dacp thread could still be using the mdns stuff after
                           // all player threads have been terminated, so mdns_unregister can't be
                           // in the rtsp_listen_loop cleanup.
    pthread_t tid = pthread_self();
    do_pthread_setname(&tid, "listener");
    pthread_setcancelstate(oldState, NULL);
    int acceptfd;
    struct timeval tv;
    pthread_cleanup_push(rtsp_listen_loop_cleanup_handler, (void *)sockfd);
    do {
      pthread_testcancel();
      tv.tv_sec = 60;
      tv.tv_usec = 0;

      // skip the first element in sockfd -- it's the count
      for (i = 1; i <= nsock; i++)
        FD_SET(sockfd[i], &fds);

      ret = select(maxfd + 1, &fds, 0, 0, &tv);

      if (ret < 0) {
        if (errno == EINTR)
          continue;
        break;
      }

      cleanup_threads();

      acceptfd = -1;
      // skip the first element in sockfd -- it's the count
      for (i = 1; i <= nsock; i++) {
        if (FD_ISSET(sockfd[i], &fds)) {
          acceptfd = sockfd[i];
          break;
        }
      }
      if (acceptfd < 0) // timeout
        continue;

      int release_conn = 1; // on exit, deallocate the buffer unless everything was okay

      rtsp_conn_info *conn = malloc(sizeof(rtsp_conn_info));
      if (conn == 0)
        die("Couldn't allocate memory for an rtsp_conn_info record.");
      pthread_cleanup_push(malloc_cleanup, &conn);
      memset(conn, 0, sizeof(rtsp_conn_info));
      conn->connection_number = RTSP_connection_index++;
      debug(2, "Connection %d is at: 0x%" PRIxPTR ".", conn->connection_number, (uintptr_t)conn);

      // this means that the OPTIONS string we send before getting an ANNOUNCE is for AirPlay 2
#ifdef CONFIG_AIRPLAY_2
      conn->airplay_type = ap_2;  // changed if an ANNOUNCE is received
      conn->timing_type = ts_ptp; // changed if an ANNOUNCE is received
#else
      conn->airplay_stream_category =
          classic_airplay_stream; // really just used for debug messages in Classic AirPlay builds
#endif

      socklen_t size_of_reply = sizeof(SOCKADDR);
      conn->fd = eintr_checked_accept(acceptfd, (struct sockaddr *)&conn->remote, &size_of_reply);
      if (conn->fd < 0) {
        debug(1, "Connection %d: New connection on port %d not accepted:", conn->connection_number,
              config.port);
        perror("failed to accept connection");

#ifndef CONFIG_AIRPLAY_2
        // in Classic AirPlay, close the connection unless idle or interruptions allowed...
      } else if ((principal_conn != NULL) && (config.allow_session_interruption == 0)) {
        debug(1, "Connection %d: %s session interruption not allowed", conn->connection_number,
              get_category_string(conn->airplay_stream_category));
        close(conn->fd);
#endif

      } else {
        size_of_reply = sizeof(SOCKADDR);
        if (getsockname(conn->fd, (struct sockaddr *)&conn->local, &size_of_reply) == 0) {

          if ((config.dont_check_timeout == 0) && (config.timeout >= 60)) {
            /*
              // shouldn't need this!

              struct timeval tv;
              tv.tv_sec = config.timeout;           // seconds
              tv.tv_usec = 0; // microseconds
              if (setsockopt(conn->fd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) != 0)
              { char errorstring[1024]; strerror_r(errno, (char *)errorstring, sizeof(errorstring));
                debug(1, "could not set time limit on read_from_rtsp_connection -- error %d
              \"%s\".", errno, errorstring);
              }
            */
// skip this stuff in OpenBSD
#ifndef COMPILE_FOR_OPENBSD
            // Thanks to https://holmeshe.me/network-essentials-setsockopt-SO_KEEPALIVE/ for this.

            // turn on keepalive stuff -- wait for keepidle + (keepcnt * keepinttvl time) seconds
            // before giving up an ETIMEOUT error is returned if the keepalive check fails

            // if TCP_KEEPINTVL is defined, check a few times before declaring the line dead
            // otherwise just wait a little while longer

#ifdef TCP_KEEPINTVL
            int keepAliveIdleTime =
                config.timeout -
                5 * 5; // wait this many seconds before checking for a dropped client
            // a minute seems a bit short...
            int keepAliveCount = 5;    // check this many times
            int keepAliveInterval = 5; // wait this many seconds between checks
#else
            int keepAliveIdleTime =
                config.timeout; // wait this many seconds before dropping a client
#endif

// --- the following is a bit  too complicated
// decide to use IPPROTO_TCP or SOL_TCP
#if defined COMPILE_FOR_BSD || defined COMPILE_FOR_OSX
#define SOL_OPTION IPPROTO_TCP
#else
#define SOL_OPTION SOL_TCP
#endif
// decide to use TCP_KEEPALIVE or TCP_KEEPIDLE
#ifdef COMPILE_FOR_OSX
#define KEEP_ALIVE_OR_IDLE_OPTION TCP_KEEPALIVE
#else
#define KEEP_ALIVE_OR_IDLE_OPTION TCP_KEEPIDLE
#endif
            debug(3, "Connection %d: set the keepAliveIdleTime to %d seconds.",
                  conn->connection_number, keepAliveIdleTime);
            if (setsockopt(conn->fd, SOL_OPTION, KEEP_ALIVE_OR_IDLE_OPTION,
                           (void *)&keepAliveIdleTime, sizeof(keepAliveIdleTime))) {
              debug(1, "can't set the keepAliveIdleTime wait time");
            }
// ---
// if TCP_KEEPINTVL is defined...
#ifdef TCP_KEEPINTVL
            debug(3, "Connection %d: set the keepAliveCount to %d.", conn->connection_number,
                  keepAliveCount);
            if (setsockopt(conn->fd, SOL_OPTION, TCP_KEEPCNT, (void *)&keepAliveCount,
                           sizeof(keepAliveCount))) {
              debug(1, "can't set the keepAliveCount count");
            }
            debug(3, "Connection %d: set the keepAliveCount interval to %d seconds.",
                  conn->connection_number, keepAliveInterval);
            if (setsockopt(conn->fd, SOL_OPTION, TCP_KEEPINTVL, (void *)&keepAliveInterval,
                           sizeof(keepAliveInterval))) {
              debug(1, "can't set the keepAliveCount count interval");
            };
#endif
            debug(3, "Connection %d: enable SO_KEEPALIVE.", conn->connection_number);
            int flags = 1;
            if (setsockopt(conn->fd, SOL_SOCKET, SO_KEEPALIVE, (void *)&flags, sizeof(flags))) {
              debug(1, "can't set SO_KEEPALIVE.");
            }
#endif
          }

          // initialise the connection info
          void *client_addr = NULL, *self_addr = NULL;
          conn->connection_ip_family = conn->local.SAFAMILY;

#ifdef AF_INET6
          if (conn->connection_ip_family == AF_INET6) {
            struct sockaddr_in6 *sa6 = (struct sockaddr_in6 *)&conn->remote;
            client_addr = &(sa6->sin6_addr);
            conn->client_rtsp_port = ntohs(sa6->sin6_port);

            sa6 = (struct sockaddr_in6 *)&conn->local;
            self_addr = &(sa6->sin6_addr);
            conn->self_rtsp_port = ntohs(sa6->sin6_port);
            conn->self_scope_id = sa6->sin6_scope_id;
          }
#endif
          if (conn->connection_ip_family == AF_INET) {
            struct sockaddr_in *sa4 = (struct sockaddr_in *)&conn->remote;
            client_addr = &(sa4->sin_addr);
            conn->client_rtsp_port = ntohs(sa4->sin_port);

            sa4 = (struct sockaddr_in *)&conn->local;
            self_addr = &(sa4->sin_addr);
            conn->self_rtsp_port = ntohs(sa4->sin_port);
          }

          inet_ntop(conn->connection_ip_family, client_addr, conn->client_ip_string,
                    sizeof(conn->client_ip_string));
          inet_ntop(conn->connection_ip_family, self_addr, conn->self_ip_string,
                    sizeof(conn->self_ip_string));

          debug(2, "Connection %d: New connection from %s:%u to self at %s:%u.",
                conn->connection_number, conn->client_ip_string, conn->client_rtsp_port,
                conn->self_ip_string, conn->self_rtsp_port);
          conn->connection_start_time = get_absolute_time_in_ns();
        } else {
          debug(1, "Error figuring out Shairport Sync's own IP number.");
        }

        ret = named_pthread_create(&conn->thread, NULL, rtsp_conversation_thread_func, conn,
                                   "rtsp_conn_%d",
                                   conn->connection_number); // also acts as a memory barrier
        if (ret) {
          char errorstring[1024];
          strerror_r(ret, (char *)errorstring, sizeof(errorstring));
          die("Connection %d: cannot create an RTSP conversation thread. Error %d: \"%s\".",
              conn->connection_number, ret, (char *)errorstring);
        }

#ifndef CONFIG_AIRPLAY_2
        // in Classic AirPlay, since we know (by getting here) that interruptions are allowed, grab
        // the principal conn
        if (get_play_lock(conn, config.allow_session_interruption) ==
            play_lock_acquired_without_breaking_in) {
          // now, if this new session did not break in, then it's okay to reset the next UDP ports
          // to the start of the range
          resetFreeUDPPort();
        }
#endif

        debug(3, "Successfully created RTSP receiver thread %d.", conn->connection_number);
        conn->running = 1; // this must happen before the thread is tracked
        track_thread(conn);
        release_conn = 0; // successfully initialised
      }
      pthread_cleanup_pop(release_conn); // release the conn malloc if any kind of error
    } while (1);
    pthread_cleanup_pop(1); // should never happen
  } else {
    die("could not establish a service on port %d -- program terminating. Is another instance of "
        "Shairport Sync running on this device?",
        config.port);
  }
  debug(1, "Fell out of the RTSP select loop -- this should never happen!");
  pthread_exit(NULL);
}
