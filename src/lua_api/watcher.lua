local Core = require "shuttlesock.core"
local Watcher = {}

function Watcher.io(...)
  error("not yet implemented")
end

function Watcher.timer(after_sec, handler)
  return Core.new_watcher("timer", after_sec, handler)
end

function Watcher.child(signum, trace, handler)
  return Core.new_watcher("child", signum, trace, handler)
end

return Watcher
