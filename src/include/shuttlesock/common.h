#ifndef SHUTTLESOCK_COMMON_H
#define SHUTTLESOCK_COMMON_H
#include <stdbool.h>

typedef enum {
  SHUSO_OK        =  1,
  SHUSO_FAIL      =  0,
  SHUSO_TIMEOUT   = -1,
  SHUSO_DEFERRED  = -2, //try again later? kind of like EAGAIN
} shuso_status_t;

typedef enum {
  SHUSO_STOP_ASK =      1,
  SHUSO_STOP_INSIST =   2,
  SHUSO_STOP_DEMAND =   3,
  SHUSO_STOP_COMMAND =  4,
  SHUSO_STOP_FORCE =    5
} shuso_stop_t;

typedef enum {
  //non-positive states MUST be kinds of non-running states
  SHUSO_PROCESS_STATE_DEAD = -1,
  SHUSO_PROCESS_STATE_NIL = 0,
  //positive states MUST be kinds of running states
  SHUSO_PROCESS_STATE_STARTING = 1,
  SHUSO_PROCESS_STATE_RUNNING  = 2,
  SHUSO_PROCESS_STATE_STOPPING = 3
} shuso_process_state_t;

#define SHUTTLESOCK_UNKNOWN_PROCESS  -404
#define SHUTTLESOCK_NOPROCESS  -3
#define SHUTTLESOCK_MASTER     -2
#define SHUTTLESOCK_MANAGER    -1
#define SHUTTLESOCK_WORKER      0

typedef struct shuso_ev_io_s shuso_ev_io;
typedef struct shuso_ev_timer_s shuso_ev_timer;
typedef struct shuso_ev_child_s shuso_ev_child;
typedef struct shuso_ev_signal_s shuso_ev_signal;


typedef struct ev_loop shuso_loop; //don't want to write struct ev_loop everywhere or use EV_A and EV_P_ macros, they're ugly.
typedef struct shuso_s shuso_t;
typedef struct shuso_common_s shuso_common_t;
typedef struct shuso_process_s shuso_process_t;
typedef struct shuso_config_s shuso_config_t;
typedef struct shuso_config_setting_s shuso_config_setting_t;

typedef struct shuso_hostinfo_s shuso_hostinfo_t;
typedef struct shuso_sockopts_s shuso_sockopts_t;
typedef struct shuso_sockopt_s shuso_sockopt_t;
typedef struct shuso_socket_s shuso_socket_t;

typedef void shuso_socket_fn(shuso_t *ctx, shuso_socket_t *socket);
typedef void shuso_socket_listener_fn(shuso_t *ctx, shuso_socket_t *socket, void *pd);

typedef void shuso_handler_fn(shuso_t *ctx, void *pd);
typedef bool shuso_config_set_fn(shuso_t *ctx, void *config, shuso_config_setting_t *cf);
typedef void *shuso_config_init_fn(shuso_t *ctx, void *parent);
typedef struct shuso_runtime_handlers_s shuso_runtime_handlers_t;
typedef struct shuso_config_handlers_s shuso_config_handlers_t;
#endif /*SHUTTLESOCK_COMMON_H*/
