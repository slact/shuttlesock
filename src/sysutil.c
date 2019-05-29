#include <shuttlesock/configure.h>
#include <unistd.h>
#include <stdint.h>

#ifndef _SC_NPROCESSORS_ONLN
#include <sys/param.h>
#include <sys/sysctl.h>
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
