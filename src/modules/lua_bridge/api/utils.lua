local Core = require "shuttlesock.core"
local Utils = {}

Utils.parse_hosts = Core.parse_hosts
Utils.master_has_superuser = Core.master_has_superuser

return Utils
