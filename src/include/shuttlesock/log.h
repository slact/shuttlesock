#ifndef SHUTTLESOCK_LOG_H
#define SHUTTLESOCK_LOG_H

#include <shuttlesock/common.h>

void shuso_log(shuso_t *S, const char *fmt, ...);
void shuso_log_debug(shuso_t *S, const char *fmt, ...);
void shuso_log_info(shuso_t *S, const char *fmt, ...);
void shuso_log_notice(shuso_t *S, const char *fmt, ...);
void shuso_log_warning(shuso_t *S, const char *fmt, ...);
void shuso_log_error(shuso_t *S, const char *fmt, ...);
void shuso_log_critical(shuso_t *S, const char *fmt, ...);
void shuso_log_fatal(shuso_t *S, const char *fmt, ...);


#endif //SHUTTLESOCK_LOG_H
