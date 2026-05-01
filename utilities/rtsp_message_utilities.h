#pragma once

#include "player.h"
#include "rtsp.h"

rtsp_message *msg_init(void);
int msg_handle_line(rtsp_message **pmsg, char *line);
int msg_add_header(rtsp_message *msg, char *name, char *value);
char *msg_get_header(rtsp_message *msg, char *name);

void _debug_log_rtsp_message(rtsp_conn_info *conn, const char *filename, const int linenumber, int level, char *prompt,
                             rtsp_message *message);

#define debug_log_rtsp_message_conn(conn, level, prompt, message)                                  \
  _debug_log_rtsp_message(conn, __FILE__, __LINE__, level, prompt, message)

#define debug_log_rtsp_message(level, prompt, message)                                             \
  _debug_log_rtsp_message(NULL, __FILE__, __LINE__, level, prompt, message)


void _debug_print_msg_headers(rtsp_conn_info *conn, const char *filename, const int linenumber, int level,
                              rtsp_message *msg);
#define debug_print_msg_headers(level, message)                                                    \
  _debug_print_msg_headers(NULL, __FILE__, __LINE__, level, message)

#define debug_print_msg_headers_conn(level, message)                                               \
  _debug_print_msg_headers(conn, __FILE__, __LINE__, level, message)


#ifdef CONFIG_AIRPLAY_2
int rtsp_message_contains_plist(rtsp_message *message);
plist_t plist_from_rtsp_content(rtsp_message *message);
#endif