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

  /* Create the initial empty Fai linked list */
  svr->FaiList = apr_pcalloc(pool, sizeof(Faidx_Obj_holder));
  svr->FaiList->pFai = NULL;
  svr->FaiList->fai_set_handler = NULL;
  svr->FaiList->nextFai = NULL;

  /* Create the empty linked lists for checksums */
  svr->MD5List = apr_pcalloc(pool, sizeof(Faidx_Checksum_obj));
  svr->MD5List->tFai = NULL;
  svr->MD5List->checksum = NULL;
  svr->MD5List->set = NULL;
  svr->MD5List->location_name = NULL;
  svr->MD5List->nextChecksum = NULL;

  svr->SHA1List = apr_pcalloc(pool, sizeof(Faidx_Checksum_obj));
  svr->SHA1List->tFai = NULL;
  svr->SHA1List->checksum = NULL;
  svr->SHA1List->set = NULL;
  svr->SHA1List->location_name = NULL;
  svr->SHA1List->nextChecksum = NULL;

  return svr;
}

static const command_rec mod_Faidx_cmds[] = {
  AP_INIT_TAKE2("FaidxSet", modFaidx_init_set, NULL, RSRC_CONF,
		"Initialize a faidx set"),
  AP_INIT_TAKE3("FaidxMD5", modFaidx_set_MD5, NULL, RSRC_CONF,
		"Add an MD5 alias for a set"),
  AP_INIT_TAKE3("FaidxSHA1", modFaidx_set_SHA1, NULL, RSRC_CONF,
		"Add a SHA1 alias for a set"),
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
  Faidx_Obj_holder* Fai_Obj = NULL;
  mod_Faidx_svr_cfg* svr = NULL;
  faidx_t* pFai = NULL;
  char* uri;
  char* uri_ptr;
  int t = UNKNOWN_VERB;
  const char* ctype_str;
  int s; /* Content type of submitted POST, reused as sequence fetched */
  int accept;
  char* sets_verb = "sets";
  char* locations_verb = "locations/";
  Faidx_Checksum_obj* checksum_obj = NULL;
  Faidx_Obj_holder* tFai_Obj;
  char *key;
  char *set;
  const char *val;
  const char *delim = "&";
  char *last;
  unsigned int Loc_count = 1;
  int translate;
  int location_offset;
  char* loc_str;
  seq_iterator_t* siterator;
  seq_iterator_t* aiterator; /* iterator from an array push call */
  apr_array_header_t* location_iterators;
  char* sequence_name = NULL;
  char* send_buf;
  char* send_buf_cur;
  char* h_buf;
  int buf_remaining;
  int flushed = 0;

  if ( !r->handler || strcmp(r->handler, "faidx") ) {
    return DECLINED ;   /* none of our business */
  } 

  if ( (r->method_number != M_GET) && (r->method_number != M_POST) ) {
    return HTTP_METHOD_NOT_ALLOWED ;  /* Reject other methods */
  }

  svr
    = ap_get_module_config(r->server->module_config, &faidx_module);
  if(svr == NULL) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error (svr) is null, it shouldn't be!");
    return HTTP_INTERNAL_SERVER_ERROR;
  }

