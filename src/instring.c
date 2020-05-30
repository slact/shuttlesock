#include <shuttlesock.h>
#include <shuttlesock/modules/config/private.h>
static bool instring_token_literal_lua_to_c(lua_State *L, shuso_instring_token_t *token, struct iovec *iov, int index) {
  size_t      len;
  const char *str;
  shuso_t    *S = shuso_state(L);
  index = lua_absindex(L, index);
  token->type = SHUSO_INSTRING_TOKEN_LITERAL;
  lua_getfield(L, index, "value");
  str = lua_tolstring(L, -1, &len);
  token->literal.len = len;
  token->literal.data = (char *)shuso_stalloc(&S->stalloc, len+1);
  if(!token->literal.data) {
    shuso_set_error(S, "no memory for instring literal");
    return false;
  }
  memcpy(token->literal.data, str, len + 1);
  
  iov->iov_base = token->literal.data;
  iov->iov_len = len;
  
  lua_pop(L, 1); //pop .value
  return true;
}

static bool instring_token_variable_lua_to_c(lua_State *L, shuso_setting_t *setting, shuso_instring_token_t *token, struct iovec *iov, int index) {
  shuso_t    *S = shuso_state(L);

  token->type = SHUSO_INSTRING_TOKEN_VARIABLE;
  shuso_variable_t *var = &token->variable;
  
  luaL_checkstack(L, 4, NULL);
  
  const char *name, *module_name;
  lua_getfield(L, index, "name");
  name = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  lua_getfield(L, index, "module_name");
  module_name = lua_tostring(L, -1);
  lua_pop(L, 1);
  
  var->block = shuso_setting_parent_block(S, setting);
  
  
  //can anyone handle this variable guys?...
  module_name ? lua_pushstring(L, var->module->name) : lua_pushnil(L);
  lua_pushstring(L, name);
  lua_pushlightuserdata(L, var->block);
  luaS_pcall_config_method(L, "find_variable", 3, 2);
  if(lua_isnil(L, -2)) {
    const char *err = lua_isstring(L, -1) ? lua_tostring(L, -1) : "unable to find variable for unknown reason";
    shuso_set_error(S, err);
    lua_pop(L, 2);
    return false;
  }
  lua_getfield(L, -1, "eval");
  var->eval = lua_topointer(L, -1);
  lua_pop(L, 1);
  var->pd = NULL;
  lua_getfield(L, -1, "constant");
  bool constant = lua_toboolean(L, -1);
  lua_pop(L, 1); //pop .constant
  lua_pop(L, 1); //pop variable
  
  var->module = module_name ? shuso_get_module(S, module_name) : NULL;
  
  var->setting = setting;
  var->block = shuso_setting_parent_block(S, setting);
  
  var->name = shuso_stalloc(&S->stalloc, strlen(name)+1);
  if(!var->name) {
    lua_pop(L, 1);
    shuso_set_error(S, "no memory for instring variable name");
    return false;
  }
  strcpy((char *)var->name, name);
  lua_pop(L, 1);
  
  lua_getfield(L, index, "params");
  int params_count = luaL_len(L, -1);
  var->params.size = params_count;
  if(params_count > 0) {
    if((var->params.array = shuso_stalloc(&S->stalloc, sizeof(*var->params.array))) == NULL) {
      return shuso_set_error(S, "no memory for instring variable index array");
    }
    for(int j = 0; j < params_count; j++) {
      const char *str;
      size_t      len;
      lua_rawgeti(L, -1, j+1);
      str = lua_tolstring(L, -1, &len);
      if((var->params.array[j] = shuso_stalloc(&S->stalloc, len+1)) == NULL) {
        return shuso_set_error(S, "no memory for instring variable index");
      }
      strcpy((char *)var->params.array[j], str);
      lua_pop(L, 1);
    }
  }
  lua_pop(L, 1);
  

  lua_pushstring(L, var->name);
  lua_pushlightuserdata(L, var->block);
  
  if(constant) {
    shuso_str_t str;
    if(!var->eval(S, var, &str)) {
      return shuso_set_error(S, "constant variable $%s evaluation failed");
    }
    return instring_token_literal_lua_to_c(L, token, iov, index);
  }
  
  iov->iov_base = NULL;
  iov->iov_len = 0;
  return true;
}

