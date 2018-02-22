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

#ifndef __MOD_FAIDX_TYPEDEF_H__
#define __MOD_FAIDX_TYPEDEF_H__

#include <apr_tables.h>
#include <openssl/md5.h>

#ifndef DEFAULT_FILES_CACHE_SIZE
#define DEFAULT_FILES_CACHE_SIZE 100
#endif

#define MAX_SIZE 16384
#define MAX_FASTA_LINE_LENGTH 60
#define CHUNK_SIZE 1048576 /* Chunk size, 1MB */
#define MAX_SEQUENCES 25 /* Maximum number of sequences a user is allowed to request, not implemented */
#define MAX_HEADER 120 /* Maximum size of a header chunk, including NUL */

#define SEQ_ENDPOINT_DIRECTIVE "sequence_base_uri"
#define SEQFILE_CACHESIZE_DIRECTIVE "sequence_cachesize"
#define LABELS_ENDPOINT_DIRECTIVE "sequence_enable_labels"
#define SEQ_DIRECTIVE "seq"

#define BEGIN_SEQFILE "<SeqFile"
#define END_SEQFILE "</SeqFile>"

#define CONTENT_JSON 1
#define CONTENT_FASTA 2
#define CONTENT_WWWFORM 3
#define CONTENT_TEXT 4

#define UNKNOWN_VERB 0
#define METADATA_VERB 1
#define CHECKSUM_VERB 2

/* Representation of a sequence in a sequence file */
typedef struct {
  const char* name;                    /* The sequence name within the file, eg 1, chrX */
  apr_array_header_t* aliases;         /* Array holding all the aliases for this sequence */
} sequence_obj;

/* Representation of a checksum */
typedef struct {
  const char* checksum_type;              /* Type of checksum */
  unsigned char file[MD5_DIGEST_LENGTH];  /* Identifier for the file,
					     stored as an MD5 hash */
  sequence_obj* sequence;                 /* Sequence within the file,
					     used to get the name of the
					     sequence to retrieve */
} checksum_obj;

/* Representation of an alias for a sequence, links back
   to a checksum record */
typedef struct {
  const char* alias;                 /* The checksum or other alias */
  checksum_obj* checksum;            /* Ptr back to a checksum object for
				        this alias */  
} alias_obj;

#ifndef strEQ
/* original in mod_ssl.h, protect the def just in case it's included */
#define strEQ(s1,s2) (strcmp((s1),(s2)) == 0)
#endif

#endif
