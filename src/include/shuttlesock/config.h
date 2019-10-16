#ifndef SHUSO_SETTINGS_H
#define SHUSO_SETTINGS_H
#include <shuttlesock/common.h>

extern shuso_module_t shuso_config_module;

#define SHUSO_SETTING_BLOCK_OPTIONAL 2

struct shuso_setting_block_s {
  shuso_setting_t             *setting;
  shuso_module_context_list_t context_list;
}; // shuso_setting_block_t

typedef struct {
  bool             parsed;
  struct {
    const shuso_setting_block_t  *root;
    const shuso_setting_block_t **array;
    size_t                  count;
  }                blocks;
} shuso_config_module_ctx_t;

struct shuso_setting_value_s {
  struct {
    unsigned    boolean:1;
    unsigned    integer:1;
    unsigned    number:1;
    unsigned    string:1;
  }           valid;
  bool        boolean;
  int         integer;
  double      number;
  const char *string;
  size_t      string_len;
  const char *raw;
  size_t      raw_len;
  
}; //shuso_setting_value_t


struct shuso_setting_values_s {
  uint16_t              count;
  shuso_setting_value_t array[];
};// shuso_setting_values_t

struct shuso_setting_s {
  const char             *name;
  const char             *module;
  const char             *raw_name;
  struct {
    shuso_setting_values_t *merged;
    shuso_setting_values_t *local;
    shuso_setting_values_t *inherited;
    shuso_setting_values_t *defaults;
  }                       values;
  shuso_setting_block_t  *block;
}; // shuso_setting_t

extern shuso_setting_value_t SHUTTLESOCK_VALUES_END;
extern shuso_module_setting_t SHUTTLESOCK_SETTINGS_END;

//For module developers
shuso_setting_t *shuso_setting(shuso_t *S, const shuso_setting_block_t *block, const char *name);
const shuso_setting_value_t *shuso_setting_check_value(shuso_t *S, const shuso_setting_block_t *block, const char *name, int nval);
bool shuso_setting_check_boolean(shuso_t *S, const shuso_setting_block_t *block,  const char *name, int n, bool *ret);
bool shuso_setting_check_integer(shuso_t *S, const shuso_setting_block_t *block,  const char *name, int n, int *ret);
bool shuso_setting_check_number(shuso_t *S, const shuso_setting_block_t *block,  const char *name, int n, double *ret);
bool shuso_setting_check_string(shuso_t *S, const shuso_setting_block_t *block,  const char *name, int n, const char **ret);


bool shuso_setting_set_error(shuso_t *S, const char *fmt, ...);

//for shuttlesock developers' eyes only
bool shuso_config_register_setting(shuso_t *S, shuso_module_setting_t *setting, shuso_module_t *module);

bool shuso_config_system_initialize(shuso_t *S);
bool shuso_config_system_initialize_worker(shuso_t *workerState, shuso_t *managerState);
bool shuso_config_system_generate(shuso_t *S);

bool shuso_config_file_parse(shuso_t *S, const char *config_file_path);
bool shuso_config_string_parse(shuso_t *S, const char *config);

#endif //SHUSO_SETTINGS_H
