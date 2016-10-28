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

#ifndef __tinytest_h
#define __tinytest_h

#include <setjmp.h>
#include <string.h>
#include <stdlib.h>

typedef int tt_bool_t;

#define TT_TRUE (1)
#define TT_FALSE (0)

typedef struct tt_test_t
{
  const char *name;
  void (*function)(void);
  tt_bool_t failed;
  struct tt_test_t *next;
} tt_test_t;

typedef struct tt_testsuite_t
{
  const char *name;
  tt_test_t *first_test;
  tt_test_t *last_test;
  tt_test_t *current_test;
  int num_tests;
  int num_failures;
  jmp_buf jump_buf;
} tt_testsuite_t;

typedef enum { TT_VAL_INT, TT_VAL_UINT, TT_VAL_INT64, TT_VAL_UINT64,
               TT_VAL_DOUBLE, TT_VAL_STR, TT_VAL_PTR } tt_valtype_t;

typedef enum { TT_OP_INT_EQ, TT_OP_INT_NE, TT_OP_INT_LT, TT_OP_INT_LE,
               TT_OP_INT_GT, TT_OP_INT_GE, TT_OP_UINT_EQ, TT_OP_UINT_NE,
               TT_OP_UINT_LT, TT_OP_UINT_LE, TT_OP_UINT_GT, TT_OP_UINT_GE,
               TT_OP_INT64_EQ, TT_OP_INT64_NE, TT_OP_INT64_LT, TT_OP_INT64_LE,
               TT_OP_INT64_GT, TT_OP_INT64_GE, TT_OP_UINT64_EQ,
               TT_OP_UINT64_NE, TT_OP_UINT64_LT, TT_OP_UINT64_LE,
               TT_OP_UINT64_GT, TT_OP_UINT64_GE, TT_OP_DOUBLE_EQ,
               TT_OP_DOUBLE_NE, TT_OP_DOUBLE_LT, TT_OP_DOUBLE_LE,
               TT_OP_DOUBLE_GT, TT_OP_DOUBLE_GE, TT_OP_STR_EQ, TT_OP_STR_NE,
               TT_OP_STR_LT, TT_OP_STR_LE, TT_OP_STR_GT, TT_OP_STR_GE,
               TT_OP_PTR_EQ, TT_OP_PTR_NE, TT_OP_TRUE, TT_OP_FALSE,
               TT_OP_FILE_EQ, TT_OP_FILE_NE, TT_OP_TXTFILE_EQ,
               TT_OP_TXTFILE_NE } tt_op_t;

typedef struct tt_val_t
{
  tt_valtype_t type;
  union {
    int int_val;
    unsigned int uint_val;
    long long int64_val;
    unsigned long long uint64_val;
    double double_val;
    const char *str_val;
    const void *ptr_val;
  } value;
} tt_val_t;


extern tt_testsuite_t *tt_suite_create(const char *name);

extern void tt_suite_destroy(tt_testsuite_t *suite);

extern void tt_suite_add_test(tt_testsuite_t *suite, const char *name,
                              void (*function)(void));

extern void tt_suite_run(tt_testsuite_t *suite);

extern void tt_expect(const char *file, int line, const char *aexpr,
                      tt_op_t op, const char *bexpr,
                      const tt_val_t a, const tt_val_t b,
                      tt_bool_t fatal);

extern void tt_expect_bool(const char *file, int line, const char *expr,
                           tt_op_t op, int val, tt_bool_t fatal);

extern void tt_fail(const char *file, int line, const char *message, ...);

extern tt_bool_t tt_file_exists(const char *file);

#ifdef _MSC_VER

extern void tt_test_int(const char *file, int line, const char *aexpr,
                        tt_op_t op, const char *bexpr, int a, int b,
                        tt_bool_t fatal);

