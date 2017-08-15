/*
 Driver to test iterator model of fetching

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

#include <stdio.h>
#include "htslib/faidx.h"
#include "htslib_fetcher.h"

int main() {
  faidx_t* fai;
  char *seq;
  int seq_len;
  int actual_len;
  seq_iterator_t* siterator;
  int reset_seq_len = 500;
  int remaining;
  int unpadded_remaining;

  fai = fai_load("/home/lairdm/Downloads/Homo_sapiens.GRCh38.dna.toplevel.fa.gz");

  //  siterator = tark_fetch_iterator(fai, "1", "1-248956422");
  siterator = tark_fetch_iterator(fai, "1", "2000-3000,11000-12000:-1");
  tark_iterator_set_line_length(siterator, 60);

  if(siterator == NULL) {
    puts("NO iterator!\n");
  }

  seq_len = reset_seq_len;

  printf("Sequence length: %d\n", siterator->seq_length);
  //  printf("Translated length: %d\n", tark_iterator_translated_length(siterator, &remaining, &unpadded_remaining));
  //  printf("Translation remaining %d\n", remaining);
  //  printf("Unpadded translation remaining %d\n\n", unpadded_remaining);

  while(seq_len > 0) {
    seq_len = reset_seq_len;
    //seq = tark_iterator_fetch_seq(siterator, &seq_len, NULL);
    seq = tark_iterator_fetch_translated_seq(siterator, &seq_len, NULL);
    printf("seq len returned: %d\n", seq_len);

    if(seq) {
      actual_len = strlen(seq);
      printf("\nfetched: %d, actual: %d\n", seq_len, actual_len);
      fprintf(stderr, "%s", seq);
      //      puts("freeing");
      free(seq);
    }

    //    tark_iterator_translated_length(siterator, &remaining, &unpadded_remaining);
    //    printf("Translation remaining %d\n", remaining);
    //    printf("Unpadded translation remaining %d\n\n", unpadded_remaining);

  }

  fprintf(stderr, "\n");

  tark_free_iterator(siterator);

}
