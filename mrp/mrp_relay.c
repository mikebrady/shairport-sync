/*
 * AirPlay 2 remote-control (MediaRemote) support. This file is part of Shairport Sync.
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

// How this works
//
// An iPhone, iPad or Mac can open a "remote control only" AirPlay 2 connection to us, which
// carries MediaRemote (MRP) protobuf messages in POST /command requests and returns the replies on
// the event channel. We relay the messages the way an AirPort Express does: we do not make up
// anything of our own.
//
// Each controller data channel is given a stream number on the connection of the source (the
// device that is playing to us, which must have said in its SETUP that it supports relay). The
// controller's messages are passed to the source as POST /command on the source's event channel,
// and the source's replies and updates, which come back as POST /command on its own connection,
// are passed to the controller whose stream they belong to. The source supplies all the
// now-playing state, artwork and queue, and carries out the commands. A source that does not
// answer the session messages like an Apple device does (OwnTone, say) leaves the controllers with
// nothing.

#include "mrp_relay.h"
#include "ap2_event_receiver.h"
#include "common.h"
#include "utilities/general_utilities.h"
#include "mrp_protobuf.h"
#include "utilities/rtsp_message_utilities.h"
#include "utilities/generate_random_uuid.h"
#include <plist/plist.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Parts of the ProtocolMessage envelope that we look at when logging: field 1 = type, field 2 =
// identifier; the payload is in an extension field whose number depends on the type.
#define MRP_TYPE_SEND_COMMAND 1
#define MRP_EXT_SEND_COMMAND 6
#define MRP_TYPE_SEND_COMMAND_RESULT 2
#define MRP_EXT_SEND_COMMAND_RESULT 7

// Every relayed message is logged, in both directions, at debug level 3 (-vvv). There is a line for
// each message saying which way it is going, on which streams, what it is, how big it is and its
// identifier (a reply has the identifier of the request it answers), and for a command, which
// command and what the result was. The fields of each message are then logged at the debug level
// MRP_LOG_RELAYED_FIELDS_LEVEL: 3 as well, 4 only at -vvvv, and 0 not at all. A picture or other
// bulk data shows as its length.
#define MRP_LOG_RELAYED_FIELDS_LEVEL 3

// Message type names, as in the MediaRemote ProtocolMessage.Type enumeration (see pyatv's .proto
// files), without the "_MESSAGE" ending. A type 0 message that we send is an empty
// acknowledgement, and the log calls it that.
// Type 131 is not in pyatv's list. It is sent by a source in its opening burst, once for each
// player, after the PLAYER_CLIENT_PROPERTIES message. It has a uniqueIdentifier (field 85) and,
// in field 103, a player path followed by what looks like account details (a device UID, a
// "sp." identifier and the user's name). The name is a guess, hence the question mark.
static const char *mrp_type_name(uint64_t t) {
  switch (t) {
  case 0: return "UNKNOWN_MESSAGE_0";
  case 1: return "SEND_COMMAND";
  case 2: return "SEND_COMMAND_RESULT";
  case 3: return "GET_STATE";
  case 4: return "SET_STATE";
  case 5: return "SET_ARTWORK";
  case 6: return "REGISTER_HID_DEVICE";
  case 7: return "REGISTER_HID_DEVICE_RESULT";
  case 8: return "SEND_HID_EVENT";
  case 9: return "SEND_HID_REPORT";
  case 10: return "SEND_VIRTUAL_TOUCH_EVENT";
  case 11: return "NOTIFICATION";
  case 12: return "CONTENT_ITEMS_CHANGED_NOTIFICATION";
  case 15: return "DEVICE_INFO";
  case 16: return "CLIENT_UPDATES_CONFIG";
  case 17: return "VOLUME_CONTROL_AVAILABILITY";
  case 18: return "GAME_CONTROLLER";
  case 19: return "REGISTER_GAME_CONTROLLER";
  case 20: return "REGISTER_GAME_CONTROLLER_RESPONSE";
  case 21: return "UNREGISTER_GAME_CONTROLLER";
  case 22: return "REGISTER_FOR_GAME_CONTROLLER_EVENTS";
  case 23: return "KEYBOARD";
  case 24: return "GET_KEYBOARD_SESSION";
  case 25: return "TEXT_INPUT";
  case 26: return "GET_VOICE_INPUT_DEVICES";
  case 27: return "GET_VOICE_INPUT_DEVICES_RESPONSE";
  case 28: return "REGISTER_VOICE_INPUT_DEVICE";
  case 29: return "REGISTER_VOICE_INPUT_DEVICE_RESPONSE";
  case 30: return "SET_RECORDING_STATE";
  case 31: return "SEND_VOICE_INPUT";
  case 32: return "PLAYBACK_QUEUE_REQUEST";
  case 33: return "TRANSACTION";
  case 34: return "CRYPTO_PAIRING";
  case 35: return "GAME_CONTROLLER_PROPERTIES";
  case 36: return "SET_READY_STATE";
  case 37: return "DEVICE_INFO_UPDATE";
  case 38: return "SET_CONNECTION_STATE";
  case 39: return "SEND_BUTTON_EVENT";
  case 40: return "SET_HILITE_MODE";
  case 41: return "WAKE_DEVICE";
  case 42: return "GENERIC";
  case 43: return "SEND_PACKED_VIRTUAL_TOUCH_EVENT";
  case 44: return "SEND_LYRICS_EVENT";
  case 46: return "SET_NOW_PLAYING_CLIENT";
  case 47: return "SET_NOW_PLAYING_PLAYER";
  case 48: return "MODIFY_OUTPUT_CONTEXT_REQUEST";
  case 49: return "GET_VOLUME";
  case 50: return "GET_VOLUME_RESULT";
  case 51: return "SET_VOLUME";
  case 52: return "VOLUME_DID_CHANGE";
  case 53: return "REMOVE_CLIENT";
  case 54: return "REMOVE_PLAYER";
  case 55: return "UPDATE_CLIENT";
  case 56: return "UPDATE_CONTENT_ITEM";
  case 57: return "UPDATE_CONTENT_ITEM_ARTWORK";
  case 58: return "UPDATE_PLAYER";
  case 59: return "PROMPT_FOR_ROUTE_AUTHORIZATION";
  case 60: return "PROMPT_FOR_ROUTE_AUTHORIZATION_RESPONSE";
  case 61: return "PRESENT_ROUTE_AUTHORIZATION_STATUS";
  case 62: return "GET_VOLUME_CONTROL_CAPABILITIES";
  case 63: return "GET_VOLUME_CONTROL_CAPABILITIES_RESULT";
  case 64: return "VOLUME_CONTROL_CAPABILITIES_DID_CHANGE";
  case 65: return "UPDATE_OUTPUT_DEVICE";
  case 66: return "REMOVE_OUTPUT_DEVICES";
  case 67: return "REMOTE_TEXT_INPUT";
  case 68: return "GET_REMOTE_TEXT_INPUT_SESSION";
  case 69: return "REMOVE_OUTPUT_DEVICES_2";
  case 70: return "PLAYBACK_SESSION_REQUEST";
  case 71: return "PLAYBACK_SESSION_RESPONSE";
  case 72: return "SET_DEFAULT_SUPPORTED_COMMANDS";
  case 73: return "PLAYBACK_SESSION_MIGRATE_REQUEST";
  case 74: return "PLAYBACK_SESSION_MIGRATE_RESPONSE";
  case 75: return "PLAYBACK_SESSION_MIGRATE_BEGIN";
  case 76: return "PLAYBACK_SESSION_MIGRATE_END";
  case 77: return "UPDATE_ACTIVE_SYSTEM_ENDPOINT";
  case 101: return "SET_DISCOVERY_MODE";
  case 102: return "UPDATE_END_POINTS";
  case 103: return "REMOVE_ENDPOINTS";
  case 104: return "PLAYER_CLIENT_PROPERTIES";
  case 105: return "ORIGIN_CLIENT_PROPERTIES";
  case 106: return "AUDIO_FADE";
  case 107: return "AUDIO_FADE_RESPONSE";
  case 120: return "CONFIGURE_CONNECTION";
  case 131: return "PLAYER_ACCOUNT_INFO?";
  default: return "?";
  }
}

// The name the controller gave itself in SETUP (e.g. "Mike Brady's iPhone"), for log messages.
static const char *mrp_peer_name(rtsp_conn_info *conn) {
  const char *name = conn->ap2_client_name;
  return name != NULL ? name : "?";
}

// Close a connection. Setting its stop flag makes its thread exit and clean up for itself, but the
// thread is usually blocked reading, so shut the socket down too, to wake it.
static void close_connection(rtsp_conn_info *conn) {
  conn->stop = 1;
  if (conn->fd > 0)
    shutdown(conn->fd, SHUT_RDWR);
}

// Send some framed messages to a controller on the event channel. If the controller does not
// accept them (a 400, say) it seems to have abandoned the MediaRemote session, so it will not use
// anything we send it: close the whole connection.
static void send_data_event(rtsp_conn_info *conn, int stream_id, const uint8_t *data,
                                    size_t length) {
  plist_t params = plist_new_dict();
  plist_dict_set_item(params, "data", plist_new_data((const char *)data, length));
  plist_t top = plist_new_dict();
  plist_dict_set_item(top, "params", params);
  ssize_t r = ap2_event_port_post_command_stream(conn, top, stream_id);
  debug(3, "Connection %d (\"%s\"): MRP: sent %zu bytes on the event channel (stream %d), result %zd.",
        conn->connection_number, mrp_peer_name(conn), length, stream_id, r);
  plist_free(top);
  if (r < 0 && conn->stop == 0) {
    debug(2,
          "Connection %d (\"%s\"): MRP: the controller did not accept the message, so this "
          "connection is being closed.",
          conn->connection_number, mrp_peer_name(conn));
    close_connection(conn);
  }
}

// The names of the MediaRemote commands, as in pyatv's CommandInfo.proto.
static const char *mrp_command_name(uint64_t c) {
  switch (c) {
  case 0: return "Unknown";
  case 1: return "Play";
  case 2: return "Pause";
  case 3: return "TogglePlayPause";
  case 4: return "Stop";
  case 5: return "NextTrack";
  case 6: return "PreviousTrack";
  case 7: return "AdvanceShuffleMode";
  case 8: return "AdvanceRepeatMode";
  case 9: return "BeginFastForward";
  case 10: return "EndFastForward";
  case 11: return "BeginRewind";
  case 12: return "EndRewind";
  case 13: return "Rewind15Seconds";
  case 14: return "FastForward15Seconds";
  case 15: return "Rewind30Seconds";
  case 16: return "FastForward30Seconds";
  case 18: return "SkipForward";
  case 19: return "SkipBackward";
  case 20: return "ChangePlaybackRate";
  case 21: return "RateTrack";
  case 22: return "LikeTrack";
  case 23: return "DislikeTrack";
  case 24: return "BookmarkTrack";
  case 25: return "NextChapter";
  case 26: return "PreviousChapter";
  case 27: return "NextAlbum";
  case 28: return "PreviousAlbum";
  case 29: return "NextPlaylist";
  case 30: return "PreviousPlaylist";
  case 31: return "BanTrack";
  case 32: return "AddTrackToWishList";
  case 33: return "RemoveTrackFromWishList";
  case 34: return "NextInContext";
  case 35: return "PreviousInContext";
  case 41: return "ResetPlaybackTimeout";
  case 45: return "SeekToPlaybackPosition";
  case 46: return "ChangeRepeatMode";
  case 47: return "ChangeShuffleMode";
  case 48: return "SetPlaybackQueue";
  case 49: return "AddNowPlayingItemToLibrary";
  case 50: return "CreateRadioStation";
  case 51: return "AddItemToLibrary";
  case 52: return "InsertIntoPlaybackQueue";
  case 53: return "EnableLanguageOption";
  case 54: return "DisableLanguageOption";
  case 55: return "ReorderPlaybackQueue";
  case 56: return "RemoveFromPlaybackQueue";
  case 57: return "PlayItemInPlaybackQueue";
  case 58: return "PrepareForSetQueue";
  case 59: return "SetPlaybackSession";
  case 60: return "PreloadedPlaybackSession";
  case 61: return "SetPriorityForPlaybackSession";
  case 62: return "DiscardPlaybackSession";
  case 63: return "Reshuffle";
  case 135: return "ChangeQueueEndAction";
  default: return "?";
  }
}

// Log some framed messages that are being relayed: which way they are going, the streams, and for
// each message its type, size and identifier, and for SEND_COMMAND the command, and for
// SEND_COMMAND_RESULT the result. controller_stream is 0 if there is no controller for the
// message.
static void log_relayed(rtsp_conn_info *conn, int to_source, int controller_stream,
                        int source_stream, const uint8_t *data, size_t length) {
  size_t pos = 0;
  char route[96];
  if (controller_stream > 0)
    snprintf(route, sizeof(route), to_source ? "controller stream %d -> source stream %d"
                                             : "source stream %d -> controller stream %d",
             to_source ? controller_stream : source_stream,
             to_source ? source_stream : controller_stream);
  else
    snprintf(route, sizeof(route), "source stream %d -> no controller", source_stream);
  char prefix[48];
  snprintf(prefix, sizeof(prefix), to_source ? "MRP c%d>s%d " : "MRP s%d>c%d ",
           to_source ? controller_stream : source_stream,
           to_source ? source_stream : controller_stream);
  while (pos < length) {
    uint64_t l;
    if (pb_read_varint(data, length, &pos, &l) != 0 || l > length - pos) {
      debug(2, "Connection %d (\"%s\"): MRP: %s: message framing is not valid.",
            conn->connection_number, mrp_peer_name(conn), route);
      return;
    }
    const uint8_t *m = data + pos;
    size_t ml = (size_t)l;
    pos += ml;
    uint64_t type = 0;
    const uint8_t *ident = NULL;
    size_t ident_length = 0;
    size_t p = 0;
    pb_field f;
    while (pb_next(m, ml, &p, &f) == 1) {
      if (f.field == 1 && f.wire == 0)
        type = f.value;
      else if (f.field == 2 && f.wire == 2) {
        ident = f.data;
        ident_length = f.length;
      }
    }
    char detail[128] = "";
    pb_field ext, g;
    if (type == MRP_TYPE_SEND_COMMAND && pb_find(m, ml, MRP_EXT_SEND_COMMAND, 2, &ext) &&
        pb_find(ext.data, ext.length, 1, 0, &g))
      snprintf(detail, sizeof(detail), ", command %llu (%s)", (unsigned long long)g.value,
               mrp_command_name(g.value));
    else if (type == MRP_TYPE_SEND_COMMAND_RESULT &&
             pb_find(m, ml, MRP_EXT_SEND_COMMAND_RESULT, 2, &ext)) {
      // SendCommandResultMessage: sendError (1), handlerReturnStatus (2)
      unsigned long long send_error = 0, status = 0;
      if (pb_find(ext.data, ext.length, 1, 0, &g))
        send_error = g.value;
      if (pb_find(ext.data, ext.length, 2, 0, &g))
        status = g.value;
      snprintf(detail, sizeof(detail), ", sendError %llu, handlerReturnStatus %llu", send_error,
               status);
    }
    debug(3, "Connection %d (\"%s\"): MRP: %s: %s (type %llu), %zu bytes, id %.*s%s.",
          conn->connection_number, mrp_peer_name(conn), route,
          type == 0 ? "ACKNOWLEDGE" : mrp_type_name(type), (unsigned long long)type, ml,
          (int)ident_length, ident != NULL ? (const char *)ident : "", detail);
    if (MRP_LOG_RELAYED_FIELDS_LEVEL > 0)
      pb_dump(MRP_LOG_RELAYED_FIELDS_LEVEL, prefix, m, ml);
  }
}

#define MRP_PT_MAX 16
// The type UUID of a remote control session, as seen in an AirPort Express's session requests.
#define MRP_PT_CLIENT_TYPE_UUID "1910A70F-DBC0-4242-AF95-115DB30604E1"

typedef struct {
  rtsp_conn_info *conn;  // the controller's connection; NULL if the entry is free
  int controller_stream; // the stream (data channel) the controller uses on its connection
  int sender_stream;     // the stream we gave it on the source's connection
} pt_entry;

static pt_entry pt_map[MRP_PT_MAX];
static int pt_next_stream = 1;
static pthread_mutex_t pt_mutex = PTHREAD_MUTEX_INITIALIZER;

static plist_t pt_data_plist(const uint8_t *data, size_t length) {
  plist_t params = plist_new_dict();
  plist_dict_set_item(params, "data", plist_new_data((const char *)data, length));
  plist_t top = plist_new_dict();
  plist_dict_set_item(top, "params", params);
  return top;
}

// Post a command on the event channel of the source, which is the connection that holds the play
// lock. Returns -1 if there is no source.
static ssize_t pt_post_to_source(plist_t command, int stream_id) {
  ssize_t result = -1;
  pthread_rwlock_rdlock(&principal_conn_lock);
  pthread_cleanup_push(rwlock_unlock, (void *)&principal_conn_lock);
  if (principal_conn != NULL && principal_conn->airplay_type == ap_2 &&
      principal_conn->airplay_stream_category != remote_control_stream)
    result = ap2_event_port_post_command_stream(principal_conn, command, stream_id);
  pthread_cleanup_pop(1);
  return result;
}

// Extract the framed MediaRemote messages from a POST /command body. The bytes are malloc'd.
static uint8_t *pt_extract_data(rtsp_message *req, size_t *length) {
  uint8_t *result = NULL;
  *length = 0;
  plist_t root = NULL;
  plist_from_memory(req->content, req->contentlength, &root);
  if (root != NULL && plist_get_node_type(root) == PLIST_DICT) {
    plist_t params = plist_dict_get_item(root, "params");
    plist_t data = params ? plist_dict_get_item(params, "data") : NULL;
    if (data != NULL && plist_get_node_type(data) == PLIST_DATA) {
      uint64_t l = 0;
      char *bytes = NULL;
      plist_get_data_val(data, &bytes, &l);
      if (bytes != NULL && l > 0) {
        result = (uint8_t *)bytes;
        *length = (size_t)l;
      } else {
        free(bytes);
      }
    }
  }
  if (root != NULL)
    plist_free(root);
  return result;
}

// A controller has sent messages: pass them to the source on the stream we gave this channel,
// starting a session for the channel first if it is new.
void mrp_relay_handle_command(rtsp_conn_info *conn, rtsp_message *req) {
  if (req->content == NULL || req->contentlength <= 0)
    return; // the controller's empty keepalive POSTs
  int controller_stream = 1;
  char *stream_header = msg_get_header(req, "X-Apple-StreamID");
  if (stream_header != NULL && atoi(stream_header) > 0)
    controller_stream = atoi(stream_header);

  size_t length = 0;
  uint8_t *data = pt_extract_data(req, &length);
  if (data == NULL) {
    debug(2, "Connection %d (\"%s\"): MRP: POST /command has no params/data.",
          conn->connection_number, mrp_peer_name(conn));
    return;
  }

  int sender_stream = 0, created = 0;
  pthread_mutex_lock(&pt_mutex);
  for (int i = 0; i < MRP_PT_MAX && sender_stream == 0; i++)
    if (pt_map[i].conn == conn && pt_map[i].controller_stream == controller_stream)
      sender_stream = pt_map[i].sender_stream;
  for (int i = 0; i < MRP_PT_MAX && sender_stream == 0; i++)
    if (pt_map[i].conn == NULL) {
      sender_stream = pt_next_stream++;
      pt_map[i].conn = conn;
      pt_map[i].controller_stream = controller_stream;
      pt_map[i].sender_stream = sender_stream;
      created = 1;
    }
  pthread_mutex_unlock(&pt_mutex);
  if (sender_stream == 0) {
    debug(2, "Connection %d (\"%s\"): MRP: too many relay streams.", conn->connection_number,
          mrp_peer_name(conn));
    free(data);
    return;
  }

  if (created) {
    // ask the source to open a remote control session for this channel
    char *uuid = generate_random_uuid();
    plist_t create = plist_new_dict();
    plist_dict_set_item(create, "type", plist_new_string("remoteConrolSessionCreate")); // sic
    plist_dict_set_item(create, "streamID", plist_new_uint((uint64_t)sender_stream));
    plist_dict_set_item(create, "clientTypeUUID", plist_new_string(MRP_PT_CLIENT_TYPE_UUID));
    plist_dict_set_item(create, "clientUUID", plist_new_string(uuid != NULL ? uuid : ""));
    free(uuid);
    ssize_t r = pt_post_to_source(create, 0);
    plist_free(create);
    debug(3,
          "Connection %d (\"%s\"): MRP: session for controller stream %d requested from the "
          "source as stream %d, result %zd.",
          conn->connection_number, mrp_peer_name(conn), controller_stream, sender_stream, r);
  }

  log_relayed(conn, 1, controller_stream, sender_stream, data, length);
  plist_t top = pt_data_plist(data, length);
  ssize_t r = pt_post_to_source(top, sender_stream);
  plist_free(top);
  debug(3,
        "Connection %d (\"%s\"): MRP: passed %zu bytes from controller stream %d to the source "
        "(stream %d), result %zd.",
        conn->connection_number, mrp_peer_name(conn), length, controller_stream, sender_stream, r);
  free(data);
}

// The source has sent a POST /command on its own connection with a stream number: it is a reply to
// a controller, or an update for it. Pass it to the controller whose stream it is, and answer the
// source with an empty plist, as an AirPort Express does.
void mrp_relay_handle_sender_command(rtsp_conn_info *conn, rtsp_message *req,
                                     rtsp_message *resp) {
  char *stream_header = msg_get_header(req, "X-Apple-StreamID");
  if (stream_header == NULL || req->content == NULL || req->contentlength <= 0)
    return; // not one of ours: leave it to the rest of the code
  int sender_stream = atoi(stream_header);
  size_t length = 0;
  uint8_t *data = pt_extract_data(req, &length);
  if (data != NULL) {
    int found = 0;
    pthread_mutex_lock(&pt_mutex);
    for (int i = 0; i < MRP_PT_MAX && !found; i++)
      if (pt_map[i].conn != NULL && pt_map[i].sender_stream == sender_stream) {
        found = 1;
        log_relayed(pt_map[i].conn, 0, pt_map[i].controller_stream, sender_stream, data, length);
        send_data_event(pt_map[i].conn, pt_map[i].controller_stream, data, length);
      }
    pthread_mutex_unlock(&pt_mutex);
    if (!found) {
      debug(3, "Connection %d: MRP: a message from the source for unknown stream %d.",
            conn->connection_number, sender_stream);
      log_relayed(conn, 0, 0, sender_stream, data, length);
    }
    free(data);
  }
  debug(3, "Connection %d: MRP: source stream %d: answered with an empty plist.",
        conn->connection_number, sender_stream);
  plist_t empty = plist_new_dict();
  plist_to_bin(empty, &resp->content, &resp->contentlength);
  plist_free(empty);
  msg_add_header(resp, "Content-Type", "application/x-apple-binary-plist");
}

static void pt_forget_connection(rtsp_conn_info *conn) {
  pthread_mutex_lock(&pt_mutex);
  for (int i = 0; i < MRP_PT_MAX; i++)
    if (pt_map[i].conn == conn)
      pt_map[i].conn = NULL;
  pthread_mutex_unlock(&pt_mutex);
}

// The source has gone, so its sessions have too: close the controllers' connections. Each one's
// thread exits and cleans up for itself. The controllers find us again when something plays.
static void pt_close_all(void) {
  pthread_mutex_lock(&pt_mutex);
  for (int i = 0; i < MRP_PT_MAX; i++)
    if (pt_map[i].conn != NULL) {
      rtsp_conn_info *c = pt_map[i].conn;
      debug(2, "Connection %d (\"%s\"): MRP: closing this relay connection (stream %d).",
            c->connection_number, mrp_peer_name(c), pt_map[i].controller_stream);
      if (c->stop == 0)
        close_connection(c);
      pt_map[i].conn = NULL;
    }
  pthread_mutex_unlock(&pt_mutex);
}

void mrp_relay_close_all(void) { pt_close_all(); }

void mrp_relay_cleanup(rtsp_conn_info *conn) { pt_forget_connection(conn); }
