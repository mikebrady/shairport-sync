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

static DNSServiceRef service;

static int mdns_dns_sd_register(char *ap1name, __attribute__((unused)) char *ap2name, int port,
                                __attribute__((unused)) char **txt_records,
                                __attribute__((unused)) char **secondary_txt_records) {
  char *recordwithoutmetadata[] = {MDNS_RECORD_WITHOUT_METADATA, NULL};
#ifdef CONFIG_METADATA
  char *recordwithmetadata[] = {MDNS_RECORD_WITH_METADATA, NULL};
#endif
  char **record;
#ifdef CONFIG_METADATA
  if (config.metadata_enabled)
    record = recordwithmetadata;
  else
#endif
    record = recordwithoutmetadata;

  uint16_t length = 0;
  char **field;

  // Concatenate string contained i record into buf.

  for (field = record; *field; field++) {
    length += strlen(*field) + 1; // One byte for length each time
  }

  char *buf = malloc(length * sizeof(char));
  if (buf == NULL) {
    warn("dns_sd: buffer record allocation failed");
    return -1;
  }

  char *p = buf;

  for (field = record; *field; field++) {
    char *newp = stpcpy(p + 1, *field);
    *p = newp - p - 1;
    p = newp;
  }

  DNSServiceErrorType error;
  error = DNSServiceRegister(&service, 0, kDNSServiceInterfaceIndexAny, ap1name, config.regtype, "",
                             NULL, htons((uint16_t)port), length, buf, NULL, NULL);

  free(buf);

  if (error == kDNSServiceErr_NoError)
    return 0;
  else {
    warn("dns-sd: DNSServiceRegister error %d", error);
    return -1;
  }
}

static void mdns_dns_sd_unregister(void) {
  if (service) {
    DNSServiceRefDeallocate(service);
    service = NULL;
  }
}

mdns_backend mdns_dns_sd = {.name = "dns-sd",
                            .mdns_register = mdns_dns_sd_register,
                            .mdns_unregister = mdns_dns_sd_unregister,
                            .mdns_dacp_monitor_start = NULL,
                            .mdns_dacp_monitor_set_id = NULL,
                            .mdns_dacp_monitor_stop = NULL};
