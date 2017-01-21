/* -*- mode: C -*- */
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

%defines
%output "y.tab.c"
%pure-parser
%lex-param{void *scanner}
%parse-param{void *scanner}
%parse-param{struct parse_context *ctx}
%parse-param{struct scan_context *scan_ctx}

%{
#include <string.h>
#include <stdlib.h>
#include "libconfig.h"
#ifdef WIN32
#include "wincompat.h"

/* prevent warnings about redefined malloc/free in generated code: */
#ifndef _STDLIB_H
#define _STDLIB_H
#endif

#include <malloc.h>
#endif
#include "parsectx.h"
#include "scanctx.h"

/* these delcarations are provided to suppress compiler warnings */
extern int libconfig_yylex();
extern int libconfig_yyget_lineno();

static const char *err_array_elem_type = "mismatched element type in array";
static const char *err_duplicate_setting = "duplicate setting name";

#define _delete(P) free((void *)(P))

#define IN_ARRAY() \
  (ctx->parent && (ctx->parent->type == CONFIG_TYPE_ARRAY))

#define IN_LIST() \
  (ctx->parent && (ctx->parent->type == CONFIG_TYPE_LIST))

static void capture_parse_pos(void *scanner, struct scan_context *scan_ctx,
                              config_setting_t *setting)
{
  setting->line = (unsigned int)libconfig_yyget_lineno(scanner);
  setting->file = scanctx_current_filename(scan_ctx);
}

#define CAPTURE_PARSE_POS(S) \
  capture_parse_pos(scanner, scan_ctx, (S))

void libconfig_yyerror(void *scanner, struct parse_context *ctx,
                       struct scan_context *scan_ctx, char const *s)
{
  if(ctx->config->error_text) return;
  ctx->config->error_line = libconfig_yyget_lineno(scanner);
  ctx->config->error_text = s;
}

%}

%union
{
  int ival;
  long long llval;
  double fval;
  char *sval;
}

%token <ival> TOK_BOOLEAN TOK_INTEGER TOK_HEX
%token <llval> TOK_INTEGER64 TOK_HEX64
%token <fval> TOK_FLOAT
%token <sval> TOK_STRING TOK_NAME
%token TOK_EQUALS TOK_NEWLINE TOK_ARRAY_START TOK_ARRAY_END TOK_LIST_START TOK_LIST_END TOK_COMMA TOK_GROUP_START TOK_GROUP_END TOK_SEMICOLON TOK_GARBAGE TOK_ERROR

%%

configuration:
    /* empty */
  | setting_list
  ;

setting_list:
    setting
  | setting_list setting
  ;

setting_list_optional:
    /* empty */
  | setting_list
  ;

setting_terminator:
    /* empty */
  | TOK_SEMICOLON
  | TOK_COMMA
  ;

setting:
  TOK_NAME
  {
    ctx->setting = config_setting_add(ctx->parent, $1, CONFIG_TYPE_NONE);

    if(ctx->setting == NULL)
    {
      libconfig_yyerror(scanner, ctx, scan_ctx, err_duplicate_setting);
      YYABORT;
    }
    else
    {
      CAPTURE_PARSE_POS(ctx->setting);
    }
  }

  TOK_EQUALS value setting_terminator
  ;

array:
  TOK_ARRAY_START
  {
    if(IN_LIST())
    {
      ctx->parent = config_setting_add(ctx->parent, NULL, CONFIG_TYPE_ARRAY);
      CAPTURE_PARSE_POS(ctx->parent);
    }
    else
    {
      ctx->setting->type = CONFIG_TYPE_ARRAY;
      ctx->parent = ctx->setting;
      ctx->setting = NULL;
    }
  }
  simple_value_list_optional
  TOK_ARRAY_END
  {
    if(ctx->parent)
      ctx->parent = ctx->parent->parent;
  }
  ;

list:
  TOK_LIST_START
  {
    if(IN_LIST())
    {
      ctx->parent = config_setting_add(ctx->parent, NULL, CONFIG_TYPE_LIST);
      CAPTURE_PARSE_POS(ctx->parent);
    }
    else
    {
      ctx->setting->type = CONFIG_TYPE_LIST;
      ctx->parent = ctx->setting;
      ctx->setting = NULL;
    }
  }
  value_list_optional
  TOK_LIST_END
  {
    if(ctx->parent)
      ctx->parent = ctx->parent->parent;
  }
  ;

value:
    simple_value
  | array
  | list
  | group
  ;

string:
  TOK_STRING { parsectx_append_string(ctx, $1); free($1); }
  | string TOK_STRING { parsectx_append_string(ctx, $2); free($2); }
  ;

