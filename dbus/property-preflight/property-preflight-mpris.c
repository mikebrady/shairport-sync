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
#include "metadata/hub.h"
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
                                   property_preflight_mpris_media_player2_validate_property, NULL)

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

/* ------------------------------------------------------------------------
 * Position: computed live on every Get/GetAll, never pushed.
 *
 * Per the MPRIS spec, Position is meant to be polled or extrapolated by
 * clients (using Rate), not proactively notified on every change.
 * This function computes
 * the value fresh every time a client actually asks for it, and nothing
 * is ever pushed proactively - so there is nothing to suppress in the
 * first place.
 *
 * ------------------------------------------------------------------------ */

static gint64 property_preflight_mpris_estimate_position_microseconds(void) {

  static gint64 position = 0;
  pthread_rwlock_rdlock(&principal_conn_lock); // don't let the principal_conn be changed
  pthread_cleanup_push(rwlock_unlock, (void *)&principal_conn_lock);
  if (principal_conn != NULL) {
    // first, figure out if we are using the progress string or the AirPlay plist information
    int using_progress_string = 1; // guess it is the older progress string
#ifdef CONFIG_AIRPLAY_2
    // if we are playing an AirPlay 2 stream then
    // we will only use the progress strings if plists have been disabled
    // because the plist information is more reliable
    if ((principal_conn->airplay_type == ap_2) && ((config.airplay_features & ((uint64_t)1 << 50)) != 0)) {
      using_progress_string = 0;
    }
#endif

    if (using_progress_string != 0) {
      // A playing state of 2 seems to mean "not really playing" even if audio (probably silence)
      // is coming through from the player.
      if ((principal_conn->input_rate != 0) && (metadata_store.npi.playing_state != 2) &&
          (metadata_store.progress_string != NULL)) {

        int32_t frames_total =
            metadata_store.progress_last_timestamp - metadata_store.progress_first_timestamp;
        int32_t frames_played =
            metadata_store.head_rtp_timestamp - metadata_store.progress_first_timestamp;
        int32_t frames_remaining =
            metadata_store.progress_last_timestamp - metadata_store.head_rtp_timestamp;

        debug(4,
              "position: %g seconds, rate: %u. Start , Current, End Timestamps: %u, %u, %u. Total, "
              "played, remaining frames: %d, %d, %d, total time: %g.",
              (1.0 * frames_played) / principal_conn->input_rate, principal_conn->input_rate,
              metadata_store.progress_first_timestamp, metadata_store.progress_current_timestamp,
              metadata_store.progress_last_timestamp, frames_total, frames_played, frames_remaining,
              (1.0 * frames_total) / principal_conn->input_rate);

        // if the timestamp that is about to be played is between the start and the finish, accept
        // it as valid.
        if ((frames_total >= 0) && (frames_played >= 0) && (frames_remaining >= 0)) {
          position = 1000000; // microseconds
          position = position * frames_played;
          position = position / principal_conn->input_rate;
        }
      }
    } else {
      // Using the plist information.
      // If Shairport Sync is playing, start with the play time since the NowPlayingInfoTimestamp.
      if (metadata_store.npi.nowPlayingInfoTimestamp.valid) {
        position = get_absolute_time_in_ns() - metadata_store.npi.nowPlayingInfoTimestamp.value;
      } else {
        // Otherwise, add the stored subsequent elapsed time
        position = metadata_store.npi.nowPlayingInfoSubsequentElapsedTime;
      }
      // add in the elapsed time recorded in the nowPlayingInfo bundle
      position += metadata_store.npi.nowPlayingInfoPriorElapsedTime;
      position /= 1000; // to microseconds
    }
  }
  pthread_cleanup_pop(1); // release the principal_conn lock
  return position;
}

static gboolean property_preflight_mpris_media_player2_player_compute_property(
    const gchar *property_name, GVariant **value, __attribute((unused)) GError **error) {
  if (g_strcmp0(property_name, "Position") == 0) {
    gint64 live_position_us = property_preflight_mpris_estimate_position_microseconds();
    *value = g_variant_ref_sink(g_variant_new_int64(live_position_us));
  }
  /* Every other property: leave *value (the cached value gdbus-codegen
   * would otherwise have returned) untouched and let it through as-is. */
  return TRUE;
}

PROPERTY_PREFLIGHT_DEFINE_SKELETON(PropertyPreflightMprisMediaPlayer2PlayerSkeleton,
                                   property_preflight_mpris_media_player2_player_skeleton,
                                   MediaPlayer2PlayerSkeleton, TYPE_MEDIA_PLAYER2_PLAYER_SKELETON,
                                   MediaPlayer2Player, MEDIA_PLAYER2_PLAYER,
                                   property_preflight_mpris_media_player2_player_validate_property,
                                   property_preflight_mpris_media_player2_player_compute_property)
