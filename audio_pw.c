/*
 * Asynchronous PipeWire Backend. This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2024--2025
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

// This uses ideas from the tone generator sample code at:
// https://github.com/PipeWire/pipewire/blob/master/src/examples/audio-src.c
// Thanks to Wim Taymans.

#include "audio.h"
#include "common.h"
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <strings.h> // for strcasecmp
#include <unistd.h>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/props.h>
#include <pipewire/extensions/metadata.h>
#include <pipewire/device.h>
#include <spa/param/route.h>
#include <spa/utils/json.h>

// forward declaration -- the audio_pw struct itself is defined at the end of this file, but
// init() needs to set some of its function pointers depending on the configuration.
extern audio_output audio_pw;

// forward declarations -- these are defined further down in this file (near the end), but
// init() needs to take their addresses earlier on.
static void volume(double vol);
static int mute(int mute_state_requested);
static output_parameters_t *parameters(void);

static char channel_map_mono[] = "FC";
static char channel_map_stereo[] = "FL FR";
static char channel_map_2p1[] = "FL FR LFE";
static char channel_map_4p0[] = "FL FR FC BC";
static char channel_map_5p0[] = "FL FR FC BL BR";
static char channel_map_5p1[] = "FL FR FC LFE BL BR";
static char channel_map_6p1[] = "FL FR FC LFE BC SL SR";
static char channel_map_7p1[] = "FL FR FC LFE BL BR SL SR";

typedef struct {
  enum spa_audio_format spa_format;
  sps_format_t sps_format;
  unsigned int bytes_per_sample;
} spa_sps_t;

// these are the only formats that audio_pw will ever allow itself to be configured with
static spa_sps_t format_lookup[] = {{SPA_AUDIO_FORMAT_S16_LE, SPS_FORMAT_S16_LE, 2},
                                    {SPA_AUDIO_FORMAT_S16_BE, SPS_FORMAT_S16_BE, 2},
                                    {SPA_AUDIO_FORMAT_S32_LE, SPS_FORMAT_S32_LE, 4},
                                    {SPA_AUDIO_FORMAT_S32_BE, SPS_FORMAT_S32_BE, 4}};

#define BUFFER_SIZE_IN_SECONDS 1

static uint8_t buffer[1024];
static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;

static int32_t current_encoded_output_format = 0;
static char *audio_lmb, *audio_umb, *audio_toq, *audio_eoq;
static size_t audio_size = 0;
static size_t audio_occupancy;
static int enable_fill;
static int stream_is_active;
static int on_process_is_running = 0;

struct data {
  struct pw_thread_loop *loop;
  struct pw_stream *stream;
  unsigned int rate;
  unsigned int bytes_per_sample;
  unsigned int channels;
};

// the pipewire global data structure
struct data data = {NULL, NULL, 0, 0, 0};

// Hardware volume curve advertised to player.c: 0 dB down to -96.30 dB, in centi-dB (matching
// Shairport Sync's own software-mixer floor). player.c maps AirPlay volume into this range and
// passes the result to volume() below, which routes it to either our own stream or the default
// sink -- see volume().
static volume_range_t volume_range = {.minimum_volume_dB = -9630, .maximum_volume_dB = 0};
static output_parameters_t output_parameters = {.volume_range = &volume_range};

// Set by "pipewire.mixer_type" (see init()): "sink" or "stream". Absent/empty means
// pw_volume_control_enabled stays 0 and Shairport Sync's own software volume control applies,
// as always (mirrors alsa.mixer_control_name). PW_VOLUME_TARGET_SINK ("sink") drives the
// system's default sink over PipeWire's native protocol; PW_VOLUME_TARGET_STREAM ("stream")
// drives our own PipeWire stream instead, leaving the sink untouched.
typedef enum { PW_VOLUME_TARGET_SINK = 0, PW_VOLUME_TARGET_STREAM = 1 } pw_volume_target_t;
static int pw_volume_control_enabled = 0;
static pw_volume_target_t pw_mixer_control_target = PW_VOLUME_TARGET_SINK;

// Last attenuation/mute volume_stream()/mute_stream() applied, cached for reapply once the
// stream (re)connects -- see reapply_last_requested_stream_volume_and_mute(). Units match what
// player.c passes to volume() (centi-dB). Written from player.c's RTSP/DACP thread, read back
// from data.loop's thread with no shared lock: both fields are word-sized, so a race yields at
// worst a one-update-stale value, never a torn one, and is inaudible given how rarely these
// change relative to audio callbacks.
static double pw_last_requested_vol_centidb = 0.0;
static int pw_last_requested_mute = 0;

// Set our stream's channel volumes/mute directly. Both fail (logged by callers) if the stream
// hasn't connected yet -- normal between process start and the first configure() call. Caller
// must hold data.loop's lock.
static int apply_stream_channel_volumes(float gain) {
  if (data.stream == NULL)
    return -ENODEV;
  if (gain > 1.0f)
    gain = 1.0f;
  if (gain < 0.0f)
    gain = 0.0f;
  unsigned int channels = data.channels;
  if ((channels == 0) || (channels > 8))
    channels = 2;
  float channel_volumes[8];
  for (unsigned int i = 0; i < channels; i++)
    channel_volumes[i] = gain;
  return pw_stream_set_control(data.stream, SPA_PROP_channelVolumes, channels, channel_volumes,
                               0);
}

static int apply_stream_mute(int muted) {
  if (data.stream == NULL)
    return -ENODEV;
  float mute_value = muted ? 1.0f : 0.0f;
  return pw_stream_set_control(data.stream, SPA_PROP_mute, 1, &mute_value, 0);
}

// Re-pin our stream to unity/unmuted. WirePlumber's "restore stream volume" can otherwise apply
// a remembered per-app volume to a fresh stream. Needed whenever the stream isn't the active
// volume surface -- control disabled, or enabled but targeting the sink -- so attenuation isn't
// applied twice. Caller must hold data.loop's lock.
static void reset_stream_to_unity(void) {
  apply_stream_channel_volumes(1.0f);
  apply_stream_mute(0);
}

// When targeting the stream (PW_VOLUME_TARGET_STREAM), re-apply the last volume_stream()/
// mute_stream() request. Needed because AirPlay's initial volume typically arrives via RTSP
// before the first configure()/pw_stream_connect(), and pw_stream_set_control() fails until
// the stream is connected -- without this, that initial value would be silently dropped.
// Caller must hold data.loop's lock.
static void reapply_last_requested_stream_volume_and_mute(void) {
  float gain = (float)pow(10.0, pw_last_requested_vol_centidb / 2000.0);
  apply_stream_channel_volumes(gain);
  apply_stream_mute(pw_last_requested_mute);
}

// ======================================================================
// Native PipeWire "sink" volume control.
//
// Ported from MPD's PwSinkMixerPlugin.cxx. Rather than shelling out to
// "wpctl set-volume"/"set-mute" (a subprocess per change, no read-back, and
// a dependency on wpctl), this opens a second, independent PipeWire client
// connection -- separate from data.core/data.stream, which belong to the
// realtime audio thread -- and drives the target sink over the native
// protocol:
//
//   1. Connect and fetch the registry.
//   2. If "pipewire.sink_target" is set, use that node.name directly,
//      skipping step 2a -- this always controls the sink actually playing
//      the audio, never an unrelated one, since the main stream targets
//      the same node (see PW_KEY_TARGET_OBJECT in init()).
//   2a. Otherwise, bind the "default" pw_metadata object and read its
//      "default.audio.sink" property (a {"name":"<node.name>"} JSON blob --
//      the same object WirePlumber updates and "@DEFAULT_AUDIO_SINK@"
//      resolves against).
//   3. Match that name against Audio/Sink nodes in the registry, bind the
//      node, and enumerate its SPA_PARAM_Props (channelVolumes).
//   4. Also bind the Node's owning Device ("device.id") and enumerate
//      SPA_PARAM_Route: for a hardware-routed sink, the Device's active
//      Route -- not the Node's own Props -- drives the audible/ALSA mixer,
//      so pw_sink_set_volume() writes to both. Sinks with no device.id
//      (virtual/software sinks) have no Route; Node Props is their only
//      volume path.
//
// wpctl/pavucontrol display volume on a "cubic" scale (linear amplitude =
// V^3); this matters only when converting player.c's centi-dB value to
// something comparable -- see volume_sink() for the derivation.
//
// Limitations, matching the MPD plugin:
//  - Only one matching-name Audio/Sink node is tracked; ties go to whichever
//    is seen first.
//  - No re-resolution if the default sink changes at runtime; restart to
//    pick up a new default.
//  - A PipeWire core error unblocks any in-progress round trip immediately
//    rather than waiting out pw_sink_wait_until_ready()'s retry budget --
//    see pw_sink_on_core_error().
// ======================================================================

#define PW_SINK_MAX_CHANNELS 8
#define PW_SINK_MAX_KNOWN_SINKS 32
#define PW_SINK_READY_RETRIES 20

typedef struct {
  uint32_t global_id;
  char name[256];
  uint32_t device_id; // SPA_ID_INVALID if this sink has none (virtual sink)
} pw_sink_known_sink_t;

// All of this mirrors data/data.loop/data.core/etc above, but is a second,
// independent connection: it must not be touched from on_process() or any
// other callback tied to data.loop, and vice versa.
static struct {
  struct pw_thread_loop *loop;
  struct pw_context *context;
  struct pw_core *core;
  struct spa_hook core_listener;
  int last_sync_seq;

  // set by pw_sink_on_core_error(), checked by pw_sink_roundtrip() /
  // pw_sink_wait_until_ready() so a core error unblocks a thread waiting in
  // pw_thread_loop_wait() immediately, instead of spinning silently until
  // the retry budget in pw_sink_wait_until_ready() runs out. Holds the
  // PipeWire error code (negative errno), or 0 if no error has occurred.
  int core_error;

  struct pw_registry *registry;
  struct spa_hook registry_listener;

  struct pw_proxy *metadata_proxy;
  struct spa_hook metadata_listener;

  struct pw_proxy *sink_proxy;
  struct spa_hook sink_node_listener;
  uint32_t sink_global_id;

  // the Device owning the resolved sink; for hardware-routed sinks this --
  // not the Node's own Props -- is what actually drives the audible/ALSA
  // mixer control; see route_* fields below.
  struct pw_proxy *device_proxy;
  struct spa_hook device_listener;
  uint32_t device_global_id;

  // the currently active output Route on that Device, as last reported by
  // the server. index/device/direction must be echoed back unchanged in our
  // own SPA_PARAM_Route write -- only the embedded Props (channelVolumes)
  // actually changes.
  //
  // NOTE: as in the MPD plugin this is ported from, for a device with more
  // than one selectable output route (e.g. built-in speaker + headphone
  // jack) simply taking the first Output-direction route seen is not fully
  // correct -- it should be matched against whichever route is actually
  // selected. Simple single-route DACs (the common case for USB/HAT audio
  // interfaces) are unaffected.
  int32_t route_index;
  int32_t route_device;
  uint32_t route_direction;
  int have_route; // boolean

  // false if the resolved sink has no device.id at all (e.g. a
  // virtual/software-only sink) -- in that case there is no Route to wait
  // for, and Node-level Props is the only volume path.
  int has_hw_route; // boolean

  pw_sink_known_sink_t known_sinks[PW_SINK_MAX_KNOWN_SINKS];
  unsigned int n_known_sinks;

  // node.name we're trying to bind, resolved from metadata (or from
  // config.pw_sink_target directly, see pw_sink_maybe_bind_node())
  char target_name[256];

  float current_volumes[PW_SINK_MAX_CHANNELS];
  uint32_t n_current_volumes;
  int have_current_volume; // boolean

  // volumes as read back from the Device's active Route, i.e. the ones that
  // actually reflect the real hardware/ALSA state.
  float route_volumes[PW_SINK_MAX_CHANNELS];
  uint32_t n_route_volumes;

  int connected; // boolean; true once pw_sink_connect() has completed successfully
} pw_sink;

static void pw_sink_maybe_bind_node(void);

static void pw_sink_on_core_done(__attribute__((unused)) void *userdata, uint32_t id, int seq) {
  if (id == PW_ID_CORE) {
    pw_sink.last_sync_seq = seq;
    pw_thread_loop_signal(pw_sink.loop, false);
  }
}

static void pw_sink_on_core_error(__attribute__((unused)) void *userdata, uint32_t id, int seq,
                                  int res, const char *message) {
  warn("pw: sink: PipeWire core error: id=%u seq=%d res=%d (%s): %s.", id, seq, res,
       strerror(-res), message ? message : "");
  // Record the fault and wake up anyone blocked in pw_thread_loop_wait() inside
  // pw_sink_roundtrip(): without this, a core error arriving mid-round-trip would
  // leave the caller waiting until pw_sink_wait_until_ready()'s fixed retry budget
  // silently expired, rather than failing promptly.
  pw_sink.core_error = (res != 0) ? res : -EIO;
  pw_thread_loop_signal(pw_sink.loop, false);
}

// Extract the "name" string field from a {"name":"...", ...} JSON blob as
// published in the "default.audio.sink"/"default.configured.audio.sink"
// metadata properties. Writes into out (size out_size), leaving it empty if
// not found or malformed.
static void pw_sink_parse_default_node_name(const char *json, char *out, size_t out_size) {
  out[0] = '\0';
  if (json == NULL)
    return;

  struct spa_json outer;
  spa_json_init(&outer, json, strlen(json));

  struct spa_json obj;
  if (spa_json_enter_object(&outer, &obj) <= 0)
    return;

  char key[256];
  while (spa_json_get_string(&obj, key, sizeof(key)) > 0) {
    if (strcmp(key, "name") == 0) {
      char value[256];
      if (spa_json_get_string(&obj, value, sizeof(value)) > 0)
        snprintf(out, out_size, "%s", value);
      return;
    }
    // skip whatever value belongs to this key -- we don't care about anything but "name"
    if (spa_json_next(&obj, NULL) <= 0)
      break;
  }
}

static int pw_sink_on_metadata_property(__attribute__((unused)) void *userdata, uint32_t subject,
                                        const char *key, __attribute__((unused)) const char *type,
                                        const char *value) {
  if (subject != PW_ID_CORE || key == NULL)
    return 0;

  // prefer the effective default; fall back to the user-configured one if that's all we have
  if (strcmp(key, "default.audio.sink") == 0 ||
      ((pw_sink.target_name[0] == '\0') && strcmp(key, "default.configured.audio.sink") == 0)) {
    char name[256];
    pw_sink_parse_default_node_name(value, name, sizeof(name));
    if (name[0] != '\0') {
      snprintf(pw_sink.target_name, sizeof(pw_sink.target_name), "%s", name);
      debug(2, "pw: sink: resolved default sink target_name=\"%s\".", name);
      pw_sink_maybe_bind_node();
    }
  }

  return 0;
}

static const struct pw_metadata_events pw_sink_metadata_events = {
    PW_VERSION_METADATA_EVENTS,
    .property = pw_sink_on_metadata_property,
};

static void pw_sink_on_sink_node_param(__attribute__((unused)) void *userdata,
                                       __attribute__((unused)) int seq, uint32_t id,
                                       __attribute__((unused)) uint32_t index,
                                       __attribute__((unused)) uint32_t next,
                                       const struct spa_pod *param) {
  if (param == NULL || !spa_pod_is_object(param))
    return;

  const struct spa_pod_object *obj = (const struct spa_pod_object *)param;
  const struct spa_pod_prop *prop;

  SPA_POD_OBJECT_FOREACH(obj, prop) {
    if (prop->key != SPA_PROP_channelVolumes)
      continue;

    uint32_t n_volumes = 0;
    const void *raw = spa_pod_get_array(&prop->value, &n_volumes);
    if (raw == NULL || n_volumes == 0)
      return;

    if (n_volumes > PW_SINK_MAX_CHANNELS)
      n_volumes = PW_SINK_MAX_CHANNELS;
    memcpy(pw_sink.current_volumes, raw, n_volumes * sizeof(float));
    pw_sink.n_current_volumes = n_volumes;
    pw_sink.have_current_volume = 1;
    debug(2, "pw: sink: read back volume for sink_global_id=%u n_volumes=%u volumes[0]=%f.", id,
          n_volumes, pw_sink.current_volumes[0]);
    return;
  }
}

static const struct pw_node_events pw_sink_node_events = {
    PW_VERSION_NODE_EVENTS,
    .param = pw_sink_on_sink_node_param,
};

static void pw_sink_on_device_param(__attribute__((unused)) void *userdata,
                                    __attribute__((unused)) int seq,
                                    __attribute__((unused)) uint32_t id,
                                    __attribute__((unused)) uint32_t index,
                                    __attribute__((unused)) uint32_t next,
                                    const struct spa_pod *param) {
  if (param == NULL || !spa_pod_is_object(param))
    return;

  const struct spa_pod_object *obj = (const struct spa_pod_object *)param;
  if (obj->body.id != SPA_PARAM_Route)
    return;

  const struct spa_pod_prop *prop;
  int32_t found_index = -1, found_device = -1;
  uint32_t found_direction = 0;
  int have_index = 0, have_direction = 0;
  float volumes[PW_SINK_MAX_CHANNELS];
  uint32_t n_volumes = 0;
  int have_volumes = 0;

  SPA_POD_OBJECT_FOREACH(obj, prop) {
    switch (prop->key) {
    case SPA_PARAM_ROUTE_index:
      if (spa_pod_get_int(&prop->value, &found_index) >= 0)
        have_index = 1;
      break;
    case SPA_PARAM_ROUTE_device:
      spa_pod_get_int(&prop->value, &found_device);
      break;
    case SPA_PARAM_ROUTE_direction:
      if (spa_pod_get_id(&prop->value, &found_direction) >= 0)
        have_direction = 1;
      break;
    case SPA_PARAM_ROUTE_props: {
      if (!spa_pod_is_object(&prop->value))
        break;
      const struct spa_pod_object *props_obj = (const struct spa_pod_object *)&prop->value;
      const struct spa_pod_prop *pp;
      SPA_POD_OBJECT_FOREACH(props_obj, pp) {
        if (pp->key != SPA_PROP_channelVolumes)
          continue;
        uint32_t n = 0;
        const void *raw = spa_pod_get_array(&pp->value, &n);
        if (raw != NULL && n > 0) {
          if (n > PW_SINK_MAX_CHANNELS)
            n = PW_SINK_MAX_CHANNELS;
          memcpy(volumes, raw, n * sizeof(float));
          n_volumes = n;
          have_volumes = 1;
        }
      }
      break;
    }
    default:
      break;
    }
  }

  // only take the first Output-direction route we see -- see the caveat on
  // route_index et al. above for devices with more than one selectable route
  if (pw_sink.have_route || !have_index || !have_direction ||
      found_direction != SPA_DIRECTION_OUTPUT)
    return;

  pw_sink.route_index = found_index;
  pw_sink.route_device = found_device;
  pw_sink.route_direction = found_direction;
  pw_sink.have_route = 1;

  if (have_volumes) {
    memcpy(pw_sink.route_volumes, volumes, n_volumes * sizeof(float));
    pw_sink.n_route_volumes = n_volumes;
  }

  debug(2, "pw: sink: resolved route index=%d device=%d direction=%u volumes[0]=%f.",
        pw_sink.route_index, pw_sink.route_device, pw_sink.route_direction,
        have_volumes ? pw_sink.route_volumes[0] : -1.0f);
}

static const struct pw_device_events pw_sink_device_events = {
    PW_VERSION_DEVICE_EVENTS,
    .param = pw_sink_on_device_param,
};

static void pw_sink_maybe_bind_node(void) {
  if (pw_sink.sink_proxy != NULL)
    return;

  const char *wanted =
      (config.pw_sink_target != NULL) ? config.pw_sink_target : pw_sink.target_name;
  if (wanted[0] == '\0')
    return;

  for (unsigned int i = 0; i < pw_sink.n_known_sinks; i++) {
    pw_sink_known_sink_t *info = &pw_sink.known_sinks[i];
    if (strcmp(info->name, wanted) != 0)
      continue;

    debug(2, "pw: sink: binding sink id=%u name=\"%s\" device.id=%u.", info->global_id,
          info->name, info->device_id);

    pw_sink.sink_global_id = info->global_id;
    pw_sink.sink_proxy = pw_registry_bind(pw_sink.registry, info->global_id,
                                          PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, 0);
    pw_node_add_listener((struct pw_node *)pw_sink.sink_proxy, &pw_sink.sink_node_listener,
                         &pw_sink_node_events, NULL);
    pw_node_enum_params((struct pw_node *)pw_sink.sink_proxy, 0, SPA_PARAM_Props, 0, UINT32_MAX,
                        NULL);

    // The Node's own Props (above) reflect real-but-inert software state for a
    // hardware-routed sink; the object that actually drives the audible/ALSA
    // mixer-control volume is the parent Device's active Route, so bind that too.
    if (info->device_id != SPA_ID_INVALID) {
      pw_sink.device_global_id = info->device_id;
      pw_sink.device_proxy = pw_registry_bind(pw_sink.registry, info->device_id,
                                              PW_TYPE_INTERFACE_Device, PW_VERSION_DEVICE, 0);
      pw_device_add_listener((struct pw_device *)pw_sink.device_proxy, &pw_sink.device_listener,
                             &pw_sink_device_events, NULL);
      pw_device_enum_params((struct pw_device *)pw_sink.device_proxy, 0, SPA_PARAM_Route, 0,
                            UINT32_MAX, NULL);
    } else {
      pw_sink.has_hw_route = 0;
      debug(2, "pw: sink: sink node id=%u has no device.id -- no hardware Route "
               "available, only software Node volume will be used.",
            info->global_id);
    }
    return;
  }
}

static void pw_sink_on_global(__attribute__((unused)) void *userdata, uint32_t id,
                              __attribute__((unused)) uint32_t permissions, const char *type,
                              __attribute__((unused)) uint32_t version,
                              const struct spa_dict *props) {
  if (props == NULL)
    return;

  if (strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
    const char *name = spa_dict_lookup(props, PW_KEY_METADATA_NAME);
    if (name != NULL && strcmp(name, "default") == 0 && pw_sink.metadata_proxy == NULL) {
      pw_sink.metadata_proxy =
          pw_registry_bind(pw_sink.registry, id, type, PW_VERSION_METADATA, 0);
      pw_metadata_add_listener((struct pw_metadata *)pw_sink.metadata_proxy,
                               &pw_sink.metadata_listener, &pw_sink_metadata_events, NULL);
    }
  } else if (strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
    const char *media_class = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    const char *node_name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    if (media_class != NULL && node_name != NULL && strcmp(media_class, "Audio/Sink") == 0) {
      const char *device_id_str = spa_dict_lookup(props, PW_KEY_DEVICE_ID);
      uint32_t device_id = SPA_ID_INVALID;
      if (device_id_str != NULL)
        device_id = (uint32_t)strtoul(device_id_str, NULL, 10);

      if (pw_sink.n_known_sinks < PW_SINK_MAX_KNOWN_SINKS) {
        pw_sink_known_sink_t *info = &pw_sink.known_sinks[pw_sink.n_known_sinks++];
        info->global_id = id;
        snprintf(info->name, sizeof(info->name), "%s", node_name);
        info->device_id = device_id;
        debug(2, "pw: sink: saw sink node id=%u name=\"%s\" device.id=%u.", id, node_name,
              device_id);
        pw_sink_maybe_bind_node();
      } else {
        debug(1, "pw: sink: too many Audio/Sink nodes seen (limit %d); ignoring id=%u "
                 "name=\"%s\".",
              PW_SINK_MAX_KNOWN_SINKS, id, node_name);
      }
    }
  }
}

static void pw_sink_on_global_remove(__attribute__((unused)) void *userdata, uint32_t id) {
  for (unsigned int i = 0; i < pw_sink.n_known_sinks; i++) {
    if (pw_sink.known_sinks[i].global_id == id) {
      pw_sink.known_sinks[i] = pw_sink.known_sinks[pw_sink.n_known_sinks - 1];
      pw_sink.n_known_sinks--;
      break;
    }
  }

  if (id == pw_sink.sink_global_id) {
    // the sink we were bound to disappeared (unplugged, etc.); drop it,
    // pw_sink_set_volume()/pw_sink_set_mute() will no-op (logged at debug level)
    // until the process is restarted, matching the equivalent MPD plugin's
    // documented limitation.
    if (pw_sink.sink_proxy != NULL) {
      pw_proxy_destroy(pw_sink.sink_proxy);
      pw_sink.sink_proxy = NULL;
    }
    pw_sink.sink_global_id = SPA_ID_INVALID;
    pw_sink.have_current_volume = 0;
  }
}

static const struct pw_registry_events pw_sink_registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = pw_sink_on_global,
    .global_remove = pw_sink_on_global_remove,
};

static const struct pw_core_events pw_sink_core_events = {
    PW_VERSION_CORE_EVENTS,
    .done = pw_sink_on_core_done,
    .error = pw_sink_on_core_error,
};

// Block (caller's thread, NOT from inside a PipeWire callback) until all requests
// issued so far have been processed by the server, or until the core reports an
// error. Caller must hold pw_sink.loop's lock. Returns 0 on success, or the
// negative PipeWire error code recorded by pw_sink_on_core_error().
static int pw_sink_roundtrip(void) {
  int seq = pw_core_sync(pw_sink.core, PW_ID_CORE, 0);
  while (pw_sink.last_sync_seq != seq) {
    if (pw_sink.core_error != 0)
      return pw_sink.core_error;
    pw_thread_loop_wait(pw_sink.loop);
  }
  return pw_sink.core_error;
}

static int pw_sink_is_ready(void) {
  if (pw_sink.sink_proxy == NULL || !pw_sink.have_current_volume)
    return 0;
  if (!pw_sink.has_hw_route)
    return 1;
  return (pw_sink.device_proxy != NULL) && pw_sink.have_route;
}

// Repeatedly pw_sink_roundtrip() until either the sink node is bound and its
// initial volume has arrived, or we give up. Caller must hold pw_sink.loop's lock.
// Returns 0 if ready, or a non-zero status: the negative PipeWire error code if
// the core reported one, or -ETIMEDOUT if the retry budget was exhausted first.
static int pw_sink_wait_until_ready(void) {
  for (int i = 0; i < PW_SINK_READY_RETRIES && !pw_sink_is_ready(); i++) {
    int rc = pw_sink_roundtrip();
    if (rc != 0)
      return rc;
  }
  return pw_sink_is_ready() ? 0 : -ETIMEDOUT;
}

// Open the second, independent PipeWire connection used for driving the default
// sink's volume, and resolve+bind the target sink node (and its owning device, if
// it has one). Called once from init() if pipewire.mixer_type is "sink"; torn
// down by pw_sink_disconnect() from deinit(). Returns 0 on success, -1 on failure
// (with a warn() already issued).
static int pw_sink_connect(void) {
  memset(&pw_sink, 0, sizeof(pw_sink));
  pw_sink.sink_global_id = SPA_ID_INVALID;
  pw_sink.device_global_id = SPA_ID_INVALID;
  pw_sink.has_hw_route = 1;
  pw_sink.last_sync_seq = -1;

  errno = 0;
  pw_sink.loop = pw_thread_loop_new("shairport-sync-sink", NULL);
  if (pw_sink.loop == NULL) {
    warn("pw: sink: pw_thread_loop_new() failed (errno %d: %s).", errno, strerror(errno));
    return -1;
  }

  pw_thread_loop_lock(pw_sink.loop);

  pw_sink.context =
      pw_context_new(pw_thread_loop_get_loop(pw_sink.loop),
                     pw_properties_new(PW_KEY_MEDIA_CATEGORY, "Manager", PW_KEY_APP_NAME,
                                       "shairport-sync-sink", NULL),
                     0);
  if (pw_sink.context == NULL) {
    warn("pw: sink: pw_context_new() failed.");
    goto fail_locked;
  }

  if (pw_thread_loop_start(pw_sink.loop) < 0) {
    warn("pw: sink: pw_thread_loop_start() failed.");
    goto fail_locked;
  }

  pw_sink.core = pw_context_connect(pw_sink.context, NULL, 0);
  if (pw_sink.core == NULL) {
    warn("pw: sink: pw_context_connect() failed (is PipeWire running?).");
    goto fail_locked;
  }

  pw_core_add_listener(pw_sink.core, &pw_sink.core_listener, &pw_sink_core_events, NULL);

  pw_sink.registry = pw_core_get_registry(pw_sink.core, PW_VERSION_REGISTRY, 0);
  if (pw_sink.registry == NULL) {
    warn("pw: sink: pw_core_get_registry() failed.");
    goto fail_locked;
  }

  pw_registry_add_listener(pw_sink.registry, &pw_sink.registry_listener,
                           &pw_sink_registry_events, NULL);

  if (config.pw_sink_target != NULL)
    // skip metadata resolution entirely; pw_sink_maybe_bind_node() will match
    // directly against config.pw_sink_target as registry globals arrive
    pw_sink_maybe_bind_node();

  int rc = pw_sink_wait_until_ready();
  if (rc != 0) {
    if (rc == -ETIMEDOUT)
      warn("pw: sink: timed out resolving PipeWire %s sink volume.",
           config.pw_sink_target != NULL ? config.pw_sink_target : "default");
    else
      warn("pw: sink: PipeWire core error while resolving %s sink volume: %s.",
           config.pw_sink_target != NULL ? config.pw_sink_target : "default", strerror(-rc));
    goto fail_locked;
  }

  pw_thread_loop_unlock(pw_sink.loop);
  pw_sink.connected = 1;
  return 0;

fail_locked:
  pw_thread_loop_unlock(pw_sink.loop);
  // fall through to full teardown of whatever got created before the failure
  {
    struct pw_thread_loop *loop_to_stop = pw_sink.loop;
    if (pw_sink.core != NULL)
      pw_core_disconnect(pw_sink.core);
    if (loop_to_stop != NULL)
      pw_thread_loop_stop(loop_to_stop);
    if (pw_sink.context != NULL)
      pw_context_destroy(pw_sink.context);
    if (loop_to_stop != NULL)
      pw_thread_loop_destroy(loop_to_stop);
  }
  memset(&pw_sink, 0, sizeof(pw_sink));
  return -1;
}

static void pw_sink_disconnect(void) {
  if (pw_sink.loop == NULL)
    return;

  pw_thread_loop_lock(pw_sink.loop);
  if (pw_sink.sink_proxy != NULL) {
    pw_proxy_destroy(pw_sink.sink_proxy);
    pw_sink.sink_proxy = NULL;
  }
  if (pw_sink.device_proxy != NULL) {
    pw_proxy_destroy(pw_sink.device_proxy);
    pw_sink.device_proxy = NULL;
  }
  if (pw_sink.metadata_proxy != NULL) {
    pw_proxy_destroy(pw_sink.metadata_proxy);
    pw_sink.metadata_proxy = NULL;
  }
  if (pw_sink.registry != NULL) {
    pw_proxy_destroy((struct pw_proxy *)pw_sink.registry);
    pw_sink.registry = NULL;
  }
  if (pw_sink.core != NULL) {
    pw_core_disconnect(pw_sink.core);
    pw_sink.core = NULL;
  }
  pw_thread_loop_unlock(pw_sink.loop);

  pw_thread_loop_stop(pw_sink.loop);

  if (pw_sink.context != NULL)
    pw_context_destroy(pw_sink.context);

  pw_thread_loop_destroy(pw_sink.loop);

  memset(&pw_sink, 0, sizeof(pw_sink));
}

// Write pw_sink.current_volumes into the Node's own SPA_PARAM_Props. Caller must
// hold pw_sink.loop's lock. Always attempted (in addition to pw_sink_apply_route_
// volume() below) since it's the only volume path for sinks with no hardware
// Route at all.
//
// Deliberately does NOT round-trip / wait for a server ack: pw_node_set_param()
// itself is a fire-and-forget send over the connection, and volume() (which
// eventually calls this via pw_sink_set_volume()) is called synchronously from
// shairport-sync's RTSP conversation thread -- the same thread responsible for
// timely DACP request/response traffic with the AirPlay source. Blocking here
// for a full round trip to the PipeWire server on every volume change risks
// stalling that thread long enough for the DACP traffic to time out and the
// source to consider the connection unresponsive. The eventual server-side
// result (and any read-back via pw_sink_on_sink_node_param()/
// pw_sink_on_device_param()) still arrives asynchronously on pw_sink.loop's own
// thread; we just don't block on it here.
static void pw_sink_apply_node_volume(void) {
  uint32_t n = pw_sink.n_current_volumes > 0 ? pw_sink.n_current_volumes : 2;

  uint8_t pod_buffer[512];
  struct spa_pod_builder b;
  spa_pod_builder_init(&b, pod_buffer, sizeof(pod_buffer));

  struct spa_pod_frame obj_frame, array_frame;
  spa_pod_builder_push_object(&b, &obj_frame, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
  spa_pod_builder_prop(&b, SPA_PROP_channelVolumes, 0);
  spa_pod_builder_push_array(&b, &array_frame);
  for (uint32_t i = 0; i < n; i++)
    spa_pod_builder_float(&b, pw_sink.current_volumes[i]);
  spa_pod_builder_pop(&b, &array_frame);
  const struct spa_pod *param = spa_pod_builder_pop(&b, &obj_frame);

  pw_node_set_param((struct pw_node *)pw_sink.sink_proxy, SPA_PARAM_Props, 0, param);
}

// Write pw_sink.current_volumes into the Device's active Route (the object that
// actually drives the audible/ALSA mixer-control volume for a hardware-routed
// sink). No-op if there is no bound Device or no Route has been resolved yet.
// Caller must hold pw_sink.loop's lock. Does not round-trip; see the comment on
// pw_sink_apply_node_volume() above for why.
static void pw_sink_apply_route_volume(void) {
  if (pw_sink.device_proxy == NULL || !pw_sink.have_route)
    return;

  uint32_t n = pw_sink.n_current_volumes > 0 ? pw_sink.n_current_volumes : 2;

  uint8_t pod_buffer[1024];
  struct spa_pod_builder b;
  spa_pod_builder_init(&b, pod_buffer, sizeof(pod_buffer));

  struct spa_pod_frame route_frame, props_frame, array_frame;
  spa_pod_builder_push_object(&b, &route_frame, SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route);

  spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_index, 0);
  spa_pod_builder_int(&b, pw_sink.route_index);

  spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_device, 0);
  spa_pod_builder_int(&b, pw_sink.route_device);

  spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_props, 0);
  spa_pod_builder_push_object(&b, &props_frame, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
  spa_pod_builder_prop(&b, SPA_PROP_channelVolumes, 0);
  spa_pod_builder_push_array(&b, &array_frame);
  for (uint32_t i = 0; i < n; i++)
    spa_pod_builder_float(&b, pw_sink.current_volumes[i]);
  spa_pod_builder_pop(&b, &array_frame);
  spa_pod_builder_pop(&b, &props_frame);

  spa_pod_builder_prop(&b, SPA_PARAM_ROUTE_save, 0);
  spa_pod_builder_bool(&b, true);

  const struct spa_pod *param = spa_pod_builder_pop(&b, &route_frame);

  pw_device_set_param((struct pw_device *)pw_sink.device_proxy, SPA_PARAM_Route, 0, param);
}

// Set the default sink's volume to the given linear amplitude (0.0 .. 1.0),
// applied to every channel. If the connection isn't ready yet (still resolving,
// or failed to resolve at startup), this is a no-op other than a debug() log --
// there is no subprocess to retry here, unlike the old wpctl-based path, so a
// sink that only appears after shairport-sync has started will simply not be
// controllable until the next restart; see the module-level comment above for
// this limitation.
static void pw_sink_set_volume(float vol_linear) {
  if ((pw_sink.loop == NULL) || (pw_sink.sink_proxy == NULL)) {
    debug(2, "pw: sink: ignoring SetVolume, sink not connected/resolved yet.");
    return;
  }

  if (vol_linear > 1.0f)
    vol_linear = 1.0f;
  if (vol_linear < 0.0f)
    vol_linear = 0.0f;

  pw_thread_loop_lock(pw_sink.loop);

  uint32_t n = pw_sink.n_current_volumes > 0 ? pw_sink.n_current_volumes : 2;
  for (uint32_t i = 0; i < n; i++)
    pw_sink.current_volumes[i] = vol_linear;
  pw_sink.n_current_volumes = n;

  debug(2, "pw: sink: SetVolume -> sink_global_id=%u n=%u v=%f.", pw_sink.sink_global_id, n,
        vol_linear);

  pw_sink_apply_node_volume();
  pw_sink_apply_route_volume();

  pw_thread_loop_unlock(pw_sink.loop);
}

// Mute/unmute the default sink via its Node's SPA_PROP_mute. (Unlike volume,
// mute has no separate Route-level representation to also set -- PipeWire
// devices don't carry a per-route mute distinct from the Node's.) Does not
// round-trip; see the comment on pw_sink_apply_node_volume() above for why.
static void pw_sink_set_mute(int muted) {
  if ((pw_sink.loop == NULL) || (pw_sink.sink_proxy == NULL)) {
    debug(2, "pw: sink: ignoring SetMute, sink not connected/resolved yet.");
    return;
  }

  pw_thread_loop_lock(pw_sink.loop);

  uint8_t pod_buffer[128];
  struct spa_pod_builder b;
  spa_pod_builder_init(&b, pod_buffer, sizeof(pod_buffer));
  float mute_value = muted ? 1.0f : 0.0f;

  struct spa_pod_frame obj_frame;
  spa_pod_builder_push_object(&b, &obj_frame, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props);
  spa_pod_builder_prop(&b, SPA_PROP_mute, 0);
  spa_pod_builder_float(&b, mute_value);
  const struct spa_pod *param = spa_pod_builder_pop(&b, &obj_frame);

  pw_node_set_param((struct pw_node *)pw_sink.sink_proxy, SPA_PARAM_Props, 0, param);

  pw_thread_loop_unlock(pw_sink.loop);
}

// use an SPS_FORMAT_... to find an entry in the format_lookup table or return NULL
static spa_sps_t *sps_format_lookup(sps_format_t to_find) {
  spa_sps_t *response = NULL;
  unsigned int i = 0;
  while ((response == NULL) && (i < sizeof(format_lookup) / sizeof(spa_sps_t))) {
    if (format_lookup[i].sps_format == to_find)
      response = &format_lookup[i];
    else
      i++;
  }
  return response;
}

static void on_process(void *userdata) {

  struct data *local_data = userdata;
  int n_frames = 0;

  pthread_mutex_lock(&buffer_mutex);

  // debug(1, "on_process called.");

  if (stream_is_active == 0)
    debug(1, "on_process called while stream inactive!");

  on_process_is_running = 1;
  if ((audio_occupancy > 0) || (enable_fill)) {

    // get a buffer to see how big it can be
    struct pw_buffer *b = pw_stream_dequeue_buffer(local_data->stream);
    if (b == NULL) {
      pw_log_warn("out of buffers: %m");
      die("PipeWire failure -- out of buffers!");
    }
    struct spa_buffer *buf = b->buffer;
    uint8_t *dest = buf->datas[0].data;
    if (dest != NULL) {
      int stride = local_data->bytes_per_sample * local_data->channels;

      // note: the requested field is the number of frames, not bytes, requested
      int max_possible_frames = SPA_MIN(b->requested, buf->datas[0].maxsize / stride);

      size_t bytes_we_can_transfer = max_possible_frames * stride;

      if (audio_occupancy > 0) {
        // if (enable_fill == 1)) {
        //   debug(1, "got audio -- disable_fill");
        // }
        enable_fill = 0;

        if (bytes_we_can_transfer > audio_occupancy)
          bytes_we_can_transfer = audio_occupancy;

        n_frames = bytes_we_can_transfer / stride;

        size_t bytes_to_end_of_buffer = (size_t)(audio_umb - audio_toq); // must be zero or positive
        if (bytes_we_can_transfer <= bytes_to_end_of_buffer) {
          // the bytes are all in a row in the audio buffer
          memcpy(dest, audio_toq, bytes_we_can_transfer);
          audio_toq += bytes_we_can_transfer;
        } else {
          // the bytes are in two places in the audio buffer
          size_t first_portion_to_write = audio_umb - audio_toq;
          if (first_portion_to_write != 0)
            memcpy(dest, audio_toq, first_portion_to_write);
          uint8_t *new_dest = dest + first_portion_to_write;
          memcpy(new_dest, audio_lmb, bytes_we_can_transfer - first_portion_to_write);
          audio_toq = audio_lmb + bytes_we_can_transfer - first_portion_to_write;
        }
        audio_occupancy -= bytes_we_can_transfer;

      } else {
        debug(3, "send silence");
        // this should really be dithered silence
        memset(dest, 0, bytes_we_can_transfer);
        n_frames = max_possible_frames;
      }

      buf->datas[0].chunk->offset = 0;
      buf->datas[0].chunk->stride = stride;
      buf->datas[0].chunk->size = n_frames * stride;
      pw_stream_queue_buffer(local_data->stream, b);

    } // (else the first data block does not contain a data pointer)
  }
  pthread_mutex_unlock(&buffer_mutex);
}

// Called from the PipeWire thread whenever the stream changes state. The key case for us is
// ERROR or an unexpected return to UNCONNECTED, both of which happen when the PipeWire daemon
// restarts (e.g. after a system suspend/resume cycle). We reset current_encoded_output_format
// to 0 so that the next configure() call is forced to call pw_stream_connect() again rather
// than skipping because it thinks the format is unchanged -- that skip is the root cause of
// silence after resume. We also clear on_process_is_running and stream_is_active so that
// delay() and play() don't try to use a broken stream. The actual reconnect is handled by
// configure() when player.c next asks us to play something.
static void on_state_changed(__attribute__((unused)) void *userdata,
                             enum pw_stream_state old_state, enum pw_stream_state new_state,
                             const char *error) {
  debug(2, "pw: stream state changed: %s -> %s%s%s.", pw_stream_state_as_string(old_state),
        pw_stream_state_as_string(new_state), error ? ": " : "", error ? error : "");
  if (new_state == PW_STREAM_STATE_ERROR ||
      (new_state == PW_STREAM_STATE_UNCONNECTED && old_state != PW_STREAM_STATE_CONNECTING)) {
    if (current_encoded_output_format != 0) {
      debug(1, "pw: stream disconnected unexpectedly -- will reconnect on next configure().");
      current_encoded_output_format = 0;
      on_process_is_running = 0;
      stream_is_active = 0;
    }
  }
}

static const struct pw_stream_events stream_events = {PW_VERSION_STREAM_EVENTS,
                                                      .state_changed = on_state_changed,
                                                      .process = on_process};

static void deinit(void) {
  if (pw_volume_control_enabled && (pw_mixer_control_target == PW_VOLUME_TARGET_SINK))
    pw_sink_disconnect();

  pw_thread_loop_stop(data.loop);
  if (data.stream != NULL)
    pw_stream_destroy(data.stream);
  pw_thread_loop_destroy(data.loop);
  pw_deinit();
  on_process_is_running = 0;
  if (audio_lmb != NULL)
    free(audio_lmb); // deallocate that buffer
}

static int init(__attribute__((unused)) int argc, __attribute__((unused)) char **argv) {
  // set up default values first
  config.audio_backend_buffer_desired_length = 0.5;
  config.audio_backend_buffer_interpolation_threshold_in_seconds =
      0.02; // below this, soxr interpolation will not occur -- it'll be basic interpolation
            // instead.

  config.audio_backend_latency_offset = 0;

  // get settings from settings file, passing in defaults for format_set, rate_set and channel_set
  // Note, these options may be in the "general" stanza or the named stanza
#ifdef CONFIG_FFMPEG
  parse_audio_options("pipewire", SPS_FORMAT_SET, SPS_RATE_SET, SPS_CHANNEL_SET);
#else
  parse_audio_options("pipewire", SPS_FORMAT_NON_FFMPEG_SET, SPS_RATE_NON_FFMPEG_SET,
                      SPS_CHANNNEL_NON_FFMPEG_SET);
#endif

  // now any PipeWire-specific options
  if (config.cfg != NULL) {
    const char *str;

    // Get the optional Application Name, if provided.
    if (config_lookup_non_empty_string(config.cfg, "pipewire.application_name", &str)) {
      config.pw_application_name = (char *)str;
    }

    // Get the optional PipeWire node name, if provided.
    if (config_lookup_non_empty_string(config.cfg, "pipewire.node_name", &str)) {
      config.pw_node_name = (char *)str;
    }

    // Get the optional PipeWire sink target name, if provided.
    if (config_lookup_non_empty_string(config.cfg, "pipewire.sink_target", &str)) {
      config.pw_sink_target = (char *)str;
    }

    // Get the optional mixer type: "sink" drives a sink directly over PipeWire's native
    // protocol -- the sink named by "pipewire.sink_target" if that's set, otherwise the
    // system's current default sink ("@DEFAULT_AUDIO_SINK@") -- intended for setups where only
    // one audio source is ever active at a time, so there's no other client whose volume this
    // would disturb. "stream" instead sets Shairport Sync's own PipeWire stream volume
    // directly, leaving the sink untouched -- appropriate when other PipeWire clients may be
    // sharing the same sink at the same time. If this isn't set, volume is adjusted in software,
    // exactly as Shairport Sync always has done.
    if (config_lookup_non_empty_string(config.cfg, "pipewire.mixer_type", &str)) {
      if (strcasecmp(str, "sink") == 0) {
        pw_volume_control_enabled = 1;
        pw_mixer_control_target = PW_VOLUME_TARGET_SINK;
      } else if (strcasecmp(str, "stream") == 0) {
        pw_volume_control_enabled = 1;
        pw_mixer_control_target = PW_VOLUME_TARGET_STREAM;
      } else {
        warn("Invalid pipewire.mixer_type option choice \"%s\". It should be \"sink\" or "
             "\"stream\".",
             str);
      }
    }
  }

  // finished collecting settings

  audio_lmb = NULL;
  audio_size = 0;
  current_encoded_output_format = 0;
  enable_fill = 1;

  int largc = 0;
  pw_init(&largc, NULL);

  // pw_sink_connect() opens its own PipeWire client connection, which requires the PipeWire
  // library itself to already be initialized -- so this must run after pw_init() above, not
  // before it.
  if (pw_volume_control_enabled && (pw_mixer_control_target == PW_VOLUME_TARGET_SINK)) {
    if (pw_sink_connect() != 0) {
      // Matches the equivalent MPD plugin's Open(), which throws and leaves the mixer
      // unusable rather than silently doing something wrong: disable hardware volume
      // control entirely for this run so player.c falls back to its own software mixer,
      // rather than calling into a half-initialized native sink connection.
      warn("pw: sink: failed to connect to PipeWire for sink volume control; falling "
           "back to software volume control.");
      pw_volume_control_enabled = 0;
    }
  }

  if (pw_volume_control_enabled) {
    debug(2, "pw: hardware volume control enabled, targeting the %s.",
          (pw_mixer_control_target == PW_VOLUME_TARGET_STREAM) ? "stream" : "sink");
    audio_pw.volume = &volume;
    audio_pw.mute = &mute;
    audio_pw.parameters = &parameters;
  } else {
    audio_pw.volume = NULL;
    audio_pw.mute = NULL;
    audio_pw.parameters = NULL;
  }

  /* make a threaded loop. */
  data.loop = pw_thread_loop_new("shairport-sync", NULL);

  pw_thread_loop_lock(data.loop);

  char *appname = config.pw_application_name;
  if (appname == NULL)
    appname = "Shairport Sync";

  char *nodename = config.pw_node_name;
  if (nodename == NULL)
    nodename = "Shairport Sync";

  struct pw_properties *props = pw_properties_new(
      PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback", PW_KEY_MEDIA_ROLE, "Music",
      PW_KEY_APP_NAME, appname, PW_KEY_NODE_NAME, nodename, NULL);

  if (config.pw_sink_target != NULL) {
    debug(3, "setting sink target to \"%s\".", config.pw_sink_target);
    pw_properties_set(props, PW_KEY_TARGET_OBJECT, config.pw_sink_target);
  }

  data.stream = pw_stream_new_simple(pw_thread_loop_get_loop(data.loop), config.appName, props,
                                     &stream_events, &data);
  pw_thread_loop_start(data.loop);

  on_process_is_running = 0;

  pw_thread_loop_unlock(data.loop);
  return 0;
}

