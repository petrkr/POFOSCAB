# POFOSCAB Tests

Pytest talks only to an already-running bridge over HTTP (`POFOSCAB_BRIDGE_URL`).
It never starts MAME, starts the bridge process, or builds/uploads PFTD.COM -
all of that is external setup you do before invoking pytest.

## Quick start

1. Start MAME (or connect real hardware) and the Python bridge yourself.
2. Make sure PFTD.COM is already on the Portfolio if you need PFTD-dependent
   tests (tests that don't need it, like `test_status`, run regardless).
3. Run:

```bash
pytest -v
```

Point at a non-default bridge:
```bash
POFOSCAB_BRIDGE_URL=http://10.220.179.55:9000 pytest -v
```

## Environment variables

- `POFOSCAB_BRIDGE_URL` - Bridge base URL (default: `http://localhost:9000`)
- `POFOSCAB_MAME_FIFO` / `POFOSCAB_MAME_OUT_FIFO` - Paths to FIFOs wired to
  an externally-started `mame ... -console` process's stdin/stdout (see
  "Controlling MAME from tests" below). Unset by default - tests needing
  them are skipped, not failed.
- `SKIP_PFTD_CHECK` - Set to 1 to force PFTD-dependent tests to run even if
  `/status` doesn't report PFTD (useful when debugging the check itself)
- `SKIP_CLEANUP` - Set to 1 to skip best-effort cleanup of test artifacts

## Controlling MAME from tests

Some scenarios need to flip the Portfolio's state mid-test (e.g. stop the
File Transfer Server, run PFTD, start the server again, run a check, shut
down). Pytest doesn't start or own the MAME process for this - as with the
bridge, an external step/user starts MAME themselves, with `-console`
instead of `-autoboot_script`, and wires its stdin/stdout to a pair of
FIFOs:

```bash
mkfifo /tmp/mame_cmd.fifo /tmp/mame_out.fifo
mame pofo -ccma ram -exp smartcable -console \
  < /tmp/mame_cmd.fifo > /tmp/mame_out.fifo 2>&1 &
```

Keep both FIFOs open on pytest's side for as long as MAME should stay
controllable - closing the input FIFO's only writer sends stdin EOF, and
MAME's console reader won't accept a second writer afterwards (confirmed:
a FIFO reader exits on the first writer's close, and a later `open()` for
writing then blocks forever with no reader left).

Point pytest at both FIFOs:
```bash
POFOSCAB_MAME_FIFO=/tmp/mame_cmd.fifo \
POFOSCAB_MAME_OUT_FIFO=/tmp/mame_out.fifo \
POFOSCAB_BRIDGE_URL=http://localhost:9000 pytest -v
```

The `mame_ctl` fixture (`conftest.py`) opens both FIFOs once per test
session and exposes `run_fileserver()`, `exit_fileserver()`, `run_pftd()`,
`soft_reboot()`, `shutdown()` - thin wrappers sending the corresponding
`tests/lua/helpers.lua` call via `send_lua()`. `send_lua()` is synchronous:
it appends a `print()` of a unique marker to the statement it sends, and
blocks (via a background reader thread on the output FIFO) until that
marker comes back, so it only returns once MAME has actually finished
running the statement - including any `emu.wait()` or polling loop inside
it. This matters for calls like `natkeyboard:post()`, which returns to Lua
immediately and types in the background: without reading a completion
marker back, pytest would move on before MAME finished, and nothing short
of a guessed `time.sleep()` could compensate.

If the FIFOs aren't set, any test requesting `mame_ctl` is skipped (e.g.
on the hardware backend, where there's no MAME to control).

At the end of the pytest session, the fixture's teardown sends `shutdown()`
(`manager.machine:exit()`) - fire-and-forget, since MAME exiting means it
never gets to print a completion marker back - before closing both FIFOs.
Don't point `POFOSCAB_MAME_FIFO`/`POFOSCAB_MAME_OUT_FIFO` at a MAME
instance another step/job still needs afterwards.

## PFTD not running

If `/status` doesn't report PFTD as present, PFTD-dependent tests are
skipped automatically (`require_pftd` fixture in `test_integration.py`) -
their cleanup can't run without PFTD either, so skipping is correct, not
just "best effort". Only `test_status` (marked `no_pftd_required`) runs
without PFTD.

## Files

- `conftest.py` - Pytest configuration and fixtures (bridge URL only)
- `pytest.ini` - Pytest settings
- `test_integration.py` - All test cases (HELLO, MKDIR, COPY, etc.)
- `test_bridge_link.py` - Unit tests for the bridge's link/reconnect logic
  (no MAME/bridge/hardware needed - uses a fake TCP listener)
- `lua/` - Lua autoboot scripts for MAME (used when you set up MAME yourself)
- `../tools/mame_bridge.py` - Python bridge to the smartcable device (start
  this yourself before running pytest)

## Test structure

Tests are organized by functionality:
- `TestBasic` - HELLO, DRIVES, /status
- `TestFileOperations` - MKDIR, RMDIR, LIST, RENAME, DELETE
- `TestFileTransfer` - UPLOAD, DOWNLOAD, COPY
- `TestDateTime` - GETDATETIME, SETDATETIME

All tests use the same `/sendRaw` HTTP API, so they work identically against
MAME (via smartcable device) or real Portfolio hardware (ESP32 client) -
pytest doesn't know or care which one is behind the bridge.
