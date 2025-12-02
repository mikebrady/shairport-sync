/*
 * Structured Buffer. This file is part of Shairport Sync
 * Copyright (c) Mike Brady 2025

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
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>


typedef struct {
  char *buf;
  size_t buf_size;
  size_t buf_pos;
} structured_buffer;

structured_buffer *sbuf_new(size_t size) {
  structured_buffer *sbuf = (structured_buffer *)malloc(sizeof(structured_buffer));
  if (sbuf != NULL) {
    memset(sbuf, 0, sizeof(structured_buffer));
    char *buf = malloc(size + 1); // extra space for a possible NULL
    if (buf == NULL) {
      free(sbuf);
      sbuf = NULL;
    } else {
      sbuf->buf_size = size;
      sbuf->buf = buf;
    }
  }
  return sbuf;
}

int sbuf_clear(structured_buffer *sbuf) {
  int response = -1;
  if ((sbuf != NULL) && (sbuf->buf != NULL)) {
    sbuf->buf_pos = 0;
    response = 0;
  }
  return response;
}

void sbuf_free(structured_buffer *sbuf) {
  if (sbuf != NULL) {
    if (sbuf->buf != NULL)
      free(sbuf->buf);
    free(sbuf);
  }
}

void sbuf_cleanup(void *arg) {
  structured_buffer *sbuf = (structured_buffer *)arg;
  debug(3, "structured_buffer cleanup");
  sbuf_free(sbuf);
}

int sbuf_printf(structured_buffer *sbuf, const char *format, ...) {
  int response = -1;
  if ((sbuf != NULL) && (sbuf->buf != NULL)) {
    char *p = sbuf->buf + sbuf->buf_pos;
    va_list args;
    va_start(args, format);
    vsnprintf(p, sbuf->buf_size - sbuf->buf_pos, format, args);
    sbuf->buf_pos = sbuf->buf_pos + strlen(p);
    response = strlen(p);
    va_end(args);
  }
  return response;
}

int sbuf_append(structured_buffer *sbuf, char *plistString, uint32_t plistStringLength) {
  int response = -1;
  if ((sbuf != NULL) && (sbuf->buf != NULL) && (plistString != NULL)) {
    if (plistStringLength == 0) {
      response = 0;
    } else {
      if (plistStringLength < (sbuf->buf_size - sbuf->buf_pos)) {
        memcpy(sbuf->buf + sbuf->buf_pos, plistString, plistStringLength);
        sbuf->buf_pos = sbuf->buf_pos + plistStringLength;
        response = 0;
      } else {
        debug(1, "plist too large -- omitted");
      }
    }
  }
  return response;
}

int sbuf_buf_and_length(structured_buffer *sbuf, char **b, size_t *l) {
  int response = 0;
  if ((sbuf != NULL) && (sbuf->buf != NULL)) {
    *b = sbuf->buf;
    *l = sbuf->buf_pos;
  } else {
    response = -1;
  }
  return response;
}
