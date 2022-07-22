#include <shuttlesock/common.h>
#include <shuttlesock/pool.h>
#include <shuttlesock/sysutil.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>

#define align_native(p)                                                   \
  (((uintptr_t) (p) + ((uintptr_t)(sizeof(void *)) - 1)) & ~((uintptr_t)(sizeof(void *)) - 1))

#define align_ptr_native(p)                                                   \
  (void *)align_native(p)

#define align_ptr(p, align)                                                   \
  (void *)(((uintptr_t) (p) + ((uintptr_t)align - 1)) & ~((uintptr_t)align - 1))

#define page_used_space(page, cur) \
  (size_t )((uintptr_t)(cur) - (uintptr_t)(page))


#if defined(SHUTTLESOCK_DEBUG_VALGRIND)
#define POOL_VALGRIND_RZB sizeof(void *)
_Static_assert(POOL_VALGRIND_RZB == align_native(POOL_VALGRIND_RZB), "Valgrind mempool redzone size must be a sizeof(void*) multiple");
#include <valgrind/memcheck.h>
#endif

static shuso_pool_page_t *add_page(shuso_pool_t *pool) {
  size_t                   pagesize = pool->page.size;
  shuso_pool_page_t    *page;
  char                    *cur;
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  char *pagedata = malloc(pagesize+2*POOL_VALGRIND_RZB);
  if(!pagedata) return NULL;
  VALGRIND_CREATE_MEMPOOL(pagedata, POOL_VALGRIND_RZB, false);
  VALGRIND_MAKE_MEM_NOACCESS(pagedata, pagesize+2*POOL_VALGRIND_RZB);
  page = (void *)(pagedata+POOL_VALGRIND_RZB);
  VALGRIND_MEMPOOL_ALLOC(pagedata, page, sizeof(*page));
  cur = align_ptr_native(((char *)&page[1]) + POOL_VALGRIND_RZB);
#else
  page = malloc(pagesize);
  if(!page) return NULL;
  cur = align_ptr_native(&page[1]);
#endif

#ifdef SHUTTLESOCK_POOL_TRACK_SPACE
  size_t wasted = 0;
  if(pool->page.last) {
    wasted = pool->page.size - page_used_space(pool->page.last, pool->page.cur);
    pool->space.free -= wasted;
    pool->space.wasted += wasted;
  }
#endif
  
  page->prev = pool->page.last;

  pool->page.cur = cur;
  pool->page.last = page;
  pool->page.count++;

#ifdef SHUTTLESOCK_POOL_TRACK_SPACE  
  page->space.reserved = page_used_space(page, pool->page.cur);
  pool->space.free += pool->page.size - page->space.reserved;
  pool->space.reserved += page->space.reserved;
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  page->space.wasted = 0;
#else
  page->space.wasted = 0;
#endif
  page->space.cur = pool->page.cur;
  page->space.used = 0;
#endif

  return page;
}

static void remove_last_page(shuso_pool_t *pool) {
  shuso_pool_page_t *page = pool->page.last;
  if(!page) return;
  pool->page.last = page->prev;
  pool->page.count--;
#ifdef SHUTTLESOCK_POOL_TRACK_SPACE
    pool->space.free -= pool->page.size - page_used_space(page, page->space.cur);
    pool->space.used -= page->space.used;
    pool->space.wasted -= page->space.wasted;
    pool->space.reserved -= page->space.reserved;
    shuso_pool_page_t *prev = pool->page.last; 
    if(prev) {
      size_t freespace_left = pool->page.size - page_used_space(prev, prev->space.cur);
      pool->space.free += freespace_left;
      pool->space.wasted -= freespace_left;
    }
#endif
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  char *pagedata = &((char *)page)[-POOL_VALGRIND_RZB];
  VALGRIND_DESTROY_MEMPOOL(pagedata);
  free(pagedata);
#else
  free(page);
#endif
}

bool shuso_pool_init(shuso_pool_t *pool, size_t pagesize) {
  memset(pool, 0x00, sizeof(*pool));
  return shuso_pool_init_clean(pool, pagesize);
}

bool shuso_pool_init_clean(shuso_pool_t *pool, size_t pagesize) {
#ifdef SHUTTLESOCK_DEBUG_NOPOOL
  pool->page.size = 0;
#else
  pool->page.size = pagesize > 0 ? pagesize : shuso_system.page_size;
  if(!add_page(pool)) {
    return false;
  }
#endif
  return true;
}

