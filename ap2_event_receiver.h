#ifndef _AP2_EVENT_RECEIVER_H
#define _AP2_EVENT_RECEIVER_H

#include "player.h"

ssize_t ap2_event_port_send_message(rtsp_conn_info *conn, char *data, size_t data_length);
ssize_t ap2_event_port_post_command(rtsp_conn_info *conn, plist_t command);

void *ap2_event_receiver(void *arg);

#endif // _AP2_EVENT_RECEIVER_H