simple_value:
    TOK_BOOLEAN
  {
    if(IN_ARRAY() || IN_LIST())
    {
      config_setting_t *e = config_setting_set_bool_elem(ctx->parent, -1,
                                                         (int)$1);

      if(! e)
      {
        libconfig_yyerror(scanner, ctx, scan_ctx, err_array_elem_type);
        YYABORT;
      }
      else
      {
        CAPTURE_PARSE_POS(e);
      }
    }
    else
      config_setting_set_bool(ctx->setting, (int)$1);
  }
  | TOK_INTEGER
  {
    if(IN_ARRAY() || IN_LIST())
    {
      config_setting_t *e = config_setting_set_int_elem(ctx->parent, -1, $1);
      if(! e)
      {
        libconfig_yyerror(scanner, ctx, scan_ctx, err_array_elem_type);
        YYABORT;
      }
      else
      {
        config_setting_set_format(e, CONFIG_FORMAT_DEFAULT);
        CAPTURE_PARSE_POS(e);
      }
    }
    else
    {
      config_setting_set_int(ctx->setting, $1);
      config_setting_set_format(ctx->setting, CONFIG_FORMAT_DEFAULT);
    }
  }
  | TOK_INTEGER64
  {
    if(IN_ARRAY() || IN_LIST())
    {
      config_setting_t *e = config_setting_set_int64_elem(ctx->parent, -1, $1);
      if(! e)
      {
        libconfig_yyerror(scanner, ctx, scan_ctx, err_array_elem_type);
        YYABORT;
      }
      else
      {
        config_setting_set_format(e, CONFIG_FORMAT_DEFAULT);
        CAPTURE_PARSE_POS(e);
      }
    }
    else
    {
      config_setting_set_int64(ctx->setting, $1);
      config_setting_set_format(ctx->setting, CONFIG_FORMAT_DEFAULT);
    }
  }
  | TOK_HEX
  {
    if(IN_ARRAY() || IN_LIST())
    {
      config_setting_t *e = config_setting_set_int_elem(ctx->parent, -1, $1);
      if(! e)
      {
        libconfig_yyerror(scanner, ctx, scan_ctx, err_array_elem_type);
        YYABORT;
      }
      else
      {
        config_setting_set_format(e, CONFIG_FORMAT_HEX);
        CAPTURE_PARSE_POS(e);
      }
    }
    else
    {
      config_setting_set_int(ctx->setting, $1);
      config_setting_set_format(ctx->setting, CONFIG_FORMAT_HEX);
    }
  }
  | TOK_HEX64
  {
    if(IN_ARRAY() || IN_LIST())
    {
      config_setting_t *e = config_setting_set_int64_elem(ctx->parent, -1, $1);
      if(! e)
      {
        libconfig_yyerror(scanner, ctx, scan_ctx, err_array_elem_type);
        YYABORT;
      }
      else
      {
        config_setting_set_format(e, CONFIG_FORMAT_HEX);
        CAPTURE_PARSE_POS(e);
      }
    }
    else
    {
      config_setting_set_int64(ctx->setting, $1);
      config_setting_set_format(ctx->setting, CONFIG_FORMAT_HEX);
    }
  }
  | TOK_FLOAT
  {
    if(IN_ARRAY() || IN_LIST())
    {
      config_setting_t *e = config_setting_set_float_elem(ctx->parent, -1, $1);
      if(! e)
      {
        libconfig_yyerror(scanner, ctx, scan_ctx, err_array_elem_type);
        YYABORT;
      }
      else
      {
        CAPTURE_PARSE_POS(e);
      }
    }
    else
      config_setting_set_float(ctx->setting, $1);
  }
  | string
  {
    if(IN_ARRAY() || IN_LIST())
    {
      const char *s = parsectx_take_string(ctx);
      config_setting_t *e = config_setting_set_string_elem(ctx->parent, -1, s);
      _delete(s);

      if(! e)
      {
        libconfig_yyerror(scanner, ctx, scan_ctx, err_array_elem_type);
        YYABORT;
      }
      else
      {
        CAPTURE_PARSE_POS(e);
      }
    }
    else
    {
      const char *s = parsectx_take_string(ctx);
      config_setting_set_string(ctx->setting, s);
      _delete(s);
    }
  }
  ;

value_list:
    value
  | value_list TOK_COMMA value
  ;

value_list_optional:
    /* empty */
  | value_list
  ;

simple_value_list:
    simple_value
  | simple_value_list TOK_COMMA simple_value
  ;

simple_value_list_optional:
    /* empty */
  | simple_value_list
  ;

group:
  TOK_GROUP_START
  {
    if(IN_LIST())
    {
      ctx->parent = config_setting_add(ctx->parent, NULL, CONFIG_TYPE_GROUP);
      CAPTURE_PARSE_POS(ctx->parent);
    }
    else
    {
      ctx->setting->type = CONFIG_TYPE_GROUP;
      ctx->parent = ctx->setting;
      ctx->setting = NULL;
    }
  }
  setting_list_optional
  TOK_GROUP_END
  {
    if(ctx->parent)
      ctx->parent = ctx->parent->parent;
  }
  ;

%%
