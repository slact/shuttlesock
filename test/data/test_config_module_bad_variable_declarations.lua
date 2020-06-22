local testmod, mutate, failmatch = ...
local Shuso = require "shuttlesock"

local var = {
  name="testvar",
  description = "this is a description",
  aliases = {"foo", "bar"},
  path = "*",
  eval = function() return "meh" end
}

if mutate.var then
  var = mutate.var
else
  for k, v in pairs(mutate) do
    if type(k) == "number" then
      var[v]=nil
    else
      var[k]=v
    end
  end
end
local ok, res = pcall(testmod.add_variable, testmod, var)

assert(not ok)
assert(res:match(failmatch), "error expected to match \""..failmatch.."\", but got \"" .. tostring(res).."\"")
