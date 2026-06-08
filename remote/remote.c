/*
 * Remote Operations
 * This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2017--2026
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

#include "remote.h"
#include "metadata/hub.h"
#include "player.h"
#include "rtsp.h"

// these are indexed by simple_command_t
static const char *simple_command_strings[] = {
    "play",          /* rcsc_play           = 0 */
    "pause",         /* rcsc_pause          = 1 */
    "playpause",     /* rcsc_play_pause     = 2 */
    "stop",          /* rcsc_stop           = 3 */
    "nextitem",      /* rcsc_next_item      = 4 */
    "previtem",      /* rcsc_previous_item  = 5 */
    "shuffle_songs", /* rcsc_toggle_shuffle = 6 */
    NULL,            /* rcsc_cycle_repeat   = 7 — no match */
    "beginff",       /* rcsc_fast_forward   = 8 */
    NULL,            /* rcsc_fast_forward_stop = 9 — no match */
    "beginrew",      /* rcsc_rewind         = 10 */
    NULL,            /* rcsc_rewind_stop    = 11 — no match */
};

#ifdef CONFIG_DACP_CLIENT
#include "dacp.h"
#endif

#ifdef CONFIG_AIRPLAY_2
#include "ap2_event_receiver.h"
#include "utilities/generate_random_uuid.h"
#include "utilities/rtsp_message_utilities.h"
#include "utilities/structured_buffer.h"

double airplayVolumeToUnitVolume(double airplayVolume) {
  double response = 0.0;
  if ((airplayVolume >= -30.0) && (airplayVolume <= 0.0)) {
    response = airplayVolume / 30.0 + 1.0;
  }
  return response;
}

plist_t prepareNSKeyedArchiver(const char *uid) {
  // this creates a BASE64 encoding of a bplist of an NSKeyedArchiver-encoded
  // NSMutableArray containing a single element: the Group UUID of which this
  // player is a member.

  // clang-format off
    /*
    <?xml version="1.0" encoding="UTF-8"?>
    <!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
      "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
    <plist version="1.0">
    <dict>
      <key>$version</key>
      <integer>100000</integer>

      <key>$archiver</key>
      <string>NSKeyedArchiver</string>

      <key>$top</key>
      <dict>
        <key>root</key>
        <dict><key>CF$UID</key><integer>1</integer></dict>
      </dict>

      <key>$objects</key>
      <array>
        <string>$null</string>                     <!-- 0: always $null -->

        <dict>                                     <!-- 1: the NSMutableArray -->
          <key>NS.objects</key>
          <array>
            <dict><key>CF$UID</key><integer>2</integer></dict>
          </array>
          <key>$class</key>
          <dict><key>CF$UID</key><integer>3</integer></dict>
        </dict>

        <string>343F4AF7-9158-4466-AC59-250A1281FFD6</string>   <!-- 2: the Group UUID string -->

        <dict>                                     <!-- 3: class definition -->
          <key>$classname</key>
          <string>NSMutableArray</string>
          <key>$classes</key>
          <array>
            <string>NSMutableArray</string>
            <string>NSArray</string>
            <string>NSObject</string>
          </array>
        </dict>

      </array>
    </dict>
    </plist>
    */
  // clang-format on

  plist_t archive_plist = plist_new_dict();
  plist_dict_set_item(archive_plist, "$version", plist_new_uint(100000));
  plist_dict_set_item(archive_plist, "$archiver", plist_new_string("NSKeyedArchiver"));

  /* $top */
  plist_t root_uid_plist = plist_new_dict();
  plist_dict_set_item(root_uid_plist, "CF$UID", plist_new_uint(1));

  plist_t top_dict_plist = plist_new_dict();
  plist_dict_set_item(top_dict_plist, "root", root_uid_plist);

  plist_dict_set_item(archive_plist, "$top", top_dict_plist);

  /* $objects array */
  plist_t objects_array = plist_new_array();

  /* [0] $null sentinel */
  plist_array_append_item(objects_array, plist_new_string("$null"));

  /* [1] NSMutableArray dict */
  plist_t class_uid_plist = plist_new_dict();
  plist_dict_set_item(class_uid_plist, "CF$UID", plist_new_uint(3));

  plist_t elem_uid_plist = plist_new_dict();
  plist_dict_set_item(elem_uid_plist, "CF$UID", plist_new_uint(2));

  plist_t ns_objects_array = plist_new_array();
  plist_array_append_item(ns_objects_array, elem_uid_plist);

  plist_t mutable_array_dict = plist_new_dict();
  plist_dict_set_item(mutable_array_dict, "NS.objects", ns_objects_array);
  plist_dict_set_item(mutable_array_dict, "$class", class_uid_plist);

  plist_array_append_item(objects_array, mutable_array_dict);

  /* [2] the UUID string */
  plist_array_append_item(objects_array, plist_new_string(uid));

  /* [3] class definition dict */
  plist_t classes_array = plist_new_array();
  plist_array_append_item(classes_array, plist_new_string("NSMutableArray"));
  plist_array_append_item(classes_array, plist_new_string("NSArray"));
  plist_array_append_item(classes_array, plist_new_string("NSObject"));

  plist_t classdef_dict = plist_new_dict();
  plist_dict_set_item(classdef_dict, "$classname", plist_new_string("NSMutableArray"));
  plist_dict_set_item(classdef_dict, "$classes", classes_array);

  plist_array_append_item(objects_array, classdef_dict);

  /* attach $objects to the archive */
  plist_dict_set_item(archive_plist, "$objects", objects_array);

  return archive_plist; /* caller is responsible for disposing of] this */
}

