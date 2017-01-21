/* -*- mode: C -*- */
/* --------------------------------------------------------------------------
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

%option nounistd
%option never-interactive
%option reentrant
%option noyywrap
%option yylineno
%option nounput
%option bison-bridge
%option header-file="scanner.h"
%option outfile="lex.yy.c"
%option extra-type="struct scan_context *"

%{

#ifdef _MSC_VER
#pragma warning (disable: 4996)
#endif

#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include "parsectx.h"
#include "scanctx.h"
#include "grammar.h"
#include "wincompat.h"

#define YY_NO_INPUT // Suppress generation of useless input() function

static unsigned long long fromhex(const char *s)
{
#ifdef __MINGW32__

  /* MinGW's strtoull() seems to be broken; it only returns the lower
   * 32 bits...
   */

  const char *p = s;
  unsigned long long val = 0;

  if(*p != '0')
    return(0);

  ++p;

  if(*p != 'x' && *p != 'X')
    return(0);

  for(++p; isxdigit(*p); ++p)
  {
    val <<= 4;
    val |= ((*p < 'A') ? (*p & 0xF) : (9 + (*p & 0x7)));
  }

  return(val);

#else /* ! __MINGW32__ */

  return(strtoull(s, NULL, 16));

#endif /* __MINGW32__ */
}

%}

true              [Tt][Rr][Uu][Ee]
false             [Ff][Aa][Ll][Ss][Ee]
name              [A-Za-z\*][-A-Za-z0-9_\*]*
integer           [-+]?[0-9]+
integer64         [-+]?[0-9]+L(L)?
hex               0[Xx][0-9A-Fa-f]+
hex64             0[Xx][0-9A-Fa-f]+L(L)?
hexchar           \\[Xx][0-9A-Fa-f]{2}
float             ([-+]?([0-9]*)?\.[0-9]*([eE][-+]?[0-9]+)?)|([-+]?([0-9]+)(\.[0-9]*)?[eE][-+]?[0-9]+)
comment           (#|\/\/).*$
include_open      ^[ \t]*@include[ \t]+\"

%x COMMENT STRING INCLUDE

%%

\/\*              { BEGIN COMMENT; }
<COMMENT>\*\/     { BEGIN INITIAL; }
<COMMENT>.        { /* ignore */ }
<COMMENT>\n       { /* ignore */ }

\"                { BEGIN STRING; }
<STRING>[^\"\\]+  { scanctx_append_string(yyextra, yytext); }
<STRING>\\n       { scanctx_append_string(yyextra, "\n"); }
<STRING>\\r       { scanctx_append_string(yyextra, "\r"); }
<STRING>\\t       { scanctx_append_string(yyextra, "\t"); }
<STRING>\\f       { scanctx_append_string(yyextra, "\f"); }
<STRING>\\\\      { scanctx_append_string(yyextra, "\\"); }
<STRING>\\\"      { scanctx_append_string(yyextra, "\""); }
<STRING>{hexchar} {
                    char c[2] = { (char)(strtol(yytext + 2, NULL, 16) & 0xFF),
                                  0 };
                    scanctx_append_string(yyextra, c);
                  }
<STRING>\\        { scanctx_append_string(yyextra, "\\"); }
<STRING>\"        {
                    yylval->sval = scanctx_take_string(yyextra);
                    BEGIN INITIAL;
                    return(TOK_STRING);
                  }

{include_open}    { BEGIN INCLUDE; }
<INCLUDE>[^\"\\]+ { scanctx_append_string(yyextra, yytext); }
<INCLUDE>\\\\     { scanctx_append_string(yyextra, "\\"); }
<INCLUDE>\\\"     { scanctx_append_string(yyextra, "\""); }
<INCLUDE>\"       {
                    const char *error;
                    FILE *fp = scanctx_push_include(yyextra,
                                                    (void *)YY_CURRENT_BUFFER,
                                                    &error);
                    if(fp)
                    {
                      yyin = fp;
                      yy_switch_to_buffer(
                        yy_create_buffer(yyin, YY_BUF_SIZE, yyscanner),
                        yyscanner);
                    }
                    else
                    {
                      yyextra->config->error_text = error;
                      yyextra->config->error_file = scanctx_current_filename(
                        yyextra);
                      yyextra->config->error_line = libconfig_yyget_lineno(
                        yyscanner);
                      return TOK_ERROR;
                    }
                    BEGIN INITIAL;
                  }

\n|\r|\f          { /* ignore */ }
[ \t]+            { /* ignore */ }

\=|\:             { return(TOK_EQUALS); }
,                 { return(TOK_COMMA); }
\{                { return(TOK_GROUP_START); }
\}                { return(TOK_GROUP_END); }
{true}            { yylval->ival = 1; return(TOK_BOOLEAN); }
{false}           { yylval->ival = 0; return(TOK_BOOLEAN); }
{name}            { yylval->sval = yytext; return(TOK_NAME); }
{float}           { yylval->fval = atof(yytext); return(TOK_FLOAT); }
{integer}         { yylval->ival = atoi(yytext); return(TOK_INTEGER); }
{integer64}       { yylval->llval = atoll(yytext); return(TOK_INTEGER64); }
{hex}             {
                    yylval->ival = strtoul(yytext, NULL, 16);
                    return(TOK_HEX);
                  }
{hex64}           { yylval->llval = fromhex(yytext); return(TOK_HEX64); }
\[                { return(TOK_ARRAY_START); }
\]                { return(TOK_ARRAY_END); }
\(                { return(TOK_LIST_START); }
\)                { return(TOK_LIST_END); }
;                 { return(TOK_SEMICOLON); }
{comment}         { /* ignore */ }
.                 { return(TOK_GARBAGE); }

<<EOF>>           {
                    YY_BUFFER_STATE buf = (YY_BUFFER_STATE)scanctx_pop_include(
                      yyextra);
                    if(buf)
                    {
                      yy_delete_buffer(YY_CURRENT_BUFFER, yyscanner);
                      yy_switch_to_buffer(buf, yyscanner);
                    }
                    else
                      yyterminate();
                  }
