#ifndef SHUTTLESOCK_BUFFER_H
#define SHUTTLESOCK_BUFFER_H

#include <shuttlesock/common.h>
#include <shuttlesock/stalloc.h>
#include <shuttlesock/shared_slab.h>
#include <sys/uio.h>
#include <sys/socket.h>

typedef enum {
  SHUSO_BUF_MSGHDR = 0,
  SHUSO_BUF_IOVEC = 1,
  SHUSO_BUF_CHARBUF = 2
} shuso_buffer_data_type_t;

typedef enum {
  SHUSO_BUF_DEFAULT = 0,
  SHUSO_BUF_HEAP,
  SHUSO_BUF_STALLOC,
  SHUSO_BUF_FIXED,
  SHUSO_BUF_SHARED,
  SHUSO_BUF_MMAPPED,
  SHUSO_BUF_EXTERNAL
} shuso_buffer_memory_type_t;

typedef struct shuso_buffer_link_s shuso_buffer_link_t;
typedef struct shuso_buffer_link_cleanup_s shuso_buffer_link_cleanup_t;

typedef struct shuso_buffer_link_s {
  union {
    struct msghdr  *msg;
    struct iovec   *iov;
    char           *buf;
  };
  union {
    int     iovcnt;
    size_t  len;
    int     flags;
  };
  
  uint8_t   data_type;
  uint8_t   memory_type;
  unsigned  have_cleanup:1;
  shuso_buffer_link_t *next;
} shuso_buffer_link_t;

typedef struct {
  shuso_buffer_link_t        *first;
  shuso_buffer_link_t        *last;
  shuso_buffer_memory_type_t  memory_type;
  union {
    shuso_stalloc_t          *stalloc_pool;
    void                     *fixed_pool;
    shuso_shared_slab_t      *shm;
    void                     *allocator_data;
  };
} shuso_buffer_t;

typedef void shuso_buffer_link_cleanup_fn(shuso_t *, shuso_buffer_t *, shuso_buffer_link_t *, void *);

typedef struct shuso_buffer_link_cleanup_s {
  void                         *cleanup_privdata;
  shuso_buffer_link_cleanup_fn *cleanup;
} shuso_buffer_link_cleanup_t;

typedef struct {
  shuso_buffer_link_t           link;
  shuso_buffer_link_cleanup_t   cleanup;
} shuso_buffer_link_with_cleanup_t;

void shuso_buffer_init_custom(shuso_t *S, shuso_buffer_t *buf);

void shuso_buffer_init(shuso_t *S, shuso_buffer_t *buf, shuso_buffer_memory_type_t, void *allocator_data);

char *shuso_buffer_add_charbuf(shuso_t *S, shuso_buffer_t *buf, size_t len);
struct iovec *shuso_buffer_add_iovec(shuso_t *S, shuso_buffer_t *buf, int iovcnt);

char *shuso_buffer_add_msg_fd(shuso_t *S, shuso_buffer_t *buf, int fd, size_t data_sz);
char *shuso_buffer_add_msg_fd_with_cleanup(shuso_t *S, shuso_buffer_t *buf, int fd,size_t data_sz, shuso_buffer_link_cleanup_fn *cleanup, void *cleanup_pd);

shuso_buffer_link_t *shuso_buffer_next(shuso_t *S, shuso_buffer_t *buf);
shuso_buffer_link_t *shuso_buffer_last(shuso_t *S, shuso_buffer_t *buf);

void shuso_buffer_queue(shuso_t *S, shuso_buffer_t *buf, shuso_buffer_link_t *buflink);
shuso_buffer_link_t *shuso_buffer_dequeue(shuso_t *S, shuso_buffer_t *buf);

void *shuso_buffer_allocate(shuso_t *S, shuso_buffer_t *buf, size_t sz);
void shuso_buffer_free(shuso_t *S, shuso_buffer_t *buf, shuso_buffer_link_t *);

void shuso_buffer_link_init_msg(shuso_t *S, shuso_buffer_link_t *link, struct msghdr *, int flags);
void shuso_buffer_link_init_charbuf(shuso_t *S, shuso_buffer_link_t *link, const char *charbuf, int len);
void shuso_buffer_link_init_shuso_str(shuso_t *S, shuso_buffer_link_t *link, const shuso_str_t *str);
void shuso_buffer_link_init_iovec(shuso_t *S, shuso_buffer_link_t *link, struct iovec *iov, int iovlen);

#define ___SHUSO_BUFFER_LINK_INIT_VARARG(_1,_2,NAME,...) NAME

#define shuso_buffer_link_init(S, link, ...) ___SHUSO_BUFFER_LINK_INIT_VARARG(__VA_ARGS__, __SHUSO_BUFFER_LINK_INIT_2, __SHUSO_BUFFER_LINK_INIT_1, ___END__VARARG__LIST__)(S, link, __VA_ARGS__)

#define __SHUSO_BUFFER_LINK_INIT_2(S, link, contents, intarg) \
  _Generic((contents), \
    struct msghdr *         : shuso_buffer_link_init_msg, \
    char *                  : shuso_buffer_link_init_charbuf, \
    struct iovec *          : shuso_buffer_link_init_iovec \
  )(S, link, contents, intarg)

#define __SHUSO_BUFFER_LINK_INIT_1(S, link, contents) \
  _Generic((contents), \
    shuso_str_t *           : shuso_buffer_link_init_shuso_str \
  )(S, link, contents)
#endif // SHUTTLESOCK_BUFFER_H
