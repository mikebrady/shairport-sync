/*
 * mdns_record_set.h -- a fixed-capacity collection of mdns_service_records.
 *
 * Records are held by value, so the whole set is one pointer-free block:
 * copyable with plain assignment, and safe in shared memory or a file.
 * Size is about 16 x 1.5 KB = 23 KB, so prefer static or heap storage over
 * a deep stack.
 *
 * A set holds at most one record per service type (compared
 * case-insensitively, as DNS names are). Putting a record whose type is
 * already present replaces the old one in the same slot, unless the new one
 * is functionally identical (see mdns_record_equal), in which case the set is
 * left untouched. Otherwise the record is appended. Order is stable:
 * replacing keeps the slot, removing closes the gap.
 */
#ifndef MDNS_RECORD_SET_H
#define MDNS_RECORD_SET_H

#include "mdns_record.h"

#define MDNS_MAX_RECORDS 16

enum { MDNS_ERR_NOTFOUND = -4 };

typedef enum {
  MDNS_PUT_ADDED,     /* a new type: announce it */
  MDNS_PUT_REPLACED,  /* same type, different content: re-announce */
  MDNS_PUT_UNCHANGED, /* same type, functionally identical: nothing to do */
} mdns_put_result;

typedef struct {
  uint32_t count; /* records in use */
  mdns_service_record rec[MDNS_MAX_RECORDS];
} mdns_record_set;

void mdns_set_init(mdns_record_set *s);

/* Puts *r into the set, replacing any record with the same service type.
 * Returns the slot index (>= 0) on success, or a negative error:
 * MDNS_ERR_INVALID (empty or unterminated type/instance, inconsistent counts
 * or offsets) or MDNS_ERR_FULL (new type and all slots used). On success,
 * *result (if not NULL) says what happened; it is not written on error. The
 * set is unchanged on error and on MDNS_PUT_UNCHANGED, so the stored record
 * keeps its original spelling and attribute order. Note that 0 is a valid
 * slot index, so test for < 0. */
int mdns_set_put(mdns_record_set *s, const mdns_service_record *r, mdns_put_result *result);

/* Slot index of the record with this service type, or -1. */
int mdns_set_find(const mdns_record_set *s, const char *type);

/* Record in slot i, or NULL if i >= count. The pointer is valid until the
 * next put or remove. */
const mdns_service_record *mdns_set_get(const mdns_record_set *s, size_t i);

/* Removes the record with this service type. MDNS_OK or MDNS_ERR_NOTFOUND. */
int mdns_set_remove(mdns_record_set *s, const char *type);

#endif
