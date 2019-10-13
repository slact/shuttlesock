local Core = require "shuttlesock.core"

local Log = {}

function Log.debug(str, ...)
  return Core.log_debug(tostring(str):format(...))
end
function Log.info(str, ...)
  return Core.log_info(tostring(str):format(...))
end
function Log.notice(str, ...)
  return Core.log_notice(tostring(str):format(...))
end
function Log.warning(str, ...)
  return Core.log_warning(tostring(str):format(...))
end
function Log.error(str, ...)
  return Core.log_error(tostring(str):format(...))
end
function Log.critical(str, ...)
  return Core.log_critical(tostring(str):format(...))
end
function Log.fatal(str, ...)
  return Core.log_fatal(tostring(str):format(...))
end

setmetatable(Log, {
  __call = function(str, ...)
    return Core.log(tostring(str):format(...))
  end
})

return Log