static shuso_instring_t *luaS_instring_lua_to_c_generic(lua_State *L, shuso_setting_t *setting, int index, shuso_instring_t *preallocd_instring) {
  index = lua_absindex(L, index);
  int                  top = lua_gettop(L);
  shuso_t             *S = shuso_state(L);
  shuso_instring_t    *instring;
  
  luaL_checkstack(L, 3, NULL);
  luaS_push_lua_module_field(L, "shuttlesock.core.instring", "is_instring");
  lua_pushvalue(L, index);
  lua_call(L, 1, 1);
  if(!lua_toboolean(L, -1)) {
    lua_settop(L, top);
    shuso_set_error(S, "not an instring object");
    return NULL;
  }
  lua_pop(L, 1);
  
  lua_getfield(L, index, "ptr");
  if(lua_islightuserdata(L, 1)) {
    instring = (void *)lua_topointer(L, -1);
    lua_settop(L, top);
    return instring;
  }
  lua_pop(L, 1);
  
  luaL_checkstack(L, 6, NULL);
  lua_getfield(L, index, "tokens");
  int token_count = luaL_len(L, -1);
  
  int var_count = 0;
  
  bool literal = true;
  int  i;
  
  if(preallocd_instring) {
    instring = preallocd_instring;
  }
  else if((instring = shuso_stalloc(&S->stalloc, sizeof(*instring))) == NULL) {
    lua_settop(L, top);
    shuso_set_error(S, "no memory for instring");
    return NULL;
  }
  
  instring->cached_value.reset_state = (shuso_instring_value_state_t){
    .boolean = SHUTTLESOCK_INSTRING_VALUE_UNKNOWN,
    .integer = SHUTTLESOCK_INSTRING_VALUE_UNKNOWN,
    .number = SHUTTLESOCK_INSTRING_VALUE_UNKNOWN, 
    .size = SHUTTLESOCK_INSTRING_VALUE_UNKNOWN, 
    .string = SHUTTLESOCK_INSTRING_VALUE_UNKNOWN
  };
  instring->cached_value.state = instring->cached_value.reset_state;
  
  shuso_buffer_init(S, &instring->buffer.head, SHUSO_BUF_EXTERNAL, NULL);
  
  instring->buffer.iov = shuso_stalloc(&S->stalloc, sizeof(struct iovec) * token_count);
  if(!instring->buffer.iov) {
    lua_settop(L, top);
    shuso_set_error(S, "no memory for instring iovec");
    return NULL;
  }
  shuso_buffer_link_init(S, &instring->buffer.link, instring->buffer.iov, token_count);
  
  shuso_instring_token_t *token = shuso_stalloc(&S->stalloc, sizeof(*token)*token_count);
  if(!token) {
    lua_settop(L, top);
    shuso_set_error(S, "no memory for instring token");
    return NULL;
  }
  
  instring->tokens.count = token_count;
  for(i = 0; i< token_count; i++) {
    lua_rawgeti(L, -1, i+1);
    
    
    lua_getfield(L, -1, "type");
    bool is_variable = luaS_streq_literal(L, -1, "variable");
    if(!is_variable) {
      assert(luaS_streq_literal(L, -1, "literal"));
    }
    lua_pop(L, 1); //pop .type
    
    if(!is_variable) {
      if(!instring_token_literal_lua_to_c(L, &token[i], &instring->buffer.iov[i], -1)) {
        lua_settop(L, top);
        return false;
      }
    }
    else {
      if(!instring_token_variable_lua_to_c(L, setting, &token[i], &instring->buffer.iov[i], -1)) {
        lua_settop(L, top);
        return false;
      }
    }
    lua_pop(L, 1); //pop [i+1]
  }
  
  lua_pop(L, 1); //pop ["tokens"]
  instring->variables.count = var_count;
  if(var_count > 0) {
    shuso_variable_t **vars = shuso_stalloc(&S->stalloc, sizeof(*vars) * var_count);
    int j = 0;
    for(i = 0; i< token_count; i++) {
      if(token[i].type == SHUSO_INSTRING_TOKEN_VARIABLE) {
        vars[j++] = &token[i].variable;
      }
    }
    instring->variables.array = vars;
  }
  else {
    instring->variables.array = NULL;
  }
    
  instring->tokens.array = token;
  
  if(literal) {
    assert(instring->variables.count == 0);
    assert(instring->tokens.count == 1);
    
    //interpolate that instring right now!
    
    //manually set the string value
    shuso_str_t *literal_str = &instring->tokens.array[0].literal;
    instring->buffer.iov[0].iov_base = literal_str->data;
    instring->buffer.iov[0].iov_len = literal_str->len;
    instring->cached_value.string = *literal_str;
    instring->cached_value.state.string = SHUTTLESOCK_INSTRING_VALUE_VALID;
    
    shuso_instring_boolean_value(S, instring, NULL);
    shuso_instring_integer_value(S, instring, NULL);
    shuso_instring_number_value(S, instring, NULL);
    shuso_instring_size_value(S, instring, NULL);
  }
  assert(top == lua_gettop(L));
  
  return instring;
}


