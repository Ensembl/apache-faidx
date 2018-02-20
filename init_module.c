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

/*

Configuration callback that should be placed in the main module initialization:

AP_INIT_TAKE1(SEQ_ENDPOINT_DIRECTIVE, ap_set_string_slot,
 (void *)APR_OFFSETOF(mod_Faidx_svr_cfg, endpoint_base),
 RSRC_CONF, "Base URI for module endpoints"),
AP_INIT_FLAG(SEQFILE_CACHESIZE_DIRECTIVE, ap_set_int_slot,
 (void *)APR_OFFSETOF(mod_Faidx_svr_cfg, cachesize),
 RSRC_CONF, "Set the cache size for seqfiles"),
AP_INIT_TAKE1(LABELS_ENDPOINT_DIRECTIVE, ap_set_flag_slot,
 (void *)APR_OFFSETOF(mod_Faidx_svr_cfg, labels_endpoints),
 RSRC_CONF, "Enable labels endpoints, limited to 'on' or 'off'"),
AP_INIT_RAW_ARGS(BEGIN_SEQFILE, seqfile_section, NULL, EXEC_ON_READ | RSRC_CONF,
    "Beginning of a sequence file definition section."),

 */

#include "init_module.h"
#include "files_manager.h"

/* 
   Parse a <seqfile> custom tag and initialize the
   corresponding seqfiles in the files manager.

   A seqfile section should look like:
   <seqfile /path/file.faa>
     Seq 1 md5  abcdef1234
     Seq 1 sha1 987654abcd
  </seqfile>
*/

static const char *seqfile_section(cmd_parms * cmd, void * _cfg, const chat * arg) {
  char* endp;
  char* checksum;
  char* file;
  char* ptr;
  char* seqname;
  char* seq_checksum;
  checksum_obj* checksum_holder;
  int line_no = 0;
  int rv;
  char line[MAX_STRING_LEN]; /* expected by ap_cfg_getline */
  /* Cast the module config for convenience */
  mod_Faidx_svr_cfg* cfg = (mod_Faidx_svr_cfg*) _cfg;

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
	ap_log_error(APLOG_MARK, APLOG,WARNING, 0, NULL, "Warning, seqfile %s has been seen before, ignoring", file);
	return NULL;
  }

  checksum = files_mgr_add_seqfile(cfg->files, file, FM_FAIDX);

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
    /* first char? or first non blank? Need to double check this */
    if(*line == '#') continue;

    first = ap_getword_conf_nc(cmd->temp_pool, &ptr);

    if( !strcasecmp(first, END_SEQFILE) ) { /* We've found the end token */
      /* We're done, return */
      return NULL;
    }

    if( !strcasecmp(first, SEQ_DIRECTIVE) ) {
      /* We've found a sequence checksum, parse and add it */
      checksum_holder = parse_seq_token(cmd, &seqname, &seq_checksum, args);

      if(checksum_holder == NULL) { /* We didn't get a checksum holder back, this is an error */
	return apr_psprintf(cmd->pool, "Malformed Seq directive on line %d of Seqfile %s", line_no, file);
      }

      checksum_holder->file = apr_pstrdup(cmd->pool, checksum);

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
	ap_log_error(APLOG_MARK, APLOG,WARNING, 0, NULL, "Warning, hash %s has been seen before, ignoring in %s", seq_checksum, file);
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
  char* ptr;
  char* checksum_type;
  checksum_obj* checksum_holder;

  /* We're going to need to pop the arguments off one by one and in between
     check we still have more arguments. With each call to ap_getword_conf_nc
     ptr is set to the next word or the trailing \0 of the string if we're at
     the end.
   */

  if(*args == '\0') return NULL; /* No arguments? That's an error! */
  *seqname = ap_getword_conf_nc(cmd->temp_pool, &ptr);

  if(*args == '\0') return NULL; /* No more arguments? That's an error! */
  checksum_type = ap_getword_conf_nc(cmd->temp_pool, &ptr);

  if(*args == '\0') return NULL; /* No more arguments? That's an error! */
  *seq_checksum = ap_getword_conf_nc(cmd->temp_pool, &ptr);
  
  if(*args) return NULL; /* More arguments? Oh, you better believe that's an error. */

  /* We're good to make our checksum object */
  checksum_holder = (checksum_obj*)ap_palloc(cmd->pool, sizeof(checksum_obj));

  checksum_holder->checksum_type = apr_pstrdup(cmd->pool, checksum_type);

  return checksum_holder;
}
