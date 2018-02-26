/*

 Build Apache configuration file segments for use with
 mod_faidx implementation of the GA4GH Reference Sequence
 Retrieval API.

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

#ifndef __MOD_FAIDX_CONFIG_BUILDER_H__
#define __MOD_FAIDX_CONFIG_BUILDER_H__

#include <apr_general.h>
#include <apr_getopt.h>
#include <apr_strings.h>

#include "htslib/faidx.h"
#include "htslib_fetcher.h"

#include <openssl/sha.h>
#include <openssl/md5.h>

#define BUFSIZE 1048576
//#define BUFSIZE 4096

typedef struct {
  MD5_CTX* md5;
  unsigned char* md5_digest;
  SHA_CTX* sha1;
  unsigned char* sha1_digest;
  SHA256_CTX* sha256;
  unsigned char* sha256_digest;
  SHA512_CTX* sha512;
  unsigned char* sha512_digest;
} digests_t;

void init_digests(digests_t* digest_ctx);
void update_digests(digests_t* digest_ctx, char* seq, int len);
void finalize_digests(digests_t* digest_ctx);
void print_digests(digests_t* digest_ctx, const char* seqname);
void print_digest(const unsigned char* digest, int digest_length);
void print_help();

#endif
