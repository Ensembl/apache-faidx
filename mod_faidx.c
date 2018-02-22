/* Faidx module

 Module to load Faidx indices and return sub-sequences
 efficiently.

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

#include "mod_faidx.h"

/* Hook our handler into Apache at startup */
static void mod_Faidx_hooks(apr_pool_t* pool) {
  ap_hook_handler(Faidx_handler, NULL, NULL, APR_HOOK_MIDDLE) ;

  ap_hook_post_config(mod_Faidx_hook_post_config,
		      NULL, NULL, APR_HOOK_FIRST);
}

/* pre-init, set some defaults for the linked lists */
static void* mod_Faidx_svr_conf(apr_pool_t* pool, server_rec* s) {
  /* Create the config object for this module */
  mod_Faidx_svr_cfg* svr = apr_pcalloc(pool, sizeof(mod_Faidx_svr_cfg));

  /* Make the pool for checksums */
  svr->checksums = apr_hash_make(pool);

  /* Initialize the files manager and get back a
     pointer to the control object */
  svr->files = init_files_mgr(pool);
  if(svr->files == NULL) {
    ap_log_error(APLOG_MARK, APLOG_CRIT, 0, s, "Error initializing files manager");
    return NULL;
  }

  /* Create the hash for alias type labels (md5, sha1, etc) */
  svr->labels = apr_hash_make(pool);

  /* Disable labels based endpoints by default, eg
     /sequence/md5/<sequence>/ */
  svr->labels_endpoints = 0;

  /* Set the endpoint url to NULL, so we can detect in the post hook
     if the user failed to set hte URI component */
  svr->endpoint_base = NULL;

  /* Set the cache size in case the user doesn't set it in the config */
  svr->cachesize = DEFAULT_FILES_CACHE_SIZE;

  return svr;
}

static const char* modFaidx_init_set(cmd_parms* cmd, void* cfg, const char* SetName) {
  ap_log_error(APLOG_MARK, APLOG_ERR, 0, cmd->server, "faidx set %s", SetName);

}

static const command_rec mod_Faidx_cmds[] = {
  /*  AP_INIT_TAKE1(SEQ_ENDPOINT_DIRECTIVE, ap_set_string_slot,
		(void *)APR_OFFSETOF(mod_Faidx_svr_cfg, endpoint_base),
		RSRC_CONF, "Base URI for module endpoints"),*/
  AP_INIT_TAKE1(SEQFILE_CACHESIZE_DIRECTIVE, ap_set_int_slot,
	       (void *)APR_OFFSETOF(mod_Faidx_svr_cfg, cachesize),
	       RSRC_CONF, "Set the cache size for seqfiles"),
  AP_INIT_FLAG(LABELS_ENDPOINT_DIRECTIVE, ap_set_flag_slot,
	       (void *)APR_OFFSETOF(mod_Faidx_svr_cfg, labels_endpoints),
	       RSRC_CONF, "Enable labels endpoints, limited to 'on' or 'off'"),
  AP_INIT_RAW_ARGS(BEGIN_SEQFILE, seqfile_section, NULL, EXEC_ON_READ | RSRC_CONF,
		   "Beginning of a sequence file definition section."),

  AP_INIT_TAKE1("FaidxSet", modFaidx_init_set, NULL, RSRC_CONF,
		"Initialize a faidx set"),
  { NULL }
};

module AP_MODULE_DECLARE_DATA faidx_module = {
  STANDARD20_MODULE_STUFF,
  NULL,
  NULL,
  mod_Faidx_svr_conf,
  NULL,
  mod_Faidx_cmds,
  mod_Faidx_hooks
} ;

/* Handler for http requests through apache */

