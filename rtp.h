#ifndef _RTP_H
#define _RTP_H

#include <sys/socket.h>

#include "player.h"

void rtp_initialise(rtsp_conn_info *conn);
void rtp_terminate(rtsp_conn_info *conn);

void *rtp_audio_receiver(void *arg);
void *rtp_control_receiver(void *arg);
void *rtp_timing_receiver(void *arg);

void rtp_setup(SOCKADDR *local, SOCKADDR *remote, uint16_t controlport, uint16_t timingport,
               rtsp_conn_info *conn);
void rtp_request_resend(seq_t first, uint32_t count, rtsp_conn_info *conn);
void rtp_request_client_pause(rtsp_conn_info *conn); // ask the client to pause

void reset_anchor_info(rtsp_conn_info *conn);

int have_timestamp_timing_information(rtsp_conn_info *conn);

int frame_to_local_time(uint32_t timestamp, uint64_t *time, rtsp_conn_info *conn);
int local_time_to_frame(uint64_t time, uint32_t *frame, rtsp_conn_info *conn);

#ifdef CONFIG_AIRPLAY_2
int have_ptp_timing_information(rtsp_conn_info *conn);
int get_ptp_anchor_local_time_info(rtsp_conn_info *conn, uint32_t *anchorRTP,
                                   uint64_t *anchorLocalTime);
void *rtp_ap2_control_receiver(void *arg);
void *rtp_realtime_audio_receiver(void *arg);
void *rtp_ap2_timing_receiver(void *arg);
void *rtp_ap2_general_message_timing_receiver(void *arg);
void set_ptp_anchor_info(rtsp_conn_info *conn, uint64_t clock_id, uint32_t rtptime,
                         uint64_t networktime);
#endif

#endif // _RTP_H
