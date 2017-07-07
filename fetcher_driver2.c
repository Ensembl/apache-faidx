#include <stdio.h>
#include "htslib/faidx.h"
#include "htslib_fetcher.h"

int main() {
  faidx_t* fai;
  char *seq;
  int seq_len;
  seq_iterator_t* siterator;
  int reset_seq_len = 500;

  fai = fai_load("/home/lairdm/Downloads/Homo_sapiens.GRCh38.dna.toplevel.fa.gz");

  siterator = tark_fetch_iterator(fai, "1", "2000-3000,11000-12000:-1");

  if(siterator == NULL) {
    puts("NO iterator!\n");
  }

  seq_len = reset_seq_len;

  while(seq_len > 0) {
    seq_len = reset_seq_len;
    seq = tark_iterator_fetch_seq(siterator, &seq_len);
    printf("seq len returned: %d\n", seq_len);

    if(seq) {
      printf("%s\n", seq);
      puts("freeing");
      free(seq);
    }

  }

  tark_free_iterator(siterator);

}
