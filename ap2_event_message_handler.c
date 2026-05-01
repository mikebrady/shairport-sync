/*
 * AirPlay 2 Event Port Message Handler. This file is part of Shairport Sync
 * Copyright (c) Mike Brady 2025
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

#include "ap2_event_message_handler.h"
#include "common.h"
#include "rtsp.h"
#include "utilities/structured_buffer.h"
#include "utilities/generate_random_uuid.h"


void decodeAndLogPlist(plist_t plist_to_log) { 
  if (plist_to_log != NULL) {
    char *plist_as_string = plist_as_xml_text(plist_to_log);
    if (plist_as_string != NULL) {
      debug(3, "--\n%s\n--\n", plist_as_string);
      free(plist_as_string);
    }
  }
}

// will return -1 if there is an error or port is not open, 0 if the port was closed and a positive
// number if okay
ssize_t ap2_event_port_send_message(rtsp_conn_info *conn, char *data, size_t data_length) {
  ssize_t result = -1; // assume a problem
  debug_mutex_lock(&conn->event_sender_mutex, 1000000, 4);
  pthread_cleanup_push(mutex_unlock, &conn->event_sender_mutex);
  if (conn->event_channel_fd != 0) {
    result = write_encrypted(conn->event_channel_fd, &conn->ap2_pairing_context.event_cipher_bundle,
                             data, data_length);
    if ((result != -1) && ((size_t)result == data_length)) {
      debug(3, "Connection %d: Packet of %zu bytes successfully written on the Event Port.",
            conn->connection_number, result);
      uint8_t packet[4096];
      result =
          read_encrypted(conn->event_channel_fd, &conn->ap2_pairing_context.event_cipher_bundle,
                         packet, sizeof(packet));
      debug(3, "Connection %d: Packet of %zu bytes successfully read on the Event Port.",
            conn->connection_number, result);
      if (result > 0) {
        packet[result] = '\0';
        debug(3, "Connection %d: Packet Received on Event Port with contents: \n--\n%s\n--\n",
              conn->connection_number, packet);
      } else {
        debug(2, "Connection %d: Event Port connection closed by client", conn->connection_number);
      }
    } else {
      result = -1; // this covers a situation where the result is positive but not the same as the
                   // data_length
    }
  } else {
    debug(1, "Connection %d: attempt to send a command to the event port over a closed socket",
          conn->connection_number);
  }
  pthread_cleanup_pop(1); // unlock the mutex
  return result;
}

ssize_t ap2_event_port_post_command(rtsp_conn_info *conn, plist_t command) {
  ssize_t result = 0;
  decodeAndLogPlist(command);  
  structured_buffer *sbuf = sbuf_new(4096);
  if (sbuf != NULL) {
    pthread_cleanup_push(sbuf_cleanup, sbuf);
    char *plistString = NULL;
    uint32_t plistStringLength = 0;
    
    plist_to_bin(command, &plistString, &plistStringLength);
    if (plistString != NULL) {
      sbuf_printf(sbuf, "POST /command RTSP/1.0\r\nContent-Length: %u\r\n", plistStringLength);
      sbuf_printf(sbuf, "Content-Type: application/x-apple-binary-plist\r\n\r\n");
      sbuf_append(sbuf, plistString, plistStringLength);
      free(plistString); // should be plist_to_bin_free, but it's not defined in older
                         // libraries
      char *b = 0;
      size_t l = 0;
      sbuf_buf_and_length(sbuf, &b, &l);
      result = ap2_event_port_send_message(conn, b, l);
      debug(3, "Connection %d: POST /command sent on the event port. Result is %zd.",
            conn->connection_number, result);
      sbuf_clear(sbuf);
    }
    pthread_cleanup_pop(1); // delete the structured buffer
  }
  return result;  
}

ssize_t ap2_event_send_update_info(rtsp_conn_info *conn) {
  // sends the updateInfo plist on the event port
  ssize_t result = -1;
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
      
      char *plist_as_string = plist_as_xml_text(update_info_plist);
      if (plist_as_string != NULL) {
        debug(3, "update_info_plist is:\n--\n\"%s\"\n--\n", plist_as_string);
        free(plist_as_string);
      }
      
      result = ap2_event_port_post_command(conn, update_info_plist);           
      plist_free(update_info_plist);
    } else {
      debug(1, "Could not build an updateInfo plist");
    }
  } else {
    debug(1, "Could not build an updateInfo value plist");
  }
  return result;
}
