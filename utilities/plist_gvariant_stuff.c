/*
 * Utilities for dealing with plist to GVariant conversions.
 * This file is part of Shairport Sync
 * Copyright (c) Mike Brady 2026
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
 
 // Note: this code is slightly specialised:
 // 1. It will skip a dict item keyed 'kMRMediaRemoteNowPlayingInfoArtworkData' 
 // 2. It will recursively convert Data items that are themselves plists.

#include <glib.h>
#include <plist/plist.h>
#include <string.h>
#include <time.h>

#include "plist_gvariant_stuff.h"

static GVariant *plist_node_to_gvariant(plist_t node);

// Only recognises binary plists (bplist00 magic header) as embedded plists.
// Anything else falls through and is treated as raw data.
static plist_t try_parse_embedded_plist(const char *data, uint64_t length) {
    if (!data || length < 8)
        return NULL;

    if (memcmp(data, "bplist00", 8) != 0)
        return NULL;

    plist_t embedded = NULL;
    plist_from_bin(data, (uint32_t)length, &embedded);
    return embedded; // NULL if the header matched but the body didn't actually parse
}


// watch out -- this will skip an item with the key kMRMediaRemoteNowPlayingInfoArtworkData
static GVariant *plist_dict_to_gvariant(plist_t node) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("a{sv}"));

    plist_dict_iter it = NULL;
    plist_dict_new_iter(node, &it);

    char *key = NULL;
    plist_t val = NULL;
    plist_dict_next_item(node, it, &key, &val);
    while (key) {
        GVariant *v = NULL;
        // Don't include the item keyed kMRMediaRemoteNowPlayingInfoArtworkData.
        // this is the raw bytes of the cover art, which will have been
        // stored in a local file whose pathname is added to the plist
        // with the key kShairportSyncNowPlayingInfoArtworkFilePath.
        if (strcmp(key, "kMRMediaRemoteNowPlayingInfoArtworkData") != 0) {
            v = plist_node_to_gvariant(val);
        }
        if (v)
            g_variant_builder_add(&builder, "{sv}", key, v);
        free(key);
        key = NULL;
        val = NULL;
        plist_dict_next_item(node, it, &key, &val);
    }
    free(it);

    return g_variant_builder_end(&builder);
}
static GVariant *plist_array_to_gvariant(plist_t node) {
    GVariantBuilder builder;
    g_variant_builder_init(&builder, G_VARIANT_TYPE("av"));

    uint32_t count = plist_array_get_size(node);
    for (uint32_t i = 0; i < count; i++) {
        plist_t item = plist_array_get_item(node, i);
        GVariant *v = plist_node_to_gvariant(item);
        if (v)
            g_variant_builder_add(&builder, "v", v);
    }

    return g_variant_builder_end(&builder);
}

static GVariant *plist_node_to_gvariant(plist_t node) {
    if (!node)
        return NULL;

    switch (plist_get_node_type(node)) {

    case PLIST_BOOLEAN: {
        uint8_t b = 0;
        plist_get_bool_val(node, &b);
        return g_variant_new_boolean(b ? TRUE : FALSE);
    }

    case PLIST_UINT: {
        uint64_t u = 0;
        plist_get_uint_val(node, &u);
        return g_variant_new_uint64(u);
    }

    case PLIST_REAL: {
        double d = 0.0;
        plist_get_real_val(node, &d);
        return g_variant_new_double(d);
    }

    case PLIST_STRING: {
        char *s = NULL;
        plist_get_string_val(node, &s);
        GVariant *v = g_variant_new_string(s ? s : "");
        free(s);
        return v;
    }

    case PLIST_DATE: {
        int32_t sec = 0, usec = 0;
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif
        plist_get_date_val(node, &sec, &usec);
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

        // Apple/Cocoa epoch (2001-01-01T00:00:00Z) offset from Unix epoch.
        const time_t APPLE_EPOCH_OFFSET = 978307200;
        time_t unix_sec = (time_t)sec + APPLE_EPOCH_OFFSET;

        struct tm tm_utc;
        gmtime_r(&unix_sec, &tm_utc);

        char buf[40];
        size_t n = strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm_utc);
        if (usec != 0)
            n += snprintf(buf + n, sizeof(buf) - n, ".%06d", usec);
        snprintf(buf + n, sizeof(buf) - n, "Z");

        return g_variant_new_string(buf);
    }

    case PLIST_DATA: {
        char *data = NULL;
        uint64_t length = 0;
        plist_get_data_val(node, &data, &length);

        plist_t embedded = try_parse_embedded_plist(data, length);
        if (embedded) {
            GVariant *v = plist_node_to_gvariant(embedded);
            plist_free(embedded);
            free(data);
            return v;
        }

        GVariant *v = g_variant_new_from_data(G_VARIANT_TYPE("ay"),
                                               data, length,
                                               TRUE, NULL, NULL);
        free(data);
        return v;
    }

    case PLIST_ARRAY:
        return plist_array_to_gvariant(node);

    case PLIST_DICT:
        return plist_dict_to_gvariant(node);

    case PLIST_UID: {
        uint64_t u = 0;
        plist_get_uid_val(node, &u);
        return g_variant_new_uint64(u);
    }

    default:
        return NULL;
    }
}

GVariant *plist_to_gvariant(plist_t root) {
    return plist_node_to_gvariant(root);
}