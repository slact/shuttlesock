#ifndef SHUTTLESOCK_RESOLVER_H
#define SHUTTLESOCK_RESOLVER_H

#include <ares.h>
#include <shuttlesock/common.h>

typedef struct shuso_resolver_s shuso_resolver_t;
typedef struct shuso_resolver_socket_s shuso_resolver_socket_t;

struct shuso_resolver_socket_s {
  shuso_resolver_socket_t       *next;
  int                            fd;
  struct {
    shuso_ev_io                    io;
    //shuso_ev_timer               timer;
  }                              ev;
  shuso_resolver_t              *resolver;
};

struct shuso_resolver_s {
  int                    options_mask;
  struct ares_options    options;
  ares_channel           channel;
  shuso_t               *ctx;
  shuso_config_t        *cf;
  shuso_resolver_socket_t *socket_head;
};

typedef enum {
  SHUSO_RESOLVER_SUCCESS = 0,
  SHUSO_RESOLVER_FAILURE = 1,
  SHUSO_RESOLVER_FAILURE_NOTIMP = 2, //bad address family
  SHUSO_RESOLVER_FAILURE_BADNAME = 3,
  SHUSO_RESOLVER_FAILURE_NODATA = 4,
  SHUSO_RESOLVER_FAILURE_NOTFOUND = 5,
  SHUSO_RESOLVER_FAILURE_NOMEM = 6,
  SHUSO_RESOLVER_FAILURE_CANCELLED = 7,
  SHUSO_RESOLVER_FAILURE_CONNREFUSED = 8
} shuso_resolver_result_t;

typedef void shuso_resolver_fn(shuso_resolver_result_t result, struct hostent *hostent, void *pd);

bool shuso_resolver_global_init(const char **err);
bool shuso_resolver_global_cleanup(void);

bool shuso_resolver_init(shuso_t *ctx, shuso_config_t *cf, shuso_resolver_t *resolver);
bool shuso_resolver_cleanup(shuso_resolver_t *resolver);

bool shuso_resolve_hostname(shuso_resolver_t *resolver, const char *name, int addr_family, shuso_resolver_fn callback, void *pd);

#endif //SHUTTLESOCK_RESOLVER_H