static int check_settings(sps_format_t sample_format, unsigned int sample_rate,
                          unsigned int channel_count) {
  // we know the formats with be big- or little-ended.
  // we will accept only S32_..., S16_...

  int response = EINVAL;

  if (sps_format_lookup(sample_format) != NULL)
    response = 0;

  debug(3, "pw: configuration: %u/%s/%u %s.", sample_rate,
        sps_format_description_string(sample_format), channel_count,
        response == 0 ? "is okay" : "can not be configured");
  return response;
}

static int check_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  return check_settings(format, rate, channels);
}

static int32_t get_configuration(unsigned int channels, unsigned int rate, unsigned int format) {
  return search_for_suitable_configuration(channels, rate, format, &check_configuration);
}

static int configure(int32_t requested_encoded_format, char **resulting_channel_map) {
  // debug(2, "pw: configure %s.", short_format_description(requested_encoded_format));
  int response = 0;
  char *channel_map = NULL;
  // if (1) {
  if (current_encoded_output_format != requested_encoded_format) {
    uint64_t start_time = get_absolute_time_in_ns();
    if (current_encoded_output_format == 0)
      debug(2, "pw: setting output configuration to %s.",
            short_format_description(requested_encoded_format));
    else
      // note -- can't use short_format_description twice in one call because it returns the same
      // string buffer each time
      debug(2, "pw: changing output configuration to %s.",
            short_format_description(requested_encoded_format));
    current_encoded_output_format = requested_encoded_format;
    spa_sps_t *format_info =
        sps_format_lookup(FORMAT_FROM_ENCODED_FORMAT(current_encoded_output_format));

    if (format_info == NULL)
      die("Can't find format information!");
    // enum spa_audio_format spa_format = format_info->spa_format;
    data.bytes_per_sample = format_info->bytes_per_sample;
    data.channels = CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format);
    data.rate = RATE_FROM_ENCODED_FORMAT(current_encoded_output_format);

    // Flush the ring buffer before reconnecting. If the stream was broken (e.g. by a
    // suspend/resume cycle), play() continued writing audio while on_process() was not consuming
    // it, so by the time we get here the buffer may be full of stale pre-suspend audio. Without
    // flushing, delay() would immediately report a huge occupancy after reconnect, causing
    // player.c's resampler to overreact and produce the audible "sped-up/slowed-down and jerky"
    // effect. Flushing here gives delay() a clean starting point. buffer_mutex is safe to acquire
    // here because we hold neither it nor data.loop's lock at this point in configure().
    pthread_mutex_lock(&buffer_mutex);
    if (audio_lmb != NULL) {
      audio_toq = audio_eoq = audio_lmb;
      audio_occupancy = 0;
    }
    pthread_mutex_unlock(&buffer_mutex);

    pw_thread_loop_lock(data.loop);
    enable_fill = 0;

    if (pw_stream_get_state(data.stream, NULL) != PW_STREAM_STATE_UNCONNECTED) {
      response = pw_stream_disconnect(data.stream);
      if (response != 0) {
        debug(1, "error %d disconnecting stream.", response);
      }
    }

    if (audio_lmb != NULL) {
      // debug(3, "deallocating existing audio_pw.c buffer.");
      free(audio_lmb);
    }

    audio_size = data.rate * BUFFER_SIZE_IN_SECONDS * data.bytes_per_sample * data.channels;
    // allocate space for the audio buffer
    audio_lmb = malloc(audio_size);
    if (audio_lmb == NULL)
      die("Can't allocate %zd bytes for PipeWire buffer.", audio_size);
    audio_toq = audio_eoq = audio_lmb;
    audio_umb = audio_lmb + audio_size;
    audio_occupancy = 0;

    // Make one parameter with the supported formats. The SPA_PARAM_EnumFormat
    // id means that this is a format enumeration (of 1 value).
    struct spa_pod_builder b = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));

    const struct spa_pod *params[1];
    // create a stream with the default channel layout corresponding to
    // the number of channels
    switch (CHANNELS_FROM_ENCODED_FORMAT(current_encoded_output_format)) {
    case 1:
      channel_map = channel_map_mono;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          // we are giving the position of 8 channels here, even if we need less than that.
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate, .position = {SPA_AUDIO_CHANNEL_FC}));
      break;
    case 2:
      channel_map = channel_map_stereo;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          // we are giving the position of 8 channels here, even if we need less than that.
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR}));
      break;
    case 3:
      channel_map = channel_map_2p1;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          // we are giving the position of 8 channels here, even if we need less than that.
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                                                SPA_AUDIO_CHANNEL_LFE}));
      break;
    case 4:
      channel_map = channel_map_4p0;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          // we are giving the position of 8 channels here, even if we need less than that.
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                                                SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_BC}));
      break;
    case 5:
      channel_map = channel_map_5p0;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          // we are giving the position of 8 channels here, even if we need less than that.
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                                                SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_RL,
                                                SPA_AUDIO_CHANNEL_RR}));
      break;
    case 6:
      channel_map = channel_map_5p1;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                                                SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
                                                SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR}));
      break;
    case 7:
      channel_map = channel_map_6p1;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                                                SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
                                                SPA_AUDIO_CHANNEL_BC, SPA_AUDIO_CHANNEL_SL,
                                                SPA_AUDIO_CHANNEL_SR}));
      break;
    case 8:
      channel_map = channel_map_7p1;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                                                SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
                                                SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR,
                                                SPA_AUDIO_CHANNEL_SL, SPA_AUDIO_CHANNEL_SR}));
      break;
    default:
      channel_map = NULL;
      params[0] = spa_format_audio_raw_build(
          &b, SPA_PARAM_EnumFormat,
          // we are giving the position of 8 channels here, even if we need less than that.
          &SPA_AUDIO_INFO_RAW_INIT(.format = format_info->spa_format, .channels = data.channels,
                                   .rate = data.rate,
                                   .position = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR,
                                                SPA_AUDIO_CHANNEL_FC, SPA_AUDIO_CHANNEL_LFE,
                                                SPA_AUDIO_CHANNEL_RL, SPA_AUDIO_CHANNEL_RR,
                                                SPA_AUDIO_CHANNEL_SL, SPA_AUDIO_CHANNEL_SR}));
      break;
    }

    // Now connect this stream. We ask that our process function is
    // called in a realtime thread.
    pw_stream_connect(data.stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                      PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                          PW_STREAM_FLAG_RT_PROCESS,
                      params, 1);
    if ((!pw_volume_control_enabled) || (pw_mixer_control_target != PW_VOLUME_TARGET_STREAM))
      reset_stream_to_unity();
    else
      reapply_last_requested_stream_volume_and_mute();
    stream_is_active = 0;
    enable_fill = 1;
    pw_thread_loop_unlock(data.loop);

    int64_t elapsed_time = get_absolute_time_in_ns() - start_time;
    debug(3, "pw: configuration took %0.3f mS.", elapsed_time * 0.000001);
  } else {
    debug(2, "pw: setting output configuration  -- configuration unchanged, so nothing done.");
  }
  if ((response == 0) && (resulting_channel_map != NULL)) {
    *resulting_channel_map = channel_map;
  }
  return response;
}

