/*
 * Apple AirPlay 2 Remote Control (RC) Event Receiver. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2014--2025
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

#include "ap2_rc_event_receiver.h"
#include "common.h"
#include "player.h"
#include "rtsp.h"
#include "utilities/network_utilities.h"
#include "utilities/structured_buffer.h"

void ap2_rc_event_receiver_cleanup_handler(void *arg) {
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  debug(2, "Connection %d: AP2 Event Receiver Cleanup.", conn->connection_number);
}

void *ap2_rc_event_receiver(void *arg) {
  //  #include <syscall.h>
  //  debug(1, "rtp_event_receiver PID %d", syscall(SYS_gettid));
  rtsp_conn_info *conn = (rtsp_conn_info *)arg;
  if (conn->airplay_stream_category == remote_control_stream)
    debug(2, "Connection %d (RC): AP2 RC Event Receiver started", conn->connection_number);
  else
    debug(2, "Connection %d: AP2 RC Event Receiver started", conn->connection_number);

  structured_buffer *sbuf = sbuf_new(4096);
  if (sbuf != NULL) {
    pthread_cleanup_push(sbuf_cleanup, sbuf);

    pthread_cleanup_push(ap2_rc_event_receiver_cleanup_handler, arg);

    // listen(conn->event_socket, 5); // this is now done in the handle_setup_2 code

    uint8_t packet[4096];
    ssize_t nread;
    SOCKADDR remote_addr;
    memset(&remote_addr, 0, sizeof(remote_addr));
    socklen_t addr_size = sizeof(remote_addr);

    int fd = eintr_checked_accept(conn->event_socket, (struct sockaddr *)&remote_addr, &addr_size);
    debug(2,
          "Connection %d: ap2_rc_event_receiver accepted a connection on socket %d and moved to a "
          "new "
          "socket %d.",
          conn->connection_number, conn->event_socket, fd);
    intptr_t pfd = fd;
    pthread_cleanup_push(socket_cleanup, (void *)pfd);
    int finished = 0;
    do {

      plist_t value_plist = generateInfoPlist(conn);
      if (value_plist != NULL) {
        void *txtData = NULL;
        size_t txtDataLength = 0;
        generateTxtDataValueInfo(conn, &txtData, &txtDataLength);
        plist_dict_set_item(value_plist, "txtAirPlay", plist_new_data(txtData, txtDataLength));
        free(txtData);
        plist_t update_info_plist = plist_new_dict();
        if (update_info_plist != NULL) {
          plist_dict_set_item(update_info_plist, "type", plist_new_string("updateInfo"));
          plist_dict_set_item(update_info_plist, "value", value_plist);
          char *plistString = NULL;
          uint32_t plistStringLength = 0;
          plist_to_bin(update_info_plist, &plistString, &plistStringLength);
          if (plistString != NULL) {
            char *plist_as_string = plist_as_xml_text(update_info_plist);
            if (plist_as_string != NULL) {
              debug(3, "Plist is: \"%s\".", plist_as_string);
              free(plist_as_string);
            }
            sbuf_printf(sbuf, "POST /command RTSP/1.0\r\nContent-Length: %u\r\n",
                        plistStringLength);
            sbuf_printf(sbuf, "Content-Type: application/x-apple-binary-plist\r\n\r\n");
            sbuf_append(sbuf, plistString, plistStringLength);

            free(plistString); // should be plist_to_bin_free, but it's not defined in older
                               // libraries
            char *b = 0;
            size_t l = 0;
            sbuf_buf_and_length(sbuf, &b, &l);
            ssize_t wres =
                write_encrypted(fd, &conn->ap2_pairing_context.event_cipher_bundle, b, l);
            if ((wres == -1) || ((size_t)wres != l))
              debug(1, "Encrypted write error");

            sbuf_clear(sbuf);
          } else {
            debug(1, "plist string not created!");
          }
          plist_free(update_info_plist);
        } else {
          debug(1, "Could not build an updateInfo plist");
        }
        // plist_free(value_plist);
      } else {
        debug(1, "Could not build an value plist");
      }

      while (finished == 0) {
        nread = read_encrypted(fd, &conn->ap2_pairing_context.event_cipher_bundle, packet,
                               sizeof(packet));

        // nread = recv(fd, packet, sizeof(packet), 0);

        if (nread < 0) {
          char errorstring[1024];
          strerror_r(errno, (char *)errorstring, sizeof(errorstring));
          debug(
              1,
              "Connection %d: error in ap2_rc_event_receiver %d: \"%s\". Could not recv a packet.",
              conn->connection_number, errno, errorstring);
          // if ((config.diagnostic_drop_packet_fraction == 0.0) ||
          //     (drand48() > config.diagnostic_drop_packet_fraction)) {
        } else if (nread > 0) {

          // ssize_t plen = nread;
          packet[nread] = '\0';
          debug(2,
                "Connection %d: ap2_rc_event_receiver Packet Received on Event Port with contents: "
                "\"%s\".",
                conn->connection_number, packet);
        } else {
          debug(2, "Connection %d: ap2_rc_event_receiver Event Port connection closed by client",
                conn->connection_number);
          finished = 1;
        }
      }

    } while (finished == 0);
    pthread_cleanup_pop(1); // close the socket
    pthread_cleanup_pop(1); // do the cleanup
    pthread_cleanup_pop(1); // delete the structured buffer
    debug(2, "Connection %d: AP2 ap2_rc_event_receiver  \"normal\" exit.", conn->connection_number);
  } else {
    debug(1, "Could not allocate a structured buffer!");
  }
  conn->ap2_event_receiver_exited = 1;
  pthread_exit(NULL);
}
