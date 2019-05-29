#if SHUTTLESOCK_PTHREAD_SETNAME_STYLE == SETNAME_STYLE_LINUX
#define _GNU_SOURCE
#endif
#include <pthread.h>
#ifdef _GNU_SOURCE
#undef _GNU_SOURCE
#endif

#include <shuttlesock/configure.h>
#include <unistd.h>
#include <stdint.h>
#include <stdbool.h>

#ifndef _SC_NPROCESSORS_ONLN
#include <sys/param.h>
#include <sys/sysctl.h>
#endif


#ifdef SHUTTLESOCK_PTHREAD_SETNAME_INCLUDE_PTRHEAD_NP
#include <pthread_np.h>
#endif

int shuso_system_cores_online(void) {
  uint32_t cores;
#ifdef _SC_NPROCESSORS_ONLN
  cores = sysconf(_SC_NPROCESSORS_ONLN);
  if (cores < 1 || cores > (uint32_t )(1<<31)) {
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
  if (cores < 1 || cores > (uint32_t )(1<<31)) {
    cores = 4;
  }
  return cores;
}

bool shuso_system_thread_setname(const char *name) {
#if SHUTTLESOCK_PTHREAD_SETNAME_STYLE == SETNAME_STYLE_LINUX
  return pthread_setname_np(pthread_self(), name) == 0;
#elif SHUTTLESOCK_PTHREAD_SETNAME_STYLE == SETNAME_STYLE_FREEBSD
  return pthread_set_name_np(pthread_self(), name) == 0;
#elif SHUTTLESOCK_PTHREAD_SETNAME_STYLE == SETNAME_STYLE_NETBSD
  return pthread_set_name_np(th, "%s", name) == 0;
#else
  return false;
#endif
}
