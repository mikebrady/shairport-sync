/*
 * Embedded dns-sd client. This file is part of Shairport.
 * Copyright (c) Paul Lietar 2013
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

#include "common.h"
#include "mdns.h"
#include <arpa/inet.h>
#include <dns_sd.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct {
  int used;
  DNSServiceRef ref;
  mdns_service_record reg; // what is registered: type, instance name, port and text
} entry;

static entry entries[MDNS_MAX_RECORDS];

static entry *find_entry(const char *type) {
  for (int i = 0; i < MDNS_MAX_RECORDS; i++)
    if (entries[i].used && (strcasecmp(entries[i].reg.service_type, type) == 0))
      return &entries[i];
  return NULL;
}

static entry *alloc_entry(void) {
  for (int i = 0; i < MDNS_MAX_RECORDS; i++)
    if (!entries[i].used) {
      memset(&entries[i], 0, sizeof(entry));
      return &entries[i];
    }
  return NULL;
}

static void drop_entry(entry *e) {
  if (e->ref != NULL)
    DNSServiceRefDeallocate(e->ref);
  memset(e, 0, sizeof(entry));
}

// DNS wire format TXT data. mdns_record_to_wire's output is never larger than
// MDNS_TXT_BYTES (an empty record is a single zero byte).
static int txt_wire(const mdns_service_record *rec, uint8_t *buf, size_t cap) {
  int len = mdns_record_to_wire(rec, buf, cap);
  if (len < 0)
    warn("dns-sd: the text records for %s do not fit in a buffer", rec->service_type);
  return len;
}

static int register_record(entry *e, const mdns_service_record *rec) {
  uint8_t txt[MDNS_TXT_BYTES + 1];
  int len = txt_wire(rec, txt, sizeof txt);
  if (len < 0)
    return -1;
  uint32_t interface_index = kDNSServiceInterfaceIndexAny;
  if (config.interface != NULL)
    interface_index = config.interface_index;
  DNSServiceRef ref = NULL;
  DNSServiceErrorType error =
      DNSServiceRegister(&ref, 0, interface_index, rec->instance_name, rec->service_type, "", NULL,
                         htons(rec->port), (uint16_t)len, txt, NULL, NULL);
  if (error != kDNSServiceErr_NoError) {
    warn("dns-sd: DNSServiceRegister error %d registering \"%s\" as %s", (int)error,
         rec->instance_name, rec->service_type);
    return -1;
  }
  e->used = 1;
  e->ref = ref;
  e->reg = *rec;
  return 0;
}

// Replaces the text of a registered service, leaving it on the network.
static int update_record(entry *e, const mdns_service_record *rec) {
  uint8_t txt[MDNS_TXT_BYTES + 1];
  int len = txt_wire(rec, txt, sizeof txt);
  if (len < 0)
    return -1;
  DNSServiceErrorType error = DNSServiceUpdateRecord(e->ref, NULL, 0, (uint16_t)len, txt, 0);
  if (error != kDNSServiceErr_NoError) {
    debug(1, "dns-sd: DNSServiceUpdateRecord error %d", (int)error);
    return -1;
  }
  e->reg = *rec;
  return 0;
}

static void dns_sd_publish(const mdns_service_record *rec, mdns_put_result why, void *ctx) {
  (void)why; // added or replaced: either way, make dns-sd match the record
  (void)ctx;
  entry *e = find_entry(rec->service_type);
  if (e != NULL) {
    if ((strcmp(e->reg.instance_name, rec->instance_name) == 0) && (e->reg.port == rec->port) &&
        (update_record(e, rec) == 0))
      return; // only the text had changed, and it has been updated in place
    drop_entry(e); // a new name or port (or a failed update): register it afresh
  }
  e = alloc_entry();
  if (e == NULL) {
    debug(1, "dns-sd: no free slot for a record of type %s.", rec->service_type);
    return;
  }
  if (register_record(e, rec) != 0)
    memset(e, 0, sizeof(entry));
}

static void dns_sd_unpublish(const mdns_service_record *rec, void *ctx) {
  (void)ctx;
  entry *e = find_entry(rec->service_type);
  if (e != NULL)
    drop_entry(e);
}

static int dns_sd_start(void) {
  memset(entries, 0, sizeof(entries));
  return 0;
}

// Called after every record has been unpublished and the publisher removed.
static void dns_sd_stop(void) {
  for (int i = 0; i < MDNS_MAX_RECORDS; i++)
    if (entries[i].used)
      drop_entry(&entries[i]);
}

mdns_backend mdns_dns_sd = {.name = "dns-sd",
                            .mdns_start = dns_sd_start,
                            .mdns_stop = dns_sd_stop,
                            .mdns_publish = dns_sd_publish,
                            .mdns_unpublish = dns_sd_unpublish,
                            .mdns_dacp_monitor_start = NULL,
                            .mdns_dacp_monitor_set_id = NULL,
                            .mdns_dacp_monitor_stop = NULL};
