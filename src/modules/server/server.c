#include <shuttlesock.h>
#include "server.h"

void shuttlesock_server_module_prepare(shuso_t *S, void *pd) {
  luaS_do_embedded_script(S->lua.state, "shuttlesock_server_module_setup", 0);
}