#ifdef DEBUG
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "We're handling a request");
#endif

  /* We only accept in POST bodies urlencoded forms or json,
     if you don't say it's a form, we're assuming it's json */
  ctype_str = apr_table_get(r->headers_in, "Content-Type");
  if(ctype_str && (strcasestr(ctype_str, 
			  "application/x-www-form-urlencoded")
	       != NULL)) {
    s = CONTENT_WWWFORM;
  } else {
    s = CONTENT_JSON;
  }

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

  /* Get our list of faidx objects */
  Fai_Obj = svr->FaiList;
  if(Fai_Obj == NULL) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error (Fai_Obj) is null, it shouldn't be!");
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  /* Dispatch the request, is it a lookup, a request for all the
     sets or all the locations in a set. This could get a little messy.
     Eventually this could be broken out in to sub-calls. */

  /* Make a copy of the uri for hunting through */
  uri = apr_pstrcat(r->pool, r->uri, NULL);

  /* Destructive, we don't care about trailing /, we always ignore
     them, so if one exists, nuke it in the copy */
  if(uri && *uri && uri[strlen(uri) - 1] == '/') {
    uri[strlen(uri) - 1] = '\0';
  }

  while(uri[0]) {
    /* Find the first verb in the uri.
       uri gets updated to the location after the separator.
    */
    uri_ptr = ap_getword(r->pool, &uri, '/');

    /* See if we've been asked for information on the sets */
    if(!strcmp(uri_ptr, sets_verb)) {
      if(uri == NULL) {
	return Faidx_sets_handler(r, Fai_Obj);
    } else {
	/* Otherwise, there was a word after the sets verb, hand
	   it off to tell the user what regions are in that set.
	   uri will be updated by ap_getword to point at this remaining
	   word on the uri */
	return Faidx_locations_handler(r, uri);
      }
    } else if( !strcmp(uri_ptr, "region") ) {
      t = REGION_FETCH;
      break;
    } else if( !strcmp(uri_ptr, "md5") ) {
      t = CHECKSUM_MD5;
      break;
    } else if( !strcmp(uri_ptr, "sha1") ) {
      t = CHECKSUM_SHA1;
      break;
    }
  }

  /* If we've made it this far we must want sequence,
     if r isn't pointing to a fetch type we know something
     is wrong and we need to bail. */
  if(t == UNKNOWN_VERB) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "No valid request given");
    return HTTP_NOT_FOUND;
  }

  /* We know we should be looking for a set name, if we
     can't fine one, that's an error */
  set = ap_getword(r->pool, &uri, '/');
  if(set == NULL) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "No set name given");
    return HTTP_BAD_REQUEST;
  }

  /* Next we need to find what verb has been used for
     sequence fetching, if it's a checksum we need to
     find the corrected set name and associated
     sequence name */

  if(t == CHECKSUM_MD5) {
    checksum_obj = mod_Faidx_fetch_checksum(r->server, NULL, set, CHECKSUM_MD5, 0);

    if(checksum_obj == NULL) {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Error, checksum type md5 for checksum %s not found!", set);
      ap_rprintf(r, "Checksum %s not found", set);

      return HTTP_NOT_FOUND;
    }

  } else if(t == CHECKSUM_SHA1) {
    checksum_obj = mod_Faidx_fetch_checksum(r->server, NULL, set, CHECKSUM_SHA1, 0);

    if(checksum_obj == NULL) {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Error, checksum type sha1 for checksum %s not found!", set);
      ap_rprintf(r, "Checksum %s not found", set);

      return HTTP_NOT_FOUND;
    }
  }

  /* Display the form data */
  if(r->method_number == M_GET) {
    formdata = parse_form_from_GET(r);
  }
  else if(r->method_number == M_POST) {
    if(s == CONTENT_WWWFORM) {
      rv = parse_form_from_POST(r, &formdata, 0);
    }
    else {
      rv = parse_form_from_POST(r, &formdata, 1);
    }
  } else {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error, no data given to module, where's your location?");
    return HTTP_BAD_REQUEST;
  }

  if(accept == CONTENT_FASTA) {
    ap_set_content_type(r, "text/x-fasta");
  } else if(accept == CONTENT_TEXT) {
    ap_set_content_type(r, "text/plain");
  } else {
    ap_set_content_type(r, "application/json");
  }

  /* If we had trouble parsing the POST data, that's an
     internal server error */
  if(rv != OK) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error, reading form data");
    return HTTP_INTERNAL_SERVER_ERROR;
  }
  else if(formdata == NULL) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error, no form data found, how did this happen?");
		  return HTTP_INTERNAL_SERVER_ERROR;
  }

  /* A null checksom_obj means we're not using a checksum, this is just
     a normal Fai fetch/region. Otherwise, we now have the Fai and
     sequence name via the checksum object. */
  if(checksum_obj == NULL) {
    tFai_Obj = mod_Faidx_fetch_fai(r->server, NULL, set, 0);

    if(tFai_Obj == NULL) {
      ap_rprintf(r, "Set %s not found", set);
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Error, fai for set %s not found!", set);
      return HTTP_NOT_FOUND;
    }
  } else {
    tFai_Obj = checksum_obj->tFai;
    sequence_name = checksum_obj->location_name;
    set = checksum_obj->set;
  }

  Loc_count = 0;
  translate = 0;

  /* See if we've been asked to translate the sequence */
  val = apr_hash_get(formdata, "translate", APR_HASH_KEY_STRING);
  if(val != NULL) {
    translate = 1;
  }

  /* Create an array to hold the location iterators */
  location_iterators = apr_array_make(r->pool, 1, sizeof(seq_iterator_t));

  /* Try and get the one or more locations from the
     request data */
  val = apr_hash_get(formdata, "location", APR_HASH_KEY_STRING);
  if(val == NULL) {
    if(sequence_name != NULL) {
      /* We don't have any locations but we have a sequence name,
         someone has asked for an entire sequence!
	 Fetch the iterator for the whole sequence, copy it
	 to our iterators array, then deallocate it.
       */

      siterator = tark_fetch_iterator(tFai_Obj->pFai, sequence_name, NULL);

      /* We didn't get an iterator, this is likely a user error */
      if(siterator == NULL) {
	ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		      "Error, problem fetching the iterator, %s", sequence_name);
	return HTTP_BAD_REQUEST;
      }

      /* Make a deep copy of the iterator in to the APR pool */
      aiterator = (seq_iterator_t*)apr_array_push(location_iterators);
      memcpy( (void*)aiterator, (void *)siterator, sizeof(seq_iterator_t) );
      aiterator->locations = apr_pmemdup(r->pool,
					 (void*)siterator->locations,
					 sizeof(seq_location_t)*tark_iterator_locations_count(siterator));
      aiterator->seq_name = apr_pstrdup(r->pool,
					(void*)siterator->seq_name);
      aiterator->location_str = apr_psprintf(r->pool, "1-%d", aiterator->seq_length);
      tark_free_iterator(siterator);
    } else {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Error, no location(s) specified!");
      ap_rputs( "    \"Error\": \"No location(s) specified\"\n}\n", r );
      return HTTP_BAD_REQUEST;
    }
  } else {
    /* We do have locations, so let's go through them and make iterators */

    for(key = apr_strtok(val, delim, &last); key != NULL;
	key = apr_strtok(NULL, delim, &last)) {

#ifdef DEBUG
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Found location %s", key);
#endif

      /* It's a region type, so we need to extract the sequence
	 name from the location */
      if(checksum_obj == NULL) {
	sequence_name = Faidx_fetch_sequence_name(r, key, &location_offset);
	loc_str = key + location_offset;
      } else {
	loc_str = key;
      }

#ifdef DEBUG
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Sequence %s, location %s", sequence_name, loc_str);
#endif

      siterator = tark_fetch_iterator(tFai_Obj->pFai, sequence_name, loc_str);

      /* We didn't get an iterator, this is likely a user error */
      if(siterator == NULL) {
	ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		      "Error, problem fetching the iterator, %s %s", sequence_name, loc_str);
	return HTTP_BAD_REQUEST;
      }

      /* Make a deep copy of the iterator in to the APR pool */
      aiterator = (seq_iterator_t*)apr_array_push(location_iterators);
      memcpy( (void*)aiterator, (void *)siterator, sizeof(seq_iterator_t) );
      aiterator->locations = apr_pmemdup(r->pool,
					 (void*)siterator->locations,
					 sizeof(seq_location_t)*tark_iterator_locations_count(siterator));
      aiterator->seq_name = apr_pstrdup(r->pool,
					(void*)siterator->seq_name);
      aiterator->location_str = loc_str;

      tark_free_iterator(siterator);

    } /* end for */
  } /* end else */

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
  while(siterator = (seq_iterator_t*)apr_array_pop(location_iterators)) {
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
    while(tark_iterator_remaining(siterator, translate) > 0) {
      s = buf_remaining;

      if(translate == 1) {
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

/* Handler for returning all sets available  */
    
static int Faidx_sets_handler(request_rec* r, Faidx_Obj_holder* Fais) {
  int Fai_count = 0;

  /* Something went wrong, we didn't get any Fais, but
     don't punish the user for our mistake. */
  if(Fais == NULL) {
    ap_rputs( "[]\n", r );
    return OK;
  }

  /* Start JSON header */
  ap_rputs( "[", r );

  while(Fais->nextFai != NULL) {
    if(Fai_count > 0) {
      ap_rputs( ",", r );
    }

    ap_rprintf( r, "\"%s\"", Fais->fai_set_handler );

    Fais = Fais->nextFai;
    Fai_count++;
  }

  ap_rputs( "]\n", r );

  return OK;
}

/* Handler for returning all locations in a particular set  */

static int Faidx_locations_handler(request_rec* r, char* set) {
  int loc_count = 0;
  Faidx_Obj_holder* Fai_Obj;

  Fai_Obj = mod_Faidx_fetch_fai(r->server, NULL, set, 0);

  /* If they do something silly like ask for a non-existent
     set, that's not an error, it's just an empty set to
     send back*/
  if(Fai_Obj == NULL || Fai_Obj->pFai == NULL) {
    ap_rputs( "{}\n", r );
    return OK;
  }

  /* Ask faidx for a list of all available keys */
  loc_count = faidx_nseq(Fai_Obj->pFai);
  ap_rputs( "{", r );

  for(int i = 0; i < loc_count; i++) {
    if(i > 0) {
      ap_rputs( ",", r );
    }

    ap_rprintf( r, "\"%s\": %d", 
		faidx_iseq(Fai_Obj->pFai, i), 
		faidx_seq_len(Fai_Obj->pFai, faidx_iseq(Fai_Obj->pFai, i) ) );
  }

  ap_rputs( "}\n", r );

  return OK;

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

/* Add error reporting?  "Could not load model" etc */

static int mod_Faidx_hook_post_config(apr_pool_t *pconf, apr_pool_t *plog,
				      apr_pool_t *ptemp, server_rec *s) {

  Faidx_Obj_holder* Fai_Obj;
  Faidx_Obj_holder** prev_Fai_Obj;
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

  Fai_Obj = svr->FaiList;
  prev_Fai_Obj = &(svr->FaiList);

  /* Register the faidx objects for cleanup when the module exits,
     needed for graceful reloads to not leak memory */
  apr_pool_cleanup_register(pconf, svr, &Faidx_cleanup_fais, apr_pool_cleanup_null);

  /* Go through and initialize the Faidx objects in the linked list */
  while(Fai_Obj->nextFai != NULL) {
#ifdef DEBUG
      ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
	"Initializing %s", Fai_Obj->fai_set_handler);
#endif

      if(Fai_Obj->pFai != NULL) {
      ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
	"Seen set before, skipping %s", Fai_Obj->fai_set_handler);
      continue;
    }

    Fai_Obj->pFai = fai_load(Fai_Obj->fai_path);
    if(Fai_Obj->pFai == NULL) {
      ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
	"Error creating Faidx %s, removing Fai", Fai_Obj->fai_set_handler);
      mod_Faidx_remove_fai(prev_Fai_Obj, Fai_Obj);
    }

    if(Fai_Obj->pFai == NULL) {
      ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
	"Error pFai component of %s is NULL!", Fai_Obj->fai_set_handler);
    }

#ifdef DEBUG
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
		 "Finished initializing Faidx");
#endif


    prev_Fai_Obj = (Faidx_Obj_holder**)&Fai_Obj->nextFai;
    Fai_Obj = (Faidx_Obj_holder*)Fai_Obj->nextFai;
  }

  /* Next we need to link up all the checksum objects to the correct
     fai object. And fail if the named fai doesn't exist. */
  if(Faidx_init_checksums(s, svr->MD5List) == DECLINED) {
    return DECLINED;
  }

  if(Faidx_init_checksums(s, svr->SHA1List) == DECLINED) {
    return DECLINED;
  }

  return 0;
}