// this creates a plist will all the components of a modernMediaRemoteCommand,
// but not the "modernMediaRemoteCommand""-keyed item itself,
// nor the "value""-keyed item
void completeModernMediaRemoteCommand(plist_t command_plist, const char *command_UUID,
                                      const char *deviceUUID) {
  plist_dict_set_item(command_plist, "type", plist_new_string("sendMediaRemoteCommand"));

  plist_t params_plist = plist_new_dict();
  plist_dict_set_item(params_plist, "kMRMediaRemoteOptionRemoteControlInterfaceIdentifier",
                      plist_new_string("com.apple.NowPlayingCap.interfaceIdentifer"));
  plist_dict_set_item(params_plist, "kMRMediaRemoteOptionSendOptionsNumber", plist_new_uint(0));
  plist_dict_set_item(params_plist, "kMRMediaRemoteOptionCommandID",
                      plist_new_string(command_UUID));
  plist_dict_set_item(params_plist, "kMRMediaRemoteOptionIsRedirectingCommand",
                      plist_new_bool(1)); // true

  plist_t archive_plist = prepareNSKeyedArchiver(deviceUUID);

  // debug(1, "kMRMediaRemoteOptionDestinationDeviceUIDs archive:");
  // decodeAndLogPlist(archive_plist);

  /* serialise to binary plist */
  char *bplist_buf = NULL;
  uint32_t bplist_len = 0;
  plist_to_bin(archive_plist, &bplist_buf, &bplist_len);
  plist_free(archive_plist);

  plist_dict_set_item(params_plist, "kMRMediaRemoteOptionDestinationDeviceUIDs",
                      plist_new_data(bplist_buf, bplist_len));
  free(bplist_buf);

  plist_dict_set_item(params_plist, "kMRMediaRemoteOptionSenderID",
                      plist_new_string("SenderDevice = <HomePod>, SenderBundleIdentifier = "
                                       "<com.apple.NowPlayingCap>, SenderPID = <353>"));

  plist_dict_set_item(command_plist, "params", params_plist);
}

// send a simple command -- one with a command number an no arguments.
ssize_t ap2_event_send_simple_modern_media_remote_command(rtsp_conn_info *conn,
                                                          unsigned int command_number) {
  ssize_t result = -1;
  char command_number_string[32];
  snprintf(command_number_string, sizeof(command_number_string), "%u", command_number);
  plist_t modernMediaCommand = plist_new_dict();
  plist_dict_set_item(modernMediaCommand, "modernMediaRemoteCommand",
                      plist_new_string(command_number_string));
  // plist_dict_set_item(modernMediaCommand,"value",
  // plist_new_string(command_values[command_number]));
  char *random_UUID = generate_random_uuid();
  completeModernMediaRemoteCommand(modernMediaCommand, random_UUID, conn->airplay_gid);
  free(random_UUID);
  result = ap2_event_port_post_command(conn, modernMediaCommand);
  plist_free(modernMediaCommand);
  return result;
}

