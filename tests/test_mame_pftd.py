"""MAME-only PFTD tests: things that need direct control of/visibility
into MAME (the -console Lua bridge via mame_ctl), not just the bridge's
/sendRaw HTTP API - so these never run against real Portfolio hardware.
Skipped automatically when MAME_DIR/the FIFOs aren't configured (same
skip mechanism as mame_ctl itself).

Usage:
  MAME_DIR=/tmp/fifodir pytest tests/test_mame_pftd.py -v
"""

import json
import os
import re
import urllib.request

import pytest

from conftest import config, wait_for_status


@pytest.fixture(autouse=True)
def _ensure_mame_setup(request):
    """Bring MAME up to the INITIAL/UPLOAD/SERVER/PFTD state (see
    mame_setup in conftest.py) before any test in this module runs -
    session-scoped, so this is a no-op if test_pftd.py's setup_tests
    (or an earlier module) already ran it, but still required here on
    its own: pytest module/test ORDER isn't guaranteed, and without
    this, running test_mame_pftd.py first (or alone) hits PFTD not
    being up yet instead of mame_setup bringing it up.
    """
    request.getfixturevalue("mame_setup")


def get_status(base_url: str) -> dict:
    with urllib.request.urlopen(f"{base_url}/status", timeout=10) as resp:
        return json.loads(resp.read().decode())


def send_raw(base_url: str, hexdata: str) -> str:
    import urllib.parse
    data = urllib.parse.urlencode({"data": hexdata}).encode()
    req = urllib.request.Request(f"{base_url}/sendRaw", data=data, method="POST")
    with urllib.request.urlopen(req, timeout=20) as resp:
        body = resp.read().decode().strip()
    try:
        parsed = json.loads(body)
    except json.JSONDecodeError:
        return body
    if isinstance(parsed, dict) and "response" in parsed:
        return parsed["response"]
    return body


def draw_ascii(base_url: str, row: int, col: int, text: str) -> str:
    """DRAW_ASCII (0x8F) - see src/pftd/drawascii.inc. Writes `text` at
    (row, col) on the LCD. Rows 0,1,5,6,7 and cols 1-38 are safe (rows
    2-4 get overwritten by the ROM's own File Transfer Server screen;
    row 0/7 and col 0/39 are PFTD's own border, drawable but reserved
    for a future status bar, not general content).
    """
    payload = bytes([row, col]) + text.encode('ascii') + b'\x00'
    return send_raw(base_url, (bytes([0x8F, 0x00, 0x70]) + payload).hex())


def _read_build_id() -> int:
    """Source of truth for "did we actually boot the build we think we
    did" - src/pftd/build_id.inc, not the running binary (see Error 5
    kind of mixup: uploading a fresh build.COM doesn't catch a stale
    build still resident from a previous run - comparing against the
    source file does).
    """
    path = os.path.join(config.PROJECT_ROOT, 'src', 'pftd', 'build_id.inc')
    with open(path) as f:
        text = f.read()
    m = re.search(r'BUILD_ID\s+equ\s+0x([0-9A-Fa-f]+)', text)
    assert m, f"Couldn't find BUILD_ID in {path}"
    return int(m.group(1), 16)


def test_hello_matches_vram_and_source(mame_ctl, artifacts):
    """HELLO's buildId (what the bridge parses from the wire protocol)
    must match both:
    - what PFTD actually prints to the LCD at startup (VRAM) - proves
      HELLO's reported version wasn't desynced from what's rendered
      (e.g. a banner-format change that forgot to also update hello.inc)
    - src/pftd/build_id.inc - proves the resident PFTD is actually the
      build this checkout expects, not a stale one left over from a
      previous run/card upload (observed: a re-run picking up an old
      card-resident PFTD.COM instead of the freshly uploaded one).

    Always saves a screenshot + VRAM dump under MAME_DIR/artifacts/,
    pass or fail - unlike other MAME-driven steps (e.g. mame_setup's
    intermediate states), this test's whole subject IS what's on the
    LCD/in VRAM, so it's always worth having something to look at, not
    just when something goes wrong.
    """
    # mame_setup may have been a no-op (e.g. the "everything already set
    # up manually" case - all SKIP/INITIAL/UPLOAD/SERVER/PFTD flags left
    # at their defaults), which doesn't mean PFTD is necessarily ready to
    # answer yet - wait for /status to actually report it instead of
    # checking once immediately, before capturing anything.
    wait_for_status(config.BRIDGE_URL, connected=True, pftd=True)

    artifacts.screenshot()
    vram = mame_ctl.dump_vram()
    artifacts.save_vram(vram)
    text = vram.decode('ascii', errors='replace')

    st = get_status(config.BRIDGE_URL)
    pftd = st.get("pftd")
    assert pftd, "PFTD not detected by /status - is it running?"

    hello_build_id = pftd["buildId"]
    hello_version = pftd["version"]

    expected_build_id = _read_build_id()
    assert hello_build_id == expected_build_id, (
        f"HELLO reports buildId {hello_build_id:08x}, but "
        f"src/pftd/build_id.inc says {expected_build_id:08x} - "
        f"resident PFTD is not this checkout's build (stale upload?)"
    )

    # PFTD.asm prints "PFTD v{MAJOR}.{MINOR}.{PATCH} (" then BUILD_ID
    # as 8 lowercase hex digits (print_hex32) - same 32-bit value
    # HELLO reports as buildId, just hex text instead of a wire int.
    expected_banner = f"PFTD v{hello_version} ({hello_build_id:08x}"
    assert expected_banner in text, (
        f"Expected {expected_banner!r} in VRAM, got: {text!r}"
    )


def test_draw_ascii_renders_to_vram(mame_ctl, artifacts):
    """DRAW_ASCII (0x8F) writes land where they're supposed to - checks
    a few representative positions across the supported region (rows
    0,1,5,6,7; cols 1-38 - see draw_ascii()'s docstring) rather than
    every cell, by writing a distinct marker string per position and
    confirming each one shows up in a single post-write VRAM dump.
    """
    wait_for_status(config.BRIDGE_URL, connected=True, pftd=True)

    # col + len(text) must stay <= 38 (inclusive) - gui_print_si's
    # AH=0x09 doesn't wrap, but a write past col 39 runs off this
    # screen's visible text area (and risks landing in/over PFTD's own
    # border column at 39).
    markers = {
        (0, 1): "R0C1",
        (1, 1): "R1C1",
        (5, 20): "R5C20",
        (6, 20): "R6C20",
        (7, 34): "R7C34",
    }
    screens = []
    for (row, col), text in markers.items():
        r = draw_ascii(config.BRIDGE_URL, row, col, text)
        assert r[:2].lower() == "20", f"DRAW_ASCII({row},{col}) failed: {r}"
        # The wire response comes back once gui_print has returned, but
        # the LCD device's own rendering can lag a frame or two behind
        # the VRAM write itself - settle() waits inside MAME (not a
        # guessed Python-side sleep) before the next write/the dump.
        mame_ctl.settle(0.05)

        # Capture after each write (not just once at the end) so the
        # progression is visible, not just the final state.
        artifacts.screenshot()
        vram = mame_ctl.dump_vram()
        artifacts.save_vram(vram)
        screens.append(vram.decode('ascii', errors='replace'))

    final_screen = screens[-1]
    for (row, col), text in markers.items():
        assert text in final_screen, (
            f"Expected {text!r} (written at row={row}, col={col}) in "
            f"VRAM, got: {final_screen!r}"
        )
