#ifndef SHUTTLESOCK_SYSUTIL_H
#define SHUTTLESOCK_SYSUTIL_H
#include <stdbool.h>
int shuso_system_cores_online(void);
bool shuso_system_thread_setname(const char *name);

#endif //SHUTTLESOCK_SYSUTIL_H
