/*
 * This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2018--2026
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

 * property-preflight.c
 *
 * Implementation of the generic value-checking helpers declared in
 * property-preflight.h. Interface-agnostic - see
 * property-preflight-shairportsync.c and property-preflight-mpris.c
 * for the actual per-interface validators that use these.
 */

#include "property-preflight.h"

/* ========================================================================
 * Generic value-checking helpers
 * ======================================================================== */

gboolean property_preflight_string_enum(const gchar *property_name, GVariant *value,
                                        const gchar *const *valid_values, const gchar *interface,
                                        GError **error) {
  const gchar *s = g_variant_get_string(value, NULL);

  if (g_strv_contains(valid_values, s))
    return TRUE;

  GString *list = g_string_new(NULL);
  gint i;

  for (i = 0; valid_values[i] != NULL; i++) {
    if (i > 0)
      g_string_append(list, ", ");
    g_string_append_printf(list, "\"%s\"", valid_values[i]);
  }

  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
              "\"%s\" is not a valid value for %s.%s --  it must be one of the following: %s, "
              "(case-sensitive).",
              s, interface, property_name, list->str);
  g_string_free(list, TRUE);

  return FALSE;
}

gboolean property_preflight_normalize_string_enum(const gchar *property_name, GVariant **value,
                                                  const gchar *const *valid_values,
                                                  const gchar *interface, GError **error) {
  const gchar *s = g_variant_get_string(*value, NULL);
  gint i;

  /* Exact match - accept as-is, no substitution needed. */
  if (g_strv_contains(valid_values, s))
    return TRUE;

  /* Case-insensitive match - substitute the canonical spelling. */
  for (i = 0; valid_values[i] != NULL; i++) {
    if (g_ascii_strcasecmp(s, valid_values[i]) == 0) {
      *value = g_variant_ref_sink(g_variant_new_string(valid_values[i]));
      return TRUE;
    }
  }

  /* No match at all, not even case-insensitively - reject. */

  GString *list = g_string_new(NULL);

  for (i = 0; valid_values[i] != NULL; i++) {
    if (i > 0)
      g_string_append(list, ", ");
    g_string_append_printf(list, "\"%s\"", valid_values[i]);
  }

  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
              "\"%s\" is not a valid value for %s.%s --  it must be one of the following: %s, "
              "case-sensitive",
              s, interface, property_name, list->str);
  g_string_free(list, TRUE);

  return FALSE;
}

gboolean property_preflight_double_range(const gchar *property_name, GVariant *value,
                                         gdouble min_value, gdouble max_value,
                                         const gchar *interface, GError **error) {
  gdouble v = g_variant_get_double(value);

  if (v >= min_value && v <= max_value)
    return TRUE;

  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
              "%.1f is not a valid value for %s.%s (expected a value between %.1f and %.1f)", v,
              interface, property_name, min_value, max_value);
  return FALSE;
}

gboolean property_preflight_clamp_double_range(const gchar *property_name, GVariant **value,
                                               gdouble min_value, gdouble max_value,
                                               GError **error) {
  gdouble v = g_variant_get_double(*value);
  gdouble clamped = CLAMP(v, min_value, max_value);

  (void)property_name;
  (void)error;

  if (clamped != v)
    *value = g_variant_ref_sink(g_variant_new_double(clamped));

  return TRUE;
}

gboolean property_preflight_int_range(const gchar *property_name, GVariant *value, gint min_value,
                                      gint max_value, const gchar *interface, GError **error) {
  gint v = g_variant_get_int32(value);

  if (v >= min_value && v <= max_value)
    return TRUE;

  g_set_error(error, G_DBUS_ERROR, G_DBUS_ERROR_INVALID_ARGS,
              "%d is not a valid value for %s.%s (expected a value between %d and %d)", v,
              interface, property_name, min_value, max_value);
  return FALSE;
}

gboolean property_preflight_clamp_int_range(const gchar *property_name, GVariant **value,
                                            gint min_value, gint max_value, GError **error) {
  gint v = g_variant_get_int32(*value);
  gint clamped = CLAMP(v, min_value, max_value);

  (void)property_name;
  (void)error;

  if (clamped != v)
    *value = g_variant_ref_sink(g_variant_new_int32(clamped));

  return TRUE;
}