static void *plain_old_malloc(shuso_pool_t *pool, size_t sz) {
  shuso_pool_allocd_t *cur;
#if defined(SHUTTLESOCK_DEBUG_SANITIZE) || defined(SHUTTLESOCK_DEBUG_VALGRIND)
  //two separate allocations so that the sanitizer can insert some redzones
  cur = malloc(sizeof(*cur));
  if(!cur) return NULL;
  cur->data = malloc(sz);
  if(!cur->data) {
    free(cur);
    return NULL;
  }
#else
  cur = malloc(sizeof(*cur) + sz);
  if(!cur) return NULL;
#endif
  cur->prev = pool->allocd.last;
  pool->allocd.last = cur;
#ifdef SHUTTLESOCK_POOL_TRACK_SPACE
  cur->size = sz;
  pool->space.allocd += sz;
#endif
  return cur->data;
}

static void plain_old_free_last(shuso_pool_t *pool) {
  shuso_pool_allocd_t *cur = pool->allocd.last;
  pool->allocd.last = cur->prev;
#ifdef SHUTTLESOCK_POOL_TRACK_SPACE
  pool->space.allocd -= cur->size;
#endif
#if defined(SHUTTLESOCK_DEBUG_SANITIZE) || defined(SHUTTLESOCK_DEBUG_VALGRIND)
  free(cur->data);
#endif
  free(cur);
}

static ssize_t page_max_free_space(ssize_t pagesize) {
  size_t headersize = align_native(sizeof(shuso_pool_page_t));
  return pagesize - headersize;
}

#ifdef SHUTTLESOCK_POOL_TRACK_SPACE
static void pool_update_space(shuso_pool_t *pool, shuso_pool_page_t *page, size_t sz, ssize_t before_pad, ssize_t after_pad) {
  pool->space.wasted += before_pad + after_pad;
  pool->space.used += sz;
  pool->space.free -= sz + before_pad + after_pad;
  page->space.wasted += before_pad + after_pad;
  page->space.used += sz;
  page->space.cur = pool->page.cur;
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  pool->space.wasted -= 2*POOL_VALGRIND_RZB;
  pool->space.reserved += 2*POOL_VALGRIND_RZB;
  page->space.wasted -= 2*POOL_VALGRIND_RZB;
  page->space.reserved += 2*POOL_VALGRIND_RZB;
#endif
}
#endif

static void *palloc_in_current_page(shuso_pool_t *pool, size_t sz, bool align) {
  ssize_t               pagesize = pool->page.size;
  shuso_pool_page_t *page = pool->page.last;
  char                 *cur = pool->page.cur;
  char                 *ret;
  ssize_t               before_padding, after_padding;
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  ret = align ? align_ptr_native(&cur[POOL_VALGRIND_RZB]) : &cur[POOL_VALGRIND_RZB];
  after_padding = POOL_VALGRIND_RZB;
#else
  ret = align ? align_ptr_native(cur) : cur;
  after_padding = 0;
#endif
  before_padding = ret - cur;
  
  if(pagesize - (ssize_t )page_used_space(page, cur) < before_padding + (ssize_t )sz + after_padding) {
    //not enough space in this page
    return NULL;
  }
  pool->page.cur = &ret[sz + after_padding];
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  VALGRIND_MEMPOOL_ALLOC(&((char *)page)[-POOL_VALGRIND_RZB], ret, sz);
#endif
#ifdef SHUTTLESOCK_POOL_TRACK_SPACE
  pool_update_space(pool, page, sz, before_padding, after_padding);
#endif
  return ret;
}

static inline void *palloc_in_new_page(shuso_pool_t *pool, size_t sz, bool align) {
  ssize_t      pagesize = pool->page.size;
  ssize_t       before_padding, after_padding;
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  before_padding = POOL_VALGRIND_RZB, after_padding = POOL_VALGRIND_RZB;
#else
  before_padding = 0, after_padding = 0;
#endif
  
  if((ssize_t )page_max_free_space(pagesize) < before_padding + (ssize_t )sz + after_padding) {
    //not enough space in a brand new page
    return NULL;
  }
  
  shuso_pool_page_t *page;
  if((page = add_page(pool)) == NULL) {
    return NULL;
  }
  assert(pool->page.last == page);
  void *ret = palloc_in_current_page(pool, sz, align);
  assert(ret);
  return ret;
}

static void *palloc_with_alignment(shuso_pool_t *pool, size_t sz, bool align) {  
  
  void *ret;
  
  if((ret = palloc_in_current_page(pool, sz, align))) {
    return ret;
  }
  if((ret = palloc_in_new_page(pool, sz, align))) {
    return ret;
  }
  if((ret = plain_old_malloc(pool, sz))) {
    return ret;
  }
  return NULL;
}

