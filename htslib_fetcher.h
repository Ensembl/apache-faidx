#ifndef __HTSLIB_FETCHER_H__
#define __HTSLIB_FETCHER_H__

#include "htslib/faidx.h"

typedef struct seq_location {
  unsigned int start;
  unsigned int end;
  unsigned int length;
} seq_location_t;

typedef struct seq_iterator {
  faidx_t* fai;
  char* seq_name;
  unsigned int seq_length;
  unsigned int seq_iterated; // Overall how far along are we
  int strand;
  seq_location_t* locations;
  unsigned int segment_ptr; // Which segment are we on
  unsigned int segment_bp_ptr; // Where are we in that segment, relative numbers
} seq_iterator_t;

char* tark_fetch_seq(faidx_t* fai, const char *str, int *seq_len);
char* tark_iterator_fetch_seq(seq_iterator_t* siterator, int *seq_len);
int tark_iterator_seek(seq_iterator_t* siterator, unsigned int bp);
seq_iterator_t* tark_fetch_iterator(faidx_t* fai, const char *seq_name, const char *locs);
void tark_free_iterator(seq_iterator_t* siterator);
char* tark_translate_seq(faidx_t* fai, const char *str, int *seq_len);
char* tark_translate_seqs(char **str, int seq_len, int nseqs, int strand);
char* tark_revcomp_seq(char *seq);
char* tark_rev_seq(char* seq);
char** tark_fetch_seqs(faidx_t* fai, const char *str, int *seq_len, int *nseqs, int *strand);

#endif
