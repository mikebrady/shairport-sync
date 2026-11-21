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
// carries MediaRemote (MRP) protobuf messages in POST /command requests and returns ours on the
// event channel. There are two ways to deal with the messages. Which is used is set by
// MRP_PASSTHROUGH below.
//
// Pass-through (MRP_PASSTHROUGH 1, as an AirPort Express does it): we do not make up anything.
// Each controller data channel is given a stream number on the connection of the source (the
// device that is playing to us), the controller's messages are passed to the source, and the
// source's replies and updates go back to the controller. It needs a source that supports relay.
//
// Synthetic state (MRP_PASSTHROUGH 0): we answer the controllers ourselves, from what Shairport
// Sync knows about the track (from the metadata hub), and pass play, pause, skip and volume
// commands on to the source with the remote control code. This works with any source, but the
// controllers see less. The code for this mode is the handlers for the controller's messages
// (handle_data and the functions it calls), the now-playing snapshot with its metadata watcher,
// and the pusher thread, which sends our state to the controllers.

#include "mrp_relay.h"
#include "ap2_event_receiver.h"
#include "common.h"
#include "utilities/general_utilities.h"
#include "mrp_protobuf.h"
#include "utilities/rtsp_message_utilities.h"
#include "utilities/generate_random_uuid.h"
#include <plist/plist.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#ifdef CONFIG_DACP_CLIENT
#include "remote/remote.h"
#endif
#ifdef CONFIG_METADATA_HUB
#include "metadata/hub.h"
#include "metadata/core.h"
#endif

// ProtocolMessage envelope: field 1 = type, field 2 = identifier; the payload is in an
// extension field whose number depends on the type.
#define MRP_TYPE_SEND_COMMAND 1
#define MRP_EXT_SEND_COMMAND 6
#define MRP_TYPE_SEND_COMMAND_RESULT 2
#define MRP_EXT_SEND_COMMAND_RESULT 7
#define MRP_TYPE_SET_STATE 4
#define MRP_EXT_SET_STATE 9
#define MRP_TYPE_DEVICE_INFO 15
#define MRP_EXT_DEVICE_INFO 20
#define MRP_TYPE_CLIENT_UPDATES_CONFIG 16
#define MRP_TYPE_VOLUME_CONTROL_AVAILABILITY 17
#define MRP_EXT_VOLUME_CONTROL_AVAILABILITY 22
#define MRP_TYPE_PLAYBACK_QUEUE_REQUEST 32
#define MRP_TYPE_SET_CONNECTION_STATE 38
#define MRP_EXT_SET_CONNECTION_STATE 42
#define MRP_TYPE_SET_NOW_PLAYING_CLIENT 46
#define MRP_EXT_SET_NOW_PLAYING_CLIENT 50
#define MRP_TYPE_SET_NOW_PLAYING_PLAYER 47
#define MRP_EXT_SET_NOW_PLAYING_PLAYER 51
#define MRP_TYPE_GET_VOLUME 49
#define MRP_TYPE_GET_VOLUME_RESULT 50
#define MRP_EXT_GET_VOLUME_RESULT 54
#define MRP_TYPE_SET_VOLUME 51
#define MRP_EXT_SET_VOLUME 55
#define MRP_TYPE_VOLUME_DID_CHANGE 52
#define MRP_EXT_VOLUME_DID_CHANGE 56
#define MRP_TYPE_REMOVE_CLIENT 53
#define MRP_EXT_REMOVE_CLIENT 57
#define MRP_TYPE_REMOVE_PLAYER 54
#define MRP_EXT_REMOVE_PLAYER 58
#define MRP_TYPE_UPDATE_CLIENT 55
#define MRP_EXT_UPDATE_CLIENT 59
#define MRP_TYPE_UPDATE_CONTENT_ITEM_ARTWORK 57
#define MRP_EXT_UPDATE_CONTENT_ITEM_ARTWORK 61
#define MRP_TYPE_UPDATE_PLAYER 58
#define MRP_EXT_UPDATE_PLAYER 62
#define MRP_TYPE_VOLUME_CONTROL_CAPABILITIES_DID_CHANGE 64
#define MRP_EXT_VOLUME_CONTROL_CAPABILITIES_DID_CHANGE 68
#define MRP_TYPE_UPDATE_OUTPUT_DEVICE 65
#define MRP_EXT_UPDATE_OUTPUT_DEVICE 69
#define MRP_TYPE_PLAYER_CLIENT_PROPERTIES 104
#define MRP_EXT_PLAYER_CLIENT_PROPERTIES 86
#define MRP_TYPE_ORIGIN_CLIENT_PROPERTIES 105
#define MRP_EXT_ORIGIN_CLIENT_PROPERTIES 87

// MediaRemote command numbers that we can pass on to the source of the audio
#define MRP_CMD_PLAY 1
#define MRP_CMD_PAUSE 2
#define MRP_CMD_TOGGLE_PLAY_PAUSE 3
#define MRP_CMD_STOP 4
#define MRP_CMD_NEXT_TRACK 5
#define MRP_CMD_PREVIOUS_TRACK 6
// a controller sends this before it sets a queue; we say yes and ignore it
#define MRP_CMD_PREPARE_FOR_SET_QUEUE 58
#define MRP_SEND_ERROR_NOT_SUPPORTED 10

// The protocol's volume is a number from 0.0 to 1.0; ours is an AirPlay volume in dB.
#define MRP_VOLUME_CAPABILITIES_ABSOLUTE 2

// Synthetic state only. Artwork is passed by reference, as a real HomePod does: the sender's artworkIdentifier (an
// Apple CDN URL template with {w}, {h} and {f}) and a filled-in artworkURL, which the controller
// fetches itself. This is the size used for the URL.
#define XSTR_(x) #x
#define XSTR(x) XSTR_(x)
#define MRP_ARTWORK_SIZE 3000
// The iPad's lock screen does not use the URL: it asks for the picture (PLAYBACK_QUEUE_REQUEST),
// and we send the sender's cover file in the reply. Cover files larger than this are not sent.
#define MRP_ARTWORK_MAX_BYTES 2000000

// The connection states of SET_CONNECTION_STATE.
#define MRP_CONNECTION_STATE_DISCONNECTED 3

// The playback states of SET_STATE.
#define MRP_PLAYBACK_STATE_UNKNOWN 0
#define MRP_PLAYBACK_STATE_INTERRUPTED 4

// Experiments. 1 is on, 0 is off. The ones about our own state (all except MRP_PASSTHROUGH) have
// an effect only with MRP_PASSTHROUGH at 0.
//
// MRP_SEND_DISCONNECTED_BEFORE_CLOSE: when the Relay flag is cleared, send each relay controller
// a SET_CONNECTION_STATE message saying "Disconnected", and then, only if
// MRP_CLOSE_AFTER_DISCONNECTED is 1, close the connection. With it at 0 the connection is left
// open, and the controller goes on listing us. If MRP_SEND_DISCONNECTED_BEFORE_CLOSE is 0, the
// connection is closed without a message. At 1 (as now), the controller drops us when the source
// disconnects, and it finds us again when something plays.
#define MRP_SEND_DISCONNECTED_BEFORE_CLOSE 1
#define MRP_CLOSE_AFTER_DISCONNECTED 1
// MRP_SEND_ARTWORK_UPDATES: when a track's artwork changes, send the controller an
// UPDATE_CONTENT_ITEM_ARTWORK message, as a real HomePod does, so that a controller showing the
// full-size artwork (the iPad's Music app, for example) asks for the new picture.
#define MRP_SEND_ARTWORK_UPDATES 1
// MRP_LOCAL_ARTWORK_FALLBACK: some sources (the Broadcasts app, for example) give no artwork URL
// but do send a picture. When that happens, say that the item has artwork, with an identifier of
// our own, so that the controller asks for the picture and we send our copy of it (see
// queue_artwork_reply).
#define MRP_LOCAL_ARTWORK_FALLBACK 1
// What a real HomePod does when a controller subscribes (compare its capture): it sends the
// volume control availability and the volume itself, and it introduces a "default player" of the
// system's own (client com.apple.tvairplayd) before the player of the source app.
// MRP_SEND_VOLUME_STATE: send VOLUME_CONTROL_AVAILABILITY and VOLUME_DID_CHANGE.
// MRP_SEND_DEFAULT_PLAYER: send the HomePod's default player before ours.
#define MRP_SEND_VOLUME_STATE 1
#define MRP_SEND_DEFAULT_PLAYER 1
// MRP_IDLE_STATE_INTERRUPTED: when nothing is playing, say the playback state is "Interrupted",
// as a HomePod does for the item that was last played, instead of "Stopped".
#define MRP_IDLE_STATE_INTERRUPTED 1
// MRP_PASSTHROUGH: relay MediaRemote the way an AirPort Express does. We do not answer the
// controllers ourselves: each controller data channel is given a stream number towards the
// source (the iPhone, iPad or Mac that is playing to us), the controller's messages are passed to
// the source as POST /command on the source's event channel, and the source's replies and its own
// updates are passed back to the controller. The source supplies all the now-playing state,
// artwork and queue, and carries out the commands. It only works while a source that supports
// relay (its SETUP had senderSupportsRelay) is playing to us, and one that answers the session
// messages like an Apple device does; a source that does not (OwnTone, say) leaves the
// controllers with nothing. When this is 1, none of the experiments above, which are about our
// own state, has any effect. At 0, we make up the state ourselves.
#define MRP_PASSTHROUGH 1
// MRP_LOG_RELAYED: with MRP_PASSTHROUGH, log every message that is relayed, in both directions.
// There is a line for each message (the debug level is 2, as -vv) saying which way it is going,
// on which streams, what it is, how big it is and its identifier (a reply has the identifier of
// the request it answers), and for a command, which command and what the result was. The fields
// of each message are then logged at the debug level MRP_LOG_RELAYED_FIELDS_LEVEL: 2 logs them
// all at -vv, 3 only at -vvv, and 0 not at all. A picture or other bulk data shows as its length.
#define MRP_LOG_RELAYED 1
#define MRP_LOG_RELAYED_FIELDS_LEVEL 2

// What we call our now-playing "app" and what the pane says when nothing is playing. A real
// HomePod names the source app as its client; its capture has "com.apple.Music". Calling ourselves
// Music makes the iPad's Music app show the artwork, text and buttons in its own expanded player.
// A real HomePod's controllers also need the origin, process and audio session details in the
// player path (see build_player_path), or the iPad crashes when it has artwork to show.
#define MRP_CLIENT_BUNDLE_ID "com.apple.Music"
#define MRP_CLIENT_DISPLAY_NAME "Music"
#define MRP_NOT_CONNECTED "Not Connected"

