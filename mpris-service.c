/*
 * This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2018--2025
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

#include "dacp.h"

#include "metadata_hub.h"
#include "mpris-service.h"

static guint ownerID = 0;
static GBusType mpris_bus_type = G_BUS_TYPE_SYSTEM; // default is the dbus system message bus

MediaPlayer2 *mprisPlayerSkeleton;
MediaPlayer2Player *mprisPlayerPlayerSkeleton;

double airplay_volume_to_mpris_volume(double sp) {
  if (sp < -30.0)
    sp = -30.0;
  if (sp > 0.0)
    sp = 0.0;
  sp = (sp / 30.0) + 1;
  return sp;
}

double mpris_volume_to_airplay_volume(double sp) {
  sp = (sp - 1.0) * 30.0;
  if (sp < -30.0)
    sp = -30.0;
  if (sp > 0.0)
    sp = 0.0;
  return sp;
}

void mpris_metadata_watcher(struct metadata_bundle *argc, __attribute__((unused)) void *userdata) {
  // debug(1, "MPRIS metadata watcher called");
  char response[100];
  media_player2_player_set_volume(mprisPlayerPlayerSkeleton,
                                  airplay_volume_to_mpris_volume(argc->airplay_volume));
  switch (argc->repeat_status) {
  case RS_NOT_AVAILABLE:
    strcpy(response, "Not Available");
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

  switch (argc->player_state) {
  case PS_NOT_AVAILABLE:
    strcpy(response, "Not Available");
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

  /*
    switch (argc->shuffle_state) {
    case SS_NOT_AVAILABLE:
      strcpy(response, "Not Available");
      break;
    case SS_OFF:
      strcpy(response, "Off");
      break;
    case SS_ON:
      strcpy(response, "On");
      break;
    }

     media_player2_player_set_shuffle_status(mprisPlayerPlayerSkeleton, response);
  */

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

  /*
    // Add the TrackID if we have one
    // Build the Track ID from the 16-byte item_composite_id in hex prefixed by
    // /org/gnome/ShairportSync
    char st[33];
    char *pt = st;
    int it;
    int non_zero = 0;
    for (it = 0; it < 16; it++) {
      if (argc->track_metadata->item_composite_id[it])
        non_zero = 1;
      snprintf(pt, 3, "%02X", argc->track_metadata->item_composite_id[it]);
      pt += 2;
    }
    *pt = 0;

    if (non_zero) {
      // debug(1, "Set ID using composite ID: \"0x%s\".", st);
      char trackidstring[1024];
      snprintf(trackidstring, sizeof(trackidstring), "/org/gnome/ShairportSync/%s", st);
      GVariant *trackid = g_variant_new("o", trackidstring);
      g_variant_builder_add(dict_builder, "{sv}", "mpris:trackid", trackid);
    } else if ((argc->track_metadata) && (argc->track_metadata->item_id)) {
      char trackidstring[128];
      // debug(1, "Set ID using mper ID: \"%u\".",argc->item_id);
      snprintf(trackidstring, sizeof(trackidstring), "/org/gnome/ShairportSync/mper_%u",
               argc->track_metadata->item_id);
      GVariant *trackid = g_variant_new("o", trackidstring);
      g_variant_builder_add(dict_builder, "{sv}", "mpris:trackid", trackid);
    }

  */

  // Build the metadata array
  debug(2, "Build metadata");
  GVariantBuilder *dict_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

  // Add in the artwork URI if it exists.
  if (argc->cover_art_pathname) {
    GVariant *artUrl = g_variant_new("s", argc->cover_art_pathname);
    g_variant_builder_add(dict_builder, "{sv}", "mpris:artUrl", artUrl);
  }

  // Add in the Track ID based on the 'mper' metadata if it is non-zero
  if (argc->item_id_is_valid != 0) {
    char trackidstring[128];
    snprintf(trackidstring, sizeof(trackidstring), "/org/gnome/ShairportSync/%" PRIX64 "",
             argc->item_id);
    GVariant *trackid = g_variant_new("o", trackidstring);
    g_variant_builder_add(dict_builder, "{sv}", "mpris:trackid", trackid);
  }

  // Add the track name if it exists
  if (argc->track_name) {
    GVariant *track_name = g_variant_new("s", argc->track_name);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:title", track_name);
  }

  // Add the album name if it exists
  if (argc->album_name) {
    GVariant *album_name = g_variant_new("s", argc->album_name);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:album", album_name);
  }

  // Add the artist name if it exists
  if (argc->artist_name) {
    GVariantBuilder *artist_as = g_variant_builder_new(G_VARIANT_TYPE("as"));
    g_variant_builder_add(artist_as, "s", argc->artist_name);
    GVariant *artists = g_variant_builder_end(artist_as);
    g_variant_builder_unref(artist_as);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:artist", artists);
  }

  // Add the genre if it exists
  if (argc->genre) {
    GVariantBuilder *genre_as = g_variant_builder_new(G_VARIANT_TYPE("as"));
    g_variant_builder_add(genre_as, "s", argc->genre);
    GVariant *genre = g_variant_builder_end(genre_as);
    g_variant_builder_unref(genre_as);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:genre", genre);
  }

  if (argc->songtime_in_milliseconds_is_valid) {
    uint64_t track_length_in_microseconds = argc->songtime_in_milliseconds;
    track_length_in_microseconds *= 1000; // to microseconds in 64-bit precision
                                          // Make up the track name and album name
    // debug(1, "Set tracklength to %lu.", track_length_in_microseconds);
    GVariant *tracklength = g_variant_new("x", track_length_in_microseconds);
    g_variant_builder_add(dict_builder, "{sv}", "mpris:length", tracklength);
  }

  GVariant *dict = g_variant_builder_end(dict_builder);
  g_variant_builder_unref(dict_builder);
  media_player2_player_set_metadata(mprisPlayerPlayerSkeleton, dict);
}

