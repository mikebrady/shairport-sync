/*
 * Embedded Avahi client. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * Additions for maintenance, metadata, AirPlay 2 and IPv6 Copyright (c) Mike Brady 2015--2026
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
 * This version of the Avahi backend is driven by the mDNS registry
 * (mdns_registry.h): it publishes whatever records the registry holds, one
 * Avahi entry group per record, and keeps Avahi in step as records are added,
 * changed and removed. There is no separate "register" and "update": the
 * single operation is "make Avahi match this record". The DACP service
 * browsing below is unchanged.
 */

#include <pthread.h>
#include <stdlib.h>


#include "common.h"
#include "mdns.h"

#ifdef CONFIG_METADATA
#include "metadata/core.h"
#endif

#ifdef CONFIG_DACP_CLIENT
#include "dacp.h"
#endif
#include <string.h>

#include <avahi-client/client.h>
#include <avahi-client/publish.h>
#include <avahi-common/error.h>
#include <avahi-common/malloc.h>
#include <avahi-common/thread-watch.h>

#include <avahi-client/lookup.h>
#include <avahi-common/alternative.h>
#include <avahi-common/address.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <strings.h>

#include "mdns_registry.h"

void threaded_poll_unlock(void *arg) { avahi_threaded_poll_unlock((AvahiThreadedPoll *)arg); }

#define pthread_avahi_threaded_poll_lock_and_push(mu)                                              \
  avahi_threaded_poll_lock(mu);                                                                    \
  pthread_cleanup_push(threaded_poll_unlock, (void *)mu)

#define check_avahi_response(debugLevelArg, veryUnLikelyArgumentName)                              \
  {                                                                                                \
    int rc = veryUnLikelyArgumentName;                                                             \
    if (rc)                                                                                        \
      debug(debugLevelArg, "avahi call response %d at __FILE__, __LINE__)", rc);                   \
  }

typedef struct {
  AvahiServiceBrowser *service_browser;
  char *dacp_id;
} dacp_browser_struct;

dacp_browser_struct private_dbs;

// static AvahiServiceBrowser *sb = NULL;
static AvahiClient *client = NULL;
static AvahiThreadedPoll *tpoll = NULL;

static void resolve_callback(AvahiServiceResolver *r, AVAHI_GCC_UNUSED AvahiIfIndex interface,
                             AVAHI_GCC_UNUSED AvahiProtocol protocol, AvahiResolverEvent event,
                             const char *name, const char *type, const char *domain,
                             __attribute__((unused)) const char *host_name,
#if defined(CONFIG_METADATA) || defined(CONFIG_DACP_CLIENT)
                             __attribute__((unused)) const AvahiAddress *address, uint16_t lport,
#else
                             __attribute__((unused)) const AvahiAddress *address,
                             __attribute__((unused)) uint16_t lport,
#endif
                             __attribute__((unused)) AvahiStringList *txt,
                             __attribute__((unused)) AvahiLookupResultFlags flags, void *userdata) {
  // debug(1,"resolve_callback, event %d.", event);
  assert(r);

  dacp_browser_struct *dbs = (dacp_browser_struct *)userdata;

  /* Called whenever a service has been resolved successfully or timed out */
  switch (event) {
  case AVAHI_RESOLVER_FAILURE:
    debug(3, "(Resolver) Failed to resolve service '%s' of type '%s' in domain '%s': %s.", name,
          type, domain, avahi_strerror(avahi_client_errno(avahi_service_resolver_get_client(r))));
    break;
  case AVAHI_RESOLVER_FOUND: {
    //    char a[AVAHI_ADDRESS_STR_MAX], *t;
    debug(3, "resolve_callback: Service '%s' of type '%s' in domain '%s':", name, type, domain);
    if (dbs->dacp_id) {
      const char *dacpid = strstr(name, "iTunes_Ctrl_");
      if (dacpid) {
        dacpid += strlen("iTunes_Ctrl_");
        while (*dacpid == '0')
          dacpid++; // skip any leading zeroes
        if (strcmp(dacpid, dbs->dacp_id) == 0) {
          debug(4, "resolve_callback: client dacp_id \"%s\" dacp port: %u.", dbs->dacp_id, lport);
#ifdef CONFIG_DACP_CLIENT
          dacp_monitor_port_update_callback(dacpid, lport);
#endif
#ifdef CONFIG_METADATA
          char port_in_chars[16];
          snprintf(port_in_chars, 7, "%u", lport);
          send_ssnc_metadata('dapo', port_in_chars, strlen(port_in_chars), 0);
#endif
        }
      } else {
        debug(1, "Resolve callback: Can't see a DACP string in a DACP Record!");
      }
    }
  }
  }
  // debug(1,"service resolver freed by resolve_callback");
  avahi_service_resolver_free(r);
}

