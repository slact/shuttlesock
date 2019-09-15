#include <shuttlesock.h>
#include <shuttlesock/resolver.h>
#include <unistd.h>
#include <errno.h>
#include <sys/uio.h>

typedef struct {
  size_t sz;
  char   data[];
} aalloc_t;

#if defined(SHUTTLESOCK_SANITIZE) || defined(SHUTTLESOCK_VALGRIND) || defined(__clang_analyzer__)
#define INIT_ARES_ALLOCS 1
#endif

#ifdef INIT_ARES_ALLOCS
static void *init_malloc(size_t sz) {
  aalloc_t *a = calloc(1, sizeof(*a) + sz);
  if(a == NULL) {
    return NULL;
  }
  a->sz = sz;
  return (void *)&a->data;
}
static void init_free(void *ptr) {
  aalloc_t *a = container_of(ptr, aalloc_t, data);
  free(a);
}
static void *init_realloc(void *ptr, size_t sz) {
  if(ptr) {
    aalloc_t *a = container_of(ptr, aalloc_t, data);
    a = realloc(a, sz);
    if(a == NULL) {
      return NULL;
    }
    if(a->sz < sz) {
      memset(&a->data[a->sz], '0', sz - a->sz);
    }
    return (void *)&a->data;
  }
  else {
    return init_malloc(sz);
  }
}
#endif

bool shuso_resolver_global_init(const char **err) {
  int rc;
#ifndef INIT_ARES_ALLOCS
  if((rc = ares_library_init(ARES_LIB_INIT_NONE)) != 0) {
    *err = ares_strerror(rc);
    return false;
  }
#else
  if((rc = ares_library_init_mem(ARES_LIB_INIT_NONE, init_malloc, init_free, init_realloc)) != 0) {
    *err = ares_strerror(rc);
    return false;
  }
#endif
  return true;
}

bool shuso_resolver_global_cleanup(void) {
  ares_library_cleanup();
  return true;
}

static void ares_socket_state_callback(void *data, ares_socket_t socket_fd, int readable, int writable);
static void ares_ev_io_callback(shuso_loop *, shuso_ev_io *, int);

const struct ares_socket_functions ares_sockfuncs;

bool shuso_resolver_init(shuso_t *S, shuso_config_t *cf, shuso_resolver_t *resolver) {
  bool                         ares_initialized = false;
  const char                  *err = NULL;
  int                          rc;
  shuso_hostinfo_t            *cf_host = cf->resolver.hosts.array;
  struct ares_addr_port_node  *hosts = NULL;
  int                          hosts_count = cf->resolver.hosts.count;
  struct ares_options          opt = resolver->options;
  int                          optmask = resolver->options_mask | ARES_OPT_SOCK_STATE_CB;
  
  resolver->ctx = S;
  resolver->cf = cf;
  
  opt.sock_state_cb = ares_socket_state_callback;
  opt.sock_state_cb_data = resolver;
  
  if((rc = ares_init_options(&resolver->channel, &opt, optmask)) != ARES_SUCCESS) {
    goto fail;
  }
  ares_initialized = true;
  
  ares_set_socket_functions(resolver->channel, &ares_sockfuncs, resolver);
  if(hosts_count > 4096) {
    err = "DNS resolver initialization failed: too many hosts (must not exceed 4096)";
    goto fail;
  }
  if(hosts_count > 0) {
    //create linked list of ares configuration hosts
    if((hosts = malloc(sizeof(*hosts) * hosts_count)) == NULL) {
      goto fail;
    }
    for(int i=0; i<hosts_count; i++) {
      if(i>0) hosts[i-1].next=&hosts[i];
      hosts[i].next = NULL;
      hosts[i].family = cf_host[i].addr_family;
      switch(hosts[i].family) {
        case AF_INET:
          hosts[i].addr.addr4 = cf_host[i].addr;
          break;
        case AF_INET6:
          memcpy(&hosts[i].addr.addr6, &cf_host[i].addr6, sizeof(cf_host[i].addr6));
          break;
        default:
          err = "DNS resolver initialization failed: unexpected address family, must be AF_INET of AF_INET6";
          goto fail;
      }
      if(cf_host[i].port != 0) {
        if(cf_host[i].udp) {
          hosts[i].udp_port = htons(cf_host[i].port);
        }
        else {
          hosts[i].tcp_port = htons(cf_host[i].port);
        }
      }
    }
    if((rc = ares_set_servers_ports(resolver->channel, hosts)) != ARES_SUCCESS) {
      goto fail;
    }
    free(hosts);
  }
  return true;


fail:
  if(rc != ARES_SUCCESS && err == NULL) {
    switch(rc) {
      case ARES_EFILE:
        err = "DNS resolver initialization failed: a configuration file could not be read.";
        break;
      case ARES_ENOMEM:
        err = "DNS resolver initialization failed: Out of memory";
        break;
      case ARES_ENOTINITIALIZED:
        err = "DNS resolver initialization failed: c-ares library isn't initialized";
        break;
      case ARES_ENOTIMP:
        err = "DNS resolver initialization failed: changing name server during pending queries is forbidden";
        break;
      default:
        err = "DNS resolver initialization failed: unforeseen error";
    }
  }
  if(err) {
    shuso_set_error(S, err);
  }
  if(ares_initialized) ares_destroy(resolver->channel);
  if(hosts) free(hosts);
  return false;
}

