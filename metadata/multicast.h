#pragma once

#include "rtsp.h"
#include <inttypes.h>

void metadata_multicast_queue_init();
void metadata_multicast_queue_stop();
int send_metadata_to_multicast_queue(const uint32_t type, const uint32_t code, const char *data,
                                     const uint32_t length, rtsp_message *carrier, int block);