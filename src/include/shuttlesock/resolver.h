#ifndef SHUTTLESOCK_RESOLVER_H
#define SHUTTLESOCK_RESOLVER_H

#include <ares.h>
struct shuso_s;
struct shuso_config_s;

typedef struct shuso_resolver_s {
  int                  options_mask;
  struct ares_options  options;
  ares_channel         channel;
} shuso_resolver_t;

bool shuso_resolver_global_init(const char **err);
bool shuso_resolver_global_cleanup(void);

bool shuso_resolver_init(struct shuso_s *ctx, struct shuso_config_s *cf, shuso_resolver_t *resolver);
bool shuso_resolver_cleanup(struct shuso_s *ctx, shuso_resolver_t *resolver);

#endif //SHUTTLESOCK_RESOLVER_H