static int Faidx_handler(request_rec* r) {
  apr_hash_t *formdata = NULL;
  int rv = OK;
  mod_Faidx_svr_cfg* svr = NULL;
  char* uri;
  char* uri_ptr;
  int t = UNKNOWN_VERB;
  const char* checksum_type = NULL;
  const char* checksum = NULL;
  checksum_obj* checksum_holder;

  const char* ctype_str;
  int s; /* Content type of submitted POST, reused as sequence fetched */
  int accept;
  char *key;
  char *set; /* In POST section, probably remove after cleanup */
  const char *val;
  char *last;
  unsigned int Loc_count = 1;
  int location_offset;
  char* loc_str;
  seq_iterator_t* siterator;
  seq_iterator_t** aiterator; /* iterator from an array push call */
  apr_array_header_t* location_iterators;
  char* sequence_name = NULL;
  char* send_buf;
  char* send_buf_cur;
  char* h_buf;
  int buf_remaining;
  int flushed = 0;

  svr
    = ap_get_module_config(r->server->module_config, &faidx_module);
  if(svr == NULL) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error (svr) is null, it shouldn't be!");
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  /* Is this our request, check the base URI the user gave us */
  if ( !r->handler || strcmp(r->handler, "faidx") ) {
    return DECLINED ;   /* none of our business */
  } 

  if ( (r->method_number != M_GET) && (r->method_number != M_POST) ) {
    return HTTP_METHOD_NOT_ALLOWED ;  /* Reject other methods */
  }

#ifdef DEBUG
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "We're handling a request");
#endif

  /* We only speak fasta, plain text or json,
     unless you ask for fasta or text,
     you're getting json */
  val = apr_table_get(r->headers_in, "Accept");
  if(val && (strcasestr(val,
			"text/x-fasta")
	     != NULL)) {
    accept = CONTENT_FASTA;
  } else if(val && (strcasestr(val,
			       "text/plain")
		    != NULL)) {
    accept = CONTENT_TEXT;
  } else {
    accept = CONTENT_JSON;
  }

  /* Ensure we have at least one checksum */
  if(svr->checksums == NULL) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error (checksums) is null, it shouldn't be!");
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  /* Make a copy of the uri for hunting through */
  uri = apr_pstrcat(r->pool, r->uri, NULL);

  /* Destructive, we don't care about trailing /, we always ignore
     them, so if one exists, nuke it in the copy */
  if(uri && *uri && uri[strlen(uri) - 1] == '/') {
    uri[strlen(uri) - 1] = '\0';
  }

  /* Create an array to hold the location iterators */
  location_iterators = apr_array_make(r->pool, 1, sizeof(seq_iterator_t*));

  /* Here is where our logic branches.

     If we have a GET we check the URI and try
     to retrieve any extra parameters from the query string.
     Finally make our lone sequence iterator object with
     everything we've found.

     If we have a POST, check we're given JSON, decode that JSON,
     and for each sequence hash section in it, make a sequence
     iterator.
  */
  if(r->method_number == M_GET) {
#ifdef DEBUG
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "uri: %s", uri);
#endif

    while(uri[0]) {
      /* Find the first verb in the uri.
	 uri gets updated to the location after the separator.
      */
      uri_ptr = ap_getword_nc(r->pool, &uri, '/');

      /* See if we've been asked for information on the sets */
      if( !strcmp(uri_ptr, "metadata") ) {
	/* Do and return metadata here */
	t = METADATA_VERB;
      } else if( svr->labels_endpoints && apr_hash_get(svr->labels, uri_ptr, APR_HASH_KEY_STRING) ) {
	t = CHECKSUM_VERB;
	checksum_type = uri_ptr;
      } else if( apr_hash_get(svr->checksums, uri_ptr, APR_HASH_KEY_STRING) ) {
	checksum = uri_ptr;
	break;
      }
    }

    if(checksum == NULL) {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "No checksum found in URI");
      return HTTP_BAD_REQUEST;
    }

    /* Get the checksum we're going to be working on */
    checksum_holder = apr_hash_get(svr->checksums, checksum, APR_HASH_KEY_STRING);

    if(checksum_holder == NULL) {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Checksum %s not found", checksum);
      return HTTP_NOT_FOUND;
    }

    /* Handle dispatching non-sequence endpoints. Currently we only have
       metadata, but others could slip in here. */
    if(t == METADATA_VERB) {
      return metadata_handler(r, checksum, checksum_holder);

      /* Return here because there's nothing more to do. */
    }

    /* Decode the query string */
    formdata = parse_form_from_GET(r);

    /* Put the checksum information in the formdata, it's just
       a a hash, so we can add to it. */
    apr_hash_set(formdata, apr_pstrdup(r->pool, "checksum"), APR_HASH_KEY_STRING, checksum);
    if(checksum_type != NULL) {
      apr_hash_set(formdata, apr_pstrdup(r->pool, "type"), APR_HASH_KEY_STRING, checksum_type);
    }

    /* If the user has passed us a range via the header, use it */
    val = apr_table_get(r->headers_in, "Range");
    if(val) {
      apr_hash_set(formdata, apr_pstrdup(r->pool, "range"), APR_HASH_KEY_STRING, apr_pstrdup(r->pool, val));
    }

    /* And let's send the request off to transform in to a seq iterator */
    siterator = NULL;
    rv = mod_Faidx_create_iterator(r, svr, formdata, &siterator);

    /* If an error condition was returned, bail and send it up the stack */
    if(rv != OK) {
      return rv;
    }

    if(siterator == NULL) return HTTP_INTERNAL_SERVER_ERROR;

    /* If we've reached this point we must have an iterator and be ready to
       send back sequence. Make a copy in to the request pool and free the
       malloc'ed one from the external library. */
    *(seq_iterator_t**)apr_array_push(location_iterators) = iterator_pool_copy(r, siterator);
#ifdef DEBUG
    print_iterator(r, siterator);