int Faidx_init_checksums(server_rec* svr, Faidx_Checksum_obj* checksum_list) {
  Faidx_Obj_holder* tFai;

  while(checksum_list->nextChecksum != NULL) {
    tFai = mod_Faidx_fetch_fai(svr, NULL, checksum_list->set, 0);

    /* If we can't find the Fai for the checksum, this is a big problem, fail. */
    if(tFai == NULL) {
      ap_log_error(APLOG_MARK, APLOG_EMERG, 0, svr,
		   "Error, set %s doesn't exist for checksum %s!", checksum_list->set, checksum_list->checksum);
      return DECLINED;
    }

    /* If the Fai doesn't have this location/sequence, that's a fail too. */
    if(!faidx_has_seq(tFai->pFai, checksum_list->location_name)) {
      ap_log_error(APLOG_MARK, APLOG_EMERG, 0, svr,
		   "Error, location %s in set %s doesn't exist for checksum %s!", checksum_list->location_name, checksum_list->set, checksum_list->checksum);
      return DECLINED;
    }

    /* Everything seems to check out, remember the Fai */
    checksum_list->tFai = tFai;

    checksum_list = checksum_list->nextChecksum;
  }

  return 0;
}

/* Clean-up the Faidx objects when the server is restarted */

static apr_status_t Faidx_cleanup_fais(void* server_cfg) {
  Faidx_Obj_holder* FaiList = ((mod_Faidx_svr_cfg*)server_cfg)->FaiList;

  ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
	       "Cleaning up Fais");

  while(FaiList->nextFai != NULL) {
    if(FaiList->pFai != NULL) {

#ifdef DEBUG
  ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
	       "Destroying %s", FaiList->fai_set_handler);
#endif

      fai_destroy(FaiList->pFai);
      FaiList->pFai = NULL;
    }

    FaiList = (Faidx_Obj_holder*)FaiList->nextFai;
  }

  return APR_SUCCESS;
}

