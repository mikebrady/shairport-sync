/*
 * Embedded Avahi client. This file is part of Shairport.
 * Copyright (c) James Laird 2013
 * Additions for maintenance, metadata, AirPlay 2 and IPv6 Copyright (c) Mike Brady 2015--2025
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

#include <pthread.h>
#include <stdlib.h>

#include "config.h"

#include "common.h"
#include "mdns.h"
#include "rtsp.h"
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

AvahiStringList *text_record_string_list = NULL;
AvahiStringList *ap2_text_record_string_list = NULL;

// static AvahiServiceBrowser *sb = NULL;
static AvahiClient *client = NULL;
// static AvahiClient *service_client = NULL;
static AvahiEntryGroup *group = NULL;
static AvahiThreadedPoll *tpoll = NULL;
// static AvahiThreadedPoll *service_poll = NULL;

static char *service_name = NULL;
char *ap2_service_name = NULL;

static int port = 0;

static void resolve_callback(AvahiServiceResolver *r, AVAHI_GCC_UNUSED AvahiIfIndex interface,
                             AVAHI_GCC_UNUSED AvahiProtocol protocol, AvahiResolverEvent event,
                             const char *name, const char *type, const char *domain,
                             __attribute__((unused)) const char *host_name,
#ifdef CONFIG_METADATA
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
      char *dacpid = strstr(name, "iTunes_Ctrl_");
      if (dacpid) {
        dacpid += strlen("iTunes_Ctrl_");
        while (*dacpid == '0')
          dacpid++; // skip any leading zeroes
        if (strcmp(dacpid, dbs->dacp_id) == 0) {
          debug(3, "resolve_callback: client dacp_id \"%s\" dacp port: %u.", dbs->dacp_id, port);
#ifdef CONFIG_DACP_CLIENT
          dacp_monitor_port_update_callback(dacpid, port);
#endif
#ifdef CONFIG_METADATA
          uint32_t nport = lport;
          nport = htonl(nport);
          send_ssnc_metadata('dapo', (const char *)&nport, sizeof(nport), 0);
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
#ifdef CONFIG_DACP_CLIENT
    dacp_browser_struct *dbs = (dacp_browser_struct *)userdata;
    char *dacpid = strstr(name, "iTunes_Ctrl_");
    if (dacpid) {
      dacpid += strlen("iTunes_Ctrl_");
      while (*dacpid == '0')
        dacpid++; // skip any leading zeroes
      if ((dbs->dacp_id) && (strcmp(dacpid, dbs->dacp_id) == 0))
        dacp_monitor_port_update_callback(dbs->dacp_id, 0); // say the port is withdrawn
    } else {
      debug(1, "Browse callback: Can't see a DACP string in a DACP Record!");
    }
#endif
    break;
  case AVAHI_BROWSER_ALL_FOR_NOW:
  case AVAHI_BROWSER_CACHE_EXHAUSTED:
    // debug(1, "(Browser) %s.", event == AVAHI_BROWSER_CACHE_EXHAUSTED ? "CACHE_EXHAUSTED" :
    // "ALL_FOR_NOW");
    break;
  }
}

static void register_service(AvahiClient *c);

static void egroup_callback(AvahiEntryGroup *g, AvahiEntryGroupState state,
                            AVAHI_GCC_UNUSED void *userdata) {
  // debug(1,"egroup_callback, state %d.", state);
  switch (state) {
  case AVAHI_ENTRY_GROUP_ESTABLISHED:
    /* The entry group has been established successfully */
    debug(2, "avahi: service '%s' successfully added.", service_name);
    break;

  case AVAHI_ENTRY_GROUP_COLLISION: {
    char *n;

    /* A service name collision with a remote service
     * happened. Let's pick a new name */
    if (service_name)
      debug(1, "avahi name collision with \"%s\" -- look for another", service_name);
    else
      debug(1, "avahi name collision (name not available) -- look for another");

    n = avahi_alternative_service_name(service_name);
    if (service_name)
      avahi_free(service_name);
    else
      debug(1, "avahi attempt to free a NULL service name");
    service_name = n;

    debug(2, "avahi: service name collision, renaming service to '%s'", service_name);

    /* And try to recreate the services */
    register_service(avahi_entry_group_get_client(g));
    break;
  }

  case AVAHI_ENTRY_GROUP_FAILURE:
    debug(1, "avahi: entry group failure: %s",
          avahi_strerror(avahi_client_errno(avahi_entry_group_get_client(g))));
    break;

  case AVAHI_ENTRY_GROUP_UNCOMMITED:
    debug(2, "avahi: service '%s' group is not yet committed.", service_name);
    break;

  case AVAHI_ENTRY_GROUP_REGISTERING:
    debug(2, "avahi: service '%s' group is registering.", service_name);
    break;

  default:
    debug(1, "avahi: unhandled egroup state: %d", state);
    break;
  }
}

