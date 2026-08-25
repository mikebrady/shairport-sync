/*
 * General Utilities
 * This file is part of Shairport Sync.
 * Copyright (c) Mike Brady 2017--2026
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

#include "general_utilities.h"

double airplayVolumeToUnitVolume(double airplayVolume) {
  double response = 0.0;
  if ((airplayVolume >= -30.0) && (airplayVolume <= 0.0)) {
    response = airplayVolume / 30.0 + 1.0;
  }
  return response;
}

#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Parses a string of the form "a/b/c" into three uint32_t values.
 * Returns 0 on success, -1 on failure (malformed input, overflow,
 * or a value that doesn't fit in 32 bits).
 */
int parse_prlg(const char *str, uint32_t *a, uint32_t *b, uint32_t *c) {
  char *endptr;
  unsigned long val;
  uint32_t *targets[3] = {a, b, c};
  const char *p = str;

  if (str == NULL || a == NULL || b == NULL || c == NULL)
    return -1;

  for (int i = 0; i < 3; i++) {
    errno = 0;

    /* Reject leading whitespace or a leading sign; strtoul
     * silently accepts both, which we don't want here. */
    if (*p == '\0' || *p == ' ' || *p == '\t' || *p == '-' || *p == '+')
      return -1;

    val = strtoul(p, &endptr, 10);

    if (endptr == p) /* no digits consumed */
      return -1;
    if (errno == ERANGE || val > UINT32_MAX)
      return -1;

    *targets[i] = (uint32_t)val;

    if (i < 2) {
      if (*endptr != '/') /* expected a separator */
        return -1;
      p = endptr + 1;
    } else {
      if (*endptr != '\0') /* trailing garbage after 3rd number */
        return -1;
    }
  }

  return 0;
}