#define MRP_MAX_MESSAGE 65536
#define MRP_MAX_MESSAGES_PER_POST 16

// Message type names, as in the MediaRemote ProtocolMessage.Type enumeration (see pyatv's .proto
// files), without the "_MESSAGE" ending. A type 0 message that we send is an empty
// acknowledgement, and the log calls it that (see log_outgoing).
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

static void queue_reply(rtsp_conn_info *conn, const uint8_t *data, size_t length) {
  uint8_t *n = realloc(conn->mrp_pending, conn->mrp_pending_length + length);
  if (n == NULL) {
    debug(2, "Connection %d (\"%s\"): MRP: out of memory queueing a reply.",
          conn->connection_number, mrp_peer_name(conn));
    return;
  }
  memcpy(n + conn->mrp_pending_length, data, length);
  conn->mrp_pending = n;
  conn->mrp_pending_length += length;
}

// Frame one ProtocolMessage: a varint length, then the message, which has the type (field 1), the
// identifier (field 2, left out if ident is NULL) and the payload in an extension field (left out
// if ext_field is 0). Returns a malloc'd buffer, with its length in *length, or NULL.
static uint8_t *frame_message(uint32_t type, const void *ident, size_t ident_length,
                              uint32_t ext_field, const void *payload, size_t payload_length,
                              size_t *length) {
  size_t cap = ident_length + payload_length + 64;
  uint8_t *body = malloc(cap);
  uint8_t *out = malloc(cap + 10);
  if (body == NULL || out == NULL) {
    free(body);
    free(out);
    return NULL;
  }
  pb_writer b, o;
  pb_writer_init(&b, body, cap);
  pb_put_uint(&b, 1, type);
  if (ident != NULL)
    pb_put_bytes(&b, 2, ident, ident_length);
  if (ext_field != 0)
    pb_put_bytes(&b, ext_field, payload, payload_length);
  pb_writer_init(&o, out, cap + 10);
  pb_put_varint(&o, b.length);
  if (b.overflow || o.overflow) {
    free(body);
    free(out);
    return NULL;
  }
  memcpy(out + o.length, body, b.length);
  *length = o.length + b.length;
  free(body);
  return out;
}

// The same, for a message of our own, which gets a new random identifier.
static uint8_t *frame_new_message(uint32_t type, uint32_t ext_field, const void *payload,
                                  size_t payload_length, size_t *length) {
  char *uuid = generate_random_uuid();
  uint8_t *framed = frame_message(type, uuid, uuid != NULL ? strlen(uuid) : 0, ext_field, payload,
                                  payload_length, length);
  free(uuid);
  return framed;
}

// Append one framed message of our own to w.
static void append_message(pb_writer *w, uint32_t type, uint32_t ext_field, const uint8_t *payload,
                           size_t payload_length) {
  size_t length = 0;
  uint8_t *framed = frame_new_message(type, ext_field, payload, payload_length, &length);
  if (framed == NULL || length > w->capacity - w->length) {
    w->overflow = 1;
  } else {
    memcpy(w->buf + w->length, framed, length);
    w->length += length;
  }
  free(framed);
}

// Queue a reply to a message from the controller, carrying its identifier.
static void queue_message(rtsp_conn_info *conn, uint32_t type, const uint8_t *ident,
                          size_t ident_length, uint32_t ext_field, const void *payload,
                          size_t payload_length) {
  size_t length = 0;
  uint8_t *framed =
      frame_message(type, ident, ident_length, ext_field, payload, payload_length, &length);
  if (framed != NULL) {
    queue_reply(conn, framed, length);
    free(framed);
  }
}

// Queue the reply to a DEVICE_INFO message.
static void queue_device_info_reply(rtsp_conn_info *conn, const uint8_t *ident,
                                    size_t ident_length) {
  uint8_t info[512];
  pb_writer d;
  pb_writer_init(&d, info, sizeof(info));
  // DeviceInfoMessage. A HomePod uses one identifier for itself, its "psi", as the uniqueIdentifier
  // (1), the deviceUID (19) and the outputDeviceUID in the volume messages, so that a controller
  // that matches them (pyatv does, with the deviceUID) finds our volume.
  pb_put_string(&d, 1, config.airplay_psi);   // uniqueIdentifier
  pb_put_string(&d, 2, config.service_name);  // name
  pb_put_string(&d, 3, "ShairportSync");      // localizedModelName
  pb_put_uint(&d, 7, 1);                      // protocolVersion
  pb_put_uint(&d, 8, 139);                    // lastSupportedMessageType (an iPhone sends 139)
  pb_put_string(&d, 19, config.airplay_psi);  // deviceUID
  if (d.overflow)
    return;
  debug(2, "Connection %d (\"%s\"): MRP: queueing a DEVICE_INFO reply.", conn->connection_number,
        mrp_peer_name(conn));
  queue_message(conn, MRP_TYPE_DEVICE_INFO, ident, ident_length, MRP_EXT_DEVICE_INFO, d.buf,
                d.length);
}

static void queue_artwork_reply(rtsp_conn_info *conn, const uint8_t *ident, size_t ident_len);

// our present volume as a number from 0.0 to 1.0
static float current_unit_volume(void) {
  return (float)airplayVolumeToUnitVolume(config.airplay_volume);
}

#ifdef CONFIG_DACP_CLIENT
// The remote control code can take a while (a DACP exchange with the source, or a timeout if it
// has gone), and it must not hold up the connection thread that calls us. So the work is done by
// one worker thread. Commands (play, pause, next...) are queued and done in the order received.
// Volume changes from a controller keep only the latest value: dragging a slider sends a burst
// of them, and the positions the slider has already passed don't matter.
#define MRP_COMMAND_QUEUE 16
static pthread_mutex_t worker_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t worker_cond = PTHREAD_COND_INITIALIZER;
static simple_command_t command_queue[MRP_COMMAND_QUEUE];
static int command_head = 0, command_count = 0;
static int pending_percent_volume = -1; // -1 means none
static pthread_once_t worker_once = PTHREAD_ONCE_INIT;

static void *remote_worker(__attribute__((unused)) void *arg) {
  for (;;) {
    pthread_mutex_lock(&worker_mutex);
    while (command_count == 0 && pending_percent_volume < 0)
      pthread_cond_wait(&worker_cond, &worker_mutex);
    int have_command = command_count > 0;
    simple_command_t command = rcsc_not_a_command;
    int percent = -1;
    if (have_command) {
      command = command_queue[command_head];
      command_head = (command_head + 1) % MRP_COMMAND_QUEUE;
      command_count--;
    } else {
      percent = pending_percent_volume;
      pending_percent_volume = -1;
    }
    pthread_mutex_unlock(&worker_mutex);
    if (have_command)
      remote_simple_command(command);
    else
      remote_set_integer_percent_volume(percent);
  }
  return NULL;
}

static void start_remote_worker(void) {
  pthread_t t;
  if (pthread_create(&t, NULL, remote_worker, NULL) == 0)
    pthread_detach(t);
}
#endif

// A controller wants to set the volume (SET_VOLUME). Hand it to the worker.
static void set_volume_from_controller(float unit) {
#ifdef CONFIG_DACP_CLIENT
  if (!(unit >= 0.0f))
    unit = 0.0f;
  if (unit > 1.0f)
    unit = 1.0f;
  pthread_once(&worker_once, start_remote_worker);
  pthread_mutex_lock(&worker_mutex);
  pending_percent_volume = (int)(unit * 100.0f + 0.5f);
  pthread_cond_signal(&worker_cond);
  pthread_mutex_unlock(&worker_mutex);
#else
  (void)unit;
#endif
}

// Pass a play, pause, next or previous command on to the source, using the remote control code
// (which uses DACP or AirPlay 2, whichever is available). Returns 1 if it was queued.
static int pass_on_command(uint64_t command) {
#ifdef CONFIG_DACP_CLIENT
  simple_command_t c;
  switch (command) {
  case MRP_CMD_PLAY: c = rcsc_play; break;
  case MRP_CMD_PAUSE: c = rcsc_pause; break;
  case MRP_CMD_TOGGLE_PLAY_PAUSE: c = rcsc_play_pause; break;
  case MRP_CMD_STOP: c = rcsc_stop; break;
  case MRP_CMD_NEXT_TRACK: c = rcsc_next_item; break;
  case MRP_CMD_PREVIOUS_TRACK: c = rcsc_previous_item; break;
  default: return 0;
  }
  pthread_once(&worker_once, start_remote_worker);
  int queued = 0;
  pthread_mutex_lock(&worker_mutex);
  if (command_count < MRP_COMMAND_QUEUE) {
    command_queue[(command_head + command_count) % MRP_COMMAND_QUEUE] = c;
    command_count++;
    queued = 1;
    pthread_cond_signal(&worker_cond);
  }
  pthread_mutex_unlock(&worker_mutex);
  return queued;
#else
  (void)command;
  return 0;
#endif
}

// SEND_COMMAND: the controller wants us to play, pause, skip... Pass the command on to the source
// if we can, and reply with a SEND_COMMAND_RESULT.
static void handle_send_command(rtsp_conn_info *conn, const uint8_t *m, size_t ml,
                                const uint8_t *ident, size_t ident_len) {
  // SendCommandMessage: command = field 1
  uint64_t command = 0;
  pb_field ext, f;
  if (pb_find(m, ml, MRP_EXT_SEND_COMMAND, 2, &ext) && pb_find(ext.data, ext.length, 1, 0, &f))
    command = f.value;
  debug(2, "Connection %d (\"%s\"): MRP: command %llu requested.", conn->connection_number,
        mrp_peer_name(conn), (unsigned long long)command);
  int passed_on = pass_on_command(command);
  if (passed_on)
    debug(2, "Connection %d (\"%s\"): MRP: command %llu passed on to the remote control code.",
          conn->connection_number, mrp_peer_name(conn), (unsigned long long)command);
  // SendCommandResultMessage: sendError (1), handlerReturnStatus (2)
  uint8_t result[16];
  pb_writer rw;
  pb_writer_init(&rw, result, sizeof(result));
  if (command == MRP_CMD_PREPARE_FOR_SET_QUEUE || passed_on) {
    pb_put_uint(&rw, 1, 0); // NoError
    pb_put_uint(&rw, 2, 0); // Success
  } else {
    pb_put_uint(&rw, 1, MRP_SEND_ERROR_NOT_SUPPORTED);
  }
  queue_message(conn, MRP_TYPE_SEND_COMMAND_RESULT, ident, ident_len, MRP_EXT_SEND_COMMAND_RESULT,
                rw.buf, rw.length);
}

