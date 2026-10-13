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
    "volumeup",      /* rcsc_volume_up      = 12 */
    "volumedown",    /* rcsc_volume_down    = 13 */
};

#ifdef CONFIG_DACP_CLIENT
#include "dacp.h"
#endif

#ifdef CONFIG_AIRPLAY_2
#include "ap2_event_receiver.h"
#include "utilities/general_utilities.h"
#include "utilities/generate_random_uuid.h"
#include "utilities/rtsp_message_utilities.h"
#include "utilities/structured_buffer.h"

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

plist_t destinationArchive(const char *deviceUUID) {
  plist_t archive_plist = prepareNSKeyedArchiver(deviceUUID);

  debug(4, "kMRMediaRemoteOptionDestinationDeviceUIDs archive:");
  decodeAndLogPlist(4, archive_plist);

  /* serialise to binary plist */
  char *bplist_buf = NULL;
  uint32_t bplist_len = 0;
  plist_to_bin(archive_plist, &bplist_buf, &bplist_len);
  plist_free(archive_plist);

  plist_t reply = plist_new_data(bplist_buf, bplist_len);
  free(bplist_buf);
  return reply;
}

plist_t paramsPlist(unsigned int send_options_number, const char *deviceUUID) {
  plist_t params_plist = plist_new_dict();
  // plist_dict_set_item(params_plist, "kMRMediaRemoteOptionRemoteControlInterfaceIdentifier",
  //                     plist_new_string("org.gnome.ShairportSync"));
  plist_dict_set_item(params_plist, "kMRMediaRemoteOptionSendOptionsNumber",
                      plist_new_uint(send_options_number));
  char *command_UUID = generate_random_uuid();
  plist_dict_set_item(params_plist, "kMRMediaRemoteOptionCommandID",
                      plist_new_string(command_UUID));
  free(command_UUID);
  plist_dict_set_item(params_plist, "kMRMediaRemoteOptionIsRedirectingCommand",
                      plist_new_bool(1)); // true

  plist_dict_set_item(params_plist, "kMRMediaRemoteOptionDestinationDeviceUIDs",
                      destinationArchive(deviceUUID));
  return params_plist;
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
  plist_dict_set_item(modernMediaCommand, "type", plist_new_string("sendMediaRemoteCommand"));
  plist_dict_set_item(modernMediaCommand, "params", paramsPlist(0, conn->airplay_gid));
  result = ap2_event_port_post_command(conn, modernMediaCommand);
  plist_free(modernMediaCommand);
  return result;
}

/*
ssize_t ap2_event_send_dev_mule(unsigned int parameter) {
  ssize_t result = -1;
  rtsp_conn_info *conn = principal_conn;
  if (conn != NULL) {
    char command_number_string[32];
    snprintf(command_number_string, sizeof(command_number_string), "%u", 26);
    plist_t modernMediaCommand = plist_new_dict();
    plist_dict_set_item(modernMediaCommand, "modernMediaRemoteCommand",
                        plist_new_string(command_number_string));

    plist_dict_set_item(modernMediaCommand, "type", plist_new_string("sendMediaRemoteCommand"));

    plist_t params = paramsPlist(0, conn->airplay_gid);
    if (params != NULL)
      // add this parameter
      plist_dict_set_item(params, "kMRMediaRemoteOptionShuffleMode",
                        plist_new_uint(parameter));
    plist_dict_set_item(modernMediaCommand, "params", params);

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
*/
ssize_t ap2_event_send_dev_mule(unsigned int repeat_mode) {
  ssize_t result = -1;
  rtsp_conn_info *conn = principal_conn;
  if (conn != NULL) {
    plist_t modernMediaCommand = plist_new_dict();
    plist_dict_set_item(modernMediaCommand, "modernMediaRemoteCommand",
                        plist_new_string("26")); // set shuffle mode

    plist_dict_set_item(modernMediaCommand, "type", plist_new_string("sendMediaRemoteCommand"));

    plist_t params = paramsPlist(0, conn->airplay_gid);
    if (params != NULL)
      plist_dict_set_item(params, "kMRMediaRemoteOptionShuffleMode", plist_new_uint(repeat_mode));
    plist_dict_set_item(modernMediaCommand, "params", params);

    result = ap2_event_port_post_command(conn, modernMediaCommand);
    plist_free(modernMediaCommand);
    if (result <= 0)
      debug(1, "Connection %d: error %zd when sending set shuffle mode command.",
            conn->connection_number, result);
  } else {
    debug(1, "No connection when sending set shuffle mode command.");
  }
  return result;
}

