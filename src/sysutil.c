#include <shuttlesock/configure.h>
#ifdef SHUTTLESOCK_PTHREAD_SETNAME_STYLE_LINUX
#define _GNU_SOURCE
#endif
#include <pthread.h>
#ifdef _GNU_SOURCE
#undef _GNU_SOURCE
#endif

#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef _SC_NPROCESSORS_ONLN
#include <sys/param.h>
#endif
#if !defined(_SC_NPROCESSORS_ONLN) || defined(__APPLE__)
#include <sys/sysctl.h>
#endif


#ifdef SHUTTLESOCK_PTHREAD_SETNAME_INCLUDE_PTRHEAD_NP
#include <pthread_np.h>
#endif

#include <stdio.h>
#include <assert.h>
#include <shuttlesock/sysutil.h>

shuso_system_setup_t shuso_system = {
  .initialized = false
};

int shuso_system_cores_online(void) {
  long cores;
#ifdef _SC_NPROCESSORS_ONLN
  cores = sysconf(_SC_NPROCESSORS_ONLN);
  if (cores < 1 || cores > ((long )1<<31)) {
    cores = sysconf(_SC_NPROCESSORS_CONF);
  }
#else
  int nm[2];
  size_t len = 4;
  nm[0] = CTL_HW; nm[1] = HW_AVAILCPU;
  sysctl(nm, 2, &cores, &len, NULL, 0);

  if(cores < 1) {
    nm[1] = HW_NCPU;
    sysctl(nm, 2, &cores, &len, NULL, 0);
  }
#endif
  if (cores < 1 || cores > ((long )1<<31)) {
    cores = 4;
  }
  return cores;
}

bool shuso_system_thread_setname(const char *name) {
#if defined SHUTTLESOCK_PTHREAD_SETNAME_STYLE_LINUX
  return pthread_setname_np(pthread_self(), name) == 0;
#elif defined SHUTTLESOCK_PTHREAD_SETNAME_STYLE_FREEBSD
  return pthread_set_name_np(pthread_self(), name) == 0;
#elif defined SHUTTLESOCK_PTHREAD_SETNAME_STYLE_NETBSD
  return pthread_set_name_np(th, "%s", name) == 0;
#else
  return false;
#endif
}

static size_t shuso_system_cacheline_size(void) {
  size_t sz = 0;
#ifdef _SC_LEVEL1_DCACHE_LINESIZE
  sz = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
  if(sz) return sz;
#endif
// Original Author: Nick Strupat
// Date: October 29, 2010
#if defined(__APPLE__)
  size_t szsz = sizeof(sz);
  sysctlbyname("hw.cachelinesize", &sz, &szsz, 0, 0);
#elif defined(__linux__)
  FILE          *p = 0;
  int            lineSize = 0;
  p = fopen("/sys/devices/system/cpu/cpu0/cache/index0/coherency_line_size", "r");
  if(p) {
    fscanf(p, "%d", &lineSize);
    sz = lineSize;
    fclose(p);
  }
#endif
  assert(sz != 0);
  return sz;
}

void shuso_system_initialize(void) {
  if(!shuso_system.initialized) {
    //NOT THREAD-SAFE!!
    shuso_system.page_size = sysconf(_SC_PAGESIZE);
    shuso_system.cacheline_size = shuso_system_cacheline_size();
    shuso_system.page_size_shift = 0;
    for(uintptr_t n = shuso_system.page_size; n >>= 1; shuso_system.page_size_shift++) { /* void */ }
    shuso_system.initialized = 1;
  }  
}
