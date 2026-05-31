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

static DNSServiceRef ap1_service;
static DNSServiceRef ap2_service;

static void mdns_dns_sd_unregister(void);

static int build_dns_sd_txt_buffer(char **records, uint16_t *length, char **buffer) {
  size_t length_needed = 0;
  char **field;

  if (records == NULL) {
    *length = 0;
    *buffer = NULL;
    return 0;
  }

  for (field = records; *field; field++) {
    size_t field_length = strlen(*field);
    if (field_length > 255) {
      warn("dns-sd: TXT record field is too long: \"%s\".", *field);
      return -1;
    }
    length_needed += field_length + 1; // One byte for length each time.
    if (length_needed > UINT16_MAX) {
      warn("dns-sd: TXT record is too long.");
      return -1;
    }
  }

  char *buf = malloc(length_needed);
  if (buf == NULL) {
    warn("dns-sd: TXT record buffer allocation failed.");
    return -1;
  }

  char *p = buf;
  for (field = records; *field; field++) {
    char *newp = stpcpy(p + 1, *field);
    *p = newp - p - 1;
    p = newp;
  }

  *length = (uint16_t)length_needed;
  *buffer = buf;
  return 0;
}

static int register_dns_sd_service(DNSServiceRef *service, const char *name, const char *regtype,
                                   int port, char **records) {
  uint16_t length = 0;
  char *buf = NULL;
  int ret = -1;

  if (build_dns_sd_txt_buffer(records, &length, &buf) != 0)
    return -1;

  DNSServiceErrorType error =
      DNSServiceRegister(service, 0, kDNSServiceInterfaceIndexAny, name, regtype, "", NULL,
                         htons((uint16_t)port), length, buf, NULL, NULL);

  free(buf);

  if (error == kDNSServiceErr_NoError) {
    debug(2, "dns-sd: service '%s' successfully registered as '%s'.", name, regtype);
    ret = 0;
  } else {
    warn("dns-sd: DNSServiceRegister error %d registering '%s' as '%s'.", error, name, regtype);
  }

  return ret;
}

static int update_dns_sd_service(DNSServiceRef service, char **records) {
  uint16_t length = 0;
  char *buf = NULL;
  int ret = -1;

  if (service == NULL)
    return 0;

  if (build_dns_sd_txt_buffer(records, &length, &buf) != 0)
    return -1;

  DNSServiceErrorType error = DNSServiceUpdateRecord(service, NULL, 0, length, buf, 0);
  free(buf);

  if (error == kDNSServiceErr_NoError)
    ret = 0;
  else
    warn("dns-sd: DNSServiceUpdateRecord error %d.", error);

  return ret;
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

  mdns_dns_sd_unregister();

  if (secondary_txt_records != NULL) {
    if ((ap2name == NULL) || (config.regtype2 == NULL)) {
      warn("dns-sd: AirPlay 2 TXT records provided without an AirPlay 2 service name or type.");
      return -1;
    }

    if (register_dns_sd_service(&ap2_service, ap2name, config.regtype2, port,
                                secondary_txt_records) != 0)
      return -1;
  }

  if (register_dns_sd_service(&ap1_service, ap1name, config.regtype, port, record) != 0) {
    mdns_dns_sd_unregister();
    return -1;
  }

  return 0;
}

static int mdns_dns_sd_update(char **txt_records, char **secondary_txt_records) {
  int ret = 0;

  if (secondary_txt_records != NULL)
    ret = update_dns_sd_service(ap2_service, secondary_txt_records);

  if ((ret == 0) && (txt_records != NULL))
    ret = update_dns_sd_service(ap1_service, txt_records);

  return ret;
}

static void mdns_dns_sd_unregister(void) {
  if (ap1_service) {
    DNSServiceRefDeallocate(ap1_service);
    ap1_service = NULL;
  }

  if (ap2_service) {
    DNSServiceRefDeallocate(ap2_service);
    ap2_service = NULL;
  }
}

mdns_backend mdns_dns_sd = {.name = "dns-sd",
                            .mdns_register = mdns_dns_sd_register,
                            .mdns_update = mdns_dns_sd_update,
                            .mdns_unregister = mdns_dns_sd_unregister,
                            .mdns_dacp_monitor_start = NULL,
                            .mdns_dacp_monitor_set_id = NULL,
                            .mdns_dacp_monitor_stop = NULL};
