-- step_start_fileserver.lua - scenario building block wrapping
-- helpers.lua's run_fileserver(). Returns a function() that does NOT
-- run on its own when dofile()'d - call from within a coroutine
-- (emu.wait needs one).

local dir = debug.getinfo(1, "S").source:match("@(.*/)")
local h = dofile(dir .. "helpers.lua")

return h.run_fileserver
