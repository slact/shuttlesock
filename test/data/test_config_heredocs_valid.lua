local testmod = ...
local Module = require "shuttlesock.module"
local Watcher = require "shuttlesock.watcher"
local Process = require "shuttlesock.process"
local Shuso = require "shuttlesock"

testmod:subscribe("core:manager.workers_started", function()
  assert(testmod.test_initcfg_times == 4)
  Shuso.stop()
end)

function testmod:initialize_config(block)
  --print(block.setting.name, block.path)
  local setting = assert(block:setting("ookay"))
  assert(setting:value(1) == "one", "incorrect string value #1")
  assert(setting:value(2) == "hello i am\n  two", "incorrect heredoc value #2")
  assert(setting:value(3) == "three", "incorrect string value #4")
  assert(setting:value(4) == "    yes this,\n    is\";~athing[{{!~\n\\n", "incorrect heredoc value #4")
  assert(setting:value(5) == "    and one more\n    more", "incorrect heredoc value #5")
  assert(setting:value(6) == "six", "incorrect string value #6")
end

assert(testmod:add())

local config = 
[[
  ookay one <<~TWO three <<-FOUR_THING <<FIVE six; #comment
    hello i am
      two
    TWO
    yes this,
    is";~athing[{{!~\n\\n
    FOUR_THING
    and one more
    more
FIVE
]]

assert(Shuso.configure_string("test_conf", config))
assert(Shuso:configure_finish())