#define TT_TEST_INT_(A, OP, B, F)                               \
  tt_test_int(__FILE__, __LINE__, #A, (OP), #B, (A), (B), (F))

#else

#define TT_TEST_INT_(A, OP, B, F)                                       \
  tt_expect(__FILE__, __LINE__, #A, (OP), #B,                           \
            (tt_val_t){ TT_VAL_INT, .value.int_val = (A) },             \
            (tt_val_t){ TT_VAL_INT, .value.int_val = (B) },             \
            (F))

#endif

#define TT_EXPECT_INT_EQ(A, B)                          \
  TT_TEST_INT_((A), TT_OP_INT_EQ, (B), TT_FALSE)

#define TT_ASSERT_INT_EQ(A, B)                  \
  TT_TEST_INT_((A), TT_OP_INT_EQ, (B), TT_TRUE)

#define TT_EXPECT_INT_NE(A, B)                          \
  TT_TEST_INT_((A), TT_OP_INT_NE, (B), TT_FALSE)

#define TT_ASSERT_INT_NE(A, B)                  \
  TT_TEST_INT_((A), TT_OP_INT_NE, (B), TT_TRUE)

#define TT_EXPECT_INT_LT(A, B)                  \
  TT_TEST_INT_((A), TT_OP_INT_LT, (B), TT_FALSE)

#define TT_ASSERT_INT_LT(A, B)                  \
  TT_TEST_INT_((A), TT_OP_INT_LT, (B), TT_TRUE)

#define TT_EXPECT_INT_LE(A, B)                  \
  TT_TEST_INT_((A), TT_OP_INT_LE, (B), TT_FALSE)

#define TT_ASSERT_INT_LE(A, B)                  \
  TT_TEST_INT_((A), TT_OP_INT_LE, (B), TT_TRUE)

#define TT_EXPECT_INT_GT(A, B)                  \
  TT_TEST_INT_((A), TT_OP_INT_GT, (B), TT_FALSE)

#define TT_ASSERT_INT_GT(A, B)                  \
  TT_TEST_INT_((A), TT_OP_INT_GT, (B), TT_TRUE)

#define TT_EXPECT_INT_GE(A, B)                  \
  TT_TEST_INT_((A), TT_OP_INT_GE, (B), TT_FALSE)

#define TT_ASSERT_INT_GE(A, B)                  \
  TT_TEST_INT_((A), TT_OP_INT_GE, (B), TT_TRUE)

#ifdef _MSC_VER

extern void tt_test_uint(const char *file, int line, const char *aexpr,
                         tt_op_t op, const char *bexpr, unsigned int a,
                         unsigned int b, tt_bool_t fatal);

#define TT_TEST_UINT_(A, OP, B, F)                              \
  tt_test_uint(__FILE__, __LINE__, #A, (OP), #B, (A), (B), (F))

#else

#define TT_TEST_UINT_(A, OP, B, F)                                      \
  tt_expect(__FILE__, __LINE__, #A, OP, #B,                             \
            (tt_val_t){ TT_VAL_UINT, .value.uint_val = (A) },           \
            (tt_val_t){ TT_VAL_UINT, .value.uint_val = (B) },           \
            (F))

#endif

#define TT_EXPECT_UINT_EQ(A, B)                  \
  TT_TEST_UINT_((A), TT_OP_UINT_EQ, (B), TT_FALSE)

#define TT_ASSERT_UINT_EQ(A, B)                  \
  TT_TEST_UINT_((A), TT_OP_UINT_EQ, (B), TT_TRUE)

#define TT_EXPECT_UINT_NE(A, B)                  \
  TT_TEST_UINT_((A), TT_OP_UINT_NE, (B), TT_FALSE)

#define TT_ASSERT_UINT_NE(A, B)                  \
  TT_TEST_UINT_((A), TT_OP_UINT_NE, (B), TT_TRUE)

#define TT_EXPECT_UINT_LT(A, B)                  \
  TT_TEST_UINT_((A), TT_OP_UINT_LT, (B), TT_FALSE)

#define TT_ASSERT_UINT_LT(A, B)                  \
  TT_TEST_UINT_((A), TT_OP_UINT_LT, (B), TT_TRUE)

#define TT_EXPECT_UINT_LE(A, B)                  \
  TT_TEST_UINT_((A), TT_OP_UINT_LE, (B), TT_FALSE)

#define TT_ASSERT_UINT_LE(A, B)                  \
  TT_TEST_UINT_((A), TT_OP_UINT_LE, (B), TT_TRUE)

#define TT_EXPECT_UINT_GT(A, B)                  \
  TT_TEST_UINT_((A), TT_OP_UINT_GT, (B), TT_FALSE)

#define TT_ASSERT_UINT_GT(A, B)                  \
  TT_TEST_UINT_((A), TT_OP_UINT_GT, (B), TT_TRUE)

#define TT_EXPECT_UINT_GE(A, B)                  \
  TT_TEST_UINT_((A), TT_OP_UINT_GE, (B), TT_FALSE)

#define TT_ASSERT_UINT_GE(A, B)                  \
  TT_TEST_UINT_((A), TT_OP_UINT_GE, (B), TT_TRUE)

#ifdef _MSC_VER

extern void tt_test_int64(const char *file, int line, const char *aexpr,
                          tt_op_t op, const char *bexpr, long long a,
                          long long b, tt_bool_t fatal);

#define TT_TEST_INT64_(A, OP, B, F)                                      \
  tt_test_int64(__FILE__, __LINE__, #A, (OP), #B, (A), (B), (F))

#else

#define TT_TEST_INT64_(A, OP, B, F)                                     \
  tt_expect(__FILE__, __LINE__, #A, OP, #B,                             \
            (tt_val_t){ TT_VAL_INT64, .value.int64_val = (A) },         \
            (tt_val_t){ TT_VAL_INT64, .value.int64_val = (B) },         \
            (F))

#endif

#define TT_EXPECT_INT64_EQ(A, B)                        \
  TT_TEST_INT64_((A), TT_OP_INT64_EQ, (B), TT_FALSE)

#define TT_ASSERT_INT64_EQ(A, B)                \
  TT_TEST_INT64_((A), TT_OP_INT64_EQ, (B), TT_TRUE)

#define TT_EXPECT_INT64_NE(A, B)                        \
  TT_TEST_INT64_((A), TT_OP_INT64_NE, (B), TT_FALSE)

#define TT_ASSERT_INT64_NE(A, B)                \
  TT_TEST_INT64_((A), TT_OP_INT64_NE, (B), TT_TRUE)

#define TT_EXPECT_INT64_LT(A, B)                        \
  TT_TEST_INT64_((A), TT_OP_INT64_LT, (B), TT_FALSE)

#define TT_ASSERT_INT64_LT(A, B)                \
  TT_TEST_INT64_((A), TT_OP_INT64_LT, (B), TT_TRUE)

#define TT_EXPECT_INT64_LE(A, B)                        \
  TT_TEST_INT64_((A), TT_OP_INT64_LE, (B), TT_FALSE)

#define TT_ASSERT_INT64_LE(A, B)                \
  TT_TEST_INT64_((A), TT_OP_INT64_LE, (B), TT_TRUE)

#define TT_EXPECT_INT64_GT(A, B)                        \
  TT_TEST_INT64_((A), TT_OP_INT64_GT, (B), TT_FALSE)

#define TT_ASSERT_INT64_GT(A, B)                \
  TT_TEST_INT64_((A), TT_OP_INT64_GT, (B), TT_TRUE)

#define TT_EXPECT_INT64_GE(A, B)                        \
  TT_TEST_INT64_((A), TT_OP_INT64_GE, (B), TT_FALSE)

#define TT_ASSERT_INT64_GE(A, B)                \
  TT_TEST_INT64_((A), TT_OP_INT64_GE, (B), TT_TRUE)

#ifdef _MSC_VER

extern void tt_test_uint64(const char *file, int line, const char *aexpr,
                           tt_op_t op, const char *bexpr,
                           unsigned long long a, unsigned long long b,
                           tt_bool_t fatal);

#define TT_TEST_UINT64_(A, OP, B, F)                                      \
  tt_test_uint64(__FILE__, __LINE__, #A, (OP), #B, (A), (B), (F))

#else

#define TT_TEST_UINT64_(A, OP, B, F)                                    \
  tt_expect(__FILE__, __LINE__, #A, OP, #B,                             \
            (tt_val_t){ TT_VAL_UINT64, .value.uint64_val = (A) },       \
            (tt_val_t){ TT_VAL_UINT64, .value.uint64_val = (B) },       \
            (F))

#endif

#define TT_EXPECT_UINT64_EQ(A, B)                        \
  TT_TEST_UINT64_((A), TT_OP_UINT64_EQ, (B), TT_FALSE)

#define TT_ASSERT_UINT64_EQ(A, B)                \
  TT_TEST_UINT64_((A), TT_OP_UINT64_EQ, (B), TT_TRUE)

#define TT_EXPECT_UINT64_NE(A, B)                        \
  TT_TEST_UINT64_((A), TT_OP_UINT64_NE, (B), TT_FALSE)

#define TT_ASSERT_UINT64_NE(A, B)                \
  TT_TEST_UINT64_((A), TT_OP_UINT64_NE, (B), TT_TRUE)

#define TT_EXPECT_UINT64_LT(A, B)                        \
  TT_TEST_UINT64_((A), TT_OP_UINT64_LT, (B), TT_FALSE)

#define TT_ASSERT_UINT64_LT(A, B)                \
  TT_TEST_UINT64_((A), TT_OP_UINT64_LT, (B), TT_TRUE)

#define TT_EXPECT_UINT64_LE(A, B)                        \
  TT_TEST_UINT64_((A), TT_OP_UINT64_LE, (B), TT_FALSE)

#define TT_ASSERT_UINT64_LE(A, B)                \
  TT_TEST_UINT64_((A), TT_OP_UINT64_LE, (B), TT_TRUE)

#define TT_EXPECT_UINT64_GT(A, B)                        \
  TT_TEST_UINT64_((A), TT_OP_UINT64_GT, (B), TT_FALSE)

#define TT_ASSERT_UINT64_GT(A, B)                \
  TT_TEST_UINT64_((A), TT_OP_UINT64_GT, (B), TT_TRUE)

#define TT_EXPECT_UINT64_GE(A, B)                        \
  TT_TEST_UINT64_((A), TT_OP_UINT64_GE, (B), TT_FALSE)

#define TT_ASSERT_UINT64_GE(A, B)                \
  TT_TEST_UINT64_((A), TT_OP_UINT64_GE, (B), TT_TRUE)

#ifdef _MSC_VER

extern void tt_test_double(const char *file, int line, const char *aexpr,
                           tt_op_t op, const char *bexpr, double a,
                           double b, tt_bool_t fatal);

#define TT_TEST_DOUBLE_(A, OP, B, F)                                    \
  tt_test_double(__FILE__, __LINE__, #A, (OP), #B, (A), (B), (F))

#else

#define TT_TEST_DOUBLE_(A, OP, B, F)                                    \
  tt_expect(__FILE__, __LINE__, #A, OP, #B,                             \
            (tt_val_t){ TT_VAL_DOUBLE, .value.double_val = (A) },       \
            (tt_val_t){ TT_VAL_DOUBLE, .value.double_val = (B) },       \
            (F))

#endif

#define TT_EXPECT_DOUBLE_EQ(A, B)                       \
  TT_TEST_DOUBLE_((A), TT_OP_DOUBLE_EQ, (B), TT_FALSE)

#define TT_ASSERT_DOUBLE_EQ(A, B)                       \
  TT_TEST_DOUBLE_((A), TT_OP_DOUBLE_EQ, (B), TT_TRUE)

#define TT_EXPECT_DOUBLE_NE(A, B)                       \
  TT_TEST_DOUBLE_((A), TT_OP_DOUBLE_NE, (B), TT_FALSE)

#define TT_ASSERT_DOUBLE_NE(A, B)                       \
  TT_TEST_DOUBLE_((A), TT_OP_DOUBLE_NE, (B), TT_TRUE)

#define TT_EXPECT_DOUBLE_LT(A, B)                       \
  TT_TEST_DOUBLE_((A), TT_OP_DOUBLE_LT, (B), TT_FALSE)

#define TT_ASSERT_DOUBLE_LT(A, B)                       \
  TT_TEST_DOUBLE_((A), TT_OP_DOUBLE_LT, (B), TT_TRUE)

#define TT_EXPECT_DOUBLE_LE(A, B)                       \
  TT_TEST_DOUBLE_((A), TT_OP_DOUBLE_LE, (B), TT_FALSE)

#define TT_ASSERT_DOUBLE_LE(A, B)                       \
  TT_TEST_DOUBLE_((A), TT_OP_DOUBLE_LE, (B), TT_TRUE)

#define TT_EXPECT_DOUBLE_GT(A, B)                       \
  TT_TEST_DOUBLE_((A), TT_OP_DOUBLE_GT, (B), TT_FALSE)

#define TT_ASSERT_DOUBLE_GT(A, B)                       \
  TT_TEST_DOUBLE_((A), TT_OP_DOUBLE_GT, (B), TT_TRUE)

#define TT_EXPECT_DOUBLE_GE(A, B)                       \
  TT_TEST_DOUBLE_((A), TT_OP_DOUBLE_GE, (B), TT_FALSE)

#define TT_ASSERT_DOUBLE_GE(A, B)                       \
  TT_TEST_DOUBLE_((A), TT_OP_DOUBLE_GE, (B), TT_TRUE)

#ifdef _MSC_VER

extern void tt_test_str(const char *file, int line, const char *aexpr,
                        tt_op_t op, const char *bexpr, const char *a,
                        const char *b, tt_bool_t fatal);

#define TT_TEST_STR_(A, OP, B, F)                               \
  tt_test_str(__FILE__, __LINE__, #A, (OP), #B, (A), (B), (F))

#else

#define TT_TEST_STR_(A, OP, B, F)                                       \
  tt_expect(__FILE__, __LINE__, #A, OP, #B,                             \
            (tt_val_t){ TT_VAL_STR, .value.str_val = (A) },             \
            (tt_val_t){ TT_VAL_STR, .value.str_val = (B) },             \
            (F))

#endif

#define TT_EXPECT_STR_EQ(A, B)                  \
  TT_TEST_STR_((A), TT_OP_STR_EQ, (B), TT_FALSE)

#define TT_ASSERT_STR_EQ(A, B)                  \
  TT_TEST_STR_((A), TT_OP_STR_EQ, (B), TT_TRUE)

#define TT_EXPECT_STR_NE(A, B)                  \
  TT_TEST_STR_((A), TT_OP_STR_NE, (B), TT_FALSE)

#define TT_ASSERT_STR_NE(A, B)                  \
  TT_TEST_STR_((A), TT_OP_STR_NE, (B), TT_TRUE)

#define TT_EXPECT_STR_LT(A, B)                  \
  TT_TEST_STR_((A), TT_OP_STR_LT, (B), TT_FALSE)

#define TT_ASSERT_STR_LT(A, B)                  \
  TT_TEST_STR_((A), TT_OP_STR_LT, (B), TT_TRUE)

#define TT_EXPECT_STR_LE(A, B)                  \
  TT_TEST_STR_((A), TT_OP_STR_LE, (B), TT_FALSE)

#define TT_ASSERT_STR_LE(A, B)                  \
  TT_TEST_STR_((A), TT_OP_STR_LE, (B), TT_TRUE)

#define TT_EXPECT_STR_GT(A, B)                  \
  TT_TEST_STR_((A), TT_OP_STR_GT, (B), TT_FALSE)

#define TT_ASSERT_STR_GT(A, B)                  \
  TT_TEST_STR_((A), TT_OP_STR_GT, (B), TT_TRUE)

#define TT_EXPECT_STR_GE(A, B)                  \
  TT_TEST_STR_((A), TT_OP_STR_GE, (B), TT_FALSE)

#define TT_ASSERT_STR_GE(A, B)                  \
  TT_TEST_STR_((A), TT_OP_STR_GE, (B), TT_TRUE)

#ifdef _MSC_VER

extern void tt_test_ptr(const char *file, int line, const char *aexpr,
                        tt_op_t op, const char *bexpr, const void *a,
                        const void *b, tt_bool_t fatal);

#define TT_TEST_PTR_(A, OP, B, F)                               \
  tt_test_ptr(__FILE__, __LINE__, #A, (OP), #B, (A), (B), (F))

#else

#define TT_TEST_PTR_(A, OP, B, F)                                       \
  tt_expect(__FILE__, __LINE__, #A, OP, #B,                             \
            (tt_val_t){ TT_VAL_PTR, .value.ptr_val = (A) },             \
            (tt_val_t){ TT_VAL_PTR, .value.ptr_val = (B) },             \
            (F))

#endif

#define TT_EXPECT_PTR_EQ(A, B)                  \
  TT_TEST_PTR_((A), TT_OP_PTR_EQ, (B), TT_FALSE)

#define TT_ASSERT_PTR_EQ(A, B)                  \
  TT_TEST_PTR_((A), TT_OP_PTR_EQ, (B), TT_TRUE)

#define TT_EXPECT_PTR_NE(A, B)                  \
  TT_TEST_PTR_((A), TT_OP_PTR_NE, (B), TT_FALSE)

#define TT_ASSERT_PTR_NE(A, B)                  \
  TT_TEST_PTR_((A), TT_OP_PTR_NE, (B), TT_TRUE)

#define TT_EXPECT_PTR_NOTNULL(A)                \
  TT_TEST_PTR_((A), TT_OP_PTR_NE, NULL, TT_FALSE)

#define TT_ASSERT_PTR_NOTNULL(A)                \
  TT_TEST_PTR_((A), TT_OP_PTR_NE, NULL, TT_TRUE)

#define TT_EXPECT_PTR_NULL(A)                   \
  TT_TEST_PTR_((A), TT_OP_PTR_EQ, NULL, TT_FALSE)

#define TT_ASSERT_PTR_NULL(A)                   \
  TT_TEST_PTR_((A), TT_OP_PTR_EQ, NULL, TT_TRUE)

#define TT_EXPECT_TRUE(A)                                               \
  tt_expect_bool(__FILE__, __LINE__, #A, TT_OP_TRUE, (A), TT_FALSE)

#define TT_ASSERT_TRUE(A)                                               \
  tt_expect_bool(__FILE__, __LINE__, #A, TT_OP_TRUE, (A), TT_TRUE)

#define TT_EXPECT_FALSE(A)                                              \
  tt_expect_bool(__FILE__, __LINE__, #A, TT_OP_FALSE, (A), TT_FALSE)

#define TT_ASSERT_FALSE(A)                                               \
  tt_expect_bool(__FILE__, __LINE__, #A, TT_OP_FALSE, (A), TT_TRUE)

#define TT_EXPECT_FILE_EQ(A, B)                         \
  TT_TEST_STR_((A), TT_OP_FILE_EQ, (B), TT_FALSE)

#define TT_ASSERT_FILE_EQ(A, B)                         \
  TT_TEST_STR_((A), TT_OP_FILE_EQ, (B), TT_TRUE)

#define TT_EXPECT_FILE_NE(A, B)                         \
  TT_TEST_STR_((A), TT_OP_FILE_NE, (B), TT_FALSE)

#define TT_ASSERT_FILE_NE(A, B)                         \
  TT_TEST_STR_((A), TT_OP_FILE_NE, (B), TT_TRUE)

#define TT_EXPECT_TXTFILE_EQ(A, B)                      \
  TT_TEST_STR_((A), TT_OP_TXTFILE_EQ, (B), TT_FALSE)

#define TT_ASSERT_TXTFILE_EQ(A, B)                      \
  TT_TEST_STR_((A), TT_OP_TXTFILE_EQ, (B), TT_TRUE)

#define TT_EXPECT_TXTFILE_NE(A, B)                      \
  TT_TEST_STR_((A), TT_OP_TXTFILE_NE, (B), TT_FALSE)

#define TT_ASSERT_TXTFILE_NE(A, B)                      \
  TT_TEST_STR_((A), TT_OP_TXTFILE_NE, (B), TT_TRUE)

#define TT_FAIL(M, ...)                         \
  tt_fail(__FILE__, __LINE__, (M), __VA_ARGS__)

#define TT_SUITE_START(S)                               \
  tt_testsuite_t *__suite__ ## S = tt_suite_create(#S)

#define TT_SUITE_TEST(S, F)                     \
  tt_suite_add_test(__suite__ ## S, #F, F);

#define TT_TEST(F)                              \
  static void F(void)

#define TT_SUITE_END(S)                         \
  tt_suite_destroy(__suite__ ## S)

#define TT_SUITE_RUN(S)                         \
  tt_suite_run(__suite__ ## S)


#endif // __tinytest_h
