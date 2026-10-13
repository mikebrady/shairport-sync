/*
 * Embedded dns-sd client. This file is part of Shairport.
 * Copyright (c) Paul Lietar 2013
 * Copyright (c) Mike Brady 2014--2025
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

static DNSServiceRef service;     // the _raop._tcp service (AirPlay 1 and AirPlay 2)
static DNSServiceRef ap2_service; // the _airplay._tcp service (AirPlay 2 only)

// Pack a NULL-terminated array of "key=value" strings into DNS TXT record
// wire format, where each string is preceded by a one-byte length.
// The caller is responsible for freeing the returned buffer.
static char *txt_from_records(char **records, uint16_t *length) {
  char **field;
  uint16_t size = 0;
  for (field = records; *field; field++)
    size += strlen(*field) + 1; // One byte for length each time
  char *buf = malloc(size + 1); // stpcpy() also writes a NUL after the last string
  if (buf == NULL)
    return NULL;
  char *p = buf;
  for (field = records; *field; field++) {
    char *newp = stpcpy(p + 1, *field);
    *p = newp - p - 1;
    p = newp;
  }
  *length = size;
  return buf;
}

static int register_service(DNSServiceRef *ref, char *name, const char *regtype, int port,
                            char **records) {
  uint16_t length = 0;
  char *buf = txt_from_records(records, &length);
  if (buf == NULL) {
    warn("dns_sd: buffer record allocation failed");
    return -1;
  }
  uint32_t interface_index = kDNSServiceInterfaceIndexAny;
  if (config.interface != NULL)
    interface_index = config.interface_index;
  DNSServiceErrorType error = DNSServiceRegister(ref, 0, interface_index, name, regtype, "", NULL,
                                                 htons((uint16_t)port), length, buf, NULL, NULL);
  free(buf);
  if (error != kDNSServiceErr_NoError) {
    warn("dns-sd: DNSServiceRegister error %d registering \"%s\" as %s", error, name, regtype);
    return -1;
  }
  return 0;
}

static int update_service(DNSServiceRef ref, char **records) {
  uint16_t length = 0;
  char *buf = txt_from_records(records, &length);
  if (buf == NULL)
    return -1;
  DNSServiceErrorType error = DNSServiceUpdateRecord(ref, NULL, 0, length, buf, 0);
  free(buf);
  if (error != kDNSServiceErr_NoError) {
    debug(1, "dns-sd: DNSServiceUpdateRecord error %d", error);
    return -1;
  }
  return 0;
}

static int mdns_dns_sd_register(char *ap1name, char *ap2name, int port, char **txt_records,
                                char **secondary_txt_records) {
  char *recordwithoutmetadata[] = {MDNS_RECORD_WITHOUT_METADATA, NULL};
#ifdef CONFIG_METADATA
  char *recordwithmetadata[] = {MDNS_RECORD_WITH_METADATA, NULL};
#endif
  char **record = txt_records;
  if (record == NULL) {
#ifdef CONFIG_METADATA
    if (config.metadata_enabled)
      record = recordwithmetadata;
    else
#endif
      record = recordwithoutmetadata;
  }

  // As with the Avahi backend, AirPlay 2 needs a second, _airplay._tcp, service
  if ((secondary_txt_records != NULL) && (ap2name != NULL)) {
    if (register_service(&ap2_service, ap2name, config.regtype2, port, secondary_txt_records) != 0)
      return -1;
  }
  if (register_service(&service, ap1name, config.regtype, port, record) != 0) {
    if (ap2_service) {
      DNSServiceRefDeallocate(ap2_service);
      ap2_service = NULL;
    }
    return -1;
  }
  return 0;
}

static int mdns_dns_sd_update(char **txt_records, char **secondary_txt_records) {
  int response = 0;
  if ((txt_records != NULL) && (service))
    response |= update_service(service, txt_records);
  if ((secondary_txt_records != NULL) && (ap2_service))
    response |= update_service(ap2_service, secondary_txt_records);
  return response;
}

static void mdns_dns_sd_unregister(void) {
  if (ap2_service) {
    DNSServiceRefDeallocate(ap2_service);
    ap2_service = NULL;
  }
  if (service) {
    DNSServiceRefDeallocate(service);
    service = NULL;
  }
}

mdns_backend mdns_dns_sd = {.name = "dns-sd",
                            .mdns_register = mdns_dns_sd_register,
                            .mdns_update = mdns_dns_sd_update,
                            .mdns_unregister = mdns_dns_sd_unregister,
                            .mdns_dacp_monitor_start = NULL,
                            .mdns_dacp_monitor_set_id = NULL,
                            .mdns_dacp_monitor_stop = NULL};
