/*
 * mdns_services.h -- builds Shairport Sync's mDNS service records
 * (_raop._tcp and, for AirPlay 2, _airplay._tcp) and puts them in the registry.
 */
#ifndef MDNS_SERVICES_H
#define MDNS_SERVICES_H

#include "common.h"
#include "mdns_registry.h"

/* Builds the _raop._tcp record from the current config. `instance` is the
 * service instance name (at most 63 bytes) and `port` the TCP port the service
 * listens on (host byte order, not 0). Returns MDNS_OK or a negative
 * MDNS_ERR_*. An attribute that would not fit in 255 bytes is an error
 * (MDNS_ERR_INVALID); it is never silently truncated. */
int mdns_build_raop_record(mdns_service_record *r, const char *instance, uint16_t port);

#ifdef CONFIG_AIRPLAY_2
/* Builds the _airplay._tcp record. conn may be NULL */
int mdns_build_airplay_record(mdns_service_record *r, const char *instance, uint16_t port,
                              const rtsp_conn_info *conn);
#endif

/* Rebuilds the service records from the current config (and conn, which may be
 * NULL) and puts them in the registry. 
 * Only records whose content actually changed are announced or reannounced.
 *
 * In AirPlay 2 mode both records are maintained; otherwise only _raop._tcp is,
 * and any _airplay._tcp record is removed. Both records are built before the
 * registry is touched, so a bad argument or an attribute that doesn't fit
 * changes nothing. Returns MDNS_OK or a negative MDNS_ERR_*. (Only registry
 * exhaustion, MDNS_ERR_FULL, can leave the first record updated and the
 * second not.) */
int mdns_update_service_records(const rtsp_conn_info *conn);

#endif
