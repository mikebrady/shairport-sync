#pragma once

#include "player.h"
#include "metadata/hub.h"

// simple commands
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
} simple_command_t;

int remote_set_airplay_volume(double volume);
int remote_set_integer_percent_volume(const int volume);
void remote_volumeup();
void remote_volumedown();
int remote_set_repeat_mode(repeat_status_type mode);
int remote_set_shuffle_mode(shuffle_status_type mode);
void remote_simple_command(simple_command_t command);

ssize_t ap2_event_send_dev_mule(unsigned int command_number);