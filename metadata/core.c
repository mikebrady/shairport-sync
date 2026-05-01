/*
 * Core metadata handler.
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


#include "core.h"
#include "pc_queue.h"

#include <stdlib.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

#ifdef CONFIG_METADATA_PIPE
#include "pipe.h"
#endif

#ifdef CONFIG_METADATA_MULTICAST
#include "multicast.h"
#endif

#ifdef CONFIG_METADATA_HUB
#include "hub.h"
#endif

#ifdef CONFIG_MQTT
#include "mqtt.h"
#endif


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

int metadata_running = 0;

void metadata_pack_cleanup_function(void *arg) {
  // debug(1, "metadata_pack_cleanup_function called");
  metadata_package *pack = (metadata_package *)arg;
  if (pack->carrier)
    msg_free(&pack->carrier); // release the message
  else if (pack->data)
    free(pack->data);
  // debug(1, "metadata_pack_cleanup_function exit");
}

void metadata_init(void) {

#ifdef CONFIG_METADATA_PIPE
  metadata_pipe_queue_init();
#endif  

#ifdef CONFIG_METADATA_MULTICAST
    metadata_multicast_queue_init();
#endif

#ifdef CONFIG_METADATA_HUB
    metadata_hub_queue_init();
#endif

#ifdef CONFIG_MQTT
  metadata_mqtt_queue_init();
#endif
  metadata_running = 1;
}

void metadata_stop(void) {
  if (metadata_running) {
    debug(2, "metadata_stop called.");

#ifdef CONFIG_MQTT
    metadata_mqtt_queue_stop();
#endif

#ifdef CONFIG_METADATA_HUB
    metadata_hub_queue_stop();
#endif

#ifdef CONFIG_METADATA_MULTICAST      
    metadata_multicast_queue_stop();
#endif

#ifdef CONFIG_METADATA_PIPE     
    metadata_pipe_queue_stop();
#endif

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

#ifdef CONFIG_METADATA_PIPE
    rc = send_metadata_to_pipe_queue(type, code, data, length, carrier, block);
#endif

#ifdef CONFIG_METADATA_MULTICAST
    rc =
        send_metadata_to_multicast_queue(type, code, data, length, carrier, block);
#endif

#ifdef CONFIG_METADATA_HUB
    rc = send_metadata_to_hub_queue(type, code, data, length, carrier, block);
#endif

#ifdef CONFIG_MQTT
    rc = send_metadata_to_mqtt_queue(type, code, data, length, carrier, block);
#endif

  }
  return rc;
}

int send_ssnc_metadata(const uint32_t code, const char *data, const uint32_t length,
                       const int block) {
  return send_metadata('ssnc', code, data, length, NULL, block);
}

