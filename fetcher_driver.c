/*
 Driver to test fetching sequences using htslib

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
