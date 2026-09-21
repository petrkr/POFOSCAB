# POFOSCAB Tests

Tests are organized as pytest fixtures with configurable backends. See the root
[README.md](../README.md) for complete instructions.

## Quick start

### Headless MAME (automatic, default)
```bash
pytest -v
```

This will:
1. Build PFTD.COM (root `make pftd`)
2. Start MAME with smartcable expansion + autoboot script
3. Start Python bridge
4. Upload PFTD.COM to Portfolio
5. Run 16+ tests against the Portfolio

### Skip specific steps

```bash
# Skip build (binary already exists)
SKIP_BUILD=1 pytest -v

# Skip upload (PFTD already on card)
SKIP_UPLOAD=1 pytest -v

# Skip both
SKIP_BUILD=1 SKIP_UPLOAD=1 pytest -v
```

### Manual MAME UI mode

Start MAME and bridge in separate terminals, then run:
```bash
POFOSCAB_BACKEND=mame_manual pytest -v
```

Terminal 1 (MAME):
```bash
cd /home/petrkr/git/mame
./mame pofo -ccma ram -exp smartcable \
  -autoboot_script /home/petrkr/git/POFOSCAB/tests/lua/flow_build_upload_pftd.lua \
  -seconds_to_run 300 -skip_gameinfo
```

Terminal 2 (bridge):
```bash
python3 tests/mame_bridge.py
```

Terminal 3 (tests):
```bash
POFOSCAB_BACKEND=mame_manual pytest -v
```

### Real hardware

Replace MAME with real Portfolio + ESP32 Smart Cable:
```bash
POFOSCAB_BACKEND=hardware POFOSCAB_BRIDGE_URL=http://10.220.179.55 pytest -v
```

## Environment variables

- `POFOSCAB_BACKEND` - Backend mode: `mame_auto` (default), `mame_manual`, `hardware`
- `POFOSCAB_BRIDGE_URL` - Bridge base URL (default: `http://localhost:9000`)
- `POFOSCAB_BRIDGE_PORT` - Bridge HTTP port (default: 9000)
- `SKIP_BUILD` - Set to 1 to skip build step
- `SKIP_UPLOAD` - Set to 1 to skip PFTD upload
- `SKIP_ESCAPE` - Set to 1 to skip escape sequence (not yet implemented)
- `MAME_BIN` - Path to MAME binary (default: `mame` in PATH)
- `MAME_PATH` - Path to MAME repository (default: `/home/petrkr/git/mame`)

## Files

- `conftest.py` - Pytest configuration and fixtures
- `pytest.ini` - Pytest settings
- `test_integration.py` - All test cases (HELLO, MKDIR, COPY, etc.)
- `mame_bridge.py` - Python bridge to smartcable device
- `lua/` - Lua autoboot scripts for MAME

## Test structure

Tests are organized by functionality:
- `TestBasic` - HELLO, DRIVES, /status
- `TestFileOperations` - MKDIR, RMDIR, LIST, RENAME, DELETE
- `TestFileTransfer` - UPLOAD, DOWNLOAD, COPY
- `TestDateTime` - GETDATETIME, SETDATETIME

All tests use the same `/sendRaw` HTTP API, so they work identically against
MAME (via smartcable device) or real Portfolio hardware (ESP32 client).
