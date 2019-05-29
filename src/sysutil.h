#ifndef __SHUTTLESOCK_SYSUTIL_H
#define __SHUTTLESOCK_SYSUTIL_H
#include <stdbool.h>
int shuso_system_cores_online(void);
bool shuso_system_thread_setname(const char *name);

#endif //__SHUTTLESOCK_SYSUTIL_H
