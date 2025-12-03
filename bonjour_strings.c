/*
 * Bonjour strings manager. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2014--2025
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

#include "bonjour_strings.h"
#include "common.h"

char *txt_records[128];
char *secondary_txt_records[128];

// mDNS advertisement strings

// Create these strings and then keep them updated.
// When necessary, update the mDNS service records, using e.g. Avahi
// from these sources.

char fwString[128];
char ap1_featuresString[128];
char ap1StatusFlagsString[128];
char ap1ModelString[128];
char ap1SrcversString[128];
char pkString[128];

#ifdef CONFIG_AIRPLAY_2
char deviceIdString[128];
char featuresString[128];
char statusflagsString[128];
char piString[128];
char gidString[128];
char psiString[128];
char fexString[128];
char modelString[128];
char srcversString[128];
char osversString[128];
char ap1OsversString[128];
#endif

#ifdef CONFIG_AIRPLAY_2
void build_bonjour_strings(rtsp_conn_info *conn) {
#else
void build_bonjour_strings(__attribute((unused)) rtsp_conn_info *conn) {
#endif

  // Watch out here, the strings that form each entry
  // need to be permanent, because we don't know
  // when avahi will look at them.
  // bnprintf is (should be) the same as snprintf except that it returns a pointer to the resulting
  // character string. so this rather odd arranement below allows you to use a snprintf for
  // convenience but get the character string as a result, both as a store for the item so that
  // Avahi can see it in future and as a pointer
  int entry_number = 0;

  // the txt_records entries are for the _raop._tcp characteristics
  // the secondary_txt_records are for the _airplay._tcp items.

#ifdef CONFIG_AIRPLAY_2
  txt_records[entry_number++] = "cn=0,1";
  txt_records[entry_number++] = "da=true";
  txt_records[entry_number++] = "et=0,1";

  uint64_t features_hi = config.airplay_features;
  features_hi = (features_hi >> 32) & 0xffffffff;
  uint64_t features_lo = config.airplay_features;
  features_lo = features_lo & 0xffffffff;

  txt_records[entry_number++] =
      bnprintf(ap1_featuresString, sizeof(ap1_featuresString), "ft=0x%" PRIX64 ",0x%" PRIX64 "",
               features_lo, features_hi);

  txt_records[entry_number++] =
      bnprintf(fwString, sizeof(fwString), "fv=%s", config.firmware_version);
  txt_records[entry_number++] = bnprintf(ap1StatusFlagsString, sizeof(ap1StatusFlagsString),
                                         "sf=0x%" PRIX32, config.airplay_statusflags);
#ifdef CONFIG_METADATA
  if (config.get_coverart == 0)
    txt_records[entry_number++] = "md=0,2";
  else
    txt_records[entry_number++] = "md=0,1,2";
#endif
  txt_records[entry_number++] =
      bnprintf(ap1ModelString, sizeof(ap1ModelString), "am=%s", config.model);
  txt_records[entry_number++] = bnprintf(pkString, sizeof(pkString), "pk=%s", config.pk_string);
  txt_records[entry_number++] = "tp=UDP";
  txt_records[entry_number++] = "vn=65537";
  txt_records[entry_number++] =
      bnprintf(ap1SrcversString, sizeof(ap1SrcversString), "vs=%s", config.srcvers);
  txt_records[entry_number++] =
      bnprintf(ap1OsversString, sizeof(ap1OsversString), "ov=%s", config.osvers);
  txt_records[entry_number++] = NULL;

#else
  // here, just replicate what happens in mdns.h when using those #defines
  txt_records[entry_number++] =
      bnprintf(ap1StatusFlagsString, sizeof(ap1StatusFlagsString), "sf=0x4");
  txt_records[entry_number++] =
      bnprintf(fwString, sizeof(fwString), "fv=%s", config.firmware_version);
  txt_records[entry_number++] =
      bnprintf(ap1ModelString, sizeof(ap1ModelString), "am=%s", config.model);
  txt_records[entry_number++] = bnprintf(ap1SrcversString, sizeof(ap1SrcversString), "vs=105.1");
  txt_records[entry_number++] = "tp=TCP,UDP";
  txt_records[entry_number++] = "vn=65537";
#ifdef CONFIG_METADATA
  if (config.get_coverart == 0)
    txt_records[entry_number++] = "md=0,2";
  else
    txt_records[entry_number++] = "md=0,1,2";
#endif
  txt_records[entry_number++] = "ss=16";
  txt_records[entry_number++] = "sr=44100";
  txt_records[entry_number++] = "da=true";
  txt_records[entry_number++] = "sv=false";
  txt_records[entry_number++] = "et=0,1";
  txt_records[entry_number++] = "ek=1";
  txt_records[entry_number++] = "cn=0,1";
  txt_records[entry_number++] = "ch=2";
  txt_records[entry_number++] = "txtvers=1";
  if (config.password == 0)
    txt_records[entry_number++] = "pw=false";
  else
    txt_records[entry_number++] = "pw=true";
  txt_records[entry_number++] = NULL;
#endif

#ifdef CONFIG_AIRPLAY_2
  // make up a secondary set of text records
  entry_number = 0;

  secondary_txt_records[entry_number++] = "acl=0";
  secondary_txt_records[entry_number++] = "btaddr=00:00:00:00:00:00";
  secondary_txt_records[entry_number++] =
      bnprintf(deviceIdString, sizeof(deviceIdString), "deviceid=%s", config.airplay_device_id);
  secondary_txt_records[entry_number++] =
      bnprintf(fexString, sizeof(fexString), "fex=%s", config.airplay_fex);
  secondary_txt_records[entry_number++] =
      bnprintf(featuresString, sizeof(featuresString), "features=0x%" PRIX64 ",0x%" PRIX64 "",
               features_lo, features_hi); // features_hi and features_lo already calculated.
  secondary_txt_records[entry_number++] = bnprintf(statusflagsString, sizeof(statusflagsString),
                                                   "flags=0x%" PRIX32, config.airplay_statusflags);
  if ((conn != NULL) && (conn->airplay_gid != 0)) {
    snprintf(gidString, sizeof(gidString), "gid=%s", conn->airplay_gid);
  } else {
    snprintf(gidString, sizeof(gidString), "gid=%s", config.airplay_pi);
  }
  secondary_txt_records[entry_number++] = gidString;

  if ((conn != NULL) && (conn->groupContainsGroupLeader != 0)) {
    secondary_txt_records[entry_number++] = "igl=0";
    secondary_txt_records[entry_number++] = "gcgl=1";
  } else {
    secondary_txt_records[entry_number++] = "igl=0";
    secondary_txt_records[entry_number++] = "gcgl=0";
  }
  // if ((conn != NULL) && (conn->airplay_gid != 0)) // if it's in a group
  //   secondary_txt_records[entry_number++] = "isGroupLeader=0";
  secondary_txt_records[entry_number++] =
      bnprintf(modelString, sizeof(modelString), "model=%s", config.model);
  secondary_txt_records[entry_number++] = "protovers=1.1";
  secondary_txt_records[entry_number++] =
      bnprintf(piString, sizeof(piString), "pi=%s", config.airplay_pi);
  secondary_txt_records[entry_number++] =
      bnprintf(psiString, sizeof(psiString), "psi=%s", config.airplay_psi);
  secondary_txt_records[entry_number++] = pkString; // already calculated
  secondary_txt_records[entry_number++] =
      bnprintf(srcversString, sizeof(srcversString), "srcvers=%s", config.srcvers);
  secondary_txt_records[entry_number++] =
      bnprintf(osversString, sizeof(osversString), "osvers=%s", config.osvers);
  secondary_txt_records[entry_number++] = "vv=2";
  secondary_txt_records[entry_number++] = fwString; // already calculated
  secondary_txt_records[entry_number++] = NULL;
#endif
}
