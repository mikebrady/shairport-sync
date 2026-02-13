#ifndef _PLAYER_H
#define _PLAYER_H

#include <arpa/inet.h>
#include <pthread.h>

#include "config.h"
#include "definitions.h"

#ifdef CONFIG_MBEDTLS
#include <mbedtls/aes.h>
#endif

#ifdef CONFIG_POLARSSL
#include <polarssl/aes.h>
#include <polarssl/havege.h>
#endif

#ifdef CONFIG_AIRPLAY_2
#define MAX_DEFERRED_FLUSH_REQUESTS 10
#include "pair_ap/pair.h"
#include <plist/plist.h>
#endif

#ifdef CONFIG_FFMPEG
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libswresample/swresample.h>
#endif

#include "alac.h"
#include "audio.h"

// clang-format off

/*
__________________________________________________________________________________________________________________________________
* ALAC Specific Info (24 bytes) (mandatory)
__________________________________________________________________________________________________________________________________

The Apple Lossless codec stores specific information about the encoded stream in the ALACSpecificConfig. This
info is vended by the encoder and is used to setup the decoder for a given encoded bitstream. 

When read from and written to a file, the fields of this struct must be in big-endian order. 
When vended by the encoder (and received by the decoder) the struct values will be in big-endian order.

    struct	ALACSpecificConfig (defined in ALACAudioTypes.h)
    abstract   	This struct is used to describe codec provided information about the encoded Apple Lossless bitstream. 
		It must accompany the encoded stream in the containing audio file and be provided to the decoder.

    field      	frameLength 		uint32_t	indicating the frames per packet when no explicit frames per packet setting is 
							present in the packet header. The encoder frames per packet can be explicitly set 
							but for maximum compatibility, the default encoder setting of 4096 should be used.

    field      	compatibleVersion 	uint8_t 	indicating compatible version, 
							value must be set to 0

    field      	bitDepth 		uint8_t 	describes the bit depth of the source PCM data (maximum value = 32)

    field      	pb 			uint8_t 	currently unused tuning parameter. 
						 	value should be set to 40

    field      	mb 			uint8_t 	currently unused tuning parameter. 
						 	value should be set to 10

    field      	kb			uint8_t 	currently unused tuning parameter. 
						 	value should be set to 14

    field      	numChannels 		uint8_t 	describes the channel count (1 = mono, 2 = stereo, etc...)
							when channel layout info is not provided in the 'magic cookie', a channel count > 2
							describes a set of discreet channels with no specific ordering

    field      	maxRun			uint16_t 	currently unused. 
   						  	value should be set to 255

    field      	maxFrameBytes 		uint32_t 	the maximum size of an Apple Lossless packet within the encoded stream. 
						  	value of 0 indicates unknown

    field      	avgBitRate 		uint32_t 	the average bit rate in bits per second of the Apple Lossless stream. 
						  	value of 0 indicates unknown

    field      	sampleRate 		uint32_t 	sample rate of the encoded stream
 */

// clang-format on

typedef struct __attribute__((__packed__)) ALACSpecificConfig {
  uint32_t frameLength;
  uint8_t compatibleVersion;
  uint8_t bitDepth;
  uint8_t pb;
  uint8_t mb;
  uint8_t kb;
  uint8_t numChannels;
  uint16_t maxRun;
  uint32_t maxFrameBytes;
  uint32_t avgBitRate;
  uint32_t sampleRate;

} ALACSpecificConfig;

// everything in here is big-endian, i.e. network byte order
typedef struct __attribute__((__packed__)) alac_ffmpeg_magic_cookie {
  uint32_t cookie_size;    // 36 bytes
  uint32_t cookie_tag;     // 'alac'
  uint32_t cookie_version; // 0
  ALACSpecificConfig alac_config;
} alac_ffmpeg_magic_cookie;

#define time_ping_history_power_of_two 7
// this must now be zero, otherwise bad things will happen
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

// these are the values coming in on a buffered audio RTP packet's SSRC field
// the apparent significances are as indicated.
// Dolby Atmos seems to be 7P1
typedef enum {
  SSRC_NONE = 0,
  ALAC_44100_S16_2 = 0x0000FACE, // this is made up
  ALAC_48000_S24_2 = 0x15000000,
  AAC_44100_F24_2 = 0x16000000,
  AAC_48000_F24_2 = 0x17000000,
  AAC_48000_F24_5P1 = 0x27000000,
  AAC_48000_F24_7P1 = 0x28000000,
} ssrc_t;

