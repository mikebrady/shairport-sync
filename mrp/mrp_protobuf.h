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
#ifndef _MRP_PROTOBUF_H
#define _MRP_PROTOBUF_H

// A very small, bounds-checked protobuf reader, just enough to log the
// MediaRemote (MRP) messages carried by AirPlay 2 remote-control connections.

#include <stddef.h>
#include <stdint.h>

typedef struct {
  uint32_t field;       // field number
  uint32_t wire;        // wire type: 0 varint, 1 64-bit, 2 length-delimited, 5 32-bit
  uint64_t value;       // varint value, or the raw 32/64-bit value
  const uint8_t *data;  // for wire type 2: pointer to the payload (inside the input buffer)
  size_t length;        // for wire type 2: payload length
} pb_field;

// returns 0 on success, -1 if the buffer is truncated or malformed
int pb_read_varint(const uint8_t *buf, size_t len, size_t *pos, uint64_t *out);

// returns 1 and fills in f if a field was read, 0 at the end of the buffer, -1 on a malformed field
int pb_next(const uint8_t *buf, size_t len, size_t *pos, pb_field *f);

// looks through a message for the last field with the given number and wire type; returns 1 and
// fills in f if there is one, 0 if not (or if the message is malformed before it)
int pb_find(const uint8_t *buf, size_t len, uint32_t field, uint32_t wire, pb_field *f);

// log a human-readable rendering of a message at the given debug level (nesting limited)
void pb_dump(int level, const char *prefix, const uint8_t *buf, size_t len);

#endif
