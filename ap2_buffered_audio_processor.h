#ifndef _AP2_BUFFERED_AUDIO_PROCESSOR_H
#define _AP2_BUFFERED_AUDIO_PROCESSOR_H

#ifdef CONFIG_METADATA
#include "player.h"

/* The caller must hold conn->flush_mutex. Also invalidates in-flight accounting. */
void reset_buffered_aac_bitrate(rtsp_conn_info *conn);
#endif

void *rtp_buffered_audio_processor(void *arg);

#endif // _AP2_BUFFERED_AUDIO_PROCESSOR_H
