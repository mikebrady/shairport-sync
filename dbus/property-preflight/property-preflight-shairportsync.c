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
 */

#include "property-preflight-shairportsync.h"
#include "common.h"
#include "remote/remote.h"
#ifdef CONFIG_CONVOLUTION
#include "FFTConvolver/convolver.h"
#include <sndfile.h>
#endif

/* ========================================================================
 * org.gnome.ShairportSync
 * ======================================================================== */

static gboolean property_preflight_shairport_sync_validate_property(const gchar *property_name,
                                                                    GVariant **value,
                                                                    GError **error) {
  gboolean result = TRUE;
  if (g_strcmp0(property_name, "DisableStandbyMode") == 0) {

    const gchar *requested_value = g_variant_get_string(*value, NULL);
    if (requested_value != NULL) {
      if ((strcmp(requested_value, "No") == 0) || (strcmp(requested_value, "Off") == 0) ||
          (strcmp(requested_value, "Never") == 0)) {
        config.disable_standby_mode = disable_standby_off;
        config.keep_dac_busy = 0;
      } else if ((strcmp(requested_value, "Yes") == 0) || (strcmp(requested_value, "On") == 0) ||
                 (strcmp(requested_value, "Always") == 0)) {
        config.disable_standby_mode = disable_standby_always;
        config.keep_dac_busy = 1;
      } else if (strcmp(requested_value, "Auto") == 0) {
        config.disable_standby_mode = disable_standby_auto;
      } else {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                    "\"%s\" is not valid for ShairportSync.DisableStandbyMode. It must be one of "
                    "the following: \"No\", \"Off\", \"Never\", \"Yes\", \"On\", \"Always\".",
                    requested_value);
        result = FALSE;
      }
    } else {
      g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                  "ShairportSync.DisableStandbyMode NULL request.");
      result = FALSE;
    }
  } else if (g_strcmp0(property_name, "LoudnessThreshold") == 0) {
    result =
        property_preflight_double_range(property_name, *value, -100.0, 0.0, "ShairportSync", error);
    if (result) {
      debug(1, ">> set loudness threshold to %f.", g_variant_get_double(*value));
      config.loudness_reference_volume_db = g_variant_get_double(*value);
    }
  } else if (g_strcmp0(property_name, "ConvolutionGain") == 0) {
#ifdef CONFIG_CONVOLUTION
    result =
        property_preflight_double_range(property_name, *value, -60.0, 18.0, "ShairportSync", error);
    if (result) {
      debug(1, ">> set convolution gain to %f.", g_variant_get_double(*value));
      config.convolution_gain = g_variant_get_double(*value);
    }
#else
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                "ShairportSync.ConvolutionGain failed. Convolution is not supported in this build "
                "of Shairport Sync");
    result = FALSE;
#endif
  } else if (g_strcmp0(property_name, "ConvolutionMaximumLengthInSeconds") == 0) {
#ifdef CONFIG_CONVOLUTION
    result =
        property_preflight_double_range(property_name, *value, 0.0, 15.0, "ShairportSync", error);
    if (result) {
      debug(1, ">> set convolution maximum length in seconds to %f.", g_variant_get_double(*value));
      config.convolution_max_length_in_seconds = g_variant_get_double(*value);
    }
#else
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                "ShairportSync.ConvolutionMaximumLengthInSeconds failed. Convolution is not "
                "supported in this build of Shairport Sync");
    result = FALSE;
#endif
  } else if (g_strcmp0(property_name, "ConvolutionImpulseResponseFiles") == 0) {
#ifdef CONFIG_CONVOLUTION
    const gchar *file_list = g_variant_get_string(*value, NULL);
    if (file_list != NULL) {

      unsigned int convolution_ir_file_count = 0;
      ir_file_info_t *convolution_ir_files = NULL; // NULL or an array of information about all the
                                                   // impulse response files loaded
      convolution_ir_files = parse_ir_filenames(file_list, &convolution_ir_file_count);

      int convolution_ir_files_status =
          sanity_check_ir_files(2, convolution_ir_files, convolution_ir_file_count);
      if (convolution_ir_files_status == 0) {
        // debug(1, ">> freeing current configuration impulse response filter files.");
        free_ir_filenames(config.convolution_ir_files, config.convolution_ir_file_count);
        config.convolution_ir_files = convolution_ir_files;
        config.convolution_ir_file_count = convolution_ir_file_count;
        config.convolution_ir_files_updated = 1;
        // debug(1, ">> setting %d configuration impulse response filter%s",
        //     config.convolution_ir_file_count, config.convolution_ir_file_count == 1 ? "" : "s");
      } else { // convolution_ir_files_status is the index of the errant file number in the array +
               // 1
        debug(1, "convolution impulse response file \"%s\" %s",
              convolution_ir_files[convolution_ir_files_status - 1].filename, sf_strerror(NULL));
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                    "ShairportSync.ConvolutionImpulseResponseFiles, \"%s\": %s",
                    convolution_ir_files[convolution_ir_files_status - 1].filename,
                    sf_strerror(NULL));
        result = FALSE;
      }
    }
