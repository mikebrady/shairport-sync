#pragma once

// This handles the plist that comes in on the AirPlay 2 COMMAND endpoint.

#include "rtsp.h"

void metadata_hub_handle_command_plist(const plist_t command_dict);
