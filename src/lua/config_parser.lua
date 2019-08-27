local mm = require "mm"
local Parser = {}
local parser_mt

local function countSlashesReverse(str, start, cur)
  local count = 0
  local rslash = string.byte("\\")
  for i=cur,start,-1 do
    if str:byte(i) == rslash then
      count= count + 1
    else
      return count
    end
  end
  return count
end

local function indent(lvl)
  return ("  "):rep(lvl)
end

function Parser.new(string)
  local self = {
    str = string,
    cur = 1,
    blocklevel = 0,
    stack = {},
    top = nil
  }
  setmetatable(self, parser_mt)
  
  self:pushBlock("ROOT")
  return self
end

function Parser.loadFile(path)
  local f, err = io.open(path, "rb")
  if not f then
    return nil, err
  end
  local str = f:read("*all")
  f:close()
  local parser = Parser.new(str)
  parser:parse()
  return parser
end

function Parser.loadString(string, name)
  local parser = Parser.new(string)
  parser:parse()
  return parser
end

local chunk_mt = {__index = {
  addDirective = function(self, directive)
    assert(self.type == "block")
    table.insert(self.directives, directive)
    table.insert(self.tokens, directive)
    return true
  end,
  
  setBlock = function(self, block)
    assert(self.type == "directive")
    assert(not self.block)
    self.block = block
    return true
  end,
  
  addValue = function(self, val)
    assert(self.type == "directive")
    assert(type(val) == "table")
    if not self.position.values_first then
      self.position.values_first = val.position.first
    end
    self.position.values_last = val.position.last
    table.insert(self.values, val)
    table.insert(self.tokens, val)
    return true
  end,
  
  addNewline = function(self, pos)
    table.insert(self.tokens, {type="newline", position = pos, value = ""})
    return true
  end,
  addComment = function(self, comment, pos)
    assert(type(comment) == "string")
    assert(type(pos) == "number")
    local cmt = {type="comment", position = pos, value = comment}
    table.insert(self.comments, cmt)
    table.insert(self.tokens, cmt)
    return true
  end,
  addSemicolon = function(self, pos)
    assert(self.type == "directive")
    local semi = {type="semicolon", position = pos}
    table.insert(self.tokens, semi)
  end
}}