static int play(__attribute__((unused)) void *buf, int samples,
                __attribute__((unused)) int sample_type, __attribute__((unused)) uint32_t timestamp,
                __attribute__((unused)) uint64_t playtime) {
  if (stream_is_active == 0) {
    pw_thread_loop_lock(data.loop);
    on_process_is_running = 0;
    pw_stream_set_active(data.stream, true);
    pw_thread_loop_unlock(data.loop);
    stream_is_active = 1;
    // debug(1, "set stream active");
  }
  // copy the samples into the queue
  // debug(3, "play %u samples; %u samples already in the buffer.", samples, audio_occupancy /
  // (data.bytes_per_sample * data.channels));
  size_t bytes_to_transfer = samples * data.channels * data.bytes_per_sample;
  pthread_mutex_lock(&buffer_mutex);
  size_t bytes_available = audio_size - audio_occupancy;
  if (bytes_available < bytes_to_transfer)
    bytes_to_transfer = bytes_available;
  if (bytes_to_transfer > 0) {
    size_t space_to_end_of_buffer = audio_umb - audio_eoq;
    if (space_to_end_of_buffer >= bytes_to_transfer) {
      memcpy(audio_eoq, buf, bytes_to_transfer);
      audio_eoq += bytes_to_transfer;
    } else {
      memcpy(audio_eoq, buf, space_to_end_of_buffer);
      buf += space_to_end_of_buffer;
      memcpy(audio_lmb, buf, bytes_to_transfer - space_to_end_of_buffer);
      audio_eoq = audio_lmb + bytes_to_transfer - space_to_end_of_buffer;
    }
    audio_occupancy += bytes_to_transfer;
  }
  pthread_mutex_unlock(&buffer_mutex);
  return 0;
}

