#include <shuttlesock/common.h>
#include <shuttlesock/stalloc.h>
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
#define STALLOC_VALGRIND_RZB sizeof(void *)
_Static_assert(STALLOC_VALGRIND_RZB == align_native(STALLOC_VALGRIND_RZB), "Valgrind mempool redzone size must be a sizeof(void*) multiple");
#include <valgrind/memcheck.h>
#endif

static shuso_stalloc_page_t *add_page(shuso_stalloc_t *st) {
  size_t                   pagesize = st->page.size;
  shuso_stalloc_page_t    *page;
  char                    *cur;
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  char *pagedata = malloc(pagesize+2*STALLOC_VALGRIND_RZB);
  if(!pagedata) return NULL;
  VALGRIND_CREATE_MEMPOOL(pagedata, STALLOC_VALGRIND_RZB, false);
  VALGRIND_MAKE_MEM_NOACCESS(pagedata, pagesize+2*STALLOC_VALGRIND_RZB);
  page = (void *)(pagedata+STALLOC_VALGRIND_RZB);
  VALGRIND_MEMPOOL_ALLOC(pagedata, page, sizeof(*page));
  cur = align_ptr_native(((char *)&page[1]) + STALLOC_VALGRIND_RZB);
#else
  page = malloc(pagesize);
  if(!page) return NULL;
  cur = align_ptr_native(&page[1]);
#endif

#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
  size_t wasted = 0;
  if(st->page.last) {
    wasted = st->page.size - page_used_space(st->page.last, st->page.cur);
    st->space.free -= wasted;
    st->space.wasted += wasted;
  }
#endif
  
  page->prev = st->page.last;

  st->page.cur = cur;
  st->page.last = page;
  st->page.count++;

#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE  
  page->space.reserved = page_used_space(page, st->page.cur);
  st->space.free += st->page.size - page->space.reserved;
  st->space.reserved += page->space.reserved;
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  page->space.wasted = 0;
#else
  page->space.wasted = 0;
#endif
  page->space.cur = st->page.cur;
  page->space.used = 0;
#endif

  return page;
}

static void remove_last_page(shuso_stalloc_t *st) {
  shuso_stalloc_page_t *page = st->page.last;
  if(!page) return;
  st->page.last = page->prev;
  st->page.count--;
#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
    st->space.free -= st->page.size - page_used_space(page, page->space.cur);
    st->space.used -= page->space.used;
    st->space.wasted -= page->space.wasted;
    st->space.reserved -= page->space.reserved;
    shuso_stalloc_page_t *prev = st->page.last; 
    if(prev) {
      size_t freespace_left = st->page.size - page_used_space(prev, prev->space.cur);
      st->space.free += freespace_left;
      st->space.wasted -= freespace_left;
    }
#endif
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  char *pagedata = &((char *)page)[-STALLOC_VALGRIND_RZB];
  VALGRIND_DESTROY_MEMPOOL(pagedata);
  free(pagedata);
#else
  free(page);
#endif
}

bool shuso_stalloc_init(shuso_stalloc_t *st, size_t pagesize) {
  memset(st, 0x00, sizeof(*st));
  return shuso_stalloc_init_clean(st, pagesize);
}

bool shuso_stalloc_init_clean(shuso_stalloc_t *st, size_t pagesize) {
#ifdef SHUTTLESOCK_DEBUG_STALLOC_NOPOOL
  st->page.size = 0;
#else
  st->page.size = pagesize > 0 ? pagesize : shuso_system.page_size;
  if(!add_page(st)) {
    return false;
  }
#endif
  return true;
}

static void *plain_old_malloc(shuso_stalloc_t *st, size_t sz) {
  shuso_stalloc_allocd_t *cur;
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
  cur->prev = st->allocd.last;
  st->allocd.last = cur;
#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
  cur->size = sz;
  st->space.allocd += sz;
#endif
  return cur->data;
}

