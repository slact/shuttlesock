local Core = require "shuttlesock.core"

local coro = {}
for k,v in pairs(coroutine) do
  coro[k]=v
end
coro.resume = Core.coroutine_resume
return coro