// GET_VOLUME: reply with a GET_VOLUME_RESULT, which has the volume (1), a float.
static void handle_get_volume(rtsp_conn_info *conn, const uint8_t *ident, size_t ident_len) {
  uint8_t result[16];
  pb_writer rw;
  pb_writer_init(&rw, result, sizeof(result));
  pb_put_float(&rw, 1, current_unit_volume());
  queue_message(conn, MRP_TYPE_GET_VOLUME_RESULT, ident, ident_len, MRP_EXT_GET_VOLUME_RESULT,
                rw.buf, rw.length);
}

// SET_VOLUME: SetVolumeMessage has the volume (1), a float.
static void handle_set_volume(rtsp_conn_info *conn, const uint8_t *m, size_t ml,
                              const uint8_t *ident, size_t ident_len) {
  pb_field ext, f;
  if (pb_find(m, ml, MRP_EXT_SET_VOLUME, 2, &ext) && pb_find(ext.data, ext.length, 1, 5, &f)) {
    uint32_t raw = (uint32_t)f.value;
    float volume;
    memcpy(&volume, &raw, sizeof(volume));
    debug(2, "Connection %d (\"%s\"): MRP: volume %.3f requested.", conn->connection_number,
          mrp_peer_name(conn), (double)volume);
    set_volume_from_controller(volume);
  }
  // acknowledge with an empty message carrying the request's identifier, in case the controller
  // waits for a reply before it sends the next volume
  if (ident != NULL)
    queue_message(conn, 0, ident, ident_len, 0, NULL, 0);
}

static void handle_data(rtsp_conn_info *conn, const uint8_t *buf, size_t len) {
  size_t pos = 0;
  int count = 0;
  while (pos < len) {
    uint64_t l;
    if (pb_read_varint(buf, len, &pos, &l) != 0 || l > len - pos || l > MRP_MAX_MESSAGE) {
      debug(2, "Connection %d (\"%s\"): MRP: bad message length framing.", conn->connection_number,
            mrp_peer_name(conn));
      return;
    }
    if (++count > MRP_MAX_MESSAGES_PER_POST) {
      debug(2, "Connection %d (\"%s\"): MRP: too many messages in one POST.",
            conn->connection_number, mrp_peer_name(conn));
      return;
    }
    const uint8_t *m = buf + pos;
    size_t ml = (size_t)l;
    pos += ml;

    uint64_t type = 0;
    const uint8_t *ident = NULL;
    size_t ident_len = 0;
    size_t p = 0;
    pb_field f;
    int r;
    while ((r = pb_next(m, ml, &p, &f)) == 1) {
      if (f.field == 1 && f.wire == 0)
        type = f.value;
      else if (f.field == 2 && f.wire == 2) {
        ident = f.data;
        ident_len = f.length;
      }
    }
    debug(2, "Connection %d (\"%s\"): MRP: received %s (type %llu), %zu bytes.",
          conn->connection_number, mrp_peer_name(conn), mrp_type_name(type),
          (unsigned long long)type, ml);
    if (r < 0) {
      debug(2, "Connection %d (\"%s\"): MRP: malformed message.", conn->connection_number,
            mrp_peer_name(conn));
      pb_dump(3, "MRP <- ", m, ml);
      continue;
    }
    pb_dump(3, "MRP <- ", m, ml);

    switch (type) {
    case MRP_TYPE_CLIENT_UPDATES_CONFIG:
      // start sending updates; the reply (an empty message) is not needed
      conn->mrp_subscribe_pending = 1;
      break;
    case MRP_TYPE_SEND_COMMAND:
      handle_send_command(conn, m, ml, ident, ident_len);
      break;
    case MRP_TYPE_GET_VOLUME:
      handle_get_volume(conn, ident, ident_len);
      break;
    case MRP_TYPE_SET_VOLUME:
      handle_set_volume(conn, m, ml, ident, ident_len);
      break;
    case MRP_TYPE_PLAYBACK_QUEUE_REQUEST:
      queue_artwork_reply(conn, ident, ident_len);
      break;
    case MRP_TYPE_DEVICE_INFO:
      queue_device_info_reply(conn, ident, ident_len);
      break;
    default:
      break;
    }
  }
}

#if MRP_PASSTHROUGH
static void passthrough_from_controller(rtsp_conn_info *conn, rtsp_message *req);
#endif

// A controller has sent messages, and we answer them ourselves (synthetic state).
__attribute__((unused)) static void synthetic_from_controller(rtsp_conn_info *conn,
                                                              rtsp_message *req) {
  {
    char *stream_header = msg_get_header(req, "X-Apple-StreamID");
    if (stream_header != NULL) {
      conn->mrp_stream_id = atoi(stream_header);
      debug(3, "Connection %d (\"%s\"): MRP: message on stream %d.", conn->connection_number,
            mrp_peer_name(conn), conn->mrp_stream_id);
    }
  }
  plist_t root = NULL;
  plist_from_memory(req->content, req->contentlength, &root);
  if (root == NULL || plist_get_node_type(root) != PLIST_DICT) {
    debug(2, "Connection %d (\"%s\"): MRP: POST /command body is not a plist dictionary.",
          conn->connection_number, mrp_peer_name(conn));
    if (root)
      plist_free(root);
    return;
  }
  plist_t params = plist_dict_get_item(root, "params");
  plist_t data = params ? plist_dict_get_item(params, "data") : NULL;
  if (data != NULL && plist_get_node_type(data) == PLIST_DATA) {
    uint64_t length = 0;
    char *bytes = NULL;
    plist_get_data_val(data, &bytes, &length);
    if (bytes != NULL && length > 0)
      handle_data(conn, (const uint8_t *)bytes, (size_t)length);
    free(bytes);
  } else {
    debug(2, "Connection %d (\"%s\"): MRP: POST /command has no params/data.",
          conn->connection_number, mrp_peer_name(conn));
  }
  plist_free(root);
}

void mrp_relay_handle_command(rtsp_conn_info *conn, rtsp_message *req) {
  if (req->content == NULL || req->contentlength <= 0)
    return; // the controller's empty keepalive POSTs
#if MRP_PASSTHROUGH
  passthrough_from_controller(conn, req);
#else
  synthetic_from_controller(conn, req);
#endif
}

// ---- pushing now-playing state to subscribed controllers (synthetic state) ----

#define MRP_MAX_RELAYS 16
#define MRP_STR 256
#define MRP_PATH 512

typedef struct {
  rtsp_conn_info *conn;            // NULL if the slot is free
  int identity_sent;               // the now-playing client and player messages have been sent
  float volume_sent;               // the volume we last told this controller about, or -1
  int was_source;                  // this controller's device has been the source of the audio
  uint32_t art_key;                // identifies the artwork we last announced to it, 0 if none
  int stream_id;                   // the stream (data channel) of the controller this entry is for
} relay_entry;

static relay_entry relays[MRP_MAX_RELAYS];
static pthread_mutex_t relays_mutex = PTHREAD_MUTEX_INITIALIZER;

// Seconds since 2001-01-01 (Apple's reference date), with sub-second resolution.
static double apple_time_now(void) {
  struct timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9 - 978307200.0;
}

typedef struct {
  char title[MRP_STR], artist[MRP_STR], album[MRP_STR];
  char source_ip[INET6_ADDRSTRLEN]; // the device that is playing to us, if any
  char cover_path[MRP_PATH];        // the cover art file, if there is one
  int state; // 1 playing, 2 paused, 3 stopped
  double airplay_volume; // dB; here only to prompt a push when the volume changes
  char artwork_id[MRP_PATH]; // the sender's artworkIdentifier, if any (a URL template)
  // Progress. duration and rate come from the hub. The trig_ fields are there only so that a
  // new position reference from the sender (a new track, a seek, a resume) changes the snapshot
  // and prompts a push; the position itself is worked out when we push, in the pusher thread,
  // because it moves continuously. elapsed was true at snap_time (Apple absolute time); the
  // controller extrapolates from there using the rate. elapsed and snap_time are set only in the
  // pusher thread, on its own copy.
  double duration;        // seconds, or 0 if unknown
  double rate;            // 1.0 while playing, 0.0 otherwise
  uint64_t trig_elapsed;  // elapsedTimeOnReceiptOfNowPlayingInfo, ns
  uint32_t trig_prog[3];  // progress string timestamps: first, current, last
  double elapsed;         // seconds
  double snap_time;       // when elapsed was true
} np_snapshot;

#ifdef CONFIG_METADATA_HUB
// This mirrors the position estimate used for the MPRIS Position property
// (property-preflight-mpris.c), which has two sources: the progress string (older senders, or
// AirPlay 2 with plists disabled) and the AirPlay 2 now-playing plist. Returns the position in
// seconds, or -1 if unknown. *duration is set from the progress string if that is the source.
static double estimate_position(double *duration) {
  double result = -1.0;
  *duration = 0.0;
  pthread_rwlock_rdlock(&principal_conn_lock); // don't let the principal_conn be changed
  pthread_cleanup_push(rwlock_unlock, (void *)&principal_conn_lock);
  if (principal_conn != NULL) {
    int using_progress_string = 1;
#ifdef CONFIG_AIRPLAY_2
    if ((principal_conn->airplay_type == ap_2) &&
        ((config.airplay_features & ((uint64_t)1 << 50)) != 0))
      using_progress_string = 0;
#endif
    if (using_progress_string != 0) {
      if ((principal_conn->input_rate != 0) && (metadata_store.npi.playing_state != 2) &&
          (metadata_store.progress_string != NULL)) {
        int32_t frames_total =
            metadata_store.progress_last_timestamp - metadata_store.progress_first_timestamp;
        int32_t frames_played =
            metadata_store.head_rtp_timestamp - metadata_store.progress_first_timestamp;
        int32_t frames_remaining =
            metadata_store.progress_last_timestamp - metadata_store.head_rtp_timestamp;
        if ((frames_total >= 0) && (frames_played >= 0) && (frames_remaining >= 0)) {
          result = (double)frames_played / principal_conn->input_rate;
          *duration = (double)frames_total / principal_conn->input_rate;
        }
      }
    }
#ifdef CONFIG_AIRPLAY_2
    else if (metadata_store.npi.elapsedTimeOnReceiptOfNowPlayingInfo.valid != 0) {
      double position_ns = (double)metadata_store.npi.elapsedTimeOnReceiptOfNowPlayingInfo.value;
      if (metadata_store.npi.playbackRate > 0.0) {
        unsigned int output_rate =
            (unsigned int)RATE_FROM_ENCODED_FORMAT(config.current_output_configuration);
        if (output_rate != 0) {
          uint64_t played = principal_conn->frames_played -
                            metadata_store.npi.framesPlayedOnReceiptOfNowPlayingInfo;
          position_ns += (double)played * 1e9 / output_rate;
        }
      }
      result = position_ns / 1e9;
    }
#endif
  }
  pthread_cleanup_pop(1);
  return result;
}
#endif

