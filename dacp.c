/*
 * DACP protocol handler. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2017--2025
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
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

// Information about the four-character codes is from many sources, with thanks, including
// https://github.com/melloware/dacp-net/blob/master/Melloware.DACP/

#include "dacp.h"
#include "common.h"
#include "config.h"

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <math.h>
#include <memory.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdlib.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include "metadata_hub.h"
#include "tinyhttp/http.h"

typedef struct {
  int players_connection_thread_index; // the connection thread index when a player thread is
                                       // associated with this, zero otherwise
  int scan_enable;                     // set to 1 if scanning should be considered
  char dacp_id[256];                   // the DACP ID string
  uint16_t port;                       // zero if no port discovered
  short connection_family;             // AF_INET6 or AF_INET
  int always_use_revision_number_1;    // for dealing with forked-daapd;
  uint32_t scope_id;                   // if it's an ipv6 connection, this will be its scope id
  char ip_string[INET6_ADDRSTRLEN];    // the ip string pointing to the client
  char *active_remote_id;              // send this when you want to send remote control commands
  void *port_monitor_private_storage;
} dacp_server_record;

int dacp_monitor_initialised = 0;
pthread_t dacp_monitor_thread;
dacp_server_record dacp_server;
void *mdns_dacp_monitor_private_storage_pointer;

// HTTP Response data/funcs (See the tinyhttp example.cpp file for more on this.)
struct HttpResponse {
  void *body;            // this will be a malloc'ed pointer
  ssize_t malloced_size; // this will be its allocated size
  ssize_t size;          // the current size of the content
  int code;
};

void *response_realloc(__attribute__((unused)) void *opaque, void *ptr, int size) {
  void *t = NULL;
  if (size == 0) {
    // debug(1, "response_realloc of size 0 to ptr %" PRIxPTR " requested, and a pointer of NULL was
    // returned.", size, ptr);
    free(ptr);
  } else {
    if (ptr == NULL) {
      t = malloc(size);
      // debug(1, "response_realloc of size %d to a NULL pointer was requested, and a pointer of %"
      // PRIxPTR " was returned.", size, t);
    } else {
      t = realloc(ptr, size);
    }
    if (t == NULL)
      debug(1, "response_realloc of size %d to ptr %" PRIxPTR " failed!", size, (uintptr_t) ptr);
  }
  return t;
}

void response_body(void *opaque, const char *data, int size) {
  struct HttpResponse *response = (struct HttpResponse *)opaque;

  ssize_t space_available = response->malloced_size - response->size;
  if (space_available < size) {
    // debug(1,"Getting more space for the response -- need %d bytes but only %ld bytes left.\n",
    // size,
    //       size - space_available);
    ssize_t size_requested = size - space_available + response->malloced_size + 16384;
    void *t = realloc(response->body, size_requested);
    response->malloced_size = size_requested;
    if (t)
      response->body = t;
    else {
      die("dacp: can't allocate any more space for parser.");
    }
  }
  memcpy(response->body + response->size, data, size);
  response->size += size;
}

static void
response_header(__attribute__((unused)) void *opaque, __attribute__((unused)) const char *ckey,
                __attribute__((unused)) int nkey, __attribute__((unused)) const char *cvalue,
                __attribute__((unused)) int nvalue) { /* example doesn't care about headers */
}

static void response_code(void *opaque, int code) {
  struct HttpResponse *response = (struct HttpResponse *)opaque;
  response->code = code;
}

static const struct http_funcs responseFuncs = {
    response_realloc,
    response_body,
    response_header,
    response_code,
};

// static pthread_mutex_t dacp_conversation_lock = PTHREAD_MUTEX_INITIALIZER;
// static pthread_mutex_t dacp_server_information_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t dacp_conversation_lock;
static pthread_mutex_t dacp_server_information_lock;
static pthread_cond_t dacp_server_information_cv;

void addrinfo_cleanup(void *arg) {
  // debug(1, "addrinfo cleanup called.");
  struct addrinfo **info = (struct addrinfo **)arg;
  freeaddrinfo(*info);
}

void mutex_lock_cleanup(void *arg) {
  pthread_mutex_t *m = (pthread_mutex_t *)arg;
  if (pthread_mutex_unlock(m))
    debug(1, "Error releasing mutex.");
}

void connect_cleanup(void *arg) {
  int *fd = (int *)arg;
  // debug(2, "dacp_send_command: close socket %d.",*fd);
  close(*fd);
}

void http_cleanup(void *arg) {
  // debug(1, "http cleanup called.");
  struct http_roundtripper *rt = (struct http_roundtripper *)arg;
  http_free(rt);
}

