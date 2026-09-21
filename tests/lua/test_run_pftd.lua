-- test_init_dip_dos.lua - throwaway manual verification of
-- step_init_dip_dos.lua against clean NVRAM.

local dir = debug.getinfo(1, "S").source:match("@(.*/)")
local init_dip_dos = dofile(dir .. "step_init_dip_dos.lua")

coroutine.wrap(function()
  emu.wait(0.5)
  init_dip_dos()
end)()
