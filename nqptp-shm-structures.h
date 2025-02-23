/*
 * This file is part of the nqptp distribution (https://github.com/mikebrady/nqptp).
 * Copyright (c) 2021--2025 Mike Brady.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 *
 * Commercial licensing is also available.
 */

#ifndef NQPTP_SHM_STRUCTURES_H
#define NQPTP_SHM_STRUCTURES_H

#define NQPTP_INTERFACE_NAME "/nqptp"

#define NQPTP_SHM_STRUCTURES_VERSION 10
#define NQPTP_CONTROL_PORT 9000

// The control port expects a UDP packet with the first character being a command letter
// and the rest being any arguments, the whole not to exceed 4096 characters.
// The "T" command,  must followed by nothing or by
// a space and a space-delimited list of IPv4 or IPv6 numbers.
// The IPs, if provided, will become the new list of timing peers, replacing any
// previous list. The first IP number is the clock that NQPTP will listen to.
// The remaining IP address are the addresses of all the timing peers. The timing peers
// are not used in this version of NQPTP.
// If no timing list is provided, the existing timing list is deleted.
// The "B" command is a message that the client -- which generates the clock --
// is about to start playing.
// NQPTP uses it to determine that the clock is active and will not sleep.
// The "E" command signifies that the client has stopped playing and that
// the clock may shortly sleep.
// The "P" command signifies that SPS has paused play (buffered audio only).
// The clock seems to stay running in this state.

// When the clock is active, it is assumed that any decreases in the offset
// between the local and remote clocks are due to delays in the network.
// NQPTP smooths the offset by clamping any decreases to a small value.
// In this way, it can follow clock drift but ignore network delays.

// When the clock is inactive, it can stop running. This causes the offset to decrease.
// NQPTP clock smoothing would treat this as a network delay, causing true sync to be lost.
// To avoid this, when the clock goes from inactive to active,
// NQPTP resets clock smoothing to the new offset.

#include <inttypes.h>
#include <pthread.h>

typedef struct {
  uint64_t master_clock_id;             // the current master clock
  uint64_t local_time;                  // the time when the offset was calculated
  uint64_t local_to_master_time_offset; // add this to the local time to get master clock time
  uint64_t master_clock_start_time;     // this is when the master clock became master
} shm_structure_set;

// The actual interface comprises a shared memory region of type struct shm_structure.
// This comprises two records of type shm_structure_set.
// The secondary record is written strictly after all writes to the main record are
// complete. This is ensured using the __sync_synchronize() construct.
// The reader should ensure that both copies match for a read to be valid.
// For safety, the secondary record should be read strictly after the first.

struct shm_structure {
  uint16_t version; // check this is equal to NQPTP_SHM_STRUCTURES_VERSION
  shm_structure_set main;
  shm_structure_set secondary;
};

#endif