void *shuso_palloc(shuso_pool_t *pool, size_t sz) {
  return palloc_with_alignment(pool, sz, true);
}
void *shuso_palloc_unaligned(shuso_pool_t *pool, size_t sz) {
  return palloc_with_alignment(pool, sz, false);
}

int shuso_pool_mark_level(shuso_pool_t *pool) { 
  shuso_pool_level_t *level = shuso_palloc(pool, sizeof(*level));
  
  if(!level) return 0;
  *level = (shuso_pool_level_t ){
    .page = pool->page.last,
    .page_cur = pool->page.cur,
#ifdef SHUTTLESOCK_POOL_TRACK_SPACE
    .page_space = (pool->page.last == NULL ? (shuso_pool_page_space_t ){0} : pool->page.last->space),
#endif
    .allocd = pool->allocd.last
  };
  
  assert(pool->levels.count < SHUTTLESOCK_POOL_MAX_LEVELS);
  pool->levels.array[pool->levels.count]=level;
  pool->levels.count++;
  return pool->levels.count;
}

// remove all levels >= levelnum from the pool
bool shuso_pool_drain_to_level(shuso_pool_t *pool, unsigned levelnum) {
  shuso_pool_level_t *level;
  if(levelnum > pool->levels.count) {
    return false;
  }
  if(levelnum == 0) {
    if(!shuso_pool_empty(pool)) {
      return false;
    }
#ifdef SHUTTLESOCK_POOL_TRACK_SPACE
    pool->space = (struct shuso_pool_space_s ){0,0,0,0,0};
#endif
    pool->page = (struct shuso_pool_pages_s ){0,0,NULL,NULL};
    pool->allocd = (struct shuso_pool_allocds_s ){NULL};
    pool->levels = (struct shuso_pool_levels_s ){{NULL}, 0};
    return true;
  }
  level = pool->levels.array[levelnum - 1];
  shuso_pool_allocd_t *allocd = level->allocd;
  if(allocd) {
    while(pool->allocd.last != allocd) {
      assert(pool->allocd.last != NULL);
      plain_old_free_last(pool);
    }
  }
  
  shuso_pool_page_t *page = level->page;
  if(page) {
    assert(pool->page.last != NULL);
    while(pool->page.last != page) {
      assert(pool->page.last != NULL);
      remove_last_page(pool);
    }
  }
  
  //roll back page
  pool->page.cur = level->page_cur;
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  VALGRIND_MEMPOOL_TRIM((&((char *)page)[-POOL_VALGRIND_RZB]), page, pool->page.cur);
#endif
#ifdef SHUTTLESOCK_POOL_TRACK_SPACE
  size_t used_diff = page ? (page->space.used - level->page_space.used) : 0;
  size_t wasted_diff = page ? (page->space.wasted - level->page_space.wasted) : 0;
  pool->space.used -= used_diff;
  pool->space.wasted -= wasted_diff;
  pool->space.free += used_diff + wasted_diff;
  if(page) {
    page->space = level->page_space;
  }
#endif
  return true;
}

bool shuso_pool_empty(shuso_pool_t *pool) {
  while(pool->allocd.last != NULL) {
    plain_old_free_last(pool);
  }
  while(pool->page.last != NULL) {
    remove_last_page(pool);
  }
  //leaves the pool in a dirty state
  return true;
}

size_t shuso_pool_space(shuso_pool_t *pool, shuso_pool_space_kind_t kind) {
  size_t space = 0;
  switch(kind) {
#ifdef SHUTTLESOCK_POOL_TRACK_SPACE
    case SPACE_USED:
      space = pool->space.used;
      break;
    case SPACE_FREE:
      space = pool->space.free;
      break;
    case SPACE_WASTED:
      space = pool->space.wasted;
      break;
    case SPACE_RESERVED:
      space = pool->space.reserved;
      break;
#else
    case SPACE_USED:
      space = pool->page.size * (pool->page.count - 1)
            + (pool->page.last == NULL ? 0 : page_used_space(pool->page.last, pool->page.cur));
      break;
    case SPACE_FREE:
      space = pool->page.last == NULL ? 0 : pool->page.size - page_used_space(pool->page.last, pool->page.cur);
      break;
    case SPACE_WASTED:
    case SPACE_RESERVED:
      //not available
      space = 0;
      break;
#endif
  }
  return space;
}
