local testmod, field, val = ...
local Shuso = require "shuttlesock"
local Process = require "shuttlesock.process"
local Atomics = require "shuttlesock.atomics"
local Watcher = require "shuttlesock.watcher"

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
  local ok, count, evald, cached = self.shared.ok, self.shared.count, self.shared.evaluated, self.shared.cached
  if ok ~= count then
    return nil, "Expected to have ".. count .. "values ok'd, but got " .. ok)
  elseif self.shared.setting_evaluated_count ~= self.shared.settings_count then
    return nil, "Settings evaluated too many times"
  end
  return true
end

testmod:subscribe("core:manager.start", function(self)
  coroutine.wrap(function()
    local receiver = IPC.Receiver.start("done")
    while true do
      local data, sender = receiver:yield()
      if not data and sender == "canceled" then return end
      assert(data == "worker_done")
      if self.shared.settings_count > 0 and self.shared.settings_ok == self.shared.settings_count then
        --test completed successfully
        assert(self.shared.settings_count == self.shared.setting_evaluated_count)
        Shuso.stop()
      end
    end
  end)
end)

testmod:subscribe("core:manager.workers_started", function(self)
  coroutine.wrap(function()
    print("hey!")
    Watcher.timer(3):yield()
    print("been trying!", self.shared.settings_ok, self.shared.settings_count, self.shared.setting_evaluated_count)
    if self.shared.settings_ok ~= self.shared.settings_count then
      Shuso.set_error("Timed out waiting for all settings to be ok")
    elseif self.shared.setting_evaluated_count ~= self.shared.settings_count then
      Shuso.set_error("Settings evaluated too many times")
    end
    Shuso.stop()
  end)()
end)

testmod:subscribe("core:worker.start", function(self)
  self.shared:increment("settings_count", 1)
  for i=1,reps do
    assert(self.setting:value("integer") == Process.procnum())
  end
  self.shared:increment("settings_ok", 1)
end)

assert(testmod:add())

local config = 
[[
  ookay $testmodvar;
]]

assert(Shuso.configure_string("test_conf", config))
assert(Shuso:configure_finish())
