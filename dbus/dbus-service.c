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
#include <stdlib.h>
#include <string.h>

#include "config.h"

#include "common.h"
#include "player.h"
#include "rtsp.h"

#include "rtp.h"

#ifdef CONFIG_AIRPLAY_2
#include "utilities/plist_gvariant_stuff.h"
#endif

#ifdef CONFIG_DACP_CLIENT
#include "dacp.h"
#endif

#include "dbus-service.h"
#include "metadata/hub.h"
#include "property-preflight/property-preflight-shairportsync.h"
#include "remote/remote.h"
#include "utilities/exit.h"
#include "utilities/general_utilities.h"

#ifdef CONFIG_CONVOLUTION
#include <FFTConvolver/convolver.h>
#endif

ShairportSync *shairportSyncSkeleton;

static GBusType dbus_bus_type = G_BUS_TYPE_SYSTEM; // default is the dbus system message bus
int service_is_running = 0;

ShairportSyncClient *shairportSyncClientSkeleton = NULL;
ShairportSyncDiagnostics *shairportSyncDiagnosticsSkeleton = NULL;
ShairportSyncRemoteControl *shairportSyncRemoteControlSkeleton = NULL;
ShairportSyncAdvancedRemoteControl *shairportSyncAdvancedRemoteControlSkeleton = NULL;

static guint ownerID = 0;

