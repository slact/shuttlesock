#ifndef SHUTTLESOCK_RESOLVER_H
#define SHUTTLESOCK_RESOLVER_H

#include <ares.h>

typedef struct shuso_resolver_s {
  int                  options_mask;
  struct ares_options  options;
  ares_channel         channel;
} shuso_resolver_t;

bool shuso_resolver_global_init(const char **err);
bool shuso_resolver_global_cleanup(void);

bool shuso_resolver_init(shuso_t *ctx, shuso_resolver_t *ares);

#endif //SHUTTLESOCK_RESOLVER_H
