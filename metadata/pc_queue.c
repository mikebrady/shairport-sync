/*
 * Producer--consumer queue and access methods.
 * This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2017--2026
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "pc_queue.h"

#include <stdlib.h>
#include <string.h>
#include <errno.h>

void pc_queue_init(pc_queue *the_queue, char *items, size_t item_size, uint32_t number_of_items,
                   const char *name) {
  if (name)
    debug(4, "Creating metadata queue \"%s\".", name);
  else
    debug(1, "Creating an unnamed metadata queue.");
  pthread_mutex_init(&the_queue->pc_queue_lock, NULL);
  pthread_cond_init(&the_queue->pc_queue_item_added_signal, NULL);
  pthread_cond_init(&the_queue->pc_queue_item_removed_signal, NULL);
  the_queue->item_size = item_size;
  the_queue->items = items;
  the_queue->count = 0;
  the_queue->capacity = number_of_items;
  the_queue->toq = 0;
  the_queue->eoq = 0;
  if (name == NULL)
    the_queue->name = NULL;
  else
    the_queue->name = strdup(name);
}

void pc_queue_delete(pc_queue *the_queue) {
  if (the_queue->name)
    debug(2, "Deleting metadata queue \"%s\".", the_queue->name);
  else
    debug(1, "Deleting an unnamed metadata queue.");
  if (the_queue->name != NULL)
    free(the_queue->name);
  // debug(2, "destroying pc_queue_item_removed_signal");
  pthread_cond_destroy(&the_queue->pc_queue_item_removed_signal);
  // debug(2, "destroying pc_queue_item_added_signal");
  pthread_cond_destroy(&the_queue->pc_queue_item_added_signal);
  // debug(2, "destroying pc_queue_lock");
  pthread_mutex_destroy(&the_queue->pc_queue_lock);
  // debug(2, "destroying signals and locks done");
}

void pc_queue_cleanup_handler(void *arg) {
  // debug(1, "pc_queue_cleanup_handler called.");
  pc_queue *the_queue = (pc_queue *)arg;
  int rc = pthread_mutex_unlock(&the_queue->pc_queue_lock);
  if (rc)
    debug(1, "Error unlocking for pc_queue_add_item or pc_queue_get_item.");
}

int pc_queue_add_item(pc_queue *the_queue, const void *the_stuff, int block) {
  int response = 0;
  int rc;
  if (the_queue) {
    if (block == 0) {
      rc = debug_mutex_lock(&the_queue->pc_queue_lock, 10000, 4);
      if (rc == EBUSY)
        return EBUSY;
    } else
      rc = debug_mutex_lock(&the_queue->pc_queue_lock, 50000, 4);
    if (rc)
      debug(1, "Error %d (\"%s\") locking for pc_queue_add_item. Block is %d.", rc, strerror(rc),
            block);
    pthread_cleanup_push(pc_queue_cleanup_handler, (void *)the_queue);
    // leave this out if you want this to return if the queue is already full
    // irrespective of the block flag.
    /*
                while (the_queue->count == the_queue->capacity) {
                        rc = pthread_cond_wait(&the_queue->pc_queue_item_removed_signal,
       &the_queue->pc_queue_lock); if (rc) debug(1, "Error waiting for item to be removed");
                }
                */
    if (the_queue->count < the_queue->capacity) {
      uint32_t i = the_queue->eoq;
      void *p = the_queue->items + the_queue->item_size * i;
      //    void * p = &the_queue->qbase + the_queue->item_size*the_queue->eoq;
      memcpy(p, the_stuff, the_queue->item_size);

      // update the pointer
      i++;
      if (i == the_queue->capacity)
        // fold pointer if necessary
        i = 0;
      the_queue->eoq = i;
      the_queue->count++;
      // debug(2,"metadata queue+ \"%s\" %d/%d.", the_queue->name, the_queue->count,
      // the_queue->capacity);
      if (the_queue->count == the_queue->capacity)
        debug(3, "metadata queue \"%s\": is now full with %d items in it!", the_queue->name,
              the_queue->count);
      rc = pthread_cond_signal(&the_queue->pc_queue_item_added_signal);
      if (rc)
        debug(1, "metadata queue \"%s\": error signalling after pc_queue_add_item",
              the_queue->name);
    } else {
      response = EWOULDBLOCK; // a bit arbitrary, this.
      debug(3,
            "metadata queue \"%s\": is already full with %d items in it. Not adding this item to "
            "the queue.",
            the_queue->name, the_queue->count);
    }
    pthread_cleanup_pop(1); // unlock the queue lock.
  } else {
    debug(1, "Adding an item to a NULL queue");
  }
  return response;
}

int pc_queue_get_item(pc_queue *the_queue, void *the_stuff) {
  int rc;
  if (the_queue) {
    rc = debug_mutex_lock(&the_queue->pc_queue_lock, 50000, 4);
    if (rc)
      debug(1, "metadata queue \"%s\": error locking for pc_queue_get_item", the_queue->name);
    pthread_cleanup_push(pc_queue_cleanup_handler, (void *)the_queue);
    while (the_queue->count == 0) {
      rc = pthread_cond_wait(&the_queue->pc_queue_item_added_signal, &the_queue->pc_queue_lock);
      if (rc)
        debug(1, "metadata queue \"%s\": error waiting for item to be added", the_queue->name);
    }
    uint32_t i = the_queue->toq;
    //    void * p = &the_queue->qbase + the_queue->item_size*the_queue->toq;
    void *p = the_queue->items + the_queue->item_size * i;
    memcpy(the_stuff, p, the_queue->item_size);

    // update the pointer
    i++;
    if (i == the_queue->capacity)
      // fold pointer if necessary
      i = 0;
    the_queue->toq = i;
    the_queue->count--;
    debug(4, "metadata queue- \"%s\" %d/%d.", the_queue->name, the_queue->count,
          the_queue->capacity);
    rc = pthread_cond_signal(&the_queue->pc_queue_item_removed_signal);
    if (rc)
      debug(1, "metadata queue \"%s\": error signalling after pc_queue_get_item", the_queue->name);
    pthread_cleanup_pop(1); // unlock the queue lock.
  } else {
    debug(1, "Removing an item from a NULL queue");
  }
  return 0;
}

