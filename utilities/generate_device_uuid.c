/*
 * Generate a device-related UUID. This file is part of Shairport Sync
 * Copyright (c) Mike Brady 2026
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

#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>

#ifndef UUID_STR_LEN
#define UUID_STR_LEN 37 // as in util-linux's uuid.h: 36 characters plus the NUL
#endif

#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>

// macOS's libuuid has no uuid_generate_sha1(), so derive the name-based
// (version 5) UUID as in RFC 4122 section 4.3, matching util-linux.
static void uuid_generate_sha1(uuid_t out, const uuid_t ns, const char *name, size_t len) {
  unsigned char digest[CC_SHA1_DIGEST_LENGTH];
  CC_SHA1_CTX ctx;
  CC_SHA1_Init(&ctx);
  CC_SHA1_Update(&ctx, ns, sizeof(uuid_t));
  CC_SHA1_Update(&ctx, name, (CC_LONG)len);
  CC_SHA1_Final(digest, &ctx);
  memcpy(out, digest, sizeof(uuid_t));
  out[6] = (out[6] & 0x0F) | 0x50; // version 5
  out[8] = (out[8] & 0x3F) | 0x80; // RFC 4122 variant
}
#endif

#include "definitions.h"
#include "generate_device_uuid.h"

// user is responsible for deallocating returned string
char *generate_device_uuid(const char *device_id) {
  uuid_t namespace_uuid;
  uuid_t derived_uuid;

  uuid_parse(SHAIRPORT_SYNC_DEVICE_NAMESPACE, namespace_uuid);
  uuid_generate_sha1(derived_uuid, namespace_uuid, device_id, strlen(device_id));

  char *uuid = malloc(UUID_STR_LEN + 1);
  uuid_unparse_lower(derived_uuid, uuid);
  return uuid;
}