static pthread_mutex_t snap_mutex = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t snap_cond = PTHREAD_COND_INITIALIZER;
static np_snapshot snapshot = {.state = 3}; // stopped, until we hear otherwise
static unsigned long wake_count = 0;
static pthread_once_t pusher_once = PTHREAD_ONCE_INIT;

static void wake_pusher(void) {
  pthread_mutex_lock(&snap_mutex);
  wake_count++;
  pthread_cond_signal(&snap_cond);
  pthread_mutex_unlock(&snap_mutex);
}

// the client part of a player path or client message
static void put_client(pb_writer *cw) {
  pb_put_uint(cw, 1, 44);  // processIdentifier (a HomePod's)
  pb_put_uint(cw, 4, 501); // processUserIdentifier
  pb_put_string(cw, 2, MRP_CLIENT_BUNDLE_ID);
  pb_put_string(cw, 7, MRP_CLIENT_DISPLAY_NAME);
}

static size_t build_player_path(uint8_t *out, size_t cap) {
  // PlayerPath { origin (1), client (2), player (3) }. The details are those a HomePod sends;
  // the iPad crashes when it is shown artwork without them.
  uint8_t c[128], p[128];
  pb_writer cw, pw, o;
  pb_writer_init(&cw, c, sizeof(c));
  put_client(&cw);
  pb_writer_init(&pw, p, sizeof(p));
  pb_put_string(&pw, 1, "MediaRemote-DefaultPlayer");
  pb_put_string(&pw, 2, "Default");
  pb_put_uint(&pw, 6, 479292); // audioSessionID
  pb_writer_init(&o, out, cap);
  {
    uint8_t og[160];
    pb_writer ow;
    pb_writer_init(&ow, og, sizeof(og));
    pb_put_string(&ow, 2, config.service_name ? config.service_name : "Shairport Sync");
    pb_put_uint(&ow, 3, 1280262988);
    pb_put_bool(&ow, 5, 1); // isLocallyHosted
    pb_put_bytes(&o, 1, ow.buf, ow.length);
  }
  pb_put_bytes(&o, 2, cw.buf, cw.length);
  pb_put_bytes(&o, 3, pw.buf, pw.length);
  return o.overflow ? 0 : o.length;
}

// A 32-bit FNV-1a hash of some strings, each taken with a terminator so that the way the text is
// split between them matters.
static uint32_t hash_strings(const char *const *strings, int count) {
  uint32_t h = 2166136261u;
  for (int i = 0; i < count; i++) {
    for (const char *c = strings[i]; *c; c++) {
      h ^= (uint8_t)*c;
      h *= 16777619u;
    }
    h ^= 0xff;
    h *= 16777619u;
  }
  return h;
}

// A short identifier for the current track, so that a new track is a new content item.
static void track_identifier(const np_snapshot *s, char *out, size_t cap) {
  const char *parts[3] = {s->title, s->artist, s->album};
  snprintf(out, cap, "%08x", hash_strings(parts, 3));
}

// A key that changes when the track or its artwork does; 0 if there is no artwork.
static uint32_t artwork_key(const np_snapshot *s) {
  if (s->state == 3 || (s->artwork_id[0] == '\0' && s->cover_path[0] == '\0'))
    return 0;
  const char *parts[5] = {s->title, s->artist, s->album, s->cover_path, s->artwork_id};
  uint32_t h = hash_strings(parts, 5);
  return h != 0 ? h : 1;
}

// Find the pixel dimensions of a JPEG or PNG. Returns 0 on success.
static int image_dimensions(const uint8_t *d, size_t n, uint32_t *w, uint32_t *h) {
  if (n > 24 && d[0] == 0x89 && d[1] == 'P' && d[2] == 'N' && d[3] == 'G') {
    *w = ((uint32_t)d[16] << 24) | ((uint32_t)d[17] << 16) | ((uint32_t)d[18] << 8) | d[19];
    *h = ((uint32_t)d[20] << 24) | ((uint32_t)d[21] << 16) | ((uint32_t)d[22] << 8) | d[23];
    return 0;
  }
  if (n > 4 && d[0] == 0xff && d[1] == 0xd8) {
    size_t p = 2;
    while (p + 9 < n) {
      if (d[p] != 0xff) {
        p++;
        continue;
      }
      uint8_t m = d[p + 1];
      if (m == 0xff) {
        p++;
        continue;
      }
      if (m == 0xd8 || m == 0x01 || (m >= 0xd0 && m <= 0xd7)) {
        p += 2;
        continue;
      }
      size_t seglen = ((size_t)d[p + 2] << 8) | d[p + 3];
      if ((m >= 0xc0 && m <= 0xcf) && m != 0xc4 && m != 0xc8 && m != 0xcc) { // start of frame
        *h = ((uint32_t)d[p + 5] << 8) | d[p + 6];
        *w = ((uint32_t)d[p + 7] << 8) | d[p + 8];
        return 0;
      }
      p += 2 + seglen;
    }
  }
  return -1;
}

// Builds the artworkURLTemplatesData blob the way a real HomePod sends it: an NSKeyedArchiver
// archive of an NSArray holding one NSDictionary describing the artwork (size, colours and the
// URL template). The colours are Apple Music's palette for the cover, which we don't have,
// so fixed neutral ones are used. Returns a malloc'd binary plist, or NULL.
static char *build_artwork_templates(const char *url_template, uint32_t size, uint32_t *length) {
  // $objects: 0 "$null", 1 array, 2 dictionary, 3..13 keys, 14.. values, then the two classes.
  static const char *keys[] = {"height",    "hasAlpha", "textColor3", "supportsLayeredImage",
                               "textColor2", "textColor1", "width",   "textColor4",
                               "bgColor",    "hasP3",    "url"};
  // indexes into the values: 0 size, 1 false, 2.. strings
  enum { V_SIZE = 14, V_FALSE, V_TC3, V_TC2, V_TC1, V_TC4, V_BG, V_URL, C_DICT, C_ARRAY };
  static const int key_value[] = {V_SIZE, V_FALSE, V_TC3, V_FALSE, V_TC2, V_TC1,
                                  V_SIZE, V_TC4,   V_BG,  V_FALSE, V_URL};
  plist_t root = plist_new_dict();
  plist_dict_set_item(root, "$archiver", plist_new_string("NSKeyedArchiver"));
  plist_dict_set_item(root, "$version", plist_new_uint(100000));
  plist_t top = plist_new_dict();
  plist_dict_set_item(top, "root", plist_new_uid(1));
  plist_dict_set_item(root, "$top", top);
  plist_t objs = plist_new_array();
  plist_array_append_item(objs, plist_new_string("$null"));
  // 1: the NSArray
  plist_t arr = plist_new_dict();
  plist_t arr_objs = plist_new_array();
  plist_array_append_item(arr_objs, plist_new_uid(2));
  plist_dict_set_item(arr, "NS.objects", arr_objs);
  plist_dict_set_item(arr, "$class", plist_new_uid(C_ARRAY));
  plist_array_append_item(objs, arr);
  // 2: the NSDictionary
  plist_t d = plist_new_dict();
  plist_t dk = plist_new_array(), dv = plist_new_array();
  for (int i = 0; i < 11; i++) {
    plist_array_append_item(dk, plist_new_uid(3 + i));
    plist_array_append_item(dv, plist_new_uid(key_value[i]));
  }
  plist_dict_set_item(d, "NS.keys", dk);
  plist_dict_set_item(d, "NS.objects", dv);
  plist_dict_set_item(d, "$class", plist_new_uid(C_DICT));
  plist_array_append_item(objs, d);
  for (int i = 0; i < 11; i++)
    plist_array_append_item(objs, plist_new_string(keys[i]));
  plist_array_append_item(objs, plist_new_uint(size));    // V_SIZE
  plist_array_append_item(objs, plist_new_bool(0));       // V_FALSE
  plist_array_append_item(objs, plist_new_string("444444")); // textColor3
  plist_array_append_item(objs, plist_new_string("222222")); // textColor2
  plist_array_append_item(objs, plist_new_string("000000")); // textColor1
  plist_array_append_item(objs, plist_new_string("666666")); // textColor4
  plist_array_append_item(objs, plist_new_string("d0d0d0")); // bgColor
  plist_array_append_item(objs, plist_new_string(url_template));
  plist_t cd = plist_new_dict(), cdc = plist_new_array();
  plist_array_append_item(cdc, plist_new_string("NSDictionary"));
  plist_array_append_item(cdc, plist_new_string("NSObject"));
  plist_dict_set_item(cd, "$classes", cdc);
  plist_dict_set_item(cd, "$classname", plist_new_string("NSDictionary"));
  plist_array_append_item(objs, cd);
  plist_t ca = plist_new_dict(), cac = plist_new_array();
  plist_array_append_item(cac, plist_new_string("NSArray"));
  plist_array_append_item(cac, plist_new_string("NSObject"));
  plist_dict_set_item(ca, "$classes", cac);
  plist_dict_set_item(ca, "$classname", plist_new_string("NSArray"));
  plist_array_append_item(objs, ca);
  plist_dict_set_item(root, "$objects", objs);
  char *bin = NULL;
  uint32_t l = 0;
  plist_to_bin(root, &bin, &l);
  plist_free(root);
  *length = l;
  return bin;
}

// When nothing is playing (the source has stopped or gone, or nothing has played yet -- the hub
// keeps the last track's details), the controllers do not clear their pane if the queue goes
// away, but they do replace the item. So make the snapshot into an item that is a message, with
// no progress. The previous track's artwork is left as it is, so it stays visible.
static void make_idle_snapshot(np_snapshot *s) {
  if (s->state != 3)
    return;
  snprintf(s->title, sizeof(s->title), "%s", MRP_NOT_CONNECTED);
  s->artist[0] = '\0'; // the player's name is shown anyway
  s->album[0] = '\0';
  s->duration = 0.0;
}

// "image/png" if the cover file is a PNG, otherwise "image/jpeg".
static const char *cover_mime_type(const char *path) {
  unsigned char h[4] = {0, 0, 0, 0};
  FILE *f = fopen(path, "rb");
  if (f != NULL) {
    if (fread(h, 1, 4, f) != 4)
      h[1] = 0;
    fclose(f);
  }
  return (h[0] == 0x89 && h[1] == 'P' && h[2] == 'N') ? "image/png" : "image/jpeg";
}

