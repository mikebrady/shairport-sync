/*
 * mDNS registration handler. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * Modifications, updates and additions (c) Mike Brady 2014--2025
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

#include "mdns.h"
#include "common.h"
#include "config.h"
#include <memory.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef CONFIG_AVAHI
extern mdns_backend mdns_avahi;
#endif
#ifdef CONFIG_DNS_SD
extern mdns_backend mdns_dns_sd;
#endif
#ifdef CONFIG_TINYSVCMDNS
extern mdns_backend mdns_tinysvcmdns;
#endif

#ifdef CONFIG_EXTERNAL_MDNS
extern mdns_backend mdns_external_avahi;
extern mdns_backend mdns_external_dns_sd;
#endif

static mdns_backend *mdns_backends[] = {
#ifdef CONFIG_AVAHI
    &mdns_avahi,
#endif
#ifdef CONFIG_DNS_SD
    &mdns_dns_sd,
#endif
#ifdef CONFIG_TINYSVCMDNS
    &mdns_tinysvcmdns,
#endif
#ifdef CONFIG_EXTERNAL_MDNS
    &mdns_external_avahi,
    &mdns_external_dns_sd,
#endif
    NULL};

void mdns_register(char **txt_records, char **secondary_txt_records) {
  char *ap1_service_name = alloca(strlen(config.service_name) + 14);
  char *p = ap1_service_name;
  int i;
  for (i = 0; i < 6; i++) {
    snprintf(p, 3, "%02X", config.ap1_prefix[i]);
    p += 2;
  }
  *p++ = '@';
  strcpy(p, config.service_name);

  mdns_backend **b = NULL;

  if (config.mdns_name != NULL) {
    for (b = mdns_backends; *b; b++) {
      if (strcmp((*b)->name, config.mdns_name) != 0) // Not the one we are looking for
        continue;
      int error = (*b)->mdns_register(ap1_service_name, config.service_name, config.port,
                                      txt_records, secondary_txt_records);
      if (error >= 0) {
        config.mdns = *b;
      }
      break;
    }

    if (*b == NULL)
      warn("mDNS backend not found");
  } else {
    // default -- pick the first back end
    for (b = mdns_backends; *b; b++) {
      int error = (*b)->mdns_register(ap1_service_name, config.service_name, config.port,
                                      txt_records, secondary_txt_records);
      if (error >= 0) {
        config.mdns = *b;
        break;
      }
    }
  }

  if (config.mdns == NULL)
    die("Could not establish mDNS advertisement!");

  mdns_dacp_monitor_start(); // create a dacp monitor thread
}

void mdns_update(char **txt_records, char **secondary_txt_records) {
  if ((config.mdns) && (config.mdns->mdns_update)) {
    config.mdns->mdns_update(txt_records, secondary_txt_records);
  } else
    debug(1, "Can't mdns_update -- no mdns_update registered.");
}

void mdns_unregister(void) {
  mdns_dacp_monitor_stop();
  if (config.mdns) {
    config.mdns->mdns_unregister();
  }
}

void mdns_dacp_monitor_start(void) {
  if ((config.mdns) && (config.mdns->mdns_dacp_monitor_start)) {
    config.mdns->mdns_dacp_monitor_start();
  } else
    debug(3, "Can't start a DACP monitor -- no mdns_dacp_monitor start registered.");
}

void mdns_dacp_monitor_stop() {
  if ((config.mdns) && (config.mdns->mdns_dacp_monitor_stop)) {
    config.mdns->mdns_dacp_monitor_stop();
  } else
    debug(3, "Can't stop a DACP monitor -- no mdns_dacp_monitor_stop registered.");
}

void mdns_dacp_monitor_set_id(const char *dacp_id) {
  if ((config.mdns) && (config.mdns->mdns_dacp_monitor_set_id)) {
    config.mdns->mdns_dacp_monitor_set_id(dacp_id);
  } else
    debug(3, "Can't set dacp_id -- no mdns_dacp_set_id registered.");
}

void mdns_ls_backends(void) {
  mdns_backend **b = NULL;
  printf("Available mDNS backends: \n");
  for (b = mdns_backends; *b; b++) {
    printf("    %s\n", (*b)->name);
  }
}
