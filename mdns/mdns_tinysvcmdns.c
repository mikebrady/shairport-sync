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

/*
 * Registry-driven tinysvcmdns backend.
 *
 * mdns_start brings up the responder (hostname and addresses); each record that
 * reaches mdns_publish is then registered as a service.
 *
 * LIMITATION: The first version of a record is what gets advertised: later
 * changes to the same type are noted and ignored, and a record that is
 * un-published remains advertised until mdns_stop shuts the whole
 * responder down. 
 */

#include "mdns.h"
#include "common.h"
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "tinysvcmdns/tinysvcmdns.h"

static struct mdnsd *svr = NULL;
static char registered[MDNS_MAX_RECORDS][32]; // the types that have been registered

static int registered_index(const char *type) {
  for (int i = 0; i < MDNS_MAX_RECORDS; i++)
    if ((registered[i][0] != '\0') && (strcasecmp(registered[i], type) == 0))
      return i;
  return -1;
}

static int mdns_tinysvcmdns_start(void) {
  struct ifaddrs *ifalist;
  struct ifaddrs *ifa;

  memset(registered, 0, sizeof(registered));
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
    mdnsd_stop(svr);
    svr = NULL;
    return -1;
  }

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
    freeifaddrs(ifalist);
    mdnsd_stop(svr);
    svr = NULL;
    return -1;
  }

  // Skip the first one, it was already added by set_hostname
  for (ifa = ifa->ifa_next; ifa != NULL; ifa = ifa->ifa_next) {
    if (ifa->ifa_flags & IFF_LOOPBACK) // Skip loop-back interfaces
      continue;
    // only check for the named interface, if specified
    if ((config.interface == NULL) || (strcmp(config.interface, ifa->ifa_name) == 0)) {
      if (ifa->ifa_addr == NULL)
        continue;
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

  // the records have their own copies of the addresses
  freeifaddrs(ifalist);
  return 0;
}

static void mdns_tinysvcmdns_publish(const mdns_service_record *rec, mdns_put_result why,
                                     void *ctx) {
  (void)why;
  (void)ctx;
  if (svr == NULL)
    return;

  if (registered_index(rec->service_type) >= 0) {
    debug(2, "tinysvcmdns: can not change a registered service (%s): its advertisement stays as "
             "first published.",
          rec->service_type);
    return;
  }

  int slot = -1;
  for (int i = 0; i < MDNS_MAX_RECORDS; i++)
    if (registered[i][0] == '\0') {
      slot = i;
      break;
    }
  if (slot < 0) {
    warn("tinysvcmdns: no free slot for a record of type %s.", rec->service_type);
    return;
  }

  char fulltype[sizeof(rec->service_type) + 8]; // the type, ".local" and a NUL
  snprintf(fulltype, sizeof fulltype, "%s.local", rec->service_type);

  const char *txt[MDNS_MAX_KV + 1]; // NULL-terminated, pointing into rec
  if (mdns_record_to_strv(rec, txt, MDNS_MAX_KV + 1) < 0) {
    warn("tinysvcmdns: the text records for %s do not fit.", rec->service_type);
    return;
  }

  struct mdns_service *svc = mdnsd_register_svc(svr, rec->instance_name, fulltype, rec->port, NULL,
                                                txt); // TTL should be 75 minutes, i.e. 4500 seconds
  if (svc == NULL) {
    warn("tinysvcmdns: could not register \"%s\" as %s.", rec->instance_name, rec->service_type);
    return;
  }
  mdns_service_destroy(svc); // the records stay with the responder
  snprintf(registered[slot], sizeof registered[slot], "%s", rec->service_type);
}

static void mdns_tinysvcmdns_unpublish(const mdns_service_record *rec, void *ctx) {
  (void)ctx;
  debug(2, "tinysvcmdns: %s can not be withdrawn individually; it stays until the responder stops.",
        rec->service_type);
}

static void mdns_tinysvcmdns_stop(void) {
  if (svr) {
    mdnsd_stop(svr);
    svr = NULL;
  }
  memset(registered, 0, sizeof(registered));
}

mdns_backend mdns_tinysvcmdns = {.name = "tinysvcmdns",
                                 .mdns_start = mdns_tinysvcmdns_start,
                                 .mdns_stop = mdns_tinysvcmdns_stop,
                                 .mdns_publish = mdns_tinysvcmdns_publish,
                                 .mdns_unpublish = mdns_tinysvcmdns_unpublish,
                                 .mdns_dacp_monitor_start = NULL,
                                 .mdns_dacp_monitor_set_id = NULL,
                                 .mdns_dacp_monitor_stop = NULL};