// The metadata of the current track. If there is artwork, say so, and what sort it is.
static void put_metadata(pb_writer *mw, const np_snapshot *s) {
  pb_put_string(mw, 1, s->title);
  pb_put_string(mw, 6, s->album);
  pb_put_string(mw, 7, s->artist);
  if (s->artwork_id[0] != '\0') {
    // Fill in the template: {w} and {h} with the size, {f} with the format.
    char url[MRP_PATH * 2];
    size_t u = 0;
    const char *p = s->artwork_id;
    while (*p != '\0' && u + 16 < sizeof(url)) {
      const char *rep_str = NULL;
      if (strncmp(p, "{w}", 3) == 0 || strncmp(p, "{h}", 3) == 0)
        rep_str = XSTR(MRP_ARTWORK_SIZE);
      else if (strncmp(p, "{f}", 3) == 0)
        rep_str = "jpg";
      if (rep_str != NULL) {
        u += (size_t)snprintf(url + u, sizeof(url) - u, "%s", rep_str);
        p += 3;
      } else {
        url[u++] = *p++;
      }
    }
    url[u] = '\0';
    pb_put_bool(mw, 19, 1);               // artworkAvailable
    pb_put_string(mw, 70, url);           // artworkURL
    pb_put_string(mw, 80, s->artwork_id); // artworkIdentifier
    {
      uint32_t blob_length = 0;
      char *blob = build_artwork_templates(s->artwork_id, MRP_ARTWORK_SIZE, &blob_length);
      if (blob != NULL) {
        pb_put_bytes(mw, 82, blob, blob_length); // artworkURLTemplatesData
        free(blob);
      }
    }
  } else if (MRP_LOCAL_ARTWORK_FALLBACK && s->cover_path[0] != '\0' && s->state != 3) {
    // no URL from the sender, but we have its picture: the controller can ask us for it
    // A HomePod playing a source like this sends artworkAvailable, artworkMIMEType and a short
    // hexadecimal artworkIdentifier, and no URL. Do the same.
    char local_id[32];
    uint32_t k = artwork_key(s);
    snprintf(local_id, sizeof(local_id), "%08x%08x", (unsigned)k, (unsigned)(k * 2654435761u));
    pb_put_bool(mw, 19, 1);          // artworkAvailable
    pb_put_string(mw, 31, cover_mime_type(s->cover_path)); // artworkMIMEType
    pb_put_string(mw, 80, local_id); // artworkIdentifier
  }
  if (s->duration > 0.0) {
    pb_put_double(mw, 14, s->duration);   // duration
    pb_put_double(mw, 35, s->elapsed);    // elapsedTime
    pb_put_float(mw, 39, (float)s->rate); // playbackRate
    pb_put_double(mw, 74, s->snap_time);  // elapsedTimeTimestamp
  }
}

// The metadata that goes with the picture itself, when we send it (see queue_artwork_reply).
static void put_picture_metadata(pb_writer *mw, const uint8_t *art, size_t art_length) {
  uint32_t w = 0, h = 0;
  if (image_dimensions(art, art_length, &w, &h) != 0)
    w = h = 0;
  pb_put_string(mw, 31, art_length > 4 && art[1] == 'P' && art[2] == 'N' ? "image/png"
                                                                         : "image/jpeg");
  if (w > 0 && h > 0) {
    pb_put_uint(mw, 77, w); // artworkDataWidth
    pb_put_uint(mw, 78, h); // artworkDataHeight
  }
}

// A HomePod's default player: the system's own client and player, shown before any app's.
// PlayerPath { origin (1), client (2), player (3) }
static size_t build_default_player_path(uint8_t *out, size_t cap, int locally_hosted) {
  uint8_t c[96], p[96], og[160];
  pb_writer cw, pw, ow, o;
  pb_writer_init(&cw, c, sizeof(c));
  pb_put_uint(&cw, 1, 80);  // processIdentifier
  pb_put_uint(&cw, 4, 501); // processUserIdentifier
  pb_put_string(&cw, 2, "com.apple.tvairplayd");
  pb_writer_init(&pw, p, sizeof(p));
  pb_put_string(&pw, 1, "MediaRemote-DefaultPlayer");
  pb_put_string(&pw, 2, "Default Player");
  pb_put_uint(&pw, 6, 0); // audioSessionID
  pb_writer_init(&ow, og, sizeof(og));
  pb_put_string(&ow, 2, config.service_name ? config.service_name : "Shairport Sync");
  pb_put_uint(&ow, 3, 1280262988);
  if (locally_hosted)
    pb_put_bool(&ow, 5, 1); // isLocallyHosted
  pb_writer_init(&o, out, cap);
  pb_put_bytes(&o, 1, ow.buf, ow.length);
  pb_put_bytes(&o, 2, cw.buf, cw.length);
  pb_put_bytes(&o, 3, pw.buf, pw.length);
  return o.overflow ? 0 : o.length;
}

// SET_STATE (state Unknown) and PLAYER_CLIENT_PROPERTIES for the default player
static void append_default_player(pb_writer *o) {
  uint8_t path[300], pay[400];
  pb_writer pw;
  size_t pl = build_default_player_path(path, sizeof(path), 0);
  if (pl == 0)
    return;
  // SET_STATE: playbackState (6), playbackQueueCapabilities (8, empty), playerPath (9),
  // playbackStateTimestamp (11)
  pb_writer_init(&pw, pay, sizeof(pay));
  pb_put_uint(&pw, 6, MRP_PLAYBACK_STATE_UNKNOWN);
  pb_put_bytes(&pw, 8, pay, 0);
  pb_put_bytes(&pw, 9, path, pl);
  pb_put_double(&pw, 11, apple_time_now());
  append_message(o, MRP_TYPE_SET_STATE, MRP_EXT_SET_STATE, pw.buf, pw.length);
  // PLAYER_CLIENT_PROPERTIES: playerPath (1) with a locally hosted origin, lastPlayingTimestamp (2)
  pl = build_default_player_path(path, sizeof(path), 1);
  if (pl == 0)
    return;
  pb_writer_init(&pw, pay, sizeof(pay));
  pb_put_bytes(&pw, 1, path, pl);
  pb_put_double(&pw, 2, -63114076800.0); // "never played"
  append_message(o, MRP_TYPE_PLAYER_CLIENT_PROPERTIES, MRP_EXT_PLAYER_CLIENT_PROPERTIES, pw.buf,
                 pw.length);
}

// VOLUME_CONTROL_AVAILABILITY: volumeControlAvailable (1), volumeCapabilities (2)
static void append_volume_availability(pb_writer *o) {
  uint8_t pay[16];
  pb_writer pw;
  pb_writer_init(&pw, pay, sizeof(pay));
  pb_put_bool(&pw, 1, 1);
  pb_put_uint(&pw, 2, MRP_VOLUME_CAPABILITIES_ABSOLUTE);
  append_message(o, MRP_TYPE_VOLUME_CONTROL_AVAILABILITY, MRP_EXT_VOLUME_CONTROL_AVAILABILITY,
                 pw.buf, pw.length);
}

// VOLUME_DID_CHANGE: volume (1), endpointUID (2), outputDeviceUID (3, the psi, which is the uid
// the Mac knows us by)
static void append_volume_did_change(pb_writer *o, float volume) {
  uint8_t pay[128];
  pb_writer pw;
  pb_writer_init(&pw, pay, sizeof(pay));
  pb_put_float(&pw, 1, volume);
  pb_put_string(&pw, 2, config.airplay_pi);
  pb_put_string(&pw, 3, config.airplay_psi);
  append_message(o, MRP_TYPE_VOLUME_DID_CHANGE, MRP_EXT_VOLUME_DID_CHANGE, pw.buf, pw.length);
}

