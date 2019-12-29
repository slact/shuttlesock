local DESTINATION, REPEAT_TIMES = ... 
REPEAT_TIMES = REPEAT_TIMES or 5000

local data_expected = "yes hello"

local message_name = "hello_everyone"

local count_expected

local Module = require "shuttlesock.module"
local IPC = require "shuttlesock.ipc"
local Shuso = require "shuttlesock"
local Process = require "shuttlesock.process"
local Atomics = require "shuttlesock.atomics"
local Log = require "shuttlesock.log"

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0",
}

local function reset(self)
  self.shared.master = 0
  self.shared.manager = 0
  self.shared.workers = 0
  self.shared.all = 0
  self.shared.sender = Process.PROCNUM_NOPROCESS
end

function testmod:initialize()
  self.shared = Atomics.new('master', 'manager', 'workers', 'running_workers', 'sender', 'all', 'repeated', 'tests_passed')
  self.shared.repeated = 0
  self.shared.running_workers = 0
  self.shared.tests_passed = false
  reset(self)
end

local function data_match(d1, d2)
  if type(d1) ~= type(d2) then
    return false
  elseif type(d1) == "table" then
    for k, v in pairs(d1) do
      if not data_match(v, d2[k]) then
        return false
      end
    end
    for k, v in pairs(d2) do
      if not data_match(v, d1[k]) then
        return false
      end
    end
  else
    return d1 == d2
  end
end

local function receiver(procname)
  return function(self)
    coroutine.wrap(function()
      local receiver = IPC.Receiver.start(message_name)
      while true do
        local data, src = receiver:yield()
        if data == nil and src == "canceled" then
          return true
        end
        self.shared:increment(procname, 1)
        self.shared:increment("all", 1)
        local countmod = 0
        if DESTINATION == "all" then
          countmod = 2
        elseif DESTINATION == "others" then
          countmod = 1
        elseif DESTINATION == "workers" then
          countmod = 0
        elseif DESTINATION == "other_workers" then
          countmod = -1
        end
        assert(IPC.send("manager", "finished", "finished a round"))
        
      end
    end)()
  end
end

local function verify(self)
  if DESTINATION == "all" then
    assert(self.shared.master == 1)
    assert(self.shared.manager == 1)
    assert(self.shared.running_workers >= 1)
    assert(self.shared.running_workers == self.shared.workers, "running: ".. self.shared.running_workers .." expected: "..self.shared.workers)
  elseif DESTINATION == "others" then
    if self.shared.master == 0 then
      assert(self.shared.sender == Process.PROCNUM_MASTER)
    end
    if self.shared.manager == 0 then
      assert(self.shared.sender == Process.PROCNUM_MANAGER)
    end
    if self.shared.workers < self.shared.running_workers then
      assert(self.shared.workers + 1 == self.shared.running_workers)
    end
  elseif DESTINATION == "workers" then
    assert(self.shared.master == 0)
    assert(self.shared.manager == 0)
    assert(self.shared.workers == self.shared.running_workers)
  elseif DESTINATION == "other_workers" then
    assert(self.shared.master == 0)
    assert(self.shared.manager == 0)
    assert(self.shared.workers + 1 == self.shared.running_workers)
  end
end

testmod:subscribe("core:master.start", receiver("master"))

testmod:subscribe("core:worker.start", receiver("workers"))
testmod:subscribe("core:worker.start", function(self)
  self.shared:increment("running_workers", 1)
end)

testmod:subscribe("core:manager.start", receiver("manager"))

testmod:subscribe("core:manager.workers_started", function(self)
  coroutine.wrap(function()
    local receiver = IPC.Receiver.start("finished")
    while self.shared.repeated < REPEAT_TIMES do
      --Log.debug("TAKE " .. self.shared.repeated)
      self.shared:increment("repeated", 1)
      local n = assert(IPC.send(DESTINATION, message_name, data_expected))
      while n > 0 do
        local data, src = receiver:yield()
        n = n - 1
      end
      verify(self)
      reset(self)
    end
    self.shared.tests_passed = true
    Shuso.stop()
  end)()
end)


testmod:subscribe("core:manager.stop", function(self)
  assert(self.shared.tests_passed)
end)


testmod:add()
