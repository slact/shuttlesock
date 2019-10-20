local function fail(str, ...)
  local arg= {...}
  if #arg == 0 then
    io.stderr:write(str.."\n")
  else
    io.stderr:write(str:format(...).."\n")
  end
  os.exit(1)
end

local ARG={...}

if ARG[1] == "--nocheck" then
  --do nothing. this is here just to make the CMake script less repetitive
  return
elseif ARG[1] == "--check" then
  local ok, luacheck  = pcall(require, "luacheck")
  if not ok then
    fail("luacheck not found")
  end
  local luacheck_config = require "luacheck.config"
  if #ARG == 1 then return end
  
  local conf = luacheck_config.load_config()
  local files = {}
  for i=2, #ARG do
    table.insert(files, ARG[i])
  end
  
  local report = luacheck(files, conf.options)
  if report.errors == 0 and report.warnings == 0 and report.fatals == 0 then
    return
  end
  
  local out = require "luacheck.format".format(report, files, {quiet=1})
  io.stderr:write(out.."\n\n")
  os.exit(1)
elseif ARG[1] == "--pack" then
  local scripts = dofile(ARG[2])

  local struct_fmt = [[  {
      .name = "%s",
      .module = %s,
      .filename = "%s",
      .%s = "%s",
      .%s = %d
    }]]

  local packed_fmt =[[//auto-generated original content, pls don't steal or edit
  #include <shuttlesock/embedded_lua_scripts.h>
  shuso_lua_embedded_scripts_t shuttlesock_lua_embedded_scripts[] = {
  %s
  };
]]

  local struct = {}

  for _, script in ipairs(scripts) do
    local path = script.file or script.src
    local file = io.open(path, "rb")
    if not file then
      fail("failed to open file %s", path)
    end
    script.data=file:read("*a")
    file:close()
    local out
    if script.compiled then
      out = script.data:gsub(".", function(c)
        return ("\\x%02x"):format(c:byte())
        --local byte = c:byte()
        --if byte < 0x30 or byte == 0x22 or byte > 0x7E then
        --  return ("\\x%02x"):format(c:byte())
        --end
      end)
    else
      out = script.data
      out = out:gsub('\\', '\\\\')
      out = out:gsub('"', '\\"')
      out = out:gsub("\r?\n", function(c)
        if c == "\r\n" then
          return '\\r\\n"\n      "'
        else
          return '\\n"\n      "'
        end
      end)
    end
    
    script.struct = struct_fmt:format(script.name, script.module and "true" or "false", script.src, script.compiled and "compiled" or "script", out, script.compiled and "compiled_len" or "script_len", #script.data)
    
    table.insert(struct, script.struct)
  end

  table.insert(struct, "  {.name=NULL,.script=NULL}")

  local out = packed_fmt:format(table.concat(struct, ",\n"))
  local outpath = ARG[3]
  local outfile = io.open(outpath, "wb")
  if not outfile then
    fail("failed to open output file %s", outpath)
  end

  outfile:write(out)
  outfile:close()
else
  fail("first arg must be --pack or --check")
end
