/*
 * property-preflight-mpris.h
 *
 * Property-preflight declarations for the two MPRIS D-Bus interfaces:
 *
 *   - org.mpris.MediaPlayer2         (mostly read-only)
 *   - org.mpris.MediaPlayer2.Player  (remote-player state)
 *
 * See property-preflight.h for the generic machinery this builds on,
 * and property-preflight-shairportsync.h for the separate "native"
 * shairport-sync interfaces.
 *
 * TODO: the include below and the PublicType names in the two
 * PROPERTY_PREFLIGHT_DECLARE_SKELETON() invocations are UNVERIFIED
 * GUESSES pending your actual generated MPRIS header - see the
 * per-invocation comments.
 */

#ifndef __PROPERTY_PREFLIGHT_MPRIS_H__
#define __PROPERTY_PREFLIGHT_MPRIS_H__

/* TODO: confirm the actual filename of your gdbus-codegen generated
 * MPRIS header and correct this include - "mpris-interface.h" is a
 * guess, not a verified name. */
#include "mpris-interface.h"
#include "property-preflight.h"

double mpris_volume_to_airplay_volume(double sp);

G_BEGIN_DECLS

/* org.mpris.MediaPlayer2
 * TODO: PublicType (MediaPlayer2) is an UNVERIFIED GUESS at the type
 * name your MPRIS gdbus-codegen invocation actually produced - it
 * follows the same "strip the org.<x>. prefix" convention seen in
 * dbus-interface.h, but that's an inference, not a confirmed fact.
 * Correct this against your actual generated MPRIS header. */
PROPERTY_PREFLIGHT_DECLARE_SKELETON(PropertyPreflightMprisMediaPlayer2Skeleton,
                                    property_preflight_mpris_media_player2_skeleton, MediaPlayer2)

/* org.mpris.MediaPlayer2.Player
 * TODO: same caveat as above - PublicType (MediaPlayer2Player) is a
 * guess pending your actual generated header. */
PROPERTY_PREFLIGHT_DECLARE_SKELETON(PropertyPreflightMprisMediaPlayer2PlayerSkeleton,
                                    property_preflight_mpris_media_player2_player_skeleton,
                                    MediaPlayer2Player)

G_END_DECLS

#endif /* __PROPERTY_PREFLIGHT_MPRIS_H__ */