#else
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                "ShairportSync.ConvolutionImpulseResponseFiles failed. Convolution is not "
                "supported in this build of Shairport Sync");
    result = FALSE;
#endif
  } else if (g_strcmp0(property_name, "ConvolutionEnabled") == 0) {
#ifdef CONFIG_CONVOLUTION
    const gboolean enabled = g_variant_get_boolean(*value);
    if (enabled) {
      debug(1, ">> activate convolution impulse response filter");
      config.convolution_enabled = 1;
    } else {
      debug(1, ">> deactivate convolution impulse response filter");
      config.convolution_enabled = 0;
      convolver_clear_state();
    }
#else
    g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                "ShairportSync.ConvolutionEnabled failed. Convolution is not supported in this "
                "build of Shairport Sync");
    result = FALSE;
#endif
  } else if (g_strcmp0(property_name, "DriftTolerance") == 0) {
    result =
        property_preflight_double_range(property_name, *value, 0.0, 2.0, "ShairportSync", error);
    if (result) {
      debug(1, ">> set drift tolerance to %g seconds.", g_variant_get_double(*value));
      config.tolerance = g_variant_get_double(*value);
    }
  } else if (g_strcmp0(property_name, "LoudnessThreshold") == 0) {
    result =
        property_preflight_double_range(property_name, *value, -100.0, 0.0, "ShairportSync", error);
    if (result) {
      debug(1, ">> set loudness threshold to %g dB.", g_variant_get_double(*value));
      config.loudness_reference_volume_db = g_variant_get_double(*value);
    }
  } else if (g_strcmp0(property_name, "LoudnessEnabled") == 0) {
    const gboolean enabled = g_variant_get_boolean(*value);
    if (enabled) {
      debug(1, ">> activate loudness filter");
      config.loudness_enabled = 1;
    } else {
      debug(1, ">> deactivate loudness filter");
      config.loudness_enabled = 0;
    }
  } else if (g_strcmp0(property_name, "Volume") == 0) {
    gdouble requested_value = g_variant_get_double(*value);
    result =
        ((requested_value == -144.0) || ((requested_value >= -30.0) && (requested_value <= 0.0)));
    if (result) {
      debug(1, ">> set (local only) airplay volume to %g.", g_variant_get_double(*value));
      config.airplay_volume = g_variant_get_double(*value);
    } else {
      g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                  "%g is not a valid value for ShairportSync.Volume --  it must be -144.0 (i.e. "
                  "mute) or within the range -30.0 to 0.0.",
                  requested_value);
      result = FALSE;
    }
  } else if (g_strcmp0(property_name, "ALACDecoder") == 0) {
    const gchar *requested_value = g_variant_get_string(*value, NULL);
    if (requested_value != NULL) {

#ifdef CONFIG_AIRPLAY_2
      if (strcmp(requested_value, "FFmpeg") != 0) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                    "The decoder \"%s\" is unrecognised or unavailable. "
                    "(The FFmpeg decoder is mandatory in AirPlay 2 editions of Shairport Sync.)",
                    requested_value);
        result = FALSE;
      }
      *value = NULL; // don't change the existing value, which is FFmpeg anyway.
#else
      if ((strcmp(requested_value, "Hammerton") == 0) &&
          ((config.decoders_supported & (1 << decoder_hammerton)) != 0))
        config.decoder_in_use = 1 << decoder_hammerton;
      else if ((strcmp(requested_value, "Apple") == 0) &&
               ((config.decoders_supported & (1 << decoder_apple_alac)) != 0))
        config.decoder_in_use = 1 << decoder_apple_alac;
      else if ((strcmp(requested_value, "FFmpeg") == 0) &&
               ((config.decoders_supported & (1 << decoder_ffmpeg_alac)) != 0))
        config.decoder_in_use = 1 << decoder_ffmpeg_alac;
      else {

        GString *list = g_string_new(NULL);
        gint i = 0;
        if ((config.decoders_supported & (1 << decoder_hammerton)) != 0) {
          g_string_append(list, "\"Hammerton\"");
          i++;
        }
        if ((config.decoders_supported & (1 << decoder_apple_alac)) != 0) {
          if (i != 0)
            g_string_append(list, ", ");
          g_string_append(list, "\"Apple\"");
          i++;
        }
        if ((config.decoders_supported & (1 << decoder_ffmpeg_alac)) != 0) {
          if (i != 0)
            g_string_append(list, ", ");
          g_string_append(list, "\"FFmpeg\"");
          i++;
        }
        if (i == 0)
          g_string_append(list, "none");
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                    "The decoder \"%s\" is unrecognised. Decoders supported: %s.", requested_value,
                    list->str);
        g_string_free(list, TRUE);
        result = FALSE;
      }

