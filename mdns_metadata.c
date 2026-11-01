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
// 'svrg' registers a service, 'svup' replaces its TXT records. Services end
// when the instance exits (nothing is sent then: the feed is already closed).
// Used only when named; no DACP monitor.

#include "common.h"
#include "mdns.h"
#include "metadata/core.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *ap1_service_name = NULL;
static char *ap2_service_name = NULL;
static int service_port = 0;

static void send_service(const uint32_t code, const char *service_type,
                         const char *service_name, char **txt_records) {
  size_t length = strlen("service=\nname=\nport=65535\n") + strlen(service_type) +
                  strlen(service_name) + 1;
  char **record;
  if (txt_records != NULL)
    for (record = txt_records; *record != NULL; record++)
      length += strlen("txt=\n") + strlen(*record);
  char *payload = malloc(length);
  if (payload == NULL) {
    warn("mdns metadata backend: can not allocate memory for the \"%s\" service.", service_type);
    return;
  }
  char *p = payload;
  p += snprintf(p, length, "service=%s\nname=%s\nport=%d\n", service_type, service_name,
                service_port);
  if (txt_records != NULL)
    for (record = txt_records; *record != NULL; record++) {
      // would split the record
      if (strchr(*record, '\n') != NULL) {
        debug(1, "mdns metadata backend: skipping a TXT record containing a newline.");
        continue;
      }
      p += snprintf(p, length - (p - payload), "txt=%s\n", *record);
    }
  send_ssnc_metadata(code, payload, p - payload, 1); // copied
  free(payload);
}

static void forget_services(void) {
  free(ap1_service_name);
  ap1_service_name = NULL;
  free(ap2_service_name);
  ap2_service_name = NULL;
}

static int mdns_metadata_register(char *ap1name, char *ap2name, int port, char **txt_records,
                                  char **secondary_txt_records) {
  // never a fallback: nothing would be advertised
  if ((config.mdns_name == NULL) || (strcmp(config.mdns_name, "metadata") != 0))
    return -1;
  if (config.metadata_enabled == 0) {
    warn("The \"metadata\" mDNS backend needs metadata to be enabled.");
    return -1;
  }
  forget_services();
  service_port = port;
  // 'svrg' is sent once, here at start-up, and not repeated. Metadata sent while nothing reads
  // the feed is dropped (e.g. while the metadata pipe has no reader), so the application must
  // be reading it before Shairport Sync starts.
  ap1_service_name = strdup(ap1name);
  send_service('svrg', config.regtype, ap1_service_name, txt_records);
  if ((ap2name != NULL) && (secondary_txt_records != NULL)) {
    ap2_service_name = strdup(ap2name);
    send_service('svrg', config.regtype2, ap2_service_name, secondary_txt_records);
  }
  debug(1, "mdns metadata backend: services handed to the metadata reader.");
  return 0;
}

static int mdns_metadata_update(char **txt_records, char **secondary_txt_records) {
  if (ap1_service_name != NULL)
    send_service('svup', config.regtype, ap1_service_name, txt_records);
  if ((ap2_service_name != NULL) && (secondary_txt_records != NULL))
    send_service('svup', config.regtype2, ap2_service_name, secondary_txt_records);
  return 0;
}

static void mdns_metadata_unregister(void) { forget_services(); }

mdns_backend mdns_metadata = {.name = "metadata",
                              .mdns_register = mdns_metadata_register,
                              .mdns_update = mdns_metadata_update,
                              .mdns_unregister = mdns_metadata_unregister,
                              .mdns_dacp_monitor_start = NULL,
                              .mdns_dacp_monitor_set_id = NULL,
                              .mdns_dacp_monitor_stop = NULL};
