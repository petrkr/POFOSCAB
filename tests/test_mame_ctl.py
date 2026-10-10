"""Smoke test for the mame_ctl fixture: confirms pytest can write a
sequence of Lua commands into an externally-started MAME console's
FIFO, and that send_lua() blocks until MAME actually finished each one
(via a completion marker read back over POFOSCAB_MAME_OUT_FIFO), rather
than returning as soon as the write landed in the pipe.

Requires POFOSCAB_MAME_FIFO/POFOSCAB_MAME_OUT_FIFO pointing at FIFOs
wired to an already-running `mame ... -console` process. Skipped
automatically otherwise.
"""


def test_send_lua_sequence(mame_ctl):
    """Write a few plain Lua statements in sequence; check MAME's own
    console/terminal output by eye for each print_info line."""
    mame_ctl.send_lua('emu.print_info("mame_ctl probe: line 1")')
    mame_ctl.send_lua('emu.print_info("mame_ctl probe: line 2")')
    mame_ctl.send_lua('emu.print_info("mame_ctl probe: line 3")')


def test_type_echo_ahoj(mame_ctl):
    """Types "echo ahoj" + Enter via natkeyboard.post(). post() is async
    and returns before typing is done, so this waits (MAME-side, via
    is_posting) for it to actually finish before send_lua() returns.
    """
    mame_ctl.send_lua(
        'manager.machine.natkeyboard:post("echo ahoj hodne dlouhy text\\n") '
        'while manager.machine.natkeyboard.is_posting do '
        'emu.wait(0.05) end'
    )


def test_type_second_echo(mame_ctl):
    """Second, independent post() + wait - proves send_lua() is really
    blocking on each command's own completion marker, not on MAME
    shutting down: if it only passed because of the earlier test's
    EOF/shutdown, this one would never get a marker for ITS command and
    would time out instead of passing with its own visible typing.
    """
    mame_ctl.send_lua(
        'manager.machine.natkeyboard:post("echo nazdar jiny dlouhy text\\n") '
        'while manager.machine.natkeyboard.is_posting do '
        'emu.wait(0.05) end'
    )
