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
#include "utilities/generate_random_uuid.h"
#include "utilities/structured_buffer.h"
#include <errno.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>

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
  pthread_mutex_lock(&conn->event_sender_mutex);
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

static ssize_t ap2_event_port_send_media_remote_message(
    rtsp_conn_info *conn, char *data, size_t data_length) {
  ssize_t result = -1;
  int receive_timeout_changed = 0;
  int send_timeout_changed = 0;
  int timeout_setup_ok = 1;
  struct timeval old_receive_timeout;
  struct timeval old_send_timeout;
  socklen_t old_receive_timeout_length = sizeof(old_receive_timeout);
  socklen_t old_send_timeout_length = sizeof(old_send_timeout);

  pthread_mutex_lock(&conn->event_sender_mutex);
  pthread_cleanup_push(mutex_unlock, &conn->event_sender_mutex);

  if ((conn->event_channel_fd > 0) &&
      (conn->ap2_pairing_context.event_cipher_bundle.cipher_ctx != NULL)) {
    if (getsockopt(conn->event_channel_fd, SOL_SOCKET, SO_RCVTIMEO, &old_receive_timeout,
                   &old_receive_timeout_length) == -1) {
      debug(1, "Connection %d: unable to read MediaRemote receive timeout: %d.",
            conn->connection_number, errno);
      timeout_setup_ok = 0;
    }
    if (getsockopt(conn->event_channel_fd, SOL_SOCKET, SO_SNDTIMEO, &old_send_timeout,
                   &old_send_timeout_length) == -1) {
      debug(1, "Connection %d: unable to read MediaRemote send timeout: %d.",
            conn->connection_number, errno);
      timeout_setup_ok = 0;
    }

    if (timeout_setup_ok != 0) {
      // Match the existing DACP control path's 500 ms socket timeout.
      struct timeval event_timeout;
      event_timeout.tv_sec = 0;
      event_timeout.tv_usec = 500000;

      if (setsockopt(conn->event_channel_fd, SOL_SOCKET, SO_RCVTIMEO, &event_timeout,
                     sizeof(event_timeout)) == -1) {
        debug(1, "Connection %d: unable to set MediaRemote receive timeout: %d.",
              conn->connection_number, errno);
        timeout_setup_ok = 0;
      } else {
        receive_timeout_changed = 1;
      }

      if ((timeout_setup_ok != 0) &&
          (setsockopt(conn->event_channel_fd, SOL_SOCKET, SO_SNDTIMEO, &event_timeout,
                      sizeof(event_timeout)) == -1)) {
        debug(1, "Connection %d: unable to set MediaRemote send timeout: %d.",
              conn->connection_number, errno);
        timeout_setup_ok = 0;
      } else if (timeout_setup_ok != 0) {
        send_timeout_changed = 1;
      }
    }

    // Do not risk an unbounded transaction if the socket timeout could not be established.
    if (timeout_setup_ok != 0) {
      errno = 0;
      ssize_t written =
          write_encrypted(conn->event_channel_fd, &conn->ap2_pairing_context.event_cipher_bundle,
                          data, data_length);
      if ((written >= 0) && ((size_t)written == data_length)) {
        uint8_t packet[4097];
        errno = 0;
        result = read_encrypted(conn->event_channel_fd,
                                &conn->ap2_pairing_context.event_cipher_bundle, packet,
                                sizeof(packet) - 1);
        if (result > 0) {
          packet[result] = '\0';
          int response_code = 0;
          if (sscanf((char *)packet, "RTSP/%*s %d", &response_code) != 1) {
            debug(1, "Connection %d: malformed MediaRemote RTSP response.",
                  conn->connection_number);
            result = -1;
          } else if ((response_code < 200) || (response_code >= 300)) {
            debug(1, "Connection %d: MediaRemote RTSP response status %d.",
                  conn->connection_number, response_code);
            result = -1;
          } else {
            debug(2, "Connection %d: MediaRemote RTSP response status %d.",
                  conn->connection_number, response_code);
          }
        } else {
          debug(1, "Connection %d: MediaRemote response failed or timed out (errno %d).",
                conn->connection_number, errno);
          result = -1;
        }
      } else {
        debug(1, "Connection %d: MediaRemote write failed or timed out (errno %d).",
              conn->connection_number, errno);
        result = -1;
      }
    } else {
      debug(1, "Connection %d: MediaRemote transaction not attempted because a bounded socket "
               "timeout could not be established.",
            conn->connection_number);
      result = -1;
    }

    if (receive_timeout_changed != 0) {
      if (setsockopt(conn->event_channel_fd, SOL_SOCKET, SO_RCVTIMEO, &old_receive_timeout,
                     sizeof(old_receive_timeout)) == -1)
        debug(1, "Connection %d: unable to restore event receive timeout: %d.",
              conn->connection_number, errno);
    }
    if (send_timeout_changed != 0) {
      if (setsockopt(conn->event_channel_fd, SOL_SOCKET, SO_SNDTIMEO, &old_send_timeout,
                     sizeof(old_send_timeout)) == -1)
        debug(1, "Connection %d: unable to restore event send timeout: %d.",
              conn->connection_number, errno);
    }
  } else {
    debug(1, "Connection %d: MediaRemote requested without a ready encrypted event channel.",
          conn->connection_number);
  }

  pthread_cleanup_pop(1);
  return result;
}