/* Remove a Faidx from the server linked list */

static void mod_Faidx_remove_fai(Faidx_Obj_holder** parent_Fai_Obj, Faidx_Obj_holder* Fai_Obj) {
  Faidx_Obj_holder* temp_Fai_Obj;

  if((Fai_Obj == NULL) || (Fai_Obj->nextFai == NULL)) {
    return;
  }

  temp_Fai_Obj = Fai_Obj;

  *parent_Fai_Obj = Fai_Obj->nextFai;

  /* TODO Figure out how to safely deallocate temp_Fai_Obj here */
}

/* Fetch a Faidx object with a given set name */

static Faidx_Obj_holder* mod_Faidx_fetch_fai(server_rec* s, apr_pool_t* pool, const char* hFai, int make) {
  Faidx_Obj_holder* Fai_Obj;
  mod_Faidx_svr_cfg* svr
    = ap_get_module_config(s->module_config, &faidx_module);

    Fai_Obj = svr->FaiList;

  /* Loop through and find the faidx object */
  while(Fai_Obj->nextFai != NULL) {
#ifdef DEBUG
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
		 "Examining Faidx %s", Fai_Obj->fai_set_handler);
#endif
    if(!apr_strnatcasecmp(Fai_Obj->fai_set_handler, hFai))
      break;

    Fai_Obj = (Faidx_Obj_holder*)Fai_Obj->nextFai;
  }

  /* If we're on the last object, which is always a telomere-ish
     empty cap of a uninitialized Fai_Obj */
  if(Fai_Obj->nextFai == NULL) {
    /* Have we been asked to make the object if we didn't find it? */
    if(make) {
#ifdef DEBUG
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
		 "Making faidx_obj_holder %s", hFai);
#endif
      /* Create a new end-of-linked list Faidx object */
      Fai_Obj->nextFai = apr_pcalloc(pool, sizeof(Faidx_Obj_holder));
      Fai_Obj->fai_set_handler = apr_pstrdup(pool, hFai);
      Fai_Obj->pFai = NULL;
      /* Set the next pointer in the new last object to NULL */
      ((Faidx_Obj_holder*)Fai_Obj->nextFai)->nextFai = NULL;
      ((Faidx_Obj_holder*)Fai_Obj->nextFai)->pFai = NULL;
      ((Faidx_Obj_holder*)Fai_Obj->nextFai)->fai_path = NULL;
      ((Faidx_Obj_holder*)Fai_Obj->nextFai)->fai_set_handler = NULL;
    } else {
      /* We weren't asked to make a new object, and didn't find one, NULL */
      return NULL;
    }
  }

