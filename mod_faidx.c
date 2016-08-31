/* Faidx module
 *
 * Module to load Faidx indices and return sub-sequences
 * efficiently.
 *
 */

#include <httpd.h>
#include <http_protocol.h>
#include <http_config.h>
#include <http_log.h>
#include <apr_strings.h>
#include <apr_hash.h>
#include <apr_escape.h>
#include <unistd.h>
#include <string.h>
#include "mod_faidx.h"

/* Hook our handler into Apache at startup */
static void mod_Faidx_hooks(apr_pool_t* pool) {
  ap_hook_handler(Faidx_handler, NULL, NULL, APR_HOOK_MIDDLE) ;

  ap_hook_post_config(mod_Faidx_hook_post_config,
		      NULL, NULL, APR_HOOK_FIRST);
}

static void* mod_Faidx_svr_conf(apr_pool_t* pool, server_rec* s) {
  mod_Faidx_svr_cfg* svr = apr_pcalloc(pool, sizeof(mod_Faidx_svr_cfg));
  svr->FaiList = apr_pcalloc(pool, sizeof(Faidx_Obj_holder));
  svr->FaiList->pFai = NULL;
  svr->FaiList->fai_set_handler = NULL;
  svr->FaiList->nextFai = NULL;
  return svr;
}

