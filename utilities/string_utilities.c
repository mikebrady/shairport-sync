/*
 * Some character string utilities. This file is part of Shairport Sync
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

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "common.h"
#include "string_utilities.h"

// mDNS supports maximum of 63-character names (we prepend 13, so only 50 left).
#define MAX_AIRPLAY_SERVICE_NAME_LENGTH 50

/* from
 * http://coding.debuntu.org/c-implementing-str_replace-replace-all-occurrences-substring#comment-722
 * with thanks.
 */

char *str_replace(const char *string, const char *substr, const char *replacement) {
  char *tok = NULL;
  char *newstr = NULL;
  char *oldstr = NULL;
  char *head = NULL;

  /* if either substr or replacement is NULL, duplicate string a let caller handle it */
  if (substr == NULL || replacement == NULL)
    return strdup(string);
  newstr = strdup(string);
  head = newstr;
  if (head) {
    while ((tok = strstr(head, substr))) {
      oldstr = newstr;
      newstr = malloc(strlen(oldstr) - strlen(substr) + strlen(replacement) + 1);
      /*failed to alloc mem, free old string and return NULL */
      if (newstr == NULL) {
        free(oldstr);
        return NULL;
      }
      memcpy(newstr, oldstr, tok - oldstr);
      memcpy(newstr + (tok - oldstr), replacement, strlen(replacement));
      memcpy(newstr + (tok - oldstr) + strlen(replacement), tok + strlen(substr),
             strlen(oldstr) - strlen(substr) - (tok - oldstr));
      memset(newstr + strlen(oldstr) - strlen(substr) + strlen(replacement), 0, 1);
      /* move back head right after the last replacement */
      head = newstr + (tok - oldstr) + strlen(replacement);
      free(oldstr);
    }
  } else {
    die("failed to allocate memory in str_replace.");
  }
  return newstr;
}

/*
 * append_truncated - Append `suffix` to `base`, returning a newly malloc'd
 * string.  If the combined result would exceed `limit` bytes (excluding the
 * NUL terminator), the base is truncated so that the output is:
 *
 *     <truncated base>...<suffix>
 *
 * The returned string is always NUL-terminated and never exceeds `limit`
 * bytes of content.  The caller is responsible for free()'ing it.
 *
 * Parameters:
 *   base   - the primary string
 *   suffix - the string to append
 *   limit  - maximum byte count of the resulting string (excl. NUL)
 *
 * Returns:
 *   A newly malloc'd string on success, or NULL on failure (limit too small
 *   to fit "..." + suffix, or malloc failure).
 */
char *append_truncated(const char *base, const char *suffix, size_t limit) {

  const char *ellipsis = "...";
  size_t ellipsis_len = strlen(ellipsis);
  size_t suffix_len = strlen(suffix);
  size_t base_len = strlen(base);
  size_t combined_len = base_len + suffix_len;
  size_t result_len;
  size_t head_len;
  char *out;

  if (combined_len <= limit) {
    /* Fits without truncation. */
    result_len = combined_len;
    out = malloc(result_len + 1);
    if (!out)
      return NULL;
    memcpy(out, base, base_len);
    memcpy(out + base_len, suffix, suffix_len + 1); /* +1 for NUL */
    return out;
  }

  /* Not enough room to fit even "..." + suffix. */
  if (limit < ellipsis_len + suffix_len)
    return NULL;

  head_len = limit - ellipsis_len - suffix_len;

  /* Respect UTF-8: don't cut in the middle of a multibyte sequence.
   * Walk back from head_len until we're at a character boundary
   * (i.e. a byte that is not a UTF-8 continuation byte 0x80–0xBF). */
  while (head_len > 0 && (base[head_len] & 0xC0) == 0x80)
    head_len--;

  result_len = head_len + ellipsis_len + suffix_len;
  out = malloc(result_len + 1);
  if (!out)
    return NULL;

  char *p = out;
  memcpy(p, base, head_len);
  p += head_len;
  memcpy(p, ellipsis, ellipsis_len);
  p += ellipsis_len;
  memcpy(p, suffix, suffix_len);
  p += suffix_len;
  *p = '\0';

  return out;
}

char *service_name(const char *raw_service_name) {
  char *response = NULL;
  // now, do the substitutions in the service name
  char hostname[256];
  gethostname(hostname, sizeof(hostname));
  // strip off a terminating .<anything>, e.g. .local from the hostname
  char *last_dot = strrchr(hostname, '.');
  if (last_dot != NULL)
    *last_dot = '\0';

  char *i0;
  if (raw_service_name == NULL)
    i0 = strdup("%H"); // default
  else
    i0 = strdup(raw_service_name); // this is the string provided in the configuration or on the
                                   // command line.
  // here, do the substitutions for %h, %H, %v and %V
  char *i1 = str_replace(i0, "%h", hostname);
  if ((hostname[0] >= 'a') && (hostname[0] <= 'z'))
    hostname[0] = hostname[0] - 0x20; // convert a lowercase first letter into a capital letter
  char *i2 = str_replace(i1, "%H", hostname);
  char *i3 = str_replace(i2, "%v", PACKAGE_VERSION);
  char *vs = get_version_string();
  char *i4 = str_replace(i3, "%V", vs); // service name complete
  // now, we may need to add "(Classic)" and/or truncate it to MAX_AIRPLAY_SERVICE_NAME_LENGTH
  // characters.
#ifdef CONFIG_AIRPLAY_2
  if ((raw_service_name == NULL) && (config.service_type == APST_forced_classic)) {
    response = append_truncated(
        i4, " (Classic)", MAX_AIRPLAY_SERVICE_NAME_LENGTH); // append "(Classic)" to default service
                                                            // name if forced to Classic
  } else {
#endif
    response = append_truncated(
        i4, "",
        MAX_AIRPLAY_SERVICE_NAME_LENGTH); // make sure it doesn't exceed the max length: 63 - 13
#ifdef CONFIG_AIRPLAY_2
  }
#endif
  free(i0);
  free(i1);
  free(i2);
  free(i3);
  free(i4);
  free(vs);
  return response;
}