#ifdef DEBUG
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
		 "Returning Faidx %s", Fai_Obj->fai_set_handler);
#endif
  /* We should have a faidx object at this point */
  return Fai_Obj;

}

/* This could be generalized further so it can also handle searching the Fai linked list */

static Faidx_Checksum_obj* mod_Faidx_fetch_checksum(server_rec* s, apr_pool_t* pool, const char* checksum, int checksum_type, int make) {
  Faidx_Checksum_obj* Checksum_Obj;
  mod_Faidx_svr_cfg* svr
    = ap_get_module_config(s->module_config, &faidx_module);

  if(checksum_type == CHECKSUM_MD5) {
    Checksum_Obj = svr->MD5List;
  } else if(checksum_type == CHECKSUM_SHA1) {
    Checksum_Obj = svr->SHA1List;
  } else {
    return NULL;
  }

  while(Checksum_Obj->nextChecksum != NULL) {
#ifdef DEBUG
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
		 "Examining Checksum %s", Checksum_Obj->checksum);
#endif
    if(!strcmp(Checksum_Obj->checksum, checksum))
      break;

    Checksum_Obj = (Faidx_Checksum_obj*)Checksum_Obj->nextChecksum;
  }

  /* If we're on the last object, which is always a telomere-ish
     empty cap of a uninitialized Checksum_Obj */
  if(Checksum_Obj->nextChecksum == NULL) {
    /* Have we been asked to make the object if we didn't find it? */
    if(make) {
#ifdef DEBUG
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
		 "Making faidx_checksum_obj %s", checksum);
#endif
      /* Create a new end-of-linked list Faidx object */
      Checksum_Obj->nextChecksum = apr_pcalloc(pool, sizeof(Faidx_Checksum_obj));
      Checksum_Obj->checksum = apr_pstrdup(pool, checksum);
      Checksum_Obj->tFai = NULL;
      /* Set the next pointer in the new last object to NULL */
      ((Faidx_Checksum_obj*)Checksum_Obj->nextChecksum)->nextChecksum = NULL;
      ((Faidx_Checksum_obj*)Checksum_Obj->nextChecksum)->tFai = NULL;
      ((Faidx_Checksum_obj*)Checksum_Obj->nextChecksum)->checksum = NULL;
      ((Faidx_Checksum_obj*)Checksum_Obj->nextChecksum)->set = NULL;
      ((Faidx_Checksum_obj*)Checksum_Obj->nextChecksum)->location_name = NULL;
    } else {
      /* We weren't asked to make a new object, and didn't find one, NULL */
      return NULL;
    }
  }

