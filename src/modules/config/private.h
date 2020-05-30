#ifndef SHUTTLESOCK_CONFIG_MODULE_PRIVATE_H
#define SHUTTLESOCK_CONFIG_MODULE_PRIVATE_H

#include <shuttlesock/common.h>

//for shuttlesock developers' eyes only
bool shuso_config_register_setting(shuso_t *S, shuso_module_setting_t *setting, shuso_module_t *module);
bool shuso_config_register_variable(shuso_t *S, shuso_module_variable_t *variable, shuso_module_t *module);

bool shuso_config_system_initialize(shuso_t *S);
bool shuso_config_system_initialize_worker(shuso_t *workerState, shuso_t *managerState);
bool shuso_config_system_generate(shuso_t *S);

bool luaS_get_config_pointer_ref(lua_State *L, const void *ptr);
bool luaS_pcall_config_method(lua_State *L, const char *method_name, int nargs, int nresults);
bool luaS_config_pointer_ref(lua_State *L, const void *ptr);

#endif //SHUTTLESOCK_CONFIG_MODULE_PRIVATE_H
