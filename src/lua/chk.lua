#!/bin/env lua
local mm = require "mm"
local Config = require "config"

local config = Config.new("nginx.conf")
assert(config:load())
assert(config:parse())
print(config:config_string())
