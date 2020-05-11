#include <shuttlesock.h>

static shuso_instring_t *luaS_instring_lua_to_c_generic(lua_State *L, int index, shuso_instring_t *preallocd_instring) {
  index = lua_absindex(L, index);
  
  shuso_t             *S = shuso_state(L);
  shuso_instring_t    *instring;
  
  int token_count = luaL_len(L, index);
  int top = lua_gettop(L);
  int var_count = 0;
  
  bool literal = true;
  int  i;
  luaL_checkstack(L, 3, NULL);
  
  if(preallocd_instring) {
    instring = preallocd_instring;
  }
  else if((instring = shuso_stalloc(&S->stalloc, sizeof(*instring))) == NULL) {
    shuso_set_error(S, "no memory for instring");
    return NULL;
  }
  
  shuso_buffer_init(S, &instring->buffer.head, SHUSO_BUF_EXTERNAL, NULL);
  
  instring->buffer.iov = shuso_stalloc(&S->stalloc, sizeof(struct iovec) * token_count);
  if(!instring->buffer.iov) {
    shuso_set_error(S, "no memory for instring iovec");
    return NULL;
  }
  shuso_buffer_link_init(S, &instring->buffer.link, instring->buffer.iov, token_count);
  
  shuso_instring_token_t *token = shuso_stalloc(&S->stalloc, sizeof(*token)*token_count);
  if(!token) {
    shuso_set_error(S, "no memory for instring token");
    return NULL;
  }
  
  size_t      len;
  const char *str;
  
  instring->tokens.count = token_count;
  for(i = 0; i< token_count; i++) {
    lua_rawgeti(L, index, i+1);
    lua_getfield(L, -1, "type");
    if(luaS_streq_literal(L, -1, "literal")) {
      token[i].type = SHUSO_INSTRING_TOKEN_LITERAL;
      lua_getfield(L, -2, "value");
      str = lua_tolstring(L, -1, &len);
      token[i].literal.len = len;
      token[i].literal.data = (char *)shuso_stalloc(&S->stalloc, len+1);
      if(!token[i].literal.data) {
        shuso_set_error(S, "no memory for instring literal");
        return NULL;
      }
      memcpy(token[i].literal.data, lua_tostring(L, -1), len + 1);
      
      instring->buffer.iov[i].iov_base = token[i].literal.data;
      instring->buffer.iov[i].iov_len = len;
      
      lua_pop(L, 1);
    }
    else if(luaS_streq_literal(L, -1, "variable")) {
      var_count++;
      literal = false;
      
      lua_getfield(L, -2, "module_ptr");
      token[i].variable.module = lua_topointer(L, -1);
      assert(token[i].variable.module);
      lua_pop(L, 1);
      
      lua_getfield(L, -2, "name");
      str = lua_tolstring(L, -1, &len);
      token[i].variable.name = shuso_stalloc(&S->stalloc, len+1);
      if(!token[i].variable.name) {
        shuso_set_error(S, "no memory for instring variable name");
        return NULL;
      }
      strcpy((char *)token[i].variable.name, str);
      lua_pop(L, 1);
      
      lua_getfield(L, -2, "indices");
      int indices_count = luaL_len(L, -1);
      token[i].variable.indices.size = indices_count;
      if(indices_count == 0) {
        token[i].variable.indices.size = 0;
      }
      else {
        if((token[i].variable.indices.array = shuso_stalloc(&S->stalloc, sizeof(*token[i].variable.indices.array))) == NULL) {
          shuso_set_error(S, "no memory for instring variable index array");
          return NULL;
        }
        for(int j = 0; j < indices_count; j++) {
          lua_rawgeti(L, -1, j+1);
          str = lua_tolstring(L, -1, &len);
          if((token[i].variable.indices.array[j] = shuso_stalloc(&S->stalloc, len+1)) == NULL) {
            shuso_set_error(S, "no memory for instring variable index");
            return NULL;
          }
          strcpy((char *)token[i].variable.indices.array[j], str);
          lua_pop(L, 1);
        }
      }
      lua_pop(L, 2);
      
      instring->buffer.iov[i].iov_base = NULL;
      instring->buffer.iov[i].iov_len = 0;
    }
  }
  
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
    //TODO interpolate that instring right now!
  }
  
  assert(top == lua_gettop(L));
  
  return instring;
}


shuso_instring_t *luaS_instring_lua_to_c(lua_State *L, int index) {
  return luaS_instring_lua_to_c_generic(L, index, NULL);
}

shuso_instrings_t *luaS_instrings_lua_to_c(lua_State *L, int index) {
  shuso_t *S = shuso_state(L);
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
    if(luaS_instring_lua_to_c_generic(L, -1, &instrings->array[i]) == NULL) {
      return NULL;
    }
  }
  
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
  luaS_push_iovec_as_string(L, instring->buffer.iov, instring->tokens.count);
  luaS_push_lua_module_field(L, "shuttlesock.core.instring", towhat);
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
  double ret = lua_tointeger(L, -1);
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

