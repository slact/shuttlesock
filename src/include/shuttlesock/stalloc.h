#ifndef SHUTTLESOCK_STALLOC_H
#define SHUTTLESOCK_STALLOC_H

#include <shuttlesock/configure.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifndef SHUTTLESOCK_STALLOC_STACK_SIZE
#define SHUTTLESOCK_STALLOC_STACK_SIZE 8
#endif

#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
typedef struct shuso_stalloc_page_space_s {
  size_t                         used;
  size_t                         wasted;
  size_t                         reserved;
  char                          *cur;
} shuso_stalloc_page_space_t;
#endif

typedef struct shuso_stalloc_page_s {
  struct shuso_stalloc_page_s   *prev;
#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
  shuso_stalloc_page_space_t     space;
#endif
} shuso_stalloc_page_t;

typedef struct shuso_stalloc_allocd_s {
  struct shuso_stalloc_allocd_s *prev;
#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
  size_t                       size;
#endif
#if defined(SHUTTLESOCK_SANITIZE) || defined(SHUTTLESOCK_VALGRIND)
  void                        *data;
#else
  char                         data[];
#endif
} shuso_stalloc_allocd_t;

typedef struct shuso_stalloc_frame_s {
  shuso_stalloc_page_t   *page;
  char                   *page_cur;
#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
  shuso_stalloc_page_space_t page_space;
#endif
  shuso_stalloc_allocd_t *allocd;
} shuso_stalloc_frame_t;

typedef struct shuso_stalloc_s {
#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
  struct shuso_stalloc_space_s {
    size_t    wasted; //free space at the end of all non-last pages and in headers & footers
    size_t    free;
    size_t    used; //non-malloc'd space in use
    size_t    reserved; //non-malloc'd space for internal use
    size_t    allocd; //malloc'd space in use
  }         space;
#endif
  struct shuso_stalloc_pages_s {
    size_t                 size;
    size_t                 count;
    shuso_stalloc_page_t  *last;
    char                  *cur; //current position in last page
  }         page;
  struct shuso_stalloc_allocds_s {
    shuso_stalloc_allocd_t *last;
  }         allocd;
  struct shuso_stalloc_stackframes_s {
    shuso_stalloc_frame_t  *stack[SHUTTLESOCK_STALLOC_STACK_SIZE];
    uint_fast8_t            count;
  }         stack;
} shuso_stalloc_t;

bool shuso_stalloc_init(shuso_stalloc_t *st, size_t pagesize);
bool shuso_stalloc_init_clean(shuso_stalloc_t *st, size_t pagesize);
void *shuso_stalloc(shuso_stalloc_t *, size_t sz);
void *shuso_stalloc_unaligned(shuso_stalloc_t *, size_t sz);
int shuso_stalloc_push(shuso_stalloc_t *);
bool shuso_stalloc_pop_to(shuso_stalloc_t *, int stackpos);
bool shuso_stalloc_empty(shuso_stalloc_t *);

typedef enum {
  SPACE_FREE = 0,
  SPACE_USED,
  SPACE_WASTED,
  SPACE_RESERVED
} shuso_stalloc_space_kind_t;

size_t shuso_stalloc_space(shuso_stalloc_t *, shuso_stalloc_space_kind_t);

#endif //SHUTTLESOCK_STALLOC_H
