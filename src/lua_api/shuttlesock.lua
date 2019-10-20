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

function Shuttlesock.set_error(...)
  return Core.set_error(...)
end


return Shuttlesock