static void browse_callback(AvahiServiceBrowser *b, AvahiIfIndex interface, AvahiProtocol protocol,
                            AvahiBrowserEvent event, const char *name, const char *type,
                            const char *domain, AVAHI_GCC_UNUSED AvahiLookupResultFlags flags,
                            void *userdata) {
  // debug(1,"browse_callback, event %d.", event);
  assert(b);
  /* Called whenever a new services becomes available on the LAN or is removed from the LAN */
  switch (event) {
  case AVAHI_BROWSER_FAILURE:
    warn("avahi: browser failure: %s.",
         avahi_strerror(avahi_client_errno(avahi_service_browser_get_client(b))));
    avahi_threaded_poll_quit(tpoll);
    break;
  case AVAHI_BROWSER_NEW:
    // debug(1, "browse_callback: avahi_service_resolver_new for service '%s' of type '%s' in domain
    // '%s'.", name, type, domain);
    /* We ignore the returned resolver object. In the callback
       function we free it. If the server is terminated before
       the callback function is called the server will free
       the resolver for us. */
    if (!(avahi_service_resolver_new(client, interface, protocol, name, type, domain,
                                     AVAHI_PROTO_UNSPEC, 0, resolve_callback, userdata)))
      debug(1, "Failed to resolve service '%s': %s.", name,
            avahi_strerror(avahi_client_errno(client)));
    break;
  case AVAHI_BROWSER_REMOVE:
    debug(3, "(Browser) REMOVE: service '%s' of type '%s' in domain '%s'.", name, type, domain);
    dacp_browser_struct *dbs = (dacp_browser_struct *)userdata;
    const char *dacpid = strstr(name, "iTunes_Ctrl_");
    if (dacpid) {
      dacpid += strlen("iTunes_Ctrl_");
      while (*dacpid == '0')
        dacpid++; // skip any leading zeroes
      if ((dbs->dacp_id) && (strcmp(dacpid, dbs->dacp_id) == 0)) {
#ifdef CONFIG_DACP_CLIENT
        dacp_monitor_port_update_callback(dbs->dacp_id, 0); // say the port is withdrawn
#endif
#ifdef CONFIG_METADATA
        send_ssnc_metadata('dapo', "0", strlen("0"), 0);
#endif
      }
    } else {
      debug(1, "Browse callback: Can't see a DACP string in a DACP Record!");
    }
    break;
  case AVAHI_BROWSER_ALL_FOR_NOW:
  case AVAHI_BROWSER_CACHE_EXHAUSTED:
    // debug(1, "(Browser) %s.", event == AVAHI_BROWSER_CACHE_EXHAUSTED ? "CACHE_EXHAUSTED" :
    // "ALL_FOR_NOW");
    break;
  }
}

/* ------------------------------------------------------------------------- *
 * Publishing: one Avahi entry group per record in the mDNS registry.
 *
 * A group per record (rather than one group for everything) means a record can
 * be added, re-registered or withdrawn without disturbing the others: Avahi
 * does not allow adding to a committed group, and resetting a shared group
 * would make every service vanish and reappear.
 *
 * All of this state is protected by the Avahi threaded-poll lock. Avahi's own
 * callbacks already hold it; code running on any other thread must take it
 * (see avahi_publish and avahi_unpublish).
 * ------------------------------------------------------------------------- */

