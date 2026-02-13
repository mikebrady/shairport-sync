/*
 * Activity Monitor
 *
 * Contains code to run an activity flag and associated timer
 * A pthread implements a simple state machine with three states,
 * "idle", "active" and "timing out".
 *
 *
 * This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2019--2025
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

#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <sys/types.h>

#include "config.h"

#include "activity_monitor.h"
#include "common.h"
#include "rtsp.h"

#ifdef CONFIG_DBUS_INTERFACE
#include "dbus-service.h"
#endif

enum am_state state;
enum ps_state { ps_inactive, ps_active } player_state;

int activity_monitor_running = 0;

pthread_t activity_monitor_thread;
pthread_mutex_t activity_monitor_mutex;
pthread_cond_t activity_monitor_cv;

void going_active(int block) {
  // debug(1, "activity_monitor: state transitioning to \"active\" with%s blocking", block ? "" :
  // "out");
  if (config.cmd_active_start)
    command_execute(config.cmd_active_start, "", block);
#ifdef CONFIG_METADATA
  debug(2, "abeg");                       // active mode begin
  send_ssnc_metadata('abeg', NULL, 0, 1); // contains cancellation points
#endif

#ifdef CONFIG_DBUS_INTERFACE
  if (dbus_service_is_running())
    shairport_sync_set_active(SHAIRPORT_SYNC(shairportSyncSkeleton), TRUE);
#endif

  if (config.disable_standby_mode == disable_standby_auto) {
    config.keep_dac_busy = 1;
  }
}

void going_inactive(int block) {
  // debug(1, "activity_monitor: state transitioning to \"inactive\" with%s blocking", block ? "" :
  // "out");
  if (config.cmd_active_stop)
    command_execute(config.cmd_active_stop, "", block);
#ifdef CONFIG_METADATA
  debug(2, "aend");                       // active mode end
  send_ssnc_metadata('aend', NULL, 0, 1); // contains cancellation points
#endif

#ifdef CONFIG_DBUS_INTERFACE
  if (dbus_service_is_running())
    shairport_sync_set_active(SHAIRPORT_SYNC(shairportSyncSkeleton), FALSE);
#endif

  if (config.disable_standby_mode == disable_standby_auto) {
    config.keep_dac_busy = 0;
  }
}

void activity_monitor_signify_activity(int active) {
  // this could be pthread_cancelled and there is likely to be cancellation points in the
  // hooked-on procedures
  pthread_mutex_lock(&activity_monitor_mutex);
  player_state = active == 0 ? ps_inactive : ps_active;
  // Now, although we could simply let the state machine in the activity monitor thread
  // look after everything, we will change state here in two situations:
  // 1. If the state machine is am_inactive and the player is ps_active
  // we will change the state to am_active and execute the going_active() function.
  // 2. If the state machine is am_active and the player is ps_inactive and
  // the activity_idle_timeout is 0, then we will change the state to am_inactive and
  // execute the going_inactive() function.
  //
  // The reason for all this is that we might want to perform the attached scripts
  // and wait for them to complete before continuing. If they were performed in the
  // activity monitor thread, then we couldn't wait for them to complete.

  // So, if the active end procedure is on a timer, it will be executed when the
  // timeout occurs and the "blocking" status is ignored.

  if ((state == am_inactive) && (player_state == ps_active)) {
    state = am_active;
    pthread_mutex_unlock(&activity_monitor_mutex);
    going_active(config.cmd_blocking);
  } else if ((state == am_active) && (player_state == ps_inactive) &&
             (config.active_state_timeout == 0.0)) {
    state = am_inactive;
    pthread_mutex_unlock(&activity_monitor_mutex);
    going_inactive(config.cmd_blocking);
  } else {
    pthread_mutex_unlock(&activity_monitor_mutex);
  }
  // lock the mutex again to send a signal
  pthread_cleanup_debug_mutex_lock(&activity_monitor_mutex, 10000, 1);
  pthread_cond_signal(&activity_monitor_cv);
  pthread_cleanup_pop(1); // release the mutex
}

void activity_thread_cleanup_handler(__attribute__((unused)) void *arg) {
  debug(3, "activity_monitor: thread exit.");
  pthread_cond_destroy(&activity_monitor_cv);
  pthread_mutex_destroy(&activity_monitor_mutex);
}

void *activity_monitor_thread_code(void *arg) {
  int rc = pthread_mutex_init(&activity_monitor_mutex, NULL);
  if (rc)
    die("activity_monitor: error %d initialising activity_monitor_mutex.", rc);

  rc = pthread_cond_init(&activity_monitor_cv, NULL);
  if (rc)
    die("activity_monitor: error %d initialising activity_monitor_cv.", rc);
  pthread_cleanup_push(activity_thread_cleanup_handler, arg);

  uint64_t sec;
  uint64_t nsec;
  struct timespec time_for_wait;

  state = am_inactive;
  player_state = ps_inactive;

  pthread_mutex_lock(&activity_monitor_mutex);
  do {
    switch (state) {
    case am_inactive:
      debug(2, "am_state: am_inactive");
      while (player_state != ps_active)
        pthread_cond_wait(&activity_monitor_cv, &activity_monitor_mutex);
      // state = am_active; this is done by the activity_monitor_signify_activity(1) function
      debug(2, "am_state: am_active");
      break;
    case am_active:
      // debug(1,"am_state: am_active");
      while (player_state != ps_inactive)
        pthread_cond_wait(&activity_monitor_cv, &activity_monitor_mutex);

      // if it's not already am_inactive, the it should be beginning to time out...
      if (state != am_inactive) {
        state = am_timing_out;

        uint64_t time_to_wait_for_wakeup_ns = (uint64_t)(config.active_state_timeout * 1000000000);

#ifdef COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN_AND_OPENBSD
        uint64_t time_of_wakeup_ns = get_realtime_in_ns() + time_to_wait_for_wakeup_ns;
        sec = time_of_wakeup_ns / 1000000000;
        nsec = time_of_wakeup_ns % 1000000000;
        time_for_wait.tv_sec = sec;
        time_for_wait.tv_nsec = nsec;
#endif

#ifdef COMPILE_FOR_OSX
        sec = time_to_wait_for_wakeup_ns / 1000000000;
        nsec = time_to_wait_for_wakeup_ns % 1000000000;
        time_for_wait.tv_sec = sec;
        time_for_wait.tv_nsec = nsec;
#endif
      }
      break;
    case am_timing_out:
      rc = 0;
      while ((player_state != ps_active) && (rc != ETIMEDOUT)) {
#ifdef COMPILE_FOR_LINUX_AND_FREEBSD_AND_CYGWIN_AND_OPENBSD
        rc = pthread_cond_timedwait(&activity_monitor_cv, &activity_monitor_mutex,
                                    &time_for_wait); // this is a pthread cancellation point
#endif
#ifdef COMPILE_FOR_OSX
        rc = pthread_cond_timedwait_relative_np(&activity_monitor_cv, &activity_monitor_mutex,
                                                &time_for_wait);
#endif
      }
      if (player_state == ps_active)
        state = am_active; // player has gone active -- do nothing, because it's still active
      else if (rc == ETIMEDOUT) {
        state = am_inactive;
        pthread_mutex_unlock(&activity_monitor_mutex);
        going_inactive(0); // don't wait for completion -- it makes no sense
        pthread_mutex_lock(&activity_monitor_mutex);
      } else {
        // activity monitor was woken up in the state am_timing_out, but not by a timeout and player
        // is not in ps_active state
        debug(1,
              "activity monitor was woken up in the state am_timing_out, but didn't change state");
      }
      break;
    default:
      debug(1, "activity monitor in an illegal state!");
      state = am_inactive;
      break;
    }
  } while (1);
  pthread_mutex_unlock(&activity_monitor_mutex);
  pthread_cleanup_pop(0); // should never happen
  pthread_exit(NULL);
}

enum am_state activity_status() { return (state); }

void activity_monitor_start() {
  // debug(1,"activity_monitor_start");
  named_pthread_create(&activity_monitor_thread, NULL, activity_monitor_thread_code, NULL,
                       "activity_mon");
  activity_monitor_running = 1;
}

void activity_monitor_stop() {
  if (activity_monitor_running) {
    debug(2, "activity_monitor_stop begin. state: %d, player_state: %d.", state, player_state);
    if ((state == am_active) || (state == am_timing_out)) {
      going_inactive(config.cmd_blocking);
      state = am_inactive;
    }
    pthread_cancel(activity_monitor_thread);
    pthread_join(activity_monitor_thread, NULL);
    debug(2, "activity_monitor_stop complete");
  }
}
