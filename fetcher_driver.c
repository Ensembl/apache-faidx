#include <stdio.h>
#include "htslib/faidx.h"

int main() {
  faidx_t* fai;
  char *seq;
  int seq_len;

  fai = fai_load("/home/lairdm/Downloads/Homo_sapiens.GRCh38.dna.toplevel.fa.gz");

  puts("here");
  seq = tark_fetch_seq(fai, "1:2000-3000,10000-11000:-1", &seq_len);
  if(seq) {
    printf("%s\n", seq);
    puts("freeing");
    free(seq);
  }
  seq = tark_fetch_seq(fai, "1:2000-3000:1", &seq_len);
  puts("finished");

  printf("%s\n", seq);
  free(seq);

  seq = tark_translate_seq(fai, "1:10000-10009,20000-20010,30000-30011,40000-40006:-1", &seq_len);
  printf("%s\n", seq);
  free(seq);

  return 0;
}