static int delay(long *the_delay) {
  long result = 0;
  int reply = -ENODEV; // ENODATA is not defined in FreeBSD

  if (on_process_is_running == 0) {
    debug(3, "pw_processor not running");
  }

  if ((stream_is_active == 0) && (on_process_is_running != 0)) {
    debug(3, "stream not active but on_process_is_running is true.");
  }
  if (on_process_is_running != 0) {

    struct pw_time stream_time_info_1, stream_time_info_2;
    ssize_t audio_occupancy_now;

    // get stable pw_time info to ensure we get an audio occupancy figure
    // that relates to the pw_time we have.
    // we do this by getting a pw_time before and after getting the occupancy
    // and accepting the information if they are both the same

    int loop_count = 1;
    int non_matching;
    int stream_time_valid_if_zero;
    do {
      stream_time_valid_if_zero =
          pw_stream_get_time_n(data.stream, &stream_time_info_1, sizeof(struct pw_time));
      audio_occupancy_now = audio_occupancy;
      pw_stream_get_time_n(data.stream, &stream_time_info_2, sizeof(struct pw_time));

      non_matching = memcmp(&stream_time_info_1, &stream_time_info_2, sizeof(struct pw_time));
      if (non_matching != 0) {
        loop_count++;
      }
    } while (((non_matching != 0) || (stream_time_valid_if_zero != 0)) && (loop_count < 10));

    if (non_matching != 0) {
      debug(1, "can't get a stable pw_time!");
    }
    if (stream_time_valid_if_zero != 0) {
      debug(1, "can't get valid stream info");
    }
    if (stream_time_info_1.rate.denom == 0) {
      debug(2, "non valid stream_time_info_1");
    }

    if ((non_matching == 0) && (stream_time_valid_if_zero == 0) &&
        (stream_time_info_1.rate.denom != 0)) {
      int64_t interval_from_pw_time_to_now_ns =
          pw_stream_get_nsec(data.stream) - stream_time_info_1.now;

      uint64_t frames_possibly_played_since_measurement =
          ((interval_from_pw_time_to_now_ns * data.rate) + 500000000L) / 1000000000L;

      uint64_t net_delay_in_frames = stream_time_info_1.queued + stream_time_info_1.buffered;

      uint64_t fixed_delay_ns =
          (stream_time_info_1.delay * stream_time_info_1.rate.num * 1000000000L) /
          stream_time_info_1.rate.denom; // ns;
      uint64_t fixed_delay_in_frames = ((fixed_delay_ns * data.rate) + 500000000L) / 1000000000L;

      net_delay_in_frames = net_delay_in_frames + fixed_delay_in_frames +
                            audio_occupancy_now / (data.bytes_per_sample * data.channels) -
                            frames_possibly_played_since_measurement;

      result = net_delay_in_frames;
      reply = 0;
    }
  }

  *the_delay = result;
  return reply;
}

