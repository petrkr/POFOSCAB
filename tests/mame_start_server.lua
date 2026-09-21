-- mame_start_server.lua - MAME -autoboot_script that drives the Atari
-- Portfolio ROM's File Transfer Server into "Waiting for connection"
-- state, so PFTD (installed via AUTOEXEC.BAT, see the ccma_ram memory
-- card image) has a live Smart Cable handshake loop to respond to.
--
-- Installing PFTD as a TSR is not enough on its own - the ROM's File
-- Transfer Server loop (which actually waits on the wire handshake) only
-- runs once entered manually from the System Setup menu. On real
-- hardware this is: Atari+S (System Setup), then 5x Down, Enter (File
-- transfer), then 2x Down, Enter (Server). This script replicates that
-- exact keystroke sequence programmatically.
--
-- Verified interactively (see project chat history) via screenshot: this
-- sequence reliably lands on "File transfer... Waiting for connection",
-- even at the fast timing used below (~0.5s post-boot delay, ~20-40ms
-- per keystroke) - full sequence completes in under 4 seconds.
--
-- ioport field tags (MAME 0.289, `pofo` driver keyboard matrix):
--   :keyboard:Y0 "Atari", :keyboard:Y4 "s  S" and down-arrow,
--   :keyboard:Y2 "Enter"

-- Optional Average-speed logging, opt-in via env var so the Server-mode
-- navigation below stays unchanged for runs that don't need it (see
-- mame_speed_log.lua).
if os.getenv("POFOSCAB_LOG_SPEED") then
  local dir = debug.getinfo(1, "S").source:match("@(.*/)")
  dofile(dir .. "mame_speed_log.lua")
end

local function find_field(tag, name)
  local port = manager.machine.ioport.ports[tag]
  if not port then
    emu.print_error("mame_start_server: no ioport " .. tag)
    return nil
  end
  local field = port.fields[name]
  if not field then
    emu.print_error("mame_start_server: no field '" .. name .. "' on " .. tag)
  end
  return field
end

local atari = find_field(":keyboard:Y0", "Atari")
local skey  = find_field(":keyboard:Y4", "s  S")
local down  = find_field(":keyboard:Y4", "\xe2\x86\x93")
local enter = find_field(":keyboard:Y2", "Enter")

local function tap(field, hold, gap)
  if field then field:set_value(1) end
  emu.wait(hold)
  if field then field:set_value(0) end
  emu.wait(gap)
end

coroutine.wrap(function()
  -- Let AUTOEXEC.BAT finish installing PFTD before touching the keyboard.
  emu.wait(0.5)

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

  emu.print_info("mame_start_server: File Transfer Server sequence complete")
end)()
