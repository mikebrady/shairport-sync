#include "config.h"

#ifdef CONFIG_FOR_MINGW
#include "uuid_compat.h"
#include <ctype.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdio.h>
#include <string.h>

static int hex_value(int c) {
  if ((c >= '0') && (c <= '9'))
    return c - '0';
  c = tolower(c);
  if ((c >= 'a') && (c <= 'f'))
    return c - 'a' + 10;
  return -1;
}

int uuid_parse(const char *in, uuid_t uu) {
  int nibbles = 0;
  memset(uu, 0, 16);
  while (*in != '\0') {
    if (*in == '-') {
      in++;
      continue;
    }
    int v = hex_value((unsigned char)*in++);
    if (v < 0)
      return -1;
    if (nibbles >= 32)
      return -1;
    if ((nibbles & 1) == 0)
      uu[nibbles / 2] = (uint8_t)(v << 4);
    else
      uu[nibbles / 2] |= (uint8_t)v;
    nibbles++;
  }
  return nibbles == 32 ? 0 : -1;
}

void uuid_unparse_lower(const uuid_t uu, char *out) {
  snprintf(out, 37,
           "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
           uu[0], uu[1], uu[2], uu[3], uu[4], uu[5], uu[6], uu[7], uu[8], uu[9],
           uu[10], uu[11], uu[12], uu[13], uu[14], uu[15]);
}

void uuid_generate_random(uuid_t out) {
  if (RAND_bytes(out, 16) != 1)
    memset(out, 0, 16);
  out[6] = (out[6] & 0x0f) | 0x40;
  out[8] = (out[8] & 0x3f) | 0x80;
}

void uuid_generate_sha1(uuid_t out, const uuid_t ns, const char *name, size_t len) {
  unsigned char digest[EVP_MAX_MD_SIZE];
  unsigned int digest_len = 0;
  EVP_MD_CTX *ctx = EVP_MD_CTX_new();
  EVP_DigestInit_ex(ctx, EVP_sha1(), NULL);
  EVP_DigestUpdate(ctx, ns, 16);
  EVP_DigestUpdate(ctx, name, len);
  EVP_DigestFinal_ex(ctx, digest, &digest_len);
  EVP_MD_CTX_free(ctx);

  memcpy(out, digest, 16);
  out[6] = (out[6] & 0x0f) | 0x50;
  out[8] = (out[8] & 0x3f) | 0x80;
}
#endif
