/*
 * Apple AirPlay 2 Event Receiver. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2014--2026
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

#include "ap2_event_receiver.h"
#include "ap2_event_message_handler.h"
#include "common.h"
#include "player.h"
#include "utilities/network_utilities.h"

#ifdef CONFIG_METADATA
#include "metadata/core.h"
#endif

void ap2_event_receiver_cleanup_handler(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  // debug(1, "Connection %d: AP2 Event Receiver Cleanup start.", conn->connection_number);
#ifdef CONFIG_METADATA
  // this is here to ensure it's only performed once during a teardown of a ptp stream
  send_ssnc_metadata('disc', conn->client_ip_string, strlen(conn->client_ip_string), 1);
#endif
  debug_mutex_lock(&conn->event_sender_mutex, 1000000, 4);
  pthread_cleanup_push(mutex_unlock, &conn->event_sender_mutex);
  close(conn->event_channel_fd);
  conn->event_channel_fd = 0;
  pthread_cleanup_pop(1); // unlock the mutex

  if (conn->airplay_gid != NULL) {
    free(conn->airplay_gid);
    conn->airplay_gid = NULL;
  }
  conn->groupContainsGroupLeader = 0;
  if (conn->dacp_active_remote != NULL) {
    free(conn->dacp_active_remote);
    conn->dacp_active_remote = NULL;
  }
  debug(2, "Connection %d: AP2 Event Receiver Cleanup is complete.", conn->connection_number);
}

void *ap2_event_receiver(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  debug(2, "Connection %d: AP2 Event Receiver started", conn->connection_number);
  SOCKADDR remote_addr;
  memset(&remote_addr, 0, sizeof(remote_addr));
  socklen_t addr_size = sizeof(remote_addr);

  debug_mutex_lock(&conn->event_sender_mutex, 1000000, 4);
  pthread_cleanup_push(mutex_unlock, &conn->event_sender_mutex);
  conn->event_channel_fd = eintr_checked_accept(conn->event_socket, (struct sockaddr *)&remote_addr, &addr_size);
  pthread_cleanup_pop(1); // unlock the mutex
  if (conn->event_channel_fd > 0) {
    debug(2,
          "Connection %d: ap2_event_receiver accepted a connection on socket %d and moved to a new "
          "socket %d.",
          conn->connection_number, conn->event_socket, conn->event_channel_fd);
    pthread_cleanup_push(ap2_event_receiver_cleanup_handler, arg);
        
    ap2_event_send_update_info(conn);
      
  
    while (1) {
      usleep(1000000);
    };
  
    debug(3, "Connection %d: AP2 Event Receiver RTP thread starting \"normal\" exit.",
          conn->connection_number);
    pthread_cleanup_pop(1); // do the cleanup
  } else {
    debug(1, "Connection %d: could not accept an event socket", conn->connection_number);
    conn->event_channel_fd = 0;
  }
  debug(2, "Connection %d: AP2 Event Receiver RTP thread \"normal\" exit.",
        conn->connection_number);
  conn->ap2_event_receiver_exited = 1;
  pthread_exit(NULL);
}
