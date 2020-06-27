local testmod, field, val = ...
local Shuso = require "shuttlesock"
local Process = require "shuttlesock.process"
local Atomics = require "shuttlesock.atomics"
local Watcher = require "shuttlesock.watcher"
local coroutine = require "shuttlesock.coroutine"
local IPC = require "shuttlesock.ipc"

local reps = 10

local var = {
  name="testmodvar",
  description = "this is a description",
  aliases = {"foo", "bar"},
  path = "*",
  eval = function(name, params, prev, self, setting)
    assert(name == "testmodvar")
    assert(#params == 0)
    if prev == nil then
      self.shared:increment("evaluated", 1)
      return tostring(Process.procnum())
    else
      self.shared:increment("cached", 1)
      assert(prev == tostring(Process.procnum()))
      return false
    end
  end
}

testmod:add_variable(var)

function testmod:initialize()
  self.shared = Atomics.new("ok", "count", "evaluated", "cached")
  self.shared.ok = 0
  self.shared.count = 0
  self.shared.evaluated = 0
  self.shared.cached = 0
end

function testmod:initialize_config(block)
  self.setting = assert(block:setting("ookay"))
end

local function verify(self)
  local workers = Process.count_workers()
  local ok, count, evald, cached = self.shared.ok, self.shared.count, self.shared.evaluated, self.shared.cached
  if count ~= workers then
    return nil, ("Expected %d attempts, got %d"):format(workers, count)
  elseif ok ~= workers then
    return nil, "Expected to have ".. workers .. "values ok'd, but got " .. ok
  elseif workers ~= evald then
    return nil, ("Expected setting to have been evaluated once per worker (%d times), got %d"):format(workers, evald)
  elseif cached ~= (reps - 1) * workers then
    return nil, ("Expected setting to have been pulled from cache %d times, got %d"):format((reps - 1) * workers, cached)
  end
  return true
end

testmod:subscribe("core:manager.start", function(self)
  coroutine.wrap(function()
    local receiver = IPC.Receiver.start("done")
    while true do
      local data, sender = receiver:yield()
      if not data and sender == "canceled" then return end
      local ok, err = verify(self)
      if ok then
        --test completed successfully
        Shuso.stop()
      end
    end
  end)()
end)

testmod:subscribe("core:manager.workers_started", function(self)
  coroutine.wrap(function()
    Watcher.timer(3):yield()
    local ok, err = verify(self)
    if not ok then Shuso.set_error(err) end
    Shuso.stop()
  end)()
end)

testmod:subscribe("core:worker.start", function(self)
  self.shared:increment("count", 1)
  for i=1,reps do
    assert(self.setting:value("integer") == Process.procnum())
  end
  self.shared:increment("ok", 1)
  IPC.send("manager", "done", 1)
end)

assert(testmod:add())

local config = 
[[
  ookay $testmodvar;
]]

assert(Shuso.configure_string("test_conf", config))
assert(Shuso:configure_finish())
