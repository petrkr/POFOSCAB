-- step_init_dip_dos.lua - scenario building block: drives the Atari
-- Portfolio through its first-boot DIP DOS interactive setup, starting
-- from clean NVRAM (no A: card). On real hardware this is:
--   Enter (dismiss the initial DIP DOS screen)
--   "e"   (select language - English)
--   Enter (confirm date)
--   Enter (confirm time)
-- ...landing on a plain DIP DOS prompt. With NVRAM already saved from a
-- prior run, only the first Enter is needed (language/date/time are
-- remembered) - callers that know NVRAM is clean can run the full
-- sequence; callers reusing saved NVRAM should call only the "Enter"
-- part or skip this step entirely.
--
-- ioport field tags: "e  E" and "Enter" are both on :keyboard:Y2,
-- cross-checked against src/mame/atari/pofo_kbd.cpp's PORT_START("Y2")
-- block in the MAME fork (~/git/mame).
--
-- Returns a function() that runs the sequence; does NOT run on its own
-- when dofile()'d. Call from within a coroutine (emu.wait needs one).
--
-- Measured (MAME "Average speed" wall-clock report): the full sequence,
-- from MAME launch to the DOS prompt ("C>") appearing, takes ~3 seconds
-- on clean NVRAM. Useful as a baseline emu.wait() budget for scenario
-- scripts chaining this step with further steps (e.g. run_fileserver).

local dir = debug.getinfo(1, "S").source:match("@(.*/)")
local h = dofile(dir .. "helpers.lua")

return function()
  local enter = h.find_field(":keyboard:Y2", "Enter")
  local ekey  = h.find_field(":keyboard:Y2", "e  E")

  -- On clean NVRAM, MAME/ROM needs time to format C: before the first
  -- DIP DOS screen even appears - confirmed by testing (with a short
  -- wait here, the "e" keypress landed too early and stuck on the
  -- language selection screen).
  emu.wait(1.5)

  -- Dismiss the initial DIP DOS screen.
  h.tap(enter, 0.02, 0.3)

  -- Select language (English).
  h.tap(ekey, 0.02, 0.3)

  -- Confirm date.
  h.tap(enter, 0.02, 0.3)

  -- Confirm time.
  h.tap(enter, 0.02, 0.3)

  emu.print_info("step_init_dip_dos: DIP DOS init sequence complete")
end
