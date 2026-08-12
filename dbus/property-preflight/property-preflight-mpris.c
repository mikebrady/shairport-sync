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
 * property-preflight-shairportsync.c - see the comment on
 * property_preflight_send_mpris_loop_status_command() below.
 *
 * The functions in the "REMOTE PLAYER TRIGGER STUBS" section below
 * are placeholders - replace their bodies with your actual mechanism
 * for telling the remote player to change. Nothing else in this file
 * needs to change to wire them up.
 *
 * TODO: the ParentType/ParentTypeMacro/PublicCastMacro identifiers in
 * both PROPERTY_PREFLIGHT_DEFINE_SKELETON() invocations below are
 * UNVERIFIED GUESSES pending your actual generated MPRIS header - see
 * the per-invocation comments.
 */

#include "property-preflight-mpris.h"

/* ========================================================================
 * REMOTE PLAYER TRIGGER STUBS
 * ======================================================================== */

static void property_preflight_send_mpris_loop_status_command(const gchar *requested_value) {
  /* TODO: replace with your actual mechanism. Note this is a
   * deliberately separate stub from
   * property_preflight_send_loop_status_command() above, since MPRIS
   * LoopStatus uses a different value vocabulary ("None"/"Track"/
   * "Playlist") than ShairportSync's own LoopStatus properties
   * ("Off"/"All"/"One") - if both ultimately drive the same
   * underlying remote player, consider having one call the other
   * with an appropriate translation, rather than duplicating the
   * actual trigger mechanism. */
  (void)requested_value;
}

static void property_preflight_send_mpris_volume_command(gdouble requested_volume) {
  /* TODO: replace with your actual mechanism. */
  (void)requested_volume;
}

/* ========================================================================
 * org.mpris.MediaPlayer2
 *
 * Mostly read-only per the MPRIS spec (CanQuit, CanRaise, Identity,
 * etc.) - nothing currently needs validating. Kept as a real
 * validator (rather than skipped entirely) so it's a one-line change
 * if that ever stops being true.
 * ======================================================================== */

static gboolean property_preflight_mpris_media_player2_validate_property(const gchar *property_name,
                                                                          GVariant **value,
                                                                          GError **error) {
  (void)property_name;
  (void)value;
  (void)error;

  debug(1, "property_preflight_mpris_media_player2_validate_property is called...");

  /* Nothing to validate here yet. */
  return TRUE;
}

/* TODO: ParentType/ParentTypeMacro/PublicCastMacro below follow the
 * same naming convention as dbus-interface.h's real types, but are
 * UNVERIFIED GUESSES for the MPRIS header - correct against your
 * actual generated header, same as PublicType in property-preflight.h. */
PROPERTY_PREFLIGHT_DEFINE_SKELETON(PropertyPreflightMprisMediaPlayer2Skeleton,
                                   property_preflight_mpris_media_player2_skeleton,
                                   MediaPlayer2Skeleton, TYPE_MEDIA_PLAYER2_SKELETON,
                                   MediaPlayer2, MEDIA_PLAYER2,
                                   property_preflight_mpris_media_player2_validate_property)

/* ========================================================================
 * org.mpris.MediaPlayer2.Player
 *
 * Value sets are per the MPRIS2 spec, NOT the ShairportSync-specific
 * vocabulary used elsewhere in this file - see the comment on
 * property_preflight_send_mpris_loop_status_command() above.
 * ======================================================================== */

static gboolean property_preflight_mpris_media_player2_player_validate_property(
    const gchar *property_name, GVariant **value, GError **error) {
  static const gchar *const valid_loop_status[] = {"None", "Track", "Playlist", NULL};

  debug(1, "property_preflight_mpris_media_player2_player_validate_property is called...");

  if (g_strcmp0(property_name, "LoopStatus") == 0) {
    if (!property_preflight_string_enum(property_name, *value, valid_loop_status,
                                        "org.mpris.MediaPlayer2.Player", error))
      return FALSE;

    property_preflight_send_mpris_loop_status_command(g_variant_get_string(*value, NULL));
    *value = NULL;
    return TRUE;
  }

  if (g_strcmp0(property_name, "Volume") == 0) {
    GVariant *original = *value;

    /* Per the MPRIS spec, Volume is a linear 0.0-1.0 scale and
     * out-of-range values should be clamped, not rejected. */
    property_preflight_clamp_double_range(property_name, value, 0.0, 1.0, error);

    property_preflight_send_mpris_volume_command(g_variant_get_double(*value));

    if (*value != original)
      g_variant_unref(*value);

    *value = NULL;
    return TRUE;
  }

  /* PlaybackStatus is not client-settable per the MPRIS spec - it's
   * driven by the Play/Pause/Stop/PlayPause methods instead, so no
   * property validation is needed for it here. Shuffle is a gboolean,
   * self-validating by its D-Bus type - if it also needs the
   * trigger-then-drop treatment (likely, since it's remote-player
   * state), add that branch the same way as LoopStatus above, minus
   * the value-check call. */

  return TRUE;
}

/* TODO: same caveat as MediaPlayer2 above - these names are guesses. */
PROPERTY_PREFLIGHT_DEFINE_SKELETON(PropertyPreflightMprisMediaPlayer2PlayerSkeleton,
                                   property_preflight_mpris_media_player2_player_skeleton,
                                   MediaPlayer2PlayerSkeleton, TYPE_MEDIA_PLAYER2_PLAYER_SKELETON,
                                   MediaPlayer2Player, MEDIA_PLAYER2_PLAYER,
                                   property_preflight_mpris_media_player2_player_validate_property)