#endif
    }
  } else if (g_strcmp0(property_name, "Interpolation") == 0) {
    const gchar *requested_value = g_variant_get_string(*value, NULL);
    if (requested_value != NULL) {
      if (strcmp(requested_value, "Basic") == 0)
        config.packet_stuffing = ST_basic;
#ifdef CONFIG_SOXR
      else if (strcmp(requested_value, "Soxr") == 0)
        config.packet_stuffing = ST_soxr;
#endif
      else if (strcmp(requested_value, "Auto") == 0)
        config.packet_stuffing = ST_auto;
      else if (strcmp(requested_value, "Vernier") == 0)
        config.packet_stuffing = ST_vernier;
      else {
#ifdef CONFIG_SOXR
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                    "\"%s\" is not valid for ShairportSync.Interpolation --  it must be \"Auto\", "
                    "\"Basic\", \"Vernier\" or \"Soxr\".",
                    requested_value);
#else
        if (strcmp(requested_value, "Soxr") == 0) {
          g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                      "Soxr interpolation is not supported in this edition of Shairport Sync --  "
                      "ShairportSync.Interpolation must be \"Auto\", \"Basic\" or \"Vernier\".");
        } else {
          g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                      "\"%s\" is not valid for ShairportSync.Interpolation --  it must be "
                      "\"Auto\", \"Basic\" or \"Vernier\".",
                      requested_value);
        }
#endif
        result = FALSE;
      }
    }
  } else if (g_strcmp0(property_name, "VolumeControlProfile") == 0) {
    const gchar *requested_value = g_variant_get_string(*value, NULL);
    if (requested_value != NULL) {
      if (strcmp(requested_value, "Standard") == 0)
        config.volume_control_profile = VCP_standard;
      else if (strcmp(requested_value, "Flat") == 0)
        config.volume_control_profile = VCP_flat;
      else if (strcmp(requested_value, "DASL") == 0)
        config.volume_control_profile = VCP_dasl_tapered;
      else {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                    "\"%s\" is not valid for ShairportSync.VolumeControlProfile --  it must be "
                    "\"Standard\", \"Flat\" or \"DASL\".",
                    requested_value);
        result = FALSE;
      }
    }
  } else {
    debug(1, "Preflight ShairportSync.%s.", property_name);
  }

  /* Not a property we validate - let it through unchanged. */
  return result;
}

PROPERTY_PREFLIGHT_DEFINE_SKELETON(PropertyPreflightShairportSyncSkeleton,
                                   property_preflight_shairport_sync_skeleton,
                                   ShairportSyncSkeleton, TYPE_SHAIRPORT_SYNC_SKELETON,
                                   ShairportSync, SHAIRPORT_SYNC,
                                   property_preflight_shairport_sync_validate_property)

/* ========================================================================
 * org.gnome.ShairportSync.Client
 * ======================================================================== */

static gboolean
property_preflight_shairport_sync_client_validate_property(const gchar *property_name,
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

static gboolean property_preflight_shairport_sync_remote_control_validate_property(
    const gchar *property_name, __attribute((unused)) GVariant **value,
    __attribute((unused)) GError **error) {
  gboolean result = TRUE;

  if (g_strcmp0(property_name, "AirplayVolume") == 0) {
    gdouble requested_value = g_variant_get_double(*value);
    *value = NULL; // don't update the D-Bus value when finished
    if ((requested_value == -144.0) || ((requested_value >= -30.0) && (requested_value <= 0.0))) {
      debug(1, ">> set airplay volume to %g.", requested_value);
      if (remote_set_airplay_volume(requested_value) == 0) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                    "ShairportSync.RemoteControl.AirplayVolume is unable to set the volume "
                    "on the client to %g.",
                    requested_value);
        result = FALSE;
      }
    } else {
      g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                  "%g is not a valid value for ShairportSync.RemoteControl.AirplayVolume --  it "
                  "must be -144.0 (i.e. "
                  "mute) or within the range -30.0 to 0.0.",
                  requested_value);
      result = FALSE;
    }
  } else {
    debug(1, "Preflight ShairportSync.RemoteControl.%s.", property_name);
  }

  /* Not a property we validate - let it through unchanged. */
  return result;
}

