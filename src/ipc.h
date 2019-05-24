#ifndef __SHUTTLESOCK_IPC_H
#define __SHUTTLESOCK_IPC_H
#include <shuttlesock.h>
#include <stdatomic.h>

typedef struct {
  _Atomic char      *code;
  _Atomic intptr_t  *ptr;
} ipc_inbuf_t;

typedef struct ipc_outbuf_s {
  void                *ptr;
  struct ipc_outbuf_s *next;
} ipc_outbuf_t;

typedef struct {
  ipc_inbuf_t     in;
  ipc_outbuf_t   *out;
} ipc_buf_t;

#endif //__SHUTTLESOCK_IPC_H
