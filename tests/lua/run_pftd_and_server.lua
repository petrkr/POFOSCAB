-- debug_run_pftd_and_server.lua - -autoboot_script for interactive MAME
-- debugger sessions: starts PFTD, then drives the ROM's File Transfer
-- Server into "Waiting for connection", and does nothing else - no
-- fixed waits for external orchestration, no manager.machine:exit().
-- Leaves MAME running so the -debug window/gdbstub-equivalent stays
-- usable afterwards. See helpers.lua for run_pftd()/run_fileserver().

local dir = debug.getinfo(1, "S").source:match("@(.*/)")
local h = dofile(dir .. "helpers.lua")

coroutine.wrap(function()
  emu.wait(0.5)

  h.run_pftd()
  emu.wait(0.5)

  h.run_fileserver()

  emu.print_info("debug_run_pftd_and_server: PFTD + File Transfer Server ready")
end)()
