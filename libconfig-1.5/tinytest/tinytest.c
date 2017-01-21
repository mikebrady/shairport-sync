/* ----------------------------------------------------------------------------
   tinytest - A tiny C unit-testing library
   Copyright (C) 2010-2014  Mark A Lindner

   This file is part of tinytest.

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

#include "tinytest.h"

#include <stdarg.h>
#include <stdio.h>

#ifdef WIN32
#include <io.h>
#define F_OK 4  // Windows doesn't have F_OK
#else
#include <unistd.h>
#endif

/*
 */

static tt_testsuite_t *__tt_current_suite = NULL;

static const char *__tt_op_strings[] = { "==", "!=", "<", "<=", ">", ">=",
                                         "==", "!=", "<", "<=", ">", ">=",
                                         "==", "!=", "<", "<=", ">", ">=",
                                         "==", "!=", "<", "<=", ">", ">=",
                                         "==", "!=", "<", "<=", ">", ">=",
                                         "==", "!=", "<", "<=", ">", ">=",
                                         "==", "!=", "", "!",
                                         "==file", "!=file",
                                         "==txtfile", "!=txtfile" };

/*
 */

static tt_bool_t __tt_chop(char *s)
{
  size_t len = strlen(s);
  char *p;
  tt_bool_t eol = TT_FALSE;

  for(p = s + len - 1; p > s; --p) {
    if ((*p == '\r') || (*p == '\n')) {
      eol = TT_TRUE;
      *p = 0;
    }
  }

  return(eol);
}

static tt_bool_t __tt_compare_files_text(const char *file1, const char *file2,
                                         tt_bool_t verbose)
{
  FILE *fp1, *fp2;
  char buf1[4096], buf2[4096];
  int line = 1;
  int character = 0;
  tt_bool_t matched = TT_TRUE, eof1, eof2;

  if(!(fp1 = fopen(file1, "rt")))
  {
    printf("cannot open file: %s\n", file1);
    return(TT_FALSE);
  }

  if(!(fp2 = fopen(file2, "rt")))
  {
    fclose(fp1);
    printf("cannot open file: %s\n", file2);
    return(TT_FALSE);
  }

  for(;;)
  {
    char *r1, *r2, *p, *q;

    r1 = fgets(buf1, sizeof(buf1), fp1);
    r2 = fgets(buf2, sizeof(buf2), fp2);

    if((r1 == NULL) || (r2 == NULL))
    {
      matched = (r1 == r2) ? TT_TRUE : TT_FALSE;
      break;
    }

    eof1 = __tt_chop(r1);
    eof2 = __tt_chop(r2);

    p = r1;
    q = r2;

    while(*p && *q)
    {
      if(*p != *q)
      {
        matched = TT_FALSE;
        break;
      }

      ++character;
      ++p;
      ++q;
    }

    if(eof1 != eof2)
      matched = TT_FALSE;

    if(matched == TT_FALSE)
      break;

    if(eof1 && eof2)
    {
      ++line;
      character = 0;
    }
  }

  fclose(fp1);
  fclose(fp2);

  if(!matched && verbose)
    printf("files \"%s\" and \"%s\" differ starting at line %d, char %d\n",
           file1, file2, line, character);

  return(matched);
}

static tt_bool_t __tt_compare_files(const char *file1, const char *file2,
                                    tt_bool_t verbose)
{
  FILE *fp1, *fp2;
  char buf1[4096], buf2[4096];
  int line = 1;
  int character = 0;
  size_t r1, r2;
  tt_bool_t done = TT_FALSE, matched = TT_TRUE;

  if(!(fp1 = fopen(file1, "rb")))
  {
    printf("cannot open file: %s\n", file1);
    return(TT_FALSE);
  }

  if(!(fp2 = fopen(file2, "rb")))
  {
    fclose(fp1);
    printf("cannot open file: %s\n", file2);
    return(TT_FALSE);
  }

  while(!done)
  {
    char *p, *q, *pe, *qe;

    r1 = fread(buf1, 1, sizeof(buf1), fp1);
    r2 = fread(buf2, 1, sizeof(buf2), fp2);

    p = buf1;
    q = buf2;
    pe = buf1 + r1;
    qe = buf2 + r2;

    while(p < pe && q < qe)
    {
      if(*p != *q)
      {
        matched = TT_FALSE;
        done = TT_TRUE;
        break;
      }

      if(*p == '\n')
      {
        ++line;
        character = 0;
      }
      else
        ++character;

      ++p;
      ++q;
    }

    if(p < pe || q < qe)
    {
      matched = TT_FALSE;
      break;
    }

    if(feof(fp1) || feof(fp2))
      break;
  }

  fclose(fp1);
  fclose(fp2);

  if(!matched && verbose)
    printf("files \"%s\" and \"%s\" differ starting at line %d, char %d\n",
           file1, file2, line, character);

  return(matched);
}

