/* ----------------------------------------------------------------------------
   libconfig - A library for processing structured configuration files
   Copyright (C) 2005-2014  Mark A Lindner

   This file is part of libconfig.

   This library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Lesser General Public License
   as published by the Free Software Foundation; either version 2.1 of
   the License, or (at your option) any later version.

   This library is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Lesser General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with this library; if not, see
   <http://www.gnu.org/licenses/>.
   ----------------------------------------------------------------------------
*/

#include "strbuf.h"

#include <string.h>
#include <stdlib.h>

#define STRING_BLOCK_SIZE 64

/* ------------------------------------------------------------------------- */

char *strbuf_release(strbuf_t *buf)
{
  char *r = buf->string;
  memset(buf, 0, sizeof(strbuf_t));
  return(r);
}

/* ------------------------------------------------------------------------- */

void strbuf_append(strbuf_t *buf, const char *text)
{
  static const size_t mask = ~(STRING_BLOCK_SIZE - 1);
  size_t len = strlen(text);
  size_t newlen = buf->length + len + 1; /* add 1 for NUL */

  if(newlen > buf->capacity)
  {
    buf->capacity = (newlen + (STRING_BLOCK_SIZE - 1)) & mask;
    buf->string = (char *)realloc(buf->string, buf->capacity);
  }

  strcpy(buf->string + buf->length, text);
  buf->length += len;
}

/* ------------------------------------------------------------------------- */