#ifdef DEBUG
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
		 "Returning Checksum obj %s", Checksum_Obj->checksum);
#endif
  /* We should have a checksum object at this point */
  return Checksum_Obj;
}

/* Remove a Checksum from the server linked list */

static void mod_Faidx_remove_checksum(Faidx_Checksum_obj** parent_Checksum_Obj, Faidx_Checksum_obj* Checksum_Obj) {
  Faidx_Checksum_obj* temp_Checksum_Obj;

  if((Checksum_Obj == NULL) || (Checksum_Obj->nextChecksum == NULL)) {
    return;
  }

  temp_Checksum_Obj = Checksum_Obj;

  *parent_Checksum_Obj = Checksum_Obj->nextChecksum;

  /* TODO Figure out how to safely deallocate temp_Checksum_Obj here */
}

/* I don't remember why this is here */
static const char* modFaidx_set_handler(cmd_parms* cmd, void* cfg, const char* HandlerName) {
  /*  Faidx_Obj_holder* Fai_Obj;
#ifdef DEBUG
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, NULL,
		 "This is being called: %s", HandlerName);
#endif

  Fai_Obj = mod_Faidx_fetch_fai(cmd->server, cmd->pool, HandlerName, 1);
  */
  return NULL;
}

/* From a configuration directive, create the entry in the Faidx set for
   this fai set. It's alright to reallocate a set name again since we
   don't actually make the faidx objects until the post_hook. */
