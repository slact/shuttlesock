local Instring = {}

local function match_parens(str, start, parenstr)
  local openbyte, closebyte, escapebyte = parenstr:byte(1), parenstr:byte(2), parenstr:byte(3)
  local open_count = 0
  local escaping_next = false
  assert(str:byte(start) == openbyte, "match_parens string must start with the open-paren character")
  for i = start, #str do
    local byte = str:byte(i)
    if escaping_next then
      escaping_next = false
    elseif byte == escapebyte then
      escaping_next = true
    elseif byte == openbyte then
      open_count = (open_count or 0) + 1
    elseif byte == closebyte then
      open_count = open_count - 1
    end
    
    if open_count == 0 then
      return str:sub(start, i)
    end
  end
  
  return nil, "unmatched parens"
end

local Token = {}
function Token.escape(str, cur)
  local m = str:match("^\\(.?.?.?)", cur)
  local simple = {
    ["\\"] = "\\",
    n = "\n",
    r = "\r",
    t = "\t",
    b = "\b",
    f = "\f",
    v = "\v",
  }
  if not m then
    return nil
  end
  local fc = m:sub(1, 1)
  if not fc then
    return false, "backslash is the last char in string", 1
  elseif simple[fc] then
    return true, {type="literal", value=simple[fc]}, 2
  elseif fc:match("^%d") then
    --decimal char (\0 \35 \128)
    m = m:match("^%d%d?%d?")
    local byte = tonumber(m)
    if not byte then
      return false, "invalid decimal byte escape char", #m + 1
    end
    return true, string.char(byte), #m + 1
  elseif fc == "x" then
    m = m:match("^x(%x%x)")
    local byte = tonumber(m, 16)
    return true, {type="literal", string.char(tonumber(byte))}, #m+2
  else
    return true, {type="literal", value=fc}, 2
  end
end
  
function Token.simple_variable(str, cur)
  local m = str:match("^%$([%w_]+)", cur)
  local start = cur
  if not m then
    return nil
  end
  cur = cur + #m + 1
  local params = {}
  while str:match("^%[", cur) do
    local bracketed = match_parens(str, cur, "[]\\")
    --print("bracketed: ", bracketed)
    if not bracketed then
      return false, "unmatched square bracket", cur
    end
    table.insert(params, bracketed:sub(2, -2))
    cur = cur + #bracketed
  end
  
  return true, {type="variable", name=m, params=params}, cur - start
end
  
function Token.bracketed_variable(str, cur)
  if not str:match("^%$%{", cur) then
    return nil
  end
  
  local m = match_parens(str, cur+1, "{}\\")
  
  cur = cur+1+#m
  
  local ok, ret, len = Token.simple_variable("$"..m:sub(2, -2), 1)
  --print(m, #m, len)
  if ok == false then
    return false, ret, cur + ret - 1
  elseif ok == nil or len < #m-2 then
    return false, "invalid bracketed variable", cur
  end
  
  return true, ret, #m+1
end

function Token.literal(str, cur, force)
  if force then
    local literal = str:sub(cur)
    return true, {type="literal", value=literal}, #literal
  else
    if not str:match("^[^%$%\\]", cur) then
      return nil
    end
    local m = str:match("^[^%$%\\]+", cur)
    assert(m)
    return true, {type="literal", value=m}, #m
  end
end


function Instring.parse(setting_value)
  assert(type(setting_value) == "table")
  local valtype = setting_value.type
  local str = setting_value.raw
  local ok, res, len
  
  local tokens = {}
  
  if valtype == "literal" or (valtype == "string" and setting_value.quote_char == "'") then
    ok, res, len = Token.literal(str, 1, true)
    if not ok then
      return nil, (res or "not a literal value"), 1, len
    end
    table.insert(tokens, res)
  elseif valtype == "variable" then
    ok, res, len = Token.simple_variable(str, 1)
    if ok == nil then
      ok, res, len = Token.bracketed_variable(str, 1)
    end
    if not ok then
      return nil, (res or "not a variable"), 1, len
    end
    table.insert(tokens, res)
  else
    assert(valtype == "string" or valtype == "value", "unexpected valtype " .. valtype)
    local cur = 1
    while cur <= #str do
      --print("match", str:sub(cur))
      ok, res, len = Token.escape(str, cur)
      if ok == nil then
        ok, res, len = Token.simple_variable(str, cur)
      end
      if ok == nil then
        ok, res, len = Token.bracketed_variable(str, cur)
      end
      if ok == nil then
        ok, res, len = Token.literal(str, cur)
      end
      assert(ok ~= nil)
      
      if ok == false then
        return nil, res, cur, len
      end
      
      assert(len > 0)
      res.pos={first = cur, last = cur+len}
      
      local last = tokens[#tokens]
      if last and last.type == "literal" and res.type == "literal" then
        last.value = last.value .. res.value
        last.pos.last = cur + len
      else
        table.insert(tokens, res)
      end
      cur = cur + len
    end
  end
  
  local instring = {
    tokens = tokens
  }
  return setmetatable(instring, Instring.metatable)
end

function Instring.tonumber(str)
  return tonumber(str)
end

function Instring.tointeger(str)
  local num = tonumber(str)
  if not num then return nil end
  return math.tointeger(math.floor(num))
end

function Instring.toboolean(str)
  if str == "yes" or str == "1" or str == "true" or str == "on" then
    return true
  elseif str == "no" or str == "0" or str == "false" or str == "off" then
    return false
  else
    return nil
  end
end

function Instring.tosize(str)
  local num, unit = str:match("^([%d%.]+)(%w*)$")
  if not num then
    return nil
  end
  num = tonumber(num)
  if not num then
    return nil
  end
  if unit:match("^[kK][bB]?$") then
    num = num * 1024
  elseif unit:match("^[Mm][bB]?$") then
    num = num * 1048576
  elseif unit:match("^[Gg][bB]?$") then
    num = num * 1073741824
  elseif #unit > 0 and (unit ~= "b" or unit ~= "B") then
    return nil --invalid/unknown unit
  end
  return math.tointeger(math.floor(num + 0.5))
end

function Instring.tostring(str)
  return str
end

function Instring.is_instring(t)
  return type(t) == "table" and getmetatable(t) == Instring.metatable
end

Instring.metatable = {
  __name="instring",
  __gxcopy_metatable = function()
    return require("shuttlesock.core.instring").metatable
  end,
}

return Instring
