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
 */
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "config.h"

#include "common.h"
#include "player.h"
#include "rtsp.h"

#include "rtp.h"

#include "metadata/hub.h"
#include "mpris-service.h"
#include "property-preflight/property-preflight-mpris.h"
#include "remote/remote.h"
#include "utilities/exit.h"

static guint ownerID = 0;
static GBusType mpris_bus_type = G_BUS_TYPE_SYSTEM; // default is the dbus system message bus

MediaPlayer2 *mprisPlayerSkeleton = NULL;
MediaPlayer2Player *mprisPlayerPlayerSkeleton = NULL;

double airplay_volume_to_mpris_volume(double sp) {
  if (sp < -30.0)
    sp = -30.0;
  if (sp > 0.0)
    sp = 0.0;
  sp = (sp / 30.0) + 1;
  return sp;
}

void mpris_metadata_watcher(struct metadata_bundle *argc) {
  // debug(1, "MPRIS metadata watcher called");
  char response[100];
  media_player2_player_set_volume(mprisPlayerPlayerSkeleton,
                                  airplay_volume_to_mpris_volume(argc->airplay_volume));

  // sticking strictly to the MPRIS enumeration only -- not including "Not Available"
  switch (argc->repeat_status) {
  case RS_NOT_AVAILABLE:
    strcpy(response, "None");
    break;
  case RS_OFF:
    strcpy(response, "None");
    break;
  case RS_ONE:
    strcpy(response, "Track");
    break;
  case RS_ALL:
    strcpy(response, "Playlist");
    break;
  }

  media_player2_player_set_loop_status(mprisPlayerPlayerSkeleton, response);

  // sticking strictly to the MPRIS enumeration only -- not including "Not Available"
  switch (argc->player_state) {
  case PS_NOT_AVAILABLE:
    strcpy(response, "Stopped");
    break;
  case PS_STOPPED:
    strcpy(response, "Stopped");
    break;
  case PS_PAUSED:
    strcpy(response, "Paused");
    break;
  case PS_PLAYING:
    strcpy(response, "Playing");
    break;
  }

  media_player2_player_set_playback_status(mprisPlayerPlayerSkeleton, response);

  // sticking strictly to the MPRIS enumeration only -- not including "Not Available"

  switch (argc->shuffle_status) {
  case SS_NOT_AVAILABLE:
    media_player2_player_set_shuffle(mprisPlayerPlayerSkeleton, FALSE);
    break;
  case SS_OFF:
    media_player2_player_set_shuffle(mprisPlayerPlayerSkeleton, FALSE);
    break;
  case SS_ON:
    media_player2_player_set_shuffle(mprisPlayerPlayerSkeleton, TRUE);
    break;
  default:
    debug(1, "This should never happen.");
  }

  // Build the metadata array
  debug(4, "Build metadata");
  GVariantBuilder *dict_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

  // Add in the artwork URI if it exists.
  if (argc->npi.cover_art_pathname) {
    GVariant *artUrl = g_variant_new("s", argc->npi.cover_art_pathname);
    g_variant_builder_add(dict_builder, "{sv}", "mpris:artUrl", artUrl);
  }

  // Add in the Track ID based on the 'mper' metadata if it is non-zero
  if (is_valid_uint64_record(&argc->npi.item_id)) {
    char trackidstring[128];
    snprintf(trackidstring, sizeof(trackidstring), "/org/gnome/ShairportSync/%" PRIu64 "",
             argc->npi.item_id.item);
    GVariant *trackid = g_variant_new("o", trackidstring);
    g_variant_builder_add(dict_builder, "{sv}", "mpris:trackid", trackid);
  }

  // Add the track name if it exists
  if (argc->npi.track_name) {
    GVariant *track_name = g_variant_new("s", argc->npi.track_name);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:title", track_name);
  }

  // Add the track number if it is valid
  if (is_valid_uint64_record(&argc->npi.track_number)) {
    GVariant *tracknumber = g_variant_new("x", argc->npi.track_number.item);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:trackNumber", tracknumber);
  }

  // Add the album name if it exists
  if (argc->npi.album_name) {
    GVariant *album_name = g_variant_new("s", argc->npi.album_name);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:album", album_name);
  }

  // Add the artist name if it exists
  if (argc->npi.artist_name) {
    GVariantBuilder *artist_as = g_variant_builder_new(G_VARIANT_TYPE("as"));
    g_variant_builder_add(artist_as, "s", argc->npi.artist_name);
    GVariant *artists = g_variant_builder_end(artist_as);
    g_variant_builder_unref(artist_as);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:artist", artists);
  }

  // Add the genre if it exists
  if (argc->npi.genre) {
    GVariantBuilder *genre_as = g_variant_builder_new(G_VARIANT_TYPE("as"));
    g_variant_builder_add(genre_as, "s", argc->npi.genre);
    GVariant *genre = g_variant_builder_end(genre_as);
    g_variant_builder_unref(genre_as);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:genre", genre);
  }

  if (is_valid_uint64_record(&argc->npi.songtime_in_microseconds)) {
    GVariant *tracklength = g_variant_new("x", argc->npi.songtime_in_microseconds);
    g_variant_builder_add(dict_builder, "{sv}", "mpris:length", tracklength);
  }

  GVariant *dict = g_variant_builder_end(dict_builder);
  g_variant_builder_unref(dict_builder);
  media_player2_player_set_metadata(mprisPlayerPlayerSkeleton, dict);
}