shuso_instring_t *luaS_instring_lua_to_c(lua_State *L, shuso_setting_t *setting, int index) {
  return luaS_instring_lua_to_c_generic(L, setting, index, NULL);
}

shuso_instrings_t *luaS_instrings_lua_to_c(lua_State *L, shuso_setting_t *setting, int index) {
  shuso_t *S = shuso_state(L);
  int top = lua_gettop(L);
  index = lua_absindex(L, index);
  size_t instring_count = luaL_len(L, index);
  shuso_instrings_t *instrings = shuso_stalloc(&S->stalloc, sizeof(*instrings) + sizeof(*instrings->array) * instring_count);
  if(instrings == NULL) {
    shuso_set_error(S, "no memory for instrings array");
    return NULL;
  }
  
  instrings->count = instring_count;
  for(unsigned i=0; i<instring_count; i++) {
    lua_geti(L, index, i+1);
    if(luaS_instring_lua_to_c_generic(L, setting, -1, &instrings->array[i]) == NULL) {
      return NULL;
    }
    lua_pop(L, 1);
  }
  assert(top == lua_gettop(L));
  return instrings;
}

static void shuso_instring_interpolate(shuso_t *S, shuso_instring_t *instring) {
  int varcount = instring->variables.count;
  
  if(varcount == 0) {
    return; //nothing to do
  }
  
  bool reset = false;
  for(int i=0; i < varcount; i++) {
    shuso_variable_t *var = instring->variables.array[i];
    shuso_str_t       val;
    if(var->eval(S, var, &val)) {
      reset = true;
      instring->buffer.iov[i] = (struct iovec) {
        .iov_base = val.data,
        .iov_len = val.len
      };
    }
  }
  
  if(reset) {
    if(instring->cached_value.string_lua_ref != LUA_NOREF) {
      luaL_unref(S->lua.state, LUA_REGISTRYINDEX, instring->cached_value.string_lua_ref);
      instring->cached_value.string_lua_ref = LUA_NOREF;
    }
    instring->cached_value.state = instring->cached_value.reset_state;
  }
}

static void luaS_push_iovec_as_string(lua_State *L, struct iovec *iov, size_t iovlen) {
  luaL_Buffer buf;
  luaL_buffinit(L, &buf);
  for(unsigned i=0; i<iovlen; i++) {
    luaL_addlstring(&buf, iov[i].iov_base, iov[i].iov_len);
  }
  luaL_pushresult(&buf);
}

static bool luaS_instring_tovalue(lua_State *L, shuso_instring_t *instring, const char *towhat) {
  luaS_push_lua_module_field(L, "shuttlesock.core.instring", towhat);
  luaS_push_iovec_as_string(L, instring->buffer.iov, instring->tokens.count);
  lua_call(L, 1, 1);
  if(lua_isnil(L, -1)) {
    lua_pop(L, 1);
    return false;
  }
  return true;
}

bool shuso_instring_boolean_value(shuso_t *S, shuso_instring_t *instring, bool *retval) {
  shuso_instring_interpolate(S, instring);
  switch(instring->cached_value.state.boolean) {
    case SHUTTLESOCK_INSTRING_VALUE_VALID:
      if(retval) {
        *retval = instring->cached_value.boolean;
      }
      return true;
    case SHUTTLESOCK_INSTRING_VALUE_INVALID:
      return false;
  }
  //quick 'n' dirty conversion using Lua
  if(!luaS_instring_tovalue(S->lua.state, instring, "toboolean")) {
    instring->cached_value.state.boolean = SHUTTLESOCK_INSTRING_VALUE_INVALID;
    return false;
  }
  instring->cached_value.state.boolean = SHUTTLESOCK_INSTRING_VALUE_VALID;
  lua_State *L = S->lua.state;
  bool ret = lua_toboolean(L, -1);
  instring->cached_value.boolean = ret;
  lua_pop(L, 1);
  if(retval) {
    *retval = ret;
  }
  return true;
}

