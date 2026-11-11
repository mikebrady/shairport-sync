/*
 * mDNS "metadata" backend. This file is part of Shairport Sync.
 * Copyright (c) 2026 Shairport Sync contributors
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

// Advertises nothing itself: each service is sent to the metadata feed for
// an application with its own mDNS responder to publish. Payload, one line each:
//   service=<type>  name=<name>  port=<port>  txt=<key=value> (per TXT record)
// 'svrg' registers a service or replaces its TXT records. It is sent for every
// record the mDNS registry adds or changes, with that record's own port.
// Services end when the instance exits (nothing is sent then: the feed is already closed).
// Used only when named; no DACP monitor.

#include "common.h"
#include "mdns.h"
#include "metadata/core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void send_service(const uint32_t code, const mdns_service_record *rec) {
  size_t length = strlen("service=\nname=\nport=65535\n") + strlen(rec->service_type) +
                  strlen(rec->instance_name) + 1;
  for (size_t i = 0; i < rec->n; i++)
    length += strlen("txt=\n") + strlen(mdns_record_get(rec, i));
  char *payload = malloc(length);
  if (payload == NULL) {
    warn("mdns metadata backend: can not allocate memory for the \"%s\" service.",
         rec->service_type);
    return;
  }
  char *p = payload;
  p += snprintf(p, length, "service=%s\nname=%s\nport=%u\n", rec->service_type, rec->instance_name,
                (unsigned)rec->port);
  for (size_t i = 0; i < rec->n; i++) {
    const char *txt = mdns_record_get(rec, i);
    // would split the record
    if (strchr(txt, '\n') != NULL) {
      debug(1, "mdns metadata backend: skipping a TXT record containing a newline.");
      continue;
    }
    p += snprintf(p, length - (size_t)(p - payload), "txt=%s\n", txt);
  }
  send_ssnc_metadata(code, payload, (size_t)(p - payload), 1); // copied
  free(payload);
}

static int mdns_metadata_start(void) {
  // never a fallback: nothing would be advertised
  if ((config.mdns_name == NULL) || (strcmp(config.mdns_name, "metadata") != 0))
    return -1;
  if (config.metadata_enabled == 0) {
    warn("The \"metadata\" mDNS backend needs metadata to be enabled.");
    return -1;
  }
  // Metadata sent while nothing reads the feed is dropped (e.g. while the metadata pipe has no
  // reader), so the application should be reading it before Shairport Sync starts.
  debug(1, "mdns metadata backend: services will be handed to the metadata reader.");
  return 0;
}

static void mdns_metadata_publish(const mdns_service_record *rec, mdns_put_result why, void *ctx) {
  (void)why; // 'svrg' registers a service or replaces its TXT records
  (void)ctx;
  send_service('svrg', rec);
}

// The feed has no message for withdrawing a service (they end with the instance).
static void mdns_metadata_unpublish(const mdns_service_record *rec, void *ctx) {
  (void)ctx;
  debug(1, "mdns metadata backend: nothing is sent when the %s service is removed.",
        rec->service_type);
}

mdns_backend mdns_metadata = {.name = "metadata",
                              .mdns_start = mdns_metadata_start,
                              .mdns_publish = mdns_metadata_publish,
                              .mdns_unpublish = mdns_metadata_unpublish,
                              .mdns_dacp_monitor_start = NULL,
                              .mdns_dacp_monitor_set_id = NULL,
                              .mdns_dacp_monitor_stop = NULL};
