-- test_upload_pftd_flow.lua - throwaway manual end-to-end verification:
-- clean boot -> ROM-native fileserver -> (external upload of PFTD.COM to
-- C: happens here, driven by the shell orchestrator) -> exit fileserver
-- -> run PFTD -> fileserver again -> (external HELLO check happens
-- here) -> Lua-driven exit, no -seconds_to_run needed.
--
-- Upload wait: measured (wall clock, twice) at ~29s for a 2488-byte
-- PFTD.COM over the bit-bang link via mame_bridge.py's upload_file().
-- 45s below is that measurement x ~1.5 safety margin.

local dir = debug.getinfo(1, "S").source:match("@(.*/)")
local h = dofile(dir .. "helpers.lua")
local init_dip_dos = dofile(dir .. "step_init_dip_dos.lua")

coroutine.wrap(function()
  emu.wait(0.5)

  init_dip_dos()
  emu.wait(0.5)

  h.run_fileserver()
  emu.print_info("test_upload_pftd_flow: WAYPOINT server-ready (ROM-native, no PFTD)")

  -- Give the shell orchestrator a window to start mame_bridge.py and
  -- upload_file() PFTD.COM to C:.
  emu.wait(45)

  h.exit_fileserver()
  emu.wait(0.5)

  h.run_pftd()
  emu.wait(0.5)

  h.run_fileserver()
  emu.print_info("test_upload_pftd_flow: WAYPOINT server-ready (PFTD)")

  -- Give the shell orchestrator a window to GET /status and check HELLO.
  emu.wait(15)

  emu.print_info("test_upload_pftd_flow: done, exiting")
  manager.machine:exit()
end)()