ssize_t ap2_event_send_dev_mule(unsigned int command_number) {
  ssize_t result = -1;
  rtsp_conn_info *conn = principal_conn;
  if (conn != NULL) {
    char command_number_string[32];
    snprintf(command_number_string, sizeof(command_number_string), "%u", command_number);
    plist_t modernMediaCommand = plist_new_dict();
    plist_dict_set_item(modernMediaCommand, "modernMediaRemoteCommand",
                        plist_new_string(command_number_string));
    plist_dict_set_item(modernMediaCommand, "kMRMediaRemoteCommandInfoRepeatMode",
                        plist_new_uint(2));
    char *random_UUID = generate_random_uuid();
    completeModernMediaRemoteCommand(modernMediaCommand, random_UUID, conn->airplay_gid);
    free(random_UUID);
    result = ap2_event_port_post_command(conn, modernMediaCommand);
    plist_free(modernMediaCommand);
    if (result <= 0)
      debug(1, "Connection %d: error %zd when sending mule command.", conn->connection_number,
            result);
  } else {
    debug(1, "No connection when sending mule command.");
  }
  return result;
}

ssize_t ap2_event_send_unit_volume_notification(rtsp_conn_info *conn, double volume) {
  ssize_t result = -1;
  // send a volume control notification request
  if ((volume >= 0.0) && (volume <= 1.0)) {
    structured_buffer *sbuf = sbuf_new(4096);
    if (sbuf != NULL) {
      pthread_cleanup_push(sbuf_cleanup, sbuf);
      plist_t params_plist = plist_new_dict();
      plist_dict_set_item(params_plist, "volume", plist_new_real(volume));

      plist_t request_plist = plist_new_dict();
      plist_dict_set_item(request_plist, "value", plist_new_string("dvlc"));
      plist_dict_set_item(request_plist, "volume", plist_new_real(volume));
      plist_dict_set_item(request_plist, "type", plist_new_string("sendMediaRemoteCommand"));
      plist_dict_set_item(request_plist, "params", params_plist);

      char *plistString = NULL;
      uint32_t plistStringLength = 0;
      plist_to_bin(request_plist, &plistString, &plistStringLength);
      if (plistString != NULL) {
        char *plist_as_string = plist_as_xml_text(request_plist);
        if (plist_as_string != NULL) {
          debug(4, "Plist is: \"%s\".", plist_as_string);
          free(plist_as_string);
        }
        sbuf_printf(sbuf, "POST /command RTSP/1.0\r\nContent-Length: %u\r\n", plistStringLength);
        sbuf_printf(sbuf, "Content-Type: application/x-apple-binary-plist\r\n\r\n");
        sbuf_append(sbuf, plistString, plistStringLength);

        free(plistString); // should be plist_to_bin_free, but it's not defined in older
                           // libraries
        char *b = 0;
        size_t l = 0;
        sbuf_buf_and_length(sbuf, &b, &l);
        result = ap2_event_port_send_message(conn, b, l);
        debug(3, "Connection %d: request to set volume to %f sent. Result is %zd.",
              conn->connection_number, volume, result);
        sbuf_clear(sbuf);
      }
      plist_free(request_plist);
      pthread_cleanup_pop(1); // delete the structured buffer
    }
  } else {
    debug(1, "Connection %d: volume notification request is %f, but must be between 0.0 and 1.0",
          conn->connection_number, volume);
  }
  return result;
}
#endif

