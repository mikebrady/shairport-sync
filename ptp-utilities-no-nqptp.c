/*
 * PTP utilities stub for AirPlay 2 without NQPTP (macOS only, experimental).
 * This provides a best-effort local clock so AirPlay 2 sessions can run without
 * PTP synchronisation (no multiroom sync, potential drift).
 */

#include "common.h"
#include "player.h"
#include "ptp-utilities.h"

#ifdef COMPILE_FOR_OSX
#ifdef CONFIG_AIRPLAY2_NO_NQPTP

#include <stdatomic.h>

static atomic_uint_fast64_t mastership_start_time_ns = 0;

void ptp_shm_interface_init() {
  // no-op
}

int ptp_shm_interface_open() {
  uint64_t expected = 0;
  uint64_t now = get_absolute_time_in_ns();
  atomic_compare_exchange_strong(&mastership_start_time_ns, &expected, now);
  return 0;
}

int ptp_shm_interface_close() { return 0; }

int ptp_get_clock_version() {
  // Pretend to match the expected SHM interface version so callers that use this
  // as a compatibility gate can proceed.
  return NQPTP_SHM_STRUCTURES_VERSION;
}

int ptp_get_clock_info(uint64_t *actual_clock_id, uint64_t *time_of_sample, uint64_t *raw_offset,
                       uint64_t *mastership_start_time) {
  uint64_t now = get_absolute_time_in_ns();
  uint64_t start = atomic_load(&mastership_start_time_ns);
  if (start == 0) {
    uint64_t expected = 0;
    atomic_compare_exchange_strong(&mastership_start_time_ns, &expected, now);
    start = atomic_load(&mastership_start_time_ns);
  }

  if (actual_clock_id)
    *actual_clock_id = 1; // stable non-zero clock id
  if (time_of_sample)
    *time_of_sample = now;
  if (raw_offset)
    *raw_offset = 0;
  if (mastership_start_time)
    *mastership_start_time = start;

  return clock_ok;
}

void ptp_send_control_message_string(const char *msg) {
  (void)msg;
  // no-op
}

#endif // CONFIG_AIRPLAY2_NO_NQPTP
#endif // COMPILE_FOR_OSX

