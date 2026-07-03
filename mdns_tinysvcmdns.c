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
#ifdef CONFIG_DACP_CLIENT
#include "dacp.h"
#endif
#ifdef CONFIG_METADATA
#include "metadata/core.h"
#endif
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pthread.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "tinysvcmdns.h"

static struct mdnsd *svr = NULL;

#ifdef CONFIG_DACP_CLIENT
static pthread_mutex_t dacp_monitor_lock = PTHREAD_MUTEX_INITIALIZER;
static char *dacp_monitor_id = NULL;

static char *nlabel_to_str_no_trailing_dot(const uint8_t *name) {
  char *name_string = nlabel_to_str(name);
  if (name_string != NULL) {
    size_t len = strlen(name_string);
    if ((len > 0) && (name_string[len - 1] == '.'))
      name_string[len - 1] = '\0';
  }
  return name_string;
}

static int service_name_matches_dacp_id(const char *service_name, const char *dacp_id) {
  const char prefix[] = "iTunes_Ctrl_";
  const char suffix[] = "._dacp._tcp.local";
  size_t service_name_len = strlen(service_name);
  size_t prefix_len = strlen(prefix);
  size_t suffix_len = strlen(suffix);

  if ((dacp_id == NULL) || (service_name_len <= prefix_len + suffix_len) ||
      (strncmp(service_name, prefix, prefix_len) != 0) ||
      (strcmp(service_name + service_name_len - suffix_len, suffix) != 0))
    return 0;

  const char *service_dacp_id = service_name + prefix_len;
  size_t service_dacp_id_len = service_name_len - prefix_len - suffix_len;
  while ((service_dacp_id_len > 0) && (*service_dacp_id == '0')) {
    service_dacp_id++;
    service_dacp_id_len--;
  }

  return (strlen(dacp_id) == service_dacp_id_len) &&
         (strncmp(service_dacp_id, dacp_id, service_dacp_id_len) == 0);
}

static void mdns_tinysvcmdns_note_dacp_port(const char *dacp_id, uint16_t port) {
  dacp_monitor_port_update_callback(dacp_id, port);
#ifdef CONFIG_METADATA
  char port_in_chars[16];
  snprintf(port_in_chars, sizeof(port_in_chars), "%u", port);
  send_ssnc_metadata('dapo', port_in_chars, strlen(port_in_chars), 0);
#endif
}

static void process_dacp_record(struct rr_entry *rr, const char *dacp_id) {
  if (rr == NULL)
    return;

  if (rr->type == RR_PTR) {
    char *name = nlabel_to_str_no_trailing_dot(rr->name);
    if ((name != NULL) && (strcmp(name, "_dacp._tcp.local") == 0)) {
      char *service_name = nlabel_to_str_no_trailing_dot(MDNS_RR_GET_PTR_NAME(rr));
      if ((service_name != NULL) && service_name_matches_dacp_id(service_name, dacp_id)) {
        if (rr->ttl == 0) {
          mdns_tinysvcmdns_note_dacp_port(dacp_id, 0);
        } else if (svr != NULL) {
          mdnsd_send_query(svr, service_name, RR_SRV);
        }
      }
      free(service_name);
    }
    free(name);
  } else if (rr->type == RR_SRV) {
    char *service_name = nlabel_to_str_no_trailing_dot(rr->name);
    if ((service_name != NULL) && service_name_matches_dacp_id(service_name, dacp_id)) {
      mdns_tinysvcmdns_note_dacp_port(dacp_id, rr->ttl == 0 ? 0 : rr->data.SRV.port);
    }
    free(service_name);
  }
}

static void process_dacp_record_list(struct rr_list *records, const char *dacp_id) {
  for (; records != NULL; records = records->next)
    process_dacp_record(records->e, dacp_id);
}

static void mdns_tinysvcmdns_packet_callback(struct mdns_pkt *pkt,
                                             __attribute__((unused)) void *userdata) {
  char *dacp_id = NULL;
  pthread_mutex_lock(&dacp_monitor_lock);
  if (dacp_monitor_id != NULL)
    dacp_id = strdup(dacp_monitor_id);
  pthread_mutex_unlock(&dacp_monitor_lock);

  if (dacp_id == NULL)
    return;

  process_dacp_record_list(pkt->rr_ans, dacp_id);
  process_dacp_record_list(pkt->rr_auth, dacp_id);
  process_dacp_record_list(pkt->rr_add, dacp_id);

  free(dacp_id);
}
#endif

