local Parser = {}
local parser_mt

local Config = {}
local config_mt


local function mm_setting(setting)
  local mm = require "mm"
  local cpy = {}
  for k,v in pairs(setting) do
    cpy[k]=v
  end
  cpy.parent = setting.parent and setting.parent.name or "--none--"
  cpy.block = "..."
  mm(cpy)
end

local DEBUG_MODE = false
local assert, error = assert, error
if not DEBUG_MODE then
  local __original_error = error
  error = function(err, n)
    __original_error(err, n or 0)
  end

  assert = function(cond, err)
    if not cond then
      error(err, 0)
    else
      return cond, err
    end
  end
end


local function resolve_path(prefix, name)
  if not prefix or name:match("^%/") then
    --absolute path or no path given
    return name
  else
    prefix = prefix:match("^(.*)%/$") or prefix --strip trailing slash
    return prefix .. "/" .. name
  end
end

local function glob(globstr)
  local system = require "shuttlesock.core.system"
  return system.glob(globstr)
end

local config_settings = {
  {
    name = "lua_path",
    path = "/",
    description = "path to all the internal lua stuff",
    nargs = "1..10",
    default = {".", "/usr/lib/shuttlesock/lua"},
    handler = function(setting, default)
      local values = setting.values or default
      setting.parent.config_lua_path = values[1]
      return true
    end
  },
  {
    name = "include_path",
    path = "",
    description = "sets path for the 'include' setting",
    nargs = 1,
    handler = function(setting)
      return true
    end
  },
  {
    name    = "include",
    path    = "",
    description = "include configs matching the provided glob pattern",
    nargs   = 1,
    default = nil,
    internal_handler = function(setting, default, config)
      local path = setting.values[1].value.string
      local include_path = config:find_setting("include_path", setting.parent)
      local paths
      if path:match("[%[%]%?%*]") then
        paths = glob(resolve_path(include_path, setting.values[1]))
      else
        paths = {path}
      end
      
      local tokens = {
        {type="comment", value = "#include " .. path ..";"}
      }
      local settings = { }
      
      config.config_include_stack = config.config_include_stack or {}
      table.insert(config.config_include_stack, config.name)
      if #config.config_include_stack > 32 then
        return nil, "too many include levels, there's probably an include loop : \n" ..
          "    loaded ".. table.concat(config.config_include_stack, "\n  included ")
      end
      
      for _, p in ipairs(paths) do
        local included_config = Config.new(p)
        included_config.config_include_stack = config.config_include_stack
        assert(included_config:load())
        assert(included_config:parse())
        
        table.insert(tokens, {type="comment", value = "# included file " .. p ..";"})
        
        for _, d in ipairs(included_config.root.block.settings) do
          table.insert(settings, d)
          d.parent = setting.parent
        end
        
        for _, t in ipairs(included_config.root.block.tokens) do
          table.insert(tokens, t)
        end
      end
      
      assert(config:replace_token(setting, table.unpack(tokens)))
      assert(config:replace_setting(setting, table.unpack(settings)))
      
      config.config_include_stack = nil
      
      return true
    end
  }
}

function Parser.new(string, opt)
  opt = opt or {}
  assert(type(string) == "string")
  assert(type(opt) == "table")
  local self = {
    name = opt.name or "(unnamed)",
    str = string,
    cur = 1,
    blocklevel = 0,
    stack = {},
    top = nil,
    root = nil
  }
  setmetatable(self, parser_mt)
  if not opt.root then
    self:push_setting("::ROOT", "config", true)
    self:push_block("::ROOT")
  else
    self:push_setting(opt.root, nil, true)
    self:push_block(opt.root.block)
  end
  if opt.file then
    self.is_file = true
  end
  self.root = self.stack[1]
  return self
end

