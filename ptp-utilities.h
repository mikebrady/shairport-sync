#ifndef __PTP_UTILITIES_H
#define __PTP_UTILITIES_H

#include "config.h"
#include "nqptp-shm-structures.h"
#include <stdint.h>

int ptp_get_clock_info(uint64_t *actual_clock_id, uint64_t *time_of_sample, uint64_t *raw_offset,
                       uint64_t *mastership_start_time);

void ptp_send_control_message_string(const char *msg);

void ptp_shm_interface_init();
int ptp_shm_interface_open();
int ptp_get_clock_version();
int ptp_shm_interface_close();

#endif /* __PTP_UTILITIES_H */
