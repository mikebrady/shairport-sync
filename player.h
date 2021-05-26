#ifndef _PLAYER_H
#define _PLAYER_H

#include <arpa/inet.h>
#include <pthread.h>

#include "config.h"
#include "definitions.h"

#ifdef CONFIG_MBEDTLS
#include <mbedtls/aes.h>
#include <mbedtls/havege.h>
#endif

#ifdef CONFIG_POLARSSL
#include <polarssl/aes.h>
#include <polarssl/havege.h>
#endif

#ifdef CONFIG_OPENSSL
#include <openssl/aes.h>
#endif

#ifdef CONFIG_AIRPLAY_2
#include "pair_ap/pair.h"
#include "plist/plist.h"
#endif

#include "alac.h"
#include "audio.h"

#define time_ping_history_power_of_two 7
#define time_ping_history                                                                          \
  (1 << time_ping_history_power_of_two) // 2^7 is 128. At 1 per three seconds, approximately six
                                        // minutes of records
typedef struct time_ping_record {
  uint64_t dispersion;
  uint64_t local_time;
  uint64_t remote_time;
  int sequence_number;
  int chosen;
} time_ping_record;

// these are for reporting the status of the clock
typedef enum {
  clock_no_anchor_info,
  clock_ok,
  clock_service_unavailable,
  clock_access_error,
  clock_data_unavailable,
  clock_no_master,
  clock_version_mismatch,
  clock_not_synchronised,
  clock_not_valid,
  clock_not_ready,
} clock_status_t;

typedef uint16_t seq_t;

typedef struct audio_buffer_entry { // decoded audio packets
  uint8_t ready;
  uint8_t status; // flags
  uint16_t resend_request_number;
  signed short *data;
  seq_t sequence_number;
  uint64_t initialisation_time; // the time the packet was added or the time it was noticed the
                                // packet was missing
  uint64_t resend_time;         // time of last resend request or zero
  uint32_t given_timestamp;     // for debugging and checking
  int length;                   // the length of the decoded data
} abuf_t;

typedef struct stats { // statistics for running averages
  int64_t sync_error, correction, drift;
} stats_t;

// default buffer size
// This needs to be a power of 2 because of the way BUFIDX(seqno) works.
// 512 is the minimum for normal operation -- it gives 512*352/44100 or just over 4 seconds of
// buffers.
// For at least 10 seconds, you need to go to 2048.
// Resend requests will be spaced out evenly in the latency period, subject to a minimum interval of
// about 0.25 seconds.
// Each buffer occupies 352*4 bytes plus about, say, 64 bytes of overhead in various places, say
// roughly 1,500 bytes per buffer.
// Thus, 2048 buffers will occupy about 3 megabytes -- no big deal in a normal machine but maybe a
// problem in an embedded device.

#define BUFFER_FRAMES 1024

typedef enum {
  ast_unknown,
  ast_uncompressed, // L16/44100/2
  ast_apple_lossless,
} audio_stream_type;

typedef struct {
  int encrypted;
  uint8_t aesiv[16], aeskey[16];
  int32_t fmtp[12];
  audio_stream_type type;
} stream_cfg;

#ifdef CONFIG_AIRPLAY_2
typedef enum { ts_ntp, ts_ptp } timing_t;
typedef enum { ap_1, ap_2 } airplay_t;
typedef enum { realtime_stream, buffered_stream } airplay_stream_t;

typedef struct {
  uint8_t *data;
  size_t len;
  size_t size;
} ap2_buffer;

typedef struct {
  int is_encrypted;
  struct pair_setup_context *setup_ctx;
  struct pair_verify_context *verify_ctx;
  struct pair_cipher_context *cipher_ctx;

  ap2_buffer encrypted_buf;
  ap2_buffer plain_buf;
} ap2_pairing;

/*
typedef struct file_cipher_context {
  struct pair_cipher_context *cipher_context;
  int active; // can be created during a pair setup but not activated until next read
  int fd;
  void *input_plaintext_buffer;
  void *input_plaintext_buffer_toq;
  size_t input_plaintext_buffer_bytes_occupied;
} file_cipher_context;
*/
#endif

