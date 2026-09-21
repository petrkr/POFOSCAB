-- step_soft_reboot.lua - scenario building block wrapping
-- helpers.lua's soft_reboot() (manager.machine:soft_reset(), MAME's own
-- API - no need to simulate a Ctrl+Alt+Del key chord). Returns a
-- function() that does NOT run on its own when dofile()'d - call from
-- within a coroutine (emu.wait needs one).
--
-- WARNING: must be the LAST step called in a scenario - soft_reset()
-- re-fires the machine reset callback and re-runs the current
-- -autoboot_script from the top. Calling it and then continuing with
-- more steps in the same coroutine loops forever (confirmed by testing).

local dir = debug.getinfo(1, "S").source:match("@(.*/)")
local h = dofile(dir .. "helpers.lua")

return h.soft_reboot
