#ifndef SHUTTLESOCK_CONFIG_MODULE_H
#define SHUTTLESOCK_CONFIG_MODULE_H

#include <shuttlesock/common.h>
#include <shuttlesock/instring.h>

extern shuso_module_t shuso_config_module;

#define SHUSO_SETTING_BLOCK_OPTIONAL 2

typedef struct shuso_setting_block_s {
  shuso_setting_t             *setting;
  const char                  *path;
  shuso_module_context_list_t context_list;
} shuso_setting_block_t;

typedef struct {
  struct {
    const shuso_setting_block_t  *root;
    const shuso_setting_block_t **array;
    size_t                  count;
  }                blocks;
} shuso_config_module_common_ctx_t;


typedef struct shuso_setting_s {
  const char             *name;
  const char             *module;
  const char             *raw_name;
  const char             *path;
  struct {
    shuso_instrings_t      *merged;
    shuso_instrings_t      *local;
    shuso_instrings_t      *inherited;
    shuso_instrings_t      *defaults;
  }                       instrings;
  shuso_setting_block_t  *parent_block;
  shuso_setting_block_t  *block;
} shuso_setting_t;

typedef enum {
  SHUSO_SETTING_MERGED =      0,
  SHUSO_SETTING_LOCAL =       1,
  SHUSO_SETTING_INHERITED =   2,
  SHUSO_SETTING_DEFAULT=      3
} shuso_setting_value_merge_type_t;

typedef enum {
  SHUSO_SETTING_BOOLEAN = 16,
  SHUSO_SETTING_INTEGER = 17,
  SHUSO_SETTING_NUMBER  = 18,
  SHUSO_SETTING_SIZE    = 19, 
  SHUSO_SETTING_STRING  = 20,
  SHUSO_SETTING_BUFFER  = 21
} shuso_setting_value_type_t;

#define SHUTTLESOCK_SETTINGS_END (shuso_module_setting_t ){.name = NULL}

//For module developers
bool shuso_setting_value(shuso_t *S, const shuso_setting_t *setting, size_t nval, shuso_setting_value_merge_type_t mergetype, shuso_setting_value_type_t valtype, void *ret);
  
shuso_setting_t *shuso_setting(shuso_t *S, const shuso_setting_block_t *block, const char *name);

shuso_setting_block_t *shuso_setting_parent_block(shuso_t *S, const shuso_setting_t *setting);

/*
 * size_t shuso_setting_values_count(shuso_t *S, const shuso_setting_t *setting);
 * size_t shuso_setting_values_count(shuso_t *S, const shuso_setting_t *setting, shuso_setting_value_merge_type_t mergetype);
 * 
 * get number of values in a setting. if mergetype is omitted, assumes SHUSO_SETTING_MERGED
 */
#define ___shuso_setting_values_count_vararg(_1,_2,NAME,...) NAME
#define shuso_setting_values_count(S, ...) ___shuso_setting_values_count_vararg(__VA_ARGS__, __shuso_setting_values_count, shuso_setting_values_count_merged, ___END__VARARG__LIST__)(S, __VA_ARGS__)
#define shuso_setting_values_count_merged(S, setting) __shuso_setting_values_count(S, setting, SHUSO_SETTING_MERGED)
size_t __shuso_setting_values_count(shuso_t *S, const shuso_setting_t *setting, shuso_setting_value_merge_type_t mergetype);

bool shuso_setting_boolean(shuso_t *S, shuso_setting_t *setting, int n, bool *ret);
bool shuso_setting_integer(shuso_t *S, shuso_setting_t *setting, int n, int *ret);
bool shuso_setting_number(shuso_t *S, shuso_setting_t *setting, int n, double *ret);
bool shuso_setting_size(shuso_t *S, shuso_setting_t *setting, int n, size_t *ret);
bool shuso_setting_string(shuso_t *S, shuso_setting_t *setting, int n, shuso_str_t *ret);
bool shuso_setting_buffer(shuso_t *S, shuso_setting_t *setting, int n, const shuso_buffer_t **ret);
bool shuso_setting_string_matches(shuso_t *S, shuso_setting_t *setting, int n, const char *lua_matchstring);


bool shuso_config_match_setting_path(shuso_t *S, const shuso_setting_t *setting, const char *path);
bool shuso_config_match_block_path(shuso_t *S, const shuso_setting_block_t *block, const char *path);
#define shuso_config_match_path(S, thing, path) \
  _Generic((thing), \
    shuso_setting_t *       : shuso_config_match_setting_path, \
    shuso_setting_block_t * : shuso_config_match_block_path \
  )(S, thing, path)


bool shuso_config_setting_error(shuso_t *S, shuso_setting_t *s, const char *fmt, ...);
bool shuso_config_block_error(shuso_t *S, shuso_setting_block_t *b, const char *fmt, ...);
#define shuso_config_error(S, thing, ...) \
  _Generic((thing), \
    shuso_setting_t *       : shuso_config_setting_error, \
    shuso_setting_block_t * : shuso_config_block_error \
  )(S, thing, __VA_ARGS__)

bool shuso_configure_string(shuso_t *S, const char *title, const char *str);
bool shuso_configure_file(shuso_t *S, const char *path);
  
#endif //SHUTTLESOCK_CONFIG_MODULE_H
