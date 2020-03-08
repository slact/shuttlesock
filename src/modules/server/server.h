#ifndef SHUTTLESOCK_SERVER_MODULE_H
#define SHUTTLESOCK_SERVER_MODULE_H

#include <shuttlesock/common.h>

void shuttlesock_server_module_prepare(shuso_t *S, void *pd);

typedef struct {
  int                 lua_hostnum;
  shuso_hostinfo_t    host;
  struct {
    size_t              count;
    struct {
      shuso_setting_block_t *block;
      shuso_setting_t       *setting;
    }                  *array;
  }                   config;
  shuso_io_t          io;
} shuso_server_binding_t;

typedef struct {
  struct {
    size_t                  count;
    shuso_server_binding_t *array;
  } binding;
} shuso_server_ctx_t;

#endif //SHUTTLESOCK_SERVER_MODULE_H