int dacp_send_command(const char *command, char **body, ssize_t *bodysize) {
  int result;
  // debug(1,"dacp_send_command: command is: \"%s\".",command);

  if (dacp_server.port == 0) {
    // debug(3, "No DACP port specified yet");
    result = 490; // no port specified
  } else {

    // will malloc space for the body or set it to NULL -- the caller should free it.

    // Using some custom HTTP-like return codes
    //  498 Bad Address information for the DACP server
    //  497 Can't establish a socket to the DACP server
    //  496 Can't connect to the DACP server
    //  495 Error receiving response
    //  494 This client is already busy
    //  493 Client failed to send a message
    //  492 Argument out of range
    //  491 Client refused connection
    //  490 No port specified

    struct addrinfo hints, *res;
    int sockfd;

    struct HttpResponse response;
    response.body = NULL;
    response.malloced_size = 0;
    response.size = 0;
    response.code = 0;

    char portstring[10], server[1024], message[1024];
    memset(&portstring, 0, sizeof(portstring));
    if (dacp_server.connection_family == AF_INET6) {
      snprintf(server, sizeof(server), "%s%%%u", dacp_server.ip_string, dacp_server.scope_id);
    } else {
      strcpy(server, dacp_server.ip_string);
    }
    snprintf(portstring, sizeof(portstring), "%u", dacp_server.port);

    // first, load up address structs with getaddrinfo():

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    // debug(1, "DACP port string is \"%s:%s\".", server, portstring);

    int ires = getaddrinfo(server, portstring, &hints, &res);
    if (ires) {
      // debug(1,"Error %d \"%s\" at getaddrinfo.",ires,gai_strerror(ires));
      response.code = 498; // Bad Address information for the DACP server
    } else {
      uint64_t start_time = get_absolute_time_in_ns();
      pthread_cleanup_push(addrinfo_cleanup, (void *)&res);
      // only do this one at a time -- not sure it is necessary, but better safe than sorry

      // int mutex_reply = sps_pthread_mutex_timedlock(&dacp_conversation_lock, 2000000, command,
      // 1);
      int mutex_reply = debug_mutex_lock(&dacp_conversation_lock, 2000000, 1);
      // int mutex_reply = pthread_mutex_lock(&dacp_conversation_lock);
      if (mutex_reply == 0) {
        pthread_cleanup_push(mutex_lock_cleanup, (void *)&dacp_conversation_lock);

        // make a socket:
        sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);

        if (sockfd == -1) {
          // debug(1, "DACP socket could not be created -- error %d:
          // \"%s\".",errno,strerror(errno));
          response.code = 497; // Can't establish a socket to the DACP server
        } else {
          pthread_cleanup_push(connect_cleanup, (void *)&sockfd);
          // debug(2, "dacp_send_command: open socket %d.",sockfd);

          // This is for limiting the time to be spent waiting for a response.

          struct timeval tv;
          tv.tv_sec = 0;
          tv.tv_usec = 500000;
          if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) == -1)
            debug(1, "dacp_send_command: error %d setting receive timeout.", errno);
          if (setsockopt(sockfd, SOL_SOCKET, SO_SNDTIMEO, (const char *)&tv, sizeof tv) == -1)
            debug(1, "dacp_send_command: error %d setting send timeout.", errno);

          // connect!
          // debug(1, "DACP socket created.");
          if (connect(sockfd, res->ai_addr, res->ai_addrlen) < 0) {
            // debug(1, "dacp_send_command: connect failed with errno %d.", errno);
            if (errno == ECONNREFUSED)
              response.code = 491; // DACP server doesn't want to talk anymore...
            else
              response.code = 496; // Can't connect to the DACP server
          } else {
            // debug(1,"DACP connect succeeded.");

            snprintf(message, sizeof(message),
                     "GET /ctrl-int/1/%s HTTP/1.1\r\nHost: %s:%u\r\nActive-Remote: %s\r\n\r\n",
                     command, dacp_server.ip_string, dacp_server.port,
                     dacp_server.active_remote_id);

            // Send command
            debug(3, "dacp_send_command: \"%s\".", command);
            ssize_t wresp = send(sockfd, message, strlen(message), 0);
            if (wresp == -1) {
              char errorstring[1024];
              getErrorText((char *)errorstring, sizeof(errorstring));
              debug(2, "dacp_send_command: write error %d: \"%s\".", errno, (char *)errorstring);
              struct linger so_linger;
              so_linger.l_onoff = 1; // "true"
              so_linger.l_linger = 0;
              int err = setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof so_linger);
              if (err)
                debug(1, "Could not set the dacp socket to abort due to a write error on closing.");
            }
            if (wresp != (ssize_t)strlen(message)) {
              // debug(1, "dacp_send_command: send failed.");
              response.code = 493; // Client failed to send a message

            } else {

              response.body = malloc(2048); // it can resize this if necessary
              response.malloced_size = 2048;
              pthread_cleanup_push(malloc_cleanup, &response.body);

              struct http_roundtripper rt;
              http_init(&rt, responseFuncs, &response);
              pthread_cleanup_push(http_cleanup, &rt);

              int needmore = 1;
              int looperror = 0;
              char buffer[8192];
              memset(buffer, 0, sizeof(buffer));
              while (needmore && !looperror) {
                const char *data = buffer;
                if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char *)&tv, sizeof tv) == -1)
                  debug(1, "dacp_send_command: error %d setting receive timeout.", errno);
                ssize_t ndata = recv(sockfd, buffer, sizeof(buffer), 0);
                // debug(3, "Received %d bytes: \"%s\".", ndata, buffer);
                if (ndata <= 0) {
                  if (ndata == -1) {
                    char errorstring[1024];
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-result"
                    strerror_r(errno, (char *)errorstring, sizeof(errorstring));
#pragma GCC diagnostic pop
                    debug(2, "dacp_send_command: receiving error %d: \"%s\".", errno, errorstring);
                    struct linger so_linger;
                    so_linger.l_onoff = 1; // "true"
                    so_linger.l_linger = 0;
                    int err =
                        setsockopt(sockfd, SOL_SOCKET, SO_LINGER, &so_linger, sizeof so_linger);
                    if (err)
                      debug(
                          1,
                          "Could not set the dacp socket to abort due to a read error on closing.");
                  }

                  free(response.body);
                  response.body = NULL;
                  response.malloced_size = 0;
                  response.size = 0;
                  response.code = 495; // Error receiving response
                  looperror = 1;
                }

                while (needmore && ndata && !looperror) {
                  int read;
                  needmore = http_data(&rt, data, ndata, &read);
                  ndata -= read;
                  data += read;
                }
              }

              if (http_iserror(&rt)) {
                debug(3, "dacp_send_command: error parsing data.");
                free(response.body);
                response.body = NULL;
                response.malloced_size = 0;
                response.size = 0;
              }
              // debug(1,"Size of response body is %d",response.size);
              pthread_cleanup_pop(1); // this should call http_cleanup
              // http_free(&rt);
              pthread_cleanup_pop(
                  0); // this should *not* free the malloced buffer -- just pop the malloc cleanup
            }
          }
          pthread_cleanup_pop(1); // this should close the socket
                                  // close(sockfd);
                                  // debug(1,"DACP socket closed.");
        }
        pthread_cleanup_pop(1); // this should unlock the dacp_conversation_lock);
        // pthread_mutex_unlock(&dacp_conversation_lock);
        // debug(1,"Sent command\"%s\" with a response body of size %d.",command,response.size);
        // debug(1,"dacp_conversation_lock released.");
      } else {
        debug(3,
              "dacp_send_command: could not acquire a lock on the dacp transmit/receive section "
              "when attempting to "
              "send the command \"%s\". Possible timeout?",
              command);
        response.code = 494; // This client is already busy
      }
      pthread_cleanup_pop(1); // this should free the addrinfo
      // freeaddrinfo(res);
      uint64_t et = get_absolute_time_in_ns() - start_time; // this will be in nanoseconds
      debug(3, "dacp_send_command: %f seconds, response code %d, command \"%s\".",
            (1.0 * et) / 1000000000, response.code, command);
    }
    *body = response.body;
    *bodysize = response.size;
    result = response.code;
  }
  return result;
}

