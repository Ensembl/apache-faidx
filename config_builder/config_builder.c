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

#include <stdio.h>
#include <stdlib.h>

#include "config_builder.h"

int main(int argc, const char *argv[]) {
    apr_status_t rv;
    apr_pool_t *mp;
    digests_t *digest_ctx;
    const char* fasta_file;
    faidx_t *fai;
    int nseq, i, buflen, aliases;
    seq_iterator_t* siterator;
    const char* seqname;
    char seq[BUFSIZE];

    aliases = 0;
    fasta_file = NULL;

    /* API is data structure driven */
    static const apr_getopt_option_t opt_option[] = {
        /* long-option, short-option, has-arg flag, description */
        { "fasta",    'f', TRUE,  "fasta input file" },         /* -f name or --fasta name */
        { "md5",      'm', FALSE, "compute md5" },              /* -m or --md5 */
        { "sha1",     '1', FALSE, "compute sha1" },             /* -1 or --sha1 */
        { "sha256",   '2', FALSE, "compute sha256" },           /* -2 or --sha256 */
        { "sha512",   '5', FALSE, "compute sha512" },           /* -5 or --sha512 */
        { "trunc512", 't', FALSE, "compute truncated sha512" }, /* -t or --trunc512 */
        { "alias",    'a', FALSE, "Add alias lines" },          /* -a or --alias */
        { "help",     'h', FALSE, "show help" },                /* -h or --help */
        { NULL, 0, 0, NULL }, /* end (a.k.a. sentinel) */
    };
    apr_getopt_t *opt;
    int optch;
    const char *optarg;
        
    apr_initialize();
    apr_pool_create(&mp, NULL);

    /* Create the digest context holder, zero'ed, so all slots
       will be NULL by default. */
    digest_ctx = apr_pcalloc(mp, sizeof(digests_t));

    /* initialize apr_getopt_t */
    apr_getopt_init(&opt, mp, argc, argv);

    /* parse the all options based on opt_option[] */
    while ((rv = apr_getopt_long(opt, opt_option, &optch, &optarg)) == APR_SUCCESS) {
      switch(optch) {
      case 'f':
	fasta_file = apr_pstrdup(mp, optarg);
	break;

      case 'm':
	digest_ctx->md5 = apr_pcalloc(mp, sizeof(MD5_CTX));
	digest_ctx->md5_digest = apr_pcalloc(mp, MD5_DIGEST_LENGTH);
	break;

      case '1':
	digest_ctx->sha1 = apr_pcalloc(mp, sizeof(SHA_CTX));
	digest_ctx->sha1_digest = apr_pcalloc(mp, SHA_DIGEST_LENGTH);
	break;

      case '2':
	digest_ctx->sha256 = apr_pcalloc(mp, sizeof(SHA256_CTX));
	digest_ctx->sha256_digest = apr_pcalloc(mp, SHA256_DIGEST_LENGTH);
	break;

      case '5':
	if (digest_ctx->sha512 == NULL) {
	  digest_ctx->sha512 = apr_pcalloc(mp, sizeof(SHA512_CTX));
	  digest_ctx->sha512_digest = apr_pcalloc(mp, SHA512_DIGEST_LENGTH);
	}
	digest_ctx->do_sha512 = 1;
	break;

      /* Both the sha512 and trunc512 need a sha512 to be calculated. So having
         sha512 not NULL signals to calculate the checksum, but do_sha512 and
         do_trunc512 will signal to output either or both digests.
       */
      case 't':
	if (digest_ctx->sha512 == NULL) {
	  digest_ctx->sha512 = apr_pcalloc(mp, sizeof(SHA512_CTX));
	  digest_ctx->sha512_digest = apr_pcalloc(mp, SHA512_DIGEST_LENGTH);
	}
	digest_ctx->do_trunc512 = 1;

      case 'a':
	aliases = 1;
	break;

      case 'h':
	print_help();
	return -1;
      }
    }
    if (rv != APR_EOF) {
      fprintf(stderr, "bad options\n");
      return -1;
    }

    if(fasta_file == NULL) {
      fprintf(stderr, "No fasta file specified\n");
      return -1;
    }

    fai = fai_load(fasta_file);
    if(fai == NULL) {
      fprintf(stderr, "Can not open file\n");
      return -1;
    }

    printf("<SeqFile \"%s\">\n", fasta_file);

    /* Number of sequences in the file */
    nseq = faidx_nseq((faidx_t*)fai);

    /* Loop through all the sequences in the fasta file */
    for(i = 0; i < nseq; ++i) {
      /* Get the sequence name from faidx */
      seqname = (const char*)apr_pstrdup(mp,
					 faidx_iseq((faidx_t*)fai, i));

      /* Use our iterator functionality to get chunks of sequence
         and run them through the openssl digest create routines */
      siterator = tark_fetch_iterator(fai, seqname, NULL, 0);

      /* Initialize all the digest contexts for ones we've been asked
         to create */
      init_digests(digest_ctx);

      printf("  # Sequence name: %s\n", seqname);

      /* Iterate through the sequence progressively calculating
         the digest */
      while(tark_iterator_remaining(siterator, 0) > 0) {
	/* How much are we allowed to recieve, will be replaced
	   with how much we HAVE received */
	buflen = BUFSIZE;
	tark_iterator_fetch_seq(siterator, &buflen, seq);

	/* We received sequence, send it through the digest creator(s) */
	if(buflen > 0) {
	  update_digests(digest_ctx, seq, buflen);
	}
      }

      /* Finalize the digest and print the corresponding Apache
         configuration lines */
      finalize_digests(digest_ctx);
      print_digests(digest_ctx, seqname);

      /* If we've been asked to make an alias entry for the sequence
         name as it appears in the fasta file */
      if(aliases) {
	printf("  Alias %s %s\n", seqname, seqname);
      }

      /* Free the iterator */
      tark_free_iterator(siterator);
    }

    printf("</SeqFile>\n");

    apr_terminate();
    return 0;
}

