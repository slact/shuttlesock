#ifndef __SHUTTLESOCK_LLIST_H
#define __SHUTTLESOCK_LLIST_H
#include <stddef.h>
#include <assert.h>

#ifndef container_of
#define container_of(ptr, type, member) ((type *)((char *)(1 ? (ptr) : &((type *)0)->member) - offsetof(type, member)))
#endif

#define llist_init(ll) \
  (ll).head = NULL;   \
  (ll).tail = NULL;   \
  (ll).count = 0      \

#define llist_prepend(ll, el) \
  if((ll).head == NULL) { \
    (ll).head = el; \
    (el)->next = NULL; \
  } \
  else { \
    (ll).head->prev = el; \
    (el)->next = (ll).head; \
  } \
  if((ll).tail == NULL) { \
    (ll).tail = el; \
  } \
  (ll).count++; \
  (el)->prev = NULL

#define llist_append(ll, el) \
  if((ll).tail == NULL) { \
    /* infer ll.head == NULL as well */ \
    (ll).tail = el; \
    (ll).head = el; \
    (el)->prev = NULL; \
  } \
  else { \
    (ll).tail->next = el; \
    (el)->prev = (ll).tail; \
    (ll).tail = el; \
  } \
  (ll).count++; \
  (el)->next = NULL

#define llist_link(el, el_type) \
  container_of(el, el_type##_link_t, data)

#define llist_remove(ll, el) \
  if((ll).tail == el) { \
    (ll).tail = (el)->prev; \
  } \
  if((ll).head) { \
    (ll).head = (el)->next; \
  } \
  if((el)->next) { \
    (el)->next->prev = (el)->prev ? (el)->prev->next : NULL; \
  } \
  if((el)->prev) { \
    (el)->prev->next = (el)->next ? (el)->next->prev : NULL; \
  } \
  (ll).count--; \
  /* debuggy stuff */ \
  el->next = NULL; \
  el->prev = NULL 

#define LLIST_TYPEDEF_LINK_STRUCT(type) \
typedef struct type##_link_s { \
  struct type##_link_s *prev; \
  struct type##_link_s *next; \
  type                  data; \
} type##_link_t

#define LLIST_STRUCT(type) \
struct { \
  type##_link_t *head; \
  type##_link_t *tail; \
  size_t         count; \
}
  
#endif //__SHUTTLESOCK_LLIST_H
