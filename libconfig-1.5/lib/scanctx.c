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

#include "scanctx.h"
#include "wincompat.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#define STRING_BLOCK_SIZE 64
#define CHUNK_SIZE 32

/* ------------------------------------------------------------------------- */

static const char *err_bad_include = "cannot open include file";
static const char *err_include_too_deep = "include file nesting too deep";

/* ------------------------------------------------------------------------- */

static const char *__scanctx_add_filename(struct scan_context *ctx,
                                          const char *filename)
{
  unsigned int count = ctx->num_filenames;
  const char **f;

  for(f = ctx->filenames; count > 0; ++f, --count)
  {
    if(!strcmp(*f, filename))
    {
      free((void *)filename);
      return(*f); /* already in list */
    }
  }

  if((ctx->num_filenames % CHUNK_SIZE) == 0)
  {
    ctx->filenames = (const char **)realloc(
      (void *)ctx->filenames,
      (ctx->num_filenames + CHUNK_SIZE) * sizeof(const char *));
  }

  ctx->filenames[ctx->num_filenames] = filename;
  ++ctx->num_filenames;
  return(filename);
}

/* ------------------------------------------------------------------------- */

void scanctx_init(struct scan_context *ctx, const char *top_filename)
{
  memset(ctx, 0, sizeof(struct scan_context));
  if(top_filename)
    ctx->top_filename = __scanctx_add_filename(ctx, strdup(top_filename));
}

/* ------------------------------------------------------------------------- */

const char **scanctx_cleanup(struct scan_context *ctx,
                             unsigned int *num_filenames)
{
  int i;

  for(i = 0; i < ctx->depth; ++i)
    fclose(ctx->streams[i]);

  free((void *)(strbuf_release(&(ctx->string))));

  *num_filenames = ctx->num_filenames;
  return(ctx->filenames);
}

/* ------------------------------------------------------------------------- */

FILE *scanctx_push_include(struct scan_context *ctx, void *buffer,
                           const char **error)
{
  FILE *fp = NULL;
  const char *file;
  char *full_file = NULL;

  *error = NULL;

  if(ctx->depth == MAX_INCLUDE_DEPTH)
  {
    *error = err_include_too_deep;
    return(NULL);
  }

  file = scanctx_take_string(ctx);
  if(ctx->config->include_dir)
  {
    full_file = (char *)malloc(strlen(ctx->config->include_dir) + strlen(file)
                               + 2);
    strcpy(full_file, ctx->config->include_dir);
    strcat(full_file, FILE_SEPARATOR);
    strcat(full_file, file);
  }

  fp = fopen(full_file ? full_file : file, "rt");
  free((void *)full_file);

  if(fp)
  {
    ctx->streams[ctx->depth] = fp;
    ctx->files[ctx->depth] = __scanctx_add_filename(ctx, file);
    ctx->buffers[ctx->depth] = buffer;
    ++(ctx->depth);
  }
  else
  {
    free((void *)file);
    *error = err_bad_include;
  }

  return(fp);
}

/* ------------------------------------------------------------------------- */

void *scanctx_pop_include(struct scan_context *ctx)
{
  void *buffer;

  if(ctx->depth == 0)
    return(NULL); /* stack underflow */

  --(ctx->depth);
  buffer = ctx->buffers[ctx->depth];
  fclose(ctx->streams[ctx->depth]);

  return(buffer);
}

/* ------------------------------------------------------------------------- */

char *scanctx_take_string(struct scan_context *ctx)
{
  char *r = strbuf_release(&(ctx->string));

  return(r ? r : strdup(""));
}

/* ------------------------------------------------------------------------- */

const char *scanctx_current_filename(struct scan_context *ctx)
{
  return((ctx->depth == 0) ? ctx->top_filename : ctx->files[ctx->depth - 1]);
}

/* ------------------------------------------------------------------------- */