int send_simple_dacp_command(const char *command) {
  int reply = 0;
  char *server_reply = NULL;
  debug(2, "send_simple_dacp_command: sending command \"%s\".", command);
  ssize_t reply_size = 0;
  reply = dacp_send_command(command, &server_reply, &reply_size);
  if (server_reply) {
    free(server_reply);
    server_reply = NULL;
  }
  return reply;
}

void relinquish_dacp_server_information(rtsp_conn_info *conn) {
  // this will set the dacp_server.players_connection_thread_index to zero iff it has the same value
  // as the conn's connection number
  // this is to signify that the player has stopped, but only if another thread (with a different
  // index) hasn't already taken over the dacp service
  debug_mutex_lock(&dacp_server_information_lock, 500000, 2);
  if (dacp_server.players_connection_thread_index == conn->connection_number)
    dacp_server.players_connection_thread_index = 0;
  debug_mutex_unlock(&dacp_server_information_lock, 3);
}

// this will be running on the thread of its caller, not of the conversation thread...
// tell the DACP conversation thread what DACP server we are listening to
// if the active_remote_id is the same we don't change anything apart from
// the conversation number
// Thus, we can keep the DACP port that might have previously been discovered
void set_dacp_server_information(rtsp_conn_info *conn) {
  // debug(1, "set_dacp_server_information");
  debug_mutex_lock(&dacp_server_information_lock, 500000, 2);
  dacp_server.players_connection_thread_index = conn->connection_number;

  if ((conn->dacp_id == NULL) || (strcmp(conn->dacp_id, dacp_server.dacp_id) != 0)) {
    if (conn->dacp_id)
      strncpy(dacp_server.dacp_id, conn->dacp_id, sizeof(dacp_server.dacp_id) - 1);
    else
      dacp_server.dacp_id[0] = '\0';
    dacp_server.port = 0;
    dacp_server.scan_enable = 0;
    dacp_server.connection_family = conn->connection_ip_family;
    dacp_server.scope_id = conn->self_scope_id;
    strncpy(dacp_server.ip_string, conn->client_ip_string, INET6_ADDRSTRLEN);
    debug(2, "set_dacp_server_information set IP to \"%s\" and DACP id to \"%s\".",
          dacp_server.ip_string, dacp_server.dacp_id);

    /*

    "long polling" is not implemented by Shairport Sync, whereby by sending the client the
    last-received revision number, the link will hang until a change occurs.

    Instead, at present, Shairport Sync always uses a revision number of 1.

    */

    // always use revision number 1
    dacp_server.always_use_revision_number_1 = 1;

    metadata_hub_modify_prolog();
    int ch = metadata_store.dacp_server_active != dacp_server.scan_enable;
    metadata_store.dacp_server_active = dacp_server.scan_enable;
    if ((metadata_store.client_ip == NULL) ||
        (strncmp(metadata_store.client_ip, conn->client_ip_string, INET6_ADDRSTRLEN) != 0)) {
      if (metadata_store.client_ip)
        free(metadata_store.client_ip);
      metadata_store.client_ip = strndup(conn->client_ip_string, INET6_ADDRSTRLEN);
      debug(3, "MH Client IP set to: \"%s\"", metadata_store.client_ip);
      ch = 1;
    }
    metadata_hub_modify_epilog(ch);
  } else {
    if (dacp_server.port) {
      // debug(1, "Re-enable scanning.");
      dacp_server.scan_enable = 1;
      //      metadata_hub_modify_prolog();
      //      int ch = metadata_store.dacp_server_active != dacp_server.scan_enable;
      //      metadata_store.dacp_server_active = dacp_server.scan_enable;
      //      metadata_hub_modify_epilog(ch);
    }
  }
  if (dacp_server.active_remote_id)
    free(dacp_server.active_remote_id);
  if (conn->dacp_active_remote)
    dacp_server.active_remote_id =
        strdup(conn->dacp_active_remote); // even if the dacp_id remains the same,
                                          // the active remote will change.
  else
    dacp_server.active_remote_id = NULL;

  debug(3, "set_dacp_server_information set active-remote id to %s.", dacp_server.active_remote_id);
  pthread_cond_signal(&dacp_server_information_cv);
  debug_mutex_unlock(&dacp_server_information_lock, 3);
}

void dacp_monitor_port_update_callback(char *dacp_id, uint16_t port) {
  debug_mutex_lock(&dacp_server_information_lock, 500000, 2);
  debug(3,
        "dacp_monitor_port_update_callback with Remote ID \"%s\", target ID \"%s\" and port "
        "number %d.",
        dacp_id, dacp_server.dacp_id, port);
  if ((dacp_id == NULL) || (dacp_server.dacp_id[0] == '\0')) {
    warn("dacp_id or dacp_server.dacp_id NULL detected");
  } else {
    if (strcmp(dacp_id, dacp_server.dacp_id) == 0) {
      dacp_server.port = port;
      if (port == 0)
        dacp_server.scan_enable = 0;
      else {
        dacp_server.scan_enable = 1;
        // debug(2, "dacp_monitor_port_update_callback enables scan");
      }
      //    metadata_hub_modify_prolog();
      //    int ch = metadata_store.dacp_server_active != dacp_server.scan_enable;
      //    metadata_store.dacp_server_active = dacp_server.scan_enable;
      //    metadata_hub_modify_epilog(ch);
    } else {
      debug(2, "dacp port monitor reporting on an out-of-use remote.");
    }
  }
  pthread_cond_signal(&dacp_server_information_cv);
  debug_mutex_unlock(&dacp_server_information_lock, 3);
}

