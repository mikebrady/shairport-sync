/*
 * mDNS record set manager. This file is part of Shairport Sync.
 * Sets of mDNS records are used for the mDNS registry that Shairport Sync maintains.
 * Copyright (c) Mike Brady 2026
 * All rights reserved.
 *
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

#include "mdns_record_set.h"

#include <string.h>
#include <strings.h> /* strcasecmp */

void mdns_set_init(mdns_record_set *s) { memset(s, 0, sizeof *s); }

/* Records may come from outside (a socket, shared memory), so check what the
 * set and the comparison rely on rather than trusting it: terminated, non-empty
 * names; counts in range; and every string lying inside buf[0..used) with the
 * last byte a NUL, so no strlen can run off the end. */
static int usable(const mdns_service_record *r) {
  if (memchr(r->service_type, '\0', sizeof r->service_type) == NULL ||
      r->service_type[0] == '\0' ||
      memchr(r->instance_name, '\0', sizeof r->instance_name) == NULL ||
      r->instance_name[0] == '\0')
    return 0;
  if (r->n > MDNS_MAX_KV || r->used > MDNS_TXT_BYTES)
    return 0;
  if (r->used > 0 && r->buf[r->used - 1] != '\0')
    return 0;
  for (size_t i = 0; i < r->n; i++)
    if (r->kv_off[i] >= r->used)
      return 0;
  return 1;
}

int mdns_set_find(const mdns_record_set *s, const char *type) {
  for (uint32_t i = 0; i < s->count && i < MDNS_MAX_RECORDS; i++)
    if (strcasecmp(s->rec[i].service_type, type) == 0)
      return (int)i;
  return -1;
}

int mdns_set_put(mdns_record_set *s, const mdns_service_record *r, mdns_put_result *result) {
  if (!usable(r))
    return MDNS_ERR_INVALID;

  mdns_put_result what;
  int i = mdns_set_find(s, r->service_type);
  if (i < 0) { /* a new type: needs a free slot */
    if (s->count >= MDNS_MAX_RECORDS)
      return MDNS_ERR_FULL;
    i = (int)s->count++;
    what = MDNS_PUT_ADDED;
    s->rec[i] = *r; /* by-value copy is safe: the record has no pointers */
  } else if (mdns_record_equal(&s->rec[i], r)) {
    what = MDNS_PUT_UNCHANGED; /* leave the stored record exactly as it is */
  } else {
    what = MDNS_PUT_REPLACED;
    s->rec[i] = *r;
  }
  if (result != NULL)
    *result = what;
  return i;
}

const mdns_service_record *mdns_set_get(const mdns_record_set *s, size_t i) {
  return i < s->count ? &s->rec[i] : NULL;
}

int mdns_set_remove(mdns_record_set *s, const char *type) {
  int i = mdns_set_find(s, type);
  if (i < 0)
    return MDNS_ERR_NOTFOUND;
  memmove(&s->rec[i], &s->rec[i + 1], (s->count - (uint32_t)i - 1) * sizeof s->rec[0]);
  s->count--;
  memset(&s->rec[s->count], 0, sizeof s->rec[0]); /* keep unused slots zeroed */
  return MDNS_OK;
}