#endif
    tark_free_iterator(siterator);


  } else if(r->method_number == M_POST) {

    /* We only accept in POST bodies in json
       if you don't say it's json, error.
       We used to allow wwwform encoding but the new
       post body format wouldn't work with that. */
    ctype_str = apr_table_get(r->headers_in, "Content-Type");
    if( (ctype_str == NULL) || 
	(strcasestr(ctype_str, "application/json")
	 == NULL) ) {
      return HTTP_BAD_REQUEST;
    }

    /* Placeholder until we fix POST handling, this currently doesn't work */
    return HTTP_BAD_REQUEST;

    /* Decode the json body */
    rv = parse_form_from_POST(r, &formdata, 1);

    /* If we had trouble parsing the POST data, that's an
       internal server error */
    if(rv != OK) {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Error, reading form data");
      return HTTP_INTERNAL_SERVER_ERROR;

    } else if(formdata == NULL) {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Error, no form data found, how did this happen?");
      return HTTP_INTERNAL_SERVER_ERROR;
    }

  } else {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error, how did we get here? Not GET or POST?");
    return HTTP_BAD_REQUEST;
  }


  if(accept == CONTENT_FASTA) {
    ap_set_content_type(r, "text/x-fasta");
  } else if(accept == CONTENT_TEXT) {
    ap_set_content_type(r, "text/plain");
  } else {
    ap_set_content_type(r, "application/json");
  }


  /* Now we're going to create our send buffer and set up our pointers */
  send_buf = (char*)apr_palloc(r->pool, sizeof(char)*CHUNK_SIZE);
  buf_remaining = CHUNK_SIZE - 1;
  send_buf_cur = send_buf;

  if(send_buf == NULL) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error, couldn't allocate send buffer");
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  h_buf = (char*)apr_palloc(r->pool, sizeof(char)*MAX_HEADER);
  if(h_buf == NULL) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error, couldn't allocate header buffer");
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  /* Start JSON header */
  if(accept == CONTENT_JSON) {
    location_offset = snprintf( h_buf, MAX_HEADER, "{\n    \"set\": \"%s\",\n",
			   set );

    if( Faidx_append_or_send( r, h_buf, location_offset, &buf_remaining, &send_buf_cur, 0 ) ) {
      flushed = 1;
    }
  }

  /* Loop through the locations, and start sending. We're behaving like
     a type writer, we'll keep filling the send buffer, then flush when
     full. This allows us to chunk if needed or set the content. */
  while(aiterator = (seq_iterator_t**)apr_array_pop(location_iterators)) {
    siterator = *aiterator;
#ifdef DEBUG
    print_iterator(r, siterator);
#endif
    location_offset = Faidx_create_header(h_buf, accept, set, siterator->seq_name, siterator->location_str, Loc_count);
    if(location_offset > MAX_HEADER) {
      location_offset = MAX_HEADER;
#ifdef DEBUG
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Header truncated %s", h_buf);
#endif
    }
    if( Faidx_append_or_send( r, h_buf, location_offset, &buf_remaining, &send_buf_cur, 0 ) ) {
      flushed = 1;
    }

    /* Go through the iterators and start fetching sequence,
       sending chunks to the user if we fill up the buffer. */
    while(tark_iterator_remaining(siterator, siterator->translate) > 0) {
      s = buf_remaining;

      if(siterator->translate == 1) {
#ifdef DEBUG
	print_iterator(r, siterator);
#endif
	tark_iterator_fetch_translated_seq( siterator, &s, send_buf_cur );
      } else {
	tark_iterator_fetch_seq( siterator, &s, send_buf_cur );
      }

      /* This is safe because we've limited the fetch to
         never be larger than the remaining buffer a few
         lines above. */
      buf_remaining -= s;
      send_buf_cur += s;

      /* See if we've filled the buffer and flush if need be */
      if(Faidx_append_or_send( r, NULL, 0, &buf_remaining, &send_buf_cur, 0 )) {
	flushed = 1;
	//ap_rputs("\nbreak\n", r);
      }

    }

    Loc_count++;

  } /* end iterator while */

  /* Make footer if needed (JSON) and append to send buffer */
  /* Flush the buffer of last chars. If flushed == 0, we never
     sent, so we're not doing chunked, set content-length */
  location_offset = Faidx_create_footer(h_buf, accept);

  /* By setting the remaining buffer to whatever we received from the
     footer (JSON close or 0), we force the buffer to be flushed at
     the end of the append_or_send call. */
  Faidx_append_or_send(r, h_buf, location_offset, &buf_remaining, &send_buf_cur, 1);

  return OK;
}

int Faidx_append_or_send(request_rec* r, char* send_ptr, int send_length, int* buf_remaining, char** buf_ptr, int flush) {
  int flushed = 0;

  do {

    if(send_length > *buf_remaining) {
      /* We can only send part of a buffer, we need to append what we can
	 then set the buffer as full */

      memcpy(*buf_ptr, send_ptr, *buf_remaining);
      send_length -= *buf_remaining;
      (*buf_ptr)[*buf_remaining] = '\0';
      *buf_remaining = 0; /* buffer is full */

    } else if(send_length > 0) {
      /* We can copy the whole buffer to the output */

      memcpy(*buf_ptr, send_ptr, send_length);
      (*buf_remaining) -= send_length;
      (*buf_ptr)[send_length] = '\0';
      (*buf_ptr) += send_length;
      send_length = 0;

    }

    /* SEND! and reset */
    if( *buf_remaining <= 0) {
      /* Rewind the pointer, reset that type writer line */
      (*buf_ptr) -= CHUNK_SIZE - 1;
      *buf_remaining = CHUNK_SIZE - 1;
      ap_rputs(*buf_ptr, r);
      flushed = 1;
    }

  } while(send_length > 0);

  if(flush) {

    (*buf_ptr) -= (CHUNK_SIZE - (*buf_remaining) - 1);
    *buf_remaining = CHUNK_SIZE - 1;
    ap_rputs(*buf_ptr, r);
    flushed = 1;
  }

  return flushed;
}

/* 
   Print a Fasta sequence to the client
   ARGS[1] : Request object
   ARGS[2] : char*, Fasta header, caller responsible for deallocating the string
   ARGS[3] : char*, Fasta sequence to print, it will be formatted to max line length,
             caller is responsible for deallocating the string

 */

void print_fasta(request_rec* r, char* header, char* seq, int seq_len) {
  char* seq_line;
  int seq_remaining;

  /* Create a null padded string, we're taking advantage of knowing
     our terminating character is already \0 */
  seq_line = apr_pcalloc(r->pool, ( MAX_FASTA_LINE_LENGTH + 1 ));
  seq_remaining = seq_len;

  if(seq_len <= 0) {
    return;
  }

  ap_rprintf(r, ">%s\n", header);

  while(seq_remaining > 0) {
    int copy_length;
    if( seq_remaining > MAX_FASTA_LINE_LENGTH ) {
      copy_length = MAX_FASTA_LINE_LENGTH;
    } else {
      /* If we know we'll be copying less than the max line length,
         ensure this new shorter string is now \0 terminated */
      copy_length = seq_remaining;
      seq_line[copy_length+1] = '\0';
    }

    /* We know the new string will always be \0 terminated, either from
       the initial pcalloc, or because we're put a \0 on the tructated string */
    seq_line = memcpy(seq_line, seq, copy_length);
    seq_remaining -= copy_length;
    seq += copy_length;

    ap_rprintf(r, "%s\n", seq_line);
  }

}

