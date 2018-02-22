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

/*
 Initialize the files manager, return a pointer to a files
 manager object.
 */

files_mgr_t* init_files_mgr(apr_pool_t *parent_pool) {
  files_mgr_t* fm;
  files_mgr_ring_t* ring;
  apr_pool_t *mp;
  apr_status_t rv;
  
  /* Create our own memory pool for easier cleanup in the destructor */
  rv = apr_pool_create(&mp, parent_pool);
  if (rv != APR_SUCCESS) {
    return NULL;
  }

  /* Create the files manager control object */
  fm = apr_pcalloc(mp, sizeof(files_mgr_t));

  /* Save our new memory pool */
  fm->mp = mp;

  /* intialize the ring container */
  ring = apr_palloc(mp, sizeof(files_mgr_ring_t));
  APR_RING_INIT(ring, _seq_file_t, link);
  fm->cache = ring; /* Save the ring in to as the cache */

  fm->seqfiles = apr_hash_make(mp); /* Make the hash to store seqfiles */

  fm->cache_size = DEFAULT_FILES_CACHE_SIZE;

  return fm;
}

/* Get a seqfile, the caller may not assume the file is
   open and ready to use.
   The key to access a seqfile is the md5 checksum of the
   path, as returned when the file was added to the collection.

   Return a seq_file_t* if found, NULL if the seqfile
   doesn't exist.
 */

seq_file_t* files_mgr_get_seqfile(files_mgr_t* fm, const unsigned char* seqfile_md5) {
  seq_file_t *seqfile;

  seqfile = (seq_file_t*)apr_hash_get(fm->seqfiles, (const void*)seqfile_md5, MD5_DIGEST_LENGTH);

  return seqfile;
}

/* Get a seqfile and prepare it for use. If the seqfile isn't open,
   the file will be openned and placed in the cache.
   The key to access a seqfile is the md5 checksum of the
   path, as returned when the file was added to the collection.

   Return a seq_file_t* if found, NULL if the seqfile
   doesn't exist.
 */

seq_file_t* files_mgr_use_seqfile(files_mgr_t* fm, const unsigned char* seqfile_md5) {
  seq_file_t *seqfile;
  int rv;

  seqfile = files_mgr_get_seqfile(fm, seqfile_md5);

  if(seqfile) {
    rv = files_mgr_open_file(fm, seqfile);

    if(rv != APR_SUCCESS) {
      return NULL;
    }
  }

  return seqfile;
}

/* Lookup a seqfile by path, this returns the seqfile object but the
   caller may not assume the file is open and ready to use.

   Returns NULL if not found.  */

seq_file_t* files_mgr_lookup_file(files_mgr_t* fm, char* path) {
  unsigned char md5[MD5_DIGEST_LENGTH];

  /* Create the digest for the filename */
  MD5((const unsigned char *)path, strlen(path), md5);

  return files_mgr_get_seqfile(fm, (const unsigned char*)md5);
}

/* Add a new seqfile to the collection.
 */

const unsigned char* files_mgr_add_seqfile(files_mgr_t* fm, char* path, int type) {
  apr_pool_t *mp;
  seq_file_t *seqfile;
  unsigned char md5[MD5_DIGEST_LENGTH];
  int rv;

  /* Grab our memory pool */
  mp = fm->mp;

  /* Create the digest for the filename */
  MD5((const unsigned char *)path, strlen(path), md5);

  /* If we've already seen this file before, skip */
  if(apr_hash_get(fm->seqfiles, md5, MD5_DIGEST_LENGTH) != NULL) {
    return apr_pmemdup(mp, (void*)md5, MD5_DIGEST_LENGTH);
  }

  seqfile = (seq_file_t*)apr_pcalloc(mp, sizeof(seq_file_t));

  seqfile->path = (const char*)apr_pstrdup(mp, path);
  seqfile->sequences = apr_hash_make(mp); /* Make the hash to store sequence objects */
  seqfile->type = type;

  rv = _files_mgr_init_seqfile(fm, seqfile);
  if(rv != APR_SUCCESS) {
    return NULL;
    /* We need some better error handling, this is a very bad
       situation. */
  }

  /* Put the seqfile in the collection */
  apr_hash_set(fm->seqfiles,
	       (const void*)apr_pmemdup(mp, (void*)md5, MD5_DIGEST_LENGTH),
	       MD5_DIGEST_LENGTH,
	       (const void*)seqfile);

  return (const unsigned char*)apr_pmemdup(mp, (void*)md5, MD5_DIGEST_LENGTH);;

}

/* Add a checksum/alias to a sequence.

   Takes:
   a files_manager
   a checksum_obj that has the seqfile checksum already in the file field
   the checksum/alias of the sequence
   the sequence name in the seqfile

   Returns APR_SUCCESS on success or APR_NOTFOUND if the seqfile or sequence
   in the seqfile are not found.
 */