PROPERTY_PREFLIGHT_DEFINE_SKELETON(
    PropertyPreflightShairportSyncRemoteControlSkeleton,
    property_preflight_shairport_sync_remote_control_skeleton, ShairportSyncRemoteControlSkeleton,
    TYPE_SHAIRPORT_SYNC_REMOTE_CONTROL_SKELETON, ShairportSyncRemoteControl,
    SHAIRPORT_SYNC_REMOTE_CONTROL,
    property_preflight_shairport_sync_remote_control_validate_property)

/* ========================================================================
 * org.gnome.ShairportSync.AdvancedRemoteControl
 * ======================================================================== */

static gboolean property_preflight_shairport_sync_advanced_remote_control_validate_property(
    const gchar *property_name, GVariant **value, GError **error) {

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
                    "\"%s\" is not valid for ShairportSync.AdvancedRemoteControl.LoopStatus. It "
                    "must be one of the following: \"Off\", \"One\", \"All\".",
                    requested_value);
        result = FALSE;
      }
      if ((result == TRUE) && (handled == 0)) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                    "ShairportSync.AdvancedRemoteControl.LoopStatus is unable to set Loop Status "
                    "on the client to \"%s\".",
                    requested_value);
        result = FALSE;
      }
    } else {
      g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                  "ShairportSync.AdvancedRemoteControl.LoopStatus NULL request.");
      result = FALSE;
    }
    *value = NULL; // don't update the LoopStatus value here -- let the remote device update it.
  } else if (g_strcmp0(property_name, "Shuffle") == 0) {
    // Send the Shuffle request to the remote device...
    int handled = 0;
    const gchar *requested_value = g_variant_get_string(*value, NULL);
    if (requested_value != NULL) {
      if (strcmp(requested_value, "Off") == 0) {
        handled = remote_set_shuffle_mode(SS_OFF);
      } else if (strcmp(requested_value, "On") == 0) {
        handled = remote_set_shuffle_mode(SS_ON);
      } else {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
                    "\"%s\" is not valid for ShairportSync.AdvancedRemoteControl.Shuffle. It "
                    "must be \"Off\" or \"On\".",
                    requested_value);
        result = FALSE;
      }
      if ((result == TRUE) && (handled == 0)) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                    "ShairportSync.AdvancedRemoteControl.Shuffle is unable to set Shuffle "
                    "on the client to \"%s\".",
                    requested_value);
        result = FALSE;
      }
    }
    *value = NULL; // don't update the Shuffle value here -- let the remote device update it.
  } else if (g_strcmp0(property_name, "Volume") == 0) {
    gint32 requested_value = g_variant_get_int32(*value);
    *value = NULL; // don't update the D-Bus value when finished
    if ((requested_value >= 0) && (requested_value <= 100)) {
      debug(1, ">> set airplay volume to %d.", requested_value);
      if (remote_set_integer_percent_volume(requested_value) == 0) {
        g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_FAILED,
                    "ShairportSync.AdvancedRemoteControl.Volume is unable to set the volume "
                    "on the client to %d%%.",
                    requested_value);
        result = FALSE;
      }
    } else {
      g_set_error(
          error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
          "%d is not a valid value for ShairportSync.AdvncedRemoteControl.Volume --  it must be "
          "within the range 0 to 100.",
          requested_value);
      result = FALSE;
    }
  } else {
    debug(1, "Preflight ShairportSync.AdvancedRemoteControl.%s.", property_name);
  }
  /* Not a property we validate or use here - let it through unchanged. */
  return result;
}

PROPERTY_PREFLIGHT_DEFINE_SKELETON(
    PropertyPreflightShairportSyncAdvancedRemoteControlSkeleton,
    property_preflight_shairport_sync_advanced_remote_control_skeleton,
    ShairportSyncAdvancedRemoteControlSkeleton,
    TYPE_SHAIRPORT_SYNC_ADVANCED_REMOTE_CONTROL_SKELETON, ShairportSyncAdvancedRemoteControl,
    SHAIRPORT_SYNC_ADVANCED_REMOTE_CONTROL,
    property_preflight_shairport_sync_advanced_remote_control_validate_property)