/* buf must be at least MAX_HEADER size */

int Faidx_create_header(char* buf, int format, char* set, char* seq_name, char* location, int seq_count) {
  unsigned int sent = 0;
  unsigned int remaining = MAX_HEADER;

  if(format == CONTENT_FASTA) {

    sent = snprintf( buf, remaining, ">%s [%s:%s]\n",
		      set,
		      seq_name,
		      location);
  } else if(format == CONTENT_JSON) {

    if(seq_count > 0) {
      sent = snprintf( buf, remaining, "\",\n");
      remaining -= sent;
      buf += sent;
    }

    sent += snprintf( buf, remaining, "    \"%s:%s\": \"",
		      seq_name,
		      location);
  } else {
    /* Ensure the buffer is cleared if we're not writing to it */
    buf[0] = '\0';
  }

  return sent;
}

/* buf must be at least MAX_HEADER size */

int Faidx_create_footer(char* buf, int format) {
  unsigned int sent = 0;
  unsigned int remaining = MAX_HEADER;

  if(format == CONTENT_JSON) {
    /* Close the JSON */
    sent = snprintf( buf, remaining, "\"\n}\n");
  } else {
    /* Ensure the buffer is cleared if we're not writing to it */
    sent = snprintf( buf, remaining, "\n");
  }

  return sent;
}

/* This is where we'll need some more abstraction when/if we start to
   handle other backend types besides faidx. It's fairly tightly coupled
   to htslib's faidx right now.

   Also we don't yet handle the case where we have a checksum "type" and
   confirm it matches the checksum we've found.
 */

const int mod_Faidx_create_iterator(request_rec* r, mod_Faidx_svr_cfg* svr, apr_hash_t *formdata, seq_iterator_t** sit) {
  seq_iterator_t* siterator;
  const char* checksum;
  checksum_obj* checksum_holder;
  seq_file_t* seqfile;
  char* locs = NULL;
  const char* str;
  int ensembl_coords = 0;
  int start;
  int end;
  int strand;
  int rv;

  checksum = apr_hash_get(formdata, "checksum", APR_HASH_KEY_STRING);
  checksum_holder = apr_hash_get(svr->checksums, checksum, APR_HASH_KEY_STRING);
  if(checksum_holder == NULL) {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Checksum %s not found", checksum);
      return HTTP_NOT_FOUND;
  }

  seqfile = files_mgr_use_seqfile(svr->files, checksum_holder->file);

  str = apr_hash_get(formdata, "strand", APR_HASH_KEY_STRING);
  if(str == NULL) {
    strand = 1;
  } else {
    strand = atoi(str);
    if((strand != 1) && (strand != -1)) return HTTP_BAD_REQUEST;
  }

  if(apr_hash_get(formdata, "range", APR_HASH_KEY_STRING)) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "RANGE");
    locs = apr_psprintf(r->pool, "%s:%d", (char*)apr_hash_get(formdata, "range", APR_HASH_KEY_STRING), strand);
  }

  if(apr_hash_get(formdata, "start", APR_HASH_KEY_STRING) || apr_hash_get(formdata, "end", APR_HASH_KEY_STRING)) {
    apr_table_setn(r->headers_out, "Accept-Ranges", "none"); /* If we have query parameters, deny Range requests */
    if(locs != NULL) {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "User specified range and start or end");
      /* If locs isn't NULL, it means we have a Range from above, that's an error */
      return HTTP_BAD_REQUEST;
    }

    str = apr_hash_get(formdata, "start", APR_HASH_KEY_STRING);
    if(str == NULL) {
      start = 1;
    } else {
      start = atoi(str);
    }

    str = apr_hash_get(formdata, "end", APR_HASH_KEY_STRING);
    if(str == NULL) {
      end = faidx_seq_len((faidx_t*)seqfile->file_ptr, checksum_holder->sequence->name) - 1;
    } else {
      end = atoi(str);
    }

    ensembl_coords = 1;
    locs = apr_psprintf(r->pool, "%d-%d:%d", start, end, strand);
  }
  
  siterator = tark_fetch_iterator((faidx_t*)seqfile->file_ptr, 
				  checksum_holder->sequence->name,
				  locs,
				  ensembl_coords);

  if(siterator == NULL) {
     ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "We didn't get can iterator back, that's very bad");
      return HTTP_INTERNAL_SERVER_ERROR;
  }

  if(locs == NULL) {
    siterator->location_str = apr_psprintf(r->pool,
					   "%d-%d:%d",
					   1,
					   siterator->seq_length,
					   strand);

    /* Special case, we want a whole sequence but perhaps we also want the opposite strand */
    siterator->strand = strand;
  } else {
    siterator->location_str = locs;
  }

  /* Just reusing variables to save some stack space */
  str = apr_hash_get(formdata, "translate", APR_HASH_KEY_STRING);
  if(str == NULL) {
    siterator->translate = 0;
  } else {
    strand = atoi(str);
    if((strand != 1) && (strand != 0)) {
      tark_free_iterator(siterator);
#ifdef DEBUG
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Translate param, bad value %s, %d", str, strand);
#endif
      return HTTP_BAD_REQUEST;
    }
    siterator->translate = strand;
  }

  /* Copy over the address of the iterator */
  *sit = siterator;

  return OK;
}

