#ifdef __cplusplus
extern "C" {
#endif

#ifndef _COMMON_H
#define _COMMON_H

#define _GNU_SOURCE

#include <sys/types.h> // for mode_t
#include <unistd.h>    // for useconds_t

#include "config.h"
#include "definitions.h"
#include "mdns.h"

// struct sockaddr_in6 is bigger than struct sockaddr. derp
#ifdef AF_INET6
#define SOCKADDR struct sockaddr_storage
#define SAFAMILY ss_family
#else
#define SOCKADDR struct sockaddr
#define SAFAMILY sa_family
#endif

#if defined(CONFIG_CONVOLUTION)
typedef enum { ev_unchecked, ev_okay, ev_invalid } ir_file_evaluation;

typedef struct {
  unsigned int samplerate; // initialized to 0, will be filter frame rate
  unsigned int channels;
  char *filename; // the parsed filename
} ir_file_info_t;
#endif

#if defined(CONFIG_DBUS_INTERFACE) || defined(CONFIG_MPRIS_INTERFACE)
#include <glib.h>
typedef enum {
  DBT_default = 0,
  DBT_system,  // use the system bus
  DBT_session, // use the session bus
} dbus_message_bus_t;
#endif

typedef enum {
  TOE_normal,
  TOE_emergency,
} type_of_exit_type;

#define sps_extra_code_output_stalled 32768
#define sps_extra_code_output_state_cannot_make_ready 32769

// yeah/no/auto
typedef enum { YNA_AUTO = -1, YNA_NO = 0, YNA_YES = 1 } yna_type;

// yeah/no/dont-care
typedef enum { YNDK_DONT_KNOW = -1, YNDK_NO = 0, YNDK_YES = 1 } yndk_type;

typedef enum {
  SS_LITTLE_ENDIAN = 0,
  SS_PDP_ENDIAN,
  SS_BIG_ENDIAN,
} endian_type;

typedef enum {
  ST_basic = 0, // straight deletion or insertion of a frame in a 352-frame packet
  ST_vernier,   // interpolate from 352/1024 samples to 353/1025 or 351/1023
  ST_soxr,      // use libsoxr to make a 352 frame packet one frame longer or shorter
  ST_auto,      // use soxr if compiled for it and if the soxr_index is low enough
} stuffing_type;

typedef enum {
  ST_stereo = 0,
  ST_mono,
  ST_reverse_stereo,
  ST_left_only,
  ST_right_only,
} playback_mode_type;

typedef enum {
  VCP_standard = 0,
  VCP_flat,
  VCP_dasl_tapered,
} volume_control_profile_type;

typedef enum {
  decoder_hammerton = 0,
  decoder_apple_alac,
  decoder_ffmpeg_alac,
} decoders_supported_type;

typedef enum {
  disable_standby_off = 0,
  disable_standby_auto,
  disable_standby_always
} disable_standby_mode_type;

// the following enum is for the formats recognised -- currently only S16LE is recognised for input,
// so these are output only for the present

// ensure sps_format_sample_size_array and sps_format_description_string_array are in sync with
// this!
typedef enum {
  SPS_FORMAT_UNKNOWN = 0,
  SPS_FORMAT_S8,
  SPS_FORMAT_LOWEST = SPS_FORMAT_S8,
  SPS_FORMAT_U8,
  SPS_FORMAT_S16_LE,
  SPS_FORMAT_S16_BE,
  SPS_FORMAT_S24_LE,
  SPS_FORMAT_S24_BE,
  SPS_FORMAT_S24_3LE,
  SPS_FORMAT_S24_3BE,
  SPS_FORMAT_S32_LE,
  SPS_FORMAT_S32_BE,
  SPS_FORMAT_HIGHEST_NATIVE = SPS_FORMAT_S32_BE,
  SPS_FORMAT_S16,
  SPS_FORMAT_S24,
  SPS_FORMAT_S32,
  SPS_FORMAT_AUTO,
  SPS_FORMAT_INVALID,
} sps_format_t;

typedef enum {
  SPS_RATE_UNKNOWN = 0,
  SPS_RATE_5512,
  SPS_RATE_LOWEST = SPS_RATE_5512,
  SPS_RATE_8000,
  SPS_RATE_11025,
  SPS_RATE_16000,
  SPS_RATE_22050,
  SPS_RATE_32000,
  SPS_RATE_44100,
  SPS_RATE_48000,
  SPS_RATE_64000,
  SPS_RATE_88200,
  SPS_RATE_96000,
  SPS_RATE_176400,
  SPS_RATE_192000,
  SPS_RATE_352800,
  SPS_RATE_384000,
  SPS_RATE_HIGHEST = SPS_RATE_384000,
  SPS_RATE_ILLEGAL,
} sps_rate_t;

// these sets omit the _UNKNOWN, _AUTO and _ILLEGAL values
#define SPS_FORMAT_SET (((1 << (SPS_FORMAT_HIGHEST_NATIVE + 1)) - 1) - (1 << SPS_FORMAT_UNKNOWN))
#define SPS_RATE_SET (((1 << (SPS_RATE_HIGHEST + 1)) - 1) - (1 << SPS_RATE_UNKNOWN))
#define SPS_CHANNEL_SET (((1 << (8 + 1)) - 1) - (1 << 0)) // channels 1 to 8, not 0-based!

#ifndef CONFIG_AIRPLAY_2
#define SPS_FORMAT_NON_FFMPEG_SET SPS_FORMAT_SET
#define SPS_RATE_NON_FFMPEG_SET                                                                    \
  ((1 << SPS_RATE_44100) | (1 << SPS_RATE_88200) | (1 << SPS_RATE_176400) | (1 << SPS_RATE_352800))
#define SPS_CHANNNEL_NON_FFMPEG_SET (1 << 2)
#endif

// up to 1048576 fps, but must be an even number
#define RATE_FROM_ENCODED_FORMAT(encoded_format) (((encoded_format >> 6) & 0x7FFFF) * 2)
#define RATE_TO_ENCODED_FORMAT(rate) (((rate / 2) & 0x7FFFF) << 6)

// up to 127 channels
#define CHANNELS_FROM_ENCODED_FORMAT(encoded_format) ((encoded_format >> 25) & 0x7F)
#define CHANNELS_TO_ENCODED_FORMAT(channels) ((channels & 0x7F) << 25)

// up to 64 different SPS_FORMATs
#define FORMAT_FROM_ENCODED_FORMAT(encoded_format) (encoded_format & 0x3F)
#define FORMAT_TO_ENCODED_FORMAT(format) (format & 0x3F)

const char *short_format_description(int32_t encoded_format);
const char *sps_format_description_string(sps_format_t format);

unsigned int sps_format_sample_size(sps_format_t format);
unsigned int sps_rate_actual_rate(sps_rate_t rate);

typedef struct {
  double missing_port_dacp_scan_interval_seconds; // if no DACP port number can be found, check at
                                                  // these intervals
  double resend_control_first_check_time; // wait this long before asking for a missing packet to be
                                          // resent
  double resend_control_check_interval_time; // wait this long between making requests
  double resend_control_last_check_time; // if the packet is missing this close to the time of use,
                                         // give up
  pthread_mutex_t lock;
  config_t *cfg;
  int endianness;
  double airplay_volume; // stored here for reloading when necessary
  double default_airplay_volume;
  char *appName; // normally the app is called shairport-syn, but it may be symlinked
  char *password;
  char *service_name; // the name for the shairport service, e.g. "Shairport Sync Version %v running
                      // on host %h"

#ifdef CONFIG_PULSEAUDIO
  char *pa_server;           // the pulseaudio server address that Shairport Sync will play on.
  char *pa_application_name; // the name under which Shairport Sync shows up as an "Application" in
                             // the Sound Preferences in most desktop Linuxes.
  // Defaults to "Shairport Sync". Shairport Sync must be playing to see it.

  char *pa_sink; // the name (or id) of the sink that Shairport Sync will play on.
#endif
#ifdef CONFIG_PIPEWIRE
  char *pw_application_name; // the name under which Shairport Sync shows up as an "Application" in
                             // the Sound Preferences in most desktop Linuxes.
                             // Defaults to "Shairport Sync".

  char *pw_node_name;   // defaults to the application's name, usually "shairport-sync".
  char *pw_sink_target; // leave this unset if you don't want to change the sink_target.
#endif
#ifdef CONFIG_METADATA
  int metadata_enabled;
  char *metadata_pipename;
  char *metadata_sockaddr;
  int metadata_sockport;
  size_t metadata_sockmsglength;
  int get_coverart;
  double metadata_progress_interval; // 0 means no progress reports
#endif
#ifdef CONFIG_MQTT
  int mqtt_enabled;
  char *mqtt_hostname;
  int mqtt_port;
  char *mqtt_username;
  char *mqtt_password;
  char *mqtt_capath;
  char *mqtt_cafile;
  char *mqtt_certfile;
  char *mqtt_keyfile;
  char *mqtt_topic;
  int mqtt_publish_raw;
  int mqtt_publish_parsed;
  int mqtt_publish_cover;
  int mqtt_publish_retain;
  int mqtt_enable_remote;
  int mqtt_enable_autodiscovery;
  char *mqtt_autodiscovery_prefix;
  char *mqtt_empty_payload_substitute;
#endif
  uint8_t ap1_prefix[6];
  uint8_t hw_addr[8]; // only needs 6 but 8 is handy when converting this to a number
  int port;
  int udp_port_base;
  int udp_port_range;
  int ignore_volume_control;
  int volume_max_db_set; // set to 1 if a maximum volume db has been set
  int volume_max_db;
  int no_sync;             // disable synchronisation, even if it's available
  int no_mmap;             // disable use of mmap-based output, even if it's available
  double resync_threshold; // if it gets out of whack by more than this number of seconds, do a
                           // resync. if zero, never do a resync.
  int allow_session_interruption;
  int timeout; // while in play mode, exit if no packets of audio come in for more than this number
               // of seconds . Zero means never exit.
  int dont_check_timeout; // this is used to maintain backward compatibility with the old -t option
                          // behaviour; only set by -t 0, cleared by everything else
  char *output_name;
  audio_output *output;
  char *mdns_name;
  mdns_backend *mdns;
  int buffer_start_fill;
  uint32_t userSuppliedLatency; // overrides all other latencies -- use with caution
  uint32_t fixedLatencyOffset;  // add this to all automatic latencies supplied to get the actual
                                // total latency
// the total latency will be limited to the min and max-latency values, if supplied
#ifdef CONFIG_LIBDAEMON
  int daemonise;
  int daemonise_store_pid; // don't try to save a PID file
  char *piddir;
  char *computed_piddir; // the actual pid directory to create, if any
  char *pidfile;
#endif

  int log_fd;                      // file descriptor of the file or pipe to log stuff to.
  char *log_file_path;             // path to file or pipe to log to, if any
  int logOutputLevel;              // log output level
  int debugger_show_elapsed_time;  // in the debug message, display the time since startup
  int debugger_show_relative_time; // in the debug message, display the time since the last one
  int debugger_show_file_and_line; // in the debug message, display the filename and line number
  int statistics_requested, use_negotiated_latencies;
  playback_mode_type playback_mode;
  char *cmd_start, *cmd_stop, *cmd_set_volume, *cmd_unfixable;
  char *cmd_active_start, *cmd_active_stop;
  int cmd_blocking, cmd_start_returns_output;
  double tolerance; // allow this much drift before attempting to correct it
  stuffing_type packet_stuffing;
  int soxr_delay_index;
  int soxr_delay_threshold; // the soxr delay must be less or equal to this for soxr interpolation
                            // to be enabled under the auto setting
  int decoders_supported;
  int decoder_in_use;
  // char *logfile;
  // char *errfile;
  char *configfile;
  char *regtype; // The regtype is the service type followed by the protocol, separated by a dot, by
                 // default “_raop._tcp.” for AirPlay 1.
  char *regtype2;  // The regtype is the service type followed by the protocol, separated by a dot,
                   // by default “_raop._tcp.” for AirPlay 2.
  char *interface; // a string containg the interface name, or NULL if nothing specified
  int interface_index;                        // only valid if the interface string is non-NULL
  double audio_backend_buffer_desired_length; // this will be the length in seconds of the
                                              // audio backend buffer -- the DAC buffer for ALSA
  double audio_backend_buffer_interpolation_threshold_in_seconds; // below this, soxr interpolation
                                                                  // will not occur -- it'll be
                                                                  // basic interpolation instead.
  double audio_decoded_buffer_desired_length;    // the length of the buffer of fully decoded audio
                                                 // prior to being sent to the output device
  double disable_standby_mode_silence_threshold; // below this, silence will be added to the output
                                                 // buffer
  double disable_standby_mode_silence_scan_interval; // check the threshold this often

  double audio_backend_latency_offset; // this will be the offset in seconds to compensate for any
                                       // fixed latency there might be in the audio path
  int audio_backend_silent_lead_in_time_auto; // true if the lead-in time should be from as soon as
                                              // packets are received
  double audio_backend_silent_lead_in_time; // the length of the silence that should precede a play.
  uint32_t minimum_free_buffer_headroom; // when effective latency is calculated, ensure this number
                                         // of buffers are unallocated
  double active_state_timeout; // the amount of time from when play ends to when the system leaves
                               // into the "active" mode.
  uint32_t volume_range_db; // the range, in dB, from max dB to min dB. Zero means use the mixer's
                            // native range.
  int volume_range_hw_priority; // when extending the volume range by combining sw and hw
                                // attenuators, lowering the volume, use all the hw attenuation
                                // before using
                                // sw attenuation
  volume_control_profile_type volume_control_profile;
  int output_format_auto_requested; // true if the configuration requests auto configuration
  int output_rate_auto_requested;   // true if the configuration requests auto configuration
  uint32_t current_output_configuration;
  // these are the formats/rate and channel configurations permitted by the settings or defaults
  uint32_t format_set;
  uint32_t rate_set;
  uint32_t channel_set;

#ifdef CONFIG_CONVOLUTION
  int convolution_enabled;
  unsigned int convolution_rate; // 0 means the convolver has never been initialised, so ignore
                                 // convolver_valid.
  // but if this is the same as the current rate and convolver_valid is false, it means that an
  // attempt to initialise the convolver has failed.
  size_t convolution_block_size;
  unsigned int convolution_ir_file_count;
  ir_file_info_t *convolution_ir_files; // NULL or an array of information about all the impulse
                                        // response files loaded
  int convolution_ir_files_updated; // set to true if the convolution_ir_files are changed. Cleared
                                    // when the convolver has been initialised
  int convolver_valid;              // set to true if the convolver can be initialised
  unsigned int convolution_threads; // number of threads in the convolver thread pool
  float convolution_gain;
  double convolution_max_length_in_seconds;
#endif

  int loudness_enabled;
  float loudness_reference_volume_db;
  int alsa_use_hardware_mute;
  double alsa_maximum_stall_time;
  disable_standby_mode_type disable_standby_mode;
  volatile int keep_dac_busy;
  yna_type use_precision_timing; // defaults to no

#ifdef CONFIG_METADATA_HUB
  char *cover_art_cache_dir;
  int retain_coverart;

  int scan_interval_when_active;   // number of seconds between DACP server scans when playing
                                   // something (1)
  int scan_interval_when_inactive; // number of seconds between DACP server scans playing nothing
                                   // (3)
  int scan_max_bad_response_count; // number of successive bad results to ignore before giving up
                                   // (10)
  int scan_max_inactive_count;     // number of scans to do before stopping if not made active again
                                   // (about 15 minutes worth)
#endif
  int disable_resend_requests; // set this to stop resend request being made for missing packets
  double diagnostic_drop_packet_fraction; // pseudo randomly drop this fraction of packets, for
                                          // debugging. Currently audio packets only...
#ifdef CONFIG_JACK
  char *jack_client_name;
  char *jack_autoconnect_pattern;
#ifdef CONFIG_SOXR
  int jack_soxr_resample_quality;
#endif
#endif
  void *gradients; // a linked list of the clock gradients discovered for all DACP IDs
                   // can't use IP numbers as they might be given to different devices
                   // can't get hold of MAC addresses.
                   // can't define the nvll linked list struct here
  char *firmware_version;
  // use these in information requests
  char *model;
  char *srcvers;
  char *osvers;

#ifdef CONFIG_AIRPLAY_2
  uint64_t airplay_features;
  uint32_t airplay_statusflags;
  char *airplay_fex;       // a base64-encoded version of the airplay_features in little-endian form
  char *airplay_device_id; // for the Bonjour advertisement and the GETINFO PList
  char *airplay_pin;       // non-NULL, 4 char PIN, if required for pairing
  char *airplay_pi;        // UUID in the Bonjour advertisement and the GETINFO Plist
  char *airplay_pgid;      // UUID in the txtAirPlay data sent on the event channel
  char *airplay_psi;       // type 4 fixed UUID
  uint8_t airplay_pk[32];  // public key
  char *pk_string;
  char *nqptp_shared_memory_interface_name; // client name for nqptp service
#endif
  int unfixable_error_reported; // only report once.

  uint64_t eight_channel_layout; // non-zero means enabled and is a channel layout
  uint64_t six_channel_layout;   // non-zero means enabled and is a channel layout

  int mixdown_enable;
  uint64_t mixdown_channel_layout;   // if mixdown_enable is true, 0 signifies auto, based on number
                                     // of channels
  int output_channel_mapping_enable; // 0 means off, non-zero means on. If on and
                                     // output_channel_map_size is 0, use the device's channel map
  const char *output_channel_map[8]; // names of the output channels
  unsigned int output_channel_map_size; // number of output channels

#if defined(CONFIG_DBUS_INTERFACE) || defined(CONFIG_MPRIS_INTERFACE)
  GMainLoop *glib_worker_loop;
  // for clean quitting from a dbus interface quit request (from the DBus or MPRIS interfaces)
  int quit_requested_from_glib_mainloop; // remember that it initialised to zero.
  dbus_message_bus_t dbus_default_message_bus;

#if defined(CONFIG_DBUS_INTERFACE)
  dbus_message_bus_t dbus_service_bus_type;
#endif
#if defined(CONFIG_MPRIS_INTERFACE)
  dbus_message_bus_t mpris_service_bus_type;
#endif

#endif

} shairport_cfg;

uint32_t nctohl(const uint8_t *p);  // read 4 characters from *p and do ntohl on them
uint16_t nctohs(const uint8_t *p);  // read 2 characters from *p and do ntohs on them
uint64_t nctoh64(const uint8_t *p); // read 8 characters from *p to a uint64_t

void memory_barrier();

void log_to_stderr(); // call this to direct logging to stderr;
void log_to_stdout(); // call this to direct logging to stdout;
void log_to_syslog(); // call this to direct logging to the system log;
void log_to_file();   // call this to direct logging to a file or (pre-existing) pipe;

// true if Shairport Sync is supposed to be sending output to the output device, false otherwise

int get_requested_connection_state_to_output();

void set_requested_connection_state_to_output(int v);

int try_to_open_pipe_for_writing(
    const char *pathname); // open it without blocking if it's not hooked up

/* from
 * http://coding.debuntu.org/c-implementing-str_replace-replace-all-occurrences-substring#comment-722
 */
char *str_replace(const char *string, const char *substr, const char *replacement);

// based on http://burtleburtle.net/bob/rand/smallprng.html

void r64init(uint64_t seed);
uint64_t r64u();
int64_t r64i();

// if you are breaking in to a session, you need to avoid the ports of the current session
// if you are law-abiding, then you can reuse the ports.
// so, you can reset the free UDP ports minder when you're legit, and leave it otherwise

// the downside of using different ports each time is that it might make the firewall
// rules a bit more complex, as they need to allow more than the minimum three ports.
// a range of 10 is suggested anyway

void resetFreeUDPPort();
uint16_t nextFreeUDPPort();

extern volatile int debuglev;

void _die(const char *filename, const int linenumber, const char *format, ...);
void _warn(const char *filename, const int linenumber, const char *format, ...);
void _inform(const char *filename, const int linenumber, const char *format, ...);
void _debug(const char *filename, const int linenumber, int level, const char *format, ...);
void _debug_print_buffer(const char *thefilename, const int linenumber, int level, void *buf,
                         size_t buf_len);

#define die(...) _die(__FILE__, __LINE__, __VA_ARGS__)
#define debug(...) _debug(__FILE__, __LINE__, __VA_ARGS__)
#define warn(...) _warn(__FILE__, __LINE__, __VA_ARGS__)
#define inform(...) _inform(__FILE__, __LINE__, __VA_ARGS__)
#define debug_print_buffer(...) _debug_print_buffer(__FILE__, __LINE__, __VA_ARGS__)

// Thanks to https://stackoverflow.com/a/1597129 for the inspiration for this identifier generation
#define MAKEUNIQUEID2(x, y) x##y
#define MADEID(x, y) MAKEUNIQUEID2(x, y)

// do X once, and never again until the app is restarted
#define once(X)                                                                                    \
  static int MADEID(once_flag_, __LINE__) = 0;                                                     \
  if (MADEID(once_flag_, __LINE__) == 0) {                                                         \
    X;                                                                                             \
    MADEID(once_flag_, __LINE__) = 1;                                                              \
  }

// do X once, and then ignore repeated calls until they stop for more than one second
#define once_per_1_second_burst(X)                                                                 \
  static uint64_t MADEID(time_, __LINE__) = 0;                                                     \
  int64_t MADEID(interval_, __LINE__) = get_absolute_time_in_ns() - MADEID(time_, __LINE__);       \
  if ((MADEID(time_, __LINE__) == 0) || (MADEID(interval_, __LINE__) > 1000000000L))               \
    X;                                                                                             \
  MADEID(time_, __LINE__) = get_absolute_time_in_ns()

void getErrorText(char *destinationString, size_t destinationStringLength);

uint8_t *base64_dec(char *input, int *outlen);
char *base64_enc(uint8_t *input, int length);

char *base64_encode_so(const unsigned char *data, size_t input_length, char *encoded_data,
                       size_t *output_length);

#define RSA_MODE_AUTH (0)
#define RSA_MODE_KEY (1)
uint8_t *rsa_apply(uint8_t *input, int inlen, int *outlen, int mode);

// given a volume (0 to -30) and high and low attenuations in dB*100 (e.g. 0 to -6000 for 0 to -60
// dB), return an attenuation depending on a linear interpolation along the range
double flat_vol2attn(double vol, long max_db, long min_db);

// The intention behind dasl_tapered is that a given percentage change in volume should result in
// the same percentage change in perceived loudness. For instance, doubling the volume level should
// result in doubling the perceived loudness. With the range of AirPlay volume being from -30 to 0,
// doubling the volume from -22.5 to -15 results in an increase of 10 dB. Similarly, doubling the
// volume from -15 to 0 results in an increase of 10 dB. For compatibility with mixers having a
// restricted attenuation range (e.g. 30 dB), "dasl_tapered" will switch to a flat profile at low
// AirPlay volumes.
double dasl_tapered_vol2attn(double vol, long max_db, long min_db);
// given a volume (0 to -30) and high and low attenuations in dB*100 (e.g. 0 to -6000 for 0 to -60
// dB), return an attenuation depending on the transfer function
double vol2attn(double vol, long max_db, long min_db);

// return a time in nanoseconds
#ifdef COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN_AND_OPENBSD
// Not defined for macOS
uint64_t get_realtime_in_ns(void);
#endif
uint64_t get_absolute_time_in_ns(void);  // monotonic_raw or monotonic
uint64_t get_monotonic_time_in_ns(void); // NTP-disciplined

// time at startup for debugging timing
// extern uint64_t ns_time_at_startup, ns_time_at_last_debug_message;

// this is for reading an unsigned 32 bit number, such as an RTP timestamp

uint32_t uatoi(const char *nptr);

extern shairport_cfg config;
extern config_t config_file_stuff;
extern int type_of_exit_cleanup; // normal, emergency, dbus requested...

extern uint64_t minimum_dac_queue_size;

int config_lookup_non_empty_string(const config_t *cfg, const char *path, const char **value);
int config_set_lookup_bool(config_t *cfg, char *where, int *dst);
int check_string_or_list_setting(config_setting_t *setting, const char *item);
int check_int_or_list_setting(config_setting_t *setting, const int item);

unsigned int config_get_string_settings_as_string_array(config_setting_t *setting,
                                                        const char ***result);
unsigned int config_get_int_settings_as_int_array(config_setting_t *setting, int **result);

void command_start(void);
void command_stop(void);
void command_execute(const char *command, const char *extra_argument, const int block);
void command_set_volume(double volume);

int mkpath(const char *path, mode_t mode);

void sps_shutdown(type_of_exit_type shutdown_type); // TOE_normal, TOE_emergency, TOE_dbus

extern sigset_t pselect_sigset;

extern pthread_mutex_t the_conn_lock;

#define conn_lock(arg)                                                                             \
  pthread_mutex_lock(&the_conn_lock);                                                              \
  arg;                                                                                             \
  pthread_mutex_unlock(&the_conn_lock);

// wait for the specified time in microseconds -- it checks every 20 milliseconds
// int sps_pthread_mutex_timedlock(pthread_mutex_t *mutex, useconds_t dally_time,
//                                 const char *debugmessage, int debuglevel);
// wait for the specified time, checking every 20 milliseconds, and block if it can't acquire the
// lock
int _debug_mutex_lock(pthread_mutex_t *mutex, useconds_t dally_time, const char *mutexName,
                      const char *filename, const int line, int debuglevel);

#define debug_mutex_lock(mu, t, d) _debug_mutex_lock(mu, t, #mu, __FILE__, __LINE__, d)

int _debug_mutex_unlock(pthread_mutex_t *mutex, const char *mutexName, const char *filename,
                        const int line, int debuglevel);

#define debug_mutex_unlock(mu, d) _debug_mutex_unlock(mu, #mu, __FILE__, __LINE__, d)

void pthread_cleanup_debug_mutex_unlock(void *arg);

#define pthread_cleanup_debug_mutex_lock(mu, t, d)                                                 \
  if (_debug_mutex_lock(mu, t, #mu, __FILE__, __LINE__, d) == 0)                                   \
  pthread_cleanup_push(pthread_cleanup_debug_mutex_unlock, (void *)mu)

#define config_lock                                                                                \
  if (pthread_mutex_trylock(&config.lock) != 0) {                                                  \
    debug(1, "config_lock: cannot acquire config.lock");                                           \
  }

#define config_unlock pthread_mutex_unlock(&config.lock)

int do_pthread_setname(pthread_t *restrict thread, const char *format, ...);

int named_pthread_create(pthread_t *restrict thread, const pthread_attr_t *restrict attr,
                         void *(*start_routine)(void *), void *restrict arg, const char *format,
                         ...);
int named_pthread_create_with_priority(pthread_t *thread, int priority,
                                       void *(*start_routine)(void *), void *arg,
                                       const char *format, ...);

extern pthread_mutex_t r64_mutex;

#define r64_lock pthread_mutex_lock(&r64_mutex)

#define r64_unlock pthread_mutex_unlock(&r64_mutex)

char *get_version_string(); // mallocs a string space -- remember to free it afterwards

int64_t generate_zero_frames(char *outp, size_t number_of_frames, int with_dither,
                             int64_t random_number_in, uint32_t encoded_output_format);

void malloc_cleanup(void *arg);

int string_update_with_size(char **str, int *flag, char *s, size_t len);

// from https://stackoverflow.com/questions/13663617/memdup-function-in-c, with thanks
void *memdup(const void *mem, size_t size);

int bind_socket_and_port(int type, int ip_family, const char *self_ip_address, uint32_t scope_id,
                         uint16_t *port, int *sock);

uint16_t bind_UDP_port(int ip_family, const char *self_ip_address, uint32_t scope_id, int *sock);

void socket_cleanup(void *arg);
void mutex_unlock(void *arg);
void rwlock_unlock(void *arg);
void mutex_cleanup(void *arg);
void cv_cleanup(void *arg);
void thread_cleanup(void *arg);
#ifdef CONFIG_AIRPLAY_2
void plist_cleanup(void *arg);
#endif

char *debug_malloc_hex_cstring(void *packet, size_t nread);

// from https://stackoverflow.com/questions/13663617/memdup-function-in-c, with thanks
// allocates memory and copies the content to it
// analogous to strndup;
void *memdup(const void *mem, size_t size);

int get_device_id(uint8_t *id, int int_length);

char *bnprintf(char *buffer, ssize_t max_bytes, const char *format, ...);

#ifdef CONFIG_CONVOLUTION

/* Parse comma-separated filenames with optional quotes from the input string
 * Returns array of ir_file_info_t structs (caller must free both array and filenames)
 * count is set to number of filenames found
 * Returns NULL on error
 */
ir_file_info_t *parse_ir_filenames(const char *input, unsigned int *file_count);
// Access: files[i].filename, files[i].rate, files[i].evaluation

/* Do a quick sanity check on the files -- see if they can be opened as sound files */
void sanity_check_ir_files(const int option_print_level, ir_file_info_t *files, unsigned int count);

/* Free the array returned by parse_filenames */
void free_ir_filenames(ir_file_info_t *files, unsigned int file_count);

#endif

#ifdef CONFIG_USE_GIT_VERSION_STRING
extern char git_version_string[];
#endif

#endif // _COMMON_H

#ifdef __cplusplus
}
#endif