do --parser
  local function count_slashes_reverse(str, start, cur)
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
  
  local parser = {}
  
  parser_mt = {
    __index = parser,
    __gxcopy_metatable = function()
      return require("shuttlesock.core.config").parser_metatable
    end
  }
  
  function parser:error(err, offset)
    if err then
      local line, column = self:location(offset)
      self.errmsg = ("%s in %s:%d:%d"):format(err, self.name or "config", line, column)
    end
    return self.errmsg
  end
  function parser:last_error()
    return self.errmsg
  end
  
  function parser:in_chunk_type(chunk_type, stack_position)
    assert(type(chunk_type) == "string")
    stack_position = stack_position or 0
    assert(type(stack_position) == "number" and stack_position <= 0)
    local chunk = self.stack[#self.stack + stack_position]
    return chunk.type == chunk_type and chunk
  end
  
  function parser:in_setting(stack_position)
    return self:in_chunk_type("setting", stack_position)
  end
  function parser:in_block(stack_position)
    return self:in_chunk_type("block", stack_position)
  end
  
  function parser:push_setting(setting_name, module_name, is_root)
    if is_root then
      assert(self.top == nil)
      assert(#self.stack == 0)
    else
      assert(self:in_block())
      assert(self:in_setting(-1))
    end
    
    local setting
    if type(setting_name) == "table" and not module_name then
      setting = setting_name
    elseif type(setting_name) == "string" then
      setting = {
        type = "setting",
        name = setting_name,
        module = module_name,
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
      if module_name then
        setting.full_name = setting.module .. ":" .. setting.name
      end
      if is_root and not setting.parent then
        setting.parent = setting --i'm my own grandpa
      else
        setting.parent = self.stack[#self.stack - 1]
      end
    else
      error("tried pushing invalid setting onto parser stack")
    end
    
    table.insert(self.stack, setting)
    self.top = setting
    return setting
  end
  
  function parser:pop_setting()
    local setting = assert(self:in_setting())
    setting.position.last = self.cur
    
    table.remove(self.stack)
    self.top = self.stack[#self.stack]
    if self.top == nil then
      assert(setting == self.root)
    else
      local block = assert(self:in_block())
      table.insert(block.settings, setting)
      table.insert(block.tokens, setting)
    end
    return true
  end
  
  function parser:add_newline(pos)
    local newline = {type="newline", position = pos, value = ""}
    table.insert(self.top.tokens, newline)
    return newline
  end
  
  function parser:add_comment(comment, pos)
    assert(type(comment) == "string")
    assert(type(pos) == "number")
    local chunk = self.top
    local cmt = {type="comment", position = pos, value = comment}
    table.insert(chunk.comments, cmt)
    table.insert(chunk.tokens, cmt)
    return cmt
  end
  
  function parser:add_semicolon(pos)
    local setting = assert(self:in_setting())
    local semi = {type="semicolon", position = pos}
    table.insert(setting.tokens, semi)
    return semi
  end
  
  function parser:add_value_to_setting(value_type, val, opt)
    if val == nil then
      val = self:match() --last match
    end
    
    local value = {
      type = value_type,
      position = {
        first = self.cur - #val,
        last = self.cur
      },
      value = {
        raw = val,
      }
    }
    if type(val) == "string" and opt and opt.quote_char then
      --unquote value
      val = val:match(("^%s(.*)%s$"):format(opt.quote_char, opt.quote_char))
    end
    
    value.value.string = tostring(val)
    value.value.number = tonumber(val)
    value.value.integer = math.tointeger(value.value.number)
    
    local boolies = {
      off=false,
      no=false,
      ["0"]=false,
      ['false']=false,
      
      on=true,
      yes=true,
      ['1']=true,
      ['true']=true
    }
    value.value.boolean = boolies[val]
    
    local setting = assert(self:in_setting())
    if not setting.position.values_first then
      setting.position.values_first = value.position.first
    end
    setting.position.values_last = value.position.last
    table.insert(setting.values, value)
    table.insert(setting.tokens, value)
    return value
  end
  
  function parser:push_block(block_name)
    local block
    if type(block_name) == "string" or not block_name then
      block = {
        type = "block",
        name = block_name,
        position = {
          first = self.cur,
          last = self.cur,
          settings_first = nil,
          settings_last = nil
        },
        settings = {},
        comments = {},
        tokens = {},
        source_setting = assert(self:in_setting())
      }
    elseif type(block_name) == "table" and block_name.type == "block" then
      block = block_name
    else
      error("tried pushing an invalid block onto the parser stack")
    end
    table.insert(self.stack, block)
    self.top = block
    return block
  end
  
  function parser:pop_block()
    assert(self:in_block(), "tried popping a block while not in block")
    local block = self.top
    table.remove(self.stack)
    self.top = self.stack[#self.stack]
    local setting = assert(self:in_setting(), "popped a block and ended not not in setting")
    if not setting.block then
      setting.block = block
      block.setting = setting
    else
      assert(setting.block == block, "setting.block doesn't match block")
      assert(block.setting == setting, "block.setting doesn't match setting")
    end
    --finishing a block means we are also finishing the setting it belongs to
    self:pop_setting()
    return block
  end

  function parser:parse()
    local root_top = self.top
    local token_count = 0
    for _ in self:parse_each_token() do
      token_count = token_count + 1
    end
    self.token_count = token_count
  
    if self:error() then
      return nil, self:error()
    end
    local unexpected_end
    
    if self.top ~= root_top then
      require "shuttlesock.core".raise_SIGABRT()
      if self:in_setting() then
        unexpected_end = ";"
      elseif self:in_block()then
        unexpected_end = "}"
      end
    end
    
    if unexpected_end then
      return nil, self:error(("unexpected end of config %s, expected \"%s\""):format(self.is_file and "file" or "string", unexpected_end), self.top)
    end
    
    self:pop_block() --pop root block
    return self.root
  end
  
  function parser:match(pattern, save_match)
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
  end
  
  function parser:find(pattern, simple, offset)
    return self.str:find(pattern, self.cur + (offset or 0), simple)
  end
  
  function parser:parse_each_token()
    --for loop iterator
    return function()
      repeat until not (self:skip_space() or self:skip_comment())
      local token, err = self:next_token()
      if not token then
        self:error(err)
        return nil
      end
      return token
    end
  end
  
  function parser:skip_space(spacechars)
    spacechars = spacechars or " \t\r\n"
    local m = self:match("^["..spacechars.."]+", "last_space")
    if m and self:in_setting() then
      local nl = nil
      repeat
        nl = m:find("\n", nl)
        if nl then
          self:add_newline(self.cur - #m + nl - 1)
          nl = nl+1
        end
      until not nl
    end
    return m
  end
  
  function parser:skip_comment()
    local m = self:match("^#[^\n]+", "last_comment")
    if m then
      self:add_comment(m, self.cur - #m)
    end
    return m
  end
  
  function parser:match_string()
    local quote_char = self.str:sub(self.cur, self.cur)
    local unquote
    local offset = 1
    repeat
      unquote = self:find(quote_char, true, offset)
      if unquote then
        offset = unquote - self.cur + 1
        if count_slashes_reverse(self.str, self.cur, unquote - 1) % 2 == 0 then
          --string end found
          self.last_match = self.str:sub(self.cur, unquote)
          self.cur = unquote + 1
          return self:add_value_to_setting("string", self.last_match, {quote_char=quote_char})
        end
      end
    until not unquote
    return nil, "unterminated string"
  end
  
  function parser:match_variable()
    local var = self:match("^%$([%w%.%_]+)")
    if var then
      return self:add_value_to_setting("variable")
    end
    
    if self:match("^%$[^%s;]+") then
      return nil, "invalid variable name " .. self:match()
    elseif self:match("^%$") then
      return nil, "empty variable name"
    else
      return nil, "invalid variable"
    end
  end
  
  function parser:match_value()
    if not self:match("^[^%s%;]+") then
      return nil, "invalid value"
    end
    return self:add_value_to_setting("value")
  end
  
  function parser:match_setting_name()
    if not self:match("^[^%s]+") then
      if self:match("^;") then
        return nil, "unexpected \";\""
      elseif self:match("^{") then
        return nil, "unexpected \"{\""
      elseif self:match("^%S+") then
        return nil, "invalid config setting name \"" .. self:match() .. "\""
      else
        return nil, "expected config setting name"
      end
    end
    local module_name, name = nil, self:match()
    if name:match("%:") then
      module_name, name = name:match("^([^%:%.]+):([^%s]+)$")
      if not name or not module_name then -- "foo:" or ":foo"
        return nil, "invalid config setting name \""..self:match().."\""
      end
    end
    return self:push_setting(name, module_name)
  end
  
  function parser:match_semicolon()
    assert(self:match("^;"))
    local semi = self:add_semicolon(self.cur - 1)
    self:skip_space(" \t")
    self:skip_comment()
    assert(self:pop_setting())
    return semi
  end
  
  function parser:print_stack()
    print("parser "..tostring(self).." stack:")
    for _, v in ipairs(self.stack) do
      print("  ", v.name, v.type)
    end
  end
  
  function parser:next_token()
    if self.cur > #self.str then
      return nil --we're done
    end
    
    if self:in_block() then
      if self:match("^}") then
        return self:pop_block()
      else
        local setting, err = self:match_setting_name()
        if not setting then
          return nil, err
        end
        return setting
      end
    end
    
    assert(self:in_setting())
    local char = self.str:sub(self.cur, self.cur)
    local ok, err
    if char == "\"" or char == "'" then
      ok, err = self:match_string()
    elseif char == "$" then
      ok, err = self:match_variable()
    elseif char == "{" then
      self.cur = self.cur + 1
      ok, err = self:push_block()
    elseif char == "}" then
      err = "unexpected \"}\""
    elseif char == ";" then
      ok, err = self:match_semicolon()
    else
      ok, err = self:match_value()
    end
    
    if not ok then
      return nil, err
    end
    return ok
  end
  
  function parser:location(pos)
    --
    if type(pos) == "table" then
      if pos.position then
        pos = pos.position.first
      elseif type(pos.first) == "number" then
        pos = pos.first
      end
    end
    if type(pos) ~= "number" then
      error("unknown position " .. tostring(pos))
    end
    local line = 1
    local cur = 1
    pos = pos or self.cur
    if pos < 0 then
      pos = #self.str - pos
    end
    while true do
      local nl = self.str:find("\n", cur, true)
      if not nl then --no more newlines
        break
      elseif nl < pos then --newline in-range
        line = line + 1
        cur = nl + 1
      else -- we've gone past last
        break
      end
    end
    return line, pos - cur
  end
end

local function mock(strength)
  local mocktable = {
    mild = {
      "Are you sure you know what you're doing?",
      "An understandable mistake, but a mistake nonetheless.",
      "Have you read the documentation?"
    },
    moderate = {
      "Seriously?",
      "That was careless of you.",
      "Come on, this is pretty obvious.",
      "Not your greatest work.",
      "Are you even trying?",
      "What do you think you're doing?"
    },
    strong = {
      "Don't be ridiculous!",
      "That's just embarassing.",
      "You are bad and you should feel bad.",
      "Was this your bright idea?",
      "A stupid mistake. Don't repeat it.",
    }
  }
  local mock_strength = mocktable[strength] or {""}
  return mock_strength[math.random(#mock_strength)]
end

local function read_file(path, file_description)
  local f, err = io.open(path, "rb")
  if not f then
    return nil, "failed to open "..file_description.." file " .. err
  end
  local str, readerr = f:read("*all")
  if not str then
    return nil, "failed to read "..file_description.." at " .. path .. ": " .. (readerr or "")
  end
  f:close()
  return str
end

function Config.new(name)
  local config = {
    name = name, --could be the filename
    string = nil,
    handlers = {},
    handlers_any_module = {},
    parsers = {}
  }
  setmetatable(config, config_mt)
  
  for _, setting_handler in pairs(config_settings) do
    assert(config:register_setting("config", setting_handler))
  end
  
  return config
end


--utility functions
function Config.split_path(pathy_thing)
  if type(pathy_thing)=="string" then
    pathy_thing = {path=pathy_thing}
  end
  assert(type(pathy_thing) == "table", "path isn't a string or table" .. debug.traceback())
  if pathy_thing.split_path then
    return pathy_thing.split_path
  end
  
  local tbl = {}
  local path = pathy_thing.path
  if not path:match("^%/") then
    table.insert(tbl, "**")
  end
  if path == "*" or path == "*/" then
    return tbl
  end
  
  if #path and not path:match("%/$") then
    path = path.."/"
  end
  for pathpart in path:gmatch("[^%/]+") do
    table.insert(tbl, pathpart)
  end
  pathy_thing.split_path = tbl
  return tbl
end
local function match_path_part(ppart, mpart)
  if not mpart then
    return false
  end
  
  local parenthesized = mpart:match("^%((.*)%)$")
  if parenthesized then
    for ormatch in parenthesized:gmatch("[^%|]+") do
      if match_path_part(ppart, ormatch) then
        return true
      end
    end
    return false
  end
  
  if ppart ~= mpart and mpart ~= "*" and mpart ~= "**" then
    local ppart_left, ppart_right = ppart:match("^([^:]+):(.*)$")
    if not ppart_left then
      ppart_left, ppart_right = nil, ppart
    end
    local mpart_left, mpart_right = mpart:match("^([^:]+):(.*)$")
    if not mpart_left then
      mpart_left, mpart_right = "*", mpart
    end
    if mpart_left ~= "*" and mpart_left ~= ppart_left then
      return false
    end
    
    if mpart_right ~= "*" and mpart_right ~= ppart_right then
      return false
    end
  end
  return true
end
local function unwind_to_last_doublestar(match, m)
  for i=m+1, #match do
    if match[i]=="**" then
      return i
    end
  end
  return false
end
function Config.match_path(path, match)
  path = Config.split_path(path)
  match = Config.split_path(match)
  if #match == 1 and match[1] == "**" then
    return true
  end
  local m = #match
  for i=#path, 1, -1 do
    local ppart = path[i]
    local mpart = match[m]
    if mpart == "**" then m = m-1 end
    if not match_path_part(ppart, mpart) then
      m = unwind_to_last_doublestar(match, m)
      if not m then
        return false
      end
    end
    m=m-1
  end
  if m == 0 then
    return true
  end
  for i=m,1,-1 do
    if match[i] ~= "**" then
      return false
    end
  end
  return true
end

do --config
  local config = {}
  config_mt = {
    __index=config,
    __gxcopy_metatable = function()
      return require("shuttlesock.core.config").metatable
    end
  }
  
  function config:parse(str, opt)
    assert(type(str) == "string")
    opt = opt or {}
    assert(type(opt) == "table")
    if opt.name then
      assert(type(opt.name) == "string")
    end
    self.name = opt.name
    
    local parser = assert(Parser.new(str, {name = self.name, root = self.root}))
    local ok, err = parser:parse()
    assert(ok, err or "parser failed for unknown reasons")
    
    self.root = parser.root
    table.insert(self.parsers, parser)
    for setting in self:each_setting() do
      if not setting.parser_index then
        setting.parser_index = #self.parsers
        if setting.block then
          setting.block.parser_index = #self.parsers
        end
      end
    end
    
    --now handle includes
    assert(self:handle("config:include_path"))
    assert(self:handle("config:include"))
    
    
    self.parsed = true
    return self
  end
  
  function config:block_handled_by_module(block, module_name)
    local setting = block.setting
    self:get_setting_path(setting)
    for _, handler in pairs(self.handlers) do
      if Config.match_path(setting, handler) then
        return true
      end
    end
    return false
  end
  
  function config:find_setting(name, context)
    if not context then context = self.root end
    local module
    if not name:match("%:") then
      module = false
    elseif name:match("^%*") then
      module = false
      name = name:match("^[^%:]+%:(.*)")
    else
      module, name = name:match("^([^%:]+)%:(.*)")
    end
    local current_context = context
    
    while current_context do
      for _, d in ipairs(current_context.block.settings) do
        if name == d.name and (module and module == d.module or true) then
          local handler = d.handled_by and self.handlers[d.handled_by] or nil
          if handler and Config.match_path(context.block, handler) then
            return d
          end
        end
      end
      
      if current_context ~= current_context.parent then
        current_context = current_context.parent
      else
        current_context = nil
      end
    end
    
    --not in parent context. maybe use a default value?
    local path = self:get_path(context.block)
    local fakesetting = {
      name = name,
      path = path
    }
    local found_handler = self:find_handler_for_setting(fakesetting)
    if found_handler then
      --copy?
      local default_setting = {}
      for k, v in pairs(found_handler.default_setting) do
        rawset(default_setting, k, v)
      end
      setmetatable(default_setting, getmetatable(found_handler.default_setting))
      return default_setting
    end
    return false
  end

  function config:find_handler_for_setting(setting)
    if setting.full_name then
      local handler = self.handlers[setting.full_name]
      if handler then
        return handler
      else
        return nil, "unknown setting " .. setting.full_name
      end
    else
      local name = "*:"..setting.name
      local possible_handlers = self.handlers_any_module[name]
      
      if not possible_handlers then
        return nil, "unknown setting " .. setting.name
      end
      self:get_path(setting)
      local matches = {}
      for _, handler in ipairs(possible_handlers) do
        if Config.match_path(setting, handler) then
          table.insert(matches, handler)
        end
      end
      if #matches == 0 then
        return nil, "unknown setting " .. setting.name
      elseif #matches > 1 then
        local handler_names = {}
        for _, h in ipairs(possible_handlers) do
          table.insert(handler_names, h.module .. "," ..h.name)
        end
        return nil, "ambiguous setting " .. setting.name..", could be any of: " .. table.concat(handler_names, ", ")
      else
        return matches[1]
      end
    end
  end

  function config:get_path(block_or_setting)
    if block_or_setting.path then
      return block_or_setting.path
    end
    local function is_root(s)
      return (s.parent == s) or not s.parent
    end
    if block_or_setting.type == "block" then
      local setting = block_or_setting.setting
      local setting_path = self:get_path(setting)
      local setting_path_part = is_root(setting) and "" or (setting.full_name or setting.name)
      local slash = setting_path:match("/$") and "" or "/"
      block_or_setting.path = setting_path..slash..setting_path_part
      return block_or_setting.path
    else
      assert(block_or_setting.type == "setting")
      local setting = block_or_setting
      if not setting.path then
        if is_root(setting) then --root
          setting.path = ""
        elseif is_root(setting.parent) then
          setting.path = "/"
        else
          local parent_path = self:get_path(setting.parent)
          local slash = parent_path:match("/$") and "" or "/"
          local parent_path_part = (setting.parent.full_name or setting.parent.name)
          setting.path = ("%s%s%s"):format(self:get_setting_path(setting.parent), slash, parent_path_part)
        end
      end
      return setting.path
    end
  end

  function config:ptr_lookup(ptr)
    assert(type(ptr)=="userdata")
    local reftable = debug.getregistry()["shuttlesock.config.pointer_ref_table"]
    if reftable then
      return reftable[ptr]
    end
  end
  
  function config:error(block_or_setting, message, ...)
    if type(block_or_setting) == "userdata" then
      block_or_setting = self:ptr_lookup(block_or_setting)
    end
    if select("#", ...) > 0 then
      message = message:format(...)
    end
    
    message = ('%s for %s "%s"'):format(message, block_or_setting.type or "(?)", block_or_setting.name or "")
    
    local parser = self.parsers[block_or_setting.parser_index]
    if not parser then
      return message .. " in unknown location"
    end
    return parser:error(message, block_or_setting)
  end
  
  function config:get_setting_path(setting)
    local function is_root(s)
      return (s.parent == s) or not s.parent
    end
    if not setting.path then
      if is_root(setting) then --root
        setting.path = ""
      elseif is_root(setting.parent) then
        setting.path = "/"
      else
        local parent_path = self:get_setting_path(setting.parent)
        local slash = parent_path:match("/$") and "" or "/"
        local parent_path_part = (setting.parent.full_name or setting.parent.name)
        setting.path = ("%s%s%s"):format(self:get_setting_path(setting.parent), slash, parent_path_part)
      end
    end
    return setting.path
  end

  function config:load(path)
    if not self.name then
      self.name = path
    end
    assert(not self.loaded, "config already loaded")
    assert(self.name, "config name must be set to the filename when using config:load()")
    local str = assert(read_file(path, "config"))
    return self:parse(str, path)
  end

  function config:merge(config2)
    error("not yet implemented")
  end
  
  function config:handle(handler_name, context)
    for setting in self:each_setting(context) do
      local ok, handler, err
      handler, err = self:find_handler_for_setting(setting)
      
      if handler and (not handler_name or handler_name == handler.full_name) then
        if handler.handler then
          ok, err = handler.handler(setting.values, handler.default)
        elseif handler.internal_handler then
          ok, err = handler.internal_handler(setting, handler.default, self)
        else
          ok = true
        end
        setting.handled_by = handler.full_name
      else
        ok = true
      end
      if not ok then
        local err_prefix = "error handling \""..(setting.full_name or setting.name).."\" setting"
        local parser = self.parsers[setting.parser_index]
        if parser then
          return nil, parser:error(err_prefix, setting.position.first) .. ": ".. (err or "unspecified error")
        else
          return nil, err_prefix .. ": ".. (err or "unspecified error")
        end
      end
    end
    return true
  end
  
  function config:each_setting(start, filters) --for loop iterator
    --assert(self.root)
    local should_walk_setting = filters and filters.setting_block or function(setting)
      return setting.block
    end
    local should_yield_setting = filters and filters.setting or function(setting) return true end
    local function walk_setting(block, parent_setting)
      for _, setting in ipairs(block.settings) do
        if should_yield_setting(setting) then
          coroutine.yield(setting, parent_setting)
        end
        if should_walk_setting(setting) then
          walk_setting(setting.block, setting)
        end
      end
    end
    if not self.root then
      return function() return nil end
    end
    return coroutine.wrap(function()
      return walk_setting(start or self.root.block, nil)
    end)
  end
  
  local function replace_in_table(element_name, tbl, element, replacements)
    for i, v in ipairs(tbl) do
      if v == element then
        table.remove(tbl, i)
        for j, rd in ipairs(replacements) do
          table.insert(tbl, i+j-1, rd)
        end
        return true
      end
    end
    return nil, element_name .. " to replace not found"
  end
  
  function config:replace_setting(setting, ...)
    return replace_in_table("setting", setting.parent.block.settings, setting, {...})
  end
  function config:replace_token(setting, ...)
    return replace_in_table("token", setting.parent.block.tokens, setting, {...})
  end
  
  function config:register_setting(module_name, setting)
    local name
    local aliases = {}
    local path
    local description
    local args_min, args_max
    local block
    local default_setting
    local default_values
    local function ensure(condition, fmt, ...)
      if not condition then
        error(("module %s setting \"%s\" "..fmt):format(module_name, setting.name, ...))
      end
      return true
    end
    local function ensure_type(field_name, expected_type)
      local t = type(setting[field_name])
      if not setting[field_name] then
        error(("module %s setting \"%s\" must be of type %s, but was %s"):format(module_name, setting.name, expected_type, t))
      end
    end
    
    assert(type(module_name) == "string", "module name must be a string, but was "..type(module_name)..". " .. mock("mild"))
    assert(type(setting) == "table", "module "..module_name.." setting must be a table")
    
    assert(type(setting.name) == "string", "module "..module_name.." setting name must be a string, but is ".. type(setting.name))
    
    ensure(setting.name:match("^[%w_%.]+$"), "name contains invalid characters")
    name = setting.name
    
    if setting.alias then
      ensure_type("aliases", "table")
      for k, v in pairs(setting.aliases) do
        ensure(type(k) == "number", "aliases must be a number-indexed table")
        ensure(v:match("^[%w_%.]+$"), "alias '%s' is invalid", v)
        table.insert(aliases, v)
      end
    end
    
    ensure_type("path", "string")
    ensure(not setting.path:match("%/%/"), "path '%s' is invalid", setting.path)
    ensure(setting.path:match("^[%w%:_%.%/%*]*$"), "path '%s' is invalid", setting.path)
    path = setting.path:match("^(.+)/$") or setting.path
    
    description = ensure(setting.description, "description is required, draconian as that may seem")
    
    if not setting.nargs then
      args_min, args_max = 1, 1
    elseif type(setting.nargs) == "number" then
      if math.type then
        ensure(math.type(setting.nargs) == "integer", "setting.nargs must be an integer")
      else
        assert(math.floor(setting.nargs) == setting.nargs, "setting.nargs must be an integer")
      end
      args_min, args_max = setting.nargs, setting.nargs
    elseif type(setting.nargs == "string") then
      args_min, args_max = setting.nargs:match("^(%d+)%s*%-%s*(%d+)$")
      if not args_min or not args_max then
        args_min, args_max = setting.nargs:match("^(%d+)%s*%.%.%s*(%d+)$")
      end
      if not args_min or not args_max then
        args_min = setting.nargs:match("^%d+$")
        args_max = args_min
      end
      if not args_min then
        assert(args_min and math.floor(args_min) ~= args_min, "nargs is invalid")
      end
      args_min, args_max = tonumber(args_min), tonumber(args_max)
    else
      ensure_type("nargs", "number or string")
    end
    ensure(args_min <= args_max, "nargs minimum must be smaller or equal to maximum")
    ensure(args_min >= 0, "nargs minimum must be non-negative. " .. mock("moderate"))
    ensure(args_max >= 0, "nargs maximum must be non-negative." .. mock("moderate"))
    
    if setting.block then
      if type(setting.block) ~= "boolean" then
        ensure(setting.block == "optional", "block must be boolean, nil, or the string \"optional\"")
      end
      block = setting.block
    else
      block = false
    end
    
    if setting.default then
      ensure_type("default", "string")
      local default_value_parser = Parser.new(("%s %s;"):format(setting.name, setting.default), {name = setting.name .. " default value"})
      local default_parsed, err = default_value_parser:parse()
      ensure(default_parsed, "default string invalid: %s", err)
      default_setting = default_parsed.block.settings[1]
      default_values = default_setting.values
      
      assert(default_values)
    end
    
    if setting.handler then
      ensure(type(setting.handler) == "function", "handler must be a function." .. mock("moderate"))
    end
    if setting.internal_handler then
      ensure(type(setting.internal_handler) == "function", "internal_handler must be a function." .. mock("moderate"))
    end
    
    local handler = {
      module = module_name,
      name = name,
      full_name = module_name .. ":" .. name,
      aliases = aliases or {},
      path = path,
      description = description,
      nargs_max = args_max,
      nargs_min = args_min,
      default_values = default_values,
      default_setting = default_setting,
      block = block,
      handler = setting.handler,
      internal_handler = setting.internal_handler
    }
    
    ensure(not self.handlers[handler.full_name], 'already exists')
    
    self.handlers[handler.full_name] = handler
    for _, alias in ipairs(handler.aliases) do
      local alias_full_name = module_name.."."..alias
      ensure(not self.handlers[alias_full_name], 'aliased as %s already exists', alias_full_name)
      self.handlers[alias_full_name] = handler
    end
    
    local shortname="*:"..name
    if not self.handlers_any_module[shortname] then
      self.handlers_any_module[shortname] = {}
    end
    table.insert(self.handlers_any_module[shortname], handler)
    
    return handler
  end
  
  function config:config_string(cur, lvl)
    local function indent(level)
      return ("  "):rep(level)
    end
    lvl = lvl or 0
    cur = cur or self.root.block
    local buf = {}
    if cur.type == "block" then
      for _, v in ipairs(cur.tokens) do
        table.insert(buf, self:config_string(v, lvl))
      end
      if cur == self.root.block then
        return table.concat(buf, "\n")
      else
        return ("{\n%s\n%s}\n"):format(table.concat(buf, "\n"), indent(lvl-1))
      end
    elseif cur.type == "setting" then
      local str = indent(lvl) .. (cur.full_name or cur.name)
      local prev_type = "none"
      for _, token in ipairs(cur.tokens) do
        local pre = prev_type == "newline" and indent(lvl+1) or " "
        
        if token.type == "newline" then
          str = str .. "\n"
        elseif token.type == "comment" then
          str = str .. pre .. token.value
        elseif token.type == "value" or token.type == "string" or token.type == "variable" then
          str = str .. pre .. token.value.raw
        elseif token.type == "semicolon" then
          str = str .. (prev_type == "newline" and indent(lvl) or "")..";"
        else
          error("unexpected token type " .. token.type)
        end
        
        prev_type = token.type
      end
      
      if cur.block then
        str = str .. " " .. self:config_string(cur.block, lvl + 1)
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
  
  function config:all_settings()
    local t = {}
    for setting in self:each_setting() do
      table.insert(t, setting)
    end
    return t
  end
  
  function config:get_root()
    if not self.root then
      return nil, "config has not been parsed and therefore has no root"
    end
    return self.root
  end
  
  function config:all_blocks()
    local t = {self.root.block}
    for setting in self:each_setting(nil, {setting = function(s) return s.block end}) do
      table.insert(t, setting.block)
    end
    return t
  end
  
  function config:default_values(setting)
    if not setting.handled_by then
      return {}
    end
    local handler = self.handlers[setting.handled_by]
    if not handler then
      return nil, ("can't find handler for setting \"%s\" handled by %s"):format(setting.full_name, setting.handled_by)
    end
    return handler.default_values or {}
  end
  
  function config:predecessor(setting) --setting to inherit values from
    setting = setting.parent
    while setting and setting.parent ~= setting do
      if setting.block then
        for _, d in ipairs(setting.block.settings) do
          if setting.handled_by and setting.handled_by == d.handled_by then
            return d
          end
        end
      end
      setting = setting.parent
    end
    return false
  end
  
  function config:inherited_values(setting)
    local predecessor = self:predecessor(setting)
    if not predecessor then
      return {}
    end
    return predecessor.values
  end
  
  function config:mm_setting(setting)
    return mm_setting(setting)
  end
  
end

--needed in luaS_gxcopy
Config.metatable = config_mt
Config.parser_metatable = parser_mt

return Config