/* Make a copy of a seq_iterator in to the request memory pool */

seq_iterator_t* iterator_pool_copy(request_rec* r, seq_iterator_t* siterator) {
  seq_iterator_t* aiterator;

  aiterator = apr_pcalloc(r->pool, sizeof(seq_iterator_t));

  memcpy( (void*)aiterator, (void *)siterator, sizeof(seq_iterator_t) );

  aiterator->locations = apr_pmemdup(r->pool,
				     (void*)siterator->locations,
				     sizeof(seq_location_t)*tark_iterator_locations_count(siterator));
  aiterator->seq_name = apr_pstrdup(r->pool,
				    (void*)siterator->seq_name);
  aiterator->location_str = apr_pstrdup(r->pool, siterator->location_str);

  return aiterator;
}

int metadata_handler(request_rec* r, const char* checksum, checksum_obj* checksum_holder) {
  mod_Faidx_svr_cfg* svr = NULL;
  seq_file_t* seqfile;

  svr = ap_get_module_config(r->server->module_config, &faidx_module);
  seqfile = files_mgr_use_seqfile(svr->files, checksum_holder->file);


  /* Abstraction needed here if we ever want to support different sequence backends */
  int i = faidx_seq_len((faidx_t*)seqfile->file_ptr, checksum_holder->sequence->name);

  /* Start JSON header */
  ap_rputs( "{\n  \"metadata\" : {\n", r );

  ap_rprintf( r, "    \"id\" : \"%s\",\n", checksum );
  ap_rprintf( r, "    \"length\" : %d,\n", i );

  ap_rputs( "    \"aliases\" : [\n", r );

  for(i = 0; i < checksum_holder->sequence->aliases->nelts; i++) {
    if(i > 0) {
      ap_rputs( ",\n", r );
    }
    alias_obj* a = ((alias_obj**)checksum_holder->sequence->aliases->elts)[i];
    ap_rprintf( r, "      {\n        \"alias\" : \"%s\"\n      }", a->alias );
  }

  ap_rputs( "\n    ]\n  }\n}\n", r );

  return OK;
}

/* 
   Parse a <seqfile> custom tag and initialize the
   corresponding seqfiles in the files manager.

   A seqfile section should look like:
   <seqfile /path/file.faa>
     Seq 1 md5  abcdef1234
     Seq 1 sha1 987654abcd
  </seqfile>
*/

static const char* seqfile_section(cmd_parms * cmd, void * dummy, const char * arg) {
  char* endp;
  const unsigned char* checksum;
  char* file;
  char* ptr;
  char* first;
  char* seqname;
  char* seq_checksum;
  checksum_obj* checksum_holder;
  int line_no = 0;
  int rv;
  char line[MAX_STRING_LEN]; /* expected by ap_cfg_getline */
  /* Cast the module config for convenience */
  mod_Faidx_svr_cfg* cfg = ap_get_module_config(cmd->server->module_config, &faidx_module);

  /* Find the argument on the <seqfile > line */
  endp = ap_strrchr_c(arg, '>');
  if(endp == NULL) {
    return apr_pstrcat(cmd->pool, cmd->cmd->name,
		       "> directive missing closing '>'", NULL);
  }

  /* NULL terminate the arg string */
  if(endp) {
    *endp = '\0';
  }

  /* Get the argument, we make the copy to allow the APR to deal with
     quoting and other weirdness. Then we hand it off to the files
     manager, this is where we'd eventually have to handle different
     file types should we ever have those, determining what it is and
     changing the constant sent in the last argument as appropriate. */
  file = ap_getword_conf(cmd->temp_pool, &arg);

  /* Check for a duplicate, if we've seen this seqfile before, ignore the entire block */
  if(files_mgr_lookup_file(cfg->files, file) != NULL) {
	ap_log_error(APLOG_MARK, APLOG_WARNING, 0, cmd->server, "Warning, seqfile %s has been seen before, ignoring", file);
	return NULL;
  }

  checksum = files_mgr_add_seqfile(cfg->files, file, FM_FAIDX);
      files_mgr_print_md5(checksum);

  /* This is also where the file type could go in, as a (optional?) second argument */
  trim(arg);
  if(*arg) {
    return "<seqfile> only takes at most one argument";
  }

  /* We didn't get a checksum back from the files manager */
  if(!checksum) {
    return apr_pstrcat(cmd->pool, cmd->cmd->name,
		       "We couldn't initialize seqfile ", file, NULL);
  }

  /* Now we start to go through the config file finding directives */
  while(!ap_cfg_getline(line, MAX_STRING_LEN, cmd->config_file)) {
    line_no++;
    ptr = line;

    first = ap_getword_conf_nc(cmd->temp_pool, &ptr);

    /* first char? or first non blank? Need to double check this */
    if(*first == '#') continue;

    if( !strcasecmp(first, END_SEQFILE) ) { /* We've found the end token */
      /* We're done, return */
      return NULL;
    }

    if( !strcasecmp(first, SEQ_DIRECTIVE) ) {
      /* We've found a sequence checksum, parse and add it */
      checksum_holder = parse_seq_token(cmd, &seqname, &seq_checksum, ptr);

      if(checksum_holder == NULL) { /* We didn't get a checksum holder back, this is an error */
	return apr_psprintf(cmd->pool, "Malformed Seq directive on line %d of <Seqfile %s>", line_no, file);
      }

      memcpy(checksum_holder->file, checksum, MD5_DIGEST_LENGTH);

      /* Add the checksum object to the sequence, this creates all the linkages between
         a checksum object and the sequence. It also creates the corresponding entry in
         the aliases list for the sequence. */
      rv = files_mgr_add_checksum(cfg->files, checksum_holder, seq_checksum, seqname);
      if(rv != APR_SUCCESS) {
	return apr_psprintf(cmd->pool, "Seq %s not found in Seqfile %s", seqname, file);
      }

      /* And we're good to add the checksum to the server's master hash */
      if(apr_hash_get(cfg->checksums, seq_checksum, APR_HASH_KEY_STRING) == NULL) {
	apr_hash_set(cfg->checksums, apr_pstrdup(cmd->pool, seq_checksum), APR_HASH_KEY_STRING, checksum_holder);
      } else {
	ap_log_error(APLOG_MARK, APLOG_WARNING, 0, cmd->server, "Warning, hash %s has been seen before, ignoring in %s", seq_checksum, file);
      }

      /* See if we've seen this type of label before, if not, remember we've seen this kind.
         This is only useful if we have the per-label endpoints enabled. */
      if(apr_hash_get(cfg->labels, checksum_holder->checksum_type, APR_HASH_KEY_STRING) == NULL) {
	apr_hash_set(cfg->labels, apr_pstrdup(cmd->pool, checksum_holder->checksum_type), APR_HASH_KEY_STRING, apr_pstrdup(cmd->pool, "1"));
      }
    }
  }

  /* If we've gotten here we've bottomed out the config file and didn't
     find the end token. */
  return apr_psprintf(cmd->pool, "Expected token not found %s", END_SEQFILE);
}

