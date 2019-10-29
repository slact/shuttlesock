local Config = require "shuttlesock.core.config"
local function assert_match_path(path, match, result)
  if result == nil then result = true end
  if Config.match_path(path, match) ~= result then
    error(("path '%s' was %sexpected to match '%s', but %s"):format(path, result and "" or "not ", match, result and "it didn't" or "it did"))
  end
end
local function assert_not_match_path(path, match)
  return assert_match_path(path, match, false)
end


assert_match_path("/foo/bar/baz", "*")
assert_not_match_path("/foo/bar/baz", "/mod:foo/bar/baz")
assert_match_path("/foo/bar/baz", "*/")
assert_not_match_path("/foo/bar/baz", "banana")
assert_not_match_path("/foo/bar/baz", "beep/")
assert_match_path("/foo/bar/baz/qux/womp/meep", "/foo/**/womp/*")
assert_match_path("/foo/bar", "/foo/bar/**")
assert_match_path("/foo/bar", "/*:foo/bar/**")
assert_match_path("/foo/bar/baz", "/*/*/*")
assert_not_match_path("/foo/bar/baz/baz", "/*/*/*")
assert_not_match_path("/foo/bar/baz", "/*/*/*/*")
assert_match_path("/wat:foo/bar", "/wat:*/bar/**")
assert_not_match_path("/wat:foo/bar", "/*:hmm/bar/**")
assert_not_match_path("/cheese/bar", "cheese")
assert_not_match_path("/cheese/bar", "/bar")
