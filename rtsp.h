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

void *rtsp_listen_loop(__attribute((unused)) void *arg);

// this can be used to [try to] forcibly stop a play session
// play_lock_r get_play_lock(rtsp_conn_info *conn, int allow_session_interruption);
// this will release the play lock only if the conn has it or if the conn is NULL

void stop_play(); // stop and drop a playing connection

extern rtsp_conn_info *principal_conn;
extern pthread_rwlock_t principal_conn_lock;

#ifdef CONFIG_AIRPLAY_2
ssize_t read_encrypted(int fd, pair_cipher_bundle *ctx, void *buf, size_t count);
ssize_t write_encrypted(int fd, pair_cipher_bundle *ctx, const void *buf, size_t count);

void generateTxtDataValueInfo(rtsp_conn_info *conn, void **response, size_t *responseLength);
plist_t generateInfoPlist(rtsp_conn_info *conn);
char *plist_as_xml_text(plist_t the_plist); // caller must free the returned NUL-terminated string
#endif

#endif // _RTSP_H
