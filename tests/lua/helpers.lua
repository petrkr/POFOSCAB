-- helpers.lua - shared ioport lookup/tap helpers plus ready-made
-- keystroke-sequence functions for tests/lua scenario scripts.
--
-- ioport field tags/names below come from the MAME fork's
-- src/mame/atari/pofo.cpp keyboard matrix (PORT_START("Yn") blocks
-- inside INPUT_PORTS_START(portfolio)), cross-checked against source,
-- not guessed:
--   Y0: Atari, d  D
--   Y2: e  E, t  T, Enter
--   Y4: s  S, p  P, down-arrow
--   Y5: f  F
--   Y7: Esc
--
-- Tag prefix history: older MAME forks had a separate pofo_kbd.cpp
-- keyboard subdevice, reachable as ":keyboard:Y0" etc. That file is
-- gone as of the "Added SmartCable LPT bridge emulator" rework - the
-- keyboard matrix now lives directly on the root machine device, so
-- the same ports are reachable as ":Y0" etc instead (confirmed by
-- enumerating manager.machine.ioport.ports against a fresh build).
-- find_field() tries both prefixes so this keeps working against
-- either MAME build without editing every call site.
--
-- natkeyboard:post()/post_coded() were tried first but their timing is
-- fixed by MAME's natural_keyboard::choose_delay() (~50-200ms/char) and
-- NOT controllable from Lua - the fork's Lua binding
-- (luaengine_input.cpp) doesn't pass a rate argument through, even
-- though the underlying C++ API supports one. Measured ~10x slower than
-- manual find_field/tap with tight timing, so all step_*.lua files use
-- the manual approach exclusively.
--
-- Usage from a step script:
--   local dir = debug.getinfo(1, "S").source:match("@(.*/)")
--   local h = dofile(dir .. "helpers.lua")
--   h.run_fileserver()

local function find_field(tag, name)
  -- tag is passed as ":keyboard:Yn" (older forks) - also try the bare
  -- ":Yn" root-level tag used by newer builds.
  local port = manager.machine.ioport.ports[tag]
  if not port then
    local bare_tag = ":" .. tag:match("([^:]+)$")
    port = manager.machine.ioport.ports[bare_tag]
  end
  if not port then
    emu.print_error("helpers: no ioport " .. tag)
    return nil
  end
  local field = port.fields[name]
  if not field then
    emu.print_error("helpers: no field '" .. name .. "' on " .. tag)
  end
  return field
end

local function tap(field, hold, gap)
  if field then field:set_value(1) end
  emu.wait(hold)
  if field then field:set_value(0) end
  emu.wait(gap)
end

-- Types "PFTD" + Enter at a plain DOS prompt to start the TSR.
local function run_pftd()
  local pkey  = find_field(":keyboard:Y4", "p  P")
  local fkey  = find_field(":keyboard:Y5", "f  F")
  local tkey  = find_field(":keyboard:Y2", "t  T")
  local dkey  = find_field(":keyboard:Y0", "d  D")
  local enter = find_field(":keyboard:Y2", "Enter")

  tap(pkey, 0.02, 0.04)
  tap(fkey, 0.02, 0.04)
  tap(tkey, 0.02, 0.04)
  tap(dkey, 0.02, 0.04)
  tap(enter, 0.02, 0.3)

  emu.print_info("helpers.run_pftd: PFTD started")
end

-- Atari+S (System Setup) -> 5x Down, Enter (File transfer) -> 2x Down,
-- Enter (Server) - drives the ROM's File Transfer Server into "Waiting
-- for connection" state.
local function run_fileserver()
  local atari = find_field(":keyboard:Y0", "Atari")
  local skey  = find_field(":keyboard:Y4", "s  S")
  local down  = find_field(":keyboard:Y4", "\xe2\x86\x93")
  local enter = find_field(":keyboard:Y2", "Enter")

  if atari then atari:set_value(1) end
  emu.wait(0.02)
  tap(skey, 0.02, 0.04)
  if atari then atari:set_value(0) end
  emu.wait(0.04)

  for _ = 1, 5 do
    tap(down, 0.02, 0.04)
  end
  tap(enter, 0.02, 0.08)

  for _ = 1, 2 do
    tap(down, 0.02, 0.04)
  end
  tap(enter, 0.02, 0.2)

  emu.print_info("helpers.run_fileserver: File Transfer Server sequence complete")
end

-- 3x Esc - backs out of the File Transfer Server / System Setup menu
-- back to a plain DOS prompt.
local function exit_fileserver()
  local esc = find_field(":keyboard:Y7", "Esc")

  for _ = 1, 3 do
    tap(esc, 0.02, 0.2)
  end

  emu.print_info("helpers.exit_fileserver: back at DOS prompt")
end

-- Soft-reboots the emulated machine via MAME's own API, equivalent to
-- Ctrl+Alt+Del on real hardware - no need to simulate that key chord.
--
-- WARNING: soft_reset() re-fires the machine reset callback, which is
-- what -autoboot_script hooks into - so it re-runs the *entire* current
-- autoboot script from the top. Confirmed by testing: calling this mid-
-- scenario loops forever (each run re-triggers another reset). Only call
-- this as the LAST step of a scenario script (e.g. to clear memory
-- between independent test runs); never call it and then continue with
-- more steps in the same coroutine.
local function soft_reboot()
  manager.machine:soft_reset()
  emu.print_info("helpers.soft_reboot: soft reset issued")
end

return {
  find_field = find_field,
  tap = tap,
  run_pftd = run_pftd,
  run_fileserver = run_fileserver,
  exit_fileserver = exit_fileserver,
  soft_reboot = soft_reboot,
}
