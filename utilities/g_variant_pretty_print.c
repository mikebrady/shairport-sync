/*
 * Pretty-print a GVariant a{sv} structure.
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

#include <glib.h>

static void indent_gstring(GString *out, int depth) {
    for (int i = 0; i < depth; i++)
        g_string_append(out, "    "); // 4 spaces per level
}

static void print_gvariant_pretty(GVariant *v, GString *out, int depth, gboolean type_annotate);

static void print_dict(GVariant *v, GString *out, int depth, gboolean type_annotate) {

    GVariantIter iter;
    g_variant_iter_init(&iter, v);
    GVariant *entry;
    gboolean first = TRUE;

    while ((entry = g_variant_iter_next_value(&iter))) {
        if (first) {
          g_string_append(out, "{\n");
        } else {
          g_string_append(out, ",\n");
        }
        first = FALSE;

        GVariant *key_v = g_variant_get_child_value(entry, 0);
        GVariant *val_v = g_variant_get_child_value(entry, 1);

        indent_gstring(out, depth + 1);
        char *key_str = g_variant_print(key_v, FALSE); // quoted 'key'
        g_string_append_printf(out, "%s: ", key_str);
        g_free(key_str);

        print_gvariant_pretty(val_v, out, depth + 1, type_annotate);

        g_variant_unref(key_v);
        g_variant_unref(val_v);
        g_variant_unref(entry);
    }
    if (first) { // if first is still true, it means the item was empty
      g_string_append(out, "{}\n");
    } else {
      g_string_append(out, "\n");
      indent_gstring(out, depth);
      g_string_append(out, "}");
    }
}

static void print_array(GVariant *v, GString *out, int depth, gboolean type_annotate) {
    g_string_append(out, "[\n");

    GVariantIter iter;
    g_variant_iter_init(&iter, v);
    GVariant *item;
    gboolean first = TRUE;

    while ((item = g_variant_iter_next_value(&iter))) {
        if (!first)
            g_string_append(out, ",\n");
        first = FALSE;

        indent_gstring(out, depth + 1);
        print_gvariant_pretty(item, out, depth + 1, type_annotate);

        g_variant_unref(item);
    }

    g_string_append(out, "\n");
    indent_gstring(out, depth);
    g_string_append(out, "]");
}

static void print_gvariant_pretty(GVariant *v, GString *out, int depth, gboolean type_annotate) {
    if (!v) {
        g_string_append(out, "null");
        return;
    }

    switch (g_variant_classify(v)) {

    case G_VARIANT_CLASS_VARIANT: {
        GVariant *inner = g_variant_get_variant(v);
        print_gvariant_pretty(inner, out, depth, type_annotate);
        g_variant_unref(inner);
        break;
    }

    case G_VARIANT_CLASS_ARRAY: {
        const GVariantType *elem_type = g_variant_type_element(g_variant_get_type(v));
        if (g_variant_type_is_dict_entry(elem_type))
            print_dict(v, out, depth, type_annotate);
        else
            print_array(v, out, depth, type_annotate);
        break;
    }

    default: {
        char *s = g_variant_print(v, type_annotate);
        g_string_append(out, s);
        g_free(s);
        break;
    }
    }
}

// Drop-in replacement for g_variant_print(), with line breaks and indenting.
// Same contract: returns a newly allocated string, caller must g_free() it.
char *g_variant_pretty_print(GVariant *value, gboolean type_annotate, int depth) {
    GString *out = g_string_new(NULL);
    print_gvariant_pretty(value, out, depth, type_annotate);
    return g_string_free(out, FALSE); // FALSE = hand ownership of the buffer to the caller
}
