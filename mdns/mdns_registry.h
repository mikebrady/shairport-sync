/*
 * mdns_registry.h -- the one application-wide set of mDNS service records.
 *
 * Any thread may add, change or remove records. The registry holds the
 * current state and tells the mDNS backend (Avahi, dns_sd, tinysvcmdns...)
 * about each real change through a publisher hook.
 *
 * Threading
 * ---------
 * - Changes (put, remove, clear, set_publisher) are serialised: only one runs
 *   at a time, and publisher callbacks are delivered in exactly the order the
 *   changes were applied, so the backend can never end up holding an older
 *   version of a record than the registry does.
 * - Callbacks run on the thread that made the change, after the data lock has
 *   been released. A callback MAY call mdns_registry_get() and
 *   mdns_registry_snapshot(), and sees the state that includes its own change.
 * - A callback MUST NOT call put, remove, clear or set_publisher: that would
 *   deadlock. If the backend needs to change a record (for example renaming
 *   an instance after a name collision), do it from the backend's own thread
 *   or event loop, outside the callback.
 * - A slow callback delays other threads that want to change records. If
 *   the backend is slow (blocking D-Bus calls, say), have the callback queue
 *   the work for a backend thread and return at once.
 * - Readers (get, snapshot) only take the short data lock, so they are never
 *   held up by a callback.
 */
#ifndef MDNS_REGISTRY_H
#define MDNS_REGISTRY_H

#include "mdns_record_set.h"

typedef struct {
  /* Called for a record that was added (why == MDNS_PUT_ADDED) or whose
   * content changed (MDNS_PUT_REPLACED). Never called for an unchanged put.
   * rec is a private copy, valid for the duration of the call only. May be NULL. */
  void (*publish)(const mdns_service_record *rec, mdns_put_result why, void *ctx);
  /* Called for a record that was removed; rec is what was removed. May be NULL. */
  void (*unpublish)(const mdns_service_record *rec, void *ctx);
  void *ctx;
} mdns_publisher;

/* Adds or replaces the record for r's service type (see mdns_set_put).
 * Returns the slot index (>= 0) or a negative MDNS_ERR_*. *result, if not NULL,
 * is set on success. The publisher is notified only for ADDED and REPLACED. */
int mdns_registry_put(const mdns_service_record *r, mdns_put_result *result);

/* Removes the record with this type. MDNS_OK, or MDNS_ERR_NOTFOUND (and no
 * notification). */
int mdns_registry_remove(const char *type);

/* Removes every record, notifying unpublish for each, last slot first (use at shutdown). */
void mdns_registry_clear(void);

/* Copies out the record with this type. MDNS_OK or MDNS_ERR_NOTFOUND. */
int mdns_registry_get(const char *type, mdns_service_record *out);

/* Copies out the whole set (about 23 KB: give it heap or static storage). The
 * copy is a consistent snapshot taken at one instant. */
void mdns_registry_snapshot(mdns_record_set *out);

/* Installs the publisher (copied), or removes it if p is NULL. Installing one
 * immediately replays every existing record to it as MDNS_PUT_ADDED, in slot
 * order, so a backend that starts late is brought up to date. Replacing a
 * publisher does not unpublish anything through the old one. */
void mdns_registry_set_publisher(const mdns_publisher *p);

#endif
