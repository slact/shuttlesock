#ifndef SHUTTLESOCK_POOL_H
#define SHUTTLESOCK_POOL_H

#include <shuttlesock/common.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#ifndef SHUTTLESOCK_POOL_MAX_LEVELS
#define SHUTTLESOCK_POOL_MAX_LEVELS 8
#endif

#ifdef SHUTTLESOCK_POOL_TRACK_SPACE
typedef struct {
  size_t                         used;
  size_t                         wasted;
  size_t                         reserved;
  char                          *cur;
} shuso_pool_page_space_t;
#endif

typedef struct shuso_pool_page_space_t {
  struct shuso_pool_page_space_t   *prev;
#ifdef SHUTTLESOCK_POOL_TRACK_SPACE
  shuso_pool_page_space_t     space;
#endif
} shuso_pool_page_t;

typedef struct shuso_pool_allocd_s {
  struct shuso_pool_allocd_s *prev;
#ifdef SHUTTLESOCK_POOL_TRACK_SPACE
  size_t                       size;
#endif
#if defined(SHUTTLESOCK_DEBUG_SANITIZE) || defined(SHUTTLESOCK_DEBUG_VALGRIND)
  void                        *data;
#else
  char                         data[];
#endif
} shuso_pool_allocd_t;

typedef struct {
  shuso_pool_page_t      *page;
  char                   *page_cur;
#ifdef SHUTTLESOCK_POOL_TRACK_SPACE
  shuso_pool_page_space_t page_space;
#endif
  shuso_pool_allocd_t    *allocd;
} shuso_pool_level_t;

typedef struct {
#ifdef SHUTTLESOCK_POOL_TRACK_SPACE
  struct shuso_pool_space_s {
    size_t    wasted; //free space at the end of all non-last pages and in headers & footers
    size_t    free;
    size_t    used; //non-malloc'd space in use
    size_t    reserved; //non-malloc'd space for internal use
    size_t    allocd; //malloc'd space in use
  }         space;
#endif
  struct shuso_pool_pages_s {
    size_t                 size;
    size_t                 count;
    shuso_pool_page_t     *last;
    char                  *cur; //current position in last page
  }         page;
  struct shuso_pool_allocds_s {
    shuso_pool_allocd_t   *last;
  }         allocd;
  struct shuso_pool_levels_s {
    shuso_pool_level_t    *array[SHUTTLESOCK_POOL_MAX_LEVELS];
    uint_fast8_t           count;
  }         levels;
} shuso_pool_t;

bool shuso_pool_init(shuso_pool_t *, size_t pagesize); //idempotent
bool shuso_pool_init_clean(shuso_pool_t *, size_t pagesize); //idempotent
void *shuso_palloc(shuso_pool_t *, size_t sz);
void *shuso_palloc_unaligned(shuso_pool_t *, size_t sz);
int shuso_pool_mark_level(shuso_pool_t *);
bool shuso_pool_drain_to_level(shuso_pool_t *, unsigned stackpos);
bool shuso_pool_empty(shuso_pool_t *); //idempotent

typedef enum {
  SPACE_FREE = 0,
  SPACE_USED,
  SPACE_WASTED,
  SPACE_RESERVED
} shuso_pool_space_kind_t;

size_t shuso_pool_space(shuso_pool_t *, shuso_pool_space_kind_t);

#endif //SHUTTLESOCK_POOL_H