int files_mgr_add_checksum(files_mgr_t* fm, checksum_obj* checksum_holder, char* checksum, char* seqname) {
  apr_pool_t *mp;
  seq_file_t *seqfile;
  sequence_obj *seq;
  alias_obj *alias;

  mp = fm->mp;

  /* First we get the seqfile associated with this checksum */
  seqfile = files_mgr_get_seqfile(fm, checksum_holder->file);

  if(!seqfile) { /* We can't find the seqfile with this checksum */
    return APR_NOTFOUND;
  }

  seq = apr_hash_get(seqfile->sequences, seqname, APR_HASH_KEY_STRING);

  if(!seq) { /* We can't find that sequence in the seqfile */
    return APR_NOTFOUND;
  }

  /* Create and insert the alias object */
  alias = files_mgr_add_alias(fm, seq);
  alias->alias = (const char*)apr_pstrdup(mp, checksum);
  alias->checksum = checksum_holder;

  /* Finally make the linkage from the checksum object back to the sequence */
  checksum_holder->sequence = seq;

  return APR_SUCCESS;
}

/* Add a new entry to the aliases array in a sequence, create that array
   if needed too.
 */
alias_obj* files_mgr_add_alias(files_mgr_t* fm, sequence_obj* seq) {
  apr_pool_t *mp;
  alias_obj *alias;

  mp = fm->mp;

  if(seq->aliases == NULL) {
    seq->aliases = apr_array_make(mp, 1, sizeof(alias_obj*));
  }

  alias = (alias_obj*)apr_palloc(mp, sizeof(alias_obj));

  *(alias_obj**)apr_array_push(seq->aliases) = alias;

  return alias;
}

/* Attempt to open the seqfile and scan through all the sequences
   building the sequences data structure.

   If our cache is not yet full put the file in the cache, otherwise
   close the file when we're done.
 */

int _files_mgr_init_seqfile(files_mgr_t* fm, seq_file_t *seqfile) {
  int rv;

  if(seqfile->type == FM_FAIDX) {
    rv = _files_mgr_init_faidx_file(fm, seqfile);
  } else {
    return APR_EINCOMPLETE; /* Unknown file type */
  }

  if(rv != APR_SUCCESS) {
    return APR_EINCOMPLETE; /* We weren't able to open or
			       initialize the file/connection */
  }

  return APR_SUCCESS;
}

/* Handler to initialize a Faidx type file
 */
int _files_mgr_init_faidx_file(files_mgr_t* fm, seq_file_t *seqfile) {
  apr_pool_t *mp;
  int rv, nseq, i;
  sequence_obj *seq;

  mp = fm->mp;

  rv = files_mgr_open_file(fm, seqfile);
  if(rv != APR_SUCCESS) {
    return APR_EINCOMPLETE; /* We weren't able to open the file */
  }

  nseq = faidx_nseq((faidx_t*)seqfile->file_ptr);
  for(i = 0; i < nseq; ++i) {
    seq = (sequence_obj*)apr_pcalloc(mp, sizeof(sequence_obj)); /* Create the sequence object */
    /* Get the sequence name from faidx and put a copy in the sequence object */
    seq->name = (const char*)apr_pstrdup(mp,
					 faidx_iseq((faidx_t*)seqfile->file_ptr, i));

    apr_hash_set(seqfile->sequences,
		 seq->name,
		 APR_HASH_KEY_STRING,
		 seq);
  }

  return APR_SUCCESS;
}


/* Attempt to open a seqfile
 */

int files_mgr_open_file(files_mgr_t* fm, seq_file_t *seqfile) {
  faidx_t *fai;

  if(seqfile->file_ptr != NULL) {
    return APR_SUCCESS; /* File is already open */
  }

  if(seqfile->type == FM_FAIDX) {
    fai = fai_load(seqfile->path);

    if(fai == NULL) {
      return APR_EGENERAL; /* We couldn't open the file */
    }

    /* Stash away the fai object */
    seqfile->file_ptr = (void*)fai;

    /* Put the seqfile in the cache */
    _files_mgr_insert_cache(fm, seqfile);

  } else {
    /* It wasn't a type we know */
    return APR_EGENERAL;
  }

  return APR_SUCCESS;
}

/* For a seqfile, is it useable, as in is the file open
   and ready for use. Returns true (1) or false (0), 
   if false a call to files_mgr_open_file must be called
   before attempting to use the seqfile.
 */

int files_mgr_seqfile_usable(seq_file_t *seqfile) {
  if(seqfile->file_ptr != NULL) {
    return 1; /* File is already open */
  }

  return 0;
}

