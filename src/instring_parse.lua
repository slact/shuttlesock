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
  print("|"..fc.."|")
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
  local m = str:match("^%$(%w+)", cur)
  local start = cur
  if not m then
    return nil
  end
  cur = cur + #m + 1
  local indices = {}
  while str:match("^%[", cur) do
    local bracketed = match_parens(str, cur, "[]\\")
    --print("bracketed: ", bracketed)
    if not bracketed then
      return false, "unmatched square bracket", cur
    end
    table.insert(indices, bracketed:sub(2, -2))
    cur = cur + #bracketed
  end
  return true, {type="variable", name=m, indices=indices}, cur - start
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


local function parse(str, quote_type)
  local cur = 1
  local ok, res, len
  local parsed = {}
  if quote_type == "'" then
    ok, res, len = Token.literal(str, cur, true)
    if not ok then
      return nil, res, cur, len
    end
    table.insert(parsed, res)
  else
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
      
      local last = parsed[#parsed]
      if last and last.type == "literal" and res.type == "literal" then
        print("join", last.value, res.value)
        last.value = last.value .. res.value
        last.pos.last = cur + len
      else
        table.insert(parsed, res)
      end
      cur = cur + len
    end
  end
  
  return parsed
end

return parse