// The messages that describe the present state to a controller: with include_identity, the ones
// a receiver sends when a controller subscribes, and always the SET_STATE with the track.
// Returns a malloc'd buffer holding the framed messages, or NULL.
static uint8_t *build_state_messages(const np_snapshot *s, int include_identity,
                                     size_t *result_length) {
  size_t cap = 8192;
  uint8_t *out = malloc(cap);
  uint8_t *scratch = malloc(3 * cap); // for the nested messages
  if (out == NULL || scratch == NULL) {
    free(out);
    free(scratch);
    return NULL;
  }
  pb_writer o;
  pb_writer_init(&o, out, cap);
  uint8_t path[300];
  size_t pl = build_player_path(path, sizeof(path));
  if (pl == 0) {
    free(out);
    free(scratch);
    return NULL;
  }

  // The messages a receiver sends when a controller subscribes. (SET_DEFAULT_SUPPORTED_COMMANDS
  // is not among them: it hides our device from the iPad's list of speakers.)
  if (include_identity) {
    uint8_t pay[200], cl[128];
    pb_writer pw, cw;
    // ORIGIN_CLIENT_PROPERTIES: lastPlayingTimestamp (1)
    pb_writer_init(&pw, pay, sizeof(pay));
    pb_put_double(&pw, 1, apple_time_now());
    append_message(&o, MRP_TYPE_ORIGIN_CLIENT_PROPERTIES, MRP_EXT_ORIGIN_CLIENT_PROPERTIES,
                   pw.buf, pw.length);
    // UPDATE_CLIENT: client (1)
    pb_writer_init(&cw, cl, sizeof(cl));
    put_client(&cw);
    pb_writer_init(&pw, pay, sizeof(pay));
    pb_put_bytes(&pw, 1, cw.buf, cw.length);
    append_message(&o, MRP_TYPE_UPDATE_CLIENT, MRP_EXT_UPDATE_CLIENT, pw.buf, pw.length);
  }
  // SET_NOW_PLAYING_CLIENT: client = field 1
  if (include_identity) {
    uint8_t c[128], pay[160];
    pb_writer cw, pw;
    pb_writer_init(&cw, c, sizeof(c));
    put_client(&cw);
    pb_writer_init(&pw, pay, sizeof(pay));
    pb_put_bytes(&pw, 1, cw.buf, cw.length);
    append_message(&o, MRP_TYPE_SET_NOW_PLAYING_CLIENT, MRP_EXT_SET_NOW_PLAYING_CLIENT, pw.buf,
                   pw.length);
  }
  if (include_identity && MRP_SEND_DEFAULT_PLAYER)
    append_default_player(&o);
  // UPDATE_OUTPUT_DEVICE: outputDevices (1) = AVOutputDeviceDescriptor, endpointUID (2).
  // Descriptor: name 1, uniqueIdentifier 2 (psi), groupID 3 (pi), modelID 4, isGroupLeader 8,
  // isGroupable 9, canRelayCommunicationChannel 20, volume 24, isVolumeControlAvailable 25,
  // volumeCapabilities 37.
  if (include_identity) {
    uint8_t dd[300], pay[360];
    pb_writer dw, pw;
    pb_writer_init(&dw, dd, sizeof(dd));
    pb_put_string(&dw, 1, config.service_name ? config.service_name : "Shairport Sync");
    pb_put_string(&dw, 2, config.airplay_psi);
    pb_put_string(&dw, 3, config.airplay_pi);
    pb_put_string(&dw, 4, "ShairportSync");
    pb_put_bool(&dw, 8, 1);
    pb_put_bool(&dw, 9, 1);
    pb_put_bool(&dw, 20, 1);
    pb_put_float(&dw, 24, (float)current_unit_volume());
    pb_put_bool(&dw, 25, 1);
    pb_put_uint(&dw, 37, MRP_VOLUME_CAPABILITIES_ABSOLUTE);
    pb_writer_init(&pw, pay, sizeof(pay));
    pb_put_bytes(&pw, 1, dw.buf, dw.length);
    pb_put_string(&pw, 2, config.airplay_pi);
    append_message(&o, MRP_TYPE_UPDATE_OUTPUT_DEVICE, MRP_EXT_UPDATE_OUTPUT_DEVICE, pw.buf,
                   pw.length);
  }
  if (include_identity && MRP_SEND_VOLUME_STATE)
    append_volume_availability(&o);
  // Volume control: VOLUME_CONTROL_CAPABILITIES_DID_CHANGE, with capabilities (1) =
  // { volumeControlAvailable (1), volumeCapabilities (2) }, endpointUID (3), outputDeviceUID (4)
  if (include_identity) {
    uint8_t av[16], pay[128];
    pb_writer aw, pw;
    pb_writer_init(&aw, av, sizeof(av));
    pb_put_bool(&aw, 1, 1);
    pb_put_uint(&aw, 2, MRP_VOLUME_CAPABILITIES_ABSOLUTE);
    pb_writer_init(&pw, pay, sizeof(pay));
    pb_put_bytes(&pw, 1, aw.buf, aw.length);
    pb_put_string(&pw, 3, config.airplay_pi);
    pb_put_string(&pw, 4, config.airplay_psi);
    append_message(&o, MRP_TYPE_VOLUME_CONTROL_CAPABILITIES_DID_CHANGE,
                   MRP_EXT_VOLUME_CONTROL_CAPABILITIES_DID_CHANGE, pw.buf, pw.length);
  }
  if (include_identity && MRP_SEND_VOLUME_STATE)
    append_volume_did_change(&o, current_unit_volume());
  // SET_NOW_PLAYING_PLAYER: playerPath = field 1
  if (include_identity) {
    uint8_t pay[400];
    pb_writer pw;
    pb_writer_init(&pw, pay, sizeof(pay));
    pb_put_bytes(&pw, 1, path, pl);
    append_message(&o, MRP_TYPE_SET_NOW_PLAYING_PLAYER, MRP_EXT_SET_NOW_PLAYING_PLAYER, pw.buf,
                   pw.length);
  }
  // SET_STATE: playbackQueue (3), playbackState (6), playerPath (9)
  {
    char ident[16];
    track_identifier(s, ident, sizeof(ident));
    uint8_t md[2048];
    pb_writer mw, iw, qw, pw;
    pb_writer_init(&mw, md, sizeof(md));
    put_metadata(&mw, s);
    pb_writer_init(&iw, scratch, cap);
    pb_put_string(&iw, 1, ident);
    pb_put_bytes(&iw, 2, mw.buf, mw.length);
    pb_writer_init(&qw, scratch + cap, cap);
    pb_put_uint(&qw, 1, 0);
    pb_put_bytes(&qw, 2, iw.buf, iw.length);
    pb_writer_init(&pw, scratch + 2 * cap, cap);
    {
      // SupportedCommands { repeated CommandInfo { command (1), enabled (2) } }
      static const uint8_t cmds[] = {MRP_CMD_PLAY, MRP_CMD_PAUSE, MRP_CMD_TOGGLE_PLAY_PAUSE,
                                     MRP_CMD_NEXT_TRACK, MRP_CMD_PREVIOUS_TRACK};
      uint8_t sc[128];
      pb_writer sw;
      pb_writer_init(&sw, sc, sizeof(sc));
      for (size_t i = 0; i < sizeof(cmds); i++) {
        uint8_t ci[8];
        pb_writer cw;
        pb_writer_init(&cw, ci, sizeof(ci));
        pb_put_uint(&cw, 1, cmds[i]);
        pb_put_bool(&cw, 2, 1);
        pb_put_bytes(&sw, 1, cw.buf, cw.length);
      }
      pb_put_bytes(&pw, 2, sw.buf, sw.length);
    }
    pb_put_bytes(&pw, 3, qw.buf, qw.length);
    {
      uint64_t state = (uint64_t)s->state;
      if (MRP_IDLE_STATE_INTERRUPTED && s->state == 3) // idle: see make_idle_snapshot
        state = MRP_PLAYBACK_STATE_INTERRUPTED;
      pb_put_uint(&pw, 6, state);
    }
    pb_put_bytes(&pw, 9, path, pl);
    if (pw.overflow || qw.overflow || iw.overflow || mw.overflow) {
      free(out);
      free(scratch);
      return NULL;
    }
    append_message(&o, MRP_TYPE_SET_STATE, MRP_EXT_SET_STATE, pw.buf, pw.length);
  }
  if (include_identity) {
    uint8_t pay[400];
    pb_writer pw;
    // PLAYER_CLIENT_PROPERTIES: playerPath (1), lastPlayingTimestamp (2)
    pb_writer_init(&pw, pay, sizeof(pay));
    pb_put_bytes(&pw, 1, path, pl);
    pb_put_double(&pw, 2, apple_time_now());
    append_message(&o, MRP_TYPE_PLAYER_CLIENT_PROPERTIES, MRP_EXT_PLAYER_CLIENT_PROPERTIES,
                   pw.buf, pw.length);
    // UPDATE_PLAYER: playerPath (1)
    pb_writer_init(&pw, pay, sizeof(pay));
    pb_put_bytes(&pw, 1, path, pl);
    append_message(&o, MRP_TYPE_UPDATE_PLAYER, MRP_EXT_UPDATE_PLAYER, pw.buf, pw.length);
  }
  free(scratch);
  if (o.overflow) {
    free(out);
    return NULL;
  }
  *result_length = o.length;
  return out;
}

// UPDATE_CONTENT_ITEM_ARTWORK: contentItems (1) = { identifier (1), metadata (2, empty) },
// playerPath (2). A real HomePod sends this when the artwork of an item changes.
static uint8_t *build_artwork_update_message(const np_snapshot *s, size_t *result_length) {
  uint8_t path[300];
  size_t pl = build_player_path(path, sizeof(path));
  if (pl == 0)
    return NULL;
  char ident[16];
  track_identifier(s, ident, sizeof(ident));
  uint8_t item[64], payload[512];
  pb_writer iw, pw;
  pb_writer_init(&iw, item, sizeof(item));
  pb_put_string(&iw, 1, ident);
  pb_put_bytes(&iw, 2, "", 0);
  pb_writer_init(&pw, payload, sizeof(payload));
  pb_put_bytes(&pw, 1, iw.buf, iw.length);
  pb_put_bytes(&pw, 2, path, pl);
  if (iw.overflow || pw.overflow)
    return NULL;
  return frame_new_message(MRP_TYPE_UPDATE_CONTENT_ITEM_ARTWORK,
                           MRP_EXT_UPDATE_CONTENT_ITEM_ARTWORK, pw.buf, pw.length, result_length);
}

// log each framed message we are about to send: a summary at level 1, the fields at level 2
static void log_outgoing(rtsp_conn_info *conn, const uint8_t *data, size_t length) {
  size_t pos = 0;
  while (pos < length) {
    uint64_t l;
    if (pb_read_varint(data, length, &pos, &l) != 0 || l > length - pos)
      return;
    const uint8_t *m = data + pos;
    size_t ml = (size_t)l;
    pos += ml;
    uint64_t type = 0;
    size_t p = 0;
    pb_field f;
    while (pb_next(m, ml, &p, &f) == 1)
      if (f.field == 1 && f.wire == 0)
        type = f.value;
    debug(2, "Connection %d (\"%s\"): MRP: sending %s (type %llu), %zu bytes.",
          conn->connection_number, mrp_peer_name(conn),
          type == 0 ? "ACKNOWLEDGE" : mrp_type_name(type), (unsigned long long)type, ml);
    if (ml <= 4096) // don't dump artwork
      pb_dump(3, "MRP -> ", m, ml);
  }
}

// Close a connection. Setting its stop flag makes its thread exit and clean up for itself, but the
// thread is usually blocked reading, so shut the socket down too, to wake it.
static void close_connection(rtsp_conn_info *conn) {
  conn->stop = 1;
  if (conn->fd > 0)
    shutdown(conn->fd, SHUT_RDWR);
}

// Send some framed messages to a controller on the event channel, without logging them. If the
// controller does not accept them (a 400, say) it seems to have abandoned the MediaRemote session, so it will not use
// anything we send it: close the whole connection.
static void send_data_event_quietly(rtsp_conn_info *conn, int stream_id, const uint8_t *data,
                                    size_t length) {
  plist_t params = plist_new_dict();
  plist_dict_set_item(params, "data", plist_new_data((const char *)data, length));
  plist_t top = plist_new_dict();
  plist_dict_set_item(top, "params", params);
  ssize_t r = ap2_event_port_post_command_stream(conn, top, stream_id);
  debug(2, "Connection %d (\"%s\"): MRP: sent %zu bytes on the event channel (stream %d), result %zd.",
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

static void send_data_event_on(rtsp_conn_info *conn, int stream_id, const uint8_t *data,
                               size_t length) {
  log_outgoing(conn, data, length);
  send_data_event_quietly(conn, stream_id, data, length);
}

// The stream of the controller's latest message, which is the one that a reply to it goes out on.
static int current_stream(const rtsp_conn_info *conn) {
  return (MRP_DISTINCT_STREAM_IDS && conn->mrp_stream_id > 0) ? conn->mrp_stream_id : 1;
}

// replies go out on the stream of the message they answer
static void send_data_event(rtsp_conn_info *conn, const uint8_t *data, size_t length) {
  send_data_event_on(conn, current_stream(conn), data, length);
}

#if MRP_PASSTHROUGH && MRP_LOG_RELAYED
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
// message. See MRP_LOG_RELAYED.
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
    debug(2, "Connection %d (\"%s\"): MRP: %s: %s (type %llu), %zu bytes, id %.*s%s.",
          conn->connection_number, mrp_peer_name(conn), route,
          type == 0 ? "ACKNOWLEDGE" : mrp_type_name(type), (unsigned long long)type, ml,
          (int)ident_length, ident != NULL ? (const char *)ident : "", detail);
    if (MRP_LOG_RELAYED_FIELDS_LEVEL > 0)
      pb_dump(MRP_LOG_RELAYED_FIELDS_LEVEL, prefix, m, ml);
  }
}
#endif