ssize_t ap2_event_send_set_repeat_mode(unsigned int repeat_mode) {
  ssize_t result = -1;
  rtsp_conn_info *conn = principal_conn;
  if (conn != NULL) {
    plist_t modernMediaCommand = plist_new_dict();
    plist_dict_set_item(modernMediaCommand, "modernMediaRemoteCommand",
                        plist_new_string("25")); // set repeat mode

    plist_dict_set_item(modernMediaCommand, "type", plist_new_string("sendMediaRemoteCommand"));

    plist_t params = paramsPlist(0, conn->airplay_gid);
    if (params != NULL)
      plist_dict_set_item(params, "kMRMediaRemoteOptionRepeatMode", plist_new_uint(repeat_mode));
    plist_dict_set_item(modernMediaCommand, "params", params);

    result = ap2_event_port_post_command(conn, modernMediaCommand);
    plist_free(modernMediaCommand);
    if (result <= 0)
      debug(1, "Connection %d: error %zd when sending set repeat mode command.",
            conn->connection_number, result);
  } else {
    debug(1, "No connection when sending set repeat mode command.");
  }
  return result;
}

ssize_t ap2_event_send_set_shuffle_mode(unsigned int shuffle_mode) {
  ssize_t result = -1;
  rtsp_conn_info *conn = principal_conn;
  if (conn != NULL) {
    plist_t modernMediaCommand = plist_new_dict();
    plist_dict_set_item(modernMediaCommand, "modernMediaRemoteCommand",
                        plist_new_string("26")); // set shuffle mode

    plist_dict_set_item(modernMediaCommand, "type", plist_new_string("sendMediaRemoteCommand"));

    plist_t params = paramsPlist(0, conn->airplay_gid);
    if (params != NULL)
      plist_dict_set_item(params, "kMRMediaRemoteOptionShuffleMode", plist_new_uint(shuffle_mode));
    plist_dict_set_item(modernMediaCommand, "params", params);

    result = ap2_event_port_post_command(conn, modernMediaCommand);
    plist_free(modernMediaCommand);
    if (result <= 0)
      debug(1, "Connection %d: error %zd when sending set shuffle mode command.",
            conn->connection_number, result);
  } else {
    debug(1, "No connection when sending set shuffle mode command.");
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

void ap2_remote_set_volume(double volume) {
  pthread_rwlock_rdlock(&principal_conn_lock); // don't let the principal_conn be changed
  pthread_cleanup_push(rwlock_unlock, (void *)&principal_conn_lock);
  if ((principal_conn != NULL) && (principal_conn->airplay_type == ap_2)) {
    debug(4, "remote_set_airplay_volume to %.3f -- AirPlay 2.", volume);

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
}

#endif

#ifdef CONFIG_AIRPLAY_2
void remote_increment_volume(int up) {
  const double increment = 1.875;

  debug(4, "config.volume is %f.", config.airplay_volume);
  double desired_volume = config.airplay_volume;
  if (desired_volume < -30.0)
    desired_volume = -30.0;

  if (up == 0) {
    desired_volume -= increment;
  } else {
    desired_volume += increment;
  }

  if (desired_volume < -30.0)
    desired_volume = -144.0;
  else if (desired_volume > 0.0)
    desired_volume = 0.0;

  pthread_rwlock_rdlock(&principal_conn_lock); // don't let the principal_conn be changed
  pthread_cleanup_push(rwlock_unlock, (void *)&principal_conn_lock);
  if ((principal_conn != NULL) && (principal_conn->airplay_type == ap_2)) {
    debug(4, "remote_increment_volume %s", up == 0 ? "down" : "up");

    double desired_unit_volume = airplayVolumeToUnitVolume(desired_volume);

    if (principal_conn != NULL) {
      ap2_event_send_unit_volume_notification(principal_conn, desired_unit_volume);
      debug(4, "remote_increment_volume set unit volume to %.3f.", desired_unit_volume);
      player_volume(desired_volume, principal_conn);
    }
  } else {
    config.airplay_volume = desired_volume;
  }
  pthread_cleanup_pop(1); // release the principal_conn lock
}
#endif

void remote_volumeup() {
// int available = 0;
#ifdef CONFIG_DACP_CLIENT
  if (metadata_store.dacp_server_active) {
    debug(4, "remote_volumeup -- DACP active.");
    send_simple_dacp_command("volumeup");
  }
#endif
#ifdef CONFIG_AIRPLAY_2
  // if (available == 0)
  remote_increment_volume(1); // increment up
#endif
}

void remote_volumedown() {
  int available = 0;
#ifdef CONFIG_DACP_CLIENT
  available = metadata_store.dacp_server_active;
  if (available) {
    debug(4, "remote_volumedown -- DACP active.");
    send_simple_dacp_command("volumedown");
  }
#endif
#ifdef CONFIG_AIRPLAY_2
  if (available == 0)
    remote_increment_volume(0); // increment down
#endif
}

// this is the "advanced" set volume capability in the Music app on classic AirPlay only.
int remote_set_integer_percent_volume(const int volume) {
  int handled = 1;
  int available = 0;
#ifdef CONFIG_DACP_CLIENT
  available = metadata_store.advanced_dacp_server_active;
  if (available) {
    dacp_set_integer_percent_volume(volume);
  }
#endif
// not quite the same... this only affects this speaker...
#ifdef CONFIG_AIRPLAY_2
  if (available == 0) {
    if (volume == 0) {
      ap2_remote_set_volume(-144.0);
    } else {
      ap2_remote_set_volume((volume * 30.0 / 100.0) - 30.0);
    }
  }
#endif
  return handled;
}

int remote_set_airplay_volume(double volume) {
  int handled = 1;
  int available = 0;
#ifdef CONFIG_DACP_CLIENT
  available = metadata_store.dacp_server_active;
  if (available) {
    debug(4, "remote_set_airplay_volume to %.3f -- DACP active.", volume);
    char command[256] = "";
    snprintf(command, sizeof(command), "setproperty?dmcp.device-volume=%.6f", volume);
    send_simple_dacp_command(command);
  }
#endif
#ifdef CONFIG_AIRPLAY_2
  if (available == 0) {
    ap2_remote_set_volume(volume);
  }
#endif
  return handled;
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
      switch (command) {
      case rcsc_not_a_command: // do nothing...
        break;
      case rcsc_volume_up:
        remote_volumeup();
        break;
      case rcsc_volume_down:
        remote_volumedown();
        break;
      case rcsc_disconnect:
        stop_play(); // stop any current session and don't replace it
        break;
      default:
        debug(4, "remote_simple_command %u -- AirPlay 2.", command);
        ap2_event_send_simple_modern_media_remote_command(principal_conn, command);
        break;
      }
    }
  }
  pthread_cleanup_pop(1); // release the principal_conn lock
#endif
}

int remote_set_repeat_mode(repeat_status_type mode) {
  int handled = 0;
  pthread_rwlock_rdlock(&principal_conn_lock); // don't let the principal_conn be changed
  pthread_cleanup_push(rwlock_unlock, (void *)&principal_conn_lock);
  if (principal_conn != NULL) {
    int command_handled_in_airplay_2 = 0;
#if CONFIG_AIRPLAY_2
    if (principal_conn->airplay_type == ap_2) {
      command_handled_in_airplay_2 = 1; // handled even if not successful
      switch (mode) {
      case RS_OFF:
        ap2_event_send_set_repeat_mode(1);
        break;
      case RS_ONE:
        ap2_event_send_set_repeat_mode(2);
        break;
      case RS_ALL:
        ap2_event_send_set_repeat_mode(3);
        break;
      default:
        debug(1, "AP2 invalid repeat mode request -- ignored.");
        break;
      }
      handled = 1;
    }
#endif
#ifdef CONFIG_DACP_CLIENT
    if (command_handled_in_airplay_2 == 0) {
      if (metadata_store.advanced_dacp_server_active != 0) {
        switch (mode) {
        case RS_OFF:
          send_simple_dacp_command("setproperty?dacp.repeatstate=0");
          break;
        case RS_ONE:
          send_simple_dacp_command("setproperty?dacp.repeatstate=1");
          break;
        case RS_ALL:
          send_simple_dacp_command("setproperty?dacp.repeatstate=2");
          break;
        default:
          debug(1, "DACP invalid repeat mode request -- ignored.");
          break;
        }
        handled = 1;
      } else {
        inform("Can't set loop status / repeat mode -- advanced remote control is not available "
               "for this client.");
      }
    }
#endif
  }
  pthread_cleanup_pop(1); // release the principal_conn lock
  return handled;
}

int remote_set_shuffle_mode(shuffle_status_type mode) {
  int handled = 1;                             // default
  pthread_rwlock_rdlock(&principal_conn_lock); // don't let the principal_conn be changed
  pthread_cleanup_push(rwlock_unlock, (void *)&principal_conn_lock);
  if (principal_conn != NULL) {
    int command_handled_in_airplay_2 = 0;
#if CONFIG_AIRPLAY_2
    if (principal_conn->airplay_type == ap_2) {
      command_handled_in_airplay_2 = 1; // handled even if not successful
      switch (mode) {
      case SS_OFF:
        ap2_event_send_set_shuffle_mode(1); // seems to be shuffle off
        break;
      case SS_ON:
        ap2_event_send_set_shuffle_mode(3); // seems to be shuffle songs
        break;
      default:
        debug(1, "AP2 invalid shuffle mode request -- ignored.");
        break;
      }
    }
#endif
#ifdef CONFIG_DACP_CLIENT
    if (command_handled_in_airplay_2 == 0) {
      if (metadata_store.advanced_dacp_server_active != 0) {
        switch (mode) {
        case SS_OFF:
          send_simple_dacp_command("setproperty?dacp.shufflestate=0");
          break;
        case SS_ON:
          send_simple_dacp_command("setproperty?dacp.shufflestate=1");
          break;
        default:
          debug(1, "DACP invalid shuffle mode request -- ignored.");
          break;
        }
      } else {
        inform("Can't set loop status / repeat mode -- advanced remote control is not available "
               "for this client.");
      }
    }
#endif
  }
  pthread_cleanup_pop(1); // release the principal_conn lock
  return handled;
}
