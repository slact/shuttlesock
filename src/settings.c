#include <shuttlesock/settings.h>
#include <shuttlesock.h>
#include <shuttlesock/lua_bridge.h>

shuso_setting_value_t SHUTTLESOCK_VALUE_END = {
  .type = SHUSO_VALUE_END_SENTINEL,
};


bool shuso_module_register_setting(shuso_t *S, shuso_module_t *module, shuso_setting_t *setting) {
  return true;
}