void remote_set_airplay_volume(double volume) {
  int available = 0;
#ifdef CONFIG_DACP_CLIENT
  available = metadata_store.dacp_server_active;
  if (available) {
    debug(1, "remote_set_airplay_volume to %.3f -- DACP active.", volume);
    char command[256] = "";
    snprintf(command, sizeof(command), "setproperty?dmcp.device-volume=%.6f", volume);
    send_simple_dacp_command(command);
  }
#endif
#ifdef CONFIG_AIRPLAY_2
  pthread_rwlock_rdlock(&principal_conn_lock); // don't let the principal_conn be changed
  pthread_cleanup_push(rwlock_unlock, (void *)&principal_conn_lock);
  if ((available == 0) && (principal_conn != NULL) && (principal_conn->airplay_type == ap_2)) {
    debug(1, "remote_set_airplay_volume to %.3f -- AirPlay 2.", volume);

    double present_unit_volume = airplayVolumeToUnitVolume(config.airplay_volume);
    double desired_unit_volume = airplayVolumeToUnitVolume(volume);

    if (principal_conn != NULL) {
      // It seems that a large change of the notified volume, e.g. from 1.0 to 0.0, evokes
      // a bug in Apple Music on macOS Tahoe, causing the local (mac) volume to jump.
      // So here, we notify changes in 0.09 increments with a short delay between them.
      // The last change can be up to 0.1.
      while (fabs(desired_unit_volume - present_unit_volume) > 1E-3) {
        if (fabs(desired_unit_volume - present_unit_volume) < 0.1) {
          present_unit_volume = desired_unit_volume;
        } else {
          if (desired_unit_volume > present_unit_volume)
            present_unit_volume += 0.09;
          else
            present_unit_volume -= 0.09;
        }
        ap2_event_send_unit_volume_notification(principal_conn, present_unit_volume);
        debug(4, "remote_set_airplay_volume set unit volume to %.3f.", present_unit_volume);
        usleep(10000);
      }
      player_volume(volume, principal_conn);
    } else {
      config.airplay_volume = volume;
    }
  }
  pthread_cleanup_pop(1); // release the principal_conn lock
#endif
}

void remote_simple_command(simple_command_t command) {
  int available = 0;
#ifdef CONFIG_DACP_CLIENT
  available = metadata_store.dacp_server_active;
  if (available) {
    // see if we can find the commands string
    if ((command <= rcsc_rewind_stop) && (simple_command_strings[command] != NULL)) {
      debug(1, "remote_simple_command \"%s\" -- DACP active.", simple_command_strings[command]);
      send_simple_dacp_command(simple_command_strings[command]);
    }
  }
#endif
#ifdef CONFIG_AIRPLAY_2
  pthread_rwlock_rdlock(&principal_conn_lock); // don't let the principal_conn be changed
  pthread_cleanup_push(rwlock_unlock, (void *)&principal_conn_lock);
  if ((available == 0) && (principal_conn != NULL) && (principal_conn->airplay_type == ap_2)) {
    if (principal_conn != NULL) {
      debug(1, "remote_simple_command %u -- AirPlay 2.", command);
      ap2_event_send_simple_modern_media_remote_command(principal_conn, command);
    }
  }
  pthread_cleanup_pop(1); // release the principal_conn lock
#endif
}

void remote_playpause() {
  int available = 0;
#ifdef CONFIG_DACP_CLIENT
  available = metadata_store.dacp_server_active;
  if (available) {
    debug(1, "remote_playpause -- DACP active.");
    send_simple_dacp_command("playpause");
  }
#endif
#ifdef CONFIG_AIRPLAY_2
  pthread_rwlock_rdlock(&principal_conn_lock); // don't let the principal_conn be changed
  pthread_cleanup_push(rwlock_unlock, (void *)&principal_conn_lock);
  if ((available == 0) && (principal_conn != NULL) && (principal_conn->airplay_type == ap_2)) {
    if (principal_conn != NULL) {
      debug(1, "remote_playpause -- AirPlay 2.");
      ap2_event_send_simple_modern_media_remote_command(principal_conn, 2);
    }
  }
  pthread_cleanup_pop(1); // release the principal_conn lock
#endif
}

void remote_player_stop(rtsp_conn_info *conn) {
  if (conn != NULL) {
    debug(1, "Connection %d: remote_player_stop -- AirPlay 2.", conn->connection_number);
    ap2_event_send_simple_modern_media_remote_command(conn, rcsc_stop);
  }
}
