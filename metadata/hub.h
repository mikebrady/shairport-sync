#pragma once
#include "common.h"
#include "config.h"
#include "rtsp.h"
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

typedef struct {
  uint64_t item; // the value
  int valid;     // set to true if valid
} uint64_record_t;

int update_string_record_with_data(char **str, const char *data, size_t length); // data and length
int update_string_record(char **str, const char *s); // returns true if the string has changed
int update_uint64_record(
    uint64_record_t *record,
    const uint64_t value); // returns true if the string has changed, sets item to valid
int is_valid_uint64_record(uint64_record_t *record);

struct metadata_bundle;

typedef void (*metadata_watcher)(struct metadata_bundle *argc);

typedef struct metadata_npi_bundle { // now playing information
  char *cover_art_pathname;
  uint64_record_t item_id; // seems to be a track ID -- see itemid in DACP.c
  unsigned char
      item_composite_id[16]; // seems to be nowplaying 4 ids: dbid, plid, playlistItem, itemid
  int item_composite_id_is_valid;
  uint64_record_t song_data_kind; // 0 seems to mean a time-limited item
  char *track_name;
  uint64_record_t track_number;
  char *artist_name;
  char *album_artist_name;
  char *album_name;
  char *genre;
  char *comment;
  char *composer;
  char *file_kind;
  char *song_description;
  char *song_album_artist;
  char *sort_name;
  char *sort_artist;
  char *sort_album;
  char *sort_composer;
  uint64_record_t songtime_in_microseconds;
#ifdef CONFIG_AIRPLAY_2
  plist_t npi_plist; // this can contain information a lot more than we use...
#endif
} metadata_npi_bundle;

typedef struct metadata_bundle {
  char *client_ip;       // IP number used by the audio source (i.e. the "client")
  char *client_name;     // the name of the client device, if available
  char *server_ip;       // IP number used by Shairport Sync
  char *stream_type;     // Realtime or Buffered
  char *source_format;   // Format of incoming audio, e.g. AAC/44100/S16_LE/2
  char *output_format;   // Format of outgoing audio, e.g. 44100/S32_LE/2 (always PCM)
  char *progress_string; // progress string, emitted by the source from time to time
  uint32_t progress_first_timestamp, progress_current_timestamp,
      progress_last_timestamp;       // parsed from the progress string
  char *frame_position_string;       // frame position string emitted by SPS on request
  char *first_frame_position_string; // first frame position string emitted by SPS on request
  int player_thread_active;          // true if a play thread is running
  int dacp_server_active; // true if there's a reachable DACP server (assumed to be the Airplay
                          // client) ; false otherwise
  int advanced_dacp_server_active; // true if there's a reachable DACP server with iTunes
                                   // capabilitiues
                                   // ; false otherwise
  int dacp_server_has_been_active; // basically this is a delayed version of dacp_server_active,
  // used detect transitions between server activity being on or off
  // e.g. to reease metadata when a server goes inactive, but not if it's permanently
  // inactive.
  play_status_type play_status; // this is the state the client is in
  shuffle_status_type shuffle_status;
  repeat_status_type repeat_status;
  play_status_type
      player_state; // this is the state of the actual player itself, which can be a bit noisy.
  active_state_type active_state;
  int speaker_volume; // this is the actual speaker volume, allowing for the main volume and the
                      // speaker volume control
  double airplay_volume;
#ifdef CONFIG_AIRPLAY_2
  plist_t supported_commands_plist;
#endif
  uint32_t head_rtp_timestamp; // the timestamp of the frame at the head of the output queue
  metadata_npi_bundle npi;
} metadata_bundle;

extern struct metadata_bundle metadata_store;
extern metadata_watcher
    metadata_watchers[number_of_watchers]; // functions to call if the metadata is changed.

void add_metadata_watcher(metadata_watcher fn);

void metadata_hub_init(void);
void metadata_hub_stop(void);
void metadata_hub_process_metadata(uint32_t type, uint32_t code, char *data, uint32_t length);
int metadata_hub_process_picture(const char *data, const size_t length);
void metadata_hub_reset_npi(metadata_npi_bundle *npi); // reset "now playing" information

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

// metadata queue stuff
void metadata_hub_queue_init();
void metadata_hub_queue_stop();
int send_metadata_to_hub_queue(const uint32_t type, const uint32_t code, const char *data,
                               const uint32_t length, rtsp_message *carrier, int block);
