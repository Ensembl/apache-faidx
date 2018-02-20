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

#ifndef __MOD_FAIDX_INIT_MODULE_H__
#define __MOD_FAIDX_INIT_MODULE_H__

#include <http_config.h>

static const char *seqfile_section(cmd_parms * cmd, void * _cfg, const char * arg);
checksum_obj* parse_seq_token(cmd_parms * cmd, char** seqname, char** seq_checksum,  char* args);

#endif
