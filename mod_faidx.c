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
#include <unistd.h>
#include "mod_faidx.h"

/* Hook our handler into Apache at startup */
static void mod_Faidx_hooks(apr_pool_t* pool) {
  ap_hook_handler(Faidx_handler, NULL, NULL, APR_HOOK_MIDDLE) ;

  ap_hook_post_config(mod_Faidx_hook_post_config,
		      NULL, NULL, APR_HOOK_FIRST);
}

static void* mod_Faidx_svr_conf(apr_pool_t* pool, server_rec* s) {
  mod_Faidx_svr_cfg* svr = apr_pcalloc(pool, sizeof(mod_Faidx_svr_cfg));
  /* Why are we pre-allocating the first element? Can this be changed later? */
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

  Fai_Obj = svr->FaiList;
  if(Fai_Obj == NULL) {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error (Fai_Obj) is null, it shouldn't be!");
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  /* Display the form data */
  if(r->method_number == M_GET) {
    formdata = parse_form_from_GET(r);
  }
  else if(r->method_number == M_POST) {
    const char* ctype = apr_table_get(r->headers_in, "Content-Type");
    if(ctype && (strcasestr(ctype, 
			    "application/x-www-form-urlencoded")
		 != NULL)) {
      rv = parse_form_from_POST(r, &formdata);
    }
#ifdef DEBUG
    else {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "content-type not set correctly, is: %s", ctype);
    }
#endif
  } else {
    ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		  "Error, no data given to module, where's your location?");
    return HTTP_INTERNAL_SERVER_ERROR;
  }

  ap_set_content_type(r, "application/json") ;
  /*  ap_rputs(
	   "<!DOCTYPE HTML PUBLIC \"-//W3C//DTD HTML 4.01//EN\">\n", r) ;
    ap_rputs(
	   "<html><head><title>Faidx</title></head>", r) ;
	   ap_rputs("<body>", r) ;*/

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
    ap_rputs( "{\n", r );

    /* Find the set we've been asked to look up in */
    set = apr_hash_get(formdata, "set", APR_HASH_KEY_STRING);
    if(set == NULL) {
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
		    "Error, no set specified!");
      ap_rputs( "    \"Error\": \"No set specified\"\n}\n", r );
      return OK;
    } else {
      /* Spit out the set we'll be using in the JSON response */
      ap_rprintf(r,"    \"set\": \"%s\",\n", set);
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
      //      fai_fetch(tFai_Obj->pFai, key, seq_len);

#ifdef DEBUG
      ap_log_rerror(APLOG_MARK, APLOG_ERR, rv, r,
			  "Received sequence %s", seq);
#endif

      if(*seq_len >= 0) {
      /* We received a result */
        ap_rprintf(r, "    \"%s\": \"%s\"\n", key, seq);
      } else {
        ap_rprintf(r, "    \"%s\": \"ERROR\"\n", key);
      }
    }


      /* DO WE NEED TO DEALLOCATE seq ? */

      Loc_count++;
  }

    /* Close the JSON */
    ap_rputs( "}\n", r );

    /*  ap_rputs("</body></html>", r) ;*/
    return OK;
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
    return OK; /* This would be the first time through */
  }
#ifdef DEBUG
  else {
    ap_log_error(APLOG_MARK, APLOG_ERR, 0, s,
		 "We're in the second iteration, carry on init, pid %d", (int)getpid());
  }
#endif

  Fai_Obj = svr->FaiList;
  prev_Fai_Obj = &(svr->FaiList);

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

static int parse_form_from_POST(request_rec* r, apr_hash_t** form) {
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
  *form = parse_form_from_string(r, buf);
  
  return OK;

}
