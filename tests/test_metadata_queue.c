/* Exercise the existing queue and pipe worker with real anonymous pipes.
 * Configuration/logging/encoding providers are isolated; delivery code below
 * is included directly from production. No filesystem FIFO or service is used. */
#define _POSIX_C_SOURCE 200809L
/* Assertions perform test setup and checks, including in -DNDEBUG builds. */
#undef NDEBUG
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define _COMMON_H
#define _RTSP_H
#undef CONFIG_METADATA_HUB
#undef CONFIG_METADATA_MULTICAST
#undef CONFIG_MQTT
#define CONFIG_METADATA_PIPE 1
#undef debug
#undef warn
#undef die
#define debug(...) ((void)0)
#define warn(...) ((void)0)
#define die(...) abort()

typedef struct { int index_number; unsigned referenceCount; } rtsp_message;
static struct { int metadata_enabled; const char *metadata_pipename; } config = {1, "test"};
static pthread_mutex_t allocations_mutex = PTHREAD_MUTEX_INITIALIZER;
static void *allocations[1024];
static unsigned live_payloads, dropped_payloads;

static void *memdup(const void *data, size_t length) {
  void *copy = malloc(length);
  assert(copy);
  memcpy(copy, data, length);
  assert(!pthread_mutex_lock(&allocations_mutex));
  unsigned slot = 0;
  while (slot < 1024 && allocations[slot]) slot++;
  assert(slot < 1024);
  allocations[slot] = copy;
  live_payloads++;
  assert(!pthread_mutex_unlock(&allocations_mutex));
  return copy;
}
static void tracked_free(void *pointer) {
  assert(!pthread_mutex_lock(&allocations_mutex));
  for (unsigned i = 0; pointer && i < 1024; i++) {
    if (allocations[i] == pointer) {
      allocations[i] = NULL;
      live_payloads--;
      dropped_payloads++;
      break;
    }
  }
  assert(!pthread_mutex_unlock(&allocations_mutex));
  free(pointer);
}
static void msg_retain(rtsp_message *message) { message->referenceCount++; }
static void msg_free(rtsp_message **message) { (*message)->referenceCount--; }
/* No reader: exercise the worker's undeliverable-message branch. */
static int try_to_open_pipe_for_writing(const char *name) { (void)name; errno = ENXIO; return -1; }
static int named_pthread_create(pthread_t *thread, const pthread_attr_t *attr,
                                void *(*entry)(void *), void *arg, const char *name) {
  (void)name;
  return pthread_create(thread, attr, entry, arg);
}
/* Encoding is not under test; retain the provider's byte-count contract. */
static char *base64_encode_so(const unsigned char *data, size_t length, char *out, size_t *size) {
  (void)data;
  size_t needed = 4 * ((length + 2) / 3);
  assert(needed <= *size);
  memset(out, 'A', needed);
  *size = needed;
  return out;
}

#define free tracked_free
#include "../metadata/pc_queue.c"
#include "../metadata/core.c"
#include "../metadata/pipe.c"
#undef free