void *dacp_monitor_thread_code(__attribute__((unused)) void *na) {
  //  #include <syscall.h>
  //  debug(1, "dacp_monitor_thread_code PID %d", syscall(SYS_gettid));
  // int scan_index = 0;
  int always_use_revision_number_1 = 0;
  // char server_reply[10000];
  // debug(1, "DACP monitor thread started.");
  // wait until we get a valid port number to begin monitoring it
  int32_t revision_number = 1;
  int bad_result_count = 0;
  int idle_scan_count = 0;
  while (1) {
    int result = 0;
    int32_t the_volume;
    pthread_cleanup_debug_mutex_lock(&dacp_server_information_lock, 500000, 2);
    if (dacp_server.scan_enable == 0) {
      metadata_hub_modify_prolog();
      int ch = (metadata_store.dacp_server_active != 0) ||
               (metadata_store.advanced_dacp_server_active != 0);
      metadata_store.dacp_server_active = 0;
      metadata_store.advanced_dacp_server_active = 0;
      /*
      debug(3,
            "setting metadata_store.dacp_server_active and "
            "metadata_store.advanced_dacp_server_active to 0 with an update "
            "flag value of %d",
            ch);
      */
      metadata_hub_modify_epilog(ch);

      uint64_t time_to_wait_for_wakeup_ns =
          (uint64_t)(1000000000 * config.missing_port_dacp_scan_interval_seconds);

#ifdef COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN_AND_OPENBSD
      uint64_t time_of_wakeup_ns = get_realtime_in_ns() + time_to_wait_for_wakeup_ns;
      uint64_t sec = time_of_wakeup_ns / 1000000000;
      uint64_t nsec = time_of_wakeup_ns % 1000000000;
#endif
#ifdef COMPILE_FOR_OSX
      uint64_t sec = time_to_wait_for_wakeup_ns / 1000000000;
      uint64_t nsec = time_to_wait_for_wakeup_ns % 1000000000;
#endif

      struct timespec time_to_wait;
      time_to_wait.tv_sec = sec;
      time_to_wait.tv_nsec = nsec;

      while ((dacp_server.scan_enable == 0) && (result != ETIMEDOUT)) {
        // debug(1, "dacp_monitor_thread_code wait for an event to possibly enable scan");

#ifdef COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN_AND_OPENBSD
        result = pthread_cond_timedwait(&dacp_server_information_cv, &dacp_server_information_lock,
                                        &time_to_wait); // this is a pthread cancellation point
        // debug(1, "result is %d, dacp_server.scan_enable is %d, time_to_wait_for_wakeup_ns is %"
        // PRId64 ".", result, dacp_server.scan_enable, time_to_wait_for_wakeup_ns);

#endif
#ifdef COMPILE_FOR_OSX
        result = pthread_cond_timedwait_relative_np(&dacp_server_information_cv,
                                                    &dacp_server_information_lock, &time_to_wait);
#endif
      }
      if (dacp_server.scan_enable == 1) {
        bad_result_count = 0;
        idle_scan_count = 0;
      }
    }

    always_use_revision_number_1 =
        dacp_server.always_use_revision_number_1; // set this while access is locked

    result = dacp_get_volume(&the_volume); // just want the http code
    pthread_cleanup_pop(1);

    if (result == 490) { // 490 means no port was specified
      if (strlen(dacp_server.dacp_id) != 0) {
        // debug(1,"mdns_dacp_monitor_set_id");
        mdns_dacp_monitor_set_id(dacp_server.dacp_id);
      }
    } else {
      // scan_index++;
      // debug(1,"DACP Scan Result: %d.", result);

      if ((result == 200) || (result == 400)) {
        bad_result_count = 0;
      } else {
        if (bad_result_count < config.scan_max_bad_response_count) // limit to some reasonable value
          bad_result_count++;
      }

      // here, do the debouncing calculations to see
      // if the dacp server  and the
      // advanced dacp server are available

      // -1 means we don't know because some bad statuses have been reported
      // 0 means definitely no
      // +1 means definitely yes

      int dacp_server_status_now = -1;
      int advanced_dacp_server_status_now = -1;

      if (bad_result_count == 0) {
        dacp_server_status_now = 1;
        if (result == 200)
          advanced_dacp_server_status_now = 1;
        else if (result == 400)
          advanced_dacp_server_status_now = 0;
      } else if (bad_result_count ==
                 config.scan_max_bad_response_count) { // if a sequence of bad return codes occurs,
                                                       // then it's gone
        dacp_server_status_now = 0;
        advanced_dacp_server_status_now = 0;
      }

      if (metadata_store.player_thread_active == 0)
        idle_scan_count++;
      else
        idle_scan_count = 0;

      debug(3, "Scan Result: %d, Bad Scan Count: %d, Idle Scan Count: %d.", result,
            bad_result_count, idle_scan_count);

      /* not used
      // decide if idle for too long
          if  (idle_scan_count == config.scan_max_inactive_count) {
            debug(2, "DACP server status scanning stopped.");
            dacp_server.scan_enable = 0;
          }
      */

      int update_needed = 0;
      metadata_hub_modify_prolog();
      if (dacp_server_status_now != -1) { // if dacp_server_status_now is actually known...
        if (metadata_store.dacp_server_active != dacp_server_status_now) {
          debug(2, "metadata_store.dacp_server_active set to %d.", dacp_server_status_now);
          metadata_store.dacp_server_active = dacp_server_status_now;
          update_needed = 1;
        }
      }
      if (advanced_dacp_server_status_now !=
          -1) { // if advanced_dacp_server_status_now is actually known...
        if (metadata_store.advanced_dacp_server_active != advanced_dacp_server_status_now) {
          debug(2, "metadata_store.advanced_dacp_server_active set to %d.", dacp_server_status_now);
          metadata_store.advanced_dacp_server_active = advanced_dacp_server_status_now;
          update_needed = 1;
        }
      }

      metadata_hub_modify_epilog(update_needed);

      // pthread_mutex_unlock(&dacp_server_information_lock);
      // debug(1, "DACP Server ID \"%u\" at \"%s:%u\", scan %d.", dacp_server.active_remote_id,
      //      dacp_server.ip_string, dacp_server.port, scan_index);

      if (result == 200) {
        metadata_hub_modify_prolog();
        int diff = metadata_store.speaker_volume != the_volume;
        if (diff)
          metadata_store.speaker_volume = the_volume;
        metadata_hub_modify_epilog(diff);

        ssize_t le;
        char *response = NULL;
        int32_t item_size;
        char command[1024] = "";
        if (always_use_revision_number_1 != 0) // see the "long polling" note above
          revision_number = 1;
        snprintf(command, sizeof(command) - 1, "playstatusupdate?revision-number=%d",
                 revision_number);
        // debug(1,"dacp_monitor_thread_code: command: \"%s\"",command);
        result = dacp_send_command(command, &response, &le);
        // debug(1,"Response to \"%s\" is %d.",command,result);
        // remember: unless the revision_number you pass in is 1,
        // response will be 200 only if there's something new to report.
        if (result == 200) {
          // if (0) {
          char *sp = response;
          if (le >= 8) {
            // here start looking for the contents of the status update
            if (dacp_tlv_crawl(&sp, &item_size) == 'cmst') { // status
              // here, we know that we are receiving playerstatusupdates, so set a flag
              metadata_hub_modify_prolog();
              // debug(1, "playstatusupdate release track metadata");
              // metadata_hub_reset_track_metadata();
              // metadata_store.playerstatusupdates_are_received = 1;
              sp -= item_size; // drop down into the array -- don't skip over it
              le -= 8;
              // char typestring[5];
              // we need to acquire the metadata data structure and possibly update it
              while (le >= 8) {
                uint32_t type = dacp_tlv_crawl(&sp, &item_size);
                le -= item_size + 8;
                char *t;
                // char u;
                // char *st;
                int32_t r;
                uint32_t ui;
                // uint64_t v;
                // int i;

                switch (type) {
                case 'cmsr': // revision number
                  t = sp - item_size;
                  revision_number = ntohl(*(uint32_t *)(t));
                  // debug(1,"New revision number received: %d", revision_number);
                  break;
                case 'caps': // play status
                  t = sp - item_size;
                  r = *(unsigned char *)(t);
                  switch (r) {
                  case 2:
                    if (metadata_store.play_status != PS_STOPPED) {
                      metadata_store.play_status = PS_STOPPED;
                      debug(2, "Play status is \"stopped\".");
                    }
                    break;
                  case 3:
                    if (metadata_store.play_status != PS_PAUSED) {
                      metadata_store.play_status = PS_PAUSED;
                      debug(2, "Play status is \"paused\".");
                    }
                    break;
                  case 4:
                    if (metadata_store.play_status != PS_PLAYING) {
                      metadata_store.play_status = PS_PLAYING;
                      debug(2, "Play status changed to \"playing\".");
                    }
                    break;
                  default:
                    debug(1, "Unrecognised play status %d received.", r);
                    break;
                  }
                  break;
                case 'cash': // shuffle status
                  t = sp - item_size;
                  r = *(unsigned char *)(t);
                  switch (r) {
                  case 0:
                    if (metadata_store.shuffle_status != SS_OFF) {
                      metadata_store.shuffle_status = SS_OFF;
                      debug(2, "Shuffle status is \"off\".");
                    }
                    break;
                  case 1:
                    if (metadata_store.shuffle_status != SS_ON) {
                      metadata_store.shuffle_status = SS_ON;
                      debug(2, "Shuffle status is \"on\".");
                    }
                    break;
                  default:
                    debug(1, "Unrecognised shuffle status %d received.", r);
                    break;
                  }
                  break;
                case 'carp': // repeat status
                  t = sp - item_size;
                  r = *(unsigned char *)(t);
                  switch (r) {
                  case 0:
                    if (metadata_store.repeat_status != RS_OFF) {
                      metadata_store.repeat_status = RS_OFF;
                      debug(2, "Repeat status is \"none\".");
                    }
                    break;
                  case 1:
                    if (metadata_store.repeat_status != RS_ONE) {
                      metadata_store.repeat_status = RS_ONE;
                      debug(2, "Repeat status is \"one\".");
                    }
                    break;
                  case 2:
                    if (metadata_store.repeat_status != RS_ALL) {
                      metadata_store.repeat_status = RS_ALL;
                      debug(2, "Repeat status is \"all\".");
                    }
                    break;
                  default:
                    debug(1, "Unrecognised repeat status %d received.", r);
                    break;
                  }
                  break;
                case 'cann': // track name
                  debug(2, "DACP Track Name seen");
                  if (string_update_with_size(&metadata_store.track_name,
                                              &metadata_store.track_name_changed, sp - item_size,
                                              item_size)) {
                    debug(2, "DACP Track Name set to: \"%s\"", metadata_store.track_name);
                  }
                  break;
                case 'cana': // artist name
                  debug(2, "DACP Artist Name seen");
                  if (string_update_with_size(&metadata_store.artist_name,
                                              &metadata_store.artist_name_changed, sp - item_size,
                                              item_size)) {
                    debug(2, "DACP Artist Name set to: \"%s\"", metadata_store.artist_name);
                  }
                  break;
                case 'canl': // album name
                  debug(2, "DACP Album Name seen");
                  if (string_update_with_size(&metadata_store.album_name,
                                              &metadata_store.album_name_changed, sp - item_size,
                                              item_size)) {
                    debug(2, "DACP Album Name set to: \"%s\"", metadata_store.album_name);
                  }
                  break;
                case 'cang': // genre
                  debug(2, "DACP Genre seen");
                  if (string_update_with_size(&metadata_store.genre, &metadata_store.genre_changed,
                                              sp - item_size, item_size)) {
                    debug(2, "DACP Genre set to: \"%s\"", metadata_store.genre);
                  }
                  break;
                case 'canp': // nowplaying 4 ids: dbid, plid, playlistItem, itemid (from mellowware
                             // see reference above)
                  debug(2, "DACP Composite ID seen");
                  if ((metadata_store.item_composite_id_is_valid == 0) ||
                      (memcmp(metadata_store.item_composite_id, sp - item_size,
                              sizeof(metadata_store.item_composite_id)) != 0)) {
                    memcpy(metadata_store.item_composite_id, sp - item_size,
                           sizeof(metadata_store.item_composite_id));
                    char st[33];
                    char *pt = st;
                    int it;
                    for (it = 0; it < 16; it++) {
                      snprintf(pt, 3, "%02X", metadata_store.item_composite_id[it]);
                      pt += 2;
                    }
                    *pt = 0;
                    debug(2, "Item composite ID changed to 0x%s.", st);
                    metadata_store.item_composite_id_changed = 1;
                    metadata_store.item_composite_id_is_valid = 1;
                  }
                  break;
                case 'astm':
                  t = sp - item_size;
                  ui = ntohl(*(uint32_t *)(t));
                  debug(2, "DACP Song Time seen: \"%u\" of length %u.", ui, item_size);
                  if (ui != metadata_store.songtime_in_milliseconds) {
                    metadata_store.songtime_in_milliseconds = ui;
                    metadata_store.songtime_in_milliseconds_changed = 1;
                    metadata_store.songtime_in_milliseconds_is_valid = 1;
                    debug(2, "DACP Song Time set to: \"%u\"",
                          metadata_store.songtime_in_milliseconds);
                  }
                  break;

                /*
                            case 'mstt':
                            case 'cant':
                            case 'cast':
                            case 'cmmk':
                            case 'caas':
                            case 'caar':
                              t = sp - item_size;
                              r = ntohl(*(uint32_t *)(t));
                              printf("    %d", r);
                              printf("    (0x");
                              t = sp - item_size;
                              for (i = 0; i < item_size; i++) {
                                printf("%02x", *t & 0xff);
                                t++;
                              }
                              printf(")");
                              break;
                            case 'asai':
                              t = sp - item_size;
                              s = ntohl(*(uint32_t *)(t));
                              s = s << 32;
                              t += 4;
                              v = (ntohl(*(uint32_t *)(t))) & 0xffffffff;
                              s += v;
                              printf("    %lu", s);
                              printf("    (0x");
                              t = sp - item_size;
                              for (i = 0; i < item_size; i++) {
                                printf("%02x", *t & 0xff);
                                t++;
                              }
                              printf(")");
                              break;
                 */
                default:
                  /*
                    printf("    0x");
                    t = sp - item_size;
                    for (i = 0; i < item_size; i++) {
                      printf("%02x", *t & 0xff);
                      t++;
                    }
                   */
                  break;
                }
                // printf("\n");
              }

              // finished possibly writing to the metadata hub
              metadata_hub_modify_epilog(
                  1); // should really see if this can be made responsive to changes
            } else {
              debug(1, "Status Update not found.\n");
            }
          } else {
            debug(1, "Can't find any content in playerstatusupdate request");
          }
        } /* else {
          if (result != 403)
            debug(1, "Unexpected response %d to playerstatusupdate request", result);
        } */
        if (response) {
          free(response);
          response = NULL;
        };
      };
      /*
      strcpy(command,"nowplayingartwork?mw=320&mh=320");
      debug(1,"Command: \"%s\", result is %d",command, dacp_send_command(command, &response, &le));
      if (response) {
        free(response);
        response = NULL;
      }
      strcpy(command,"getproperty?properties=dmcp.volume");
      debug(1,"Command: \"%s\", result is %d",command, dacp_send_command(command, &response, &le));
      if (response) {
        free(response);
        response = NULL;
      }
      strcpy(command,"setproperty?dmcp.volume=100.000000");
      debug(1,"Command: \"%s\", result is %d",command, dacp_send_command(command, &response, &le));
      if (response) {
        free(response);
        response = NULL;
      }
      */
      if (metadata_store.player_thread_active)
        sleep(config.scan_interval_when_active);
      else
        sleep(config.scan_interval_when_inactive);
    }
  }
  debug(1, "DACP monitor thread exiting -- should never happen.");
  pthread_exit(NULL);
}