typedef struct {
  int used;
  mdns_service_record want; // the latest record from the registry
  char *name;               // instance name in use (avahi_malloc'd). Differs from
                            // want.instance_name after a collision rename.
  AvahiEntryGroup *group;   // this record's own entry group, or NULL
  int registered;           // the service has been added to the group and committed
  uint16_t reg_port;        // the port it was registered with
} entry;

static entry entries[MDNS_MAX_RECORDS];
static void egroup_callback_entry(AvahiEntryGroup *g, AvahiEntryGroupState state, void *userdata);
static int client_running = 0; // the Avahi daemon is up and accepting registrations

static entry *find_entry(const char *type) {
  for (int i = 0; i < MDNS_MAX_RECORDS; i++)
    if (entries[i].used && (strcasecmp(entries[i].want.service_type, type) == 0))
      return &entries[i];
  return NULL;
}

static entry *alloc_entry(void) {
  for (int i = 0; i < MDNS_MAX_RECORDS; i++)
    if (!entries[i].used) {
      memset(&entries[i], 0, sizeof(entry));
      entries[i].used = 1;
      return &entries[i];
    }
  return NULL;
}

static void free_entry(entry *e) {
  if (e->group != NULL)
    avahi_entry_group_free(e->group); // withdraws the service
  if (e->name != NULL)
    avahi_free(e->name);
  memset(e, 0, sizeof(entry));
}

static AvahiIfIndex selected_interface(void) {
  return (config.interface != NULL) ? config.interface_index : AVAHI_IF_UNSPEC;
}

// Builds an AvahiStringList from the record's attributes. Returns 0 on success
// (*out may be NULL for an empty record), -1 if out of memory.
static int record_to_strlst(const mdns_service_record *r, AvahiStringList **out) {
  AvahiStringList *l = NULL;

  for (size_t i = 0; i < r->n; i++) {
    AvahiStringList *t = avahi_string_list_add(l, mdns_record_get(r, i));
    if (t == NULL) {
      avahi_string_list_free(l);
      return -1;
    }
    l = t;
  }
  *out = l;
  return 0;
}

