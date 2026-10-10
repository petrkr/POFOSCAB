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
