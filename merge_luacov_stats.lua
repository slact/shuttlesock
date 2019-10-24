#!/usr/bin/env lua

local function luacov_stats_load(statsfile, data)
  data = data or {}
  local fd, err = io.open(statsfile, "r")
  if not fd then
    return nil, err
  end
  while true do
  local max = fd:read("*n")
  if not max then
    break
  end
  if fd:read(1) ~= ":" then
    break
  end
  local filename = fd:read("*l")
  if not filename then
    break
  end
  if not data[filename] then
  data[filename] = {
    max = max,
    max_hits = 0
  }
  end
  for i = 1, max do
    local hits = fd:read("*n")
    if not hits then
      break
    end
    if fd:read(1) ~= " " then
      break
    end
    if hits > 0 then
      data[filename][i] = data[filename][i] or 0 + hits
      data[filename].max_hits = math.max(data[filename].max_hits, data[filename][i])
    end
  end
  end
  fd:close()
  return data
end

local function luacov_stats_save(statsfile, data, fd)
  fd = fd or assert(io.open(statsfile, "w"))

  local filenames = {}
  for filename in pairs(data) do
    table.insert(filenames, filename)
  end
  table.sort(filenames)

  for _, filename in ipairs(filenames) do
    local filedata = data[filename]
    fd:write(filedata.max, ":", filename, "\n")

    for i = 1, filedata.max do
      fd:write(tostring(filedata[i] or 0), " ")
    end
    fd:write("\n")
  end
  fd:close()
end

local outfile
local data = {}
local infiles={}
for _, v in ipairs({...}) do
  local out = v:match("^--out=(.*)")
  if out then
    outfile = out
  else
    table.insert(infiles, v)
  end
end
if #infiles == 0 then
  io.stderr:write("No stats files to load.\n")
    os.exit(1)
end
for _, f in ipairs(infiles) do
  local err
  data, err = luacov_stats_load(f, data)
  if not data then
    io.stderr:write("Failed to open stats file "..(err or "").."\n")
    os.exit(1)
  end
end
if outfile then
  print("Loaded %s stats file%s", #infiles, #infiles == 1 and "" or "s")
  luacov_stats_save(outfile, data)
  for _,f in ipairs(infiles) do
    local ok, err = os.remove(f)
    if not ok then
      io.stderr:write("Failed to remove merged stats file " .. f..": "..err.."\n")
    else
    end
  end
  print(("Removed %d merged stats file%s."):format(#infiles, #infiles==1 and "" or "s"))
  print(("Saved stats to file %s"):format(outfile))
else
  luacov_stats_save(nil, data, io.stdout)
end
