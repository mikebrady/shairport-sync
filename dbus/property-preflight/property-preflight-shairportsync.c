/*
 * This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2018--2026
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

 * property-preflight-shairportsync.c
 *
 * Validators and skeleton subclasses for the four "native"
 * shairport-sync D-Bus interfaces. "Remote-player state" properties
 * (LoopStatus, Volume) use the trigger-then-drop pattern: validate/
 * clamp the requested value, kick off whatever tells the remote
 * player to change, then drop the write. The property is only ever
 * actually updated later, by whatever code already closes the loop
 * when the remote player's own confirmation arrives - see
 * shairport_sync_client_set_loop_status() and friends being called
 * from that (separate, pre-existing) code path, not from here.
 * ShairportSync's own local-config properties (DisableStandbyMode,
 * LoudnessThreshold) validate and apply directly instead, since
 * they're not remote-player-backed.
 *
 * The functions in the "REMOTE PLAYER TRIGGER STUBS" section below
 * are placeholders - replace their bodies with your actual mechanism
 * for telling the remote player to change. Nothing else in this file
 * needs to change to wire them up.
 */

#include "common.h"
#include "property-preflight-shairportsync.h"
#include "remote/remote.h"

/* ========================================================================
 * org.gnome.ShairportSync
 * ======================================================================== */

static gboolean property_preflight_shairport_sync_validate_property(const gchar *property_name,
                                                                     GVariant **value, GError **error) {
  static const gchar *const disable_standby_mode_values[] = {"Auto", "No", "Off",    "Never",
                                                              "Yes",  "On", "Always", NULL};

  debug(1, "Preflight ShairportSync.%s.", property_name);

  if (g_strcmp0(property_name, "DisableStandbyMode") == 0)
    return property_preflight_string_enum(property_name, *value, disable_standby_mode_values,
                                          "ShairportSync", error);
  else if (g_strcmp0(property_name, "LoudnessThreshold") == 0)
    return property_preflight_double_range(property_name, *value, -100.0, 0.0, "ShairportSync", error);
  /* Not a property we validate - let it through unchanged. */
  return TRUE;
}

PROPERTY_PREFLIGHT_DEFINE_SKELETON(PropertyPreflightShairportSyncSkeleton,
                                   property_preflight_shairport_sync_skeleton, ShairportSyncSkeleton,
                                   TYPE_SHAIRPORT_SYNC_SKELETON, ShairportSync, SHAIRPORT_SYNC,
                                   property_preflight_shairport_sync_validate_property)

/* ========================================================================
 * org.gnome.ShairportSync.Client
 * ======================================================================== */

static gboolean property_preflight_shairport_sync_client_validate_property(const gchar *property_name,
                                                                           __attribute((unused)) GVariant **value,
                                                                           __attribute((unused)) GError **error) {
  debug(1, "Preflight ShairportSync.Client.%s.", property_name);
  gboolean result = TRUE;

  /* Not a property we validate - let it through unchanged. */
  return result;
}

PROPERTY_PREFLIGHT_DEFINE_SKELETON(PropertyPreflightShairportSyncClientSkeleton,
                                   property_preflight_shairport_sync_client_skeleton,
                                   ShairportSyncClientSkeleton, TYPE_SHAIRPORT_SYNC_CLIENT_SKELETON,
                                   ShairportSyncClient, SHAIRPORT_SYNC_CLIENT,
                                   property_preflight_shairport_sync_client_validate_property)

/* ========================================================================
 * org.gnome.ShairportSync.RemoteControl
 * ======================================================================== */

static gboolean
property_preflight_shairport_sync_remote_control_validate_property(const gchar *property_name,
                                                                    __attribute((unused)) GVariant **value,
                                                                    __attribute((unused)) GError **error) {

  debug(1, "Preflight ShairportSync.RemoteControl.%s.", property_name);
  gboolean result = TRUE;

  /* Not a property we validate - let it through unchanged. */
  return result;
}

PROPERTY_PREFLIGHT_DEFINE_SKELETON(PropertyPreflightShairportSyncRemoteControlSkeleton,
                                   property_preflight_shairport_sync_remote_control_skeleton,
                                   ShairportSyncRemoteControlSkeleton,
                                   TYPE_SHAIRPORT_SYNC_REMOTE_CONTROL_SKELETON,
                                   ShairportSyncRemoteControl, SHAIRPORT_SYNC_REMOTE_CONTROL,
                                   property_preflight_shairport_sync_remote_control_validate_property)

/* ========================================================================
 * org.gnome.ShairportSync.AdvancedRemoteControl
 * ======================================================================== */

static gboolean property_preflight_shairport_sync_advanced_remote_control_validate_property(
    const gchar *property_name, GVariant **value, GError **error) {
    
  debug(1, "Preflight ShairportSync.AdvancedRemoteControl.%s.", property_name);
  gboolean result = TRUE;

  if (g_strcmp0(property_name, "LoopStatus") == 0) {
    int handled = 0;
    // Send valid LoopStatus request to the remote device...
    const gchar *requested_value = g_variant_get_string(*value, NULL);
    if (requested_value != NULL) {
      if (strcmp(requested_value, "Off") == 0) {
        handled = remote_set_repeat_mode(RS_OFF);
      } else if (strcmp(requested_value, "One") == 0) {
        handled = remote_set_repeat_mode(RS_ONE);
      } else if (strcmp(requested_value, "All") == 0) {
        handled = remote_set_repeat_mode(RS_ALL);
      } else {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
              "\"%s\" is not valid for ShairportSync.Client.LoopStatus. It must be one of the following: \"Off\", \"One\", \"All\".",
              requested_value);
        result = FALSE;
      }
      if ((result == TRUE) && (handled == 0)) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
              "ShairportSync.Client.LoopStatus is unable to set Loop Status on the client to \"%s\".", requested_value);
        result = FALSE;
      }
    } else {
      debug(1, "NULL loop status requested value.");
    }
    *value = NULL; // don't update the LoopStatus value here -- let the remote device update it.
  } else if (g_strcmp0(property_name, "Volume") == 0) {
    GVariant *original = *value;

    /* Clamp into range - never actually fails, but rules out an
     * absurd request before it reaches the remote player. */
    property_preflight_clamp_int_range(property_name, value, 0, 100, error);

    // property_preflight_send_volume_command(g_variant_get_int32(*value));

    /* If clamping substituted a corrected variant, free it ourselves -
     * the wrapper's own cleanup never runs once we set *value to NULL
     * below, since it happens after that point. */
    if (*value != original)
      g_variant_unref(*value);

    *value = NULL;
  }
  /* Not a property we validate - let it through unchanged. */
  return result;
}

PROPERTY_PREFLIGHT_DEFINE_SKELETON(
    PropertyPreflightShairportSyncAdvancedRemoteControlSkeleton,
    property_preflight_shairport_sync_advanced_remote_control_skeleton,
    ShairportSyncAdvancedRemoteControlSkeleton,
    TYPE_SHAIRPORT_SYNC_ADVANCED_REMOTE_CONTROL_SKELETON, ShairportSyncAdvancedRemoteControl,
    SHAIRPORT_SYNC_ADVANCED_REMOTE_CONTROL,
    property_preflight_shairport_sync_advanced_remote_control_validate_property)

