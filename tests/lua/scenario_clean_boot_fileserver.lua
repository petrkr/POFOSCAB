-- scenario_clean_boot_fileserver.lua - reference example -autoboot_script
-- showing how to compose helpers.lua's run_pftd/run_fileserver/
-- exit_fileserver/soft_reboot building blocks. Not wired into
-- pytest MAME backend - copy/adjust this pattern for new test cases.
--
-- Usage (clean NVRAM, first boot):
--   mame pofo -ccma ram -exp smartcable -skip_gameinfo \
--     -autoboot_script tests/lua/scenario_clean_boot_fileserver.lua \
--     -window -nomax
-- ...with ~/.mame/nvram/pofo/nvram absent/renamed aside so DIP DOS runs
-- its full first-boot init.

local dir = debug.getinfo(1, "S").source:match("@(.*/)")
local h = dofile(dir .. "helpers.lua")
local init_dip_dos = dofile(dir .. "step_init_dip_dos.lua")

coroutine.wrap(function()
  emu.wait(0.5)

  init_dip_dos()
  emu.wait(0.5)

  h.run_fileserver()
end)()
