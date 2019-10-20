local Core = require "shuttlesock.core"
local Shuttlesock = {}

function Shuttlesock.stop()
  return Core.stop()
end

function Shuttlesock.run()
  return Core.run()
end

function Shuttlesock.runstate()
  return Core.runstate()
end

function Shuttlesock.set_error(str, ...)
  return Core.set_error(tostring(str):format(...))
end

function Shuttlesock.pointer()
  return Core.shuttlesock_pointer()
end

return Shuttlesock