/*
 */

tt_testsuite_t *tt_suite_create(const char *name)
{
  tt_testsuite_t *suite = calloc(1, sizeof(tt_testsuite_t));
  suite->name = strdup(name);
  return(suite);
}

/*
 */

void tt_suite_destroy(tt_testsuite_t *suite)
{
  tt_test_t *test = suite->first_test;

  while(test)
  {
    tt_test_t *tmp = test->next;
    free((void *)test->name);
    free(test);
    test = tmp;
  }

  free((void *)suite->name);
  free(suite);
}

/*
 */

void tt_suite_add_test(tt_testsuite_t *suite, const char *name,
                       void (*function)(void))
{
  tt_test_t *test = calloc(1, sizeof(tt_test_t));
  test->name = strdup(name);
  test->function = function;

  if(suite->last_test != NULL)
    suite->last_test->next = test;

  suite->last_test = test;

  if(suite->first_test == NULL)
    suite->first_test = test;

  ++suite->num_tests;
}

/*
 */

void tt_suite_run(tt_testsuite_t *suite)
{
  __tt_current_suite = suite;

  suite->num_failures = 0;

  for(suite->current_test = suite->first_test;
      suite->current_test;
      suite->current_test = suite->current_test->next)
  {
    printf("[TEST] %s\n", suite->current_test->name);

    if(setjmp(suite->jump_buf) == 0)
    {
      suite->current_test->function();
    }

    if(suite->current_test->failed)
    {
      printf("[FAIL] %s\n", suite->current_test->name);
      ++suite->num_failures;
    }
    else
    {
      printf("[ OK ] %s\n", suite->current_test->name);
    }
  }

  if(suite->num_failures > 0)
    puts("*** FAILURES! ***");

  printf("%d tests; %d passed, %d failed\n",
         suite->num_tests, suite->num_tests - suite->num_failures,
         suite->num_failures);

  suite->current_test = NULL;
  __tt_current_suite = NULL;
}

/*
 */

void tt_output_val(FILE *stream, const tt_val_t *val)
{
  switch(val->type)
  {
    case TT_VAL_INT:
      fprintf(stream, "%d", val->value.int_val);
      break;

    case TT_VAL_UINT:
      fprintf(stream, "%u", val->value.uint_val);
      break;

    case TT_VAL_INT64:
      fprintf(stream, "%lld", val->value.int64_val);
      break;

    case TT_VAL_UINT64:
      fprintf(stream, "%llu", val->value.uint64_val);
      break;

    case TT_VAL_DOUBLE:
      fprintf(stream, "%f", val->value.double_val);
      break;

    case TT_VAL_STR:
    {
      const char *p;

      fputc('\"', stream);
      for(p = val->value.str_val; *p; ++p)
      {
        if(*p == '\n')
          fputs("\\n", stream);
        else if(*p == '\r')
          fputs("\\r", stream);
        else if(*p == '\t')
          fputs("\\t", stream);
        else if(*p == '\f')
          fputs("\\f", stream);
        else if(*p < ' ')
          fprintf(stream, "\\0x%02X", *p);
        else
          fputc(*p, stream);
      }
      fputc('\"', stream);
      break;
    }

    case TT_VAL_PTR:
      fprintf(stream, "%p", val->value.ptr_val);
      break;

    default:
      fputs("???", stream);
      break;
  }
}

/*
 */

