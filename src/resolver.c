#include <shuttlesock.h>
#include <shuttlesock/resolver.h>

bool shuso_resolver_global_init(const char **err) {
  int rc;
  if((rc = ares_library_init(ARES_LIB_INIT_NONE)) != 0) {
    *err = ares_strerror(rc);
    return false;
  }
  return true;
}

bool shuso_resolver_global_cleanup(void) {
  ares_library_cleanup();
  return true;
}

const struct ares_socket_functions ares_sockfuncs;

bool shuso_resolver_init(shuso_t *ctx, shuso_resolver_t *ares) {
  int rc = ares_init_options(ares->channel, &ares->options, ares->options_mask);
  if(rc == ARES_EFILE) {
    return shuso_set_error(ctx, "DNS resolver initialization failed: a configuration file could not be read.");
  }
  else if(rc == ARES_ENOMEM) {
    return shuso_set_error(ctx, "DNS resolver initialization failed: Out of memory");
  }
#ifdef ARES_ENOTINITIALIZED
  else if(rc == ARES_ENOTINITIALIZED) {
    return shuso_set_error(ctx, "DNS resolver initialization failed: c-ares library isn't initialized");
  }
#endif
  
  ares_set_socket_functions(ares->channel, &ares_sockfuncs, ctx);
  return true;
}


static ares_socket_t ares_socket_open(int domain, int type, int protocol, void * user_data) {
  return -1;
}
int ares_socket_close(ares_socket_t fd, void * user_data) {
  return 1;
}
int ares_socket_connect(ares_socket_t fd, const struct sockaddr * addr, ares_socklen_t addr_len, void * user_data) {
  return -1;
}
ares_ssize_t ares_socket_recvfrom(ares_socket_t fd, void * buffer, size_t buf_size, int flags, struct sockaddr * addr, ares_socklen_t * addr_len, void * user_data) {
  return -1;
}
ares_ssize_t ares_socket_send(ares_socket_t fd, const struct iovec * data, int len, void * user_data) {
  return -1;
}

const struct ares_socket_functions ares_sockfuncs = {
  ares_socket_open,
  ares_socket_close,
  ares_socket_connect,
  ares_socket_recvfrom,
  ares_socket_send
};