static void flush(void) {
  pthread_mutex_lock(&buffer_mutex);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;
  // if (enable_fill == 0) {
  //   debug(1, "flush enable_fill");
  // }
  enable_fill = 1;
  pthread_mutex_unlock(&buffer_mutex);
}

// Both targets get vol (centi-dB, 0 = unity, matching volume_range above) from player.c and
// convert to linear amplitude with dB = vol/100, linear = 10^(dB/20) = 10^(vol/2000).
// SPA_PROP_channelVolumes is linear amplitude everywhere it appears -- Node Props, Device
// Route, or our own stream -- regardless of what a given tool displays. wpctl/pavucontrol
// happen to *display* on a cubic scale (displayed = raw_linear^(1/3), confirmed empirically:
// a sink read back here at 0.008 showed as 0.20 in `wpctl get-volume`, and 0.008^(1/3) = 0.20),
// but that's a UI convention only -- nothing here needs to convert to or from it.

// PW_VOLUME_TARGET_STREAM: set our own stream's volume/mute directly.
static void volume_stream(double vol) {
  pw_last_requested_vol_centidb = vol; // cached for reapply on (re)connect -- see above
  float gain = (float)pow(10.0, vol / 2000.0);
  pw_thread_loop_lock(data.loop);
  if (apply_stream_channel_volumes(gain) != 0)
    debug(1, "pw: could not set the stream volume control."); // expected pre-connect; reapplied
                                                                // once the stream connects
  pw_thread_loop_unlock(data.loop);
}