void dacp_monitor_start() {
  int rc;

  rc = pthread_cond_init(&dacp_server_information_cv, NULL);
  if (rc)
    debug(1, "Error initialising the DACP Server Information Condition Variable");

  pthread_mutexattr_t mta;

  rc = pthread_mutexattr_init(&mta);
  if (rc)
    debug(1, "Error creating the DACP Conversation Lock Mutex Att Init");

  rc = pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_ERRORCHECK);
  if (rc)
    debug(1, "Error creating the DACP Conversation Lock Mutex Errorcheck");

  // rc = pthread_mutexattr_setname_np(&mta, "DACP Conversation Lock");
  // if (rc)
  //  debug(1,"Error creating the DACP Conversation Lock Mutex Set Name");

  rc = pthread_mutex_init(&dacp_conversation_lock, &mta);
  if (rc)
    debug(1, "Error creating the DACP Conversation Lock Mutex Init");
  // else
  //  debug(1, "DACP Conversation Lock Mutex Init");

  rc = pthread_mutexattr_destroy(&mta);
  if (rc)
    debug(1, "Error creating the DACP Conversation Lock Attr Destroy");

  rc = pthread_mutexattr_init(&mta);
  if (rc)
    debug(1, "Error creating the DACP Server Information Lock Mutex Att Init");

  rc = pthread_mutexattr_settype(&mta, PTHREAD_MUTEX_ERRORCHECK);
  if (rc)
    debug(1, "Error creating the DACP Server Information Lock Mutex Errorcheck");

  // rc = pthread_mutexattr_setname_np(&mta, "DACP Conversation Lock");
  // if (rc)
  //  debug(1,"Error creating the DACP Server Information Lock Mutex Set Name");

  rc = pthread_mutex_init(&dacp_server_information_lock, &mta);
  if (rc)
    debug(1, "Error creating the DACP Server Information Lock Mutex Init");

  rc = pthread_mutexattr_destroy(&mta);
  if (rc)
    debug(1, "Error creating the DACP Server Information Lock Attr Destroy");

  memset(&dacp_server, 0, sizeof(dacp_server_record));

  named_pthread_create(&dacp_monitor_thread, NULL, dacp_monitor_thread_code, NULL, "dacp");
  dacp_monitor_initialised = 1;
}

