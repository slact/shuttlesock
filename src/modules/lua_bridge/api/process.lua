local Core = require "shuttlesock.core"
local Process = {}

Process.PROCNUM_UNKNOWN_PROCESS = -404
Process.PROCNUM_NOPROCESS = -3
Process.PROCNUM_MASTER = -2
Process.PROCNUM_MANAGER = -1
Process.PROCNUM_WORKER = 0

function Process.procnum()
  return Core.procnum()
end

function Process.runstate(procnum)
  return Core.process_runstate(procnum)
end

function Process.all_procnums()
  return Core.procnums_active()
end

function Process.worker_procnums()
  --this is not efficient, but whatever
  local procnums = Process.all_procnums()
  local worker_procnums = {}
  for _, num in ipairs(procnums) do
    if num >= Process.PROCNUM_WORKER then
      table.insert(worker_procnums, num)
    end
  end
  return worker_procnums
end

function Process.share_heap(procnum1, procnum2)
  local procnums
  if procnum1 and procnum2 then
    procnums = {}
    for _, pnum in ipairs{procnum1, procnum2} do
      if type(pnum) == "table" then
        for _, p in pairs(pnum) do
          table.insert(procnums, p)
        end
      else
        table.insert(procnums, pnum)
      end
    end
  elseif type(procnum1) ~= "table" and not procnum2 then
    procnums = {Core.procnum(), procnum1}
  elseif type(procnum1) == "table" and not procnum2 then
    procnums = procnum1
  end
  
  local have_master, have_nonmaster = false, false
  for _, num in ipairs(procnums) do
    if num == Process.PROCNUM_MASTER then
      have_master = true
    else
      have_nonmaster = true
    end
    if have_master and have_nonmaster then
      return false
    end
  end
  return true
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

function Process.procnum_to_string(num)
  if num == Process.PROCNUM_UNKNOWN_PROCESS then
    return "unknown process"
  elseif num == Process.PROCNUM_NOPROCESS then
    return "no process"
  elseif num == Process.PROCNUM_MASTER then
    return "master"
  elseif num == Process.PROCNUM_MANAGER then
    return "manager"
  elseif num >= Process.PROCNUM_WORKER then
    return "worker #"..math.tointeger(num)
  else
    return "invalid process number "..num
  end
end

return Process