static const command_rec mod_Faidx_cmds[] = {
  AP_INIT_TAKE2("FaidxSet", modFaidx_init_set, NULL, RSRC_CONF,
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
  Faidx_Obj_holder* Fai_Obj = NULL;
  mod_Faidx_svr_cfg* svr = NULL;
  faidx_t* pFai = NULL;
  int results;
  char* uri;
  char* uri_ptr;
  const char* ctype_str;
  int ctype;
  const char* accept_str;
  int accept;
  char* sets_verb = "sets";
  char* locations_verb = "locations/";

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

  /* We only accept in POST bodies urlencoded forms or json,
     if you don't say it's a form, we're assuming it's json */
  ctype_str = apr_table_get(r->headers_in, "Content-Type");
  if(ctype_str && (strcasestr(ctype_str, 
			  "application/x-www-form-urlencoded")
	       != NULL)) {
    ctype = CONTENT_WWWFORM;
  } else {
    ctype = CONTENT_JSON;
  }

  /* We only speak fasta or json, unless you ask for fasta,
     you're getting json */
  accept_str = apr_table_get(r->headers_in, "Accept");
  if(accept_str && (strcasestr(accept_str,
			       "text/x-fasta")
		    != NULL)) {
    accept = CONTENT_FASTA;
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

  /* First we're seeing if we've been asked for the list of sets */
  uri_ptr = strrstr(uri, sets_verb);
  if(uri_ptr != NULL && strlen(uri_ptr) == strlen(sets_verb)) {
      /* We've been asked for the list of sets, find that and return */
    return Faidx_sets_handler(r, Fai_Obj);
  }

  uri_ptr = strrstr(uri, locations_verb);
  if(uri_ptr != NULL) {
    uri_ptr += strlen(locations_verb);

    /* We've found the locations verb, we've jumped forward
       that length, now see if we still have more string.
       If so, we need to start finding the set name. */
    if(uri_ptr && *uri_ptr) {
      /* uri_ptr is pointing to the set we want locations for,
	 fetch those and return */
      return Faidx_locations_handler(r, uri_ptr);

    }
  }

  /* Display the form data */
  if(r->method_number == M_GET) {
    formdata = parse_form_from_GET(r);
  }
  else if(r->method_number == M_POST) {
    if(ctype == CONTENT_WWWFORM) {
      rv = parse_form_from_POST(r, &formdata, 0);
    }
    else {
      rv = parse_form_from_POST(r, &formdata, 1);
    }
  } else {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error, no data given to module, where's your location?");
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  if(accept == CONTENT_FASTA) {
    ap_set_content_type(r, "text/x-fasta") ;
  } else {
    ap_set_content_type(r, "application/json") ;
  }

  if(rv != OK) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error, reading form data");
    return HTTP_INTERNAL_SERVER_ERROR;
  }
  else if(formdata == NULL) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error, no form data found");
    return OK;
  } else {
    Faidx_Obj_holder* tFai_Obj;
    char *key;
    char *set;
    char *val;
    const char *delim = "&";
    char *last;
    unsigned int Loc_count;
    char *seq;
    int *seq_len = apr_pcalloc(r->pool, sizeof(int));
    /* Is this a memory leak? */

    Loc_count = 0;

    /* Start JSON header */
    if(accept != CONTENT_FASTA) {
      ap_rputs( "{\n", r );
    }

    /* Find the set we've been asked to look up in */
    set = apr_hash_get(formdata, "set", APR_HASH_KEY_STRING);
    if(set == NULL) {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Error, no set specified!");
      ap_rputs( "    \"Error\": \"No set specified\"\n}\n", r );
      return OK;
    } else if(accept != CONTENT_FASTA) {
      /* Spit out the set we'll be using in the JSON response */
      ap_rprintf(r,"    \"set\": \"%s\",", set);
    }

    /* Go fetch that faidx object */
    tFai_Obj = mod_Faidx_fetch_fai(r->server, NULL, set, 0);
    if(tFai_Obj != NULL) {
#ifdef DEBUG
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Found Faidx set of type %s", tFai_Obj->fai_set_handler);
#endif
      if(tFai_Obj->pFai == NULL) {
	ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		      "Error, pFai component is NULL!");
	return HTTP_INTERNAL_SERVER_ERROR;
      }
    }

    /* Try and get the one or more locations from the
       request data */
    val = apr_hash_get(formdata, "location", APR_HASH_KEY_STRING);
    if(val == NULL) {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Error, no location(s) specified!");
      ap_rputs( "    \"Error\": \"No location(s) specified\"\n}\n", r );
      return OK;
    }


    /* Go through the location(s) we've been given and 
       look them up in the faidx object */
    for(key = apr_strtok(val, delim, &last); key != NULL;
	key = apr_strtok(NULL, delim, &last)) {

#ifdef DEBUG
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Found location %s", key);
#endif

      seq = fai_fetch(tFai_Obj->pFai, key, seq_len);

#ifdef DEBUG
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
			  "Received sequence %s", seq);
#endif

      if(seq == NULL) {
	seq = apr_pcalloc(r->pool, sizeof(char));
	*seq = '\0';
      }

      if(accept == CONTENT_FASTA) {
	char* header = apr_psprintf(r->pool, "%s [%s]", set, key);
	print_fasta(r, header, seq, *seq_len);
      } else {
	if(Loc_count > 0) {
	  ap_rputs(",\n", r);
	} else {
	  ap_rputs("\n", r);
	}

	ap_rprintf(r, "    \"%s\": \"%s\"", key, seq);
      }

      /* fai_fetch malloc's the sequence returned, we're
	 responsible for freeing it */
      if(seq && *seq_len > 0) {
	free(seq);
	seq = NULL;
      }

      Loc_count++;    
    }

  }

    /* Close the JSON */
    if(accept != CONTENT_FASTA) {
      ap_rputs( "}\n", r );
    }

    return OK;
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
  faidx_keys* keys;
  faidx_keys* t_key;

  Fai_Obj = mod_Faidx_fetch_fai(r->server, NULL, set, 0);

  /* If they do something silly like ask for a non-existent
     set, that's not an error, it's just an empty set to
     send back*/
  if(Fai_Obj == NULL || Fai_Obj->pFai == NULL) {
    ap_rputs( "[]\n", r );
    return OK;
  }

  /* Ask faidx for a list of all available keys */
  keys = faidx_fetch_keys(Fai_Obj->pFai);
  ap_rputs( "[", r );

  while(keys != NULL) {
    if(loc_count > 0) {
      ap_rputs( ",", r );
    }

    ap_rprintf( r, "\"%s\"", keys->key );

    /* We're responsible for nuking the keys as we use them */
    t_key = keys;
    keys = (faidx_keys*)keys->next;
    free(t_key);

    loc_count++;
  }

  ap_rputs( "]\n", r );

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

/* Add error reporting?  "Could not load model" etc */

static int mod_Faidx_hook_post_config(apr_pool_t *pconf, apr_pool_t *plog,
				      apr_pool_t *ptemp, server_rec *s) {

  Faidx_Obj_holder* Fai_Obj;
  Faidx_Obj_holder** prev_Fai_Obj;
  mod_Faidx_svr_cfg* svr
    = ap_get_module_config(s->module_config, &faidx_module);
  void *data = NULL;
  const char *userdata_key = "shm_counter_post_config";

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

  /* Figure out how to safely deallocate temp_Fai_Obj here */
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

  /* Check here, if reallocating an existing set, deallocate the fai_path in the
     existing Fai_Obj */

  Fai_Obj->fai_path = apr_pstrdup(cmd->pool, SetFilename);

  if(Fai_Obj->fai_path == NULL) {
    ap_log_error(APLOG_MARK, APLOG_EMERG, 0, cmd->server,
		 "Error, unable to allocate string for fai path!");
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

  if(args == NULL) {
    return NULL;
  }

  form = apr_hash_make(r->pool);
  
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
