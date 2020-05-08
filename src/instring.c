#include <shuttlesock.h>

shuso_instring_t *luaS_instring_lua_to_c(lua_State *L, int index) {
  shuso_t *S = shuso_state(L);
  int token_count = luaL_len(L, index);
  int top = lua_gettop(L);
  int var_count = 0;
  
  bool literal = true;
  int  i;
  luaL_checkstack(L, 3, NULL);
  
  shuso_instring_t *shu;
  shuso_instring_token_t *token = shuso_stalloc(&S->stalloc, sizeof(*token)*token_count);
  if(!token) {
    shuso_set_error(S, "no memory for instring token");
    return NULL;
  }
  
  size_t      len;
  const char *str;
  
  shu->tokens.count = token_count;
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
      int indices_count = luaL_len(L, -1);\
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
    }
  }
  
  if((shu = shuso_stalloc(&S->stalloc, sizeof(*shu))) == NULL) {
    shuso_set_error(S, "no memory for instring");
    return NULL;
  }
  
  shu->variables.count = var_count;
  if(var_count > 0) {
    shuso_variable_t **vars = shuso_stalloc(&S->stalloc, sizeof(*vars) * var_count);
    int j = 0;
    for(i = 0; i< token_count; i++) {
      if(token[i].type == SHUSO_INSTRING_TOKEN_VARIABLE) {
        vars[j++] = &token[i].variable;
      }
    }
    shu->variables.array = vars;
  }
  else {
    shu->variables.array = NULL;
  }
    
  shu->tokens.array = token;
  
  if(literal) {
    assert(shu->variables.count == 0);
    assert(shu->tokens.count == 1);
    //TODO interpolate that instring right now!
  }
  
  assert(top == lua_gettop(L));
  
  return shu;
}

void shuso_instring_interpolate(shuso_t *S, shuso_instring_t *str, shuso_str_t *out) {
  
}
