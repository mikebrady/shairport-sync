
#pragma once

#include "common.h"
#include "config.h"
#include <pthread.h>

typedef struct {
  pthread_mutex_t pc_queue_lock;
  pthread_cond_t pc_queue_item_added_signal;
  pthread_cond_t pc_queue_item_removed_signal;
  char *name;
  size_t item_size;  // number of bytes in each item
  uint32_t count;    // number of items in the queue
  uint32_t capacity; // maximum number of items
  uint32_t toq;      // first item to take
  uint32_t eoq;      // free space at end of queue
  void *items;       // a pointer to where the items are actually stored
} pc_queue;          // producer-consumer queue

void pc_queue_init(pc_queue *the_queue, char *items, size_t item_size, uint32_t number_of_items,
                   const char *name);

void pc_queue_delete(pc_queue *the_queue);
int pc_queue_add_item(pc_queue *the_queue, const void *the_stuff, int block);
int pc_queue_get_item(pc_queue *the_queue, void *the_stuff);
