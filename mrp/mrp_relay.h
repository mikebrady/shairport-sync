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
#ifndef _MRP_RELAY_H
#define _MRP_RELAY_H

#include "player.h"
#include "rtsp.h"

// A controller can set up more than one MediaRemote data channel (stream) on a connection, and
// each is a MediaRemote session of its own. When this is 1, each channel is given a stream ID of
// its own (1, 2, 3...), each is sent its own now-playing state, and replies go out on the stream
// of the message they answer. When it is 0, every channel is stream 1, which makes controllers that
// open two channels, such as the iPad and the Mac, wait before they show anything.
#define MRP_DISTINCT_STREAM_IDS 1

// Called when a POST /command arrives on a remote-control-only connection.
// Decodes and logs the MediaRemote protobuf messages it carries and queues any replies.
void mrp_relay_handle_command(rtsp_conn_info *conn, rtsp_message *req);

// Sends any queued replies on the event channel. Call it AFTER the RTSP response to the
// POST /command has been written, because the controller waits for that response first.
void mrp_relay_flush(rtsp_conn_info *conn);

// Asks every live relay connection to close, e.g. when the Relay flag has been cleared.
// It only sets each connection's stop flag and shuts its socket down, so the connection thread
// exits and cleans up for itself; it does not wait for the threads, so it is safe to call from
// anywhere, including a connection's own cleanup.
void mrp_relay_close_all(void);

// Frees anything queued. Call when the connection is being closed.
void mrp_relay_cleanup(rtsp_conn_info *conn);

#endif