void dacp_monitor_stop() {
  if (dacp_monitor_initialised) { // only if it's been started and initialised
    debug(2, "dacp_monitor_stop");
    pthread_cancel(dacp_monitor_thread);
    pthread_join(dacp_monitor_thread, NULL);
    pthread_mutex_destroy(&dacp_server_information_lock);
    debug(3, "DACP Conversation Lock Mutex Destroyed");
    pthread_mutex_destroy(&dacp_conversation_lock);
    pthread_cond_destroy(&dacp_server_information_cv);
    debug(3, "DACP Server Information Condition Variable destroyed.");
    if (dacp_server.active_remote_id) {
      free(dacp_server.active_remote_id);
      dacp_server.active_remote_id = NULL;
    }
  }
}

uint32_t dacp_tlv_crawl(char **p, int32_t *length) {
  char typecode[5];
  memcpy(typecode, *p, 4);
  typecode[4] = '\0';
  uint32_t type = ntohl(*(uint32_t *)*p);
  *p += 4;
  *length = ntohl(*(uint32_t *)*p);
  *p += 4 + *length;
  // debug(1,"Type seen: '%s' of length %d",typecode,*length);
  return type;
}

int dacp_get_client_volume(int32_t *result) {
  // debug(1,"dacp_get_client_volume");
  char *server_reply = NULL;
  int32_t overall_volume = -1;
  ssize_t reply_size;
  // debug(1,"dacp_get_client_volume: dacp_send_command");
  int response =
      dacp_send_command("getproperty?properties=dmcp.volume", &server_reply, &reply_size);
  if (response == 200) { // if we get an okay
    char *sp = server_reply;
    int32_t item_size;
    if (reply_size >= 8) {
      if (dacp_tlv_crawl(&sp, &item_size) == 'cmgt') {
        sp -= item_size; // drop down into the array -- don't skip over it
        reply_size -= 8;
        while (reply_size >= 8) {
          uint32_t type = dacp_tlv_crawl(&sp, &item_size);
          reply_size -= item_size + 8;
          if (type == 'cmvo') { // drop down into the dictionary -- don't skip over it
            char *t = sp - item_size;
            overall_volume = ntohl(*(uint32_t *)(t));
          }
        }
      } else {
        debug(1, "Unexpected payload response from getproperty?properties=dmcp.volume");
      }
    } else {
      debug(1, "Too short a response from getproperty?properties=dmcp.volume");
    }
    // debug(1, "Overall Volume is %d.", overall_volume);
  }

  if (server_reply) {
    // debug(1, "Freeing response memory.");
    free(server_reply);
    server_reply = NULL;
  }

  if (result) {
    *result = overall_volume;
    // debug(1,"dacp_get_client_volume returns: %" PRId32 ".",overall_volume);
  }
  return response;
}

