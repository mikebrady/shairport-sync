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

// Called when a POST /command arrives on a remote-control-only connection, which carries
// MediaRemote protobuf messages from a controller. They are passed to the source of the audio.
void mrp_relay_handle_command(rtsp_conn_info *conn, rtsp_message *req);

// Called when a POST /command arrives on a connection that is not remote-control-only (that is,
// on the source's connection). A message that carries an X-Apple-StreamID is a reply or an update
// for a controller: it is passed to the controller that stream belongs to, and resp is filled in.
// Otherwise it does nothing.
void mrp_relay_handle_sender_command(rtsp_conn_info *conn, rtsp_message *req, rtsp_message *resp);

// Asks every controller connection to close, e.g. when the Relay flag has been cleared.
// It only sets each connection's stop flag and shuts its socket down, so the connection thread
// exits and cleans up for itself; it does not wait for the threads, so it is safe to call from
// anywhere, including a connection's own cleanup.
void mrp_relay_close_all(void);

// Forgets a connection. Call when the connection is being closed.
void mrp_relay_cleanup(rtsp_conn_info *conn);

#endif