static int deregister_service(AVAHI_GCC_UNUSED AvahiClient *c) {
  int response = 0;
  if (group != NULL) {
    response = avahi_entry_group_free(group);
    group = NULL;
  }
  return response;
}

static void register_service(AvahiClient *c) {
  if (!group)
    group = avahi_entry_group_new(c, egroup_callback, NULL);
  if (!group)
    debug(1, "avahi: avahi_entry_group_new failed");
  else {
    // debug(2, "register_service -- go ahead and register.");
    if (!avahi_entry_group_is_empty(group))
      return;

    int ret = 0;

    AvahiIfIndex selected_interface;
    if (config.interface != NULL)
      selected_interface = config.interface_index;
    else
      selected_interface = AVAHI_IF_UNSPEC;
    if (ap2_text_record_string_list) {
      ret = avahi_entry_group_add_service_strlst(group, selected_interface, AVAHI_PROTO_UNSPEC, 0,
                                                 ap2_service_name, config.regtype2, NULL, NULL,
                                                 port, ap2_text_record_string_list);
      if (ret == AVAHI_ERR_COLLISION) {
        die("Error: AirPlay 2 name \"%s\" is already in use.", ap2_service_name);
      }
    }
    if ((ret == 0) && (text_record_string_list)) {
      ret = avahi_entry_group_add_service_strlst(group, selected_interface, AVAHI_PROTO_UNSPEC, 0,
                                                 service_name, config.regtype, NULL, NULL, port,
                                                 text_record_string_list);
      if (ret == AVAHI_ERR_COLLISION) {
        die("Error: AirPlay 1 name \"%s\" is already in use.", service_name);
      }
    }
    if (ret == 0) {
      ret = avahi_entry_group_commit(group);
      debug(2, "avahi: avahi_entry_group_commit %d", ret);
      if (ret < 0)
        debug(1, "avahi: avahi_entry_group_commit failed");
    } else if (ret < 0) {
      debug(1, "avahi: avahi_entry_group_add_service failed");
    } else {
      debug(1, "avahi: unexpected positive return");
    }
  }
}