// Adds e's service to e->group (which must be empty) and commits it.
// Returns 0 on success, or a negative Avahi error.
static int add_service(entry *e) {
  AvahiIfIndex iface = selected_interface();

  // When general.address / --address binds this instance to one local IP,
  // publish its service against a hostname of its own rather than the system's
  // default hostname, so that clients connect to the instance they selected.
  // NULL => default Avahi behaviour, unchanged.
  //
  // The label carries the address, the record's own port and a short tag taken
  // from its service type. The address alone is not unique per instance, and
  // now that every record has its own port and its own entry group, the tag
  // keeps two records that happen to share a port from trying to publish the
  // same address record from different groups. Dots and colons in the address
  // become hyphens so an IPv6 literal yields a valid single DNS label.
  char instance_hostname[256];
  const char *host_to_use = NULL;
  if ((config.address != NULL) && (config.address[0] != '\0')) {
    AvahiAddress addr;
    memset(&addr, 0, sizeof(addr));
    if (avahi_address_parse(config.address, AVAHI_PROTO_UNSPEC, &addr) != NULL) {
      char address_label[INET6_ADDRSTRLEN];
      strncpy(address_label, config.address, sizeof(address_label) - 1);
      address_label[sizeof(address_label) - 1] = '\0';
      for (size_t i = 0; address_label[i] != '\0'; i++)
        if ((address_label[i] == '.') || (address_label[i] == ':'))
          address_label[i] = '-';

      char tag[16]; // "_raop._tcp" -> "raop"
      size_t t = 0;
      const char *p = e->want.service_type;
      if (*p == '_')
        p++;
      while ((*p != '\0') && (*p != '.') && (t < sizeof(tag) - 1)) {
        tag[t++] = isalnum((unsigned char)*p) ? (char)tolower((unsigned char)*p) : '-';
        p++;
      }
      tag[t] = '\0';

      char label[64]; // a DNS label is at most 63 bytes
      int len = snprintf(label, sizeof(label), "shairport-%s-%u-%s", address_label,
                         (unsigned)e->want.port, tag);
      if ((len < 0) || (len > 63)) { // did not fit
        debug(1,
              "avahi: hostname label for %s would be too long -- falling back to the default "
              "host advertisement.",
              e->want.service_type);
      } else {
        snprintf(instance_hostname, sizeof(instance_hostname), "%s.local", label);

        // The Avahi daemon already auto-publishes an address record mapping this
        // IP to the system's default hostname. Publishing a second hostname for
        // that address is not a collision as far as this API is concerned.
        // AVAHI_PUBLISH_NO_REVERSE is used because only forward (A/AAAA)
        // resolution is needed -- asking for the reverse mapping would collide
        // with the PTR record the daemon already publishes. (ALLOW_MULTIPLE is
        // rejected here with AVAHI_ERR_INVALID_FLAGS: it is accepted only by
        // avahi_entry_group_add_record().)
        int aret = avahi_entry_group_add_address(e->group, iface, addr.proto,
                                                 AVAHI_PUBLISH_NO_REVERSE, instance_hostname, &addr);
        if (aret == 0) {
          host_to_use = instance_hostname;
          debug(1, "avahi: published distinct hostname \"%s\" -> \"%s\".", instance_hostname,
                config.address);
        } else {
          debug(1,
                "avahi: avahi_entry_group_add_address failed (%d) for hostname \"%s\" -- falling "
                "back to the default host advertisement.",
                aret, instance_hostname);
        }
      }
    } else {
      debug(1,
            "avahi: avahi_address_parse failed for \"%s\" -- falling back to the default host "
            "advertisement.",
            config.address);
    }
  }

  AvahiStringList *txt = NULL;
  if (record_to_strlst(&e->want, &txt) != 0) {
    debug(1, "avahi: out of memory building the text records for %s.", e->want.service_type);
    return AVAHI_ERR_NO_MEMORY;
  }

  int ret = avahi_entry_group_add_service_strlst(e->group, iface, AVAHI_PROTO_UNSPEC, 0, e->name,
                                                 e->want.service_type, NULL, host_to_use,
                                                 e->want.port, txt);
  avahi_string_list_free(txt); // Avahi has its own copy
  if (ret == AVAHI_ERR_COLLISION)
    die("Error: the mDNS name \"%s\" (%s) is already in use.", e->name, e->want.service_type);

  if (ret == 0) {
    ret = avahi_entry_group_commit(e->group);
    debug(2, "avahi: avahi_entry_group_commit %d for %s", ret, e->want.service_type);
    if (ret < 0)
      debug(1, "avahi: avahi_entry_group_commit failed (%s)", avahi_strerror(ret));
  } else {
    debug(1, "avahi: avahi_entry_group_add_service failed for %s (%s)", e->want.service_type,
          avahi_strerror(ret));
  }
  return ret;
}

// Changes only the text records of an already-registered service. This is the
// cheap path used for the frequent changes (status flags, group information):
// the service does not disappear and reappear.
static void update_txt(entry *e) {
  AvahiStringList *txt = NULL;
  if (record_to_strlst(&e->want, &txt) != 0) {
    debug(1, "avahi: out of memory building the text records for %s.", e->want.service_type);
    return;
  }
  int err = avahi_entry_group_update_service_txt_strlst(
      e->group, selected_interface(), AVAHI_PROTO_UNSPEC, 0, e->name, e->want.service_type, NULL,
      txt);
  avahi_string_list_free(txt);
  if (err != 0)
    debug(1, "avahi_entry_group_update_service_txt_strlst error \"%s\" updating %s.",
          avahi_strerror(err), e->want.service_type);
}

// THE operation: make Avahi match e->want. Caller holds the poll lock and the
// client must be running.
static void sync_entry(entry *e) {
  if (!e->used || (e->name == NULL))
    return;

  if (e->registered && (e->reg_port == e->want.port)) {
    update_txt(e); // same name and port: only the text can have changed
    return;
  }

  // New record, changed port or name, or a re-registration after the daemon
  // restarted or a collision rename: (re)create the service in an empty group.
  if (e->group != NULL) {
    check_avahi_response(1, avahi_entry_group_reset(e->group));
  } else {
    e->group = avahi_entry_group_new(client, egroup_callback_entry, e);
    if (e->group == NULL) {
      debug(1, "avahi: avahi_entry_group_new failed for %s", e->want.service_type);
      return;
    }
  }
  e->registered = 0;
  if (add_service(e) == 0) {
    e->registered = 1;
    e->reg_port = e->want.port;
  }
}