int files_mgr_resize_cache(files_mgr_t* fm, int new_cache_size) {
  seq_file_t *oldest_seqfile;

  if(new_cache_size > MAX_CACHESIZE) { /* Sanity, don't let them open
					thousands of file handles unless
				        they REALLY know what they're doing */
    new_cache_size = MAX_CACHESIZE;
  }

  if(new_cache_size < fm->cache_size) { /* Are we shrinking the cache? */

    while(fm->cache_used > new_cache_size) {
      /* While our used cache is larger than the
         new size, go through and close the oldest file */
      oldest_seqfile = APR_RING_LAST( fm->cache );
      files_mgr_close_file(fm, oldest_seqfile);
    }
  }

  /* Otherwise if we're expanding the cache, nothing to do but note
     down the new cache size */

  fm->cache_size = new_cache_size;

  return APR_SUCCESS;

}

/* Touch/add an item to the cache. If the seqfile is already in
   the cache it will now be at the start. If it's not and the cache
   is full, remove the oldest item and add this seqfile to the front.
 */

int _files_mgr_insert_cache(files_mgr_t* fm, seq_file_t *seqfile) {
  seq_file_t *oldest_seqfile;

  _files_mgr_remove_from_cache(fm, seqfile);

  if(fm->cache_used >= fm->cache_size) { /* Is the cache full? */
    /* Remove the oldest/last item */
    oldest_seqfile = APR_RING_LAST( fm->cache );
    files_mgr_close_file(fm, oldest_seqfile);
  }

  /* At this point the cache should have enough space,
     the touched item should not be in there, prepend it.
     If paranoid we could put some more checks here to ensure
     all of that is true. */
  APR_RING_INSERT_HEAD(fm->cache, seqfile, _seq_file_t, link);
  (fm->cache_used)++;

  return APR_SUCCESS;
}

/* Remove a given seqfile from the cache. Return APR_SUCCESS if we found
   and removed it, APR_NOTFOUND if we didn't find it.
 */
int _files_mgr_remove_from_cache(files_mgr_t* fm, seq_file_t *seqfile) {
  seq_file_t *tmp_seqfile;

  /* Loop around the ring looking for the element. We're going backwards
     because our two remove use cases are either removing the last or
     removing a element of unknown position. So at least make the former
     of those two use cases O(1), and the latter O(logn). Looping forward
     would make the former always O(n). */
  for(tmp_seqfile = APR_RING_LAST(fm->cache);
      tmp_seqfile != APR_RING_SENTINEL(fm->cache, _seq_file_t, link);
      tmp_seqfile = APR_RING_PREV(tmp_seqfile, link)) {

    if(tmp_seqfile == seqfile) { /* Is the seqfile already in the cache? */
      APR_RING_UNSPLICE(tmp_seqfile, tmp_seqfile, link);
      (fm->cache_used)--;
      return APR_SUCCESS;
    }
  }

  return APR_NOTFOUND;
}

/* Close a file or connection associated with a seqfile,
   if it's open. And remove it from the cache, if it's
   in the cache.

   Return APR_SUCCESS if we closed a file/connection,
   APR_EGENERAL if nothing was closed. */

int files_mgr_close_file(files_mgr_t* fm, seq_file_t *seqfile) {

  /* If the file pointer is NULL, it means the associated
     file or connection is already closed, nothing to do. */
  if(seqfile->file_ptr == NULL) {
    return APR_SUCCESS;
  }

  if(seqfile->type == FM_FAIDX) {
    /* Faidx type file, call fai_destoy */
    fai_destroy((faidx_t*)seqfile->file_ptr);
    seqfile->file_ptr = NULL;

  } else {

    /* We weren't able to close anything */
    return APR_EGENERAL;
  }

  /* Remove the seqfile from the cache, if it's in there */
  _files_mgr_remove_from_cache(fm, seqfile);

  return APR_SUCCESS;
}

/* Destroy a files manager and deallocate all associated memory

   Our course of action should be to iterate through the hash,
   for each element close any associated file or connection.
   Then because we use our own memory pool to allocate all the
   structures we can simply destroy that pool to destroy
   everything else.

   This destructor will also free the files manager control object,
   the caller should assume the pointer they passed in is now invalid. */

void destroy_files_mgr(files_mgr_t* fm) {
  apr_hash_index_t *hi;
  seq_file_t *seqfile;
  apr_pool_t *mp;

  /* Grab our memory pool */
  mp = fm->mp;

  /* Iterate over the hash values */
  for (hi = apr_hash_first(mp, fm->seqfiles); hi; hi = apr_hash_next(hi)) {
    apr_hash_this(hi, NULL, NULL, (void**)&seqfile);
    files_mgr_close_file(fm, seqfile); /* Try and close the associated file or
				       connection, if open */
  }

  /* Free the memory pool */
  apr_pool_destroy(mp);
}

void files_mgr_print_md5(const unsigned char* md5) {
  int i;

  for(i = 0; i < MD5_DIGEST_LENGTH; i++)
    fprintf(stderr, "%02x", md5[i]);
  fprintf(stderr, "\n");
}
