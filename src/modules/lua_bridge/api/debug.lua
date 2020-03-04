local Core = require "shuttlesock.core"

local Debug = {}

function Debug.abort()
  return Core.raise_SIGABRT()
end

function Debug.raise_signal(signo)
  return Core.raise(signo)
end

function Debug.log_stack(label)
  Core.print_stack(label)
end

return Debug
