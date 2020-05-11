#ifndef SHUTTLESOCK_INSTRING_H
#define SHUTTLESOCK_INSTRING_H
#include <shuttlesock/common.h>
#include <shuttlesock/buffer.h>
#include <shuttlesock/module.h>

typedef struct shuso_variable_s shuso_variable_t;

typedef bool shuso_variable_eval_fn(shuso_t *S, shuso_variable_t *var, void *eval_state, int nindices, ...);
typedef void shuso_variable_cleanup_fn(shuso_t *S, shuso_variable_t *var, shuso_str_t *val);

typedef struct shuso_variable_s {
  const shuso_module_t *module;
  const char           *name;
  struct {
    const char         **array;
    size_t               size;
  }                    indices;
  shuso_variable_eval_fn     *eval;
  void                       *eval_state;
} shuso_variable_t;

#define SHUTTLESOCK_INSTRING_VALUE_UNKNOWN 0
#define SHUTTLESOCK_INSTRING_VALUE_INVALID 1
#define SHUTTLESOCK_INSTRING_VALUE_VALID 2

typedef enum {
  SHUSO_INSTRING_TOKEN_LITERAL = 0,
  SHUSO_INSTRING_TOKEN_VARIABLE = 1
} shuso_instring_token_type_t;

typedef struct {
  shuso_instring_token_type_t type;
  union {
    shuso_str_t         literal;
    shuso_variable_t    variable;
  };
} shuso_instring_token_t;

typedef struct {
  shuso_str_t   raw;
  struct {
    shuso_variable_t **array;
    size_t             count;
  } variables;
  struct {
    shuso_instring_token_t *array;
    size_t                   count;
  } tokens;
  struct {
    shuso_buffer_t      head;
    shuso_buffer_link_t link;
    struct iovec       *iov;
  } buffer;
  shuso_buffer_t      buffer;
  struct {
    struct { //valid/invalid/unknown
      unsigned    boolean:2;
      unsigned    integer:2;
      unsigned    number:2;
      unsigned    string:2;
      unsigned    buffer:2;
    }           state;
    shuso_buffer_t buffer;
    bool        boolean;
    int         integer;
    double      number;
    shuso_str_t string;
  } cached_value;
} shuso_instring_t;

typedef struct shuso_instrings_s {
  size_t            count;
  shuso_instring_t  array[];
} shuso_instrings_t;

shuso_setting_value_t *shuso_instring_value(shuso_t *S, shuso_instring_t *instring);
shuso_instring_t *luaS_instring_lua_to_c(lua_State *L, int index);
shuso_instrings_t *luaS_instrings_lua_to_c(lua_State *L, int index);

#endif //SHUTTLESOCK_SHUSTRING_H
