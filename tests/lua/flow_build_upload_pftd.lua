-- flow_build_upload_pftd.lua - -autoboot_script for
-- tests/build_upload_pftd.sh. Assumes already-initialized NVRAM with a
-- saved DIP DOS state and AUTOEXEC.BAT already run (no DIP DOS init
-- step needed - just Enter-past-boot timing) - drives: ROM-native
-- fileserver (waypoint 1, external PFTD.COM upload happens here) ->
-- exit -> run PFTD -> fileserver again (waypoint 2, external HELLO
-- check happens here) -> Lua-driven exit.
--
-- Upload wait: this is a fixed emu.wait() - Lua has no visibility into
-- the bridge/upload progress (that happens entirely over the
-- smartcable TCP socket, outside MAME). Measured (wall clock) at ~11-12s
-- for a 2488-byte PFTD.COM on the current MAME build (byte-level
-- TCP with C++ bit-bang) - see tests/tests.md for performance details.
-- 30s gives comfortable headroom; if a future build is slower, this
-- needs bumping again (watch for a "Connection reset by peer"
-- mid-transfer, which means the wait expired and exit_fileserver()
-- ran while the upload was still in flight).

local dir = debug.getinfo(1, "S").source:match("@(.*/)")
local h = dofile(dir .. "helpers.lua")

coroutine.wrap(function()
  emu.wait(0.5)

  h.run_fileserver()
  emu.print_info("flow_build_upload_pftd: WAYPOINT server-ready (ROM-native, no PFTD)")

  -- Give the shell orchestrator a window to start mame_bridge.py and
  -- upload_file() PFTD.COM to C:.
  emu.wait(30)

  h.exit_fileserver()
  emu.wait(0.5)

  h.run_pftd()
  emu.wait(0.5)

  h.run_fileserver()
  emu.print_info("flow_build_upload_pftd: WAYPOINT server-ready (PFTD)")

  -- Give the shell orchestrator a window to GET /status and check HELLO.
  -- Needs to cover: bridge process startup, its background detect loop
  -- catching the ROM's idle broadcast (~100ms typical), and one HELLO
  -- round-trip (measured up to ~1.1s when the very first attempt lands
  -- badly - see mame_bridge.py's send_raw_instrumented comment).
  emu.wait(3)

  emu.print_info("flow_build_upload_pftd: done, exiting")
  manager.machine:exit()
end)()