static const char* modFaidx_init_set(cmd_parms* cmd, void* cfg, const char* SetName, const char* SetFilename) {
  Faidx_Obj_holder* Fai_Obj;

#ifdef DEBUG
  ap_log_error(APLOG_MARK, APLOG_ERR, 0, cmd->server,
		   "FaidxSet directive %s, %s", SetName, SetFilename);
#endif
  Fai_Obj = mod_Faidx_fetch_fai(cmd->server, cmd->pool, SetName, 1);

  if(Fai_Obj == NULL) {
    return "Error, could not allocate Faidx set";
  }

  /* TODO Check here, if reallocating an existing set, deallocate the fai_path in the
     existing Fai_Obj */

  Fai_Obj->fai_path = apr_pstrdup(cmd->pool, SetFilename);

  if(Fai_Obj->fai_path == NULL) {
    ap_log_error(APLOG_MARK, APLOG_EMERG, 0, cmd->server,
		 "Error, unable to allocate string for fai path!");
  }

  return NULL;
}

static const char* modFaidx_set_MD5(cmd_parms* cmd, void* cfg, const char* Checksum, const char* SetName, const char* Location) {
  Faidx_Checksum_obj* Checksum_Obj;

#ifdef DEBUG
  ap_log_error(APLOG_MARK, APLOG_ERR, 0, cmd->server,
	       "FaidxMD5 directive %s, %s, %s", Checksum, SetName, Location);
#endif
  Checksum_Obj = mod_Faidx_fetch_checksum(cmd->server, cmd->pool, Checksum, CHECKSUM_MD5, 1);

  if(Checksum_Obj == NULL) {
    return "Error, could not allocate Faidx checksum object";
  }

  /* TODO Check here, if reallocating an existing set, deallocate the checksum in the
     existing Checksum_Obj */

  Checksum_Obj->set = apr_pstrdup(cmd->pool, SetName);
  Checksum_Obj->location_name = apr_pstrdup(cmd->pool, Location);

  if(Checksum_Obj->location_name == NULL) {
    ap_log_error(APLOG_MARK, APLOG_EMERG, 0, cmd->server,
		 "Error, unable to allocate string for location path! (%s)", SetName);
  }

  return NULL;
}

static const char* modFaidx_set_SHA1(cmd_parms* cmd, void* cfg, const char* Checksum, const char* SetName, const char* Location) {
  Faidx_Checksum_obj* Checksum_Obj;

#ifdef DEBUG
  ap_log_error(APLOG_MARK, APLOG_ERR, 0, cmd->server,
	       "FaidxSHA1 directive %s, %s, %s", Checksum, SetName, Location);
#endif
  Checksum_Obj = mod_Faidx_fetch_checksum(cmd->server, cmd->pool, Checksum, CHECKSUM_SHA1, 1);

  if(Checksum_Obj == NULL) {
    return "Error, could not allocate Faidx checksum object";
  }

  /* TODO Check here, if reallocating an existing set, deallocate the checksum in the
     existing Checksum_Obj */

  Checksum_Obj->set = apr_pstrdup(cmd->pool, SetName);
  Checksum_Obj->location_name = apr_pstrdup(cmd->pool, Location);

  if(Checksum_Obj->location_name == NULL) {
    ap_log_error(APLOG_MARK, APLOG_EMERG, 0, cmd->server,
		 "Error, unable to allocate string for location path! (%s)", SetName);
  }

  return NULL;
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
