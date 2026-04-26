#ifndef _RTSP_H
#define _RTSP_H

#include "player.h"

typedef struct {
  int index_number;
  uint32_t referenceCount; // we might start using this...
  unsigned int nheaders;
  char *name[16];
  char *value[16];

  uint32_t contentlength;
  char *content;

  // for requests
  char method[16];
  char path[256];

  // for responses
  int respcode;
} rtsp_message;

void msg_retain(rtsp_message *msg);
void msg_free(rtsp_message **msgh);

extern pthread_rwlock_t principal_conn_lock;
extern rtsp_conn_info *principal_conn;
extern rtsp_conn_info **conns;

void *rtsp_listen_loop(__attribute((unused)) void *arg);

void lock_player();
void unlock_player();

// result of trying to acquire or release the play lock
typedef enum {
  play_lock_released,
  play_lock_already_released,
  play_lock_already_acquired,
  play_lock_acquired_without_breaking_in,
  play_lock_acquired_by_breaking_in,
  play_lock_aquisition_failed
} play_lock_r;

// this can be used to [try to] forcibly stop a play session
play_lock_r get_play_lock(rtsp_conn_info *conn, int allow_session_interruption);
// this will release the play lock only if the conn has it or if the conn is NULL
void release_play_lock(rtsp_conn_info *conn);


#ifdef CONFIG_AIRPLAY_2
ssize_t read_encrypted(int fd, pair_cipher_bundle *ctx, void *buf, size_t count);
ssize_t write_encrypted(int fd, pair_cipher_bundle *ctx, const void *buf, size_t count);

void generateTxtDataValueInfo(rtsp_conn_info *conn, void **response, size_t *responseLength);
plist_t generateInfoPlist(rtsp_conn_info *conn);
char *plist_as_xml_text(plist_t the_plist); // caller must free the returned NUL-terminated string
#endif

#endif // _RTSP_H
