#ifndef SHUSO_SETTINGS_H
#define SHUSO_SETTINGS_H
#include <shuttlesock/common.h>

extern shuso_module_t shuso_config_module;

#define SHUSO_SETTING_BLOCK_OPTIONAL 2

struct shuso_setting_block_s {
  //this is for debugging mostly
  const char     *name;
  //const char     *value_string;
  lua_reference_t ref;
}; // shuso_setting_block_t

typedef struct {
  lua_reference_t  ref;
  bool             parsed;
  struct {
    shuso_setting_block_t  *root;
    shuso_setting_block_t **array;
    size_t                 count;
  }                blocks;
  struct {
    const char       *str;
    size_t            len;
    lua_reference_t   ref;
  }                serialized;
} shuso_config_module_ctx_t;

struct shuso_setting_value_s {
  shuso_setting_value_type_t type;
  union {
    bool        value_bool;
    int         value_int;
    double      value_float;
    const char *value_string;
  };
  size_t        len;
}; //shuso_setting_value_t


struct shuso_setting_values_s {
  uint16_t              count;
  shuso_setting_value_t array[];
};// shuso_setting_values_t

struct shuso_setting_s {
  const char             *name;
  const char             *aliases;
  const char             *path;
  const char             *description;
  const char             *nargs;
  shuso_setting_value_t  *default_values;
  int                     block; // 0/false - no, 1/true - yes, SHUSO_SETTING_BLOCK_OPTIONAL - maybe
  shuso_setting_values_t *values;
  shuso_setting_values_t *inherited_values;
  lua_reference_t         config_setting_ref;
}; // shuso_setting_t

extern shuso_setting_value_t SHUTTLESOCK_VALUES_END;
extern shuso_setting_t SHUTTLESOCK_SETTINGS_END;

typedef void shuso_setting_context_t; //placeholder

//For module developers
shuso_setting_t *shuso_setting(shuso_t *S, const char *name, shuso_setting_context_t *ctx);
bool shuso_setting_set_error(shuso_t *S, const char *fmt, ...);

//for shuttlesock developers' eyes only
bool shuso_config_register_setting(shuso_t *S, shuso_setting_t *setting, shuso_module_t *module);

bool shuso_config_system_initialize(shuso_t *S);
bool shuso_config_system_generate(shuso_t *S);

bool shuso_config_file_parse(shuso_t *S, const char *config_file_path);
bool shuso_config_string_parse(shuso_t *S, const char *config);

#endif //SHUSO_SETTINGS_H
