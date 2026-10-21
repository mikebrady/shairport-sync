#pragma once
#include "player.h"

int audio_bitrate_reset(audio_bitrate_meter *meter);
void report_player_counters(rtsp_conn_info *conn);
uint64_t input_bitrate_packet_generation(rtsp_conn_info *conn, uint32_t ssrc, uint32_t sequence,
                                         uint32_t timestamp, int32_t timestamp_gap);
void account_input_bitrate(rtsp_conn_info *conn, uint32_t ssrc, uint64_t bytes, uint32_t samples,
                           uint32_t sequence, uint32_t timestamp, uint64_t generation);
