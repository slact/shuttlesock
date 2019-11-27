local Module = require "shuttlesock.module"
local Shuso = require "shuttlesock"

local SVERSION, SVERSION_MAJOR, SVERSION_MINOR, SVERSION_PATCH, SVERSION_PADDED = ...

function checkversion()
  assert(SVERSION == ("%d.%d.%d"):format(SVERSION_MAJOR, SVERSION_MINOR, SVERSION_PATCH))
  assert(SVERSION == Shuso.VERSION)
  assert(SVERSION == Shuso._VERSION)
  assert(SVERSION == _G._SHUTTLESOCK_VERSION)
  local padded = tonumber(("%0.4d%0.4d%0.4d"):format(SVERSION_MAJOR, SVERSION_MINOR, SVERSION_PATCH))
  assert(SVERSION_PADDED == padded)
end

checkversion()

local testmod = Module.new {
  name= "lua_testmod",
  version = "0.0.0"
}

testmod:subscribe("core:master.start", checkversion)
testmod:subscribe("core:manager.start", checkversion)
testmod:subscribe("core:worker.start", checkversion)

testmod:subscribe("core:manager.workers_started", function()
  Shuso.stop()
end)

assert(testmod:add())