checksum_obj* parse_seq_token(cmd_parms * cmd, char** seqname, char** seq_checksum,  char* args) {
  char* checksum_type;
  checksum_obj* checksum_holder;

  /* We're going to need to pop the arguments off one by one and in between
     check we still have more arguments. With each call to ap_getword_conf_nc
     args is set to the next word or the trailing \0 of the string if we're at
     the end.
   */

  if(*args == '\0') return NULL; /* No arguments? That's an error! */
  *seqname = ap_getword_conf_nc(cmd->temp_pool, &args);

  if(*args == '\0') return NULL; /* No more arguments? That's an error! */
  checksum_type = ap_getword_conf_nc(cmd->temp_pool, &args);

  if(*args == '\0') return NULL; /* No more arguments? That's an error! */
  *seq_checksum = ap_getword_conf_nc(cmd->temp_pool, &args);

  if(*args) return NULL; /* More arguments? Oh, you better believe that's an error. */

  /* We're good to make our checksum object */
  checksum_holder = (checksum_obj*)apr_palloc(cmd->pool, sizeof(checksum_obj));

  checksum_holder->checksum_type = apr_pstrdup(cmd->pool, checksum_type);

  return checksum_holder;
}


/* Add error reporting?  "Could not load model" etc */

static int mod_Faidx_hook_post_config(apr_pool_t *pconf, apr_pool_t *plog,
				      apr_pool_t *ptemp, server_rec *s) {

  mod_Faidx_svr_cfg* svr
    = ap_get_module_config(s->module_config, &faidx_module);
  void *data = NULL;
  const char *userdata_key = "shm_counter_post_config";
  int result;

 /* We don't support threaded MPMs, as htslib is not thread safe.
    So die immediately on trying to configure the module.
  */
  ap_mpm_query(AP_MPMQ_IS_THREADED, &result);
  if(result == AP_MPMQ_DYNAMIC || result == AP_MPMQ_STATIC) {
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
		 "htslib is not thread safe, please use a non-threaded MPM");
    return DECLINED;
  }

  /* If the user hasn't set a URI fragment for us, this is a failure */
  /*  if(svr->endpoint_base == NULL) {
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
		 "Endpoint base URI isn't set");
    return DECLINED;

    }*/

#ifdef DEBUG
  ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
	       "Beginning to initialize Faidx, pid %d", (int)getpid());
#endif

  /* Apache loads DSO modules twice. We want to wait until the second
   * load before setting up our global mutex and shared memory segment.
   * To avoid the first call to the post_config hook, we set some
   * dummy userdata in a pool that lives longer than the first DSO
   * load, and only run if that data is set on subsequent calls to
   * this hook. */
  apr_pool_userdata_get(&data, userdata_key, s->process->pool);
  if (data == NULL) {
    /* WARNING: This must *not* be apr_pool_userdata_setn(). The
     * reason for this is because the static symbol section of the
     * DSO may not be at the same address offset when it is reloaded.
     * Since setn() does not make a copy and only compares addresses,
     * the get() will be unable to find the original userdata. */
#ifdef DEBUG
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
		 "We're in the first iteration, skipping init, pid %d", (int)getpid());
#endif
    apr_pool_userdata_set((const void *)1, userdata_key,
			  apr_pool_cleanup_null, s->process->pool);

    /* And... in apache 2.2 the post_config hook is only run once, fuck */
#ifdef APACHE24
    return OK; /* This would be the first time through */
#endif

  }
#ifdef DEBUG
  else {
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
		 "We're in the second iteration, carry on init, pid %d", (int)getpid());
  }
