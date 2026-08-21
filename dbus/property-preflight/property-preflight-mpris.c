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

 * property-preflight-mpris.c
 *
 * Validators and skeleton subclasses for the two MPRIS D-Bus
 * interfaces. Value sets are per the MPRIS2 spec, NOT the
 * ShairportSync-specific vocabulary used in
 * property-preflight-shairportsync.c.
 */

#include "property-preflight-mpris.h"
#include "remote/remote.h"

double mpris_volume_to_airplay_volume(double sp) {
  sp = (sp - 1.0) * 30.0;
  if (sp < -30.0)
    sp = -30.0;
  if (sp > 0.0)
    sp = 0.0;
  return sp;
}

/* ========================================================================
 * org.mpris.MediaPlayer2
 *
 * Mostly read-only per the MPRIS spec (CanQuit, CanRaise, Identity,
 * etc.) - nothing currently needs validating. Kept as a real
 * validator (rather than skipped entirely) so it's a one-line change
 * if that ever stops being true.
 * ======================================================================== */

static gboolean property_preflight_mpris_media_player2_validate_property(
    __attribute((unused)) const gchar *property_name, __attribute((unused)) GVariant **value,
    __attribute((unused)) GError **error) {

  debug(1, "property_preflight_mpris_media_player2_validate_property is called...");

  /* Nothing to validate here yet. */
  return TRUE;
}

PROPERTY_PREFLIGHT_DEFINE_SKELETON(PropertyPreflightMprisMediaPlayer2Skeleton,
                                   property_preflight_mpris_media_player2_skeleton,
                                   MediaPlayer2Skeleton, TYPE_MEDIA_PLAYER2_SKELETON, MediaPlayer2,
                                   MEDIA_PLAYER2,
                                   property_preflight_mpris_media_player2_validate_property)

/* ========================================================================
 * org.mpris.MediaPlayer2.Player
 *
 * Value sets are per the MPRIS2 spec, NOT the ShairportSync-specific
 * vocabulary used elsewhere in this file.
 * ======================================================================== */

static gboolean
property_preflight_mpris_media_player2_player_validate_property(const gchar *property_name,
                                                                GVariant **value, GError **error) {

  gboolean result = TRUE;

  debug(1, "property_preflight_mpris_media_player2_player_validate_property is called...");

  if (g_strcmp0(property_name, "Volume") == 0) {
    gdouble requested_value = g_variant_get_double(*value);
    *value = NULL; // don't update the D-Bus value when finished
    if ((requested_value >= 0.0) && (requested_value <= 1.0)) {
      debug(1, ">> set MPRIS volume to %g.", requested_value);
      if (remote_set_airplay_volume(mpris_volume_to_airplay_volume(requested_value)) == 0) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                    "MPRIS MediaPlayer2.Player.Volume is unable to set the volume "
                    "on the client to %g%%.",
                    requested_value);
        result = FALSE;
      }
    } else {
      g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                  "%g is not a valid value for the MPRIS MediaPlayer2.Player.Volume property --  "
                  "it must be "
                  "within the range 0.0 to 1.0.",
                  requested_value);
      result = FALSE;
    }
  } else if (g_strcmp0(property_name, "LoopStatus") == 0) {
    int handled = 0;
    // Send valid LoopStatus request to the remote device...
    const gchar *requested_value = g_variant_get_string(*value, NULL);
    if (requested_value != NULL) {
      if (strcmp(requested_value, "None") == 0) {
        handled = remote_set_repeat_mode(RS_OFF);
      } else if (strcmp(requested_value, "Track") == 0) {
        handled = remote_set_repeat_mode(RS_ONE);
      } else if (strcmp(requested_value, "Playlist") == 0) {
        handled = remote_set_repeat_mode(RS_ALL);
      } else {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                    "\"%s\" is not valid for the MPRIS MediaPlayer2.Player.LoopStatus property. It "
                    "must be one of the following: \"None\", \"Track\", \"Playlist\".",
                    requested_value);
        result = FALSE;
      }
      if ((result == TRUE) && (handled == 0)) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                    "the MPRIS MediaPlayer2.Player.LoopStatus property could not be set to \"%s\" "
                    "on the client.",
                    requested_value);
        result = FALSE;
      }
    } else {
      g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                  " MediaPlayer2.Player.LoopStatus NULL request.");
      result = FALSE;
    }
    *value = NULL; // don't update the LoopStatus value here -- let the remote device update it.
  } else if (g_strcmp0(property_name, "Shuffle") == 0) {
    int handled = 0;
    const gboolean requested_value = g_variant_get_boolean(*value);
    if (requested_value) {
      handled = remote_set_shuffle_mode(SS_ON);
    } else {
      handled = remote_set_shuffle_mode(SS_OFF);
    }
    if (handled == 0) {
      g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                  "the MPRIS MediaPlayer2.Player.Shuffle property could not be set to \"%s\" "
                  "on the client.",
                  requested_value ? "TRUE" : "FALSE");
      result = FALSE;
    }
    *value = NULL; // don't update the Shuffle value here -- let the remote device update it.
  } else {
    debug(1, "Preflight MPRIS MediaPlayer2.Player.%s.", property_name);
  }
  return result;
}

PROPERTY_PREFLIGHT_DEFINE_SKELETON(PropertyPreflightMprisMediaPlayer2PlayerSkeleton,
                                   property_preflight_mpris_media_player2_player_skeleton,
                                   MediaPlayer2PlayerSkeleton, TYPE_MEDIA_PLAYER2_PLAYER_SKELETON,
                                   MediaPlayer2Player, MEDIA_PLAYER2_PLAYER,
                                   property_preflight_mpris_media_player2_player_validate_property)