void dbus_metadata_watcher(struct metadata_bundle *argc) {
  char response[100];

  const char *th;
  shairport_sync_advanced_remote_control_set_volume(shairportSyncAdvancedRemoteControlSkeleton,
                                                    argc->speaker_volume);

  shairport_sync_remote_control_set_airplay_volume(shairportSyncRemoteControlSkeleton,
                                                   argc->airplay_volume);
  shairport_sync_advanced_remote_control_set_volume(
      shairportSyncAdvancedRemoteControlSkeleton,
      lround(100 * airplayVolumeToUnitVolume(argc->airplay_volume)));

  shairport_sync_remote_control_set_client(shairportSyncRemoteControlSkeleton, argc->client_ip);
  shairport_sync_remote_control_set_client_name(shairportSyncRemoteControlSkeleton,
                                                argc->client_name);

  // although it's a DACP server, the server is in fact, part of the the AirPlay "client" (their
  // term).
  if (argc->dacp_server_active) {
    shairport_sync_remote_control_set_available(shairportSyncRemoteControlSkeleton, TRUE);
  } else {
    shairport_sync_remote_control_set_available(shairportSyncRemoteControlSkeleton, FALSE);
  }

  if (argc->advanced_dacp_server_active) {
    shairport_sync_advanced_remote_control_set_available(shairportSyncAdvancedRemoteControlSkeleton,
                                                         TRUE);
  } else {
    shairport_sync_advanced_remote_control_set_available(shairportSyncAdvancedRemoteControlSkeleton,
                                                         FALSE);
  }

  if (argc->progress_string) {
    // debug(1, "Check progress string");
    th = shairport_sync_remote_control_get_progress_string(shairportSyncRemoteControlSkeleton);
    if ((th == NULL) || (strcasecmp(th, argc->progress_string) != 0)) {
      // debug(1, "Progress string should be changed");
      shairport_sync_remote_control_set_progress_string(shairportSyncRemoteControlSkeleton,
                                                        argc->progress_string);
    }
  }

  if (argc->frame_position_string) {
    // debug(1, "Check frame position string");
    th = shairport_sync_get_frame_position(shairportSyncSkeleton);
    if ((th == NULL) || (strcasecmp(th, argc->frame_position_string) != 0)) {
      // debug(1, "Frame position string should be changed");
      shairport_sync_set_frame_position(shairportSyncSkeleton, argc->frame_position_string);
    }
  }

  if (argc->first_frame_position_string) {
    // debug(1, "Check first frame position string");
    th = shairport_sync_get_first_frame_position(shairportSyncSkeleton);
    if ((th == NULL) || (strcasecmp(th, argc->first_frame_position_string) != 0)) {
      // debug(1, "First frame position string should be changed");
      shairport_sync_set_first_frame_position(shairportSyncSkeleton,
                                              argc->first_frame_position_string);
    }
  }

  if (argc->stream_type) {
    // debug(1, "Check stream type");
    th = shairport_sync_remote_control_get_stream_type(shairportSyncRemoteControlSkeleton);
    if ((th == NULL) || (strcasecmp(th, argc->stream_type) != 0)) {
      // debug(1, "Stream type string should be changed");
      shairport_sync_remote_control_set_stream_type(shairportSyncRemoteControlSkeleton,
                                                    argc->stream_type);
    }
  }

  if (argc->output_format) {
    // debug(1, "Check output format");
    th = shairport_sync_get_output_format(shairportSyncSkeleton);
    if ((th == NULL) || (strcasecmp(th, argc->output_format) != 0)) {
      // debug(1, "Output format string should be changed");
      shairport_sync_set_output_format(shairportSyncSkeleton, argc->output_format);
    }
  }

  if (argc->source_format) {
    // debug(1, "Check source format");
    th = shairport_sync_get_source_format(shairportSyncSkeleton);
    if ((th == NULL) || (strcasecmp(th, argc->source_format) != 0)) {
      // debug(1, "Source format string should be changed");
      shairport_sync_set_source_format(shairportSyncSkeleton, argc->source_format);
    }
  }

  switch (argc->player_state) {
  case PS_NOT_AVAILABLE:
    shairport_sync_remote_control_set_player_state(shairportSyncRemoteControlSkeleton,
                                                   "Not Available");
    break;
  case PS_STOPPED:
    shairport_sync_remote_control_set_player_state(shairportSyncRemoteControlSkeleton, "Stopped");
    break;
  case PS_PAUSED:
    shairport_sync_remote_control_set_player_state(shairportSyncRemoteControlSkeleton, "Paused");
    break;
  case PS_PLAYING:
    shairport_sync_remote_control_set_player_state(shairportSyncRemoteControlSkeleton, "Playing");
    break;
  default:
    debug(1, "This should never happen.");
  }

  switch (argc->play_status) {
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
  default:
    debug(1, "This should never happen.");
  }

  th = shairport_sync_advanced_remote_control_get_playback_status(
      shairportSyncAdvancedRemoteControlSkeleton);

  // only set this if it's different
  if ((th == NULL) || (strcasecmp(th, response) != 0)) {
    debug(3, "Playback Status should be changed");
    shairport_sync_advanced_remote_control_set_playback_status(
        shairportSyncAdvancedRemoteControlSkeleton, response);
  }

  // repeat status (was loop status)
  switch (argc->repeat_status) {
  case RS_NOT_AVAILABLE:
    shairport_sync_advanced_remote_control_set_loop_status(
        shairportSyncAdvancedRemoteControlSkeleton, "Not Available");
    break;
  case RS_OFF:
    shairport_sync_advanced_remote_control_set_loop_status(
        shairportSyncAdvancedRemoteControlSkeleton, "Off");
    break;
  case RS_ONE:
    shairport_sync_advanced_remote_control_set_loop_status(
        shairportSyncAdvancedRemoteControlSkeleton, "One");
    break;
  case RS_ALL:
    shairport_sync_advanced_remote_control_set_loop_status(
        shairportSyncAdvancedRemoteControlSkeleton, "All");
    break;
  default:
    shairport_sync_advanced_remote_control_set_loop_status(
        shairportSyncAdvancedRemoteControlSkeleton, "Error");
  }

  //

  switch (argc->shuffle_status) {
  case SS_NOT_AVAILABLE:
    shairport_sync_advanced_remote_control_set_shuffle(shairportSyncAdvancedRemoteControlSkeleton,
                                                       "Not Available");
    break;
  case SS_OFF:
    shairport_sync_advanced_remote_control_set_shuffle(shairportSyncAdvancedRemoteControlSkeleton,
                                                       "Off");
    break;
  case SS_ON:
    shairport_sync_advanced_remote_control_set_shuffle(shairportSyncAdvancedRemoteControlSkeleton,
                                                       "On");
    break;
  default:
    shairport_sync_advanced_remote_control_set_shuffle(shairportSyncAdvancedRemoteControlSkeleton,
                                                       "Error");
  }

  // Build the metadata array
  // debug(2, "Build metadata");
  GVariantBuilder *dict_builder = g_variant_builder_new(G_VARIANT_TYPE("a{sv}"));

  // Add in the artwork URI if it exists.
  if (argc->npi.cover_art_pathname) {
    GVariant *artUrl = g_variant_new("s", argc->npi.cover_art_pathname);
    g_variant_builder_add(dict_builder, "{sv}", "mpris:artUrl", artUrl);
  }

  // Add in the Track ID based on the 'mper' metadata if it is valid
  if (is_valid_uint64_record(&argc->npi.item_id)) {
    char trackidstring[128];
    snprintf(trackidstring, sizeof(trackidstring), "/org/gnome/ShairportSync/%" PRIu64 "",
             argc->npi.item_id.item);
    GVariant *trackid = g_variant_new("o", trackidstring);
    g_variant_builder_add(dict_builder, "{sv}", "mpris:trackid", trackid);
  }

  // Add in the Song Data Kind based on the 'asdk' metadata if it is valid
  // It seems that this is 0 for a timed play, e.g. a track or an album, but is 1 for an untimed
  // play, such as a stream.

  if (is_valid_uint64_record(&argc->npi.song_data_kind)) {
    GVariant *songdatakind = g_variant_new_uint32(argc->npi.song_data_kind.item);
    g_variant_builder_add(dict_builder, "{sv}", "sps:songdatakind", songdatakind);
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

  // Add the artist name list if it exists
  if (argc->npi.artist_name) {
    GVariantBuilder *artist_as = g_variant_builder_new(G_VARIANT_TYPE("as"));
    g_variant_builder_add(artist_as, "s", argc->npi.artist_name);
    GVariant *artists = g_variant_builder_end(artist_as);
    g_variant_builder_unref(artist_as);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:artist", artists);
  }

  // Add the album artist list if it exists
  if (argc->npi.album_artist_name) {
    GVariantBuilder *album_artist_as = g_variant_builder_new(G_VARIANT_TYPE("as"));
    g_variant_builder_add(album_artist_as, "s", argc->npi.album_artist_name);
    GVariant *album_artists = g_variant_builder_end(album_artist_as);
    g_variant_builder_unref(album_artist_as);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:albumArtist", album_artists);
  }

  // Add the composer list if it exists
  if (argc->npi.composer) {
    GVariantBuilder *composer_as = g_variant_builder_new(G_VARIANT_TYPE("as"));
    g_variant_builder_add(composer_as, "s", argc->npi.composer);
    GVariant *composers = g_variant_builder_end(composer_as);
    g_variant_builder_unref(composer_as);
    g_variant_builder_add(dict_builder, "{sv}", "xesam:composer", composers);
  }

  // Add the genre list if it exists
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
  shairport_sync_remote_control_set_metadata(shairportSyncRemoteControlSkeleton, dict);

#ifdef CONFIG_AIRPLAY_2
  // output the NowPlayingPlist stuff
  GVariant *npi = plist_to_gvariant(argc->npi.npi_plist);
  if (argc->npi.npi_plist != NULL) {
    shairport_sync_client_set_now_playing_information(shairportSyncClientSkeleton, npi);
  }

  // output the CommandInformation stuff
  GVariant *sc = plist_to_gvariant(argc->supported_commands_plist);
  if (sc != NULL) {
    shairport_sync_client_set_command_information(shairportSyncClientSkeleton, sc);
  }

#endif
}

static gboolean on_handle_set_volume(ShairportSyncAdvancedRemoteControl *skeleton,
                                     GDBusMethodInvocation *invocation, const gint volume,
                                     __attribute__((unused)) gpointer user_data) {
  debug(4, "D-Bus set \"advanced\" volume (integer percent) to %d.", volume);
  remote_set_integer_percent_volume(volume);
  shairport_sync_advanced_remote_control_complete_set_volume(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_fast_forward(ShairportSyncRemoteControl *skeleton,
                                       GDBusMethodInvocation *invocation,
                                       __attribute__((unused)) gpointer user_data) {
  debug(4, "D-Bus fast forward.");
  remote_simple_command(rcsc_fast_forward);
  shairport_sync_remote_control_complete_fast_forward(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_rewind(ShairportSyncRemoteControl *skeleton,
                                 GDBusMethodInvocation *invocation,
                                 __attribute__((unused)) gpointer user_data) {
  debug(4, "D-Bus rewind.");
  remote_simple_command(rcsc_rewind);
  shairport_sync_remote_control_complete_rewind(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_toggle_mute(ShairportSyncRemoteControl *skeleton,
                                      GDBusMethodInvocation *invocation,
                                      __attribute__((unused)) gpointer user_data) {
#ifdef CONFIG_DACP_CLIENT
  send_simple_dacp_command("mutetoggle");
#endif
  shairport_sync_remote_control_complete_toggle_mute(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_next(ShairportSyncRemoteControl *skeleton,
                               GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  debug(4, "D-Bus next item.");
  remote_simple_command(rcsc_next_item);
  shairport_sync_remote_control_complete_next(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_previous(ShairportSyncRemoteControl *skeleton,
                                   GDBusMethodInvocation *invocation,
                                   __attribute__((unused)) gpointer user_data) {
  debug(4, "D-Bus previous item.");
  remote_simple_command(rcsc_previous_item);
  shairport_sync_remote_control_complete_previous(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_pause(ShairportSyncRemoteControl *skeleton,
                                GDBusMethodInvocation *invocation,
                                __attribute__((unused)) gpointer user_data) {
  debug(4, "D-Bus pause.");
  remote_simple_command(rcsc_pause);
  shairport_sync_remote_control_complete_pause(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_play_pause(ShairportSyncRemoteControl *skeleton,
                                     GDBusMethodInvocation *invocation,
                                     __attribute__((unused)) gpointer user_data) {
  debug(4, "D-Bus playpause.");
  remote_simple_command(rcsc_play_pause);
  shairport_sync_remote_control_complete_play_pause(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_play(ShairportSyncRemoteControl *skeleton,
                               GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  debug(4, "D-Bus play.");
  remote_simple_command(rcsc_play);
  shairport_sync_remote_control_complete_play(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_stop(ShairportSyncRemoteControl *skeleton,
                               GDBusMethodInvocation *invocation,
                               __attribute__((unused)) gpointer user_data) {
  debug(4, "D-Bus stop.");
  remote_simple_command(rcsc_stop);
  shairport_sync_remote_control_complete_stop(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_resume(ShairportSyncRemoteControl *skeleton,
                                 GDBusMethodInvocation *invocation,
                                 __attribute__((unused)) gpointer user_data) {
#ifdef CONFIG_DACP_CLIENT
  send_simple_dacp_command("playresume");
#endif
  shairport_sync_remote_control_complete_resume(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_shuffle_songs(ShairportSyncRemoteControl *skeleton,
                                        GDBusMethodInvocation *invocation,
                                        __attribute__((unused)) gpointer user_data) {
  debug(4, "D-Bus shuffle_songs.");
  remote_simple_command(rcsc_toggle_shuffle);
  shairport_sync_remote_control_complete_shuffle_songs(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_volume_up(ShairportSyncRemoteControl *skeleton,
                                    GDBusMethodInvocation *invocation,
                                    __attribute__((unused)) gpointer user_data) {
  debug(4, "D-Bus VolumeUp");
  remote_simple_command(rcsc_volume_up);
  shairport_sync_remote_control_complete_volume_up(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_volume_down(ShairportSyncRemoteControl *skeleton,
                                      GDBusMethodInvocation *invocation,
                                      __attribute__((unused)) gpointer user_data) {
  debug(4, "D-Bus VolumeDown");
  remote_simple_command(rcsc_volume_down);
  shairport_sync_remote_control_complete_volume_down(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_set_airplay_volume(ShairportSyncRemoteControl *skeleton,
                                             GDBusMethodInvocation *invocation,
                                             const gdouble volume,
                                             __attribute__((unused)) gpointer user_data) {

  debug(4, "D-Bus set airplay volume to %.3f.", volume);
  remote_set_airplay_volume(volume);
  shairport_sync_remote_control_complete_set_airplay_volume(skeleton, invocation);
  return TRUE;
}

gboolean notify_elapsed_time_callback(ShairportSyncDiagnostics *skeleton,
                                      __attribute__((unused)) gpointer user_data) {
  // debug(1, "\"notify_elapsed_time_callback\" called.");
  if (shairport_sync_diagnostics_get_elapsed_time(skeleton)) {
    config.debugger_show_elapsed_time = 1;
    debug(1, ">> include elapsed time in logs");
  } else {
    config.debugger_show_elapsed_time = 0;
    debug(1, ">> do not include elapsed time in logs");
  }
  return TRUE;
}

gboolean notify_delta_time_callback(ShairportSyncDiagnostics *skeleton,
                                    __attribute__((unused)) gpointer user_data) {
  // debug(1, "\"notify_delta_time_callback\" called.");
  if (shairport_sync_diagnostics_get_delta_time(skeleton)) {
    config.debugger_show_relative_time = 1;
    debug(1, ">> include delta time in logs");
  } else {
    config.debugger_show_relative_time = 0;
    debug(1, ">> do not include delta time in logs");
  }
  return TRUE;
}

gboolean notify_file_and_line_callback(ShairportSyncDiagnostics *skeleton,
                                       __attribute__((unused)) gpointer user_data) {
  // debug(1, "\"notify_file_and_line_callback\" called.");
  if (shairport_sync_diagnostics_get_file_and_line(skeleton)) {
    config.debugger_show_file_and_line = 1;
    debug(1, ">> include file and line in logs");
  } else {
    config.debugger_show_file_and_line = 0;
    debug(1, ">> do not include file and line in logs");
  }
  return TRUE;
}

gboolean notify_statistics_callback(ShairportSyncDiagnostics *skeleton,
                                    __attribute__((unused)) gpointer user_data) {
  // debug(1, "\"notify_statistics_callback\" called.");
  if (shairport_sync_diagnostics_get_statistics(skeleton)) {
    debug(1, ">> log statistics");
    if (config.statistics_requested == 0)
      statistics_row = 0; // redraw the header line
    config.statistics_requested = 1;
  } else {
    debug(1, ">> do not log statistics");
    config.statistics_requested = 0;
  }
  return TRUE;
}

gboolean notify_verbosity_callback(ShairportSyncDiagnostics *skeleton,
                                   __attribute__((unused)) gpointer user_data) {
  gint th = shairport_sync_diagnostics_get_verbosity(skeleton);
  if ((th >= 0) && (th <= 3)) {
    if (th == 0)
      debug(1, ">> set log verbosity to %d.", th);
    if (((debug_level() == 0) && (th != 0)) || ((debug_level() != 0) && (th == 0)))
      statistics_row = 0; // if the debug level changes, redraw the header line
    set_debug_level(th);
    debug(1, ">> set log verbosity to %d.", th);
  } else {
    debug(1, ">> invalid log verbosity: %d. Ignored.", th);
    shairport_sync_diagnostics_set_verbosity(skeleton, debug_level());
  }
  return TRUE;
}

/*
// this is no longer needed at all
gboolean notify_disable_standby_callback(ShairportSync *skeleton,
                                         __attribute__((unused)) gpointer user_data) {
  // debug(1, "\"notify_disable_standby_callback\" called.");
  if (shairport_sync_get_disable_standby(skeleton)) {
    debug(1, ">> disable standby mode");
    config.keep_dac_busy = 1;
  } else {
    debug(1, ">> do not disable standby mode");
    config.keep_dac_busy = 0;
  }
  return TRUE;
}
#ifdef CONFIG_CONVOLUTION
gboolean notify_convolution_enabled_callback(ShairportSync *skeleton,
                                             __attribute__((unused)) gpointer user_data) {
  // debug(1, "\"notify_convolution_callback\" called.");
  if (shairport_sync_get_convolution_enabled(skeleton)) {
    debug(1, ">> activate convolution impulse response filter");
    config.convolution_enabled = 1;
  } else {
    debug(1, ">> deactivate convolution impulse response filter");
    config.convolution_enabled = 0;
    convolver_clear_state();
  }
  return TRUE;
}
#else
gboolean notify_convolution_enabled_callback(__attribute__((unused)) ShairportSync *skeleton,
                                             __attribute__((unused)) gpointer user_data) {
  warn(">> Convolution support is not built in to this build of Shairport Sync.");
  return TRUE;
}
#endif


#ifdef CONFIG_CONVOLUTION
gboolean
notify_convolution_maximum_length_in_seconds_callback(ShairportSync *skeleton,
                                                      __attribute__((unused)) gpointer user_data) {

  gdouble th = shairport_sync_get_convolution_maximum_length_in_seconds(skeleton);
  if ((th >= 0.0) && (th <= 15.0)) {
    debug(1, ">> set convolution maximum length in seconds to %f.", th);
    config.convolution_max_length_in_seconds = th;
  } else {
    debug(1, ">> invalid convolution gain: %f. Ignored.", th);
    shairport_sync_set_convolution_maximum_length_in_seconds(
        skeleton, config.convolution_max_length_in_seconds);
  }
  return TRUE;
}
#else
gboolean notify_convolution_maximum_length_in_seconds_callback(
    __attribute__((unused)) ShairportSync *skeleton, __attribute__((unused)) gpointer user_data) {
  warn(">> Convolution support is not built in to this build of Shairport Sync.");
  return TRUE;
}
#endif

#ifdef CONFIG_CONVOLUTION
gboolean notify_convolution_gain_callback(ShairportSync *skeleton,
                                          __attribute__((unused)) gpointer user_data) {

  gdouble th = shairport_sync_get_convolution_gain(skeleton);
  if ((th <= 18.0) && (th >= -60.0)) {
    debug(1, ">> set convolution gain to %f.", th);
    config.convolution_gain = th;
  } else {
    debug(1, ">> invalid convolution gain: %f. Ignored.", th);
    shairport_sync_set_convolution_gain(skeleton, config.convolution_gain);
  }
  return TRUE;
}
#else
gboolean notify_convolution_gain_callback(__attribute__((unused)) ShairportSync *skeleton,
                                          __attribute__((unused)) gpointer user_data) {
  warn(">> Convolution support is not built in to this build of Shairport Sync.");
  return TRUE;
}
#endif

#ifdef CONFIG_CONVOLUTION
gboolean
notify_convolution_impulse_response_files_callback(ShairportSync *skeleton,
                                                   __attribute__((unused)) gpointer user_data) {
  char *th = (char *)shairport_sync_get_convolution_impulse_response_files(skeleton);
  if (th != NULL) {
    debug(1, ">> freeing current configuration impulse response filter files.");
    free_ir_filenames(config.convolution_ir_files, config.convolution_ir_file_count);
    config.convolution_ir_files = NULL;
    config.convolution_ir_file_count = 0;

    config.convolution_ir_files = parse_ir_filenames(th, &config.convolution_ir_file_count);
    sanity_check_ir_files(1, config.convolution_ir_files, config.convolution_ir_file_count);
    debug(1, ">> setting %d configuration impulse response filter%s",
          config.convolution_ir_file_count, config.convolution_ir_file_count == 1 ? "" : "s");
    config.convolution_ir_files_updated = 1;
  }
  return TRUE;
}
#else
gboolean
notify_convolution_impulse_response_files_callback(__attribute__((unused)) ShairportSync *skeleton,
                                                   __attribute__((unused)) gpointer user_data) {
  __attribute__((unused)) char *th =
      (char *)shairport_sync_get_convolution_impulse_response_files(skeleton);
  return TRUE;
}
#endif

gboolean notify_loudness_enabled_callback(ShairportSync *skeleton,
                                          __attribute__((unused)) gpointer user_data) {
  // debug(1, "\"notify_loudness_callback\" called.");
  if (shairport_sync_get_loudness_enabled(skeleton)) {
    debug(1, ">> activate loudness filter");
    config.loudness_enabled = 1;
  } else {
    debug(1, ">> deactivate loudness filter");
    config.loudness_enabled = 0;
  }
  return TRUE;
}

gboolean notify_loudness_threshold_callback(ShairportSync *skeleton,
                                            __attribute__((unused)) gpointer user_data) {
  gdouble th = shairport_sync_get_loudness_threshold(skeleton);
  if ((th <= 0.0) && (th >= -100.0)) {
    debug(1, ">> set loudness threshold to %f.", th);
    config.loudness_reference_volume_db = th;
  } else {
    debug(1, ">> invalid loudness threshold: %f. Ignored.", th);
  }
  return TRUE;
}

gboolean notify_drift_tolerance_callback(ShairportSync *skeleton,
                                         __attribute__((unused)) gpointer user_data) {
  gdouble dt = shairport_sync_get_drift_tolerance(skeleton);
  if ((dt >= 0.0) && (dt <= 2.0)) {
    debug(1, ">> set drift tolerance to %f seconds.", dt);
    config.tolerance = dt;
  } else {
    debug(1, ">> invalid drift tolerance: %f seconds. Ignored.", dt);
    shairport_sync_set_drift_tolerance(skeleton, config.tolerance);
  }
  return TRUE;
}

gboolean notify_volume_callback(ShairportSync *skeleton,
                                __attribute__((unused)) gpointer user_data) {
  gdouble iv = shairport_sync_get_volume(skeleton);
  if (((iv >= -30.0) && (iv <= 0.0)) || (iv == -144.0)) {
    debug(2, ">> set volume to %7.4f.", iv);
    config.airplay_volume = iv;
  } else {
    debug(1, ">> invalid volume: %f. Ignored.", iv);
    shairport_sync_set_volume(skeleton, config.airplay_volume);
  }
  return TRUE;
}

gboolean notify_disable_standby_mode_callback(ShairportSync *skeleton,
                                              __attribute__((unused)) gpointer user_data) {
  char *th = (char *)shairport_sync_get_disable_standby_mode(skeleton);
  if ((strcmp(th, "No") == 0) || (strcmp(th, "Off") == 0) || (strcmp(th, "Never") == 0)) {
    config.disable_standby_mode = disable_standby_off;
    config.keep_dac_busy = 0;
  } else if ((strcmp(th, "Yes") == 0) || (strcmp(th, "On") == 0) || (strcmp(th, "Always") == 0)) {
    config.disable_standby_mode = disable_standby_always;
    config.keep_dac_busy = 1;
  } else if (strcmp(th, "Auto") == 0) {
    config.disable_standby_mode = disable_standby_auto;
  }
  return TRUE;
}

gboolean notify_alacdecoder_callback(ShairportSync *skeleton,
                                     __attribute__((unused)) gpointer user_data) {
  char *th = (char *)shairport_sync_get_alacdecoder(skeleton);

#ifdef CONFIG_AIRPLAY_2
  if (strcasecmp(th, "FFmpeg") != 0) {
    warn(" This request, to set the decoder to \"%s\", is ignored. For AirPlay 2, the FFmpeg "
         "decoder is always used.",
         th);
  }
#else
  if ((strcasecmp(th, "Hammerton") == 0) &&
      ((config.decoders_supported & (1 << decoder_hammerton)) != 0))
    config.decoder_in_use = 1 << decoder_hammerton;
  else if ((strcasecmp(th, "Apple") == 0) &&
           ((config.decoders_supported & (1 << decoder_apple_alac)) != 0))
    config.decoder_in_use = 1 << decoder_apple_alac;
  else if ((strcasecmp(th, "FFmpeg") == 0) &&
           ((config.decoders_supported & (1 << decoder_ffmpeg_alac)) != 0))
    config.decoder_in_use = 1 << decoder_ffmpeg_alac;
  else {
    warn("An unrecognised or unsupported decoder: \"%s\" was requested via D-Bus interface. "
         "(Possibly "
         "support for this decoder was not compiled "
         "into this version of Shairport Sync.)",
         th);
  }
#endif

  return TRUE;
}


gboolean notify_interpolation_callback(ShairportSync *skeleton,
                                       __attribute__((unused)) gpointer user_data) {
  char *th = (char *)shairport_sync_get_interpolation(skeleton);
  // #ifdef CONFIG_SOXR
  if (strcasecmp(th, "Basic") == 0)
    config.packet_stuffing = ST_basic;
#ifdef CONFIG_SOXR
  else if (strcasecmp(th, "Soxr") == 0)
    config.packet_stuffing = ST_soxr;
#endif
  else if (strcasecmp(th, "Auto") == 0)
    config.packet_stuffing = ST_auto;
  else if (strcasecmp(th, "Vernier") == 0)
    config.packet_stuffing = ST_vernier;
  else {
#ifdef CONFIG_SOXR
    warn("An unrecognised interpolation method: \"%s\" was requested via the D-Bus interface.", th);
#else
    if (strcasecmp(th, "soxr") == 0) {
      warn("Soxr interpolation is not supported in this edition of Shairport Sync.");
    } else {
      warn("An unrecognised interpolation method: \"%s\" was requested via the D-Bus interface.",
           th);
    }
#endif
    // set the shairport_sync_set_interpolation on the D-Bus interface back to what it is in the
    // setting.
    switch (config.packet_stuffing) {
    case ST_basic:
      shairport_sync_set_interpolation(skeleton, "basic");
      break;
    case ST_soxr:
      shairport_sync_set_interpolation(skeleton, "soxr");
      break;
    case ST_vernier:
      shairport_sync_set_interpolation(skeleton, "vernier");
      break;
    case ST_auto:
      shairport_sync_set_interpolation(skeleton, "auto");
      break;
    default:
      debug(1, "This should never happen, but defaulting to \"vernier\" interpolation!");
      shairport_sync_set_interpolation(skeleton, "vernier");
      break;
    }
  }
  return TRUE;
}

gboolean notify_volume_control_profile_callback(ShairportSync *skeleton,
                                                __attribute__((unused)) gpointer user_data) {
  char *th = (char *)shairport_sync_get_volume_control_profile(skeleton);
  //  enum volume_control_profile_type previous_volume_control_profile =
  //  config.volume_control_profile;
  if (strcasecmp(th, "standard") == 0)
    config.volume_control_profile = VCP_standard;
  else if (strcasecmp(th, "flat") == 0)
    config.volume_control_profile = VCP_flat;
  else if (strcasecmp(th, "dasl_tapered") == 0)
    config.volume_control_profile = VCP_dasl_tapered;
  else {
    warn("Unrecognised Volume Control Profile: \"%s\".", th);
    switch (config.volume_control_profile) {
    case VCP_standard:
      shairport_sync_set_volume_control_profile(skeleton, "standard");
      break;
    case VCP_flat:
      shairport_sync_set_volume_control_profile(skeleton, "flat");
      break;
    case VCP_dasl_tapered:
      shairport_sync_set_volume_control_profile(skeleton, "dasl_tapered");
      break;
    default:
      debug(1, "This should never happen!");
      shairport_sync_set_volume_control_profile(skeleton, "standard");
      break;
    }
  }
  return TRUE;
}
*/

static gboolean on_handle_quit(ShairportSync *skeleton, GDBusMethodInvocation *invocation,
                               __attribute__((unused)) const gchar *command,
                               __attribute__((unused)) gpointer user_data) {
  debug(1, ">> quit request...");
  shairport_sync_complete_quit(skeleton, invocation);
  exit_request(EXIT_SUCCESS);
  return TRUE;
}

static gboolean on_handle_mule(ShairportSync *skeleton, GDBusMethodInvocation *invocation,
                               const gint parameter, __attribute__((unused)) gpointer user_data) {
  debug(1, "Mule with parameter %d.", parameter);
#ifdef CONFIG_AIRPLAY_2
  ap2_event_send_dev_mule(parameter);
#else
  debug(1, "Mule is only availabe in AirPlay 2 builds.");
#endif
  shairport_sync_complete_mule(skeleton, invocation);
  return TRUE;
}

/*
static gboolean on_handle_set_shuffle(ShairportSyncClient *skeleton,
                                      GDBusMethodInvocation *invocation, const gchar *modeString,
                                      __attribute__((unused)) gpointer user_data) {
  debug(1, "SetShuffle with mode \"%s\".", modeString);
  shuffle_status_type requested_shuffle_mode = SS_NOT_AVAILABLE;
  if (strcasecmp(modeString, "off") == 0) {
    requested_shuffle_mode = SS_OFF;
  } else if (strcasecmp(modeString, "on") == 0) {
    requested_shuffle_mode = SS_ON;
  } else {
    warn("Illegal SetShuffle: \"%s\" -- ignored.", modeString);
    requested_shuffle_mode = SS_NOT_AVAILABLE;
  }
  if (requested_shuffle_mode != SS_NOT_AVAILABLE) {
    remote_set_shuffle_mode(requested_shuffle_mode);
  }
  shairport_sync_client_complete_set_shuffle(skeleton, invocation);
  return TRUE;
}
*/

static gboolean on_handle_remote_command(ShairportSync *skeleton, GDBusMethodInvocation *invocation,
                                         const gchar *command,
                                         __attribute__((unused)) gpointer user_data) {
  debug(1, "RemoteCommand with command \"%s\".", command);
  int reply = 0;
  char *client_reply_hex = "";
#ifdef CONFIG_DACP_CLIENT
  char *client_reply = NULL;
  ssize_t reply_size = 0;
  reply = dacp_send_command((const char *)command, &client_reply, &reply_size);
  client_reply_hex = alloca(reply_size * 2 + 1);
  if (client_reply_hex) {
    char *p = client_reply_hex;
    if (client_reply) {
      char *q = client_reply;
      int i;
      for (i = 0; i < reply_size; i++) {
        snprintf(p, 3, "%02X", *q);
        p += 2;
        q++;
      }
    }
    *p = '\0';
  }
#endif
  shairport_sync_complete_remote_command(skeleton, invocation, reply, client_reply_hex);
  return TRUE;
}

static gboolean on_handle_drop_session(ShairportSync *skeleton, GDBusMethodInvocation *invocation,
                                       __attribute__((unused)) gpointer user_data) {
  stop_play(); // stop any current session and don't replace it
  shairport_sync_complete_drop_session(skeleton, invocation);
  return TRUE;
}

static gboolean on_handle_set_frame_position_update_interval(
    ShairportSync *skeleton, GDBusMethodInvocation *invocation, const gdouble seconds,
    __attribute__((unused)) gpointer user_data) {
  debug(1, ">> set frame position update interval to %.6f.", seconds);
  config.metadata_progress_interval = seconds;
  shairport_sync_complete_set_frame_position_update_interval(skeleton, invocation);
  return TRUE;
}

static void on_dbus_name_acquired(GDBusConnection *connection, const gchar *name,
                                  __attribute__((unused)) gpointer user_data) {
  debug(1, "Shairport Sync native D-Bus interface \"%s\" acquired on the %s bus.", name,
        (dbus_bus_type == G_BUS_TYPE_SESSION) ? "session" : "system");

  // define the skeletons

  shairportSyncSkeleton = property_preflight_shairport_sync_skeleton_new();
  shairportSyncClientSkeleton = property_preflight_shairport_sync_client_skeleton_new();
  shairportSyncDiagnosticsSkeleton = shairport_sync_diagnostics_skeleton_new();
  shairportSyncRemoteControlSkeleton =
      property_preflight_shairport_sync_remote_control_skeleton_new();
  shairportSyncAdvancedRemoteControlSkeleton =
      property_preflight_shairport_sync_advanced_remote_control_skeleton_new();

  // set initial D-Bus interface values to correspond with initial Shairport Sync settings

  shairport_sync_set_loudness_threshold(SHAIRPORT_SYNC(shairportSyncSkeleton),
                                        config.loudness_reference_volume_db);
  shairport_sync_set_drift_tolerance(SHAIRPORT_SYNC(shairportSyncSkeleton), config.tolerance);
  shairport_sync_set_volume(SHAIRPORT_SYNC(shairportSyncSkeleton), config.airplay_volume);

  if ((config.decoder_in_use & (1 << decoder_hammerton)) != 0) {
    shairport_sync_set_alacdecoder(SHAIRPORT_SYNC(shairportSyncSkeleton), "Hammerton");
  } else if ((config.decoder_in_use & (1 << decoder_apple_alac)) != 0) {
    shairport_sync_set_alacdecoder(SHAIRPORT_SYNC(shairportSyncSkeleton), "Apple");
  } else if ((config.decoder_in_use & (1 << decoder_ffmpeg_alac)) != 0) {
    shairport_sync_set_alacdecoder(SHAIRPORT_SYNC(shairportSyncSkeleton), "FFmpeg");
  }

  shairport_sync_set_active(SHAIRPORT_SYNC(shairportSyncSkeleton), FALSE);

  switch (config.disable_standby_mode) {
  case disable_standby_off:
    shairport_sync_set_disable_standby_mode(SHAIRPORT_SYNC(shairportSyncSkeleton), "Off");
    break;
  case disable_standby_always:
    shairport_sync_set_disable_standby_mode(SHAIRPORT_SYNC(shairportSyncSkeleton), "Always");
    break;
  case disable_standby_auto:
    shairport_sync_set_disable_standby_mode(SHAIRPORT_SYNC(shairportSyncSkeleton), "Auto");
    break;
  default:
    debug(1, "invalid disable_standby mode!");
    break;
  }
  if (config.packet_stuffing == ST_basic) {
    shairport_sync_set_interpolation(SHAIRPORT_SYNC(shairportSyncSkeleton), "Basic");
  } else if (config.packet_stuffing == ST_auto) {
    shairport_sync_set_interpolation(SHAIRPORT_SYNC(shairportSyncSkeleton), "Auto");
  } else if (config.packet_stuffing == ST_vernier) {
    shairport_sync_set_interpolation(SHAIRPORT_SYNC(shairportSyncSkeleton), "Vernier");
  } else {
    shairport_sync_set_interpolation(SHAIRPORT_SYNC(shairportSyncSkeleton), "Soxr");
  }
  if (config.volume_control_profile == VCP_standard)
    shairport_sync_set_volume_control_profile(SHAIRPORT_SYNC(shairportSyncSkeleton), "Standard");
  else if (config.volume_control_profile == VCP_dasl_tapered)
    shairport_sync_set_volume_control_profile(SHAIRPORT_SYNC(shairportSyncSkeleton), "DASL");
  else
    shairport_sync_set_volume_control_profile(SHAIRPORT_SYNC(shairportSyncSkeleton), "Flat");

  if (config.loudness_enabled == 0) {
    shairport_sync_set_loudness_enabled(SHAIRPORT_SYNC(shairportSyncSkeleton), FALSE);
  } else {
    shairport_sync_set_loudness_enabled(SHAIRPORT_SYNC(shairportSyncSkeleton), TRUE);
  }

#ifdef CONFIG_CONVOLUTION
  if (config.convolution_enabled == 0) {
    shairport_sync_set_convolution_enabled(SHAIRPORT_SYNC(shairportSyncSkeleton), FALSE);
  } else {
    shairport_sync_set_convolution_enabled(SHAIRPORT_SYNC(shairportSyncSkeleton), TRUE);
  }

  const char *str = NULL;
  if ((config.cfg != NULL) &&
      (config_lookup_non_empty_string(config.cfg, "dsp.convolution_ir_files", &str))) {
    shairport_sync_set_convolution_impulse_response_files(SHAIRPORT_SYNC(shairportSyncSkeleton),
                                                          str);
  } else {
    shairport_sync_set_convolution_impulse_response_files(SHAIRPORT_SYNC(shairportSyncSkeleton),
                                                          NULL);
  }
  shairport_sync_set_convolution_maximum_length_in_seconds(
      SHAIRPORT_SYNC(shairportSyncSkeleton), config.convolution_max_length_in_seconds);
#endif

  shairport_sync_set_service_name(SHAIRPORT_SYNC(shairportSyncSkeleton), config.service_name);

#ifdef CONFIG_AIRPLAY_2
  if (config.service_type == APST_airplay2) {
    shairport_sync_set_protocol(SHAIRPORT_SYNC(shairportSyncSkeleton), "AirPlay 2");
  } else {
#endif
    shairport_sync_set_protocol(SHAIRPORT_SYNC(shairportSyncSkeleton), "AirPlay");
#ifdef CONFIG_AIRPLAY_2
  }
#endif

  shairport_sync_set_version(SHAIRPORT_SYNC(shairportSyncSkeleton), PACKAGE_VERSION);
  char *vs = get_version_string();
  shairport_sync_set_version_string(SHAIRPORT_SYNC(shairportSyncSkeleton), vs);
  if (vs)
    free(vs);

  shairport_sync_diagnostics_set_verbosity(
      SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), debug_level());

  if (config.statistics_requested == 0) {
    shairport_sync_diagnostics_set_statistics(
        SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), FALSE);
  } else {
    shairport_sync_diagnostics_set_statistics(
        SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), TRUE);
  }

  if (config.debugger_show_elapsed_time == 0) {
    shairport_sync_diagnostics_set_elapsed_time(
        SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), FALSE);
  } else {
    shairport_sync_diagnostics_set_elapsed_time(
        SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), TRUE);
  }

  if (config.debugger_show_relative_time == 0) {
    shairport_sync_diagnostics_set_delta_time(
        SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), FALSE);
  } else {
    shairport_sync_diagnostics_set_delta_time(
        SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), TRUE);
  }

  if (config.debugger_show_file_and_line == 0) {
    shairport_sync_diagnostics_set_file_and_line(
        SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), FALSE);
  } else {
    shairport_sync_diagnostics_set_file_and_line(
        SHAIRPORT_SYNC_DIAGNOSTICS(shairportSyncDiagnosticsSkeleton), TRUE);
  }

  shairport_sync_remote_control_set_metadata(shairportSyncRemoteControlSkeleton,
                                             g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0));
  shairport_sync_client_set_now_playing_information(
      shairportSyncClientSkeleton, g_variant_new_array(G_VARIANT_TYPE("{sv}"), NULL, 0));
  shairport_sync_client_set_command_information(shairportSyncClientSkeleton,
                                                g_variant_new_array(G_VARIANT_TYPE("v"), NULL, 0));

  shairport_sync_remote_control_set_player_state(shairportSyncRemoteControlSkeleton,
                                                 "Not Available");
  shairport_sync_advanced_remote_control_set_playback_status(
      shairportSyncAdvancedRemoteControlSkeleton, "Not Available");

  shairport_sync_advanced_remote_control_set_loop_status(shairportSyncAdvancedRemoteControlSkeleton,
                                                         "Not Available");

  shairport_sync_advanced_remote_control_set_shuffle(shairportSyncAdvancedRemoteControlSkeleton,
                                                     "Not Available");

  usleep(20000); // allow settings to be made before connecting the callbacks.

  // connect up the callbacks

  // g_signal_connect(shairportSyncSkeleton, "notify::interpolation",
  //                  G_CALLBACK(notify_interpolation_callback), NULL);
  // g_signal_connect(shairportSyncSkeleton, "notify::alacdecoder",
  //                  G_CALLBACK(notify_alacdecoder_callback), NULL);
  // g_signal_connect(shairportSyncSkeleton, "notify::disable-standby-mode",
  //                  G_CALLBACK(notify_disable_standby_mode_callback), NULL);
  // g_signal_connect(shairportSyncSkeleton, "notify::volume-control-profile",
  //                  G_CALLBACK(notify_volume_control_profile_callback), NULL);
  // g_signal_connect(shairportSyncSkeleton, "notify::disable-standby",
  //                 G_CALLBACK(notify_disable_standby_callback), NULL);
  // g_signal_connect(shairportSyncSkeleton, "notify::convolution-enabled",
  //                 G_CALLBACK(notify_convolution_enabled_callback), NULL);
  // g_signal_connect(shairportSyncSkeleton, "notify::convolution-gain",
  //                  G_CALLBACK(notify_convolution_gain_callback), NULL);
  // g_signal_connect(shairportSyncSkeleton, "notify::convolution-maximum-length-in-seconds",
  //                 G_CALLBACK(notify_convolution_maximum_length_in_seconds_callback), NULL);
  // g_signal_connect(shairportSyncSkeleton, "notify::convolution-impulse-response-files",
  //                  G_CALLBACK(notify_convolution_impulse_response_files_callback), NULL);
  // g_signal_connect(shairportSyncSkeleton, "notify::loudness-enabled",
  //                  G_CALLBACK(notify_loudness_enabled_callback), NULL);
  // g_signal_connect(shairportSyncSkeleton, "notify::loudness-threshold",
  //                  G_CALLBACK(notify_loudness_threshold_callback), NULL);
  // g_signal_connect(shairportSyncSkeleton, "notify::drift-tolerance",
  //                  G_CALLBACK(notify_drift_tolerance_callback), NULL);
  // g_signal_connect(shairportSyncSkeleton, "notify::volume", G_CALLBACK(notify_volume_callback),
  //                  NULL);

  g_signal_connect(shairportSyncSkeleton, "handle-quit", G_CALLBACK(on_handle_quit), NULL);

  g_signal_connect(shairportSyncSkeleton, "handle-remote-command",
                   G_CALLBACK(on_handle_remote_command), NULL);

  g_signal_connect(shairportSyncSkeleton, "handle-mule", G_CALLBACK(on_handle_mule), NULL);

  g_signal_connect(shairportSyncSkeleton, "handle-drop-session", G_CALLBACK(on_handle_drop_session),
                   NULL);

  g_signal_connect(shairportSyncSkeleton, "handle-set-frame-position-update-interval",
                   G_CALLBACK(on_handle_set_frame_position_update_interval), NULL);

  // g_signal_connect(shairportSyncClientSkeleton, "handle-set-shuffle",
  //                 G_CALLBACK(on_handle_set_shuffle), NULL);

  // g_signal_connect(shairportSyncClientSkeleton, "notify::loop-status",
  //                 G_CALLBACK(notify_loop_status_callback), NULL);

  // g_signal_connect(shairportSyncClientSkeleton, "notify::sample-property",
  //                  G_CALLBACK(notify_sample_property_callback), NULL);

  g_signal_connect(shairportSyncDiagnosticsSkeleton, "notify::verbosity",
                   G_CALLBACK(notify_verbosity_callback), NULL);

  g_signal_connect(shairportSyncDiagnosticsSkeleton, "notify::statistics",
                   G_CALLBACK(notify_statistics_callback), NULL);

  g_signal_connect(shairportSyncDiagnosticsSkeleton, "notify::elapsed-time",
                   G_CALLBACK(notify_elapsed_time_callback), NULL);

  g_signal_connect(shairportSyncDiagnosticsSkeleton, "notify::delta-time",
                   G_CALLBACK(notify_delta_time_callback), NULL);

  g_signal_connect(shairportSyncDiagnosticsSkeleton, "notify::file-and-line",
                   G_CALLBACK(notify_file_and_line_callback), NULL);

  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-fast-forward",
                   G_CALLBACK(on_handle_fast_forward), NULL);

  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-rewind",
                   G_CALLBACK(on_handle_rewind), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-toggle-mute",
                   G_CALLBACK(on_handle_toggle_mute), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-next", G_CALLBACK(on_handle_next),
                   NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-previous",
                   G_CALLBACK(on_handle_previous), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-pause", G_CALLBACK(on_handle_pause),
                   NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-play-pause",
                   G_CALLBACK(on_handle_play_pause), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-play", G_CALLBACK(on_handle_play),
                   NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-stop", G_CALLBACK(on_handle_stop),
                   NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-resume",
                   G_CALLBACK(on_handle_resume), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-shuffle-songs",
                   G_CALLBACK(on_handle_shuffle_songs), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-volume-up",
                   G_CALLBACK(on_handle_volume_up), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-volume-down",
                   G_CALLBACK(on_handle_volume_down), NULL);
  g_signal_connect(shairportSyncRemoteControlSkeleton, "handle-set-airplay-volume",
                   G_CALLBACK(on_handle_set_airplay_volume), NULL);

  g_signal_connect(shairportSyncAdvancedRemoteControlSkeleton, "handle-set-volume",
                   G_CALLBACK(on_handle_set_volume), NULL);

  add_metadata_watcher(dbus_metadata_watcher);

  // connect to the bus

  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(shairportSyncSkeleton), connection,
                                   "/org/gnome/ShairportSync", NULL);
  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(shairportSyncClientSkeleton),
                                   connection, "/org/gnome/ShairportSync", NULL);
  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(shairportSyncDiagnosticsSkeleton),
                                   connection, "/org/gnome/ShairportSync", NULL);
  g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(shairportSyncRemoteControlSkeleton),
                                   connection, "/org/gnome/ShairportSync", NULL);
  g_dbus_interface_skeleton_export(
      G_DBUS_INTERFACE_SKELETON(shairportSyncAdvancedRemoteControlSkeleton), connection,
      "/org/gnome/ShairportSync", NULL);

  debug(1, "Shairport Sync native D-Bus service started at \"%s\" on the %s bus.", name,
        (dbus_bus_type == G_BUS_TYPE_SESSION) ? "session" : "system");
  service_is_running = 1;
}

static void on_dbus_name_lost(__attribute__((unused)) GDBusConnection *connection,
                              __attribute__((unused)) const gchar *name,
                              __attribute__((unused)) gpointer user_data) {
  warn("could not acquire a Shairport Sync native D-Bus interface \"%s\" on the %s bus.", name,
       (dbus_bus_type == G_BUS_TYPE_SESSION) ? "session" : "system");
  ownerID = 0;
}

int start_dbus_service() {

  // set up default message bus
  if (config.dbus_default_message_bus == DBT_session)
    dbus_bus_type = G_BUS_TYPE_SESSION;

  // look for explicit overrides
  if (config.dbus_service_bus_type == DBT_system)
    dbus_bus_type = G_BUS_TYPE_SYSTEM;
  else if (config.dbus_service_bus_type == DBT_session)
    dbus_bus_type = G_BUS_TYPE_SESSION;

  debug(1,
        "Looking for a Shairport Sync native D-Bus interface \"org.gnome.ShairportSync\" on the %s "
        "bus.",
        (dbus_bus_type == G_BUS_TYPE_SESSION) ? "session" : "system");
  ownerID = g_bus_own_name(dbus_bus_type, "org.gnome.ShairportSync", G_BUS_NAME_OWNER_FLAGS_NONE,
                           NULL, on_dbus_name_acquired, on_dbus_name_lost, NULL, NULL);
  debug(2, "ownerID: %d.", ownerID);
  return 0; // this is just to quieten a compiler warning
}

void stop_dbus_service() {
  if (ownerID) {
    debug(2, "stopping dbus service -- unowning ownerID %d.", ownerID);
    g_bus_unown_name(ownerID);
  } else if (service_is_running != 0) {
    debug(1, "Zero OwnerID for running \"org.gnome.ShairportSync\" dbus service.");
  }
  service_is_running = 0;
}

int dbus_service_is_running() { return service_is_running; }