int dacp_set_include_speaker_volume(int64_t machine_number, int32_t vo) {
  debug(2, "dacp_set_include_speaker_volume to %" PRId32 ".", vo);
  char message[1000];
  memset(message, 0, sizeof(message));
  snprintf(message, sizeof(message),
           "setproperty?include-speaker-id=%" PRId64 "&dmcp.volume=%" PRId32 "", machine_number,
           vo);
  debug(2, "sending \"%s\"", message);
  return send_simple_dacp_command(message);
  // should return 204
}

int dacp_set_speaker_volume(int64_t machine_number, int32_t vo) {
  char message[1000];
  memset(message, 0, sizeof(message));
  snprintf(message, sizeof(message), "setproperty?speaker-id=%" PRId64 "&dmcp.volume=%" PRId32 "",
           machine_number, vo);
  debug(2, "sending \"%s\"", message);
  return send_simple_dacp_command(message);
  // should return 204
}

int dacp_get_speaker_list(dacp_spkr_stuff *speaker_info, int max_size_of_array,
                          int *actual_speaker_count) {
  // char typestring[5];
  char *server_reply = NULL;
  int speaker_index = -1; // will be incremented before use
  int speaker_count = -1; // will be fixed if there is no problem
  ssize_t le;

  // debug(1,"dacp_speaker_list: dacp_send_command");
  int response = dacp_send_command("getspeakers", &server_reply, &le);
  if (response == 200) {
    char *sp = server_reply;
    int32_t item_size;
    if (le >= 8) {
      if (dacp_tlv_crawl(&sp, &item_size) == 'casp') {
        //          debug(1,"Speakers:",item_size);
        sp -= item_size; // drop down into the array -- don't skip over it
        le -= 8;
        while (le >= 8) {
          uint32_t type = dacp_tlv_crawl(&sp, &item_size);
          if (type == 'mdcl') { // drop down into the dictionary -- don't skip over it
            // debug(1,">>>> Dictionary:");
            sp -= item_size;
            le -= 8;
            speaker_index++;
            if (speaker_index == max_size_of_array) {
              return 413; // Payload Too Large -- too many speakers
            }
            speaker_info[speaker_index].active = 0;
            speaker_info[speaker_index].speaker_number = 0;
            speaker_info[speaker_index].volume = 0;
            speaker_info[speaker_index].name[0] = '\0';
          } else {
            le -= item_size + 8;
            char *t;
            // char u;
            int32_t r;
            int64_t s, v;
            switch (type) {
            case 'minm':
              t = sp - item_size;
              strncpy((char *)&speaker_info[speaker_index].name, t,
                      sizeof(speaker_info[speaker_index].name));
              speaker_info[speaker_index].name[sizeof(speaker_info[speaker_index].name) - 1] =
                  '\0'; // just in case
              break;
            case 'cmvo':
              t = sp - item_size;
              r = ntohl(*(uint32_t *)(t));
              speaker_info[speaker_index].volume = r;
              // debug(1,"The individual volume of speaker \"%s\" is
              // \"%d\".",speaker_info[speaker_index].name,r);
              break;
            case 'msma':
              t = sp - item_size;
              s = ntohl(*(uint32_t *)(t));
              s = s << 32;
              t += 4;
              v = (ntohl(*(uint32_t *)(t))) & 0xffffffff;
              s += v;
              speaker_info[speaker_index].speaker_number = s;
              // debug(1,"Speaker machine number: %ld",s);
              break;

            case 'caia':
              speaker_info[speaker_index].active = 1;
              break;
            /*
                            case 'caip':
                            case 'cavd':
                            case 'caiv':
                            case 'cads':

                              *(uint32_t *)typestring = htonl(type);
                              typestring[4] = 0;



                              t = sp-item_size;
                              u = *t;
                              debug(1,"Type: '%s' Value: \"%d\".",typestring,u);
                              break;
            */
            default:
              break;
            }
          }
        }
        // debug(1,"Total of %d speakers found. Here are the active ones:",speaker_index+1);
        speaker_count = speaker_index + 1; // number of speaker entries in the array
      } else {
        debug(1, "Speaker array not found.");
      }
      /*
              int i;
              for (i=0;i<le;i++) {
                if (*sp < ' ')
                  debug(1,"%d  %02x", i, *sp);
                else
                  debug(1,"%d  %02x  '%c'", i, *sp,*sp);
                sp++;
              }
      */
    } else {
      debug(1, "Can't find any content in dacp speakers request");
    }
    free(server_reply);
    server_reply = NULL;
  } else {
    // debug(1, "Unexpected response %d to dacp speakers request", response);
    if (server_reply) {
      debug(1, "Freeing response memory.");
      free(server_reply);
      server_reply = NULL;
    }
  }
  if (actual_speaker_count)
    *actual_speaker_count = speaker_count;
  return response;
}

