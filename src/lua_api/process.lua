local Core = require "shuttlesock.core"
local Process = {}

function Process.procnum()
  return Core.procnum()
end

function Process.runstate()
  return Core.process_runstate()
end

function Process.type()
  local procnum = Process.procnum()
  if procnum >= 0 then
    return "worker"
  elseif procnum == -1 then
    return "manager"
  elseif procnum == -2 then
    return "master"
  else
    return nil
  end
end

function Process.count_workers()
  return Core.count_workers()
end

return Process
