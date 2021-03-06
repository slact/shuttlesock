#ifndef SHUTTLESOCK_SYSUTIL_H
#define SHUTTLESOCK_SYSUTIL_H
#include <stdbool.h>
#include <stdlib.h>

typedef struct {
  size_t      page_size;
  size_t      page_size_shift;
  size_t      cacheline_size; //data cacheline
  
  bool        initialized;
} shuso_system_setup_t;

typedef enum {
  SHUSO_SYSTEM_SOCKOPT_MISSING = 0,
  SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_INT,
  SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_FLAG,
  SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_LINGER,
  SHUSO_SYSTEM_SOCKOPT_VALUE_TYPE_TIMEVAL,
} shuso_system_sockopt_type_t;

typedef struct {
  const char  *str;
  int          level;
  int          name;
  shuso_system_sockopt_type_t value_type;
} shuso_system_sockopts_t;
extern shuso_system_sockopts_t shuso_system_sockopts[];

extern shuso_system_setup_t shuso_system;

void shuso_system_initialize(void);

int shuso_system_cores_online(void);
bool shuso_system_thread_setname(const char *name);

const char *shuso_system_strsignal(int sig);
const char *shuso_system_errnoname(int errno_);

#endif //SHUTTLESOCK_SYSUTIL_H
