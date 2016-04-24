#ifndef _COMMON_H
#define _COMMON_H

#include <stdint.h>
#include <sys/socket.h>
#include <libconfig.h>

#include "config.h"
#include "audio.h"
#include "mdns.h"

#if defined(__APPLE__) && defined(__MACH__)
/* Apple OSX and iOS (Darwin). ------------------------------ */
#include <TargetConditionals.h>
#if TARGET_OS_MAC == 1
/* OSX */
#define COMPILE_FOR_OSX 1
#endif
#endif

#if defined(__linux__) || defined(__FreeBSD__) || defined(__CYGWIN__)
/* Linux and FreeBSD */
#define COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN 1
#endif

// struct sockaddr_in6 is bigger than struct sockaddr. derp
#ifdef AF_INET6
#define SOCKADDR struct sockaddr_storage
#define SAFAMILY ss_family
#else
#define SOCKADDR struct sockaddr
#define SAFAMILY sa_family
#endif

enum endian_type {
  SS_LITTLE_ENDIAN = 0,
  SS_PDP_ENDIAN,
  SS_BIG_ENDIAN,
} endian_type;

enum stuffing_type {
  ST_basic = 0,
  ST_soxr,
} type;

typedef struct {
  config_t *cfg;
  char *password;
  char *apname;
#ifdef CONFIG_METADATA
  int metadata_enabled;
  char *metadata_pipename;
  int get_coverart;
#endif
  uint8_t hw_addr[6];
  int port;
  int udp_port_base;
  int udp_port_range;
  int ignore_volume_control;
  int no_sync; // disable synchronisation, even if it's available
  int resyncthreshold; // if it get's out of whack my more than this, resync. Zero means never
                       // resync.
  int allow_session_interruption;
  int timeout; // while in play mode, exit if no packets of audio come in for more than this number
               // of seconds . Zero means never exit.
  int dont_check_timeout; // this is used to maintain backward compatability with the old -t option
                          // behaviour; only set by -t 0, cleared by everything else
  char *output_name;
  audio_output *output;
  char *mdns_name;
  mdns_backend *mdns;
  int buffer_start_fill;
  int32_t latency;
  int32_t userSuppliedLatency; // overrides all other latencies -- use with caution
  int32_t iTunesLatency;       // supplied with --iTunesLatency option
  int32_t AirPlayLatency; // supplied with --AirPlayLatency option
  int32_t ForkedDaapdLatency; // supplied with --ForkedDaapdLatency option
  int daemonise;
  int statistics_requested,use_negotiated_latencies;
  int mono;
  char *cmd_start, *cmd_stop;
  int cmd_blocking;
  int tolerance; // allow this much drift before attempting to correct it
  enum stuffing_type packet_stuffing;
  char *pidfile;
  // char *logfile;
  // char *errfile;
  char *configfile;
  char *regtype; // The regtype is the service type followed by the protocol, separated by a dot, by default “_raop._tcp.”.
  long audio_backend_buffer_desired_length; // this will be the desired number of frames in the
                                            // audio backend buffer -- the DAC buffer for ALSA
  long audio_backend_latency_offset; // this will be the offset to compensate for any fixed latency
  uint32_t volume_range_db; // the range, in dB, from max dB to min dB. Zero means use the mixer's native range.
                                     // there might be in the audio
} shairport_cfg;

// true if Shairport Sync is supposed to be sending output to the output device, false otherwise

int get_requested_connection_state_to_output();

void set_requested_connection_state_to_output(int v);

ssize_t non_blocking_write(int fd, const void *buf, size_t count); // used in a few places

int debuglev;
void die(char *format, ...);
void warn(char *format, ...);
void inform(char *format, ...);
void debug(int level, char *format, ...);

uint8_t *base64_dec(char *input, int *outlen);
char *base64_enc(uint8_t *input, int length);

#define RSA_MODE_AUTH (0)
#define RSA_MODE_KEY (1)
uint8_t *rsa_apply(uint8_t *input, int inlen, int *outlen, int mode);

// given a volume (0 to -30) and high and low attenuations in dB*100 (e.g. 0 to -6000 for 0 to -60
// dB), return an attenuation depending on the transfer function
double vol2attn(double vol, long max_db, long min_db);

// return a monolithic (always increasing) time in nanoseconds

uint64_t get_absolute_time_in_fp(void);

// this is for reading an unsigned 32 bit number, such as an RTP timestamp

long endianness;
uint32_t uatoi(const char *nptr);

shairport_cfg config;
config_t config_file_stuff;

int32_t buffer_occupancy; // allow it to be negative because seq_diff may be negative
int64_t session_corrections;
uint32_t play_segment_reference_frame;
uint64_t play_segment_reference_frame_remote_time;

void command_start(void);
void command_stop(void);

void shairport_shutdown();
// void shairport_startup_complete(void);

#endif // _COMMON_H
