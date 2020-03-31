local core = ...

local is_light_userdata = core.is_light_userdata

local function rawpairs(t)
  
  return next, t, nil
end

local function dbgval(val)
  local t = type(val)
  if t == "string" then
    return ('"%.30s%s"'):format(val, #val>30 and "[...]" or "")
  elseif t == "function" then
    local dbginfo = debug.getinfo(val, "S")
    return ('%s function %s%s%s%s'):format(dbginfo.what, dbginfo.namewhat or "", dbginfo.namewhat and " " or "", dbginfo.short_src, dbginfo.linedefined == -1 and "" or ":"..tostring(dbginfo.linedefined))
  else
    return tostring(val)
  end
end


local function gxcopy_check(val, checked, stack, top)
  if checked[val] then
    return true
  end
  local ok, err
  local t = type(val)
  if t == "userdata" and not is_light_userdata(val) then
    local mt = getmetatable(val)
    if not mt then
      return nil, "userdata without metatable"
    elseif mt.__gxcopy_load ~= nil then
      return nil, "userdata without __gxcopy_load metatable field"
    elseif mt.__gxcopy_save ~= nil then
      return nil, "userdata without __gxcopy_save metatable field"
    elseif type(mt.__gxcopy_load) ~= "function" then
      return nil, "userdata with __gxcopy_load metatable field that is not a function"
    elseif type(mt.__gxcopy_save) ~= "function" then
      return nil, "userdata with __gxcopy_save metatable field that is not a function"
    end
    top = {"userdata"}
    table.insert(stack, top)
      ok, err = gxcopy_check(mt.__gxcopy_save, checked, stack, top)
      if not ok then
        table.insert(top, "metatable field __gxcopy_save")
        return nil, err
      end
      ok, err = gxcopy_check(mt.__gxcopy_load, checked, stack, top)
      if not ok then
        table.insert(top, "metatable field __gxcopy_load")
        return nil, err
      end
    table.remove(stack, #top)
  elseif t == "thread" then
    return nil, "coroutine"
  elseif t == "table" then
    checked[val] = true
    if not top then
      top = {"table"}
      table.insert(stack, top)
    end
    local mt = getmetatable(val)
    if mt and mt.__gxcopy_metatable ~= nil then
      if type(mt.__gxcopy_metatable) ~= "function" then
        return nil, "metatable field __gxcopy_metatable that is not a function"
      end
      local nups = debug.getinfo(mt.__gxcopy_metatable, 'u').nups
      if nups == 1 then
        local upname, upval = debug.getupvalue(mt.__gxcopy_metatable, 1)
        if upval ~= _ENV then
          return nil, "metatable function __gxcopy_metatable that has 1 upvalue " .. upname
        end
      elseif nups > 0 then
        return nil, ("metatable function __gxcopy_metatable that has %d upvalues"):format(nups)
      end
      
      local cpymt_ok, cpymt = pcall(mt.__gxcopy_metatable)
      if not cpymt_ok then
        return nil, ("metatable function __gxcopy_metatable that raises error %s"):format(cpymt)
      end
      if cpymt ~= mt then
        return nil, "metatable function __gxcopy_metatable that doesn't return the exact same metatable"
      end
      
      table.insert(top, "metatable field __gxcopy_metatable")
      local toptop = #top
      ok, err = gxcopy_check(val, checked, stack, top)
      if not ok then
        return nil, err
      end
      table.remove(top, toptop)
    elseif mt then
      table.insert(top, "metatable")
      local toptop = #top
      ok, err = gxcopy_check(mt, checked, stack, top)
      if not ok then
        return nil, err
      end
      table.remove(top, toptop)
    end
    
    if mt and (mt.__gxcopy_save ~= nil or mt.gxcopy_load ~= nil) then
      if type(mt.__gxcopy_save) ~= "function" then
        return nil, "metatable field __gxcopy_save that is not a function"
      end
      if type(mt.__gxcopy_load) ~= "function" then
        return nil, "metatable field __gxcopy_load that is not a function"
      end
    end
    
    for k,v in rawpairs(val) do
      ok, err = gxcopy_check(k, checked, stack)
      if not ok then
        table.insert(top, "key")
        return nil, err
      end
      ok, err = gxcopy_check(v, checked, stack)
      if not ok then
        table.insert(top, ("value at key [%s]"):format(dbgval(k)))
        return nil, err
      end
    end
    
    table.remove(stack, #stack)
  elseif t == "function" then
    checked[val] = true
    local nups = debug.getinfo(val, "u").nups
    if nups > 0 then
      top = {""}
      table.insert(stack, top)
      local upval, upval_name
      for i=1, nups do
        upval_name, upval = debug.getupvalue(val, i)
        ok, err = gxcopy_check(upval, checked, stack)
        if not ok then
          top[1] = ("%s upvalue %s"):format(dbgval(val), #upval_name>0 and upval_name or tostring(i))
          return nil, err
        end
      end
      table.remove(stack, #stack)
    end
  end
  return true
end

function core.gxcopy_check(val, what)
  local checked = {}
  for _, v in pairs(package.loaded) do
    checked[v]=true
  end
  checked[_G]=true
  local stack = {}
  local ok, err = gxcopy_check(val, checked, stack)
  if not ok then
    for k, v in ipairs(stack) do
      stack[k]= table.concat(v, " ")
    end
    local own = (type(val) == "table" or #stack > 0) and "contains" or "is"
    what = what or "cannot gxcopy "..type(val)
    table.insert(stack, 1, what.." because it ".. own .." a " .. err)
    return nil, table.concat(stack, "\n  in ")
  end
  return true
end

function core.parse_host(str)
  local ipv6, ipv4, host, port
  
  ipv6, port = str:match("^%[([^%]]+%]):(%d+)$")
  if not ipv6 then
    ipv6 = str:match("^%[([^%]]+%])$")
    port = nil
  end
  if not ipv6 then
    ipv4, port = str:match("^(%d+%.%d+%.%d+%.%d+):(%d+)$")
  end
  if not ipv4 then
    ipv4 = str:match("^(%d+%.%d+%.%d+%.%d+)$")
    port = nil
  end
  if not ipv4 then
    host, port = str:match("^(.*):(%d+)$")
  end
  if not host then
    host = str
    port = nil
  end
  
  if host and not host:match("^[%D%-%.]$") or host:match("%.%.") then
    return nil, 'invalid hostname "' .. host ..'"'
  end
  
  local portnum
  if port then
    portnum = tonumber(port)
    if not portnum or portnum < 1 or portnum > 65535 or math.type(portnum) ~= "integer" then
      return "invalid port ".. tostring(port)
    end
    portnum = port
  end
  
  return {
    ipv4 = ipv4,
    ipv6 = ipv6,
    port = portnum,
    hostname = host
  }
end

core.event_data_wrappers = {}