static gboolean on_handle_quit(MediaPlayer2 *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  debug(1, ">> quit request...");
  config.quit_requested_from_glib_mainloop = 1;
  g_main_loop_quit(config.glib_worker_loop);
  media_player2_complete_quit(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_next(MediaPlayer2Player *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("nextitem");
  media_player2_player_complete_next(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_previous(MediaPlayer2Player *skeleton, GDBusMethodInvocation *invocation,
                                   __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("previtem");
  media_player2_player_complete_previous(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_stop(MediaPlayer2Player *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("stop");
  media_player2_player_complete_stop(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_pause(MediaPlayer2Player *skeleton, GDBusMethodInvocation *invocation,
                                __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("pause");
  media_player2_player_complete_pause(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_play_pause(MediaPlayer2Player *skeleton,
                                     GDBusMethodInvocation *invocation,
                                     __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("playpause");
  media_player2_player_complete_play_pause(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_play(MediaPlayer2Player *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  send_simple_dacp_command("play");
  media_player2_player_complete_play(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_set_volume(MediaPlayer2Player *skeleton,
                                     GDBusMethodInvocation *invocation, const gdouble volume,
                                     __attribute__((unused)) gpointer user_data) {
  double ap_volume = mpris_volume_to_airplay_volume(volume);
  debug(2, "Set mpris volume to %.6f, i.e. airplay volume to %.6f.", volume, ap_volume);
  char command[256] = "";
  snprintf(command, sizeof(command), "setproperty?dmcp.device-volume=%.6f", ap_volume);
  send_simple_dacp_command(command);
  media_player2_player_complete_play(skeleton, invocation);
  return TRUE;
}

static void on_mpris_name_acquired(GDBusConnection *connection, const gchar *name,
                                   __attribute__((unused)) gpointer user_data) {

  const char *empty_string_array[] = {NULL};

  debug(2, "MPRIS well-known interface name \"%s\" acquired on the %s bus.", name,
        (mpris_bus_type == G_BUS_TYPE_SESSION) ? "session" : "system");
  mprisPlayerSkeleton = media_player2_skeleton_new();
  mprisPlayerPlayerSkeleton = media_player2_player_skeleton_new();

  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(mprisPlayerSkeleton), connection,
                                   "/org/mpris/MediaPlayer2", NULL);
  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(mprisPlayerPlayerSkeleton), connection,
                                   "/org/mpris/MediaPlayer2", NULL);

  media_player2_set_desktop_entry(mprisPlayerSkeleton, "shairport-sync");
  media_player2_set_identity(mprisPlayerSkeleton, "Shairport Sync");
  media_player2_set_can_quit(mprisPlayerSkeleton, TRUE);
  media_player2_set_can_raise(mprisPlayerSkeleton, FALSE);
  media_player2_set_has_track_list(mprisPlayerSkeleton, FALSE);
  media_player2_set_supported_uri_schemes(mprisPlayerSkeleton, empty_string_array);
  media_player2_set_supported_mime_types(mprisPlayerSkeleton, empty_string_array);

  media_player2_player_set_playback_status(mprisPlayerPlayerSkeleton, "Stopped");
  media_player2_player_set_loop_status(mprisPlayerPlayerSkeleton, "None");
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

  add_metadata_watcher(mpris_metadata_watcher, NULL);

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