bool shuso_resolver_cleanup(shuso_resolver_t *resolver) {
  ares_destroy(resolver->channel);
  //TODO: other cleanup maybe?
  return true;
}

static void ares_ev_io_callback(shuso_loop *loop, shuso_ev_io *w, int events) {
  shuso_resolver_socket_t *rsock = shuso_ev_data(w);
  ares_socket_t rfd = ARES_SOCKET_BAD, wfd = ARES_SOCKET_BAD;
  if(events & EV_READ) {
    rfd = w->ev.fd;
  }
  if(events & EV_WRITE) {
    wfd = w->ev.fd;
  }
  ares_process_fd(rsock->resolver->channel, rfd, wfd);
}

static void ares_socket_state_callback(void *data, ares_socket_t socket_fd, int readable, int writable) {
  //do nothing
}

static ares_socket_t ares_socket_open(int domain, int type, int protocol, void *user_data) {
  shuso_resolver_t        *resolver = user_data;
  shuso_t                 *S = resolver->ctx;
  ares_socket_t            fd;
  shuso_resolver_socket_t *rsock = malloc(sizeof(*rsock));
  if(!rsock) {
    shuso_set_error(S, "DNS resolver: failed to allocate memory for socket struct");
    return -1;
  }
  do {
    fd = socket(domain, type, protocol);
  } while(fd < 0 && errno == EINTR);
  if(fd < 0) {
    free(rsock);
    shuso_set_error(S, "DNS resolver: failed to open socket");
    return -1;
  }
  if(shuso_set_nonblocking(fd) == -1) {
    free(rsock);
    close(fd);
    shuso_set_error(S, "DNS resolver: failed to set socket as non-blocking");
    return -1;
  }
  
  //push to front of singly-linked list
  rsock->next = resolver->socket_head;
  resolver->socket_head = rsock;
  
  rsock->fd = fd;
  rsock->resolver = resolver;
  
  shuso_ev_io_init(S, &rsock->ev.io, rsock->fd, EV_READ | EV_WRITE, ares_ev_io_callback, rsock);
  
  shuso_ev_io_start(S, &rsock->ev.io);
  
  return fd;
  
}

