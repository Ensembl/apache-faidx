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

#ifndef __MOD_FAIDX_FILES_MANAGER_H__
#define __MOD_FAIDX_FILES_MANAGER_H__

#include "typedef.h"
#include <stdio.h>

#include <apr_general.h>

#include <apr_ring.h>
#include <apr_hash.h>
#include <apr_strings.h>
#include <openssl/md5.h>
#include "htslib/faidx.h"

#define FM_FAIDX 1

/* For sanity, don't let them go beyond unless
   they really know what they're doing and recompile */
#define MAX_CACHESIZE 4096

/* Representation of a sequence file.
   This can also be used as an element in an APR 
   ring container using the member 'link' as the
   ring APR_RING_ENTRY */
typedef struct _seq_file_t {
  APR_RING_ENTRY(_seq_file_t) link; /* Ring entry for APR Ring macros*/

  const char* path;                 /* Path and filename of sequences */
  int type;                         /* Type of file, only FAIDX implemented currently */
  apr_hash_t* sequences;            /* Hash of all sequences in the sequence file */
  void* file_ptr;                   /* Ptr to the file handle, a faidx_t for FAIDX type.
				       NULL if the file or connection is closed. */
} seq_file_t;

/* APR ring container type */
typedef struct _files_mgr_ring_t files_mgr_ring_t;
APR_RING_HEAD(_files_mgr_ring_t, _seq_file_t);

/* Files manager control object */
typedef struct {
  int cache_size;          /* Number of files to keep open */
  int cache_used;          /* Number of open files in the cache */
  files_mgr_ring_t* cache; /* Ring buffer of cached open files */
  apr_hash_t* seqfiles;    /* Hash of seqfiles, keyed on the MD5 of the full filename
			      for FAIDX type */
  apr_pool_t *mp;          /* Memory pool for our use, created as a sub-pool of
			      the pool passed in at init unless that pool was NULL */
} files_mgr_t;

files_mgr_t* init_files_mgr(apr_pool_t *parent_pool);
seq_file_t* files_mgr_get_seqfile(files_mgr_t* fm, const unsigned char* seqfile_md5);
seq_file_t* files_mgr_use_seqfile(files_mgr_t* fm, const unsigned char* seqfile_md5);
seq_file_t* files_mgr_lookup_file(files_mgr_t* fm, char* path);
const unsigned char* files_mgr_add_seqfile(files_mgr_t* fm, char* path, int type);
int files_mgr_add_checksum(files_mgr_t* fm, checksum_obj* checksum_holder, char* checksum, char* seqname);
alias_obj* _files_mgr_create_alias(files_mgr_t* fm, sequence_obj* seq);
int files_mgr_add_alias(files_mgr_t* fm, const unsigned char* seqfile_md5, char* seqname, char* alias);
int _files_mgr_init_seqfile(files_mgr_t* fm, seq_file_t *seqfile);
int _files_mgr_init_faidx_file(files_mgr_t* fm, seq_file_t *seqfile);
int files_mgr_open_file(files_mgr_t* fm, seq_file_t *seqfile);
int files_mgr_seqfile_usable(seq_file_t *seqfile);
int files_mgr_resize_cache(files_mgr_t* fm, int new_cache_size);
int _files_mgr_insert_cache(files_mgr_t* fm, seq_file_t *seqfile);
int _files_mgr_remove_from_cache(files_mgr_t* fm, seq_file_t *seqfile);
int files_mgr_close_file(files_mgr_t* fm, seq_file_t *seqfile);
void destroy_files_mgr(files_mgr_t* fm);

/* Debugging functions */
void files_mgr_print_md5(const unsigned char* md5);

#endif