// Returns 0 on success, non-zero if player.c should fall back to a software mute.
static int mute_stream(int mute_state_requested) {
  pw_last_requested_mute = mute_state_requested; // cached for reapply on (re)connect
  pw_thread_loop_lock(data.loop);
  int response = apply_stream_mute(mute_state_requested);
  pw_thread_loop_unlock(data.loop);
  if (response != 0)
    debug(1, "pw: could not set the stream mute control."); // expected pre-connect
  return response;
}

// PW_VOLUME_TARGET_SINK (default): drive the target sink over PipeWire's native protocol (see
// the pw_sink_* block above) instead of our own stream. Intended for boxes where only one audio
// source is ever active at a time, so the shared sink volume should track AirPlay volume; use
// PW_VOLUME_TARGET_STREAM instead if other clients may share the sink concurrently.
static void volume_sink(double vol) {
  pw_last_requested_vol_centidb = vol; // cached for reapply on reconnect after suspend/resume
  pw_thread_loop_lock(data.loop);
  reset_stream_to_unity();
  pw_thread_loop_unlock(data.loop);

  pw_sink_set_volume((float)pow(10.0, vol / 2000.0));
}

// Always reports success: muting the sink is the real mute here, so player.c shouldn't also
// fall back to a software mute.
static int mute_sink(int mute_state_requested) {
  pw_thread_loop_lock(data.loop);
  reset_stream_to_unity();
  pw_thread_loop_unlock(data.loop);

  pw_sink_set_mute(mute_state_requested);
  return 0;
}

