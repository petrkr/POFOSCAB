"""Pytest configuration and fixtures for POFOSCAB tests.

Pytest only ever talks to an already-running bridge at POFOSCAB_BRIDGE_URL.
It never starts MAME, the bridge process, or builds/uploads PFTD.COM -
that's all external setup the user does before running pytest.
"""

import os
import time
import json
import pytest


# ============================================================================
# Configuration
# ============================================================================

class TestConfig:
    """Test configuration from environment variables."""

    PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    BRIDGE_URL = os.getenv('POFOSCAB_BRIDGE_URL', 'http://localhost:9000')

    # Path to a FIFO feeding an externally-started `mame ... -console`
    # process's stdin. Set by whatever step/user started MAME - pytest
    # never starts MAME itself, only writes Lua commands into this pipe.
    MAME_FIFO = os.getenv('POFOSCAB_MAME_FIFO')


config = TestConfig()


def wait_for_http(url: str, timeout: int = 30) -> bool:
    """Wait for HTTP endpoint to respond."""
    import urllib.request
    start = time.time()
    while time.time() - start < timeout:
        try:
            urllib.request.urlopen(f"{url}/status", timeout=2)
            return True
        except Exception:
            time.sleep(0.1)
    return False


# ============================================================================
# Fixtures
# ============================================================================

@pytest.fixture(scope="session")
def cfg():
    """Provide test configuration."""
    return config


class MameCtl:
    """Writes Lua commands into an externally-started `mame -console`
    process's stdin FIFO. Fire-and-forget - callers verify effects via
    the bridge's /status endpoint, not via any response read back here.

    Keeps a single long-lived file descriptor open for the FIFO rather
    than opening/closing per command: MAME's -console reads stdin as one
    continuous stream for the life of the process, and a FIFO writer
    that closes after each write sends EOF to that reader - the next
    open() then blocks forever with no reader left to pair with
    (confirmed: a `cat fifo &` reader exits after the first writer
    closes, so a second writer's open() hangs).
    """

    HELPERS_LUA = os.path.join(
        TestConfig.PROJECT_ROOT, 'tests', 'lua', 'helpers.lua'
    )

    def __init__(self, fifo_path: str):
        self._fifo_path = fifo_path
        self._loaded_helpers = False
        self._fh = open(fifo_path, 'w')

    def close(self) -> None:
        self._fh.close()

    def send_lua(self, code: str) -> None:
        """Write a Lua statement to the MAME console FIFO."""
        self._fh.write(code.rstrip('\n') + '\n')
        self._fh.flush()

    def _send_helper(self, call: str) -> None:
        if not self._loaded_helpers:
            self.send_lua(f'h = dofile("{self.HELPERS_LUA}")')
            self._loaded_helpers = True
        self.send_lua(call)

    def run_fileserver(self) -> None:
        self._send_helper('h.run_fileserver()')

    def exit_fileserver(self) -> None:
        self._send_helper('h.exit_fileserver()')

    def run_pftd(self) -> None:
        self._send_helper('h.run_pftd()')

    def soft_reboot(self) -> None:
        self._send_helper('h.soft_reboot()')
        self._loaded_helpers = False  # soft reset re-runs from a clean Lua state

    def shutdown(self) -> None:
        self._send_helper('h.shutdown()')


@pytest.fixture(scope="session")
def mame_ctl(cfg):
    """Provide MAME console control, if POFOSCAB_MAME_FIFO is set.

    Skips any test that requests this fixture when the FIFO isn't
    configured, instead of failing - e.g. on the hardware backend,
    where there's no MAME to control.
    """
    if not cfg.MAME_FIFO:
        pytest.skip(
            "POFOSCAB_MAME_FIFO not set - no externally-started MAME "
            "console to control"
        )
    ctl = MameCtl(cfg.MAME_FIFO)
    yield ctl
    ctl.close()
