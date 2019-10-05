#include <shuttlesock/common.h>
#include <shuttlesock/stalloc.h>
#include <shuttlesock/sysutil.h>
#include <assert.h>
#include <unistd.h>
#include <stdio.h>
#if defined(SHUTTLESOCK_VALGRIND)
#include <valgrind/memcheck.h>
#endif

#define align_ptr_native(p)                                                   \
  (void *)(((uintptr_t) (p) + ((uintptr_t)(sizeof(void *)) - 1)) & ~((uintptr_t)(sizeof(void *)) - 1))

#define align_ptr(p, align)                                                   \
  (void *)(((uintptr_t) (p) + ((uintptr_t)align - 1)) & ~((uintptr_t)align - 1))

#define page_used_space(page, cur) \
  (size_t )((uintptr_t)(cur) - (uintptr_t)(page))


static shuso_stalloc_page_t *add_page(shuso_stalloc_t *st) {
  shuso_stalloc_page_t *page = malloc(st->page.size);
  if(!page) return NULL;
#ifdef SHUTTLESOCK_VALGRIND
  VALGRIND_MAKE_MEM_NOACCESS(page, st->page.size);
  VALGRIND_MAKE_MEM_UNDEFINED(page, sizeof(*page)); //page header
  st->page.cur+=sizeof(void *); //add padding
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
  st->page.cur = align_ptr_native(&page[sizeof(*page)]);
  st->page.last = page;
  st->page.count++;

#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE  
  page->space.reserved = page_used_space(page, st->page.cur);
  st->space.free += st->page.size - page->space.reserved;
  st->space.reserved += page->space.reserved;
  page->space.wasted = 0;
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
  free(page);
}

bool shuso_stalloc_init(shuso_stalloc_t *st, size_t pagesize) {
  memset(st, 0x00, sizeof(*st));
  return shuso_stalloc_init_clean(st, pagesize);
}

bool shuso_stalloc_init_clean(shuso_stalloc_t *st, size_t pagesize) {
#if defined(SHUTTLESOCK_DEBUG_STALLOC_NOPOOL)
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
#if defined(SHUTTLESOCK_SANITIZE) || defined(SHUTTLESOCK_VALGRIND)
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
#if defined(SHUTTLESOCK_SANITIZE) || defined(SHUTTLESOCK_VALGRIND)
  free(cur->data);
#endif
  free(cur);
}

static inline void *shuso_stalloc_with_alignment(shuso_stalloc_t *st, size_t sz, bool align) {
  ssize_t               pagesize = st->page.size;
  shuso_stalloc_page_t *page = st->page.last;
  char                 *cur = st->page.cur;
  ssize_t               space_left = pagesize - page_used_space(page, cur);
  char                 *ret;
  ssize_t               align_padding;
  if(align) {
    ret = align_ptr_native(cur);
    align_padding = ret - cur;
  }
  else {
    ret = cur;
    align_padding = 0;
  }
#ifdef SHUTTLESOCK_VALGRIND
  ssize_t valgrind_padding = 0;
  if(ret == cur) {
    //add padding between used page chunks
    ret = align_ptr_native(&ret[1]);
    valgrind_padding = ret - cur;
    align_padding += valgrind_padding;
    assert(align_padding >= (ssize_t )sizeof(void *));
  }
#endif
  if(space_left - align_padding >= (ssize_t )sz) {
    st->page.cur = &ret[sz];
#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
    st->space.wasted += align_padding;
#ifdef SHUTTLESOCK_VALGRIND
    st->space.wasted -= valgrind_padding;
    st->space.reserved += valgrind_padding;
#endif
    st->space.used += sz;
    st->space.free -= sz + align_padding;
    page->space.wasted += align_padding;
    page->space.used += sz;
    page->space.cur = st->page.cur;
#endif
#ifdef SHUTTLESOCK_VALGRIND
    VALGRIND_MAKE_MEM_UNDEFINED(ret, sz);
#endif
  }
  else if((ssize_t )sz < pagesize) {
    if((page = add_page(st)) == NULL) {
      return NULL;
    }
    //new page is aligned for sure
#ifdef SHUTTLESOCK_STALLOC_TRACK_SPACE
    st->space.used += sz;
    st->space.free -= sz;
    page->space.used += sz;
    page->space.cur = st->page.cur;
#endif
    ret = st->page.cur;
    st->page.cur = &ret[sz];
#ifdef SHUTTLESOCK_VALGRIND
    VALGRIND_MAKE_MEM_UNDEFINED(ret, sz);
#endif
  }
  else {
    if((ret = plain_old_malloc(st, sz)) == NULL) {
      return NULL;
    }
  }
  return ret;
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
    .page_space = st->page.last->space,
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
