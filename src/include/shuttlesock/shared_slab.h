// This is an adaptation of Nginx's pretty good shared memory slab allocator

/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef SHUTTLESOCK_SHARED_SLAB_H
#define SHUTTLESOCK_SHARED_SLAB_H

#define SHUTTLESOCK_SHARED_SLAB_DEFAULT_SIZE 16777216 //16M

#include <shuttlesock/build_config.h>
#include <shuttlesock/common.h>
#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>

typedef struct shuso_slab_page_s  shuso_slab_page_t;

struct shuso_slab_page_s {
    uintptr_t         slab;
    shuso_slab_page_t  *next;
    uintptr_t         prev;
};

typedef struct {
    unsigned            total;
    unsigned            used;

    unsigned            reqs;
    unsigned            fails;
} shuso_slab_stat_t;


typedef struct {
    size_t              min_size;
    size_t              min_shift;

    shuso_slab_page_t  *pages;
    shuso_slab_page_t  *last;
    shuso_slab_page_t   free;

    shuso_slab_stat_t  *stats;
    unsigned            pfree;

    unsigned char       *start;
    unsigned char       *end;

    pthread_mutex_t      mutex;

    //unsigned char     zero;

    unsigned            log_nomem:1;

    void               *data;
    void               *addr;
} shuso_slab_pool_t;


typedef struct {
  
  const char        *name;
  int                fd;
  void              *ptr;
  size_t             size;
  shuso_slab_pool_t *pool;
  
} shuso_shared_slab_t;

void shuso_shared_slab_sizes_init(void);

bool shuso_shared_slab_create(shuso_t *S, shuso_shared_slab_t *shm, size_t sz, const char *name);
bool shuso_shared_slab_destroy(shuso_t *S, shuso_shared_slab_t *shm);

void *shuso_shared_slab_alloc(shuso_shared_slab_t *shm, size_t size);
void *shuso_shared_slab_alloc_locked(shuso_shared_slab_t *shm, size_t size);
void *shuso_shared_slab_calloc(shuso_shared_slab_t *shm, size_t size);
void *shuso_shared_slab_calloc_locked(shuso_shared_slab_t *shm, size_t size);
void shuso_shared_slab_free(shuso_shared_slab_t *shm, void *p);
void shuso_shared_slab_free_locked(shuso_shared_slab_t *shm, void *p);


#endif /* SHUTTLESOCK_SHARED_SLAB_H */
