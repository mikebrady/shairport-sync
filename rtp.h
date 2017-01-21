#ifndef _RTP_H
#define _RTP_H

#include <sys/socket.h>

#include "player.h"

typedef struct inter_threads_record {
  uint32_t please_stop;
} inter_threads_record;

void *rtp_audio_receiver(void *arg);
void *rtp_control_receiver(void *arg);
void *rtp_timing_receiver(void *arg);

void rtp_setup(SOCKADDR *local, SOCKADDR *remote, int controlport, int timingport, uint32_t active_remote,
               int *local_server_port, int *local_control_port, int *local_timing_port);
void rtp_shutdown(void);
void rtp_request_resend(seq_t first, uint32_t count);
void rtp_request_client_pause(void); // ask the client to pause

void get_reference_timestamp_stuff(uint32_t *timestamp, uint64_t *timestamp_time, uint64_t *remote_timestamp_time);
void clear_reference_timestamp(void);

static uint64_t local_to_remote_time_jitters;
static uint64_t local_to_remote_time_jitters_count;

#endif // _RTP_H
