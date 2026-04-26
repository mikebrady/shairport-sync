#pragma once

#include "common.h"
#include "config.h"
#include "../rtsp.h"
#include "pc_queue.h"
#include <pthread.h>

typedef struct {
  uint32_t type;
  uint32_t code;
  char *data;
  uint32_t length;
  rtsp_message *carrier;
} metadata_package;

void metadata_pack_cleanup_function(void *arg);

// initialise and completely delete the metadata stuff

void metadata_init(void);
void metadata_stop(void);


int send_metadata_to_queue(pc_queue *queue, const uint32_t type, const uint32_t code,
                           const char *data, const uint32_t length, rtsp_message *carrier,
                           int block);

// sends metadata out to the metadata system.

int send_metadata(const uint32_t type, const uint32_t code, const char *data, const uint32_t length,
                  rtsp_message *carrier, int block);

// It is sent with the type 'ssnc' the given code, data and length
// The handler at the other end must know what to do with the data
// e.g. if it's malloced, to free it, etc.
// nothing is done automatically
int send_ssnc_metadata(const uint32_t code, const char *data, const uint32_t length,
                       const int block);