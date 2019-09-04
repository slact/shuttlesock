local Parser = {}
local parser_mt

local Config = {}
local config_mt

local function mm_directive(directive)
  local mm = require "mm"
  local cpy = {}
  for k,v in pairs(directive) do
    cpy[k]=v
  end
  cpy.parent = "..."
  cpy.block = "..."
  mm(cpy)
end

local config_directives = {
  {
    name = "include_path",
    path = "",
    description = "include path for relative paths in config.include directives",
    nargs = 1,
    handler = function(values, directive, module_ctx, shuttlesock_ctx)
      local parent = directive.parent
      parent.config_include_path = directive.values[1]
      return true
    end
  },
  {
    name    = "include",
    path    = "",
    description = "include configs matching the provided glob pattern",
    nargs   = 1,
    default = nil,
    internal_handler = function(directive, config, shuttlesock_ctx)
      local path = directive.values[1].raw
      
      local include_path = config:getDirective("include_path", directive.parent)
      local paths
      if path:match("[%[%]%?%*]") then
        paths = Config.glob(include_path, directive.values[1])
      else
        paths = {path}
      end
      
      local tokens = {
        {type="comment", value = "#include " .. path ..";"}
      }
      local directives = { }
      
      config.config_include_stack = config.config_include_stack or {}
      table.insert(config.config_include_stack, config.name)
      if #config.config_include_stack > 32 then
        return nil, "too many include levels, there's probably an include loop : \n" ..
          "    loaded ".. table.concat(config.config_include_stack, "\n  included ")
      end
      
      for _, p in ipairs(paths) do
        local included_config = Config.new(p)
        included_config.config_include_stack = config.config_include_stack
        local ok, err = included_config:load()
        if not ok then return nil, err end
        ok, err = included_config:handle("config.include_path")
        if not ok then return nil, err end
        ok, err = included_config:handle("config.include")
        if not ok then return nil, err end
        
        table.insert(tokens, {type="comment", value = "# included file " .. p ..";"})
        
        for _, d in ipairs(included_config.root.block.directives) do
          table.insert(directives, d)
          d.parent = directive.parent
        end
        
        for _, t in ipairs(included_config.root.block.tokens) do
          table.insert(tokens, t)
        end
      end
      
      assert(config:replaceToken(directive, table.unpack(tokens)))
      assert(config:replaceDirective(directive, table.unpack(directives)))
      
      config.config_include_stack = nil
      
      return true
    end
  }
}