// Read an entire file into a newly-allocated, null-terminated buffer.
// Returns NULL (with errno set) if the file cannot be opened or read.
// The caller must free the returned buffer.
char *read_file_to_string(const char *pathname) {
  FILE *f = fopen(pathname, "rb");
  if (f == NULL)
    return NULL;
  char *buffer = NULL;
  if (fseek(f, 0, SEEK_END) == 0) {
    long size = ftell(f);
    if ((size >= 0) && (fseek(f, 0, SEEK_SET) == 0)) {
      buffer = malloc((size_t)size + 1);
      if (buffer != NULL) {
        size_t got = fread(buffer, 1, (size_t)size, f);
        if (ferror(f)) {
          free(buffer);
          buffer = NULL;
          errno = EIO;
        } else {
          buffer[got] = '\0';
        }
      }
    }
  }
  int saved_errno = errno;
  fclose(f);
  errno = saved_errno;
  return buffer;
}

// Append n bytes of src to the growable, null-terminated buffer *dst (of length
// *length and allocation *capacity), growing it if needed, and return the
// (possibly moved) buffer.
static char *append_to_string(char *dst, size_t *length, size_t *capacity, const char *src,
                              size_t n) {
  if (*length + n + 1 > *capacity) {
    while (*length + n + 1 > *capacity)
      *capacity = *capacity ? *capacity * 2 : 64;
    char *bigger = realloc(dst, *capacity);
    if (bigger == NULL)
      die("could not allocate memory in append_to_string.");
    dst = bigger;
  }
  memcpy(dst + *length, src, n);
  *length += n;
  dst[*length] = '\0';
  return dst;
}

// Expand ${NAME} environment-variable references in the given text.
//
// Syntax:
//   ${NAME}  is replaced by the value of environment variable NAME, where NAME
//            matches [A-Za-z_][A-Za-z0-9_]*. The braces are required: a bare
//            "$NAME", or a stray "$", is never touched.
//   $${      is replaced by a literal "${", so the text can still contain a
//            literal "${".
// Referencing an undefined variable is a fatal error (die) rather than a silent
// empty substitution, because an empty value in the wrong place is nasty to
// debug. Text containing no "${" is returned byte-for-byte unchanged.
//
// The name_for_errors argument is only used to identify the text in error
// messages (for the configuration file, its pathname).
//
// Returns a newly-allocated, null-terminated string; the caller must free it.
char *expand_environment_variables(const char *text, const char *name_for_errors) {
  size_t capacity = strlen(text) + 1;
  size_t length = 0;
  char *result = malloc(capacity);
  if (result == NULL)
    die("could not allocate memory while expanding \"%s\".", name_for_errors);
  result[0] = '\0';

  for (size_t i = 0; text[i] != '\0';) {
    if ((text[i] == '$') && (text[i + 1] == '$') && (text[i + 2] == '{')) {
      // "$${" -> literal "${"
      result = append_to_string(result, &length, &capacity, "${", 2);
      i += 3;
    } else if ((text[i] == '$') && (text[i + 1] == '{')) {
      // "${NAME}" -> value of environment variable NAME
      size_t start = i + 2;
      size_t end = start;
      if (!(isalpha((unsigned char)text[end]) || (text[end] == '_')))
        die("malformed environment-variable reference in \"%s\": \"${\" must be followed by a name "
            "matching [A-Za-z_][A-Za-z0-9_]* (use \"$${\" for a literal \"${\").",
            name_for_errors);
      while (isalnum((unsigned char)text[end]) || (text[end] == '_'))
        end++;
      if (text[end] != '}')
        die("unterminated environment-variable reference \"${%.*s\" in \"%s\": expected a closing "
            "\"}\".",
            (int)(end - start), text + start, name_for_errors);
      size_t name_length = end - start;
      char *variable_name = malloc(name_length + 1);
      if (variable_name == NULL)
        die("could not allocate memory while expanding \"%s\".", name_for_errors);
      memcpy(variable_name, text + start, name_length);
      variable_name[name_length] = '\0';
      const char *value = getenv(variable_name);
      if (value == NULL)
        die("the environment variable \"${%s}\", referenced in \"%s\", is not set.", variable_name,
            name_for_errors);
      result = append_to_string(result, &length, &capacity, value, strlen(value));
      free(variable_name);
      i = end + 1; // skip past the '}'
    } else {
      result = append_to_string(result, &length, &capacity, &text[i], 1);
      i++;
    }
  }
  return result;
}