typedef struct audio_buffer_entry { // decoded audio packets
  uint8_t ready;
  uint8_t status; // flags
  uint16_t resend_request_number;
  signed short *data;
  seq_t sequence_number;
  uint64_t initialisation_time; // the time the packet was added or the time it was noticed the
                                // packet was missing
  uint64_t resend_time;         // time of last resend request or zero
  uint32_t timestamp;           // for timing
  int32_t timestamp_gap;        // the difference between the timestamp and the expected timestamp.
  size_t length; // the length of the decoded data (or silence requested) in input frames
#ifdef CONFIG_FFMPEG
  ssrc_t ssrc;      // this is the type of this specific frame.
  AVFrame *avframe; // In AP2 and optionally in AP1, an AVFrame will be
  // used to carry audio rather than just a malloced memory space.
#endif
} abuf_t;

typedef struct stats { // statistics for running averages
  uint32_t timestamp;  // timestamp (denominated in input frames)
  size_t frames;       // number of audio frames in the block (denominated in output frames)
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

// maximum number of frames that can be added or removed from a packet_count
#define INTERPOLATION_LIMIT 20

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

// the following is used even when not built for AirPlay 2
typedef enum {
  unspecified_stream_category = 0,
  ptp_stream,
  ntp_stream,
  remote_control_stream,
  classic_airplay_stream
} airplay_stream_c; // "c" for category

#ifdef CONFIG_AIRPLAY_2
typedef enum { ts_ntp, ts_ptp } timing_t;
typedef enum { ap_1, ap_2 } airplay_t;
typedef enum { realtime_stream, buffered_stream } airplay_stream_t;

typedef struct {
  uint8_t *data;
  size_t length;
  size_t size;
} sized_buffer;

typedef struct {
  struct pair_cipher_context *cipher_ctx;
  sized_buffer encrypted_read_buffer;
  sized_buffer plaintext_read_buffer;
  int is_encrypted;
  char *description;
} pair_cipher_bundle; // cipher context and buffers

typedef struct {
  struct pair_setup_context *setup_ctx;
  struct pair_verify_context *verify_ctx;
  pair_cipher_bundle control_cipher_bundle;
  pair_cipher_bundle event_cipher_bundle;
  pair_cipher_bundle data_cipher_bundle;
  char *data_cipher_salt;
} ap2_pairing;

typedef struct {
  uint32_t inUse;  // record free or contains a current flush record
  uint32_t active; // set if blocks within the given range are being flushed.
  uint32_t flushFromTS;
  uint32_t flushFromSeq;
  uint32_t flushUntilTS;
  uint32_t flushUntilSeq;
} ap2_flush_request_t;

#endif

typedef struct {
  int connection_number;           // for debug ID purposes, nothing else...
  int is_playing;                  // set true by player_play, set false by player_stop
  unsigned int sync_samples_index; // for estimating the gap between the highest and lowest timing
                                   // error over the past n samples
  unsigned int sync_samples_count; // the array of samples is defined locally
  int at_least_one_frame_seen_this_session; // set when the first frame is output
  int resend_interval;                      // this is really just for debugging
  char *UserAgent;                          // free this on teardown
  int AirPlayVersion; // zero if not an AirPlay session. Used to help calculate latency
  int latency_warning_issued;
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

  uint64_t playstart;
  uint64_t connection_start_time; // the time the device is selected, which could be a long time
                                  // before a play
  pthread_t thread, timer_requester, rtp_audio_thread, rtp_control_thread, rtp_timing_thread;

  // buffers to delete on exit
  int32_t *tbuf;
  char *outbuf;

  // for generating running statistics...

  // stats_t *statistics;

  // for holding the output rate information until printed out at the end of a session
  double raw_frame_rate;
  double corrected_frame_rate;
  int frame_rate_valid;

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
  unsigned int frames_per_packet, input_num_channels, input_bit_depth, input_effective_bit_depth,
      input_rate;
  int input_bytes_per_frame;
  unsigned int output_sample_ratio;
  unsigned int output_bit_depth;
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
  int last_seqno_valid;
  seq_t last_seqno_read;
  // mutexes and condition variables
  pthread_cond_t flowcontrol;
  pthread_mutex_t ab_mutex, flush_mutex, volume_control_mutex, player_create_delete_mutex;

  int fix_volume;
  double own_airplay_volume;
  int own_airplay_volume_set;

  int ab_buffering, ab_synced;
  uint32_t first_packet_timestamp;
  int flush_requested;
  int flush_output_flushed; // true if the output device has been flushed.
  uint32_t flush_rtp_timestamp;
  uint64_t time_of_last_audio_packet;
  seq_t ab_read, ab_write;

  int do_loudness; // if loudness is requested and there is no external mixer

#ifdef CONFIG_MBEDTLS
  mbedtls_aes_context dctx;
#endif

#ifdef CONFIG_POLARSSL
  aes_context dctx;
#endif

  int32_t framesProcessedInThisEpoch;
  int32_t framesGeneratedInThisEpoch;
  int32_t correctionsRequestedInThisEpoch;
  int64_t syncErrorsInThisEpoch;

  // RTP stuff
  // only one RTP session can be active at a time.
  int rtp_running;
  uint64_t rtp_time_of_last_resend_request_error_ns;

  char client_ip_string[INET6_ADDRSTRLEN]; // the ip string of the client
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

  int udp_clock_is_initialised;
  int udp_clock_sender_is_initialised;

  int anchor_remote_info_is_valid;

  // these can be modified if the master clock changes over time
  uint64_t anchor_clock;
  uint64_t anchor_time; // this is the time according to the clock
  uint32_t anchor_rtptime;

  // these are used to identify when the master clock becomes equal to the
  // actual anchor clock information, so it can be used to avoid accumulating errors
  uint64_t actual_anchor_clock;
  uint64_t actual_anchor_time;
  uint32_t actual_anchor_rtptime;

  clock_status_t clock_status;

  airplay_stream_c
      airplay_stream_category; // is it a remote control stream or a normal "full service" stream?
                               // (will be unspecified if not build for AirPlay 2)

#ifdef CONFIG_AIRPLAY_2
  char *airplay_gid; // UUID in the Bonjour advertisement -- if NULL, the group UUID is the same as
                     // the pi UUID
  airplay_t airplay_type; // are we using AirPlay 1 or AirPlay 2 protocol on this connection?
  airplay_stream_t airplay_stream_type; // is it realtime audio or buffered audio...
  timing_t timing_type;                 // are we using NTP or PTP on this connection?

  pthread_t *rtp_event_thread;
  pthread_t *rtp_data_thread;
  pthread_t rtp_ap2_control_thread;
  pthread_t rtp_realtime_audio_thread;
  pthread_t rtp_buffered_audio_thread;

  int ap2_event_receiver_exited;

  int last_anchor_info_is_valid;
  uint32_t last_anchor_rtptime;
  uint64_t last_anchor_local_time;
  uint64_t last_anchor_time_of_update;
  uint64_t last_anchor_validity_start_time;

  int ap2_immediate_flush_requested;
  uint32_t ap2_immediate_flush_until_rtp_timestamp;
  uint32_t ap2_immediate_flush_until_sequence_number;

  ap2_flush_request_t ap2_deferred_flush_requests[MAX_DEFERRED_FLUSH_REQUESTS];

  ssize_t ap2_audio_buffer_size;
  ssize_t ap2_audio_buffer_minimum_size;

  int ap2_rate;         // protect with flush mutex, 0 means don't play, 1 means play
  int ap2_play_enabled; // protect with flush mutex

  ap2_pairing ap2_pairing_context;
  struct pair_result *pair_setup_result; // need to keep the shared secret

  int event_socket;
  int data_socket;
  SOCKADDR ap2_remote_control_socket_addr; // a socket pointing to the control port of the client
  socklen_t ap2_remote_control_socket_addr_length;
  int ap2_control_socket;
  int realtime_audio_socket;
  int buffered_audio_socket;

  uint16_t local_data_port;
  uint16_t local_event_port;
  uint16_t local_ap2_control_port;
  uint16_t local_realtime_audio_port;
  uint16_t local_buffered_audio_port;

  uint64_t audio_format;
  uint64_t compression;
  unsigned char *session_key; // needs to be free'd at the end
  char *ap2_client_name;      // needs to be free'd at teardown phase 2
  uint64_t frames_packet;
  uint64_t type;                  // 96 (Realtime Audio), 103 (Buffered Audio), 130 (Remote Control)
  uint64_t networkTimeTimelineID; // the clock ID used by the player
  uint8_t groupContainsGroupLeader; // information coming from the SETUP
  uint64_t compressionType;
#endif

#ifdef CONFIG_FFMPEG
  ssrc_t incoming_ssrc;  // The SSRC of incoming packets. In AirPlay 2, the RTP SSRC seems to encode
                         // something about the contents of the packet -- Atmos/etc. We use it also
                         // even in AP1 as a code
  ssrc_t resampler_ssrc; // the SSRC of packets for which the software resampler has been set up.
  // normally it's the same as that of incoming packets, but if the encoding of incoming packets
  // changes dynamically and the decoding chain hasn't been reset, the resampler will have to deal
  // with queued AVFrames encoded according to the previous SSRC.
  const AVCodec *codec;
  AVCodecContext *codec_context;
  // the swr can't be used just after the incoming packet has been decoded as explained below

  // The reasons that resampling can not occur when the packet initially arrives are twofold.
  // Resampling requires input samples from before and after the resampling instant.
  // So, at the end of a block, since the subsequent samples aren't in the block, resampling
  // is deferred until the next block is loaded. From this, the two reasons follow:
  // First, the "next" block to be provided in player_put_packet is not guaranteed to be
  // the next block in sequence -- packets can arrive out of sequence in UDP transmission.
  // Second, not all the frames that should be generated for a block will be generated
  // by a call to swr_convert. The frames that can't be calculated will not be provided, and
  // will be held back and provided to the subsequent call.
  // Tht means that the first frame output by swr_convert will not in general,
  // not correspond to the first frame provided to it, throwing
  // timing calculations off.

  // In summary, we have to wait until (1) we have all the blocks in order,
  // and (2) we have to track the number of resampler output frames to
  // keep the correspondence between them and the input frames.

  // We can calculate the "deficit" between the number of frames that should be generated
  // versus the number of frames actually generated.

  // For example, converting 352 frames at 44,100 to 48,000 should result
  // in 352 * 48000 / 44100, or 383.129252 frames.

  // Say only 360 frames are actually produced, then the deficit is 23.129252.

  // If those 360 frames are sent to the output device, then the timing of the next
  // block of 352 frames will be ahead by 23.129252 frames at 48,000 fps -- about 0.48 ms.

  // We need to add the delay corresponding to the frames that should have been sent to
  // keep timing correct. I.e. when calculating the buffer delay at the start of the following
  // block, those 23.129252 frames the were not actually sent should be added to it.

  // The "deficit" can readily be kept up to date and can be always added to the
  // DAC buffer delay to exactly compensate for the

  SwrContext *swr; // this will do transcoding anf resampling, if necessary, just prior to output
  int ffmpeg_decoding_chain_initialised;
  int64_t resampler_output_channels;
  int resampler_output_bytes_per_sample;
  int64_t frames_retained_in_the_resampler; // swr will retain frames it hasn't finished processing
  // they'll come out before the frames corresponding to the start of next block passed to
  // swrconvert so we need to compensate for their absence in sync timing
  unsigned int output_channel_to_resampler_channel_map[8];
  unsigned int output_channel_map_size;
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
} rtsp_conn_info;

extern int statistics_row; // will be reset to zero when debug level changes or statistics enabled

void reset_buffer(rtsp_conn_info *conn);

size_t get_audio_buffer_occupancy(rtsp_conn_info *conn);

int32_t modulo_32_offset(uint32_t from, uint32_t to);

void ab_resync(rtsp_conn_info *conn);

int player_play(rtsp_conn_info *conn);
int player_stop(rtsp_conn_info *conn);

void player_volume(double f, rtsp_conn_info *conn);
void player_volume_without_notification(double f, rtsp_conn_info *conn);
void player_flush(uint32_t timestamp, rtsp_conn_info *conn);
// void player_full_flush(rtsp_conn_info *conn);

seq_t get_revised_seqno(rtsp_conn_info *conn, uint32_t timestamp);
void clear_buffers_from(rtsp_conn_info *conn, seq_t from_here);
uint32_t player_put_packet(uint32_t ssrc, seq_t seqno, uint32_t actual_timestamp, uint8_t *data,
                           size_t len, int mute, int32_t timestamp_gap, rtsp_conn_info *conn);
int64_t monotonic_timestamp(uint32_t timestamp,
                            rtsp_conn_info *conn); // add an epoch to the timestamp. The monotonic
// timestamp guaranteed to start between 2^32 2^33
// frames and continue up to 2^64 frames
// which is about 2*10^8 * 1,000 seconds at 384,000 frames per second -- about 2 trillion seconds.
// assumes, without checking, that successive timestamps in a series always span an interval of less
// than one minute.

double suggested_volume(rtsp_conn_info *conn); // volume suggested for the connection

const char *get_ssrc_name(ssrc_t ssrc);
size_t get_ssrc_block_length(ssrc_t ssrc);

const char *get_category_string(airplay_stream_c cat);

int ssrc_is_recognised(ssrc_t ssrc);
int ssrc_is_aac(ssrc_t ssrc); // used to decide if a mute might be needed (AAC only)
void prepare_decoding_chain(rtsp_conn_info *conn, ssrc_t ssrc); // also sets up timing stuff
void clear_decoding_chain(rtsp_conn_info *conn);                // tear down the decoding chain

#endif //_PLAYER_H