static void sync_all(void) {
  for (int i = 0; i < MDNS_MAX_RECORDS; i++)
    if (entries[i].used)
      sync_entry(&entries[i]);
}

static void egroup_callback_entry(AvahiEntryGroup *g, AvahiEntryGroupState state, void *userdata) {
  entry *e = (entry *)userdata;
  if ((e == NULL) || !e->used || (e->name == NULL))
    return;
  switch (state) {
  case AVAHI_ENTRY_GROUP_ESTABLISHED:
    /* The entry group has been established successfully */
    debug(2, "avahi: service '%s' (%s) successfully added.", e->name, e->want.service_type);
    break;

  case AVAHI_ENTRY_GROUP_COLLISION: {
    /* A service name collision with a remote service happened. Pick a new name
     * and try to recreate the service. The new name lives here, in the
     * backend's own entry, and is deliberately not written back to the
     * registry: that would take the registry's lock while the Avahi lock is
     * held, the opposite order to avahi_publish(), and could deadlock. */
    debug(1, "avahi name collision with \"%s\" (%s) -- look for another", e->name,
          e->want.service_type);
    char *n = avahi_alternative_service_name(e->name);
    if (n == NULL) {
      debug(1, "avahi: out of memory choosing an alternative name for \"%s\"", e->name);
      break;
    }
    avahi_free(e->name);
    e->name = n;
    e->registered = 0;
    debug(2, "avahi: service name collision, renaming service to '%s'", e->name);
    sync_entry(e);
    break;
  }

  case AVAHI_ENTRY_GROUP_FAILURE:
    debug(1, "avahi: entry group failure for '%s': %s", e->name,
          avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));
    break;

  case AVAHI_ENTRY_GROUP_UNCOMMITED:
    debug(2, "avahi: service '%s' group is not yet committed.", e->name);
    break;

  case AVAHI_ENTRY_GROUP_REGISTERING:
    debug(2, "avahi: service '%s' group is registering.", e->name);
    break;

  default:
    debug(1, "avahi: unhandled egroup state: %d", state);
    break;
  }
}

static void client_callback(AvahiClient *c, AvahiClientState state,
                            AVAHI_GCC_UNUSED void *userdata) {
  // debug(1,"client_callback, state %d.", state);
  int err;

  // Avahi can call this before avahi_client_new() has returned, i.e. before the
  // global `client` has been assigned, and sync_entry() uses that global.
  client = c;

  switch (state) {
  case AVAHI_CLIENT_S_REGISTERING:
    // The daemon is (re)registering the host: withdraw our services; they are
    // added again from the stored records when the daemon is running.
    client_running = 0;
    for (int i = 0; i < MDNS_MAX_RECORDS; i++)
      if (entries[i].used) {
        if (entries[i].group)
          check_avahi_response(1, avahi_entry_group_reset(entries[i].group));
        entries[i].registered = 0;
      }
    break;

  case AVAHI_CLIENT_S_RUNNING:
    client_running = 1;
    sync_all(); // publishes everything that arrived while we were not running
    break;

  case AVAHI_CLIENT_FAILURE:
    err = avahi_client_errno(c);
    if (err == AVAHI_ERR_DISCONNECTED) {
      debug(1, "avahi client disconnected -- reconnection attempted.");
      if (c) {
        // it seems that the avahi_threaded_poll thread is still running and locked here
        client_running = 0;
        for (int i = 0; i < MDNS_MAX_RECORDS; i++)
          if (entries[i].used) { // the groups belong to the old client: delete them
            if (entries[i].group) {
              avahi_entry_group_free(entries[i].group);
              entries[i].group = NULL;
            }
            entries[i].registered = 0;
          }
        dacp_browser_struct *dbs = &private_dbs;
        if (dbs->service_browser) {
          int rc = avahi_service_browser_free(dbs->service_browser); // delete the service browser
          if (rc != 0)
            debug(1,
                  "Error %d freeing the Avahi service browser after the Avahi client has been "
                  "disconnected.",
                  rc);
          dbs->service_browser = NULL;
        }
        avahi_client_free(c); // delete the client
      }
      if (!(client = avahi_client_new(avahi_threaded_poll_get(tpoll), AVAHI_CLIENT_NO_FAIL,
                                      client_callback, userdata, &err))) {
        warn("avahi: failed to create a replacement client object: %s", avahi_strerror(err));
        avahi_threaded_poll_quit(tpoll);
      }
    } else {
      warn("avahi: client failure: %s", avahi_strerror(err));
      avahi_threaded_poll_quit(tpoll);
    }
    break;

  case AVAHI_CLIENT_S_COLLISION:
    debug(2, "avahi: state is AVAHI_CLIENT_S_COLLISION...needs a rename");
    client_running = 0;
    break;

  case AVAHI_CLIENT_CONNECTING:
    debug(2, "avahi: received AVAHI_CLIENT_CONNECTING");
    break;

  default:
    debug(1, "avahi: unexpected and unhandled avahi client state: %d", state);
    break;
  }
}