static unsigned queue_count(void) {
  assert(!pthread_mutex_lock(&metadata_queue.pc_queue_lock));
  unsigned count = metadata_queue.count;
  assert(!pthread_mutex_unlock(&metadata_queue.pc_queue_lock));
  return count;
}
static unsigned outstanding(void) {
  assert(!pthread_mutex_lock(&allocations_mutex));
  unsigned count = live_payloads;
  assert(!pthread_mutex_unlock(&allocations_mutex));
  return count;
}
static uint64_t now_ns(void) {
  struct timespec now;
  assert(!clock_gettime(CLOCK_MONOTONIC, &now));
  return (uint64_t)now.tv_sec * 1000000000 + now.tv_nsec;
}
static void pause_briefly(void) {
  const struct timespec delay = {0, 1000000};
  nanosleep(&delay, NULL);
}
static void initialise_queue(void) {
  pc_queue_init(&metadata_queue, (char *)metadata_queue_items, sizeof(metadata_package),
                 metadata_queue_size, "test");
}
static void absent_reader_and_disabled_metadata(void) {
  initialise_queue();
  assert(!send_ssnc_metadata('arst', "0/0/0", 5, 0));
  metadata_package package;
  pc_queue_get_item(&metadata_queue, &package);
  metadata_process(package.type, package.code, package.data, package.length);
  metadata_pack_cleanup_function(&package);
  assert(!queue_count() && !outstanding());
  config.metadata_enabled = 0;
  assert(!send_ssnc_metadata('arst', "0/0/0", 5, 0));
  assert(!queue_count() && !outstanding());
  config.metadata_enabled = 1;
  pc_queue_delete(&metadata_queue);
}
static void drain_until_empty(int reader) {
  uint64_t deadline = now_ns() + 5000000000;
  char bytes[8192];
  while (queue_count() || outstanding()) {
    assert(now_ns() < deadline);
    ssize_t result = read(reader, bytes, sizeof(bytes));
    if (result < 0) {
      assert(errno == EAGAIN || errno == EWOULDBLOCK);
      pause_briefly();
    }
  }
}
static void stalled_reader_saturation_and_recovery(void) {
  int pipes[2];
  assert(!pipe(pipes));
  int flags = fcntl(pipes[1], F_GETFL);
  assert(flags >= 0 && !fcntl(pipes[1], F_SETFL, flags | O_NONBLOCK));
  char padding[4096] = {0};
  while (write(pipes[1], padding, sizeof(padding)) > 0) {}
  assert(errno == EAGAIN || errno == EWOULDBLOCK);
  assert(!fcntl(pipes[1], F_SETFL, flags));
  fd = pipes[1];
  initialise_queue();
  assert(!pthread_create(&metadata_thread, NULL, metadata_thread_function, NULL));
  assert(!send_ssnc_metadata('arst', "0/0/0", 5, 0));
  uint64_t deadline = now_ns() + 5000000000;
  while (queue_count()) { assert(now_ns() < deadline); pause_briefly(); }
  assert(outstanding() == 1); // worker has an item and a full pipe prevents delivery

  for (unsigned i = 0; i < metadata_queue_size; i++)
    assert(!send_ssnc_metadata('abrt', "64000/96000/48000", 17, 0));
  assert(queue_count() == metadata_queue_size);
  unsigned freed_before = dropped_payloads;
  assert(send_ssnc_metadata('arst', "1/2/3", 5, 0) == EWOULDBLOCK);
  assert(queue_count() == metadata_queue_size);
  assert(outstanding() == metadata_queue_size + 1);
  assert(dropped_payloads == freed_before + 1);
  /* Include ordinary metadata: it shares the same capacity and drop policy. */
  assert(send_ssnc_metadata('sdsc', "ALAC/44100/S16_LE/2", 19, 1) == EWOULDBLOCK);
  assert(dropped_payloads == freed_before + 2);
  assert(outstanding() == metadata_queue_size + 1);

  flags = fcntl(pipes[0], F_GETFL);
  assert(flags >= 0 && !fcntl(pipes[0], F_SETFL, flags | O_NONBLOCK));
  drain_until_empty(pipes[0]);
  assert(!send_ssnc_metadata('arst', "4/5/6", 5, 0));
  drain_until_empty(pipes[0]);
  assert(!pthread_cancel(metadata_thread));
  assert(!pthread_join(metadata_thread, NULL));
  assert(!close(pipes[0])); // worker cleanup closes its write descriptor
  pc_queue_delete(&metadata_queue);
  assert(!outstanding());
}
int main(void) {
  absent_reader_and_disabled_metadata();
  stalled_reader_saturation_and_recovery();
  puts("Metadata delivery passed: absent/stalled reader, bounded saturation, cleanup and recovery.");
}
