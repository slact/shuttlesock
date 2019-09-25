#ifndef SHUSO_SETTINGS_H
#define SHUSO_SETTINGS_H
#include <shuttlesock/common.h>

struct shuso_setting_value_s {
  shuso_setting_value_type_t type;
  union {
    bool        value_bool;
    int         value_int;
    double      value_float;
    double      value_time;
    const char *value_string;
  };
  bool          inherited;
  bool          defaulted;
}; //shuso_settimg_value_t


struct shuso_setting_values_s {
  uint16_t              count;
  shuso_setting_value_t values[];
};// shuso_setting_values_t

struct shuso_setting_s {
  const char             *name;
  const char             *path;
  const char             *description;
  const char             *nargs;
  shuso_setting_values_t *default_values;
  shuso_setting_values_t *values;
  shuso_setting_values_t *inherited_values;
  lua_reference_t         config_lua_ref;
}; // shuso_setting_t

extern shuso_setting_value_t SHUTTLESOCK_VALUE_END;

typedef void shuso_setting_context_t; //placeholder

//For module developers
shuso_setting_t *shuso_setting(shuso_t *S, const char *name, shuso_setting_context_t *ctx);
bool shuso_setting_set_error(shuso_t *S, const char *fmt, ...);


//for shuttlesock developers' eyes only
bool shuso_module_register_setting(shuso_t *S, shuso_module_t *module, shuso_setting_t *setting);

#endif //SHUSO_SETTINGS_H
