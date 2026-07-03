#ifndef UUID_COMPAT_H
#define UUID_COMPAT_H

#include <stddef.h>
#include <stdint.h>

typedef uint8_t uuid_t[16];

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 36
#endif

int uuid_parse(const char *in, uuid_t uu);
void uuid_unparse_lower(const uuid_t uu, char *out);
void uuid_generate_random(uuid_t out);
void uuid_generate_sha1(uuid_t out, const uuid_t ns, const char *name, size_t len);

#endif
