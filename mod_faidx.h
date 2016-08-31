#ifndef __MOD_FAIDX_H__
#define __MOD_FAIDX_H__

#include "htslib/faidx.h"

static const int MAX_SIZE = 16384;
static const int MAX_FASTA_LINE_LENGTH = 70;

#define CONTENT_JSON 1
#define CONTENT_FASTA 2
#define CONTENT_WWWFORM 3

/* Representation of a Faidx object */
typedef struct {
  faidx_t* pFai;
  /* Name of the fai set, ie homo_sapiens_grch37 */
  char* fai_set_handler;
  /* Path to the .faa or .gz, including filename */
  char* fai_path;
  void* nextFai;
} Faidx_Obj_holder;

/* server config structure */
typedef struct {
  Faidx_Obj_holder* FaiList;
} mod_Faidx_svr_cfg;


static int Faidx_handler(request_rec* r);
static int Faidx_sets_handler(request_rec* r, Faidx_Obj_holder* Fais);
static int Faidx_locations_handler(request_rec* r, char* set);
static int mod_Faidx_hook_post_config(apr_pool_t *pconf, apr_pool_t *plog,
                                       apr_pool_t *ptemp, server_rec *s);
static apr_status_t Faidx_cleanup_fais(void* server_cfg);
static void mod_Faidx_hooks(apr_pool_t* pool);
static void mod_Faidx_remove_fai(Faidx_Obj_holder** parent_Fai_Obj, Faidx_Obj_holder* Fai_Obj);

static void* mod_Faidx_svr_conf(apr_pool_t* pool, server_rec* s);
static Faidx_Obj_holder* mod_Faidx_fetch_fai(server_rec* s, apr_pool_t* pool,  const char* hFai, int make);
static const char* modFaidx_set_handler(cmd_parms* cmd, void* cfg, const char* HandlerName);
static const char* modFaidx_init_set(cmd_parms* cmd, void* cfg, const char* SetName, const char* SetFilename);
void print_fasta(request_rec* r, char* header, char* seq, int seq_len);

static apr_hash_t *parse_form_from_string(request_rec *r, char *args);
static apr_hash_t* parse_form_from_GET(request_rec *r);
static int parse_form_from_POST(request_rec* r, apr_hash_t** form, int json_data);
static apr_hash_t *parse_json_from_string(request_rec * r, char *p);
static char* parse_key(request_rec * r, char **p);
static char* get_value(request_rec * r, char** s);

static char* strrstr(char*, char*);

#endif
