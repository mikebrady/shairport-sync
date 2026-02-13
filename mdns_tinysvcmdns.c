/*
 * mDNS registration handler. This file is part of Shairport.
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

#include "mdns.h"
#include "common.h"
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "tinysvcmdns.h"

static struct mdnsd *svr = NULL;

static int mdns_tinysvcmdns_register(char *ap1name, __attribute__((unused)) char *ap2name, int port,
                                     __attribute__((unused)) char **txt_records,
                                     __attribute__((unused)) char **secondary_txt_records) {
  struct ifaddrs *ifalist;
  struct ifaddrs *ifa;

  svr = mdnsd_start();
  if (svr == NULL) {
    warn("tinysvcmdns: mdnsd_start() failed");
    return -1;
  }

  // Thanks to Paul Lietar for this
  // room for name + .local + NULL
  char hostname[100 + 6];
  gethostname(hostname, 99);
  // according to POSIX, this may be truncated without a final NULL !
  hostname[99] = 0;

  // will not work if the hostname doesn't end in .local
  char *hostend = hostname + strlen(hostname);
  if ((strlen(hostname) < strlen(".local")) || (strcmp(hostend - 6, ".local") != 0)) {
    strcat(hostname, ".local");
  }

  if (getifaddrs(&ifalist) < 0) {
    warn("tinysvcmdns: getifaddrs() failed");
    return -1;
  }

  ifa = ifalist;

  // Look for an ipv4/ipv6 non-loopback interface to use as the main one.
  for (ifa = ifalist; ifa != NULL; ifa = ifa->ifa_next) {
    // only check for the named interface, if specified
    if ((config.interface == NULL) || (strcmp(config.interface, ifa->ifa_name) == 0)) {

      if (!(ifa->ifa_flags & IFF_LOOPBACK) && ifa->ifa_addr &&
          ifa->ifa_addr->sa_family == AF_INET) {
        uint32_t main_ip = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;

        mdnsd_set_hostname(svr, hostname, main_ip); // TTL should be 120 seconds
        break;
      } else if (!(ifa->ifa_flags & IFF_LOOPBACK) && ifa->ifa_addr &&
                 ifa->ifa_addr->sa_family == AF_INET6) {
        struct in6_addr *addr = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;

        mdnsd_set_hostname_v6(svr, hostname, addr); // TTL should be 120 seconds
        break;
      }
    }
  }

  if (ifa == NULL) {
    warn("tinysvcmdns: no non-loopback ipv4 or ipv6 interface found");
    return -1;
  }

  // Skip the first one, it was already added by set_hostname
  for (ifa = ifa->ifa_next; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_flags & IFF_LOOPBACK) // Skip loop-back interfaces
      continue;
    // only check for the named interface, if specified
    if ((config.interface == NULL) || (strcmp(config.interface, ifa->ifa_name) == 0)) {
      switch (ifa->ifa_addr->sa_family) {
      case AF_INET: { // ipv4
        uint32_t ip = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;
        struct rr_entry *a_e =
            rr_create_a(create_nlabel(hostname), ip); // TTL should be 120 seconds
        mdnsd_add_rr(svr, a_e);
      } break;
      case AF_INET6: { // ipv6
        struct in6_addr *addr = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;
        struct rr_entry *aaaa_e =
            rr_create_aaaa(create_nlabel(hostname), addr); // TTL should be 120 seconds
        mdnsd_add_rr(svr, aaaa_e);
      } break;
      }
    }
  }

  freeifaddrs(ifa);

  char *txtwithoutmetadata[] = {MDNS_RECORD_WITHOUT_METADATA, NULL};
#ifdef CONFIG_METADATA
  char *txtwithmetadata[] = {MDNS_RECORD_WITH_METADATA, NULL};
#endif
  char **txt;

#ifdef CONFIG_METADATA
  if (config.metadata_enabled)
    txt = txtwithmetadata;
  else
#endif

    txt = txtwithoutmetadata;

  if (config.regtype == NULL)
    die("tinysvcmdns: regtype is null");

  char *extendedregtype = malloc(strlen(config.regtype) + strlen(".local") + 1);

  if (extendedregtype == NULL)
    die("tinysvcmdns: could not allocated memory to request a Zeroconf service");

  strcpy(extendedregtype, config.regtype);
  strcat(extendedregtype, ".local");

  struct mdns_service *svc =
      mdnsd_register_svc(svr, ap1name, extendedregtype, port, NULL,
                         (const char **)txt); // TTL should be 75 minutes, i.e. 4500 seconds
  mdns_service_destroy(svc);

  free(extendedregtype);

  return 0;
}

static void mdns_tinysvcmdns_unregister(void) {
  if (svr) {
    mdnsd_stop(svr);
    svr = NULL;
  }
}

mdns_backend mdns_tinysvcmdns = {.name = "tinysvcmdns",
                                 .mdns_register = mdns_tinysvcmdns_register,
                                 .mdns_unregister = mdns_tinysvcmdns_unregister,
                                 .mdns_dacp_monitor_start = NULL,
                                 .mdns_dacp_monitor_set_id = NULL,
                                 .mdns_dacp_monitor_stop = NULL};