void tt_expect(const char *file, int line, const char *aexpr,
               tt_op_t op, const char *bexpr, const tt_val_t a,
               const tt_val_t b, tt_bool_t fatal)
{
  tt_bool_t result = TT_FALSE;

  switch(op)
  {
    case TT_OP_INT_EQ:
      result = (a.value.int_val == b.value.int_val);
      break;

    case TT_OP_INT_NE:
      result = (a.value.int_val != b.value.int_val);
      break;

    case TT_OP_INT_LT:
      result = (a.value.int_val < b.value.int_val);
      break;

    case TT_OP_INT_LE:
      result = (a.value.int_val <= b.value.int_val);
      break;

    case TT_OP_INT_GT:
      result = (a.value.int_val > b.value.int_val);
      break;

    case TT_OP_INT_GE:
      result = (a.value.int_val >= b.value.int_val);
      break;

    case TT_OP_UINT_EQ:
      result = (a.value.uint_val == b.value.uint_val);
      break;

    case TT_OP_UINT_NE:
      result = (a.value.uint_val != b.value.uint_val);
      break;

    case TT_OP_UINT_LT:
      result = (a.value.uint_val < b.value.uint_val);
      break;

    case TT_OP_UINT_LE:
      result = (a.value.uint_val <= b.value.uint_val);
      break;

    case TT_OP_UINT_GT:
      result = (a.value.uint_val > b.value.uint_val);
      break;

    case TT_OP_UINT_GE:
      result = (a.value.uint_val >= b.value.uint_val);
      break;

    case TT_OP_INT64_EQ:
      result = (a.value.int64_val == b.value.int64_val);
      break;

    case TT_OP_INT64_NE:
      result = (a.value.int64_val != b.value.int64_val);
      break;

    case TT_OP_INT64_LT:
      result = (a.value.int64_val < b.value.int64_val);
      break;

    case TT_OP_INT64_LE:
      result = (a.value.int64_val <= b.value.int64_val);
      break;

    case TT_OP_INT64_GT:
      result = (a.value.int64_val > b.value.int64_val);
      break;

    case TT_OP_INT64_GE:
      result = (a.value.int64_val >= b.value.int64_val);
      break;

    case TT_OP_UINT64_EQ:
      result = (a.value.uint64_val == b.value.uint64_val);
      break;

    case TT_OP_UINT64_NE:
      result = (a.value.uint64_val != b.value.uint64_val);
      break;

    case TT_OP_UINT64_LT:
      result = (a.value.uint64_val < b.value.uint64_val);
      break;

    case TT_OP_UINT64_LE:
      result = (a.value.uint64_val <= b.value.uint64_val);
      break;

    case TT_OP_UINT64_GT:
      result = (a.value.uint64_val > b.value.uint64_val);
      break;

    case TT_OP_UINT64_GE:
      result = (a.value.uint64_val >= b.value.uint64_val);
      break;

    case TT_OP_DOUBLE_EQ:
      result = (a.value.double_val == b.value.double_val);
      break;

    case TT_OP_DOUBLE_NE:
    {
      double diff = (a.value.double_val - b.value.double_val);
      result = ((diff < -.0001) || (diff > .0001));
      break;
    }

    case TT_OP_DOUBLE_LT:
      result = (a.value.double_val < b.value.double_val);
      break;

    case TT_OP_DOUBLE_LE:
      result = (a.value.double_val <= b.value.double_val);
      break;

    case TT_OP_DOUBLE_GT:
      result = (a.value.double_val > b.value.double_val);
      break;

    case TT_OP_DOUBLE_GE:
      result = (a.value.double_val >= b.value.double_val);
      break;

    case TT_OP_STR_EQ:
      result = !strcmp(a.value.str_val, b.value.str_val);
      break;

    case TT_OP_STR_NE:
      result = strcmp(a.value.str_val, b.value.str_val);
      break;

    case TT_OP_STR_LT:
      result = (strcmp(a.value.str_val, b.value.str_val) < 0);
      break;

    case TT_OP_STR_LE:
      result = (strcmp(a.value.str_val, b.value.str_val) <= 0);
      break;

    case TT_OP_STR_GT:
      result = (strcmp(a.value.str_val, b.value.str_val) > 0);
      break;

    case TT_OP_STR_GE:
      result = (strcmp(a.value.str_val, b.value.str_val) >= 0);
      break;

    case TT_OP_PTR_EQ:
      result = (a.value.ptr_val == b.value.ptr_val);
      break;

    case TT_OP_PTR_NE:
      result = (a.value.ptr_val != b.value.ptr_val);
      break;

    case TT_OP_FILE_EQ:
      result = __tt_compare_files(a.value.str_val, b.value.str_val, TT_TRUE);
      break;

    case TT_OP_FILE_NE:
      result = !__tt_compare_files(a.value.str_val, b.value.str_val, TT_TRUE);
      break;

    case TT_OP_TXTFILE_EQ:
      result = __tt_compare_files_text(a.value.str_val, b.value.str_val,
                                       TT_TRUE);
      break;

    case TT_OP_TXTFILE_NE:
      result = !__tt_compare_files_text(a.value.str_val, b.value.str_val,
                                        TT_TRUE);
      break;

    default:
      break;
  }

  if(!result)
  {
    __tt_current_suite->current_test->failed = TT_TRUE;

    printf("%s:%d: failed %s: %s [", file, line, (fatal ? "assert" : "expect"),
           aexpr);
    tt_output_val(stdout, &a);
    printf("] %s %s [", __tt_op_strings[op], bexpr);
    tt_output_val(stdout, &b);
    puts("]");

    if(fatal)
      longjmp(__tt_current_suite->jump_buf, 0);
  }
}

