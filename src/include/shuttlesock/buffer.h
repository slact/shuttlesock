#ifndef SHUTTLESOCK_BUFFER_H
#define SHUTTLESOCK_BUFFER_H

#include <shuttlesock/common.h>
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
struct shuso_buffer_link_s {
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
  
  shuso_buffer_link_t *next;
};

typedef struct {
  void                   *cleanup_privdata;
  void                 *(*cleanup)(void *data, void *pd);
} shuso_buffer_link_cleanup_t;


typedef struct {
  shuso_buffer_link_t           link;
  shuso_buffer_link_cleanup_t   cleanup;
} shuso_buffer_link_with_cleanup_t;

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

void shuso_buffer_init_custom(shuso_buffer_t *buf);

void shuso_buffer_init(shuso_buffer_t *buf, shuso_buffer_memory_type_t, void *allocator_data);

char *shuso_buffer_add_charbuf(shuso_t *S, shuso_buffer_t *buf, size_t len);
struct iovec *shuso_buffer_add_iovec(shuso_t *S, shuso_buffer_t *buf, int iovcnt);
char *shuso_buffer_add_msg_fd(shuso_t *S, shuso_buffer_t *buf, int fd, size_t data_sz);

#endif // SHUTTLESOCK_BUFFER_H