// -------- dispatch, based on pipewire.mixer_type --------
static void volume(double vol) {
  if (pw_mixer_control_target == PW_VOLUME_TARGET_STREAM)
    volume_stream(vol);
  else
    volume_sink(vol);
}

static int mute(int mute_state_requested) {
  if (pw_mixer_control_target == PW_VOLUME_TARGET_STREAM)
    return mute_stream(mute_state_requested);
  return mute_sink(mute_state_requested);
}

static output_parameters_t *parameters(void) { return &output_parameters; }

static void stop(void) {
  pthread_mutex_lock(&buffer_mutex);
  audio_toq = audio_eoq = audio_lmb;
  audio_umb = audio_lmb + audio_size;
  audio_occupancy = 0;
  // if (enable_fill == 0) {
  //   debug(1, "stop enable_fill");
  // }
  pthread_mutex_unlock(&buffer_mutex);
  if (stream_is_active == 1) {
    pw_thread_loop_lock(data.loop);
    // pw_stream_flush(data.stream, true);
    pw_stream_set_active(data.stream, false);
    pw_thread_loop_unlock(data.loop);
    stream_is_active = 0;
    // debug(1, "set stream inactive");
  }
}

audio_output audio_pw = {.name = "pipewire",
                         .help = NULL,
                         .init = &init,
                         .deinit = &deinit,
                         .start = NULL,
                         .get_configuration = &get_configuration,
                         .configure = &configure,
                         .stop = &stop,
                         .is_running = NULL,
                         .flush = &flush,
                         .delay = &delay,
                         .stats = NULL,
                         .play = &play,
                         .volume = NULL,   // set in init(), depending on configuration
                         .parameters = NULL, // set in init(), depending on configuration
                         .mute = NULL};      // set in init(), depending on configuration
