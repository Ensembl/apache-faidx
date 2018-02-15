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

#ifndef __MOD_FAIDX_H__
#define __MOD_FAIDX_H__

#include "typedef.h"
#include "htslib/faidx.h"
#include "htslib_fetcher.h"

//static const int MAX_SIZE = 16384;
//static const int MAX_FASTA_LINE_LENGTH = 60;
//static const int CHUNK_SIZE = 1048576; /* Chunk size, 1MB */
//static const int MAX_SEQUENCES = 25; /* Maximum number of sequences a user is allowed to request, not implemented */
//static const int MAX_HEADER = 120; /* Maximum size of a header chunk, including NUL */

#define OFFSET(remaining) (CHUNK_SIZE - remaining - 1)


/* RETIRED - Representation of a Faidx object */
typedef struct {
  faidx_t* pFai;
  /* Name of the fai set, ie homo_sapiens_grch37 */
  char* fai_set_handler;
  /* Path to the .faa or .gz, including filename */
  char* fai_path;
  void* nextFai;
} Faidx_Obj_holder;

/* RETIRED */
typedef struct {
  /* The Fai object this checksum refers to */
  Faidx_Obj_holder* tFai;
  /* Checksum user could give us */
  char* checksum;
  /* The location in the region, ie. X in GRCh38 */
  char* set;
  char* location_name;
  void* nextChecksum;
} Faidx_Checksum_obj;

/* server config structure */
typedef struct {
  apr_hash_t* checksums;    /* Checksums allowed to be queried, from seqfile record blocks */
  files_mgr* files;         /* Files manager object pointer */
  apr_hash_t* labels;       /* Labels for sequence aliases seen, eg md5, sha1 */
  int labels_endpoints;     /* Boolean flag on if labels based endpoints are
			       enabled. eg /sequence/md5/<hash>/ */
  int cachesize;            /* The cachesize for number of file handles to keep open */
  //  Faidx_Obj_holder* FaiList;
  //  Faidx_Checksum_obj* MD5List;
  //  Faidx_Checksum_obj* SHA1List;
} mod_Faidx_svr_cfg;

static int Faidx_handler(request_rec* r);
static int Faidx_sets_handler(request_rec* r, Faidx_Obj_holder* Fais);
static int Faidx_locations_handler(request_rec* r, char* set);
static int mod_Faidx_hook_post_config(apr_pool_t *pconf, apr_pool_t *plog,
                                       apr_pool_t *ptemp, server_rec *s);
int Faidx_init_checksums(server_rec* svr, Faidx_Checksum_obj* checksum_list);
static apr_status_t Faidx_cleanup_fais(void* server_cfg);
static void mod_Faidx_hooks(apr_pool_t* pool);
static void mod_Faidx_remove_fai(Faidx_Obj_holder** parent_Fai_Obj, Faidx_Obj_holder* Fai_Obj);

static void* mod_Faidx_svr_conf(apr_pool_t* pool, server_rec* s);
static Faidx_Obj_holder* mod_Faidx_fetch_fai(server_rec* s, apr_pool_t* pool,  const char* hFai, int make);
static Faidx_Checksum_obj* mod_Faidx_fetch_checksum(server_rec* s, apr_pool_t* pool, const char* checksum, int checksum_type, int make);
static void mod_Faidx_remove_checksum(Faidx_Checksum_obj** parent_Checksum_Obj, Faidx_Checksum_obj* Checksum_Obj);
static const char* modFaidx_set_handler(cmd_parms* cmd, void* cfg, const char* HandlerName);
static const char* modFaidx_init_set(cmd_parms* cmd, void* cfg, const char* SetName, const char* SetFilename);
static const char* modFaidx_set_MD5(cmd_parms* cmd, void* cfg, const char* Checksum, const char* SetName, const char* Location);
static const char* modFaidx_set_SHA1(cmd_parms* cmd, void* cfg, const char* Checksum, const char* SetName, const char* Location);
void print_fasta(request_rec* r, char* header, char* seq, int seq_len);
int Faidx_create_header(char* buf, int format, char* set, char* seq_name, char* location, int seq_count);
int Faidx_append_or_send(request_rec* r, char* send_ptr, int send_length, int* buf_remaining, char** buf_ptr, int flush);
int Faidx_create_footer(char* buf, int format);

static apr_hash_t *parse_form_from_string(request_rec *r, char *args);
static apr_hash_t* parse_form_from_GET(request_rec *r);
static int parse_form_from_POST(request_rec* r, apr_hash_t** form, int json_data);
static apr_hash_t *parse_json_from_string(request_rec * r, char *p);
static char* parse_key(request_rec * r, char **p);
static char* get_value(request_rec * r, char** s);

static char* Faidx_fetch_sequence_name(request_rec * r, char* location, int* i);
static char* strrstr(char*, char*);

#endif