int dacp_get_volume(int32_t *the_actual_volume) {
  // get the speaker volume information from the DACP source and store it in the metadata_hub
  // A volume command has been sent from the client
  // let's get the master volume from the DACP remote control
  struct dacp_speaker_stuff speaker_info[50];
  // we need the overall volume and the speakers information to get this device's relative volume to
  // calculate the real volume

  int32_t overall_volume = 0;
  int32_t actual_volume = 0;
  int http_response = dacp_get_client_volume(&overall_volume);
  if (http_response == 200) {
    // debug(1,"Overall volume is: %u.",overall_volume);
    int speaker_count = 0;
    http_response = dacp_get_speaker_list((dacp_spkr_stuff *)&speaker_info, 50, &speaker_count);
    if (http_response == 200) {
      // get our machine number
      uint16_t *hn = (uint16_t *)config.ap1_prefix;
      uint32_t *ln = (uint32_t *)(config.ap1_prefix + 2);
      uint64_t t1 = ntohs(*hn);
      uint64_t t2 = ntohl(*ln);
      int64_t machine_number = (t1 << 32) + t2; // this form is useful

      // Let's find our own speaker in the array and pick up its relative volume
      int i;
      int32_t relative_volume = 0;
      for (i = 0; i < speaker_count; i++) {
        if (speaker_info[i].speaker_number == machine_number) {
          relative_volume = speaker_info[i].volume;
          /*
          debug(1,"Our speaker was found with a relative volume of: %u.",relative_volume);

          if (speaker_info[i].active)
            debug(1,"Our speaker is active.");
          else
            debug(1,"Our speaker is inactive.");
          */
        }
      }
      actual_volume = (overall_volume * relative_volume + 50) / 100;
      // debug(1,"Overall volume: %d, relative volume: %d%, actual volume:
      // %d.",overall_volume,relative_volume,actual_volume);
      // debug(1,"Our actual speaker volume is %d.",actual_volume);
      // metadata_hub_modify_prolog();
      // metadata_store.speaker_volume = actual_volume;
      // metadata_hub_modify_epilog(1);
    } else {
      debug(2, "Unexpected return code %d from dacp_get_speaker_list.", http_response);
    }
  } else if ((http_response != 400) && (http_response != 490)) {
    debug(3, "Unexpected return code %d from dacp_get_client_volume.", http_response);
  }
  if (the_actual_volume) {
    // debug(1,"dacp_get_volume returns %d.",actual_volume);
    *the_actual_volume = actual_volume;
  }
  return http_response;
}

int dacp_set_volume(int32_t vo) {
  int http_response = 492; // argument out of range
  if ((vo >= 0) && (vo <= 100)) {
    // get the information we need -- the absolute volume, the speaker list, our ID
    struct dacp_speaker_stuff speaker_info[50];
    int32_t overall_volume;
    http_response = dacp_get_client_volume(&overall_volume);
    if (http_response == 200) {
      int speaker_count;
      http_response = dacp_get_speaker_list((dacp_spkr_stuff *)&speaker_info, 50, &speaker_count);
      if (http_response == 200) {
        // get our machine number
        uint16_t *hn = (uint16_t *)config.ap1_prefix;
        uint32_t *ln = (uint32_t *)(config.ap1_prefix + 2);
        uint64_t t1 = ntohs(*hn);
        uint64_t t2 = ntohl(*ln);
        int64_t machine_number = (t1 << 32) + t2; // this form is useful

        // Let's find our own speaker in the array and pick up its relative volume
        int i;
        int32_t active_speakers = 0;
        for (i = 0; i < speaker_count; i++) {
          if (speaker_info[i].speaker_number == machine_number) {
            debug(2, "Our speaker number found: %" PRId64 " with relative volume %" PRId32 ".", machine_number,
                  speaker_info[i].volume);
          }
          if (speaker_info[i].active == 1) {
            active_speakers++;
          }
        }

        if (active_speakers == 1) {
          // must be just this speaker
          debug(2, "Remote-setting volume to %d on just one speaker.", vo);
          http_response = dacp_set_include_speaker_volume(machine_number, vo);
        } else if (active_speakers == 0) {
          debug(2, "No speakers!");
        } else {
          debug(2, "Speakers: %d, active: %d", speaker_count, active_speakers);
          if (vo >= overall_volume) {
            debug(2, "Multiple speakers active, but desired new volume is highest");
            http_response = dacp_set_include_speaker_volume(machine_number, vo);
          } else {
            // the desired volume is less than the current overall volume and there is more than
            // one
            // speaker
            // we must find out the highest other speaker volume.
            // If the desired volume is less than it, we must set the current_overall volume to
            // that
            // highest volume
            // and set our volume relative to it.
            // If the desired volume is greater than the highest current volume, then we can just
            // go
            // ahead
            // with dacp_set_include_speaker_volume, setting the new current overall volume to the
            // desired new level
            // with the speaker at 100%

            int32_t highest_other_volume = 0;
            for (i = 0; i < speaker_count; i++) {
              if ((speaker_info[i].speaker_number != machine_number) &&
                  (speaker_info[i].active == 1) &&
                  (speaker_info[i].volume > highest_other_volume)) {
                highest_other_volume = speaker_info[i].volume;
              }
            }
            highest_other_volume = (highest_other_volume * overall_volume + 50) / 100;
            if (highest_other_volume <= vo) {
              debug(2,
                    "Highest other volume %d is less than or equal to the desired new volume %d.",
                    highest_other_volume, vo);
              http_response = dacp_set_include_speaker_volume(machine_number, vo);
            } else {
              debug(2, "Highest other volume %d is greater than the desired new volume %d.",
                    highest_other_volume, vo);
              // if the present overall volume is higher than the highest other volume at present,
              // then bring it down to it.
              if (overall_volume > highest_other_volume) {
                debug(2, "Lower overall volume to new highest volume.");
                http_response = dacp_set_include_speaker_volume(
                    machine_number,
                    highest_other_volume); // set the overall volume to the highest one
              }
              if (highest_other_volume != 0) {
                int32_t desired_relative_volume =
                    (vo * 100 + (highest_other_volume / 2)) / highest_other_volume;
                debug(2, "Set our speaker volume relative to the highest volume.");
                http_response = dacp_set_speaker_volume(
                    machine_number,
                    desired_relative_volume); // set the overall volume to the highest one
              }
            }
          }
        }
      } else {
        debug(2, "Can't get speakers list");
      }
    } else {
      debug(2, "Can't get client volume");
    }

  } else {
    debug(2, "Invalid volume: %d -- ignored.", vo);
  }
  return http_response;
}