static void client_callback(AvahiClient *c, AvahiClientState state,
                            AVAHI_GCC_UNUSED void *userdata) {
  // debug(1,"client_callback, state %d.", state);
  int err;

  switch (state) {
  case AVAHI_CLIENT_S_REGISTERING:
    if (group)
      check_avahi_response(1, avahi_entry_group_reset(group));
    break;

  case AVAHI_CLIENT_S_RUNNING:
    register_service(c);
    break;

  case AVAHI_CLIENT_FAILURE:
    err = avahi_client_errno(c);
    if (err == AVAHI_ERR_DISCONNECTED) {
      debug(1, "avahi client disconnected -- reconnection attempted.");
      if (c) {
        // it seems that the avahi_threaded_poll thread is still running and locked here
        deregister_service(c); // delete the group
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
    debug(2, "avahi: state is AVAHI_CLIENT_S_COLLISION...needs a rename: %s", service_name);
    break;

  case AVAHI_CLIENT_CONNECTING:
    debug(2, "avahi: received AVAHI_CLIENT_CONNECTING");
    break;

  default:
    debug(1, "avahi: unexpected and unhandled avahi client state: %d", state);
    break;
  }
}

static int avahi_update(char **txt_records, char **secondary_txt_records) {
  // debug(1, "avahi_update.");

  /*
    service_name = strdup(ap1name);
    if (ap2name != NULL)
      ap2_service_name = strdup(ap2name);
    port = srvport;
  */
  int err = 0;
  AvahiIfIndex selected_interface;
  if (config.interface != NULL)
    selected_interface = config.interface_index;
  else
    selected_interface = AVAHI_IF_UNSPEC;
  pthread_avahi_threaded_poll_lock_and_push(tpoll);
  // avahi_threaded_poll_lock(tpoll);
  // pthread_cleanup_push(threaded_poll_unlock, (void *)tpoll)

  if (txt_records != NULL) {
    if (text_record_string_list)
      avahi_string_list_free(text_record_string_list);
    text_record_string_list = avahi_string_list_new_from_array((const char **)txt_records, -1);
    err = avahi_entry_group_update_service_txt_strlst(group, selected_interface, AVAHI_PROTO_UNSPEC,
                                                      0, service_name, config.regtype, NULL,
                                                      text_record_string_list);
    if (err != 0)
      debug(1, "avahi_update error updating primary txt records.");
  }

  if (secondary_txt_records != NULL) {
    if (ap2_text_record_string_list)
      avahi_string_list_free(ap2_text_record_string_list);
    ap2_text_record_string_list =
        avahi_string_list_new_from_array((const char **)secondary_txt_records, -1);
    err = avahi_entry_group_update_service_txt_strlst(group, selected_interface, AVAHI_PROTO_UNSPEC,
                                                      0, ap2_service_name, config.regtype2, NULL,
                                                      ap2_text_record_string_list);
    if (err != 0)
      debug(1, "avahi_update error updating secondary txt records.");
  }

  pthread_cleanup_pop(1); // unlock the avahi_threaded_poll_lock
  return 0;
}

static int avahi_register(char *ap1name, char *ap2name, int srvport, char **txt_records,
                          char **secondary_txt_records) {
  // debug(1, "avahi_register.");
  service_name = strdup(ap1name);
  if (ap2name != NULL)
    ap2_service_name = strdup(ap2name);

  text_record_string_list = avahi_string_list_new_from_array((const char **)txt_records, -1);

  if (secondary_txt_records != NULL)
    ap2_text_record_string_list =
        avahi_string_list_new_from_array((const char **)secondary_txt_records, -1);

  port = srvport;

  int err;
  if (!(tpoll = avahi_threaded_poll_new())) {
    warn("couldn't create avahi threaded tpoll!");
    return -1;
  }
  if (!(client = avahi_client_new(avahi_threaded_poll_get(tpoll), AVAHI_CLIENT_NO_FAIL,
                                  client_callback, NULL, &err))) {
    warn("couldn't create avahi client: %s!", avahi_strerror(err));
    return -1;
  }

  if (avahi_threaded_poll_start(tpoll) < 0) {
    warn("couldn't start avahi tpoll thread");
    return -1;
  }

  return 0;
}

static void avahi_unregister(void) {
  debug(2, "avahi_unregister.");
  if (tpoll) {
    debug(2, "avahi: stop the threaded poll.");
    avahi_threaded_poll_stop(tpoll);

    if (client) {
      debug(2, "avahi: free the client.");
      avahi_client_free(client);
      client = NULL;
    } else {
      debug(1, "avahi attempting to unregister a NULL client");
    }
    debug(2, "avahi: free the threaded poll.");
    avahi_threaded_poll_free(tpoll);
    tpoll = NULL;
  } else {
    debug(1, "No avahi threaded poll.");
  }

  if (service_name) {
    debug(2, "avahi: free the service name.");
    free(service_name);
    service_name = NULL;
  } else
    debug(1, "avahi attempt to free NULL service name");

  if (ap2_service_name) {
    debug(2, "avahi: free the ap2 service_name.");
    free(ap2_service_name);
    ap2_service_name = NULL;
  }

  if (text_record_string_list) {
    debug(2, "avahi free text_record_string_list");
    avahi_string_list_free(text_record_string_list);
    text_record_string_list = NULL;
  } else
    debug(1, "avahi attempt to free NULL text_record_string_list");

  if (ap2_text_record_string_list) {
    debug(2, "avahi free ap_2text_record_string_list");
    avahi_string_list_free(ap2_text_record_string_list);
    ap2_text_record_string_list = NULL;
  }
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
                           .mdns_register = avahi_register,
                           .mdns_update = avahi_update,
                           .mdns_unregister = avahi_unregister,
                           .mdns_dacp_monitor_start = avahi_dacp_monitor_start,
                           .mdns_dacp_monitor_set_id = avahi_dacp_monitor_set_id,
                           .mdns_dacp_monitor_stop = avahi_dacp_monitor_stop};
