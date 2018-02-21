/* htslib seq fetch and assembly module

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

#ifndef __HTSLIB_FETCHER_H__
#define __HTSLIB_FETCHER_H__

#include "htslib/faidx.h"

#include <stdio.h>

typedef struct seq_location {
  unsigned int start;
  unsigned int end;
  unsigned int length;
} seq_location_t;

typedef struct seq_iterator {
  faidx_t* fai;
  char* seq_name;
  char* location_str;
  unsigned int seq_length;
  unsigned int seq_iterated; // Overall how far along are we
  unsigned int line_length; // How long a line to print before wrapping
  int strand;
  int translate;
  seq_location_t* locations;
  unsigned int segment_ptr; // Which segment are we on
  unsigned int segment_bp_ptr; // Where are we in that segment, relative numbers
} seq_iterator_t;

seq_iterator_t* tark_fetch_iterator(faidx_t* fai, const char *seq_name, const char *locs, int ensembl_coords);
int tark_iterator_translated_length(seq_iterator_t* siterator, int* remaining, int* unpadded_remaining);
char* tark_fetch_seq(faidx_t* fai, const char *str, int *seq_len);
char* tark_iterator_fetch_translated_seq(seq_iterator_t* siterator, int *seq_len, char* seq_ptr);
char* tark_iterator_fetch_seq(seq_iterator_t* siterator, int *seq_len, char* seq_ptr);
char* _tark_iterator_fetch_seq(seq_iterator_t* siterator, int *seq_len, char* seq_ptr, int do_line_length);
void tark_iterator_set_line_length(seq_iterator_t* siterator, unsigned int length);
int tark_iterator_adjusted_seq_len(int window, int bp_remaining, int bp_iterated, int line_length, int* bytes_to_cr);
int tark_iterator_seek(seq_iterator_t* siterator, unsigned int bp);
int tark_iterator_locations_count(seq_iterator_t* siterator);
int tark_iterator_remaining(seq_iterator_t* siterator, int translated);
void tark_free_iterator(seq_iterator_t* siterator);
char* tark_translate_seq(faidx_t* fai, const char *str, int *seq_len);
char* tark_translate_seqs(char **str, int seq_len, int nseqs, int strand);
char* tark_revcomp_seq(char *seq);
char* tark_rev_seq(char* seq);
char** tark_fetch_seqs(faidx_t* fai, const char *str, int *seq_len, int *nseqs, int *strand);
int memcpy_with_cr(void* dest, void* src, int len, int line_len, int *bytes_to_cr);

#endif
