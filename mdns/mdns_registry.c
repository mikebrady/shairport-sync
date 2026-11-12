/*
 * mDNS registry. This file is part of Shairport Sync.
 * Shairport Sync keeps all mNDS information in a registry made of
 * sets of mNDNS records.
 * Changes in the registry cause the mDNS back ends to run to update
 * what is published on the network.
 * Copyright (c) Mike Brady 2026
 * All rights reserved.
 *
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

#include "mdns_registry.h"

#include <pthread.h>
#include <string.h>

/* Two locks, always taken in this order (pub_lock, then data_lock):
 *
 *   pub_lock  serialises every change and the callbacks it triggers, which is
 *             what guarantees callbacks arrive in the order changes were made.
 *             It is held across callbacks. It also guards `publisher`.
 *   data_lock protects `registry`. It is only held for the duration of a copy,
 *             never across a callback, so readers are never blocked by one.
 *
 * Only changes take pub_lock, so while a change holds it nothing else can modify
 * `registry`, and readers see a consistent state under data_lock alone. */
static pthread_mutex_t pub_lock = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t data_lock = PTHREAD_MUTEX_INITIALIZER;

static mdns_record_set registry; /* all zero is a valid empty set */
static mdns_publisher publisher; /* guarded by pub_lock */

int mdns_registry_put(const mdns_service_record *r, mdns_put_result *result) {
  mdns_service_record copy;
  mdns_put_result what = MDNS_PUT_UNCHANGED;
  int notify = 0;

  pthread_mutex_lock(&pub_lock);
  pthread_mutex_lock(&data_lock);
  int slot = mdns_set_put(&registry, r, &what);
  if (slot >= 0 && what != MDNS_PUT_UNCHANGED) {
    copy = registry.rec[slot]; /* what is stored now */
    notify = 1;
  }
  pthread_mutex_unlock(&data_lock);

  if (notify && publisher.publish != NULL)
    publisher.publish(&copy, what, publisher.ctx);
  if (slot >= 0 && result != NULL)
    *result = what;
  pthread_mutex_unlock(&pub_lock);
  return slot;
}

int mdns_registry_remove(const char *type) {
  mdns_service_record copy;
  int rc = MDNS_ERR_NOTFOUND;

  pthread_mutex_lock(&pub_lock);
  pthread_mutex_lock(&data_lock);
  int slot = mdns_set_find(&registry, type);
  if (slot >= 0) {
    copy = registry.rec[slot]; /* keep what is being removed, for the callback */
    rc = mdns_set_remove(&registry, type);
  }
  pthread_mutex_unlock(&data_lock);

  if (rc == MDNS_OK && publisher.unpublish != NULL)
    publisher.unpublish(&copy, publisher.ctx);
  pthread_mutex_unlock(&pub_lock);
  return rc;
}

void mdns_registry_clear(void) {
  mdns_service_record copy;

  pthread_mutex_lock(&pub_lock);
  for (;;) {
    pthread_mutex_lock(&data_lock);
    if (registry.count == 0) {
      pthread_mutex_unlock(&data_lock);
      break;
    }
    copy = registry.rec[registry.count - 1];
    mdns_set_remove(&registry, copy.service_type);
    pthread_mutex_unlock(&data_lock);

    if (publisher.unpublish != NULL)
      publisher.unpublish(&copy, publisher.ctx);
  }
  pthread_mutex_unlock(&pub_lock);
}

int mdns_registry_get(const char *type, mdns_service_record *out) {
  int rc = MDNS_ERR_NOTFOUND;
  pthread_mutex_lock(&data_lock);
  int slot = mdns_set_find(&registry, type);
  if (slot >= 0) {
    *out = registry.rec[slot];
    rc = MDNS_OK;
  }
  pthread_mutex_unlock(&data_lock);
  return rc;
}

void mdns_registry_snapshot(mdns_record_set *out) {
  pthread_mutex_lock(&data_lock);
  *out = registry;
  pthread_mutex_unlock(&data_lock);
}

void mdns_registry_set_publisher(const mdns_publisher *p) {
  mdns_service_record copy;

  pthread_mutex_lock(&pub_lock);
  if (p != NULL)
    publisher = *p;
  else
    memset(&publisher, 0, sizeof publisher);

  if (publisher.publish != NULL) {
    /* Replay existing records. No change can happen meanwhile (we hold pub_lock),
     * so the count and slots are stable between iterations. */
    for (size_t i = 0;; i++) {
      pthread_mutex_lock(&data_lock);
      if (i >= registry.count) {
        pthread_mutex_unlock(&data_lock);
        break;
      }
      copy = registry.rec[i];
      pthread_mutex_unlock(&data_lock);
      publisher.publish(&copy, MDNS_PUT_ADDED, publisher.ctx);
    }
  }
  pthread_mutex_unlock(&pub_lock);
}