void init_digests(digests_t* digest_ctx) {

  if(digest_ctx->md5 != NULL) {
    MD5_Init(digest_ctx->md5);
  }

  if(digest_ctx->sha1 != NULL) {
    SHA1_Init(digest_ctx->sha1);
  }

  if(digest_ctx->sha256 != NULL) {
    SHA256_Init(digest_ctx->sha256);
  }

  if(digest_ctx->sha512 != NULL) {
    SHA512_Init(digest_ctx->sha512);
  }

}

void update_digests(digests_t* digest_ctx, char* seq, int len) {

  if(digest_ctx->md5 != NULL) {
    MD5_Update(digest_ctx->md5, seq, len);
  }

  if(digest_ctx->sha1 != NULL) {
    SHA1_Update(digest_ctx->sha1, seq, len);
  }

  if(digest_ctx->sha256 != NULL) {
    SHA256_Update(digest_ctx->sha256, seq, len);
  }

  if(digest_ctx->sha512 != NULL) {
    SHA512_Update(digest_ctx->sha512, seq, len);
  }

}

void finalize_digests(digests_t* digest_ctx) {

  if(digest_ctx->md5 != NULL) {
    MD5_Final(digest_ctx->md5_digest, digest_ctx->md5);
  }

  if(digest_ctx->sha1 != NULL) {
    SHA1_Final(digest_ctx->sha1_digest, digest_ctx->sha1);
  }

  if(digest_ctx->sha256 != NULL) {
    SHA256_Final(digest_ctx->sha256_digest, digest_ctx->sha256);
  }

  if(digest_ctx->sha512 != NULL) {
    SHA512_Final(digest_ctx->sha512_digest, digest_ctx->sha512);
  }

}

void print_digests(digests_t* digest_ctx, const char* seqname) {

  if(digest_ctx->md5 != NULL) {
    printf("  Seq %s md5 ", seqname);
    print_digest(digest_ctx->md5_digest, MD5_DIGEST_LENGTH);
    printf("\n");
  }

  if(digest_ctx->sha1 != NULL) {
    printf("  Seq %s sha1 ", seqname);
    print_digest(digest_ctx->sha1_digest, SHA_DIGEST_LENGTH);
    printf("\n");
  }

  if(digest_ctx->sha256 != NULL) {
    printf("  Seq %s sha256 ", seqname);
    print_digest(digest_ctx->sha256_digest, SHA256_DIGEST_LENGTH);
    printf("\n");
  }

  if(digest_ctx->do_sha512) {
    printf("  Seq %s sha512 ", seqname);
    print_digest(digest_ctx->sha512_digest, SHA512_DIGEST_LENGTH);
    printf("\n");
  }

  if(digest_ctx->do_trunc512) {
    printf("  Seq %s trunc512 ", seqname);
    print_digest(digest_ctx->sha512_digest, TRUNC512_LENGTH);
    printf("\n");
  }

}

void print_digest(const unsigned char* digest, int digest_length) {
  int i;

  for(i = 0; i < digest_length; i++)
    printf("%02x", digest[i]);
}

void print_help() {

  printf("\nCreate a configuration chunk to place in the Apache configuration files.\n\n");
  printf("-f [filename] or --fasta [filename]  - fasta file to extract sequences from and calculate checksums\n");
  printf("-m or --md5                          - calulate md5 checksums\n");
  printf("-1 or --sha1                         - calculate sha1 checksums\n");
  printf("-2 or --sha256                       - calculate sha256 checksums\n");
  printf("-5 or --sha512                       - calculate sha512 checksums\n");
  printf("-t or --trunc512                     - calculate truncated sha512 checksums, as defined in the API specification\n");
  printf("-a or --alias                        - add Alias line for sequence name as it appears in fasta file\n");
  printf("-h or --help                         - print this help message\n\n");
}