parser_mt = {__index = {
  
  error = function(self, err)
    if err then
      local line, column = self:location()
      self.errmsg = ("%s in %s:%d:%d"):format(err, "config", line, column)
      error(self.errmsg)
      return nil, err
    else
      return self.errmsg
    end
  end,
  
  inDirective = function(self)
    return self.top.type == "directive"
  end,
  inBlock = function(self)
    return self.top.type == "block"
  end,
  
  pushDirective = function(self, directive_name)
    assert(self:inBlock())
    local directive = {
      type = "directive",
      name = directive_name,
      position = {
        first = self.cur,
        last = self.cur,
        values_first = nil,
        values_last = nil
      },
      values = {},
      tokens = {},
      comments = {},
      block = nil,
    }
    setmetatable(directive, chunk_mt)
    table.insert(self.stack, directive)
    self.top = directive
    return true
  end,
  
  popDirective = function(self)
    assert(self:inDirective())
    local directive = self.top
    directive.position.last = self.cur
    
    table.remove(self.stack)
    self.top = self.stack[#self.stack]
    assert(self:inBlock())
    self.top:addDirective(directive)
    return true
  end,
  
  addValueToDirective = function(self, value_type, val)
    if val == nil then
      val = self:match() --last match
    end
    
    local value = {
      type = value_type,
      position = {
        first = self.cur - #val,
        last = self.cur
      },
      raw = val
    }
    
    assert(self:inDirective())
    self.top:addValue(value)
    return true
  end,
  
  pushBlock = function(self, block_name)
    local block = {
      type = "block",
      name = block_name,
      position = {
        first = self.cur,
        last = self.cur,
        directives_first = nil,
        directives_last = nil
      },
      directives = {},
      comments = {},
      tokens = {}
    }
    setmetatable(block, chunk_mt)
    table.insert(self.stack, block)
    self.top = block
    return true
  end,
  
  popBlock = function(self)
    local block = self.top
    table.remove(self.stack)
    self.top = self.stack[#self.stack]
    assert(self:inDirective())
    self.top:setBlock(block)
    
    --finishing a block means we are also finishing the directive it belongs to
    if block.name ~= "ROOT" then
      self:popDirective()
    end
    return true
  end,

  parse = function(self)
    local token_count = 0
    for _ in self:eachToken() do
      token_count = token_count + 1
    end
    
    if self.top.type == "directive" then
      local err = "unexpected end of file, expected \";\""
      self:error(err)
      return nil, err
    elseif self.top.type == "block" and self.top.name ~= "ROOT" then
      local err = "unexpected end of file, expected \"}\""
      self:error(err)
      return nil, err
    end
      
    return self.top
  end,
  
  match = function(self, pattern, save_match)
    if not pattern then
      --get last match
      return (self.last_match or {})
    else
      assert(pattern:sub(1, 1) == "^", "match must start with ^, but have \"" .. pattern .. "\"")
      local m = self.str:match(pattern, self.cur)
      if not m then
        return nil
      end
      if save_match == nil then
        save_match = "last_match"
      end
      if save_match then
        self[save_match] = m
      end
      self.cur = self.cur + #m
      -- if save_match is false, skip it
      return m
    end
  end,
  
  find = function(self, pattern, simple, offset)
    return self.str:find(pattern, self.cur + (offset or 0), simple)
  end,
  
  eachToken = function(self)
    --for loop iterator
    return function()
      repeat until not (self:skipSpace() or self:skipComment())
      local token = self:nextToken()
      return token
    end
  end,
  
  skipSpace = function(self, spacechars)
    spacechars = spacechars or " \t\r\n"
    local m = self:match("^["..spacechars.."]+", "last_space")
    if m and self:inDirective() then
      local nl = nil
      repeat
        nl = m:find("\n", nl)
        if nl then
          self.top:addNewline(self.cur - #m + nl - 1)
          nl = nl+1
        end
      until not nl
    end
    return m
  end,
  skipComment = function(self)
    local m = self:match("^#[^\n]+", "last_comment")
    if m then
      self.top:addComment(m, self.cur - #m)
    end
    return m
  end,
  
  matchString = function(self)
    local quote_char = self.str:sub(self.cur, self.cur)
    local unquote
    local offset = 1
    repeat
      unquote = self:find(quote_char, true, offset)
      if unquote then
        offset = unquote - self.cur + 1
        if countSlashesReverse(self.str, self.cur, unquote - 1) % 2 == 0 then
          --string end found
          self.last_match = self.str:sub(self.cur, unquote)
          self.cur = unquote + 1
          return self:addValueToDirective("string")
        end
      end
    until not unquote
    return nil, "unterminated string"
  end,
  
  matchVariable = function(self)
    local var = self:match("^%$([%w%.%_]+)")
    if var then
      return self:addValueToDirective("variable")
    end
    
    if self:match("^%$[^%s;]+") then
      self:error("invalid variable name " .. self:match())
      return nil
    elseif self:match("^%$") then
      self:error("empty variable name")
      return nil
    else
      self:error("invalid variable")
      return nil
    end
  end,
  
  matchValue = function(self)
    if not self:match("^[^%s;]+") then
      self:error("invalid value")
      return nil
    end
    return self:addValueToDirective("value")
  end,
  
  matchDirectiveName = function(self)
    if not self:match("^[%w%._]+") then
      if self:match("^;") then
        self:error("unexpected \";\"")
      elseif self:match("^{") then
        self:error("unexpected \"{\"")
      elseif self:match("^%S+") then
        self:error("invalid config directive name \"" .. self:match() .. "\"" , "^%S+")
      else
        self:error("expected config directive")
      end
      return nil
    end
    self:pushDirective(self:match())
    return true
  end,
  
  matchSemicolon = function(self)
    assert(self:match("^;"))
    self.top:addSemicolon(self.cur - 1)
    self:skipSpace(" \t")
    self:skipComment()
    return self:popDirective()
  end,
  
  printStack = function(self)
    for _, v in ipairs(self.stack) do
      print("  ", v.name, v.type)
    end
  end,
  
  nextToken = function(self)
    if self.cur >= #self.str then
      return nil --we're done
    end
    if self:inBlock() then
      if self:match("^}") then
        return self:popBlock()
      else
        local ok, err = self:matchDirectiveName()
        if not ok then
          self:error(err)
          return nil
        end
      end
      return true
    end
    
    assert(self:inDirective())
    local char = self.str:sub(self.cur, self.cur)
    local ok, err
    if char == "\"" or char == "'" then
      ok, err = self:matchString()
    elseif char == "$" then
      ok, err = self:matchVariable()
    elseif char == "{" then
      self.cur = self.cur + 1
      ok, err = self:pushBlock()
    elseif char == "}" then
      err = "unexpected \"}\""
    elseif char == ";" then
      ok, err = self:matchSemicolon()
    else
      ok, err = self:matchValue()
    end
    
    if not ok then
      self:error(err)
      return nil
    end
    return true
  end,
  
  location = function(self, pos)
    local line = 1
    local cur = 1
    pos = pos or self.cur
    if pos < 0 then
      pos = #self.str - pos
    end
    while true do
      local nl = self.str:find("\n", cur, true)
      if not nl then --no more newlines
        print "no more newlines"
        break
      elseif nl < pos then --newline in-range
        line = line + 1
        cur = nl + 1 
      else -- we've gone past last
        break
      end
    end
    return line, pos - cur
  end,
  
  debugString = function(self, cur, lvl)
    lvl = lvl or 0
    cur = cur or self.top
    local buf = {}
    if cur.type == "block" then
      for _, v in ipairs(cur.tokens) do
        table.insert(buf, self:debugString(v, lvl))
      end
      if cur.name == "ROOT" then
        return table.concat(buf, "\n")
      else
        return ("{\n%s\n%s}\n"):format(table.concat(buf, "\n"), indent(lvl-1))
      end
    elseif cur.type == "directive" then
      local str = indent(lvl) .. cur.name
      local prev_type = "none"
      for i, token in ipairs(cur.tokens) do
        local pre = prev_type == "newline" and indent(lvl+1) or " "
        
        if token.type == "newline" then
          str = str .. "\n"
        elseif token.type == "comment" then
          str = str .. pre .. token.value
        elseif token.type == "value" or token.type == "string" or token.type == "variable" then
          str = str .. pre .. "<"..token.raw..">"
        elseif token.type == "semicolon" then
          str = str .. (prev_type == "newline" and indent(lvl) or "")..";"
        else
          error("unexpected token type " .. token.type)
        end
        
        prev_type = token.type
      end
      
      if cur.block then
        str = str .. " " .. self:debugString(cur.block, lvl + 1)
        return str
      else
        return str
      end
    elseif cur.type == "comment" then
      return indent(lvl)..cur.value
    else
      error("unexpected chunk type " .. cur.type)
    end
  end
}}

return Parser
