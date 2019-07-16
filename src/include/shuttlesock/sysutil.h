#ifndef SHUTTLESOCK_SYSUTIL_H
#define SHUTTLESOCK_SYSUTIL_H
#include <stdbool.h>

typedef struct {
  size_t      page_size;
  size_t      cacheline_size;
  size_t      page_shift;
  
  bool        initialized;
} shuso_sysinfo_t;
extern shuso_sysinfo_t shuttlesock_sysinfo;

void shuttlesock_system_info_initialize(void);

int shuso_system_cores_online(void);
bool shuso_system_thread_setname(const char *name);

#endif //SHUTTLESOCK_SYSUTIL_H
