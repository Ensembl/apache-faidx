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

#include "files_manager.h"
#include "htslib_fetcher.h"

#include "test_harness.h"

char* cat = INSERT_DATA_PATH "test/data-files/Felis_catus.Felis_catus_6.2.dna.sample.fa";
char* human = INSERT_DATA_PATH "test/data-files/Homo_sapiens.sample.fa.gz";

/*
  Test the files_manager interface
 */

int main(int argc, const char* argv[]) {
  apr_pool_t *mp;
  files_mgr_t* fm;
  seq_file_t* seqfile;
  const unsigned char** checksums;
  char* seq;
  int seq_len;

  checksums = malloc(2 * sizeof(char*));

  apr_initialize();
  apr_pool_create(&mp, NULL);

  fm = init_files_mgr(mp);
  ASSERT_PTR_NOTNULL(fm);

  checksums[0] = files_mgr_add_seqfile(fm, cat, FM_FAIDX);
  ASSERT_PTR_NOTNULL(checksums[0]);

  checksums[1] = files_mgr_add_seqfile(fm, human, FM_FAIDX);
  ASSERT_PTR_NOTNULL(checksums[1]);

  seqfile = files_mgr_get_seqfile(fm, checksums[1]);
  seq = tark_fetch_seq((faidx_t*)seqfile->file_ptr, "1:61-66", &seq_len);

  ASSERT_STR_EQUAL("ACCCTA", seq);
  ASSERT_INT_EQUAL(6, seq_len);

  return 0;
}
