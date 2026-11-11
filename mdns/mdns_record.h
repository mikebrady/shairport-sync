/*
 * mdns_record.h -- a backend-neutral, position-independent description of an
 * mDNS / DNS-SD service.
 *
 * The whole record is one fixed-size block with no pointers, so it can be
 * copied with plain assignment, memcpy'd, written to a file, or placed in
 * shared memory. Strings are stored as NUL-terminated text packed into buf;
 * kv_off[i] is the offset of string i within buf.
 *
 * Budget: each "key=value" string costs (length + 1) bytes in buf, which is
 * exactly what it costs on the wire in a TXT record (length byte + text), so
 * `used` is the TXT record's size and MDNS_TXT_BYTES is the real limit.
 */
#ifndef MDNS_RECORD_H
#define MDNS_RECORD_H

#include <stddef.h>
#include <stdint.h>

#define MDNS_MAX_KV 32
#define MDNS_TXT_BYTES 1300 /* practical ceiling for a TXT record in one packet */
#define MDNS_MAX_STRING 255 /* DNS-SD limit for one TXT string */

enum {
  MDNS_OK = 0,
  MDNS_ERR_INVALID = -1,   /* malformed argument */
  MDNS_ERR_FULL = -2,      /* too many pairs, or TXT budget exhausted */
  MDNS_ERR_DUPLICATE = -3, /* key already present (keys are case-insensitive) */
};

typedef struct {
  char service_type[32];  /* e.g. "_airplay._tcp" */
  char instance_name[64]; /* up to 63 bytes (UTF-8) plus NUL */
  uint16_t port;          /* service port, host byte order; never 0 */
  uint16_t n;             /* number of key=value strings in use */
  uint16_t used;          /* bytes of buf in use == TXT wire size */
  uint16_t kv_off[MDNS_MAX_KV];
  char buf[MDNS_TXT_BYTES];
} mdns_service_record;

_Static_assert(MDNS_TXT_BYTES <= UINT16_MAX, "offsets must fit in uint16_t");

/* Setup. Zeroes the record, then sets the service type ("_name._tcp" or
 * "_name._udp"), the instance name (1-63 bytes) and the port (host byte order).
 * Port 0 is refused: in DNS-SD it means "no service here", and I believe Avahi
 * rejects it too. Returns MDNS_OK, or MDNS_ERR_INVALID if anything is bad, in
 * which case r is left zeroed (empty). */
int mdns_record_init(mdns_service_record *r, const char *type, const char *instance,
                     uint16_t port);

/* Rename later, e.g. after an mDNS name collision. Unchanged on error. */
int mdns_record_set_instance(mdns_service_record *r, const char *name);

/* Change the port of an existing record (host byte order). Every record has its
 * own port. 0 is refused with MDNS_ERR_INVALID, leaving the record unchanged.
 * No range is enforced: any port 1-65535 is accepted. */
int mdns_record_set_port(mdns_service_record *r, uint16_t port);

/* Editing. On any error the record is left unchanged. */
int mdns_record_add(mdns_service_record *r, const char *kv); /* "key=value" or "key" */
int mdns_record_remove(mdns_service_record *r, size_t index);

/* Access. The returned string points into the record: don't keep it across edits. */
const char *mdns_record_get(const mdns_service_record *r, size_t index);
int mdns_record_find(const mdns_service_record *r, const char *key); /* index or -1 */

/* True if a and b describe the same service: the same type (case-insensitive),
 * the same instance name, the same port, and the same set of attributes. Attribute order and
 * the case of keys are ignored; values are compared exactly, and "k" (no
 * value) differs from "k=" (empty value). Bytes beyond the strings are never
 * looked at, so stale padding cannot cause a false difference. */
int mdns_record_equal(const mdns_service_record *a, const mdns_service_record *b);

/* Conversions. */

/* DNS wire format ([len]text[len]text...), as DNSServiceRegister wants.
 * An empty record is the single byte 0, as the DNS requires. Returns the
 * length written, or -1 if out is too small. */
int mdns_record_to_wire(const mdns_service_record *r, uint8_t *out, size_t cap);

/* NULL-terminated array of string pointers (the form tinysvcmdns takes).
 * The pointers refer into r. out needs room for n + 1 entries. Returns n,
 * or -1 if cap is too small. */
int mdns_record_to_strv(const mdns_service_record *r, const char **out, size_t cap);

#endif
