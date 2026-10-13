#pragma once

#include "metadata/hub.h"
#include "player.h"

// simple commands

// from rcsc_play to rcsc_rewind_stop, the encoded number corresponds
// to the command number used in the actual AP2 remote control command

// some of these are used only in mqtt.c

typedef enum {
  rcsc_play = 0,
  rcsc_pause,
  rcsc_play_pause,
  rcsc_stop,
  rcsc_next_item,
  rcsc_previous_item,
  rcsc_toggle_shuffle,
  rcsc_cycle_repeat,
  rcsc_fast_forward,
  rcsc_fast_forward_stop,
  rcsc_rewind,
  rcsc_rewind_stop,
  // these do not correspond to actual AP2 remote control command numbers...
  rcsc_volume_up,
  rcsc_volume_down,
  rcsc_not_a_command,
  rcsc_command,
  rcsc_disconnect,

// these are only implemented in the DACP client, not in AirPlay 2
#ifdef CONFIG_DACP_CLIENT
  rcsc_queue_next,
  rcsc_mute_toggle,
  rcsc_play_resume,
#endif
} simple_command_t;

int remote_set_airplay_volume(double volume);
int remote_set_integer_percent_volume(const int volume);

int remote_set_repeat_mode(repeat_status_type mode);
int remote_set_shuffle_mode(shuffle_status_type mode);

void remote_simple_command(simple_command_t command);

ssize_t ap2_event_send_dev_mule(unsigned int command_number);