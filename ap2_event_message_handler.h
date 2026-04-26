#ifndef _AP2_EVENT_COMMAND_SENDER_H
#define _AP2_EVENT_COMMAND_SENDER_H

#include <stddef.h>
#include <sys/types.h>
#include "player.h"

ssize_t ap2_event_send_unit_volume_notification(rtsp_conn_info *conn, double volume);
ssize_t ap2_event_send_modern_media_remote_command(rtsp_conn_info *conn, unsigned int command_number);
ssize_t ap2_event_send_update_info(rtsp_conn_info *conn);

ssize_t ap2_event_send_dev_mule();

#endif // _AP2_EVENT_COMMAND_SENDER_H