static ssize_t ap2_event_port_post_media_remote_command(rtsp_conn_info *conn, plist_t command) {
  ssize_t result = -1;
  decodeAndLogPlist(command);
  structured_buffer *sbuf = sbuf_new(4096);
  if (sbuf != NULL) {
    pthread_cleanup_push(sbuf_cleanup, sbuf);
    char *plist_string = NULL;
    uint32_t plist_string_length = 0;
    plist_to_bin(command, &plist_string, &plist_string_length);
    if (plist_string != NULL) {
      sbuf_printf(sbuf, "POST /command RTSP/1.0\r\nContent-Length: %u\r\n",
                  plist_string_length);
      sbuf_printf(sbuf, "Content-Type: application/x-apple-binary-plist\r\n\r\n");
      sbuf_append(sbuf, plist_string, plist_string_length);
      free(plist_string);
      char *buffer = NULL;
      size_t length = 0;
      sbuf_buf_and_length(sbuf, &buffer, &length);
      result = ap2_event_port_send_media_remote_message(conn, buffer, length);
    }
    pthread_cleanup_pop(1);
  }
  return result;
}

ssize_t ap2_event_send_modern_media_remote_command(rtsp_conn_info *conn,
                                                   unsigned int command_number) {
  ssize_t result = -1;

  if (conn == NULL)
    return result;

  // Transport control support is intentionally limited to the six basic commands.
  if (command_number > 5) {
    debug(1, "Connection %d: unsupported AirPlay 2 MediaRemote transport command %u.",
          conn->connection_number, command_number);
    return result;
  }

  plist_t command_plist = plist_new_dict();
  if (command_plist == NULL)
    return result;

  plist_t params_plist = plist_new_dict();
  if (params_plist == NULL) {
    plist_free(command_plist);
    return result;
  }

  char *command_uuid = generate_random_uuid();
  if (command_uuid == NULL) {
    plist_free(params_plist);
    plist_free(command_plist);
    debug(1, "Connection %d: unable to generate MediaRemote command UUID.",
          conn->connection_number);
    return result;
  }

  plist_dict_set_item(command_plist, "type", plist_new_string("sendMediaRemoteCommand"));
  plist_dict_set_item(command_plist, "modernMediaRemoteCommand", plist_new_uint(command_number));
  plist_dict_set_item(params_plist, "kMRMediaRemoteOptionCommandID",
                      plist_new_string(command_uuid));
  free(command_uuid);

  plist_dict_set_item(params_plist, "kMRMediaRemoteOptionOriginatedFromRemoteDevice",
                      plist_new_uint(1));
  plist_dict_set_item(params_plist, "kMRMediaRemoteOptionSendOptionsNumber", plist_new_uint(0));
  plist_dict_set_item(params_plist, "kMRMediaRemoteOptionIsRedirectingCommand",
                      plist_new_uint(1));
  plist_dict_set_item(command_plist, "params", params_plist);

  debug(2, "Connection %d: sending AirPlay 2 MediaRemote transport command %u.",
        conn->connection_number, command_number);
  result = ap2_event_port_post_media_remote_command(conn, command_plist);
  debug(2, "Connection %d: AirPlay 2 MediaRemote transport command %u result %zd.",
        conn->connection_number, command_number, result);
  plist_free(command_plist);
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