static void plain_old_free_last(shuso_stalloc_t *st) {
  shuso_stalloc_allocd_t *cur = st->allocd.last;
  st->allocd.last = cur->prev;
#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
  st->space.allocd -= cur->size;
#endif
#if defined(SHUTTLESOCK_DEBUG_SANITIZE) || defined(SHUTTLESOCK_DEBUG_VALGRIND)
  free(cur->data);
#endif
  free(cur);
}

static ssize_t page_max_free_space(ssize_t pagesize) {
  size_t headersize = align_native(sizeof(shuso_stalloc_page_t));
  return pagesize - headersize;
}

#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
static void stalloc_update_space(shuso_stalloc_t *st, shuso_stalloc_page_t *page, size_t sz, ssize_t before_pad, ssize_t after_pad) {
  st->space.wasted += before_pad + after_pad;
  st->space.used += sz;
  st->space.free -= sz + before_pad + after_pad;
  page->space.wasted += before_pad + after_pad;
  page->space.used += sz;
  page->space.cur = st->page.cur;
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  st->space.wasted -= 2*STALLOC_VALGRIND_RZB;
  st->space.reserved += 2*STALLOC_VALGRIND_RZB;
  page->space.wasted -= 2*STALLOC_VALGRIND_RZB;
  page->space.reserved += 2*STALLOC_VALGRIND_RZB;
#endif
}
#endif

static void *stalloc_in_current_page(shuso_stalloc_t *st, size_t sz, bool align) {
  ssize_t               pagesize = st->page.size;
  shuso_stalloc_page_t *page = st->page.last;
  char                 *cur = st->page.cur;
  char                 *ret;
  ssize_t               before_padding, after_padding;
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  ret = align ? align_ptr_native(&cur[STALLOC_VALGRIND_RZB]) : &cur[STALLOC_VALGRIND_RZB];
  after_padding = STALLOC_VALGRIND_RZB;
#else
  ret = align ? align_ptr_native(cur) : cur;
  after_padding = 0;
#endif
  before_padding = ret - cur;
  
  if(pagesize - (ssize_t )page_used_space(page, cur) < before_padding + (ssize_t )sz + after_padding) {
    //not enough space in this page
    return NULL;
  }
  st->page.cur = &ret[sz + after_padding];
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  VALGRIND_MEMPOOL_ALLOC(&((char *)page)[-STALLOC_VALGRIND_RZB], ret, sz);
#endif
#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
  stalloc_update_space(st, page, sz, before_padding, after_padding);
#endif
  return ret;
}

static inline void *stalloc_in_new_page(shuso_stalloc_t *st, size_t sz, bool align) {
  ssize_t      pagesize = st->page.size;
  ssize_t       before_padding, after_padding;
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  before_padding = STALLOC_VALGRIND_RZB, after_padding = STALLOC_VALGRIND_RZB;
#else
  before_padding = 0, after_padding = 0;
#endif
  
  if((ssize_t )page_max_free_space(pagesize) < before_padding + (ssize_t )sz + after_padding) {
    //not enough space in a brand new page
    return NULL;
  }
  
  shuso_stalloc_page_t *page;
  if((page = add_page(st)) == NULL) {
    return NULL;
  }
  assert(st->page.last == page);
  void *ret = stalloc_in_current_page(st, sz, align);
  assert(ret);
  return ret;
}

static void *shuso_stalloc_with_alignment(shuso_stalloc_t *st, size_t sz, bool align) {  
  
  void *ret;
  
  if((ret = stalloc_in_current_page(st, sz, align))) {
    return ret;
  }
  if((ret = stalloc_in_new_page(st, sz, align))) {
    return ret;
  }
  if((ret = plain_old_malloc(st, sz))) {
    return ret;
  }
  return NULL;
}

void *shuso_stalloc(shuso_stalloc_t *st, size_t sz) {
  return shuso_stalloc_with_alignment(st, sz, true);
}
void *shuso_stalloc_unaligned(shuso_stalloc_t *st, size_t sz) {
  return shuso_stalloc_with_alignment(st, sz, false);
}

