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

#include "test_harness.h"

void assert_int_equal(const int expected, const int actual, int lineno)
{
    if (expected == actual) return;

    fprintf(stderr, "Line %d: expected <%d>, but saw <%d>\n", lineno, expected, actual);
    fflush(stderr);

    exit(1);
}

void assert_int_nequal(const int expected, const int actual, int lineno)
{
    if (expected != actual) return;

    fprintf(stderr, "Line %d: expected something other than <%d>, but saw <%d>\n",
	    lineno, expected, actual);
    fflush(stderr);

    exit(1);
}

void assert_size_equal(size_t expected, size_t actual, int lineno)
{
    if (expected == actual) return;

    /* Note that the comparison is type-exact, reporting must be a best-fit */
    fprintf(stderr, "Line %d: expected %lu, but saw %lu\n", lineno, 
	    (unsigned long)expected, (unsigned long)actual);
    fflush(stderr);

    exit(1);
}

void assert_str_equal(const char *expected, const char *actual, int lineno)
{
    if (!expected && !actual) return;
    if (expected && actual)
        if (!strcmp(expected, actual)) return;

    fprintf(stderr, "Line %d: expected <%s>, but saw <%s>\n", lineno, expected, actual);
    fflush(stderr);

    exit(1);
}

void assert_str_nequal(const char *expected, const char *actual,
                       size_t n, int lineno)
{
    if (!strncmp(expected, actual, n)) return;

    fprintf(stderr, "Line %d: expected something other than <%s>, but saw <%s>\n",
	    lineno, expected, actual);
    fflush(stderr);

    exit(1);
}

void assert_ptr_notnull(const void *ptr, int lineno)
{
    if (ptr != NULL) return;

    fprintf(stderr, "Line %d: expected non-NULL, but saw NULL\n", lineno);
    fflush(stderr);

    exit(1);
}
 
void assert_ptr_equal(const void *expected, const void *actual, int lineno)
{
    if (expected == actual) return;

    fprintf(stderr, "Line %d: expected <%p>, but saw <%p>\n", lineno, expected, actual);
    fflush(stderr);

    exit(1);
}

void assert_fail(const char *message, int lineno)
{
  fprintf(stderr, "Line %d: %s\n", lineno, message);
  fflush(stderr);

  exit(1);
}
 
void assert_assert(const char *message, int condition, int lineno)
{
    if (condition) return;

    fprintf(stderr, "Line %d: %s\n", lineno, message);
    fflush(stderr);

    exit(1);
}

void assert_true(int condition, int lineno)
{
    if (condition) return;

    fprintf(stderr, "Line %d: Condition is false, but expected true\n", lineno);
    fflush(stderr);

    exit(1);
}

void assert_false(int condition, int lineno)
{
    if (!condition) return;

    fprintf(stderr, "Line %d: Condition is true, but expected false\n", lineno);
    fflush(stderr);

    exit(1);
}