/* ---- the registry's publisher: called on whichever thread changed a record ---- */

static void avahi_publish(const mdns_service_record *rec, mdns_put_result why, void *ctx) {
  (void)why; // added or replaced: either way, make Avahi match the record
  (void)ctx;
  if (tpoll == NULL) {
    debug(1, "avahi: a record for %s arrived but Avahi is not running.", rec->service_type);
    return;
  }
  entry *e;
  pthread_avahi_threaded_poll_lock_and_push(tpoll);
  e = find_entry(rec->service_type);
  if (e == NULL) {
    e = alloc_entry();
    if (e != NULL)
      e->name = avahi_strdup(rec->instance_name);
  } else if (strcmp(e->want.instance_name, rec->instance_name) != 0) {
    avahi_free(e->name); // a new instance name replaces any collision rename
    e->name = avahi_strdup(rec->instance_name);
    e->registered = 0; // a changed name needs a re-registration
  }
  if (e == NULL) {
    debug(1, "avahi: no free slot for a record of type %s.", rec->service_type);
  } else if (e->name == NULL) {
    debug(1, "avahi: out of memory publishing a record of type %s.", rec->service_type);
    free_entry(e);
  } else {
    e->want = *rec;
    if (client_running)
      sync_entry(e);
    // else: client_callback publishes it when the daemon is running
  }
  pthread_cleanup_pop(1); // unlock the avahi_threaded_poll_lock
}

static void avahi_unpublish(const mdns_service_record *rec, void *ctx) {
  (void)ctx;
  if (tpoll == NULL)
    return;
  entry *e;
  pthread_avahi_threaded_poll_lock_and_push(tpoll);
  e = find_entry(rec->service_type);
  if (e != NULL)
    free_entry(e); // frees the group, which withdraws the service
  pthread_cleanup_pop(1); // unlock the avahi_threaded_poll_lock
}

/* ---- lifecycle ---- */

static int avahi_start(void) {
  memset(entries, 0, sizeof(entries));
  client_running = 0;

  int err;
  if (!(tpoll = avahi_threaded_poll_new())) {
    warn("couldn't create avahi threaded tpoll!");
    return -1;
  }
  if (!(client = avahi_client_new(avahi_threaded_poll_get(tpoll), AVAHI_CLIENT_NO_FAIL,
                                  client_callback, NULL, &err))) {
    warn("couldn't create avahi client: %s!", avahi_strerror(err));
    avahi_threaded_poll_free(tpoll);
    tpoll = NULL;
    return -1;
  }

  if (avahi_threaded_poll_start(tpoll) < 0) {
    warn("couldn't start avahi tpoll thread");
    avahi_client_free(client);
    client = NULL;
    avahi_threaded_poll_free(tpoll);
    tpoll = NULL;
    return -1;
  }
  return 0;
}

