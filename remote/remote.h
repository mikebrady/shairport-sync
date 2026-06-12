#pragma once

#include "player.h"

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

void remote_set_airplay_volume(double volume);
void remote_volumeup();
void remote_volumedown();

void remote_simple_command(simple_command_t command);

void remote_player_stop(rtsp_conn_info *conn);

ssize_t ap2_event_send_dev_mule(unsigned int command_number);