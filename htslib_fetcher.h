#ifndef __HTSLIB_FETCHER_H__
#define __HTSLIB_FETCHER_H__

#include "htslib/faidx.h"

char* tark_fetch_seq(faidx_t* fai, const char *str, int *seq_len);
char* tark_translate_seq(faidx_t* fai, const char *str, int *seq_len);
char* tark_translate_seqs(char **str, int seq_len, int nseqs, int strand);
char* tark_revcomp_seq(char *seq);
char* tark_rev_seq(char* seq);
char** tark_fetch_seqs(faidx_t* fai, const char *str, int *seq_len, int *nseqs, int *strand);

#endif
