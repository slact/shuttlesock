#ifndef SHUTTLESOCK_INSTRING_H
#define SHUTTLESOCK_INSTRING_H
#include <shuttlesock/common.h>
#include <shuttlesock/buffer.h>
#include <shuttlesock/module.h>

typedef struct shuso_variable_s {
  const shuso_module_t *module;
  const char           *name;
  shuso_setting_t      *setting;
  shuso_setting_block_t *block;
  struct {
    const char         **array;
    size_t               size;
  }                    params;
  shuso_variable_eval_fn     *eval;
  void                       *privdata;
  void                       *state;
  
} shuso_variable_t;

#define SHUTTLESOCK_INSTRING_VALUE_UNKNOWN 0
#define SHUTTLESOCK_INSTRING_VALUE_INVALID 1
#define SHUTTLESOCK_INSTRING_VALUE_VALID   2

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

typedef struct { //valid/invalid/unknown
  unsigned    boolean:2;
  unsigned    integer:2;
  unsigned    number:2;
  unsigned    size:2;
  unsigned    string:2;
} shuso_instring_value_state_t;

typedef struct {
  shuso_str_t   raw;
  struct {
    shuso_variable_t **array;
    size_t             count;
  } variables;
  struct {
    shuso_instring_token_t *array;
    size_t                  count;
  } tokens;
  struct {
    shuso_buffer_t      head;
    shuso_buffer_link_t link;
    struct iovec       *iov;
  } buffer;
  struct {
    shuso_instring_value_state_t reset_state;
    shuso_instring_value_state_t state;
    bool        boolean;
    int         integer;
    double      number;
    size_t      size;
    shuso_str_t string;
    lua_reference_t string_lua_ref;
  } cached_value;
} shuso_instring_t;

typedef struct shuso_instrings_s {
  size_t            count;
  shuso_instring_t  array[];
} shuso_instrings_t;


shuso_instring_t *luaS_instring_lua_to_c(lua_State *L, shuso_setting_t *setting, int index);
shuso_instrings_t *luaS_instrings_lua_to_c(lua_State *L, shuso_setting_t *setting, int index);
shuso_instrings_t *shuso_instrings_copy_for_worker(shuso_t *S, shuso_instrings_t *old);


bool shuso_instring_boolean_value(shuso_t *S, shuso_instring_t *instring, bool *retval);
bool shuso_instring_integer_value(shuso_t *S, shuso_instring_t *instring, int *retval);
bool shuso_instring_number_value(shuso_t *S, shuso_instring_t *instring, double *retval);
bool shuso_instring_size_value(shuso_t *S, shuso_instring_t *instring, size_t *retval);
bool shuso_instring_string_value(shuso_t *S, shuso_instring_t *instring, shuso_str_t *retval);
bool shuso_instring_buffer_value(shuso_t *S, shuso_instring_t *instring, shuso_buffer_t **retval);
#define shuso_instring_value(S, instring, retval) \
  _Generic((instring), \
    bool *                : shuso_instring_boolean_value, \
    int *                 : shuso_instring_integer_value, \
    double *              : shuso_instring_number_value, \
    size_t *              : shuso_instring_size_value, \
    shuso_str_t *         : shuso_instring_string_value, \
    shuso_buffer_t **     : shuso_instring_buffer_value \
  )(S, instring, retval)

#endif //SHUTTLESOCK_SHUSTRING_H
