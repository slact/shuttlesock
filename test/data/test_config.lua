local testmod, config = ...
local Shuso = require "shuttlesock"
assert(testmod:add())

assert(config, "invalid test: config string missing")
if Shuso.configure_string("test_conf", config) then
  Shuso.configure_finish()
end
