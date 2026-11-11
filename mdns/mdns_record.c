#include "mdns_record.h"

#include <string.h>
#include <strings.h> /* strncasecmp */

static int set_type(mdns_service_record *r, const char *type) {
  size_t len = strlen(type);
  if (len < 7 || len >= sizeof r->service_type || type[0] != '_')
    return MDNS_ERR_INVALID;
  const char *tail = type + len - 5;
  if (strcasecmp(tail, "._tcp") != 0 && strcasecmp(tail, "._udp") != 0) /* DNS names are case-insensitive */
    return MDNS_ERR_INVALID;
  memcpy(r->service_type, type, len + 1);
  return MDNS_OK;
}

int mdns_record_set_instance(mdns_service_record *r, const char *name) {
  size_t len = strlen(name);
  if (len == 0 || len >= sizeof r->instance_name) /* max 63 bytes */
    return MDNS_ERR_INVALID;
  memcpy(r->instance_name, name, len + 1);
  return MDNS_OK;
}

int mdns_record_set_port(mdns_service_record *r, uint16_t port) {
  if (port == 0)
    return MDNS_ERR_INVALID;
  r->port = port;
  return MDNS_OK;
}

int mdns_record_init(mdns_service_record *r, const char *type, const char *instance,
                     uint16_t port) {
  memset(r, 0, sizeof *r);
  if (port == 0 || set_type(r, type) != MDNS_OK ||
      mdns_record_set_instance(r, instance) != MDNS_OK) {
    memset(r, 0, sizeof *r); /* leave nothing half-set */
    return MDNS_ERR_INVALID;
  }
  r->port = port;
  return MDNS_OK;
}

/* Key = text before the first '=' (or the whole string). RFC 6763 6.4: the key
 * must be non-empty and printable ASCII (0x20-0x7E), and cannot contain '='. */
static int key_length(const char *kv, size_t len, size_t *keylen) {
  const char *eq = memchr(kv, '=', len);
  size_t kl = eq ? (size_t)(eq - kv) : len;
  if (kl == 0)
    return 0;
  for (size_t i = 0; i < kl; i++)
    if ((unsigned char)kv[i] < 0x20 || (unsigned char)kv[i] > 0x7e)
      return 0;
  *keylen = kl;
  return 1;
}

const char *mdns_record_get(const mdns_service_record *r, size_t index) {
  return index < r->n ? r->buf + r->kv_off[index] : NULL;
}

int mdns_record_find(const mdns_service_record *r, const char *key) {
  size_t kl = strlen(key);
  for (size_t i = 0; i < r->n; i++) {
    const char *s = r->buf + r->kv_off[i];
    size_t sl = strlen(s);
    const char *eq = memchr(s, '=', sl);
    size_t k = eq ? (size_t)(eq - s) : sl;
    if (k == kl && strncasecmp(s, key, kl) == 0) /* keys are case-insensitive */
      return (int)i;
  }
  return -1;
}

int mdns_record_add(mdns_service_record *r, const char *kv) {
  size_t len = strlen(kv), kl;
  if (len == 0 || len > MDNS_MAX_STRING || !key_length(kv, len, &kl))
    return MDNS_ERR_INVALID;

  char key[MDNS_MAX_STRING + 1];
  memcpy(key, kv, kl);
  key[kl] = '\0';
  if (mdns_record_find(r, key) >= 0)
    return MDNS_ERR_DUPLICATE;

  if (r->n >= MDNS_MAX_KV || (size_t)r->used + len + 1 > MDNS_TXT_BYTES)
    return MDNS_ERR_FULL;

  memcpy(r->buf + r->used, kv, len + 1); /* includes the NUL */
  r->kv_off[r->n++] = r->used;
  r->used = (uint16_t)(r->used + len + 1);
  return MDNS_OK;
}

int mdns_record_remove(mdns_service_record *r, size_t index) {
  if (index >= r->n)
    return MDNS_ERR_INVALID;
  size_t off = r->kv_off[index];
  size_t len = strlen(r->buf + off) + 1;

  memmove(r->buf + off, r->buf + off + len, r->used - off - len); /* close the gap */
  for (size_t j = index + 1; j < r->n; j++)
    r->kv_off[j - 1] = (uint16_t)(r->kv_off[j] - len); /* later strings moved down */
  r->n--;
  r->used = (uint16_t)(r->used - len);
  memset(r->buf + r->used, 0, len); /* keep the unused tail zeroed */
  return MDNS_OK;
}

int mdns_record_to_wire(const mdns_service_record *r, uint8_t *out, size_t cap) {
  if (r->n == 0) { /* an empty TXT record is one zero byte */
    if (cap < 1)
      return -1;
    out[0] = 0;
    return 1;
  }
  size_t pos = 0;
  for (size_t i = 0; i < r->n; i++) {
    const char *s = r->buf + r->kv_off[i];
    size_t l = strlen(s);
    if (pos + 1 + l > cap)
      return -1;
    out[pos++] = (uint8_t)l;
    memcpy(out + pos, s, l);
    pos += l;
  }
  return (int)pos;
}

int mdns_record_to_strv(const mdns_service_record *r, const char **out, size_t cap) {
  if (cap < (size_t)r->n + 1)
    return -1;
  for (size_t i = 0; i < r->n; i++)
    out[i] = r->buf + r->kv_off[i];
  out[r->n] = NULL;
  return r->n;
}

int mdns_record_equal(const mdns_service_record *a, const mdns_service_record *b) {
  if (strcasecmp(a->service_type, b->service_type) != 0 ||
      strcmp(a->instance_name, b->instance_name) != 0 || a->port != b->port || a->n != b->n)
    return 0;
  /* Keys are unique within a record and the counts match, so checking that every
   * attribute of a appears in b with the same value is enough. */
  for (size_t i = 0; i < a->n; i++) {
    const char *s = a->buf + a->kv_off[i];
    const char *eq = strchr(s, '=');
    size_t kl = eq ? (size_t)(eq - s) : strlen(s);
    size_t j;
    for (j = 0; j < b->n; j++) {
      const char *t = b->buf + b->kv_off[j];
      const char *teq = strchr(t, '=');
      size_t tl = teq ? (size_t)(teq - t) : strlen(t);
      if (tl == kl && strncasecmp(s, t, kl) == 0) {
        if (strcmp(s + kl, t + kl) != 0) /* from the '=' (or NUL) onwards */
          return 0;
        break;
      }
    }
    if (j == b->n)
      return 0; /* key not present in b */
  }
  return 1;
}
