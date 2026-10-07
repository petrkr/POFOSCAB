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
- `SKIP_PFTD_CHECK` - Set to 1 to force PFTD-dependent tests to run even if
  `/status` doesn't report PFTD (useful when debugging the check itself)
- `SKIP_CLEANUP` - Set to 1 to skip best-effort cleanup of test artifacts

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