static gboolean on_handle_quit(MediaPlayer2 *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  debug(4, "MPRIS quit.");
  media_player2_complete_quit(skeleton, invocation);
  exit_request(EXIT_SUCCESS);
  return TRUE;
}

static gboolean on_handle_next(MediaPlayer2Player *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  debug(4, "MPRIS next item.");
  remote_simple_command(rcsc_next_item);
  media_player2_player_complete_next(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_previous(MediaPlayer2Player *skeleton, GDBusMethodInvocation *invocation,
                                   __attribute__((unused)) gpointer user_data) {
  debug(4, "MPRIS previous item.");
  remote_simple_command(rcsc_previous_item);
  media_player2_player_complete_previous(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_stop(MediaPlayer2Player *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  debug(4, "MPRIS stop.");
  remote_simple_command(rcsc_stop);
  media_player2_player_complete_stop(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_pause(MediaPlayer2Player *skeleton, GDBusMethodInvocation *invocation,
                                __attribute__((unused)) gpointer user_data) {
  debug(4, "MPRIS pause.");
  remote_simple_command(rcsc_pause);
  media_player2_player_complete_pause(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_play_pause(MediaPlayer2Player *skeleton,
                                     GDBusMethodInvocation *invocation,
                                     __attribute__((unused)) gpointer user_data) {
  debug(4, "MPRIS playpause.");
  remote_simple_command(rcsc_play_pause);
  media_player2_player_complete_play_pause(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_play(MediaPlayer2Player *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  debug(4, "MPRIS play.");
  remote_simple_command(rcsc_play);
  media_player2_player_complete_play(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_set_volume(MediaPlayer2Player *skeleton,
                                     GDBusMethodInvocation *invocation, const gdouble volume,
                                     __attribute__((unused)) gpointer user_data) {
  debug(1, "MPRIS set volume to %g.", volume);
  remote_set_airplay_volume(mpris_volume_to_airplay_volume(volume));
  media_player2_player_complete_play(skeleton, invocation);
  return TRUE;
}

static void on_mpris_name_acquired(GDBusConnection *connection, const gchar *name,
                                   __attribute__((unused)) gpointer user_data) {

  const char *empty_string_array[] = {NULL};

  debug(2, "MPRIS well-known interface name \"%s\" acquired on the %s bus.", name,
        (mpris_bus_type == G_BUS_TYPE_SESSION) ? "session" : "system");

  mprisPlayerSkeleton = property_preflight_mpris_media_player2_skeleton_new();
  mprisPlayerPlayerSkeleton = property_preflight_mpris_media_player2_player_skeleton_new();

  media_player2_set_desktop_entry(mprisPlayerSkeleton, "shairport-sync");
  media_player2_set_identity(mprisPlayerSkeleton, "Shairport Sync");
  media_player2_set_can_quit(mprisPlayerSkeleton, TRUE);
  media_player2_set_can_raise(mprisPlayerSkeleton, FALSE);
  media_player2_set_has_track_list(mprisPlayerSkeleton, FALSE);
  media_player2_set_supported_uri_schemes(mprisPlayerSkeleton, empty_string_array);
  media_player2_set_supported_mime_types(mprisPlayerSkeleton, empty_string_array);

  media_player2_player_set_metadata(mprisPlayerPlayerSkeleton,
                                    g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0));
  media_player2_player_set_volume(mprisPlayerPlayerSkeleton,
                                  airplay_volume_to_mpris_volume(config.airplay_volume));
  media_player2_player_set_playback_status(mprisPlayerPlayerSkeleton, "Stopped");
  media_player2_player_set_loop_status(mprisPlayerPlayerSkeleton, "None");
  // Position is computed live by property_preflight_mpris_media_player2_player_compute_property()
  // on every Get/GetAll (see property-preflight-mpris.c) - this call just seeds the underlying
  // cached value with something harmless; it is never actually read by a D-Bus client.
  media_player2_player_set_position(mprisPlayerPlayerSkeleton, 0.0);
  media_player2_player_set_shuffle(mprisPlayerPlayerSkeleton, FALSE);
  media_player2_player_set_minimum_rate(mprisPlayerPlayerSkeleton, 1.0);
  media_player2_player_set_maximum_rate(mprisPlayerPlayerSkeleton, 1.0);
  media_player2_player_set_can_go_next(mprisPlayerPlayerSkeleton, TRUE);
  media_player2_player_set_can_go_previous(mprisPlayerPlayerSkeleton, TRUE);
  media_player2_player_set_can_play(mprisPlayerPlayerSkeleton, TRUE);
  media_player2_player_set_can_pause(mprisPlayerPlayerSkeleton, TRUE);
  media_player2_player_set_can_seek(mprisPlayerPlayerSkeleton, FALSE);
  media_player2_player_set_can_control(mprisPlayerPlayerSkeleton, TRUE);

  g_signal_connect(mprisPlayerSkeleton, "handle-quit", G_CALLBACK(on_handle_quit), NULL);

  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-play", G_CALLBACK(on_handle_play), NULL);
  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-pause", G_CALLBACK(on_handle_pause), NULL);
  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-play-pause", G_CALLBACK(on_handle_play_pause),
                   NULL);
  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-stop", G_CALLBACK(on_handle_stop), NULL);
  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-next", G_CALLBACK(on_handle_next), NULL);
  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-previous", G_CALLBACK(on_handle_previous),
                   NULL);
  g_signal_connect(mprisPlayerPlayerSkeleton, "handle-set-volume", G_CALLBACK(on_handle_set_volume),
                   NULL);

  add_metadata_watcher(mpris_metadata_watcher);

  // connect to the bus

  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(mprisPlayerSkeleton), connection,
                                   "/org/mpris/MediaPlayer2", NULL);
  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(mprisPlayerPlayerSkeleton), connection,
                                   "/org/mpris/MediaPlayer2", NULL);

  debug(1, "MPRIS service started at \"%s\" on the %s bus.", name,
        (mpris_bus_type == G_BUS_TYPE_SESSION) ? "session" : "system");
}

static void on_mpris_name_lost(__attribute__((unused)) GDBusConnection *connection,
                               const gchar *name, __attribute__((unused)) gpointer user_data) {
  warn("could not acquire an MPRIS interface named \"%s\" on the %s bus.", name,
       (mpris_bus_type == G_BUS_TYPE_SESSION) ? "session" : "system");
  ownerID = 0;
}

int start_mpris_service() {
  mprisPlayerSkeleton = NULL;
  mprisPlayerPlayerSkeleton = NULL;

  // set up default message bus

  if (config.dbus_default_message_bus == DBT_session)
    mpris_bus_type = G_BUS_TYPE_SESSION;

  // look for explicit overrides
  if (config.mpris_service_bus_type == DBT_system)
    mpris_bus_type = G_BUS_TYPE_SYSTEM;
  else if (config.mpris_service_bus_type == DBT_session)
    mpris_bus_type = G_BUS_TYPE_SESSION;

  debug(1, "Looking for an MPRIS interface \"org.mpris.MediaPlayer2.ShairportSync\" on the %s bus.",
        (mpris_bus_type == G_BUS_TYPE_SESSION) ? "session" : "system");
  ownerID = g_bus_own_name(mpris_bus_type, "org.mpris.MediaPlayer2.ShairportSync",
                           G_BUS_NAME_OWNER_FLAGS_NONE, NULL, on_mpris_name_acquired,
                           on_mpris_name_lost, NULL, NULL);
  return 0; // this is just to quieten a compiler warning
}

void stop_mpris_service() {
  if (ownerID) {
    debug(2, "stopping MPRIS service -- unowning ownerID %d.", ownerID);
    g_bus_unown_name(ownerID);
  }
}
