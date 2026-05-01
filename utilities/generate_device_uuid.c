/*
 * Generate a device-related UUID. This file is part of Shairport Sync
 * Copyright (c) Mike Brady 2026

 * Modifications, including those associated with audio synchronization, multithreading and
 * metadata handling copyright (c) Mike Brady 2014--2025
 * All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
 
#include <stdlib.h>
#include <string.h>
#include <uuid/uuid.h>
 
#include "generate_device_uuid.h"
#include "definitions.h"

// user is responsible for deallocating returned string
char *generate_device_uuid(const char *device_id) {
    uuid_t namespace_uuid;
    uuid_t derived_uuid;

    uuid_parse(SHAIRPORT_SYNC_DEVICE_NAMESPACE, namespace_uuid); 
    uuid_generate_sha1(derived_uuid, namespace_uuid, device_id, strlen(device_id));

    char *uuid = malloc(UUID_STR_LEN + 1);
    uuid_unparse_lower(derived_uuid, uuid);
    return uuid;
}