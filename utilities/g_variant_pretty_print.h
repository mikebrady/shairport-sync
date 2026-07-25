#pragma once
#include <glib.h>

// depth is how many steps inwards to start at...
char *g_variant_pretty_print(GVariant *value, gboolean type_annotate, int depth);