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

/*

Configuration callback that should be placed in the main module initialization:

AP_INIT_RAW_ARGS(BEGIN_SEQFILE, seqfile_section, NULL, EXEC_ON_READ | OR_ALL,
    "Beginning of a sequence file definition section.")

 */

#include "init_module.h"

static const char *seqfile_section(
  cmd_params * cmd,
  void * cfg,
  const chat * arg) {

}