static int ares_socket_close(ares_socket_t fd, void *user_data) {
  shuso_resolver_t        *resolver = user_data;
  shuso_resolver_socket_t *prev = NULL, *cur;
  for(cur = resolver->socket_head; cur && cur->fd != fd; cur = cur->next) {
    prev = cur;
  }
  assert(cur);
  assert(cur->fd == fd);
  
  //remove from linked list
  if(prev) {
    prev->next = cur->next;
  } 
  else { // no prev, means we're at the head
    resolver->socket_head = cur->next;
  }
  
  shuso_ev_io_stop(resolver->ctx, &cur->ev.io);
  free(cur);
  
  int rc;
  do {
    rc = close(fd);
  } while(rc == EINTR);
  
  return rc;
}
static int ares_socket_connect(ares_socket_t fd, const struct sockaddr *addr, ares_socklen_t addr_len, void *user_data) {
  int rc;
  do {
    rc = connect(fd, addr, addr_len);
  } while(rc == EINTR);
  return rc;
}
static ares_ssize_t ares_socket_recvfrom(ares_socket_t fd, void *buffer, size_t buf_size, int flags, struct sockaddr *addr, ares_socklen_t* addr_len, void *user_data) {
  ssize_t received_len;
  do {
    received_len = recvfrom(fd, buffer, buf_size, flags, addr, addr_len);
  } while(received_len == -1 && errno == EINTR);
  return received_len;
}
static ares_ssize_t ares_socket_sendv(ares_socket_t fd, const struct iovec *data, int len, void * user_data) {
  ssize_t  sent_len;
  do {
    sent_len = writev(fd, data, len);
  } while(sent_len == -1 && errno == EINTR);
  return sent_len;
}

const struct ares_socket_functions ares_sockfuncs = {
  ares_socket_open,
  ares_socket_close,
  ares_socket_connect,
  ares_socket_recvfrom,
  ares_socket_sendv
};


typedef struct {
  shuso_resolver_t   *resolver;
  shuso_resolver_fn  *callback;
  void               *pd;
} shuso_resolve_hostname_data_t;

static void shuso_resolve_hostname_callback(void *arg, int status,
 int timeouts, struct hostent *hostent) {
  shuso_resolver_result_t result;
  switch(status) {
    case ARES_SUCCESS:
      result = SHUSO_RESOLVER_SUCCESS;
      break;
    case ARES_ENOTIMP:
      result = SHUSO_RESOLVER_FAILURE_NOTIMP;
      break;
    case ARES_EBADNAME:
      result = SHUSO_RESOLVER_FAILURE_BADNAME;
      break;
    case ARES_ENODATA:
      result = SHUSO_RESOLVER_FAILURE_NODATA;
      break;
    case ARES_ENOTFOUND:
      result = SHUSO_RESOLVER_FAILURE_NOTFOUND;
      break;
    case ARES_ENOMEM:
      result = SHUSO_RESOLVER_FAILURE_NOMEM;
      break;
    case ARES_ECANCELLED:
    case ARES_EDESTRUCTION:
      result = SHUSO_RESOLVER_FAILURE_CANCELLED;
      break;
    case ARES_ECONNREFUSED:
      result = SHUSO_RESOLVER_FAILURE_CONNREFUSED;
      break;
    default:
      result = SHUSO_RESOLVER_FAILURE;
      break;
  }
  
  //TODO: handle _timeouts_, the number of times this query has timed out
  shuso_resolve_hostname_data_t *data = arg;
  data->callback(data->resolver->ctx, result, hostent, data->pd);
  free(arg);
}

bool shuso_resolve_hostname(shuso_resolver_t *resolver, const char *name, int addr_family, shuso_resolver_fn callback, void *pd) {
  shuso_resolve_hostname_data_t *data = malloc(sizeof(*data));
  if(!data) {
    return false;
  }
  data->resolver = resolver;
  data->callback = callback;
  data->pd = pd;
  
  ares_gethostbyname(resolver->channel, name, addr_family, shuso_resolve_hostname_callback, data);
  return true;
}

