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
  return parser:parse()
end

function Parser.loadString(string, name)
  local parser = Parser.new(string)
  return parser:parse()
end

local chunk_mt = {__index = {
  type = "chunk",
  setBlock = function(self, block)
    assert(not self.block)
    self.block = block
    return true
  end,
  
  addValue = function(self, val)
    assert(type(val) == "table")
    if not self.position.value_first then
      self.position.value_first = val.position.first
    end
    self.position.value_last = val.position.last
    table.insert(self.values, val)
    return true
  end
}}
local block_mt = {__index = {
  type = "block",
  addChunk = function(self, chunk)
    table.insert(self.chunks, chunk)
    return true
  end
}}

parser_mt = {__index = {
  
  error = function(self, err)
    if err then
      self.errmsg = err
      error(err)
      return nil, err
    else
      return self.errmsg
    end
  end,
  
  inChunk = function(self)
    return self.top.type == "chunk"
  end,
  inBlock = function(self)
    return self.top.type == "block"
  end,
  
  pushChunk = function(self, chunk_name)
    assert(self:inBlock())
    local chunk = {
      name = chunk_name,
      position = {
        first = self.cur,
        last = self.cur,
        value_first = self.cur,
        value_last = self.cur
      },
      values = {},
      block = nil,
    }
    setmetatable(chunk, chunk_mt)
    table.insert(self.stack, chunk)
    self.top = chunk
    return true
  end,
  
  popChunk = function(self)
    assert(self:inChunk())
    local chunk = self.top
    chunk.position.last = self.cur
    
    table.remove(self.stack)
    self.top = self.stack[#self.stack]
    assert(self:inBlock())
    self.top:addChunk(chunk)
    return true
  end,
  
  addValueToChunk = function(self, value_type, val)
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
    
    assert(self:inChunk())
    self.top:addValue(value)
    return true
  end,
  
  pushBlock = function(self, block_name)
    local block = {
      name = block_name,
      position = {
        first = self.cur,
        last = self.cur
      },
      chunks = {}
    }
    setmetatable(block, block_mt)
    table.insert(self.stack, block)
    self.top = block
    return true
  end,
  
  popBlock = function(self)
    local block = self.top
    table.remove(self.stack)
    self.top = self.stack[#self.stack]
    assert(self:inChunk())
    self.top:setBlock(block)
    
    --finishing a block means we are also finishing the chunk it belongs to
    if block.name ~= "ROOT" then
      self:popChunk()
    end
    return true
  end,

  parse = function(self)
    local token_count = 0
    for _ in self:eachToken() do
      token_count = token_count + 1
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
  
  skipSpace = function(self)
    return self:match("^[ \t\r\n]+", "last_space")
  end,
  skipComment = function(self)
    return self:match("^#[^\n]+", "last_comment")
  end,
  
  matchString = function(self)
    local quote_char = assert(self:match("^['\"]", false))
    local unquote
    local offset = 1
    repeat
      unquote = self:find(quote_char, true, offset)
      if unquote then
        offset = unquote - self.cur + 1
        if countSlashesReverse(self.str, self.cur, unquote - 1) % 2 == 0 then
          --string end found
          self.last_match = self.str:sub(self.cur+1, unquote)
          self.cur = unquote + 1
          return self:addValueToChunk("string")
        end
      end
    until not unquote
    return nil, "unterminated string"
  end,
  
  matchVariable = function(self)
    local var = self:match("^%$([%w%.%_]+)")
    if var then
      return self:addValueToChunk("variable")
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
    return self:addValueToChunk("value")
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
        local directive_name = self:match("^[%w%._]+")
        if not directive_name then
          self:error("expected config directive name", "^%S+")
          return nil
        end
        self:pushChunk(directive_name)
      end
      return true
    end
    
    assert(self:inChunk())
    local char = self:match("^.", false)
    local ok, err
    if char == "\"" or char == "'" then
      self.cur = self.cur - 1
      ok, err = self:matchString()
    elseif char == "$" then
      self.cur = self.cur - 1
      ok, err = self:matchVariable()
    elseif char == "{" then
      ok, err = self:pushBlock()
    elseif char == ";" then
      ok, err = self:popChunk()
    else
      self.cur = self.cur - 1
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
    local current_line_start = -1
    pos = pos or self.cur
    if pos < 0 then
      pos = #self.str - pos
    end
    while true do
      local nl = self.str:find("\n", cur, true)
      if not nl then --no more newlines
        break
      elseif cur <= pos then --newline in-range
        line = line + 1
        current_line_start = pos
        cur = pos + 1
      else -- we've gone past last
        break
      end
    end
    return line, pos - current_line_start + 1
  end,
}}

return Parser