#endif

  /* Register the faidx objects for cleanup when the module exits,
     needed for graceful reloads to not leak memory */
  apr_pool_cleanup_register(pconf, svr, &Faidx_cleanup_fais, apr_pool_cleanup_null);

  /* Resize the cache */
  files_mgr_resize_cache(svr->files, svr->cachesize);

  return 0;
}

/* Clean-up the Faidx objects when the server is restarted */

static apr_status_t Faidx_cleanup_fais(void* server_cfg) {

#ifdef DEBUG
  ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
	       "Cleaning up files manager");
#endif

  destroy_files_mgr(((mod_Faidx_svr_cfg*)server_cfg)->files);

  return APR_SUCCESS;
}

static apr_hash_t *parse_form_from_string(request_rec *r, char *args) {
  apr_hash_t *form;
  /*  apr_array_header_t *values = NULL;*/
  char *pair;
  char *eq;
  const char *delim = "&";
  char *last;
  char *values;

  form = apr_hash_make(r->pool);

  /* Because the set name is in the uri and we have
     checksums for whole sequences, we're allowed
     empty url arguments. */
  if(args == NULL) {
    return form;
  }
  
  /* Split the input on '&' */
  for (pair = apr_strtok(args, delim, &last); pair != NULL;
       pair = apr_strtok(NULL, delim, &last)) {
    for (eq = pair; *eq; ++eq) {
      if (*eq == '+') {
	*eq = ' ';
      }
    }

    /* split into key value and unescape it */
    eq = strchr(pair, '=');
    
    if(eq) {
      *eq++ = '\0';
      ap_unescape_url(pair);
      ap_unescape_url(eq);
    } else {
      eq = "";
      ap_unescape_url(pair);
    }

    /* Store key/value pair in out form hash. Given that there
     * may be many values for the same key, we store values
     * in an array (which we'll have to create the first
     * time we encounter the key in question).
     */
    values = apr_hash_get(form, pair, APR_HASH_KEY_STRING);
    if(values != NULL) {
      values = apr_pstrcat(r->pool, values, "&", eq, NULL);
      /*      values = apr_array_make(r->pool, 1, sizeof(const char*));
	      apr_hash_set(form, pair, APR_HASH_KEY_STRING, values);*/
    } else {
      values = apr_pstrdup(r->pool, eq);
    }
    apr_hash_set(form, pair, APR_HASH_KEY_STRING, values);
  }

  return form;
  
}

static apr_hash_t* parse_form_from_GET(request_rec *r) {
  return parse_form_from_string(r, r->args);
}

static int parse_form_from_POST(request_rec* r, apr_hash_t** form, int json_data) {
  int bytes, eos;
  apr_size_t count;
  apr_status_t rv;
  apr_bucket_brigade *bb;
  apr_bucket_brigade *bbin;
  char *buf;
  apr_bucket *b;
  apr_bucket *nextb;
  const char *clen = apr_table_get(r->headers_in, "Content-Length");
  if(clen != NULL) {
    bytes = strtol(clen, NULL, 0);
    if(bytes >= MAX_SIZE) {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
		    "Request too big (%d bytes; limit %d)",
		    bytes, MAX_SIZE);
      return HTTP_REQUEST_ENTITY_TOO_LARGE;
    }
  } else {
    bytes = MAX_SIZE;
  }

  bb = apr_brigade_create(r->pool, r->connection->bucket_alloc);
  bbin = apr_brigade_create(r->pool, r->connection->bucket_alloc);
  count = 0;

  do {
    rv = ap_get_brigade(r->input_filters, bbin, AP_MODE_READBYTES,
			APR_BLOCK_READ, bytes);
    if(rv != APR_SUCCESS) {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "failed to read from input");
      return HTTP_INTERNAL_SERVER_ERROR;
    }
    for (b = APR_BRIGADE_FIRST(bbin);
	 b != APR_BRIGADE_SENTINEL(bbin);
	 b = nextb ) {
      nextb = APR_BUCKET_NEXT(b);
      if(APR_BUCKET_IS_EOS(b) ) {
	eos = 1;
      }
      if (!APR_BUCKET_IS_METADATA(b)) {
	if(b->length != (apr_size_t)(-1)) {
	  count += b->length;
	  if(count > MAX_SIZE) {
	    /* This is more data than we accept, so we're
	     * going to kill the request. But we have to
	     * mop it up first.
	     */
	    apr_bucket_delete(b);
	  }
	}
      }
      if(count <= MAX_SIZE) {
	APR_BUCKET_REMOVE(b);
	APR_BRIGADE_INSERT_TAIL(bb, b);
      }
    }
  } while(!eos);

  /* OK, done with the data. Kill the request if we got too much data. */
  if(count > MAX_SIZE) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Request too big (%d bytes; limit %d)",
		  bytes, MAX_SIZE);
    return HTTP_REQUEST_ENTITY_TOO_LARGE;
  }

  /* We've got all the data. Now put it in a buffer and parse it. */
  buf = apr_palloc(r->pool, count+1);
  rv = apr_brigade_flatten(bb, buf, &count);
  if(rv != APR_SUCCESS) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error (flatten) reading from data");
    return HTTP_INTERNAL_SERVER_ERROR;
  }
  buf[count] = '\0';
#ifdef DEBUG
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
		 "Found form data: %s", buf);
#endif

  if(json_data) {
    *form = parse_json_from_string(r, buf);
  } else {
    *form = parse_form_from_string(r, buf);
  }

  return OK;

}

