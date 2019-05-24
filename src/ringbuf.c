#include <unistd.h>
#include <stdbool.h>
#include <stdlib.h>
#include "ringbuf.h"

ringbuf_char_t *ringbuf_char_new(size_t ringlength) {
  ringbuf_char_t *ring = calloc(1, sizeof(*ring) + ringlength);
  if(!ring) {
    return NULL;
  }
  ring->length = ringlength;
  return ring;
}

bool ringbuf_char_push(ringbuf_char_t *ring, char chr) {
  //assume element_size is 1
  uint64_t      in, in_next;
  bool          stored_ok = false;
  uint64_t      length = ring->length;
  static char   nul = 0;
  while(!stored_ok) {
    in = atomic_fetch_add(&ring->in, 1);
    in_next = in % length;
    atomic_compare_exchange_weak(&ring->in, &in, in_next); //modulo pls
    stored_ok = atomic_compare_exchange_strong(&ring->ring[in_next], &nul, chr);
    if(in_next == atomic_load(&ring->out)) {
      //outta space
      return false;
    }
  }
  return true;
}

bool ringbuf_char_pop(ringbuf_char_t *ring, char *out) {
  return true;
  
  
}
