#ifndef __SHUTTLESOCK_RUNGBUF_H
#define __SHUTTLESOCK_RUNGBUF_H
#include <stdatomic.h>

typedef struct {
  uint16_t          length;
  _Atomic uint64_t  out;
  _Atomic uint64_t  in;
  _Atomic char      ring[];
} ringbuf_char_t;

#endif //__SHUTTLESOCK_RUNGBUF_H
