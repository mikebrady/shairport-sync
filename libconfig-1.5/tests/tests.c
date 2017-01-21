/* ----------------------------------------------------------------------------
   libconfig - A library for processing structured configuration files
   Copyright (C) 2005-2010  Mark A Lindner

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

#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#ifdef _MSC_VER
#define snprintf _snprintf
#endif

#include <libconfig.h>
#include <tinytest.h>

/* ------------------------------------------------------------------------- */

static void parse_and_compare(const char *input_file, const char *output_file)
{
  config_t cfg;
  int ok;

  config_init(&cfg);
  config_set_include_dir(&cfg, "./testdata");

  ok = config_read_file(&cfg, input_file);
  if(!ok)
  {
    printf("error: %s:%d\n", config_error_text(&cfg),
           config_error_line(&cfg));
  }
  TT_ASSERT_TRUE(ok);

  remove("temp.cfg");
  TT_ASSERT_TRUE(config_write_file(&cfg, "temp.cfg"));

  TT_ASSERT_TXTFILE_EQ("temp.cfg", output_file);
  remove("temp.cfg");

  config_destroy(&cfg);
}

static void parse_file_and_compare_error(const char *input_file,
                                         const char *parse_error)
{
  config_t cfg;
  char actual_error[128];
  char expected_error[128];

  config_init(&cfg);
  TT_ASSERT_FALSE(config_read_file(&cfg, input_file));

  snprintf(expected_error, sizeof(expected_error), "%s:%s",
           input_file, parse_error);

  snprintf(actual_error, sizeof(actual_error), "%s:%d %s\n",
           config_error_file(&cfg), config_error_line(&cfg),
           config_error_text(&cfg));

  config_destroy(&cfg);

  TT_ASSERT_STR_EQ(actual_error, expected_error);
}

static void parse_string_and_compare_error(const char *input_text,
                                           const char *parse_error)
{
  config_t cfg;
  char actual_error[128];
  char expected_error[128];

  config_init(&cfg);
  TT_ASSERT_FALSE(config_read_string(&cfg, input_text));

  snprintf(expected_error, sizeof(expected_error), "(null):%s", parse_error);

  snprintf(actual_error, sizeof(actual_error), "%s:%d %s\n",
           config_error_file(&cfg), config_error_line(&cfg),
           config_error_text(&cfg));

  config_destroy(&cfg);

  TT_ASSERT_STR_EQ(actual_error, expected_error);
}

static const char *read_file_to_string(const char *file)
{
  struct stat stbuf;
  FILE *fp;
  int size;
  char *buf;
  size_t r;

  TT_ASSERT_INT_EQ(0, stat(file, &stbuf));

  size = stbuf.st_size;
  buf = (char *)malloc(size + 1);

  fp = fopen(file, "rt");
  TT_ASSERT_PTR_NOTNULL(fp);

  r = fread(buf, 1, size, fp);
  fclose(fp);

  TT_ASSERT_INT_EQ(size, r);

  *(buf + size) = 0;
  return(buf);
}

/* ------------------------------------------------------------------------- */

TT_TEST(ParsingAndFormatting)
{
  int i;

  for(i = 0;; ++i)
  {
    char input_file[128], output_file[128];
    sprintf(input_file, "testdata/input_%d.cfg", i);
    sprintf(output_file, "testdata/output_%d.cfg", i);
    printf("parsing %s\n", input_file);

    if(!tt_file_exists(input_file) || !tt_file_exists(output_file))
      break;

    parse_and_compare(input_file, output_file);
  }
}

/* ------------------------------------------------------------------------- */

TT_TEST(ParseInvalidFiles)
{
  int i;

  for(i = 0;; ++i)
  {
    char input_file[128], error_file[128];
    char error_text[128];
    FILE *fp;

    sprintf(input_file, "testdata/bad_input_%d.cfg", i);
    sprintf(error_file, "testdata/parse_error_%d.txt", i);

    if(!tt_file_exists(input_file) || !tt_file_exists(error_file))
      break;

    fp = fopen(error_file, "rt");
    TT_ASSERT_PTR_NOTNULL(fp);
    TT_ASSERT_PTR_NOTNULL(fgets(error_text, sizeof(error_text), fp));
    fclose(fp);

    parse_file_and_compare_error(input_file, error_text);
  }
}

/* ------------------------------------------------------------------------- */

TT_TEST(ParseInvalidStrings)
{
  int i;

  for(i = 0;; ++i)
  {
    char input_file[128], error_file[128];
    const char *input_text;
    char error_text[128];
    FILE *fp;

    sprintf(input_file, "testdata/bad_input_%d.cfg", i);
    sprintf(error_file, "testdata/parse_error_%d.txt", i);

    if(!tt_file_exists(input_file) || !tt_file_exists(error_file))
      break;

    input_text = read_file_to_string(input_file);

    fp = fopen(error_file, "rt");
    TT_ASSERT_PTR_NOTNULL(fp);
    TT_ASSERT_PTR_NOTNULL(fgets(error_text, sizeof(error_text), fp));
    fclose(fp);

    parse_string_and_compare_error(input_text, error_text);

    free((void *)input_text);
  }
}

/* ------------------------------------------------------------------------- */

int main(int argc, char **argv)
{
  TT_SUITE_START(LibConfigTests);
  TT_SUITE_TEST(LibConfigTests, ParsingAndFormatting);
  TT_SUITE_TEST(LibConfigTests, ParseInvalidFiles);
  TT_SUITE_TEST(LibConfigTests, ParseInvalidStrings);
  TT_SUITE_RUN(LibConfigTests);
  TT_SUITE_END(LibConfigTests);

  return(0);
}
