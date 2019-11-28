#include <shuttlesock/common.h>

typedef struct {
  lua_State       *Lshared;
  lua_reference_t  ref;
  pthread_mutex_t  lock;
}

luaL_Reg shuttlesock_shared_table[] = {
  
  
};
