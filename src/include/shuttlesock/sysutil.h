#ifndef SHUTTLESOCK_SYSUTIL_H
#define SHUTTLESOCK_SYSUTIL_H
#include <stdbool.h>

typedef struct {
  size_t      page_size;
  size_t      page_size_shift;
  size_t      cacheline_size; //data cacheline
  
  bool        initialized;
} shuso_system_setup_t;
extern shuso_system_setup_t shuso_system;

void shuso_system_initialize(void);

int shuso_system_cores_online(void);
bool shuso_system_thread_setname(const char *name);

#endif //SHUTTLESOCK_SYSUTIL_H
