#ifndef SHUTTLESOCK_SERVER_MODULE_H
#define SHUTTLESOCK_SERVER_MODULE_H

#include <shuttlesock/common.h>

void shuttlesock_server_module_prepare(shuso_t *S, void *pd);

typedef struct {
  int                 lua_hostnum;
  const char         *server_type;
  shuso_hostinfo_t    host;
  struct {
    size_t              count;
    struct {
      shuso_setting_block_t *block;
      shuso_setting_t       *setting;
    }                  *array;
    shuso_setting_block_t *common_parent_block;
  }                   config;
} shuso_server_binding_t;

typedef struct {
  shuso_socket_t         *socket;
  shuso_server_binding_t *binding;
} shuso_server_accept_data_t;

typedef struct {
  union {
    struct sockaddr     any;
    struct sockaddr_in  inet;
    struct sockaddr_in6 inet6;
  }                 sockaddr;
  int                     fd;
  shuso_event_t          *accept_event;
  shuso_server_binding_t *binding;
} shuso_server_tentative_accept_data_t;

typedef struct {
  struct {
    size_t                  count;
    shuso_server_binding_t *array;
  } binding;
} shuso_server_ctx_t;

#endif //SHUTTLESOCK_SERVER_MODULE_H
