#ifndef _RTSP_H
#define _RTSP_H

#include "player.h"

extern pthread_rwlock_t principal_conn_lock;
extern rtsp_conn_info *principal_conn;
extern rtsp_conn_info **conns;

void *rtsp_listen_loop(__attribute((unused)) void *arg);

void lock_player();
void unlock_player();

// this can be used to forcibly stop a play session
int get_play_lock(rtsp_conn_info *conn, int allow_session_interruption);

// initialise and completely delete the metadata stuff

void metadata_init(void);
void metadata_stop(void);

// sends metadata out to the metadata pipe, if enabled.
// It is sent with the type 'ssnc' the given code, data and length
// The handler at the other end must know what to do with the data
// e.g. if it's malloced, to free it, etc.
// nothing is done automatically

int send_ssnc_metadata(const uint32_t code, const char *data, const uint32_t length,
                       const int block);

#ifdef CONFIG_AIRPLAY_2
ssize_t read_encrypted(int fd, pair_cipher_bundle *ctx, void *buf, size_t count);
ssize_t write_encrypted(int fd, pair_cipher_bundle *ctx, const void *buf, size_t count);

void generateTxtDataValueInfo(rtsp_conn_info *conn, void **response, size_t *responseLength);
plist_t generateInfoPlist(rtsp_conn_info *conn);
char *plist_as_xml_text(plist_t the_plist); // caller must free the returned NUL-terminated string
#endif

#endif // _RTSP_H
