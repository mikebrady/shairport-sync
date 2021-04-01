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
  int unachievable_audio_backend_latency_offset_notified; // set when a latency warning is issued

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
  char self_ip_string[INET6_ADDRSTRLEN];   // the ip string being used by this program -- it
                                           // could be one of many, so we need to know it
  uint32_t self_scope_id;                  // if it's an ipv6 connection, this will be its scope
  short connection_ip_family;              // AF_INET / AF_INET6

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

  uint32_t reference_timestamp;
  uint64_t remote_reference_timestamp_time;

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

  int player_stop_set;

} rtsp_conn_info;

uint32_t modulo_32_offset(uint32_t from, uint32_t to);
uint64_t modulo_64_offset(uint64_t from, uint64_t to);

int player_play(rtsp_conn_info *conn);
int player_stop(rtsp_conn_info *conn);

void player_volume(double f, rtsp_conn_info *conn);
void player_volume_without_notification(double f, rtsp_conn_info *conn);
void player_flush(uint32_t timestamp, rtsp_conn_info *conn);
void player_put_packet(seq_t seqno, uint32_t actual_timestamp, uint8_t *data, int len,
                       rtsp_conn_info *conn);
int64_t monotonic_timestamp(uint32_t timestamp,
                            rtsp_conn_info *conn); // add an epoch to the timestamp. The monotonic
// timestamp guaranteed to start between 2^32 2^33
// frames and continue up to 2^64 frames
// which is about 2*10^8 * 1,000 seconds at 384,000 frames per second -- about 2 trillion seconds.
// assumes, without checking, that successive timestamps in a series always span an interval of less
// than one minute.

#endif //_PLAYER_H
