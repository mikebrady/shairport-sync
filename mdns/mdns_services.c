/*
 * mDNS Services. This file is part of Shairport Sync.
 * This is responsible for generating mDNS service information
 * and passing it to the mDNS registry.
 * It is concerned only with the _raop and _airplay services.
 * Other services can be sent to the registry from elsewhere in the application.
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


#include "mdns_services.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#define TRY(x)                                                                                     \
  do {                                                                                             \
    int rc_ = (x);                                                                                 \
    if (rc_ != MDNS_OK)                                                                            \
      return rc_;                                                                                  \
  } while (0)

/* printf-style mdns_record_add. A string too long for one TXT entry is refused
 * rather than truncated (the old code silently cut each at 127 bytes). */
static int addf(mdns_service_record *r, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static int addf(mdns_service_record *r, const char *fmt, ...) {
  char buf[MDNS_MAX_STRING + 1];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  if (n < 0 || (size_t)n >= sizeof buf)
    return MDNS_ERR_INVALID;
  return mdns_record_add(r, buf);
}

#ifdef CONFIG_AIRPLAY_2
static void split_features(uint64_t *lo, uint64_t *hi) {
  *hi = (config.airplay_features >> 32) & 0xffffffff;
  *lo = config.airplay_features & 0xffffffff;
}
#endif

/* ---- _raop._tcp ---- */

int mdns_build_raop_record(mdns_service_record *r, const char *instance, uint16_t port) {
  TRY(mdns_record_init(r, "_raop._tcp", instance, port));

#ifdef CONFIG_AIRPLAY_2
  if (config.service_type == APST_airplay2) {
    uint64_t features_lo, features_hi;
    split_features(&features_lo, &features_hi);

    TRY(mdns_record_add(r, "cn=0,1"));
    TRY(mdns_record_add(r, "da=true"));
    TRY(mdns_record_add(r, "et=0,1"));
    if (config.password != NULL)
      TRY(mdns_record_add(r, "pw=true"));
    TRY(addf(r, "ft=0x%" PRIX64 ",0x%" PRIX64 "", features_lo, features_hi));
    TRY(addf(r, "fv=%s", config.firmware_version));
    TRY(addf(r, "sf=0x%" PRIX32 "", config.airplay_statusflags));
#ifdef CONFIG_METADATA
    TRY(mdns_record_add(r, config.get_coverart == 0 ? "md=0,2" : "md=0,1,2"));
#endif
    TRY(addf(r, "am=%s", config.model));
    TRY(addf(r, "pk=%s", config.pk_string));
    TRY(mdns_record_add(r, "tp=UDP"));
    TRY(mdns_record_add(r, "vn=65537"));
    TRY(addf(r, "vs=%s", config.srcvers));
    return MDNS_OK;
  }
#endif

  /* AirPlay 1: replicates what mdns.h does with its #defines */
  TRY(addf(r, "sf=0x%" PRIX32 "", config.airplay_statusflags));
  TRY(addf(r, "fv=%s", config.firmware_version));
  TRY(addf(r, "am=%s", config.model));
  TRY(mdns_record_add(r, "vs=105.1"));
  TRY(mdns_record_add(r, "tp=TCP,UDP"));
  TRY(mdns_record_add(r, "vn=65537"));
#ifdef CONFIG_METADATA
  TRY(mdns_record_add(r, config.get_coverart == 0 ? "md=0,2" : "md=0,1,2"));
#endif
  TRY(mdns_record_add(r, "ss=16"));
  TRY(mdns_record_add(r, "sr=44100"));
  TRY(mdns_record_add(r, "da=true"));
  TRY(mdns_record_add(r, "sv=false"));
  TRY(mdns_record_add(r, "et=0,1"));
  TRY(mdns_record_add(r, "ek=1"));
  TRY(mdns_record_add(r, "cn=0,1"));
  TRY(mdns_record_add(r, "ch=2"));
  TRY(mdns_record_add(r, "txtvers=1"));
  TRY(mdns_record_add(r, config.password == NULL ? "pw=false" : "pw=true"));
  return MDNS_OK;
}

/* ---- _airplay._tcp (AirPlay 2 only) ---- */

#ifdef CONFIG_AIRPLAY_2
int mdns_build_airplay_record(mdns_service_record *r, const char *instance, uint16_t port,
                              const rtsp_conn_info *conn) {
  uint64_t features_lo, features_hi;
  split_features(&features_lo, &features_hi);

  TRY(mdns_record_init(r, "_airplay._tcp", instance, port));
  TRY(mdns_record_add(r, "acl=0"));
  TRY(addf(r, "deviceid=%s", config.airplay_device_id));
  TRY(addf(r, "fex=%s", config.airplay_fex));
  TRY(addf(r, "features=0x%" PRIX64 ",0x%" PRIX64 "", features_lo, features_hi));
  TRY(addf(r, "flags=0x%" PRIX32 "", config.airplay_statusflags));
  if ((conn != NULL) && (conn->airplay_gid != 0))
    TRY(addf(r, "gid=%s", conn->airplay_gid));
  else
    TRY(addf(r, "gid=%s", config.airplay_pi));
  TRY(mdns_record_add(r, "igl=0"));
  if ((conn != NULL) && (conn->groupContainsGroupLeader != 0))
    TRY(mdns_record_add(r, "gcgl=1"));
  else
    TRY(mdns_record_add(r, "gcgl=0"));
  TRY(addf(r, "model=%s", config.model));
  TRY(mdns_record_add(r, "protovers=1.1"));
  TRY(addf(r, "pi=%s", config.airplay_pi));
  TRY(addf(r, "psi=%s", config.airplay_psi));
  TRY(addf(r, "pk=%s", config.pk_string));
  TRY(addf(r, "srcvers=%s", config.srcvers));
  TRY(addf(r, "fv=%s", config.firmware_version));
  return MDNS_OK;
}
#endif

/* ---- registry update ---- */

int mdns_update_service_records(const rtsp_conn_info *conn) {
  mdns_service_record raop;
  // first, build the ap1 service name
  char *ap1_service_name = alloca(strlen(config.service_name) + 14);
  char *p = ap1_service_name;
  int i;
  for (i = 0; i < 6; i++) {
    snprintf(p, 3, "%02X", config.ap1_prefix[i]);
    p += 2;
  }
  *p++ = '@';
  strcpy(p, config.service_name);

  TRY(mdns_build_raop_record(&raop, ap1_service_name, config.port));

#ifdef CONFIG_AIRPLAY_2
  mdns_service_record airplay;
  memset(&airplay, 0, sizeof airplay);
  int want_airplay = (config.service_type == APST_airplay2);
  if (want_airplay)
    TRY(mdns_build_airplay_record(&airplay, config.service_name, config.port, conn));
#else
  (void)conn;
#endif

  /* Everything has been built successfully; now change the registry. */
  int rc = mdns_registry_put(&raop, NULL);
  if (rc < 0)
    return rc;

#ifdef CONFIG_AIRPLAY_2
  if (want_airplay) {
    rc = mdns_registry_put(&airplay, NULL);
    if (rc < 0)
      return rc;
  } else {
    (void)mdns_registry_remove("_airplay._tcp"); /* not advertised in this mode */
  }
#endif
  return MDNS_OK;
}
