#include <shuttlesock.h>
#include <shuttlesock/buffer.h>
#include <errno.h>

void shuso_buffer_init(shuso_buffer_t *buf, shuso_buffer_memory_type_t mtype, void *allocator_data) {
  *buf = (shuso_buffer_t) {
    .first = NULL,
    .last = NULL,
    .memory_type = mtype,
    .allocator_data = allocator_data
  };
  
  switch(mtype) {
    case SHUSO_BUF_HEAP:
      assert(allocator_data == NULL);
      break;
    
    case SHUSO_BUF_STALLOC:
    case SHUSO_BUF_FIXED:
    case SHUSO_BUF_SHARED:
      assert(allocator_data);
      break;
      
    case SHUSO_BUF_EXTERNAL:
      break;
      
    default:
      //nope, this is not allowed
      abort();
  }
}

void *shuso_buffer_allocate(shuso_t *S, shuso_buffer_t *buf, size_t sz) {
  void *data = NULL;
  switch(buf->memory_type) {
    case SHUSO_BUF_HEAP:
      data = malloc(sz);
      break;
    case SHUSO_BUF_STALLOC:
      data = shuso_stalloc(buf->stalloc_pool, sz);
      break;
    case SHUSO_BUF_FIXED:
      //TODO
      abort();
      break;
    case SHUSO_BUF_SHARED:
      data = shuso_shared_slab_alloc(buf->shm, sz);
      break;
    case SHUSO_BUF_DEFAULT:
    case SHUSO_BUF_EXTERNAL:
    case SHUSO_BUF_MMAPPED:
      //that makes no sense
      abort();
  }
  if(!data) {
    shuso_set_error_errno(S, "Failed to allocate %d bytes for buffer", sz, ENOMEM);
  }
  
  return data;
}

static void buffer_append_link(shuso_buffer_t *buf, shuso_buffer_link_t *link) {
  shuso_buffer_link_t *last = buf->last;
  if(!buf->first) {
    buf->first = link;
  }
  if(last) {
    last->next = link;
  }
  buf->last = link;
}

char *shuso_buffer_add_charbuf(shuso_t *S, shuso_buffer_t *buf, size_t len) {
  shuso_buffer_link_t *link;
  char                *charbuf;
  size_t               sz = sizeof(*link) + len;
  if((link = shuso_buffer_allocate(S, buf, sz)) == NULL) {
    return NULL;
  }
  charbuf = (void *)&link[1];
  *link = (shuso_buffer_link_t ) {
    .buf = charbuf,
    .len = len,
    .data_type = SHUSO_BUF_CHARBUF,
    .memory_type = SHUSO_BUF_DEFAULT,
    .next = NULL
  };
  buffer_append_link(buf, link);
  return charbuf;
}

struct iovec *shuso_buffer_add_iovec(shuso_t *S, shuso_buffer_t *buf, int iovcnt) {
  shuso_buffer_link_t *link;
  struct iovec        *iov;
  size_t               sz = sizeof(*link) + sizeof(*iov)*iovcnt;
  if((link = shuso_buffer_allocate(S, buf, sz)) == NULL) {
    return NULL;
  }
  iov = (void *)&link[1];
  *link = (shuso_buffer_link_t ) {
    .iov = iov,
    .iovcnt = iovcnt,
    .data_type = SHUSO_BUF_IOVEC,
    .memory_type = SHUSO_BUF_DEFAULT,
    .next = NULL
  };
  buffer_append_link(buf, link);
  return iov;
}

char *shuso_buffer_add_msg_fd(shuso_t *S, shuso_buffer_t *buf, int fd,size_t data_sz) {
  struct msgblob_s {
    struct msghdr                         msg;
    struct iovec iov;
    union {
      char buf[CMSG_SPACE(sizeof(int))];
      struct cmsghdr align;
    }                                    ancillary_buf;
  } *msgblob;
  
  shuso_buffer_link_t *link;
  struct msghdr       *msg;
  size_t               sz = sizeof(*msgblob) + data_sz;
  if((link = shuso_buffer_allocate(S, buf, sz)) == NULL) {
    return NULL;
  }
  
  msgblob = (void *)&link[1];
  
  *msgblob = (struct msgblob_s) {
    .ancillary_buf = {{0}},
    .msg = {
      .msg_name = NULL,
      .msg_namelen = 0,
      .msg_iov = &msgblob->iov,
      .msg_iovlen = 1,
      .msg_control = &msgblob->ancillary_buf.buf,
      .msg_controllen = sizeof(msgblob->ancillary_buf.buf),
      .msg_flags = 0
    }
  };
  msg = &msgblob->msg;
  
  struct cmsghdr *cmsg = CMSG_FIRSTHDR(msg);
  cmsg->cmsg_level = SOL_SOCKET;
  cmsg->cmsg_type = SCM_RIGHTS;
  cmsg->cmsg_len = CMSG_LEN(sizeof(fd));
  memcpy(CMSG_DATA(cmsg), &fd, sizeof(fd));
  
  msgblob->iov.iov_base = &msgblob[1]; //clang analyzer doesn't like this.
  //it would be nice to have added a data[] at the end of the msgblob, but the trailing 
  // space is already taken up by char buf[CMSG_SPACE(...)]
  
  msgblob->iov.iov_len = data_sz;
  
  *link = (shuso_buffer_link_t ) {
    .msg = msg,
    .flags = 0,
    .data_type = SHUSO_BUF_IOVEC,
    .memory_type = SHUSO_BUF_DEFAULT,
    .next = NULL
  };
  buffer_append_link(buf, link);
  return msgblob->iov.iov_base;
}
