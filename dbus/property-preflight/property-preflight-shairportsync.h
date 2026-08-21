/*
 * property-preflight-shairportsync.h
 *
 * Property-preflight declarations for the four "native" shairport-sync
 * D-Bus interfaces:
 *
 *   - org.gnome.ShairportSync                      (local config)
 *   - org.gnome.ShairportSync.Client                (remote-player state)
 *   - org.gnome.ShairportSync.RemoteControl         (remote-player state)
 *   - org.gnome.ShairportSync.AdvancedRemoteControl (remote-player state)
 *
 * See property-preflight.h for the generic machinery this builds on,
 * and property-preflight-mpris.h for the separate MPRIS interfaces.
 */

#ifndef __PROPERTY_PREFLIGHT_SHAIRPORTSYNC_H__
#define __PROPERTY_PREFLIGHT_SHAIRPORTSYNC_H__

#include "dbus-interface.h"
#include "property-preflight.h"

G_BEGIN_DECLS

/* org.gnome.ShairportSync */
PROPERTY_PREFLIGHT_DECLARE_SKELETON(PropertyPreflightShairportSyncSkeleton,
                                    property_preflight_shairport_sync_skeleton, ShairportSync)

/* org.gnome.ShairportSync.Client */
PROPERTY_PREFLIGHT_DECLARE_SKELETON(PropertyPreflightShairportSyncClientSkeleton,
                                    property_preflight_shairport_sync_client_skeleton,
                                    ShairportSyncClient)

/* org.gnome.ShairportSync.RemoteControl */
PROPERTY_PREFLIGHT_DECLARE_SKELETON(PropertyPreflightShairportSyncRemoteControlSkeleton,
                                    property_preflight_shairport_sync_remote_control_skeleton,
                                    ShairportSyncRemoteControl)

/* org.gnome.ShairportSync.AdvancedRemoteControl */
PROPERTY_PREFLIGHT_DECLARE_SKELETON(
    PropertyPreflightShairportSyncAdvancedRemoteControlSkeleton,
    property_preflight_shairport_sync_advanced_remote_control_skeleton,
    ShairportSyncAdvancedRemoteControl)

G_END_DECLS

#endif /* __PROPERTY_PREFLIGHT_SHAIRPORTSYNC_H__ */
