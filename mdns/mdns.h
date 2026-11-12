#ifndef _MDNS_H
#define _MDNS_H

#include "mdns_registry.h"
#include "player.h"
#include <stdint.h>

void mdns_init(void);
void mdns_unregister(void);
void mdns_register(void);
void mdns_dacp_monitor_start();
void mdns_dacp_monitor_stop(void);
void mdns_dacp_monitor_set_id(const char *dacp_id);

void mdns_ls_backends(void);

/* A backend is driven by the mDNS registry (see mdns_registry.h): mdns_register()
 * calls mdns_start() and then installs mdns_publish / mdns_unpublish as the
 * registry's publisher, so records already in the registry are replayed to it.
 *
 *   mdns_start    -- bring up the mDNS machinery; return >= 0 on success.
 *   mdns_publish  -- a record was added or its content changed.
 *   mdns_unpublish-- a record was removed.
 *   mdns_stop     -- called last, after every record has been unpublished
 *                    and the publisher removed. May be NULL.
 * Note that some backends can not update or unpublish a published service.
 */
typedef struct {
  char *name;

  int (*mdns_start)(void);
  void (*mdns_stop)(void);
  void (*mdns_publish)(const mdns_service_record *rec, mdns_put_result why, void *ctx);
  void (*mdns_unpublish)(const mdns_service_record *rec, void *ctx);

  void (*mdns_dacp_monitor_start)();
  void (*mdns_dacp_monitor_set_id)(const char *);
  void (*mdns_dacp_monitor_stop)();
} mdns_backend;

#endif // _MDNS_H