static int mdns_tinysvcmdns_register(char *ap1name, char *ap2name, int port, char **txt_records,
                                     char **secondary_txt_records) {
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

  // Look for an ipv4 non-loopback interface to use as the main one.
  for (ifa = ifalist; ifa != NULL; ifa = ifa->ifa_next) {
    // only check for the named interface, if specified
    if ((config.interface == NULL) || (strcmp(config.interface, ifa->ifa_name) == 0)) {
      if (!(ifa->ifa_flags & IFF_LOOPBACK) && ifa->ifa_addr &&
          ifa->ifa_addr->sa_family == AF_INET) {
        uint32_t main_ip = ((struct sockaddr_in *)ifa->ifa_addr)->sin_addr.s_addr;

        mdnsd_set_hostname(svr, hostname, main_ip); // TTL should be 120 seconds
        if (config.interface != NULL)
          mdnsd_set_ipv4_interface(svr, main_ip);
        break;
      }
    }
  }

  // If no ipv4 address was found, try ipv6.
  if (ifa == NULL) {
    for (ifa = ifalist; ifa != NULL; ifa = ifa->ifa_next) {
      if ((config.interface == NULL) || (strcmp(config.interface, ifa->ifa_name) == 0)) {
        if (!(ifa->ifa_flags & IFF_LOOPBACK) && ifa->ifa_addr &&
            ifa->ifa_addr->sa_family == AF_INET6) {
        struct in6_addr *addr = &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr;

        mdnsd_set_hostname_v6(svr, hostname, addr); // TTL should be 120 seconds
        break;
        }
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

  freeifaddrs(ifalist);

  if (config.regtype == NULL)
    die("tinysvcmdns: regtype is null");

  char *extendedregtype = malloc(strlen(config.regtype) + strlen(".local") + 1);

  if (extendedregtype == NULL)
    die("tinysvcmdns: could not allocated memory to request a Zeroconf service");

  strcpy(extendedregtype, config.regtype);
  strcat(extendedregtype, ".local");

  struct mdns_service *svc =
      mdnsd_register_svc(svr, ap1name, extendedregtype, port, NULL,
                         (const char **)txt_records); // TTL should be 75 minutes, i.e. 4500 seconds
  mdns_service_destroy(svc);

  free(extendedregtype);

  if ((ap2name != NULL) && (secondary_txt_records != NULL)) {
    if (config.regtype2 == NULL)
      die("tinysvcmdns: regtype2 is null");

    extendedregtype = malloc(strlen(config.regtype2) + strlen(".local") + 1);

    if (extendedregtype == NULL)
      die("tinysvcmdns: could not allocated memory to request a secondary Zeroconf service");

    strcpy(extendedregtype, config.regtype2);
    strcat(extendedregtype, ".local");

    svc = mdnsd_register_svc(svr, ap2name, extendedregtype, port, NULL,
                            (const char **)secondary_txt_records);
    mdns_service_destroy(svc);

    free(extendedregtype);
  }

  return 0;
}

static void mdns_tinysvcmdns_unregister(void) {
  if (svr) {
    mdnsd_set_packet_callback(svr, NULL, NULL);
    mdnsd_stop(svr);
    svr = NULL;
  }
}

#ifdef CONFIG_DACP_CLIENT
static void mdns_tinysvcmdns_dacp_monitor_start(void) {
  if (svr != NULL)
    mdnsd_set_packet_callback(svr, mdns_tinysvcmdns_packet_callback, NULL);
}

static void mdns_tinysvcmdns_dacp_monitor_set_id(const char *dacp_id) {
  pthread_mutex_lock(&dacp_monitor_lock);
  free(dacp_monitor_id);
  dacp_monitor_id = dacp_id == NULL ? NULL : strdup(dacp_id);
  pthread_mutex_unlock(&dacp_monitor_lock);

  if ((svr != NULL) && (dacp_id != NULL) && (strlen(dacp_id) > 0))
    mdnsd_send_query(svr, "_dacp._tcp.local", RR_PTR);
}

static void mdns_tinysvcmdns_dacp_monitor_stop(void) {
  if (svr != NULL)
    mdnsd_set_packet_callback(svr, NULL, NULL);

  pthread_mutex_lock(&dacp_monitor_lock);
  free(dacp_monitor_id);
  dacp_monitor_id = NULL;
  pthread_mutex_unlock(&dacp_monitor_lock);
}
#endif

mdns_backend mdns_tinysvcmdns = {.name = "tinysvcmdns",
                                 .mdns_register = mdns_tinysvcmdns_register,
                                 .mdns_unregister = mdns_tinysvcmdns_unregister,
#ifdef CONFIG_DACP_CLIENT
                                 .mdns_dacp_monitor_start =
                                     mdns_tinysvcmdns_dacp_monitor_start,
                                 .mdns_dacp_monitor_set_id =
                                     mdns_tinysvcmdns_dacp_monitor_set_id,
                                 .mdns_dacp_monitor_stop =
                                     mdns_tinysvcmdns_dacp_monitor_stop};
#else
                                 .mdns_dacp_monitor_start = NULL,
                                 .mdns_dacp_monitor_set_id = NULL,
                                 .mdns_dacp_monitor_stop = NULL};
#endif
