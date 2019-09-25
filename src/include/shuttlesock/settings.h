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

#endif //SHUSO_SETTINGS_H