bool shuso_instring_integer_value(shuso_t *S, shuso_instring_t *instring, int *retval) {
  shuso_instring_interpolate(S, instring);
  switch(instring->cached_value.state.integer) {
    case SHUTTLESOCK_INSTRING_VALUE_VALID:
      if(retval) {
        *retval = instring->cached_value.integer;
      }
      return true;
    case SHUTTLESOCK_INSTRING_VALUE_INVALID:
      return false;
  }
  //quick 'n' dirty conversion using Lua
  if(!luaS_instring_tovalue(S->lua.state, instring, "tointeger")) {
    instring->cached_value.state.integer = SHUTTLESOCK_INSTRING_VALUE_INVALID;
    return false;
  }
  instring->cached_value.state.integer = SHUTTLESOCK_INSTRING_VALUE_VALID;
  lua_State *L = S->lua.state;
  int ret = lua_tointeger(L, -1);
  instring->cached_value.integer = ret;
  lua_pop(L, 1);
  if(retval) {
    *retval = ret;
  }
  return true;
}

bool shuso_instring_number_value(shuso_t *S, shuso_instring_t *instring, double *retval) {
  shuso_instring_interpolate(S, instring);
  switch(instring->cached_value.state.number) {
    case SHUTTLESOCK_INSTRING_VALUE_VALID:
      if(retval) {
        *retval = instring->cached_value.number;
      }
      return true;
    case SHUTTLESOCK_INSTRING_VALUE_INVALID:
      return false;
  }
  //quick 'n' dirty conversion using Lua
  if(!luaS_instring_tovalue(S->lua.state, instring, "tonumber")) {
    instring->cached_value.state.number = SHUTTLESOCK_INSTRING_VALUE_INVALID;
    return false;
  }
  instring->cached_value.state.number = SHUTTLESOCK_INSTRING_VALUE_VALID;
  lua_State *L = S->lua.state;
  double ret = lua_tonumber(L, -1);
  instring->cached_value.number = ret;
  lua_pop(L, 1);
  if(retval) {
    *retval = ret;
  }
  return true;
}

bool shuso_instring_size_value(shuso_t *S, shuso_instring_t *instring, size_t *retval) {
  shuso_instring_interpolate(S, instring);
  switch(instring->cached_value.state.size) {
    case SHUTTLESOCK_INSTRING_VALUE_VALID:
      if(retval) {
        *retval = instring->cached_value.size;
      }
      return true;
    case SHUTTLESOCK_INSTRING_VALUE_INVALID:
      return false;
  }
  //quick 'n' dirty conversion using Lua
  if(!luaS_instring_tovalue(S->lua.state, instring, "tosize")) {
    instring->cached_value.state.size = SHUTTLESOCK_INSTRING_VALUE_INVALID;
    return false;
  }
  instring->cached_value.state.size = SHUTTLESOCK_INSTRING_VALUE_VALID;
  lua_State *L = S->lua.state;
  double ret = lua_tointeger(L, -1);
  instring->cached_value.size = ret;
  lua_pop(L, 1);
  if(retval) {
    *retval = ret;
  }
  return true;
}

bool shuso_instring_string_value(shuso_t *S, shuso_instring_t *instring, shuso_str_t *retval) {
  shuso_instring_interpolate(S, instring);
  switch(instring->cached_value.state.string) {
    case SHUTTLESOCK_INSTRING_VALUE_VALID:
      if(retval) {
        *retval = instring->cached_value.string;
      }
      return true;
    case SHUTTLESOCK_INSTRING_VALUE_INVALID:
      return false;
  }
  //quick 'n' dirty conversion using Lua
  if(!luaS_instring_tovalue(S->lua.state, instring, "tostring")) {
    instring->cached_value.state.string = SHUTTLESOCK_INSTRING_VALUE_INVALID;
    return false;
  }
  instring->cached_value.state.string = SHUTTLESOCK_INSTRING_VALUE_VALID;
  lua_State *L = S->lua.state;
  if(retval) {
    instring->cached_value.string.data = (char *)lua_tolstring(L, -1, &instring->cached_value.string.len);
    *retval = instring->cached_value.string;
  }
  assert(instring->cached_value.string_lua_ref == LUA_NOREF);
  instring->cached_value.string_lua_ref = luaL_ref(L, -1);
  return true;
}

bool shuso_instring_buffer_value(shuso_t *S, shuso_instring_t *instring, shuso_buffer_t **retval) {
  shuso_instring_interpolate(S, instring);
  *retval = &instring->buffer.head;
  return true;
}

