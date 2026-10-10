"""Pytest configuration and fixtures for POFOSCAB tests.

Pytest only ever talks to an already-running bridge at POFOSCAB_BRIDGE_URL.
It never starts MAME, the bridge process, or builds/uploads PFTD.COM -
that's all external setup the user does before running pytest.
"""

import os
import queue
import threading
import time
import uuid
import json
import pytest


# ============================================================================
# Configuration
# ============================================================================

class TestConfig:
    """Test configuration from environment variables."""

    PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    BRIDGE_URL = os.getenv('POFOSCAB_BRIDGE_URL', 'http://localhost:9000')

    # Paths to FIFOs wired to an externally-started `mame ... -console`
    # process's stdin/stdout. Set by whatever step/user started MAME -
    # pytest never starts MAME itself, only talks to these pipes.
    MAME_FIFO = os.getenv('POFOSCAB_MAME_FIFO')
    MAME_OUT_FIFO = os.getenv('POFOSCAB_MAME_OUT_FIFO')


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
    process's stdin FIFO, and blocks each call until MAME has actually
    finished running it - confirmed via a per-command marker read back
    from MAME's stdout FIFO, not a guessed time.sleep(). This matters
    for commands like natkeyboard:post(), which returns to Lua
    immediately and types in the background: without reading a
    completion marker back, send_lua() would return as soon as the
    write() landed in the pipe, regardless of whether MAME had started,
    let alone finished, running it.

    Keeps a single long-lived file descriptor open for each FIFO rather
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

    def __init__(self, fifo_path: str, out_fifo_path: str, timeout: float = 30.0):
        self._loaded_helpers = False
        self._timeout = timeout
        self._in = open(fifo_path, 'w')
        self._out = open(out_fifo_path, 'r')
        self._lines: queue.Queue[str] = queue.Queue()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def _read_loop(self) -> None:
        for line in self._out:
            self._lines.put(line.rstrip('\n'))

    def close(self) -> None:
        self._in.close()
        self._out.close()

    def send_lua(self, code: str) -> None:
        """Run a Lua statement in MAME and block until it has finished.

        Appends a print() of a unique marker after the given code, in
        the same statement, so the marker only appears on MAME's stdout
        once everything before it (including any emu.wait()/polling
        loop inside `code`) has completed.
        """
        marker = f"MAMECTL_DONE:{uuid.uuid4().hex}"
        self._in.write(code.rstrip('\n') + f' print("{marker}")\n')
        self._in.flush()
        deadline = time.monotonic() + self._timeout
        while time.monotonic() < deadline:
            try:
                line = self._lines.get(timeout=0.5)
            except queue.Empty:
                continue
            if line == marker:
                return
        raise TimeoutError(f"MAME did not finish running: {code!r}")

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
        """Exits MAME. Fire-and-forget, unlike send_lua(): MAME exiting
        means it never gets to print a completion marker back, so
        waiting for one here would always time out.
        """
        if not self._loaded_helpers:
            self._in.write(f'h = dofile("{self.HELPERS_LUA}")\n')
        self._in.write('h.shutdown()\n')
        self._in.flush()


@pytest.fixture(scope="session")
def mame_ctl(cfg):
    """Provide MAME console control, if POFOSCAB_MAME_FIFO and
    POFOSCAB_MAME_OUT_FIFO are set.

    Skips any test that requests this fixture when the FIFOs aren't
    configured, instead of failing - e.g. on the hardware backend,
    where there's no MAME to control.
    """
    if not cfg.MAME_FIFO or not cfg.MAME_OUT_FIFO:
        pytest.skip(
            "POFOSCAB_MAME_FIFO/POFOSCAB_MAME_OUT_FIFO not set - no "
            "externally-started MAME console to control"
        )
    ctl = MameCtl(cfg.MAME_FIFO, cfg.MAME_OUT_FIFO)
    yield ctl
    try:
        ctl.shutdown()
    finally:
        ctl.close()
