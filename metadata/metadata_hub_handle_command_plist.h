#pragma once

// This handles the plist that comes in on the AirPlay 2 COMMAND endpoint.

#include "rtsp.h"

void metadata_hub_handle_command_plist(rtsp_conn_info *conn, const plist_t command_dict);