typedef struct {
  int connection_number;     // for debug ID purposes, nothing else...
  int resend_interval;       // this is really just for debugging
  char *UserAgent;           // free this on teardown
  int AirPlayVersion;        // zero if not an AirPlay session. Used to help calculate latency
  uint32_t latency;          // the actual latency used for this play session
  uint32_t minimum_latency;  // set if an a=min-latency: line appears in the ANNOUNCE message; zero
                             // otherwise
  uint32_t maximum_latency;  // set if an a=max-latency: line appears in the ANNOUNCE message; zero
                             // otherwise
  int software_mute_enabled; // if we don't have a real mute that we can use

  int fd;
  int authorized;   // set if a password is required and has been supplied
  char *auth_nonce; // the session nonce, if needed
  stream_cfg stream;
  SOCKADDR remote, local;
  volatile int stop;
  volatile int running;
  volatile uint64_t watchdog_bark_time;
  volatile int watchdog_barks;  // number of times the watchdog has timed out and done something
  int unfixable_error_reported; // set when an unfixable error command has been executed.

  time_t playstart;
  pthread_t thread, timer_requester, rtp_audio_thread, rtp_control_thread, rtp_timing_thread,
      player_watchdog_thread;

  // buffers to delete on exit
  signed short *tbuf;
  int32_t *sbuf;
  char *outbuf;

  // for generating running statistics...

  stats_t *statistics;

  // for holding the output rate information until printed out at the end of a session
  double frame_rate;
  int frame_rate_status;

  // for holding input rate information until printed out at the end of a session

  double input_frame_rate;
  int input_frame_rate_starting_point_is_valid;

  uint64_t frames_inward_measurement_start_time;
  uint32_t frames_inward_frames_received_at_measurement_start_time;

  uint64_t frames_inward_measurement_time;
  uint32_t frames_inward_frames_received_at_measurement_time;

  // other stuff...
  pthread_t *player_thread;
  abuf_t audio_buffer[BUFFER_FRAMES];
  unsigned int max_frames_per_packet, input_num_channels, input_bit_depth, input_rate;
  int input_bytes_per_frame, output_bytes_per_frame, output_sample_ratio;
  int max_frame_size_change;
  int64_t previous_random_number;
  alac_file *decoder_info;
  uint64_t packet_count;
  uint64_t packet_count_since_flush;
  int connection_state_to_output;
  uint64_t first_packet_time_to_play;
  int64_t time_since_play_started; // nanoseconds
                                   // stats
  uint64_t missing_packets, late_packets, too_late_packets, resend_requests;
  int decoder_in_use;
  // debug variables
  int32_t last_seqno_read;
  // mutexes and condition variables
  pthread_cond_t flowcontrol;
  pthread_mutex_t ab_mutex, flush_mutex, volume_control_mutex;

  int fix_volume;
  double initial_airplay_volume;
  int initial_airplay_volume_set;

  uint32_t timestamp_epoch, last_timestamp,
      maximum_timestamp_interval; // timestamp_epoch of zero means not initialised, could start at 2
                                  // or 1.
  int ab_buffering, ab_synced;
  int64_t first_packet_timestamp;
  int flush_requested;
  int flush_output_flushed; // true if the output device has been flushed.
  uint32_t flush_rtp_timestamp;
  uint64_t time_of_last_audio_packet;
  seq_t ab_read, ab_write;

#ifdef CONFIG_MBEDTLS
  mbedtls_aes_context dctx;
#endif

#ifdef CONFIG_POLARSSL
  aes_context dctx;
#endif

#ifdef CONFIG_OPENSSL
  AES_KEY aes;
#endif

  int amountStuffed;

  int32_t framesProcessedInThisEpoch;
  int32_t framesGeneratedInThisEpoch;
  int32_t correctionsRequestedInThisEpoch;
  int64_t syncErrorsInThisEpoch;

  // RTP stuff
  // only one RTP session can be active at a time.
  int rtp_running;
  uint64_t rtp_time_of_last_resend_request_error_ns;

  char client_ip_string[INET6_ADDRSTRLEN]; // the ip string pointing to the client
  uint16_t client_rtsp_port;
  char self_ip_string[INET6_ADDRSTRLEN]; // the ip string being used by this program -- it
  uint16_t self_rtsp_port;               // could be one of many, so we need to know it

  uint32_t self_scope_id;     // if it's an ipv6 connection, this will be its scope
  short connection_ip_family; // AF_INET / AF_INET6

  SOCKADDR rtp_client_control_socket; // a socket pointing to the control port of the client
  SOCKADDR rtp_client_timing_socket;  // a socket pointing to the timing port of the client
  int audio_socket;                   // our local [server] audio socket
  int control_socket;                 // our local [server] control socket
  int timing_socket;                  // local timing socket

  uint16_t remote_control_port;
  uint16_t remote_timing_port;
  uint16_t local_audio_port;
  uint16_t local_control_port;
  uint16_t local_timing_port;

  int64_t latency_delayed_timestamp; // this is for debugging only...

  // this is what connects an rtp timestamp to the remote time

  int anchor_remote_info_is_valid;
  int anchor_clock_is_new;
  uint64_t anchor_clock;
  uint64_t anchor_time; // this is the time according to the clock
  uint32_t anchor_rtptime;

  clock_status_t clock_status;

#ifdef CONFIG_AIRPLAY_2
  airplay_t airplay_type; // are we using AirPlay 1 or AirPlay 2 protocol on this connection?
  airplay_stream_t airplay_stream_type; // is it realtime audio or buffered audio...
  timing_t timing_type;                 // are we using NTP or PTP on this connection?

  pthread_t rtp_event_thread;
  pthread_t rtp_ap2_control_thread;
  pthread_t rtp_realtime_audio_thread;
  pthread_t rtp_buffered_audio_thread;

  int last_anchor_info_is_valid;
  uint32_t last_anchor_rtptime;
  uint64_t last_anchor_local_time;
  uint64_t last_anchor_time_of_update;

  ssize_t ap2_audio_buffer_size;
  int ap2_flush_requested;
  uint32_t ap2_flush_rtp_timestamp;
  uint32_t ap2_flush_sequence_number;
  int ap2_rate;         // protect with flush mutex, 0 means don't play, 1 means play
  int ap2_play_enabled; // protect with flush mutex

  ap2_pairing ap2_control_pairing;

  int event_socket;
  SOCKADDR ap2_remote_control_socket_addr; // a socket pointing to the control port of the client
  socklen_t ap2_remote_control_socket_addr_length;
  int ap2_control_socket;
  int realtime_audio_socket;
  int buffered_audio_socket;

  uint16_t local_event_port;
  uint16_t local_ap2_control_port;
  uint16_t local_realtime_audio_port;
  uint16_t local_buffered_audio_port;

  plist_t client_setup_plist;
  uint64_t audio_format;
  uint64_t compression;
  unsigned char *session_key; // needs to be free'd at the end
  uint64_t frames_packet;
  uint64_t type;
  uint64_t networkTimeTimelineID; // the clock ID used by the player

  char *ap2_timing_peer_list_message;

#endif

  // used as the initials values for calculating the rate at which the source thinks it's sending
  // frames
  uint32_t initial_reference_timestamp;
  uint64_t initial_reference_time;
  double remote_frame_rate;

  // the ratio of the following should give us the operating rate, nominally 44,100
  int64_t reference_to_previous_frame_difference;
  uint64_t reference_to_previous_time_difference;

  // debug variables
  int request_sent;

  int time_ping_count;
  struct time_ping_record time_pings[time_ping_history];

  uint64_t departure_time; // dangerous -- this assumes that there will never be two timing
                           // request in flight at the same time

  pthread_mutex_t reference_time_mutex;
  pthread_mutex_t watchdog_mutex;

  double local_to_remote_time_gradient; // if no drift, this would be exactly 1.0; likely it's
                                        // slightly above or  below.
  int local_to_remote_time_gradient_sample_count; // the number of samples used to calculate the
                                                  // gradient
  // add the following to the local time to get the remote time modulo 2^64
  uint64_t local_to_remote_time_difference; // used to switch between local and remote clocks
  uint64_t local_to_remote_time_difference_measurement_time; // when the above was calculated

  int last_stuff_request;

  // int64_t play_segment_reference_frame;
  // uint64_t play_segment_reference_frame_remote_time;

  int32_t buffer_occupancy; // allow it to be negative because seq_diff may be negative
  int64_t session_corrections;

  int play_number_after_flush;

  // remote control stuff. The port to which to send commands is not specified, so you have to use
  // mdns to find it.
  // at present, only avahi can do this

  char *dacp_id; // id of the client -- used to find the port to be used
  //  uint16_t dacp_port;          // port on the client to send remote control messages to, else
  //  zero
  char *dacp_active_remote;   // key to send to the remote controller
  void *dapo_private_storage; // this is used for compatibility, if dacp stuff isn't enabled.

  int enable_dither; // needed for filling silences before play actually starts
  uint64_t dac_buffer_queue_minimum_length;
} rtsp_conn_info;

extern pthread_mutex_t playing_conn_lock;

void get_audio_buffer_size_and_occupancy(unsigned int *size, unsigned int *occupancy,
                                         rtsp_conn_info *conn);

int32_t modulo_32_offset(uint32_t from, uint32_t to);

int player_play(rtsp_conn_info *conn);
int player_stop(rtsp_conn_info *conn);

void player_volume(double f, rtsp_conn_info *conn);
void player_volume_without_notification(double f, rtsp_conn_info *conn);
void player_flush(uint32_t timestamp, rtsp_conn_info *conn);
void player_full_flush(rtsp_conn_info *conn);
void player_put_packet(int original_format, seq_t seqno, uint32_t actual_timestamp, uint8_t *data,
                       int len, rtsp_conn_info *conn);
int64_t monotonic_timestamp(uint32_t timestamp,
                            rtsp_conn_info *conn); // add an epoch to the timestamp. The monotonic
// timestamp guaranteed to start between 2^32 2^33
// frames and continue up to 2^64 frames
// which is about 2*10^8 * 1,000 seconds at 384,000 frames per second -- about 2 trillion seconds.
// assumes, without checking, that successive timestamps in a series always span an interval of less
// than one minute.

#endif //_PLAYER_H
