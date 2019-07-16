// This is an adaptation of Nginx's pretty good shared memory slab allocator

/*
 * Copyright (C) Igor Sysoev
 * Copyright (C) Nginx, Inc.
 */


#ifndef SHUTTLESOCK_SHARED_SLAB_H
#define SHUTTLESOCK_SHARED_SLAB_H

#include <shuttlesock/configure.h>
#include <stdint.h>
#include <pthread.h>

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


void shuso_shared_slab_sizes_init(void);


void shuso_shared_slab_init(shuso_slab_pool_t *pool);
void *ngx_slab_alloc(shuso_slab_pool_t *pool, size_t size);
void *ngx_slab_alloc_locked(shuso_slab_pool_t *pool, size_t size);
void *ngx_slab_calloc(shuso_slab_pool_t *pool, size_t size);
void *ngx_slab_calloc_locked(shuso_slab_pool_t *pool, size_t size);
void ngx_slab_free(shuso_slab_pool_t *pool, void *p);
void ngx_slab_free_locked(shuso_slab_pool_t *pool, void *p);


#endif /* SHUTTLESOCK_SHARED_SLAB_H */