int shuso_stalloc_push(shuso_stalloc_t *st) { 
  shuso_stalloc_frame_t *frame = shuso_stalloc(st, sizeof(*frame));
  
  if(!frame) return 0;
  *frame = (shuso_stalloc_frame_t ){
    .page = st->page.last,
    .page_cur = st->page.cur,
#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
    .page_space = (st->page.last == NULL ? (shuso_stalloc_page_space_t ){0} : st->page.last->space),
#endif
    .allocd = st->allocd.last
  };
  
  assert(st->stack.count < SHUTTLESOCK_STALLOC_STACK_SIZE);
  st->stack.stack[st->stack.count]=frame;
  st->stack.count++;
  return st->stack.count;
}

// remove all frames >= stackpos from the stack
// stackpos is NOT the number of frames to pop, but an absolute position for the stack to reach down to.
// that is why this function is weirdly named 'pop_to' instead of 'pop', which expects a number of frames to pop
bool shuso_stalloc_pop_to(shuso_stalloc_t *st, unsigned stackpos) {
  shuso_stalloc_frame_t *frame;
  if(stackpos > st->stack.count) {
    return false;
  }
  if(stackpos == 0) {
    if(!shuso_stalloc_empty(st)) {
      return false;
    }
#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
    st->space = (struct shuso_stalloc_space_s ){0,0,0,0,0};
#endif
    st->page = (struct shuso_stalloc_pages_s ){0,0,NULL,NULL};
    st->allocd = (struct shuso_stalloc_allocds_s ){NULL};
    st->stack = (struct shuso_stalloc_stackframes_s ){{NULL}, 0};
    return true;
  }
  frame = st->stack.stack[stackpos - 1];
  shuso_stalloc_allocd_t *allocd = frame->allocd;
  if(allocd) {
    while(st->allocd.last != allocd) {
      assert(st->allocd.last != NULL);
      plain_old_free_last(st);
    }
  }
  
  shuso_stalloc_page_t *page = frame->page;
  if(page) {
    assert(st->page.last != NULL);
    while(st->page.last != page) {
      assert(st->page.last != NULL);
      remove_last_page(st);
    }
  }
  
  //roll back page
  st->page.cur = frame->page_cur;
#ifdef SHUTTLESOCK_DEBUG_VALGRIND
  VALGRIND_MEMPOOL_TRIM((&((char *)page)[-STALLOC_VALGRIND_RZB]), page, st->page.cur);
#endif
#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
  size_t used_diff = page ? (page->space.used - frame->page_space.used) : 0;
  size_t wasted_diff = page ? (page->space.wasted - frame->page_space.wasted) : 0;
  st->space.used -= used_diff;
  st->space.wasted -= wasted_diff;
  st->space.free += used_diff + wasted_diff;
  if(page) {
    page->space = frame->page_space;
  }
#endif
  return true;
}

bool shuso_stalloc_empty(shuso_stalloc_t *st) {
  while(st->allocd.last != NULL) {
    plain_old_free_last(st);
  }
  while(st->page.last != NULL) {
    remove_last_page(st);
  }
  //leaves the stalloc in a dirty state
  return true;
}

size_t shuso_stalloc_space(shuso_stalloc_t *st, shuso_stalloc_space_kind_t kind) {
  size_t space = 0;
  switch(kind) {
#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
    case SPACE_USED:
      space = st->space.used;
      break;
    case SPACE_FREE:
      space = st->space.free;
      break;
    case SPACE_WASTED:
      space = st->space.wasted;
      break;
    case SPACE_RESERVED:
      space = st->space.reserved;
      break;
#else
    case SPACE_USED:
      space = st->page.size * (st->page.count - 1)
            + st->page.last == NULL ? 0 : page_used_space(st->page.last, st->page.cur);
      break;
    case SPACE_FREE:
      space = st->page.last == NULL ? 0 : st->page.size - page_used_space(st->page.last, st->page.cur);
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
