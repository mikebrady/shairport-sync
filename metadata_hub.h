#pragma once
#include "common.h"
#include "config.h"
#include <pthread.h>

#define number_of_watchers 2

typedef enum {
  PS_NOT_AVAILABLE = 0,
  PS_STOPPED,
  PS_PAUSED,
  PS_PLAYING,
} play_status_type;

typedef enum {
  AM_INACTIVE = 0,
  AM_ACTIVE,
} active_state_type;

typedef enum {
  SS_NOT_AVAILABLE = 0,
  SS_OFF,
  SS_ON,
} shuffle_status_type;

typedef enum {
  RS_NOT_AVAILABLE = 0,
  RS_OFF,
  RS_ONE,
  RS_ALL,
} repeat_status_type;

int string_update(char **str, int *changed, char *s);
int int_update(int *receptacle, int *changed, int value);

struct metadata_bundle;

typedef void (*metadata_watcher)(struct metadata_bundle *argc, void *userdata);

typedef struct metadata_bundle {

  char *client_ip; // IP number used by the audio source (i.e. the "client"), which is also the DACP
                   // server
  int client_ip_changed;

  char *client_name; // the name of the client device, if available
  int client_name_changed;

  char *server_ip; // IP number used by Shairport Sync
  int server_ip_changed;

  char *stream_type; // Realtime or Buffered
  int stream_type_changed;

  char *source_format; // Format of incoming audio, e.g. AAC/44100/S16_LE/2
  int source_format_changed;

  char *output_format; // Format of outgoing audio, e.g. 44100/S32_LE/2 (always PCM)
  int output_format_changed;

  char *progress_string; // progress string, emitted by the source from time to time
  int progress_string_changed;

  char *frame_position_string; // frame position string emitted by SPS on request
  int frame_position_string_changed;

  char *first_frame_position_string; // first frame position string emitted by SPS on request
  int first_frame_position_string_changed;

  int player_thread_active; // true if a play thread is running
  int dacp_server_active;   // true if there's a reachable DACP server (assumed to be the Airplay
                            // client) ; false otherwise
  int advanced_dacp_server_active; // true if there's a reachable DACP server with iTunes
                                   // capabilitiues
                                   // ; false otherwise
  int dacp_server_has_been_active; // basically this is a delayed version of dacp_server_active,
  // used detect transitions between server activity being on or off
  // e.g. to reease metadata when a server goes inactive, but not if it's permanently
  // inactive.
  play_status_type play_status;
  shuffle_status_type shuffle_status;
  repeat_status_type repeat_status;

  // the following pertain to the track playing

  char *cover_art_pathname;
  int cover_art_pathname_changed;

  uint64_t item_id; // seems to be a track ID -- see itemid in DACP.c
  int item_id_changed;
  int item_id_is_valid;

  unsigned char
      item_composite_id[16]; // seems to be nowplaying 4 ids: dbid, plid, playlistItem, itemid
  int item_composite_id_changed;
  int item_composite_id_is_valid;

  int song_data_kind;
  int song_data_kind_changed;
  int song_data_kind_is_valid;

  char *track_name;
  int track_name_changed;

  char *artist_name;
  int artist_name_changed;

  char *album_artist_name;
  int album_artist_name_changed;

  char *album_name;
  int album_name_changed;

  char *genre;
  int genre_changed;

  char *comment;
  int comment_changed;

  char *composer;
  int composer_changed;

  char *file_kind;
  int file_kind_changed;

  char *song_description;
  int song_description_changed;

  char *song_album_artist;
  int song_album_artist_changed;

  char *sort_name;
  int sort_name_changed;

  char *sort_artist;
  int sort_artist_changed;

  char *sort_album;
  int sort_album_changed;

  char *sort_composer;
  int sort_composer_changed;

  uint32_t songtime_in_milliseconds;
  int songtime_in_milliseconds_changed;
  int songtime_in_milliseconds_is_valid;

  // end

  play_status_type
      player_state; // this is the state of the actual player itself, which can be a bit noisy.
  active_state_type active_state;

  int speaker_volume; // this is the actual speaker volume, allowing for the main volume and the
                      // speaker volume control
  double airplay_volume;

  metadata_watcher watchers[number_of_watchers]; // functions to call if the metadata is changed.
  void *watchers_data[number_of_watchers];       // their individual data

} metadata_bundle;

extern struct metadata_bundle metadata_store;

void add_metadata_watcher(metadata_watcher fn, void *userdata);

void metadata_hub_init(void);
void metadata_hub_stop(void);
void metadata_hub_process_metadata(uint32_t type, uint32_t code, char *data, uint32_t length);
void metadata_hub_reset_track_metadata(void);
void metadata_hub_release_track_artwork(void);

// these functions lock and unlock the read-write mutex on the metadata hub and run the watchers
// afterwards
void _metadata_hub_modify_prolog(const char *filename, const int linenumber);
void _metadata_hub_modify_epilog(
    int modified, const char *filename,
    const int linenumber); // set to true if modifications occurred, 0 otherwise

/*
// these are for safe reading
void _metadata_hub_read_prolog(const char *filename, const int linenumber);
void _metadata_hub_read_epilog(const char *filename, const int linenumber);
*/

#define metadata_hub_modify_prolog(void) _metadata_hub_modify_prolog(__FILE__, __LINE__)
#define metadata_hub_modify_epilog(modified)                                                       \
  _metadata_hub_modify_epilog(modified, __FILE__, __LINE__)

#define metadata_hub_read_prolog(void) _metadata_hub_read_prolog(__FILE__, __LINE__)
#define metadata_hub_read_epilog(void) _metadata_hub_modify_epilog(__FILE__, __LINE__)