function Parser.new(name, string, root_directive)
  local self = {
    name = name,
    str = string,
    cur = 1,
    blocklevel = 0,
    stack = {},
    top = nil,
    root = nil
  }
  setmetatable(self, parser_mt)
  if not root_directive then
    self:pushDirective("root", "config", true)
  else
    self:pushDirective(root_directive, nil, true)
  end
  self:pushBlock("ROOT")
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
  
  parser_mt = {__index = parser}
    
  function parser:error(err, offset)
    if err then
      local line, column = self:location(offset)
      self.errmsg = ("%s in %s:%d:%d"):format(err, self.name or "config", line, column)
      return err
    else
      return self.errmsg
    end
  end
  function parser:lastError()
    return self.errmsg
  end
  
  function parser:inChunkType(chunk_type, stack_position)
    assert(type(chunk_type) == "string")
    stack_position = stack_position or 0
    assert(type(stack_position) == "number" and stack_position <= 0)
    local chunk = self.stack[#self.stack + stack_position]
    return chunk.type == chunk_type and chunk
  end
  
  function parser:inDirective(stack_position)
    return self:inChunkType("directive", stack_position)
  end
  function parser:inBlock(stack_position)
    return self:inChunkType("block", stack_position)
  end
  
  function parser:pushDirective(directive_name, module_name, is_root)
    if is_root then
      assert(self.top == nil)
      assert(#self.stack == 0)
    else
      assert(self:inBlock())
      assert(self:inDirective(-1))
    end
    
    local directive
    if type(directive_name) == "table" and not module_name then
      directive = directive_name
    else
      directive = {
        type = "directive",
        name = directive_name,
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
        directive.full_name = directive.module .. "." .. directive.name
      end
      if is_root and not directive.parent then
        directive.parent = directive --i'm my own grandpa
      else
        directive.parent = self.stack[#self.stack - 1]
      end
    end
    
    table.insert(self.stack, directive)
    self.top = directive
    return true
  end
  
  function parser:popDirective()
    local directive = assert(self:inDirective())
    directive.position.last = self.cur
    
    table.remove(self.stack)
    self.top = self.stack[#self.stack]
    if self.top == nil then
      assert(directive == self.root)
    else
      local block = assert(self:inBlock())
      table.insert(block.directives, directive)
      table.insert(block.tokens, directive)
    end
    return true
  end
  
  function parser:addNewline(pos)
    table.insert(self.top.tokens, {type="newline", position = pos, value = ""})
    return self.top
  end
  
  function parser:addComment(comment, pos)
    assert(type(comment) == "string")
    assert(type(pos) == "number")
    local chunk = self.top
    local cmt = {type="comment", position = pos, value = comment}
    table.insert(chunk.comments, cmt)
    table.insert(chunk.tokens, cmt)
    return chunk
  end
  
  function parser:addSemicolon(pos)
    local directive = assert(self:inDirective())
    local semi = {type="semicolon", position = pos}
    table.insert(directive.tokens, semi)
    return directive
  end
  
  function parser:addValueToDirective(value_type, val)
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
    
    local directive = assert(self:inDirective())
    if not directive.position.values_first then
      directive.position.values_first = value.position.first
    end
    directive.position.values_last = value.position.last
    table.insert(directive.values, value)
    table.insert(directive.tokens, value)
    return true
  end
  
  function parser:pushBlock(block_name)
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
      tokens = {},
      source_directive = assert(self:inDirective())
    }
    table.insert(self.stack, block)
    self.top = block
    return true
  end
  
  function parser:popBlock()
    local block = self.top
    table.remove(self.stack)
    self.top = self.stack[#self.stack]
    local directive = assert(self:inDirective())
    assert(not directive.block)
    directive.block = block
    --finishing a block means we are also finishing the directive it belongs to
    self:popDirective()
    return true
  end

  function parser:parse()
    local token_count = 0
    for _ in self:eachToken() do
      token_count = token_count + 1
    end
    
    if self:inDirective() == "directive" then
      return nil, self:error("unexpected end of file, expected \";\"")
    elseif self:inBlock() == "block" and self.top.source_directive ~= self.root then
      return nil, self:error("unexpected end of file, expected \"}\"")
    else
      self:popBlock()
    end
      
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
  
  function parser:eachToken()
    --for loop iterator
    return function()
      repeat until not (self:skipSpace() or self:skipComment())
      local token = self:nextToken()
      return token
    end
  end
  
  function parser:skipSpace(spacechars)
    spacechars = spacechars or " \t\r\n"
    local m = self:match("^["..spacechars.."]+", "last_space")
    if m and self:inDirective() then
      local nl = nil
      repeat
        nl = m:find("\n", nl)
        if nl then
          self:addNewline(self.cur - #m + nl - 1)
          nl = nl+1
        end
      until not nl
    end
    return m
  end
  
  function parser:skipComment()
    local m = self:match("^#[^\n]+", "last_comment")
    if m then
      self:addComment(m, self.cur - #m)
    end
    return m
  end
  
  function parser:matchString()
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
          return self:addValueToDirective("string")
        end
      end
    until not unquote
    return nil, "unterminated string"
  end
  
  function parser:matchVariable()
    local var = self:match("^%$([%w%.%_]+)")
    if var then
      return self:addValueToDirective("variable")
    end
    
    if self:match("^%$[^%s;]+") then
      return nil, self:error("invalid variable name " .. self:match())
    elseif self:match("^%$") then
      return nil, self:error("empty variable name")
    else
      return nil, self:error("invalid variable")
    end
  end
  
  function parser:matchValue()
    if not self:match("^[^%s;]+") then
      return nil, self:error("invalid value")
    end
    return self:addValueToDirective("value")
  end
  
  function parser:matchDirectiveName()
    if not self:match("^[^%s]+") then
      if self:match("^;") then
        self:error("unexpected \";\"")
      elseif self:match("^{") then
        self:error("unexpected \"{\"")
      elseif self:match("^%S+") then
        self:error("invalid config directive name \"" .. self:match() .. "\"")
      else
        self:error("expected config directive")
      end
      return nil, self:lastError()
    end
    local module_name, name = nil, self:match()
    if name:match("%.") then
      module_name, name = name:match("^([^%.]+)%.([^%s]+)$")
      if not module_name then
        return nil, self:error("invalid config directive name \""..self:match().."\"")
      end
    end
    self:pushDirective(name, module_name)
    return true
  end
  
  function parser:matchSemicolon()
    assert(self:match("^;"))
    self:addSemicolon(self.cur - 1)
    self:skipSpace(" \t")
    self:skipComment()
    return self:popDirective()
  end
  
  function parser:printStack()
    for _, v in ipairs(self.stack) do
      print("  ", v.name, v.type)
    end
  end
  
  function parser:nextToken()
    if self.cur >= #self.str then
      return nil --we're done
    end
    
    if self:inBlock() then
      if self:match("^}") then
        return self:popBlock()
      else
        local ok, err = self:matchDirectiveName()
        if not ok then
          return nil, self:error(err)
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
      return nil, self:error(err)
    end
    return true
  end
  
  function parser:location(pos)
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

local function resolve_path(prefix, name)
  if not prefix or name:match("^%/") then
    --absolute path
    return name
  else
    prefix = prefix:match("^(.*)%/$") or prefix --strip trailing slash
    return prefix .. "/" .. name
  end
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

function Config.glob(glob)
  error("glob function not available. set it with Config.glob = func")
end

function Config.new(name, string)
  local config = {
    name = name, --could be the filename
    string = string,
    handlers = {},
    handlers_any_module = {},
    parent_lookup_table = setmetatable({}, {__mode='kv'}),
    parsers = {}
  }
  setmetatable(config, config_mt)
  
  for _, directive_handler in pairs(config_directives) do
    assert(config:register("config", directive_handler))
  end
  
  return config
end

do --config
  local config = {}
  config_mt = {__index=config}
  
  local function split_path(pathy_thing)
    if pathy_thing.split_path then
      return pathy_thing.split_path
    end
    
    local tbl = {}
    if not pathy_thing.path:match("^%/") then
      table.insert(tbl, "*.*")
    end
    for pathpart in pathy_thing.path:gmatch("^[^%/]+") do
      table.insert(tbl, pathpart:match("%.") and pathpart or "*."..pathpart)
    end
    pathy_thing.split_path = tbl
    return tbl
  end

  local function match_path(directive, handler)
    local dpath = split_path(directive)
    local hpath = split_path(handler)
    for i=#dpath, 1, -1 do
      local d = dpath[i]
      for j = #hpath, 1, -1 do
        local h = hpath[j]
        if d ~= h and h ~= "*.*" and h:match("^%*%.(.+)") ~= d:match("^[^%.]*%.(.+)") then
          return false
        end
      end
    end
    return true
  end
  
  function config:getDirective(name, context)
    if not context then context = self.root end
    local module
    if not name:match("%.") then
      module = false
    elseif name:match("^%*") then
      module = false
      name = name:match("^[^%.]+%.(.*)")
    else
      module, name = name:match("^([^%.]+)%.(.*)")
    end
    
    while context and context.parent ~= context do
      for _, d in ipairs(context.block.directives) do
        if name == d.name and (module and module == d.module or true) then
          return d
        end
      end
      context = context.parent
    end
    
    return nil, "directive not found"
  end
  
  function config:findHandlerForDirective(directive)
    if directive.full_name then
      local handler = self.handlers[directive.full_name]
      if handler then
        return handler
      else
        return nil, "unknown directive " .. directive.full_name
      end
    else
      local name = "*."..directive.name
      local possible_handlers = self.handlers_any_module[name]
      if not possible_handlers then
        return nil, "unknown directive " .. directive.name
      end
      self:getDirectivePath(directive)
      local matches = {}
      for _, handler in ipairs(possible_handlers) do
        if match_path(directive, handler) then
          table.insert(matches, handler)
        end
      end
      if #matches == 0 then
        return nil, "unknown directive " .. directive.name
      elseif #matches > 1 then
        local handler_names = {}
        for _, h in ipairs(possible_handlers) do
          table.insert(handler_names, h.module .. "," ..h.name)
        end
        return nil, "ambiguous directive " .. directive.name..", could be any of :" .. table.concat(handler_names, ", ")
      else
        return matches[1]
      end
    end
  end
  
  function config:getDirectivePath(directive)
    if directive.path then
      return directive.path
    end
    local buf = {}
    local cur = directive
    while true do
      cur = cur.parent
      if cur.parent == cur or not cur then
        directive.path = "/"..table.concat(buf, "/")
        return directive.path
      else
        table.insert(cur, directive.full_name or ("*."..directive.name))
      end
    end
  end
  
  function config:load(path_prefix)
    assert(not self.loaded, "config already loaded")
    local config_string
    if self.string then
      config_string = self.string
    else
      local err
      config_string, err = read_file(resolve_path(path_prefix, self.name), "config")
      if not config_string then
        return nil, err
      end
    end
    
    local parser = Parser.new(self.name, config_string)
    local ok = true
    local res, err = parser:parse()
    if not ok then
      return nil, res
    end
    if not res then
      return nil, err
    end
    
    self.root = parser.root
    
    table.insert(self.parsers, parser)
    for directive in self:eachDirective() do
      directive.parser_index = #self.parsers
    end
    
    --now handle includes
    assert(self:handle("config.include_path"))
    assert(self:handle("config.include"))
    
    return self
  end

  function config:handle(handlers, shuttlesock_ctx)
    for directive in self:eachDirective() do
      local ok, handler, err
      handler, err = self:findHandlerForDirective(directive)
      if handler then
        if handler.handler then
          ok, err = handler.handler(directive.values, directive, shuttlesock_ctx)
        elseif handler.internal_handler then
          ok, err = handler.internal_handler(directive, self, shuttlesock_ctx)
        end
      else
        ok = true
      end
      if not ok then
        local err_prefix = "error in \""..(directive.full_name or directive.name).."\" directive"
        local parser = self.parsers[directive.parser_index]
        if parser then
          return nil, parser:error(err_prefix, directive.position.first) .. ": " .. err
        else
          return nil, err_prefix .. ": " .. err
        end
      end
    end
    return true
  end
  
  function config:eachDirective(start, filters) --for loop iterator
    local walk_directive = filters and filters.directive_block or function(directive)
      return directive.block
    end
    local function walkDirective(block, parent_directive)
      for _, directive in ipairs(block.directives) do
        coroutine.yield(directive, parent_directive)
        if walk_directive(directive) then
          walkDirective(directive.block, directive)
        end
      end
    end
    assert(self.root)
    return coroutine.wrap(function()
      return walkDirective(self.root.block or start, nil)
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
  
  function config:replaceDirective(directive, ...)
    return replace_in_table("directive", directive.parent.block.directives, directive, {...})
  end
  function config:replaceToken(directive, ...)
    return replace_in_table("token", directive.parent.block.tokens, directive, {...})
  end
  
  function config:register(module_name, directive)
    local name
    local aliases = {}
    local path
    local description
    local args_min, args_max
    local block
    local default
    
    assert(type(module_name) == "string", "module name must be a string." .. mock("mild"))
    assert(type(directive) == "table", "drective must be a table")
    
    assert(type(directive.name) == "string", "directive.name must be a string")
    assert(directive.name:match("^[%w_%.]+"), "directive.name \""..directive.name.."\" is invalid")
    name = directive.name
    
    if directive.alias then
      assert(type(directive.aliases) == "table", "directive.aliases must be a table")
      for k, v in pairs(directive.aliases) do
        assert(type(k) == "number", "directive aliases must be a number-indexed table")
        assert(v:match("^[%w_%.]+"), "directive alias \"" ..v.."\" is invalid")
        table.insert(aliases, v)
      end
    end
    
    assert(type(directive.path) == "string", "directive.path must be a string")
    assert(not directive.path:match("%/%/"), "directive.path \"" .. directive.path .."\" is invalid")
    assert(directive.path:match("^[%w_%.%/]*"), "directive.path \"" .. directive.path .."\" is invalid")
    path = directive.path:match("^(.+)/$") or directive.path
    
    description = assert(directive.description, "it may seem draconian, but drective.description is required.")
    
    if not directive.nargs then
      args_min, args_max = 1, 1
    elseif type(directive.nargs) == "number" then
      if math.type then
        assert(math.type(directive.nargs) == "integer", "directive.nargs must be an integer")
      else
        assert(math.floor(directive.nargs) == directive.nargs, "directive.nargs must be an integer")
      end
      args_min, args_max = directive.nargs, directive.nargs
    elseif type(directive.nargs == "string") then
      args_min, args_max = directive.nargs:match("^(%d+)%s*%-%s*(%d+)$")
      if not args_min then
        args_min = tonumber(directive.nargs)
        assert(args_min and math.floor(args_min) ~= args_min, "directive.nargs is invalid")
        args_max = args_min
      end
    else
      error("directive.nargs must be a number or string")
    end
    assert(args_min <= args_max, "directive.nargs minimum must be smaller or equal to maximum")
    assert(args_min >= 0, "directive.nargs minimum must be non-negative." .. mock("moderate"))
    assert(args_max >= 0, "directive.nargs maximum must be non-negative." .. mock("moderate"))
    
    if directive.block then
      if type(directive.block) ~= "boolean" then
        assert(directive.block == "optional", "directive.block must be boolean, nil, or the string \"optional\"")
      end
      block = directive.block
    else
      block = false
    end
    
    if directive.default then
      if type(directive.default) == "table" then
        for k, v in pairs(table) do
          assert(type(k) == "number", "directive.default key must be numeric." .. mock("strong"))
          assert(type(v) == "number" or type(v) == "string" or type(v) == "boolean", "directive.default table values must be strings, numbers, or booleans")
        end
      else
        local t = type(directive.default)
        assert(t == "number" or t == "string" or t == "boolean", "directive.default table values must be strings, numbers, or booleans")
      end
      default = directive.default
    end
    
    if not directive.internal_handler then
      assert(type(directive.handler) == "function", "directive.handler must be a function." .. mock("moderate"))
    end
    
    local handler = {
      module = module_name,
      name = name,
      path = path,
      description = description,
      arg_max = args_max,
      arg_min = args_min,
      arg_default = default,
      block = block,
      handler = directive.handler,
      internal_handler = directive.internal_handler
    }
    
    local full_name = module_name .. "." .. name
    
    assert(not self.handlers[full_name], ('directive "%s" for module %s already exists'):format(name, module_name))
    
    self.handlers[full_name] = handler
    
    local shortname="*."..name
    if not self.handlers_any_module[shortname] then
      self.handlers_any_module[shortname] = {}
    end
    table.insert(self.handlers_any_module[shortname], handler)
    
    return true
  end
  
  function config:configString(cur, lvl)
    local function indent(level)
      return ("  "):rep(level)
    end
    lvl = lvl or 0
    cur = cur or self.root.block
    local buf = {}
    if cur.type == "block" then
      for _, v in ipairs(cur.tokens) do
        table.insert(buf, self:configString(v, lvl))
      end
      if cur == self.root.block then
        return table.concat(buf, "\n")
      else
        return ("{\n%s\n%s}\n"):format(table.concat(buf, "\n"), indent(lvl-1))
      end
    elseif cur.type == "directive" then
      local str = indent(lvl) .. (cur.full_name or cur.name)
      local prev_type = "none"
      for _, token in ipairs(cur.tokens) do
        local pre = prev_type == "newline" and indent(lvl+1) or " "
        
        if token.type == "newline" then
          str = str .. "\n"
        elseif token.type == "comment" then
          str = str .. pre .. token.value
        elseif token.type == "value" or token.type == "string" or token.type == "variable" then
          str = str .. pre .. token.raw
        elseif token.type == "semicolon" then
          str = str .. (prev_type == "newline" and indent(lvl) or "")..";"
        else
          error("unexpected token type " .. token.type)
        end
        
        prev_type = token.type
      end
      
      if cur.block then
        str = str .. " " .. self:configString(cur.block, lvl + 1)
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
end

return Config