#if MRP_PASSTHROUGH
// ---- pass-through relay (see MRP_PASSTHROUGH) ----

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
static void passthrough_from_controller(rtsp_conn_info *conn, rtsp_message *req) {
  int controller_stream = 1;
  char *stream_header = msg_get_header(req, "X-Apple-StreamID");
  if (stream_header != NULL && atoi(stream_header) > 0)
    controller_stream = atoi(stream_header);
  conn->mrp_stream_id = controller_stream;

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
    debug(2,
          "Connection %d (\"%s\"): MRP: session for controller stream %d requested from the "
          "source as stream %d, result %zd.",
          conn->connection_number, mrp_peer_name(conn), controller_stream, sender_stream, r);
  }

#if MRP_LOG_RELAYED
  log_relayed(conn, 1, controller_stream, sender_stream, data, length);
#else
  log_outgoing(conn, data, length);
#endif
  plist_t top = pt_data_plist(data, length);
  ssize_t r = pt_post_to_source(top, sender_stream);
  plist_free(top);
  debug(2,
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
#if MRP_LOG_RELAYED
        log_relayed(pt_map[i].conn, 0, pt_map[i].controller_stream, sender_stream, data, length);
        send_data_event_quietly(pt_map[i].conn, pt_map[i].controller_stream, data, length);
#else
        send_data_event_on(pt_map[i].conn, pt_map[i].controller_stream, data, length);
#endif
      }
    pthread_mutex_unlock(&pt_mutex);
    if (!found) {
      debug(2, "Connection %d: MRP: a message from the source for unknown stream %d.",
            conn->connection_number, sender_stream);
#if MRP_LOG_RELAYED
      log_relayed(conn, 0, 0, sender_stream, data, length);
#endif
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

// The source has gone, so its sessions have too: close the controllers' connections.
static void pt_close_all(int closing) {
  pthread_mutex_lock(&pt_mutex);
  for (int i = 0; i < MRP_PT_MAX; i++)
    if (pt_map[i].conn != NULL) {
      rtsp_conn_info *c = pt_map[i].conn;
      debug(2, "Connection %d (\"%s\"): MRP: %s this relay connection (stream %d).",
            c->connection_number, mrp_peer_name(c), closing ? "closing" : "leaving open",
            pt_map[i].controller_stream);
      if (closing && c->stop == 0)
        close_connection(c);
      pt_map[i].conn = NULL;
    }
  pthread_mutex_unlock(&pt_mutex);
}
#endif

// Read a cover art file. Returns a malloc'd buffer or NULL.
static uint8_t *read_artwork(const char *path, size_t *length) {
  if (path[0] == '\0')
    return NULL;
  FILE *f = fopen(path, "rb");
  if (f == NULL)
    return NULL;
  uint8_t *buf = NULL;
  if (fseek(f, 0, SEEK_END) == 0) {
    long sz = ftell(f);
    if (sz > 0 && sz <= MRP_ARTWORK_MAX_BYTES && fseek(f, 0, SEEK_SET) == 0) {
      buf = malloc((size_t)sz);
      if (buf != NULL && fread(buf, 1, (size_t)sz, f) == (size_t)sz)
        *length = (size_t)sz;
      else {
        free(buf);
        buf = NULL;
      }
    } else if (sz > MRP_ARTWORK_MAX_BYTES) {
      debug(2, "MRP: artwork file is %ld bytes, which is too large to send.", sz);
    }
  }
  fclose(f);
  return buf;
}

// A controller has asked for the current item and its artwork (PLAYBACK_QUEUE_REQUEST). Reply with
// a SET_STATE carrying the item, with the image data, and the same identifier as the request.
static void queue_artwork_reply(rtsp_conn_info *conn, const uint8_t *ident, size_t ident_len) {
  np_snapshot s;
  pthread_mutex_lock(&snap_mutex);
  s = snapshot;
  pthread_mutex_unlock(&snap_mutex);
  make_idle_snapshot(&s);
  size_t art_length = 0;
  uint8_t *art = read_artwork(s.cover_path, &art_length);
  debug(2, "Connection %d (\"%s\"): MRP: artwork for the reply: %zu bytes from \"%s\".",
        conn->connection_number, mrp_peer_name(conn), art_length, s.cover_path);
  size_t cap = art_length + 8192;
  uint8_t *scratch = malloc(4 * cap);
  if (scratch == NULL) {
    free(art);
    return;
  }
  uint8_t *md = scratch, *item = scratch + cap, *queue = scratch + 2 * cap,
          *state = scratch + 3 * cap;
  uint8_t path[300];
  size_t pl = build_player_path(path, sizeof(path));
  char tid[16];
  track_identifier(&s, tid, sizeof(tid));
  pb_writer mw, iw, qw, sw;
  pb_writer_init(&mw, md, cap);
  put_metadata(&mw, &s);
  if (art != NULL && art_length > 0)
    put_picture_metadata(&mw, art, art_length);
  pb_writer_init(&iw, item, cap);
  pb_put_string(&iw, 1, tid);
  pb_put_bytes(&iw, 2, mw.buf, mw.length);
  if (art != NULL && art_length > 0) {
    pb_put_bytes(&iw, 3, art, art_length); // artworkData
    uint32_t w = 0, h = 0;
    if (image_dimensions(art, art_length, &w, &h) == 0 && w > 0 && h > 0) {
      pb_put_uint(&iw, 13, w); // artworkDataWidth
      pb_put_uint(&iw, 14, h); // artworkDataHeight
    }
  }
  pb_writer_init(&qw, queue, cap);
  pb_put_uint(&qw, 1, 0); // location
  pb_put_bytes(&qw, 2, iw.buf, iw.length);
  pb_writer_init(&sw, state, cap);
  pb_put_bytes(&sw, 3, qw.buf, qw.length);
  pb_put_uint(&sw, 6, (uint64_t)s.state);
  if (pl > 0)
    pb_put_bytes(&sw, 9, path, pl);
  if (mw.overflow || iw.overflow || qw.overflow || sw.overflow) {
    debug(2, "Connection %d (\"%s\"): MRP: could not build an artwork reply.",
          conn->connection_number, mrp_peer_name(conn));
  } else {
    debug(2, "Connection %d (\"%s\"): MRP: queueing an artwork reply, %zu bytes of artwork.",
          conn->connection_number, mrp_peer_name(conn), art_length);
    queue_message(conn, MRP_TYPE_SET_STATE, ident, ident_len, MRP_EXT_SET_STATE, sw.buf,
                  sw.length);
  }
  free(scratch);
  free(art);
}

// Pushes our state to the subscribed controllers (synthetic state only): it wakes whenever the
// snapshot changes, or a controller subscribes, and sends each controller what it has not yet
// been sent, working out the position of the track as it does.
static void *pusher_thread(__attribute__((unused)) void *arg) {
  unsigned long seen = 0;
  np_snapshot pushed; // the state we last pushed, to see whether the state, as opposed to the
                      // volume, has changed
  memset(&pushed, 0, sizeof(pushed));
  int have_pushed = 0;
  for (;;) {
    np_snapshot s;
    pthread_mutex_lock(&snap_mutex);
    while (wake_count == seen)
      pthread_cond_wait(&snap_cond, &snap_mutex);
    seen = wake_count;
    s = snapshot;
    pthread_mutex_unlock(&snap_mutex);

    np_snapshot a = s, b = pushed;
    a.airplay_volume = b.airplay_volume = 0.0;
    int state_changed = !have_pushed || memcmp(&a, &b, sizeof(a)) != 0;
    pushed = s;
    have_pushed = 1;
#ifdef CONFIG_METADATA_HUB
    {
      double progress_duration = 0.0;
      double position = estimate_position(&progress_duration);
      if (position >= 0.0) {
        if (s.duration <= 0.0)
          s.duration = progress_duration;
        s.elapsed = position;
        s.snap_time = apple_time_now();
      } else {
        s.duration = 0.0; // no usable position: leave the progress bar out
      }
    }
#endif
    make_idle_snapshot(&s);
    float volume = current_unit_volume();

    pthread_mutex_lock(&relays_mutex);
    for (int i = 0; i < MRP_MAX_RELAYS; i++) {
      relay_entry *e = &relays[i];
      if (e->conn == NULL || e->conn->stop != 0) // free, or closing
        continue;
      // the device that is playing to us does not accept our state while it is playing (it
      // returns a 400), and it doesn't need it, so skip it
      if (s.source_ip[0] != '\0' && strcmp(e->conn->client_ip_string, s.source_ip) == 0) {
        e->was_source = 1;
        continue;
      }
      // when its playback ends, a device that was the source is closing its own session, and
      // often returns a 400 to the "not connected" message. It doesn't need it: it has its own
      // now-playing. Leave it alone until something is playing again, or it starts a new relay
      // session.
      if (e->was_source) {
        if (s.state == 3)
          continue;
        e->was_source = 0;
      }
      // the client and player identity messages are sent once per connection; the state says
      // whether there is artwork, but the artwork itself is only sent when it is asked for
      if (state_changed || e->identity_sent == 0) {
        debug(2, "Connection %d (\"%s\"): MRP: pushing state %d, \"%s\" / \"%s\" / \"%s\" on stream %d.",
              e->conn->connection_number, mrp_peer_name(e->conn), s.state, s.title, s.artist,
              s.album, e->stream_id);
        size_t l = 0;
        uint8_t *buf = build_state_messages(&s, e->identity_sent == 0, &l);
        if (buf == NULL) {
          debug(2, "MRP: could not build the state messages.");
          continue;
        }
        send_data_event_on(e->conn, e->stream_id, buf, l);
        free(buf);
        e->identity_sent = 1;
      }
#if MRP_SEND_ARTWORK_UPDATES
      {
        // when the track or its artwork changes, tell the controller the artwork has changed
        uint32_t key = artwork_key(&s);
        if (key != e->art_key) {
          if (key != 0) {
            size_t l = 0;
            uint8_t *buf = build_artwork_update_message(&s, &l);
            if (buf != NULL) {
              send_data_event_on(e->conn, e->stream_id, buf, l);
              free(buf);
            }
          }
          e->art_key = key;
        }
      }
#endif
      if (fabsf(e->volume_sent - volume) > 0.0005f) {
        uint8_t buf[256];
        pb_writer vw;
        pb_writer_init(&vw, buf, sizeof(buf));
        append_volume_did_change(&vw, volume);
        if (!vw.overflow) {
          send_data_event_on(e->conn, e->stream_id, vw.buf, vw.length);
          e->volume_sent = volume;
        }
      }
    }
    pthread_mutex_unlock(&relays_mutex);
  }
  return NULL;
}

#ifdef CONFIG_METADATA_HUB
#ifdef CONFIG_AIRPLAY_2
// The URL template for the artwork, from the sender's artworkIdentifier in the now-playing
// plist, or an empty string if there is none. Apple Music gives a path such as
// "Music211/v4/6c/ba/3f/<uuid>/3701624512364.png". A real HomePod passes it on as the URL template
// "https://is1-ssl.mzstatic.com/image/thumb/<path>/{w}x{h}bb.{f}".
static void artwork_template(plist_t npi_plist, char *out, size_t cap) {
  out[0] = '\0';
  plist_t id_item =
      plist_dict_get_item(npi_plist, "kMRMediaRemoteNowPlayingInfoArtworkIdentifier");
  if (id_item == NULL)
    return;
  char *id = NULL;
  plist_get_string_val(id_item, &id);
  if (id == NULL)
    return;
  if (strncmp(id, "https://", 8) == 0)
    snprintf(out, cap, "%s", id);
  else if (strstr(id, "://") == NULL && strchr(id, '/') != NULL && id[0] != '/')
    snprintf(out, cap, "https://is1-ssl.mzstatic.com/image/thumb/%s/{w}x{h}bb.{f}", id);
  free(id);
}
#endif

// Runs with the hub lock held, in whichever thread changed the metadata, so it must not block.
static void mrp_metadata_watcher(struct metadata_bundle *b) {
  np_snapshot n;
  memset(&n, 0, sizeof(n));
  // play_status is what the sender told us, but senders often never send it; if it is unknown,
  // fall back to player_state, which Shairport Sync itself maintains.
  play_status_type ps = b->play_status;
  if (ps == PS_NOT_AVAILABLE)
    ps = b->player_state;
  switch (ps) {
  case PS_PLAYING: n.state = 1; break;
  case PS_PAUSED: n.state = 2; break;
  default: n.state = 3; break; // stopped, or not available
  }
  if (b->npi.track_name) snprintf(n.title, sizeof(n.title), "%s", b->npi.track_name);
  if (b->npi.artist_name) snprintf(n.artist, sizeof(n.artist), "%s", b->npi.artist_name);
  if (b->npi.album_name) snprintf(n.album, sizeof(n.album), "%s", b->npi.album_name);
  if (b->npi.cover_art_pathname) {
    const char *cp = b->npi.cover_art_pathname;
    if (strncmp(cp, "file://", 7) == 0)
      cp += 7;
    snprintf(n.cover_path, sizeof(n.cover_path), "%s", cp);
  }
  n.airplay_volume = b->airplay_volume;
#ifdef CONFIG_AIRPLAY_2
  if (b->npi.npi_plist != NULL) {
    artwork_template(b->npi.npi_plist, n.artwork_id, sizeof(n.artwork_id));
    // Diagnostic: what artwork information has the sender given us?
    debug(3, "MRP: sender artwork identifier %s, cover file %s, track \"%s\".",
          n.artwork_id[0] != '\0' ? n.artwork_id : "(none)",
          b->npi.cover_art_pathname ? b->npi.cover_art_pathname : "(none)",
          b->npi.track_name ? b->npi.track_name : "");
  } else {
    debug(3, "MRP: no now-playing plist yet.");
  }
#endif
  if (b->npi.songtime_in_microseconds.valid)
    n.duration = (double)b->npi.songtime_in_microseconds.value / 1e6;
  n.trig_prog[0] = b->progress_first_timestamp;
  n.trig_prog[1] = b->progress_current_timestamp;
  n.trig_prog[2] = b->progress_last_timestamp;
#ifdef CONFIG_AIRPLAY_2
  if (b->npi.elapsedTimeOnReceiptOfNowPlayingInfo.valid)
    n.trig_elapsed = b->npi.elapsedTimeOnReceiptOfNowPlayingInfo.value;
  n.rate = n.state == 1 ? (b->npi.playbackRate > 0.0 ? b->npi.playbackRate : 1.0) : 0.0;
#else
  n.rate = n.state == 1 ? 1.0 : 0.0;
#endif
  if ((n.state == 1 || n.state == 2) && b->client_ip)
    snprintf(n.source_ip, sizeof(n.source_ip), "%s", b->client_ip);
  pthread_mutex_lock(&snap_mutex);
  if (n.state != snapshot.state)
    debug(2,
          "MRP: state is now %d (1 playing, 2 paused, 3 stopped), from play_status %d and "
          "player_state %d (0 not available, 1 stopped, 2 paused, 3 playing).",
          n.state, (int)b->play_status, (int)b->player_state);
  int changed = memcmp(&n, &snapshot, sizeof(n)) != 0;
  if (changed) {
    snapshot = n;
    wake_count++;
    pthread_cond_signal(&snap_cond);
  }
  pthread_mutex_unlock(&snap_mutex);
}
#endif

static void start_pusher(void) {
  pthread_t t;
  if (pthread_create(&t, NULL, pusher_thread, NULL) == 0)
    pthread_detach(t);
#ifdef CONFIG_METADATA_HUB
  add_metadata_watcher(mrp_metadata_watcher);
  // The hub calls its watchers only when something changes, and this one is not added until the
  // first relay connects, so ask the hub to run its watchers now, to catch up with the present
  // state. Otherwise the snapshot stays at its initial "stopped" until the next change, even if
  // something is already playing.
  send_ssnc_metadata('rfsh', NULL, 0, 1);
#endif
}

// Start a relay's life: nothing has been sent to the controller yet.
static void reset_relay(relay_entry *e) {
  e->identity_sent = 0;
  e->was_source = 0;
  e->volume_sent = -1.0f;
  e->art_key = 0;
}

static void register_relay(rtsp_conn_info *conn) {
  pthread_once(&pusher_once, start_pusher);
  // a controller can have more than one data channel (stream) on a connection, and each one is
  // a MediaRemote session of its own, so each gets its own entry
  int stream_id = current_stream(conn);
  pthread_mutex_lock(&relays_mutex);
  int found = 0;
  for (int i = 0; i < MRP_MAX_RELAYS; i++)
    if (relays[i].conn == conn && relays[i].stream_id == stream_id) {
      // The controller has started its MediaRemote session again on this connection (it can do
      // that), so it has forgotten our now-playing client, player and artwork. Send them again.
      reset_relay(&relays[i]);
      found = 1;
    }
  for (int i = 0; i < MRP_MAX_RELAYS && !found; i++)
    if (relays[i].conn == NULL) {
      relays[i].conn = conn;
      relays[i].stream_id = stream_id;
      reset_relay(&relays[i]);
      found = 1;
    }
  pthread_mutex_unlock(&relays_mutex);
  if (!found)
    debug(2, "Connection %d (\"%s\"): MRP: too many relay connections.", conn->connection_number,
          mrp_peer_name(conn));
  wake_pusher(); // send the current state to the new subscriber (and to the others, harmlessly)
}

void mrp_relay_flush(rtsp_conn_info *conn) {
  if (conn->mrp_pending != NULL) {
    uint8_t *data = conn->mrp_pending;
    size_t length = conn->mrp_pending_length;
    conn->mrp_pending = NULL;
    conn->mrp_pending_length = 0;
    send_data_event(conn, data, length);
    free(data);
  }
  if (conn->mrp_subscribe_pending) {
    conn->mrp_subscribe_pending = 0;
    register_relay(conn);
  }
}

// Tell a controller that its connection to us is Disconnected. (Only controllers have been seen to
// send this message; no receiver has.)
static void send_disconnected(rtsp_conn_info *conn, int stream_id) {
  uint8_t buf[1024], pay[320];
  pb_writer o, pw;
  pb_writer_init(&o, buf, sizeof(buf));
  // SET_CONNECTION_STATE: state (1)
  pb_writer_init(&pw, pay, sizeof(pay));
  pb_put_uint(&pw, 1, MRP_CONNECTION_STATE_DISCONNECTED);
  append_message(&o, MRP_TYPE_SET_CONNECTION_STATE, MRP_EXT_SET_CONNECTION_STATE, pw.buf,
                 pw.length);
  if (!o.overflow)
    send_data_event_on(conn, stream_id, o.buf, o.length);
}

void mrp_relay_close_all(void) {
  const int closing = MRP_CLOSE_AFTER_DISCONNECTED || !MRP_SEND_DISCONNECTED_BEFORE_CLOSE;
#if MRP_PASSTHROUGH
  pt_close_all(1);
#endif
  pthread_mutex_lock(&relays_mutex);
  for (int i = 0; i < MRP_MAX_RELAYS; i++) {
    rtsp_conn_info *c = relays[i].conn;
    if (c != NULL) {
      debug(2, "Connection %d (\"%s\"): MRP: %s this relay connection (stream %d).",
            c->connection_number, mrp_peer_name(c), closing ? "closing" : "leaving open",
            relays[i].stream_id);
      if (MRP_SEND_DISCONNECTED_BEFORE_CLOSE)
        send_disconnected(c, relays[i].stream_id);
    }
  }
  if (closing) // a connection can have several entries; close each connection once
    for (int i = 0; i < MRP_MAX_RELAYS; i++)
      if (relays[i].conn != NULL && relays[i].conn->stop == 0)
        close_connection(relays[i].conn);
  pthread_mutex_unlock(&relays_mutex);
}

void mrp_relay_cleanup(rtsp_conn_info *conn) {
#if MRP_PASSTHROUGH
  pt_forget_connection(conn);
#endif
  pthread_mutex_lock(&relays_mutex);
  for (int i = 0; i < MRP_MAX_RELAYS; i++)
    if (relays[i].conn == conn)
      relays[i].conn = NULL;
  pthread_mutex_unlock(&relays_mutex);
  free(conn->mrp_pending);
  conn->mrp_pending = NULL;
  conn->mrp_pending_length = 0;
}

#if !MRP_PASSTHROUGH
void mrp_relay_handle_sender_command(__attribute__((unused)) rtsp_conn_info *conn,
                                     __attribute__((unused)) rtsp_message *req,
                                     __attribute__((unused)) rtsp_message *resp) {}
#endif
