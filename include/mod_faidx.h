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
#include "files_manager.h"

#include "htslib/faidx.h"
#include "htslib_fetcher.h"

#include <httpd.h>
#include <http_protocol.h>
#include <http_config.h>
#include <http_log.h>
#include <apr_strings.h>
#include <apr_hash.h>
#include <apr_escape.h>
#include <ap_mpm.h>
#include <unistd.h>
#include <string.h>

//static const int MAX_SIZE = 16384;
//static const int MAX_FASTA_LINE_LENGTH = 60;
//static const int CHUNK_SIZE = 1048576; /* Chunk size, 1MB */
//static const int MAX_SEQUENCES = 25; /* Maximum number of sequences a user is allowed to request, not implemented */
//static const int MAX_HEADER = 120; /* Maximum size of a header chunk, including NUL */

#define OFFSET(remaining) (CHUNK_SIZE - remaining - 1)
#define trim(line) while (*(line)==' ' || *(line)=='\t') (line)++

/* server config structure */
typedef struct {
  char* endpoint_base;      /* Base url for our endpoints */
  apr_hash_t* checksums;    /* Checksums allowed to be queried, from seqfile record blocks */
  files_mgr_t* files;         /* Files manager object pointer */
  apr_hash_t* labels;       /* Labels for sequence aliases seen, eg md5, sha1 */
  int labels_endpoints;     /* Boolean flag on if labels based endpoints are
			       enabled. eg /sequence/md5/<hash>/ */
  int cachesize;            /* The cachesize for number of file handles to keep open */
} mod_Faidx_svr_cfg;

static int Faidx_handler(request_rec* r);
static int mod_Faidx_hook_post_config(apr_pool_t *pconf, apr_pool_t *plog,
                                       apr_pool_t *ptemp, server_rec *s);
static apr_status_t Faidx_cleanup_fais(void* server_cfg);
static void mod_Faidx_hooks(apr_pool_t* pool);

static void* mod_Faidx_svr_conf(apr_pool_t* pool, server_rec* s);
void print_fasta(request_rec* r, char* header, char* seq, int seq_len);
int Faidx_create_header(char* buf, int format, char* set, char* seq_name, char* location, int seq_count);
int Faidx_append_or_send(request_rec* r, char* send_ptr, int send_length, int* buf_remaining, char** buf_ptr, int flush);
int Faidx_create_footer(char* buf, int format);
int Faidx_create_end(char* buf, int format);
const int mod_Faidx_create_iterator(request_rec* r, mod_Faidx_svr_cfg* svr, apr_hash_t *formdata, seq_iterator_t** sit);
seq_iterator_t* iterator_pool_copy(request_rec* r, seq_iterator_t* siterator);
int metadata_handler(request_rec* r, const char* checksum, checksum_obj* checksum_holder);

static const char* seqfile_section(cmd_parms * cmd, void * _cfg, const char * arg);
checksum_obj* parse_seq_token(cmd_parms * cmd, char** seqname, char** seq_checksum,  char* args);
char* parse_alias_token(cmd_parms * cmd, char** seqname, char** alias,  char* args);
static const char* modFaidx_init_cachesize(cmd_parms* cmd, void* cfg, const char* cachesize);

static apr_hash_t *parse_form_from_string(request_rec *r, char *args);
static apr_hash_t* parse_form_from_GET(request_rec *r);
static int parse_form_from_POST(request_rec* r, apr_hash_t** form, int json_data);
static apr_hash_t *parse_json_from_string(request_rec * r, char *p);
static char* parse_key(request_rec * r, char **p);
static char* get_value(request_rec * r, char** s);

static char* Faidx_fetch_sequence_name(request_rec * r, char* location, int* i);
static char* strrstr(char*, char*);

void print_iterator(request_rec* r, seq_iterator_t* siterator);

#endif
