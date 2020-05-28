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

function Shuttlesock.configure_file(file)
  return Core.configure_file(file)
end

function Shuttlesock.configure_string(str, name)
  return Core.configure_string(str, name)
end

function Shuttlesock.configure_finish()
  return Core.configure_finish()
end

Shuttlesock.VERSION = Core.version()
Shuttlesock._VERSION = Shuttlesock.VERSION

return Shuttlesock