/*
 */

void tt_expect_bool(const char *file, int line, const char *expr, tt_op_t op,
                    int val, tt_bool_t fatal)
{
  tt_bool_t result = TT_FALSE;

  switch(op)
  {
    case TT_OP_TRUE:
      result = (val != 0);
      break;

    case TT_OP_FALSE:
      result = (val == 0);
      break;

    default:
      break;
  }

  if(!result)
  {
    __tt_current_suite->current_test->failed = TT_TRUE;

    printf("%s:%d: failed %s: %s(%s)\n", file, line,
           (fatal ? "assert" : "expect"), __tt_op_strings[op], expr);

    if(fatal)
      longjmp(__tt_current_suite->jump_buf, 0);
  }
}

/*
 */

void tt_fail(const char *file, int line, const char *message, ...)
{
  va_list vp;
  va_start(vp, message);
  printf("%s:%d: failed: ", file, line);
  vprintf(message, vp);
  va_end(vp);
  putchar('\n');
}

/*
 */

tt_bool_t tt_file_exists(const char *file)
{
  return(access(file, F_OK) == 0);
}

#ifdef _MSC_VER

// All of this extra code is because MSVC doesn't support the C99 standard.
// Sigh.

/*
 */

void tt_test_int(const char *file, int line, const char *aexpr, tt_op_t op,
                 const char *bexpr, int a, int b, tt_bool_t fatal)
{
  tt_val_t aval = { TT_VAL_INT }, bval = { TT_VAL_INT };
  aval.value.int_val = a;
  bval.value.int_val = b;
  tt_expect(file, line, aexpr, op, bexpr, aval, bval, fatal);
}

/*
 */

void tt_test_uint(const char *file, int line, const char *aexpr, tt_op_t op,
                  const char *bexpr, unsigned int a, unsigned int b,
                  tt_bool_t fatal)
{
  tt_val_t aval = { TT_VAL_UINT }, bval = { TT_VAL_UINT };
  aval.value.uint_val = a;
  bval.value.uint_val = b;
  tt_expect(file, line, aexpr, op, bexpr, aval, bval, fatal);
}

/*
 */

void tt_test_int64(const char *file, int line, const char *aexpr, tt_op_t op,
                   const char *bexpr, long long a, long long b,
                   tt_bool_t fatal)
{
  tt_val_t aval = { TT_VAL_INT64 }, bval = { TT_VAL_INT64 };
  aval.value.int64_val = a;
  bval.value.int64_val = b;
  tt_expect(file, line, aexpr, op, bexpr, aval, bval, fatal);
}

/*
 */

void tt_test_uint64(const char *file, int line, const char *aexpr,
                    tt_op_t op, const char *bexpr, unsigned long long a,
                    unsigned long long b, tt_bool_t fatal)
{
  tt_val_t aval = { TT_VAL_UINT64 }, bval = { TT_VAL_UINT64 };
  aval.value.uint64_val = a;
  bval.value.uint64_val = b;
  tt_expect(file, line, aexpr, op, bexpr, aval, bval, fatal);
}

/*
 */

void tt_test_double(const char *file, int line, const char *aexpr, tt_op_t op,
                    const char *bexpr, double a, double b, tt_bool_t fatal)
{
  tt_val_t aval = { TT_VAL_DOUBLE }, bval = { TT_VAL_DOUBLE };
  aval.value.double_val = a;
  bval.value.double_val = b;
  tt_expect(file, line, aexpr, op, bexpr, aval, bval, fatal);
}

/*
 */

void tt_test_str(const char *file, int line, const char *aexpr, tt_op_t op,
                 const char *bexpr, const char *a, const char *b,
                 tt_bool_t fatal)
{
  tt_val_t aval = { TT_VAL_STR }, bval = { TT_VAL_STR };
  aval.value.str_val = a;
  bval.value.str_val = b;
  tt_expect(file, line, aexpr, op, bexpr, aval, bval, fatal);
}

/*
 */

void tt_test_ptr(const char *file, int line, const char *aexpr, tt_op_t op,
                 const char *bexpr, const void *a, const void *b,
                 tt_bool_t fatal)
{
  tt_val_t aval = { TT_VAL_PTR }, bval = { TT_VAL_PTR };
  aval.value.ptr_val = a;
  bval.value.ptr_val = b;
  tt_expect(file, line, aexpr, op, bexpr, aval, bval, fatal);
}

#endif // WIN32

/* end of source file */
