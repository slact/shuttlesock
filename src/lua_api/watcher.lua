local Core = require "shuttlesock.core"
local Watcher = {}

function Watcher.new(wt)
  local t = type(wt)
  if t == "string" then
    return Core.new_watcher(wt)
  elseif t == "table" then
    assert(type(wt.type)=="string", "watcher initialization table 'type' field is invalid")
    local watcher = assert(Core.new_watcher(wt.type))
    for k, v in pairs(wt) do
      if k ~= "type" then
        watcher[k]=v
      end
    end
    return watcher
  else
    error("invalid parameter type to Watcher initializer -- must be string or table")
  end
end

return Watcher
