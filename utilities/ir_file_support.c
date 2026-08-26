/*
 * Utilities for dealing with ir (Finite Impulse Response) file lists.

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

/* Parse comma-separated filenames with optional quotes
 * Returns array of ir_file_info_t structs (caller must free both array and filenames)
 * count is set to number of filenames found
 * Returns NULL on error
 */

#include "ir_file_support.h"
#include <ctype.h>    // isspace()
#include <inttypes.h> // PRId64
#include <sndfile.h>
#include <stdlib.h> // malloc

ir_file_info_t *parse_ir_filenames(const char *input, unsigned int *file_count) {
  if (!input || !file_count)
    return NULL;

  *file_count = 0;
  unsigned int capacity = 10;
  ir_file_info_t *files = malloc(capacity * sizeof(ir_file_info_t));
  if (!files)
    return NULL;

  const char *p = input;

  while (*p) {
    /* Skip whitespace before filename */
    while (isspace((unsigned char)*p))
      p++;
    if (!*p)
      break;

    /* Check if we need to resize array */
    if (*file_count >= capacity) {
      capacity *= 2;
      ir_file_info_t *temp = realloc(files, capacity * sizeof(ir_file_info_t));
      if (!temp) {
        for (unsigned int i = 0; i < *file_count; i++)
          free(files[i].filename);
        free(files);
        return NULL;
      }
      files = temp;
    }

    /* Parse one filename */
    char quote_char = 0;
    char *buffer = NULL;
    size_t buf_len = 0;
    size_t buf_cap = 64;

    if (*p == '"' || *p == '\'') {
      /* Quoted filename */
      quote_char = *p;
      p++;

      buffer = malloc(buf_cap);
      if (!buffer) {
        for (unsigned int i = 0; i < *file_count; i++)
          free(files[i].filename);
        free(files);
        return NULL;
      }

      /* Parse quoted string with escape handling */
      while (*p && *p != quote_char) {
        if (*p == '\\' && *(p + 1)) {
          /* Escape sequence */
          p++;
          if (buf_len >= buf_cap - 1) {
            buf_cap *= 2;
            char *temp = realloc(buffer, buf_cap);
            if (!temp) {
              free(buffer);
              for (unsigned int i = 0; i < *file_count; i++)
                free(files[i].filename);
              free(files);
              return NULL;
            }
            buffer = temp;
          }
          buffer[buf_len++] = *p++;
        } else {
          if (buf_len >= buf_cap - 1) {
            buf_cap *= 2;
            char *temp = realloc(buffer, buf_cap);
            if (!temp) {
              free(buffer);
              for (unsigned int i = 0; i < *file_count; i++)
                free(files[i].filename);
              free(files);
              return NULL;
            }
            buffer = temp;
          }
          buffer[buf_len++] = *p++;
        }
      }
      buffer[buf_len] = '\0';
      if (*p == quote_char)
        p++; /* Skip closing quote */

      files[*file_count].samplerate = 0;
      // files[*file_count].evaluation = ev_unchecked;
      files[*file_count].filename = buffer;
      (*file_count)++;
    } else {
      /* Unquoted filename - read until comma or end, handle escapes */
      buffer = malloc(buf_cap);
      if (!buffer) {
        for (unsigned int i = 0; i < *file_count; i++)
          free(files[i].filename);
        free(files);
        return NULL;
      }

      while (*p && *p != ',') {
        if (*p == '\\' && *(p + 1)) {
          /* Escape sequence */
          p++;
          if (buf_len >= buf_cap - 1) {
            buf_cap *= 2;
            char *temp = realloc(buffer, buf_cap);
            if (!temp) {
              free(buffer);
              for (unsigned int i = 0; i < *file_count; i++)
                free(files[i].filename);
              free(files);
              return NULL;
            }
            buffer = temp;
          }
          buffer[buf_len++] = *p++;
        } else {
          if (buf_len >= buf_cap - 1) {
            buf_cap *= 2;
            char *temp = realloc(buffer, buf_cap);
            if (!temp) {
              free(buffer);
              for (unsigned int i = 0; i < *file_count; i++)
                free(files[i].filename);
              free(files);
              return NULL;
            }
            buffer = temp;
          }
          buffer[buf_len++] = *p++;
        }
      }

      /* Trim trailing whitespace */
      while (buf_len > 0 && isspace((unsigned char)buffer[buf_len - 1])) {
        buf_len--;
      }
      buffer[buf_len] = '\0';

      files[*file_count].samplerate = 0;
      files[*file_count].channels = 0;
      // files[*file_count].evaluation = ev_unchecked;
      files[*file_count].filename = buffer;
      (*file_count)++;
    }

    /* Skip comma and whitespace */
    while (isspace((unsigned char)*p))
      p++;
    if (*p == ',') {
      p++;
      while (isspace((unsigned char)*p))
        p++;
    }
  }

  return files;
}

/* Do a quick sanity check on the files -- see if they can be opened as sound files */
unsigned int sanity_check_ir_files(const int option_print_level, ir_file_info_t *files,
                                   unsigned int count) {
  int error_detected = 0; // means all okay
  if (files != NULL) {
    unsigned int i = 0;
    while ((i < count) && (error_detected == 0)) {
      SF_INFO sfinfo = {};
      // sfinfo.format = 0;

      SNDFILE *file = sf_open(files[i].filename, SFM_READ, &sfinfo);
      if (file) {
        // files[i].evaluation = ev_okay;
        files[i].samplerate = sfinfo.samplerate;
        files[i].channels = sfinfo.channels;
        debug(option_print_level,
              "convolution impulse response file %u, \"%s\": %" PRId64
              " frames (%.1f seconds), %d channel%s at %d frames per second.",
              i + 1, files[i].filename, sfinfo.frames, (float)sfinfo.frames / sfinfo.samplerate,
              sfinfo.channels, sfinfo.channels == 1 ? "" : "s", sfinfo.samplerate);
        sf_close(file);
      } else {
        error_detected = i + 1;
        /*
        debug(option_print_level, "convolution impulse response file \"%s\" %s", files[i].filename,
              sf_strerror(NULL));
        warn("Error accessing the convolution impulse response file \"%s\". %s", files[i].filename,
             sf_strerror(NULL));
        */
      }
      if (error_detected == 0)
        i++;
    }
  } else {
    debug(option_print_level, "no convolution impulse response files found.");
  }
  return error_detected; // this is either 0 or 1 more thatn the fiel numbver
}

/* Free the array returned by parse_filenames */
void free_ir_filenames(ir_file_info_t *files, unsigned int file_count) {
  if (!files)
    return;
  for (unsigned int i = 0; i < file_count; i++) {
    free(files[i].filename);
  }
  free(files);
}
