/*

 Wrapper between mod_faidx and htslib to fetch one or
 more sequences and either return that or translate
 to a protein sequence.

 Copyright [2016-2017] EMBL-European Bioinformatics Institute
 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
*/

#ifndef __MOD_FAIDX_TEST_HARNESS_H__
#define __MOD_FAIDX_TEST_HARNESS_H__

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DATAFILE_PATH
#define DATAFILE_PATH ../
#endif

#define STRINGIFY(x) #x
#define INSERT_DATA_PATH_(x) STRINGIFY(x)
#define INSERT_DATA_PATH INSERT_DATA_PATH_(DATAFILE_PATH)

void assert_int_equal(const int expected, const int actual, int lineno);
void assert_int_nequal(const int expected, const int actual, int lineno);
void assert_str_equal(const char *expected, const char *actual, int lineno);
void assert_str_nequal(const char *expected, const char *actual,
                       size_t n, int lineno);
void assert_ptr_notnull(const void *ptr, int lineno);
void assert_ptr_equal(const void *expected, const void *actual, int lineno);
void assert_true(int condition, int lineno);
void assert_false(int condition, int lineno);
void assert_fail(const char *message, int lineno);
void assert_not_impl(const char *message, int lineno);
void assert_assert(const char *message, int condition, int lineno);
void assert_size_equal(size_t expected, size_t actual, int lineno);

#define ASSERT_INT_EQUAL(a, b)        assert_int_equal(a, b, __LINE__)
#define ASSERT_INT_NEQUAL(a, b)       assert_int_nequal(a, b, __LINE__)
#define ASSERT_STR_EQUAL(a, b)        assert_str_equal(a, b, __LINE__)
#define ASSERT_STR_NEQUAL(a, b, c)    assert_str_nequal(a, b, c, __LINE__)
#define ASSERT_PTR_NOTNULL(a)         assert_ptr_notnull(a, __LINE__)
#define ASSERT_PTR_EQUAL(a, b)        assert_ptr_equal(a, b, __LINE__)
#define ASSERT_TRUE(a)                assert_true(a, __LINE__);
#define ASSERT_FALSE(a)               assert_false(a, __LINE__);
#define ASSERT_FAIL(a)                assert_fail(a, __LINE__);
#define ASSERT_NOT_IMPL(a)            assert_not_impl(a, __LINE__);
#define ASSERT_ASSERT(a, b)           assert_assert(a, b, __LINE__);

#define ABTS_SIZE_EQUAL(a, b)         assert_size_equal(a, b, __LINE__)

#endif