// Called after the registry has unpublished every record and its publisher has
// been removed, so no publish or unpublish can be running.
static void avahi_stop(void) {
  debug(2, "avahi_stop.");
  if (tpoll) {
    debug(2, "avahi: stop the threaded poll.");
    avahi_threaded_poll_stop(tpoll);

    if (client) {
      debug(2, "avahi: free the client."); // this also frees any remaining entry groups
      avahi_client_free(client);
      client = NULL;
    } else {
      debug(1, "avahi attempting to stop with a NULL client");
    }
    debug(2, "avahi: free the threaded poll.");
    avahi_threaded_poll_free(tpoll);
    tpoll = NULL;
  } else {
    debug(1, "No avahi threaded poll.");
  }

  for (int i = 0; i < MDNS_MAX_RECORDS; i++) {
    if (entries[i].name)
      avahi_free(entries[i].name);
    memset(&entries[i], 0, sizeof(entry));
  }
  client_running = 0;
}

void avahi_dacp_monitor_start(void) {
  // debug(1, "avahi_dacp_monitor_start.");
  memset((void *)&private_dbs, 0, sizeof(dacp_browser_struct));
  debug(2, "avahi_dacp_monitor_start Avahi DACP monitor successfully started");
  return;
}

void avahi_dacp_monitor_set_id(const char *dacp_id) {
  // debug(1, "avahi_dacp_monitor_set_id: Search for DACP ID \"%s\".", t);
  dacp_browser_struct *dbs = &private_dbs;

  if (((dbs->dacp_id) && (dacp_id) && (strcmp(dbs->dacp_id, dacp_id) == 0)) ||
      ((dbs->dacp_id == NULL) && (dacp_id == NULL))) {
    debug(3, "no change...");
  } else {
    if (dbs->dacp_id)
      free(dbs->dacp_id);
    if (dacp_id == NULL)
      dbs->dacp_id = NULL;
    else {
      char *t = strdup(dacp_id);
      if (t) {
        dbs->dacp_id = t;
        pthread_avahi_threaded_poll_lock_and_push(tpoll);
        // avahi_threaded_poll_lock(tpoll);
        if (dbs->service_browser)
          avahi_service_browser_free(dbs->service_browser);

        if (!(dbs->service_browser =
                  avahi_service_browser_new(client, AVAHI_IF_UNSPEC, AVAHI_PROTO_UNSPEC,
                                            "_dacp._tcp", NULL, 0, browse_callback, (void *)dbs))) {
          warn("failed to create avahi service browser: %s\n",
               avahi_strerror(avahi_client_errno(client)));
        }
        pthread_cleanup_pop(1); // unlock the avahi_threaded_poll_lock
        // avahi_threaded_poll_unlock(tpoll);
        debug(2, "dacp_monitor for \"%s\"", dacp_id);
      } else {
        warn("avahi_dacp_set_id: can not allocate a dacp_id string in dacp_browser_struct.");
      }
    }
  }
}

void avahi_dacp_monitor_stop() {
  // debug(1, "avahi_dacp_monitor_stop");
  dacp_browser_struct *dbs = &private_dbs;
  // stop and dispose of everything
  pthread_avahi_threaded_poll_lock_and_push(tpoll);
  // avahi_threaded_poll_lock(tpoll);
  if (dbs->service_browser) {
    avahi_service_browser_free(dbs->service_browser);
    dbs->service_browser = NULL;
  }
  pthread_cleanup_pop(1); // unlock the avahi_threaded_poll_lock
  // avahi_threaded_poll_unlock(tpoll);
  free(dbs->dacp_id);
  debug(2, "avahi_dacp_monitor_stop Avahi DACP monitor successfully stopped");
}

mdns_backend mdns_avahi = {.name = "avahi",
                           .mdns_start = avahi_start,
                           .mdns_stop = avahi_stop,
                           .mdns_publish = avahi_publish,
                           .mdns_unpublish = avahi_unpublish,
                           .mdns_dacp_monitor_start = avahi_dacp_monitor_start,
                           .mdns_dacp_monitor_set_id = avahi_dacp_monitor_set_id,
                           .mdns_dacp_monitor_stop = avahi_dacp_monitor_stop};
