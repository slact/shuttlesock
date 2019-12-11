local foo = {
  bar = 10
}
local gc_called
setmetatable(foo, {__gc=function() gc_called = true end})

collectgarbage("collect")

assert(not gc_called)

foo = nil

collectgarbage("collect")

assert(gc_called)
