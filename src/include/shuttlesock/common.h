#ifndef SHUTTLESOCK_COMMON_H
#define SHUTTLESOCK_COMMON_H
#include <stdbool.h>
#include <stdint.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <sys/socket.h>
#include <shuttlesock/build_config.h>
#ifndef container_of
#define container_of(ptr, type, member) ((type *)(void *)((char *)(1 ? (ptr) : &((type *)0)->member) - offsetof(type, member)))
#endif

typedef struct shuso_fn_debug_info_s shuso_fn_debug_info_t;

typedef struct ev_loop shuso_loop; //don't want to write struct ev_loop everywhere or use EV_A and EV_P_ macros, they're ugly.

#define SHUSO_NO    0
#define SHUSO_YES   1
#define SHUSO_MAYBE 2

typedef enum {
  SHUSO_OK        =  1,
  SHUSO_FAIL      =  0,
  SHUSO_TIMEOUT   = -1,
  SHUSO_DEFERRED  = -2, //try again later? kind of like EAGAIN
} shuso_status_t;

typedef enum {
  SHUSO_LOG_FATAL =     1,
  SHUSO_LOG_CRITICAL =  2,
  SHUSO_LOG_ERROR =     3,
  SHUSO_LOG_WARNING =   4,
  SHUSO_LOG_NOTICE =    5, 
  SHUSO_LOG_INFO =      6,
  SHUSO_LOG_DEBUG =     7
} shuso_loglevel_t;

typedef enum {
  SHUSO_STOP_ASK =      1,
  SHUSO_STOP_INSIST =   2,
  SHUSO_STOP_DEMAND =   3,
  SHUSO_STOP_COMMAND =  4,
  SHUSO_STOP_FORCE =    5
} shuso_stop_t;

typedef enum {
  //non-positive states MUST be kinds of non-running states
  SHUSO_STATE_DEAD = -5,
  SHUSO_STATE_STOPPED = -4,
  SHUSO_STATE_MISCONFIGURED = -3,
  SHUSO_STATE_CONFIGURING = -2,
  SHUSO_STATE_CONFIGURED = -1,
  SHUSO_STATE_NIL = 0,
  SHUSO_STATE_STARTING = 1,
  SHUSO_STATE_RUNNING  = 2,
  SHUSO_STATE_STOPPING = 3
} shuso_runstate_t;

typedef enum {
  SHUTTLESOCK_VALUE_BOOLEAN = 1,
  SHUTTLESOCK_VALUE_INTEGER,
  SHUTTLESOCK_VALUE_NUMBER,
  SHUTTLESOCK_VALUE_STRING,
  SHUTTLESOCK_VALUE_BUFFER
} shuso_value_type_t;

#define SHUTTLESOCK_UNKNOWN_PROCESS  -404
#define SHUTTLESOCK_NOPROCESS  -3
#define SHUTTLESOCK_MASTER     -2
#define SHUTTLESOCK_MANAGER    -1
#define SHUTTLESOCK_WORKER      0

typedef struct shuso_ev_io_s shuso_ev_io;
typedef struct shuso_ev_timer_s shuso_ev_timer;
typedef struct shuso_ev_child_s shuso_ev_child;
typedef struct shuso_ev_signal_s shuso_ev_signal;
typedef union shuso_ev_any_u shuso_ev_any;
typedef int lua_reference_t;


typedef struct shuso_s shuso_t;
typedef struct shuso_common_s shuso_common_t;
typedef struct shuso_process_s shuso_process_t;

typedef struct shuso_setting_block_s shuso_setting_block_t;
typedef struct shuso_setting_s shuso_setting_t;

typedef struct shuso_config_s shuso_config_t;

typedef struct shuso_module_s shuso_module_t;
typedef struct shuso_event_s shuso_event_t;
typedef struct shuso_module_context_list_s shuso_module_context_list_t;
typedef struct shuso_module_setting_s shuso_module_setting_t;
typedef struct shuso_event_state_s shuso_event_state_t;

typedef struct shuso_variable_s shuso_variable_t;

typedef struct shuso_sockopts_s shuso_sockopts_t;

typedef union {
  struct sockaddr     any;
  struct sockaddr_in  in;
#ifdef SHUTTLESOCK_HAVE_IPV6
  struct sockaddr_in6 in6;
#endif
  struct sockaddr_un  un;
} shuso_sockaddr_t;

typedef struct shuso_hostinfo_s {
  const char        *name;
  sa_family_t       family; //address family: AF_INET/AF_INET6/AF_UNIX
  int               type; //socket type: SOCK_STREAM/SOCK_DGRAM/SOCK_RAW
  shuso_sockaddr_t *sockaddr;
} shuso_hostinfo_t;

typedef struct shuso_socket_s {
  int               fd;
  shuso_hostinfo_t  host;
} shuso_socket_t;

typedef struct shuso_str_s {
  char            *data;
  size_t           len;
} shuso_str_t;

typedef struct shuso_io_uring_handle_s shuso_io_uring_handle_t;
typedef void shuso_io_uring_fn(shuso_t *S, int32_t ret, uint32_t flags, shuso_io_uring_handle_t *handle, void *pd);
struct shuso_io_uring_handle_s {
  shuso_io_uring_fn       *callback;
  void                    *pd;
}; // shuso_io_uring_handle_t

typedef struct shuso_connection_s shuso_connection_t;

typedef void shuso_socket_fn(shuso_t *S, shuso_socket_t *socket);
typedef void shuso_socket_listener_fn(shuso_t *S, shuso_socket_t *socket, void *pd);

typedef void shuso_handler_fn(shuso_t *S, void *pd);

typedef bool shuso_module_init_fn(shuso_t *S, shuso_module_t *);
typedef bool shuso_module_init_worker_fn(shuso_t *S, shuso_module_t *, shuso_t *Smanager);
typedef bool shuso_module_config_init_fn(shuso_t *S, shuso_module_t *, shuso_setting_block_t *);

typedef void shuso_event_fn(shuso_t *S, shuso_event_state_t *, intptr_t code, void *data, void *pd);

typedef bool shuso_variable_eval_fn(shuso_t *S, shuso_variable_t *var, shuso_str_t *ret_val);


#if defined(__has_feature)
  #if __has_feature(memory_sanitizer)
    #define SHUSO_MEMORY_SANITIZER_ENABLED
  #endif
  #if __has_feature(memory_sanitizer)
    #define SHUSO_NO_SANITIZE_MEMORY __attribute__((no_sanitize("memory")))
  #else
    #define SHUSO_NO_SANITIZE_MEMORY
  #endif
  
  #if __has_feature(memory_sanitizer)
    #define SHUSO_NO_SANITIZE_ADDRESS __attribute__((no_sanitize("address")))
  #else
    #define SHUSO_NO_SANITIZE_ADDRESS
  #endif
#else
  #define SHUSO_NO_SANITIZE_MEMORY
  #define SHUSO_NO_SANITIZE_ADDRESS
#endif

#endif /*SHUTTLESOCK_COMMON_H*/
