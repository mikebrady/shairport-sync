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
#include "mrp_protobuf.h"
#include "common.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

int pb_read_varint(const uint8_t *buf, size_t len, size_t *pos, uint64_t *out) {
  uint64_t r = 0;
  unsigned int shift = 0;
  size_t p = *pos;
  while (p < len && shift < 64) {
    uint8_t c = buf[p++];
    r |= (uint64_t)(c & 0x7f) << shift;
    if ((c & 0x80) == 0) {
      *pos = p;
      *out = r;
      return 0;
    }
    shift += 7;
  }
  return -1;
}

int pb_next(const uint8_t *buf, size_t len, size_t *pos, pb_field *f) {
  if (*pos >= len)
    return 0;
  uint64_t key;
  size_t p = *pos;
  if (pb_read_varint(buf, len, &p, &key) != 0)
    return -1;
  memset(f, 0, sizeof(*f));
  f->field = (uint32_t)(key >> 3);
  f->wire = (uint32_t)(key & 7);
  if (f->field == 0)
    return -1;
  switch (f->wire) {
  case 0:
    if (pb_read_varint(buf, len, &p, &f->value) != 0)
      return -1;
    break;
  case 1:
    if (len - p < 8)
      return -1;
    memcpy(&f->value, buf + p, 8); // raw value, not interpreted
    p += 8;
    break;
  case 5:
    if (len - p < 4)
      return -1;
    {
      uint32_t v;
      memcpy(&v, buf + p, 4);
      f->value = v;
    }
    p += 4;
    break;
  case 2: {
    uint64_t l;
    if (pb_read_varint(buf, len, &p, &l) != 0)
      return -1;
    if (l > len - p)
      return -1;
    f->data = buf + p;
    f->length = (size_t)l;
    p += (size_t)l;
    break;
  }
  default:
    return -1; // groups etc. are not used
  }
  *pos = p;
  return 1;
}

int pb_find(const uint8_t *buf, size_t len, uint32_t field, uint32_t wire, pb_field *f) {
  size_t pos = 0;
  pb_field g;
  int found = 0;
  while (pb_next(buf, len, &pos, &g) == 1)
    if (g.field == field && g.wire == wire) {
      *f = g;
      found = 1;
    }
  return found;
}

static int looks_like_text(const uint8_t *d, size_t l) {
  if (l == 0)
    return 0;
  for (size_t i = 0; i < l; i++)
    if (d[i] < 0x20 || d[i] == 0x7f)
      return 0; // control characters: not text. Bytes >= 0x80 are accepted as UTF-8
  return 1;
}

#define PB_DUMP_MAX_DEPTH 6
#define PB_DUMP_MAX_FIELDS 512

static void pb_dump_inner(int level, const char *prefix, const uint8_t *buf, size_t len, int depth) {
  size_t pos = 0;
  pb_field f;
  int count = 0, r;
  char indent[32];
  int n = depth * 2;
  if (n > 30)
    n = 30;
  memset(indent, ' ', n);
  indent[n] = '\0';
  while ((r = pb_next(buf, len, &pos, &f)) == 1) {
    if (++count > PB_DUMP_MAX_FIELDS) {
      debug(level, "%s%s...", prefix, indent);
      return;
    }
    if (f.wire == 0) {
      debug(level, "%s%s%u: %llu", prefix, indent, f.field, (unsigned long long)f.value);
    } else if (f.wire == 2) {
      if (looks_like_text(f.data, f.length)) {
        debug(level, "%s%s%u: \"%.*s\"", prefix, indent, f.field, (int)f.length,
              (const char *)f.data);
      } else {
        // is it a well-formed submessage?
        size_t p2 = 0;
        pb_field g;
        int rr = 0;
        if (f.length > 0)
          while ((rr = pb_next(f.data, f.length, &p2, &g)) == 1)
            ;
        if (f.length > 0 && rr == 0 && depth < PB_DUMP_MAX_DEPTH) {
          debug(level, "%s%s%u: {", prefix, indent, f.field);
          pb_dump_inner(level, prefix, f.data, f.length, depth + 1);
          debug(level, "%s%s}", prefix, indent);
        } else {
          debug(level, "%s%s%u: <%zu bytes>", prefix, indent, f.field, f.length);
        }
      }
    } else {
      debug(level, "%s%s%u: 0x%llx (wire %u)", prefix, indent, f.field, (unsigned long long)f.value,
            f.wire);
    }
  }
  if (r < 0)
    debug(level, "%s%s<malformed>", prefix, indent);
}

void pb_dump(int level, const char *prefix, const uint8_t *buf, size_t len) {
  if (debug_level() >= level)
    pb_dump_inner(level, prefix, buf, len, 0);
}