static apr_hash_t *parse_json_from_string(request_rec * r, char *p) {
  apr_hash_t *form;
  int in_set = 0;
  int is_array = 0;
  char *values;
  char *cur_key = NULL;

  form = apr_hash_make(r->pool);

  for(; *p != '\0'; p++) {
    switch(*p) {
    case '\0':
#ifdef DEBUG
	ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
		      "Premature end of data, exiting");
#endif
      return NULL;

    case ' ': case '\t': case '\n': case '\r':
    case ',':
      break;

    case '{':
      if(in_set) {
#ifdef DEBUG
	ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
		      "We only accept flat json, error, found a {");
#endif
	return NULL;
      }
      in_set = 1;
      break;

    case '"':
      p++;
      if(cur_key) {
	char* value = get_value(r, &p);
#ifdef DEBUG
	ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
		      "Found value: %s", value);
#endif
	values = apr_hash_get(form, cur_key, APR_HASH_KEY_STRING);
	if(values != NULL) {
          values = apr_pstrcat(r->pool, values, "&", value, NULL);
        } else {
  	  values = apr_pstrdup(r->pool, value);
        }
	apr_hash_set(form, cur_key, APR_HASH_KEY_STRING, values);

	if(!is_array) {
	    cur_key = NULL;
        }

      } else {
	cur_key = parse_key(r, &p);
#ifdef DEBUG
	ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
		      "Found key: %s", cur_key);
#endif
      }

      break;
    case '-': case '0': case '1': case '2': case '3': case '4': case '5': case '6': case '7': case '8': case '9':
      {
	char* pe;
	long i; double d;
	i = strtoll(p, &pe, 0);
	if (pe==p || errno==ERANGE) {
	  ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
			"Invalid number %s", p);
	  return NULL;
	}
	if (*pe=='.' || *pe=='e' || *pe=='E') { // double value
	  d = strtod(p, &pe);
	  if (pe==p || errno==ERANGE) {
	    ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
	                  "Invalid number %s", p);
	    return NULL;
	  }
	  pe = apr_psprintf(r->pool, "%lf", d);
	}
	else {
	  pe = apr_itoa(r->pool, i);
	}
	values = apr_hash_get(form, cur_key, APR_HASH_KEY_STRING);
	if(values != NULL) {
          values = apr_pstrcat(r->pool, values, "&", pe, NULL);
        } else {
  	  values = pe;
	  if(!is_array) {
	    cur_key = NULL;
          }
        }
	apr_hash_set(form, cur_key, APR_HASH_KEY_STRING, values);
      }
    case '[':
      is_array = 1;
      break;
    case ']':
      cur_key = NULL;
      is_array = 0;
      break;

    case '}':
      if(cur_key) {
#ifdef DEBUG
	ap_log_rerror(APLOG_MARK, APLOG_ERR, 0, r,
		      "Premature end of json, we're still in a tag");
#endif
	return NULL;
      }
    } // end switch

  }

  return form;
}

static char* parse_key(request_rec * r, char **p) {
  char* key;
  char* end_quote;

  end_quote = strchr(*p, '"');
  if(end_quote) {
    *end_quote = '\0';
    // We don't need to copy the key since we now have a null terminated string
    // in the middle of the original string
    key = *p;
    //    key = apr_pstrdup(r->pool, *p);
    *p = end_quote;
    return key;
  }

  return NULL;
}

static char* get_value(request_rec * r, char** p) {
  char *end_quote;
  char* value;
  apr_size_t len;

  end_quote = strchr(*p, '"');

  if(end_quote) {
    *end_quote = '\0';
    apr_unescape_url(NULL, *p, APR_ESCAPE_STRING, NULL, NULL, 0, &len);
    value = *p;
    *p = end_quote;
    return value;
  }

  return NULL;
}

static char* Faidx_fetch_sequence_name(request_rec * r, char* location, int* i) {
  char* sequence_name;

  for(*i = 0; location[*i] != '\0'; (*i)++) if(location[*i] == ':') break; // Find the first colon

  /* If we're at a null, it means we didn't find a colon, no sequence name */
  if(location[*i] == '\0') {
    return NULL;
  } else {
    /* If we are at a colon, blank it out */
    location[*i] = '\0';
  }

  /* We're on the colon, we want the offset to be the start
     of the location string */
  (*i)++;

  return apr_pstrmemdup(r->pool, location, *i); // *i + 1?

}

/*
** find the last occurrance of find in string
*/
static char *
strrstr(char *string, char *find)
{
	size_t stringlen, findlen;
	char *cp;

	findlen = strlen(find);
	stringlen = strlen(string);
	if (findlen > stringlen)
		return NULL;

	for (cp = string + stringlen - findlen; cp >= string; cp--)
		if (strncmp(cp, find, findlen) == 0)
			return cp;

	return NULL;
}

void print_iterator(request_rec* r, seq_iterator_t* siterator) {
  int rv;
  int i;

  ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "__ITERATOR__");
  ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "seq_name: %s", siterator->seq_name);
  ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "location_str: %s", siterator->location_str);
  ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "seq_length: %d", siterator->seq_length);
  ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "seq_iterated: %d", siterator->seq_iterated);
  ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "line_length: %d", siterator->line_length);
  ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "strand: %d", siterator->strand);
  ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "translate: %d", siterator->translate);
  ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "location count: %d",tark_iterator_locations_count(siterator));

  for(i = 0; i < tark_iterator_locations_count(siterator); i++) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "Seq Location %d", i);
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "start: %d", siterator->locations[0].start);
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "end: %d", siterator->locations[0].end);
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r, "length: %d", siterator->locations[0].length);
  }
}
