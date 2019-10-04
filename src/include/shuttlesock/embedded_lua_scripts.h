#ifndef SHUTTLESOCK_EMBEDDED_LUA_SCRIPTS_H
#define SHUTTLESOCK_EMBEDDED_LUA_SCRIPTS_H
#include <stdbool.h>
#include <stddef.h>
typedef struct shuso_lua_embedded_scripts_s {
  const char *name;
  bool        module;
  const char *script;
  size_t      strlen;
  const char *compiled;
  size_t      compiled_len;
} shuso_lua_embedded_scripts_t;
extern shuso_lua_embedded_scripts_t shuttlesock_lua_embedded_scripts[];
/*
struct shuso_lua_embedded_c_modules_s {
  const char *name;
  void       *func;
  size_t      strlen;
} shuttlesock_lua_embedded_c_modules[];
*/
#endif //SHUTTLESOCK_EMBEDDED_LUA_SCRIPTS_H
