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

    # Directory holding the two FIFOs wired to an externally-started
    # `mame ... -console` process's stdin/stdout (mame_in, mame_out). Set
    # by whatever step/user started MAME - pytest never starts MAME
    # itself, only talks to these pipes.
    MAME_DIR = os.getenv('MAME_DIR')
    MAME_FIFO = os.path.join(MAME_DIR, 'mame_in') if MAME_DIR else None
    MAME_OUT_FIFO = os.path.join(MAME_DIR, 'mame_out') if MAME_DIR else None

    # Each defaults to "already done, don't do it again" (0) - manual
    # runs normally point at a MAME that's already past the wizard, with
    # PFTD uploaded and running under the fileserver, so the common case
    # needs no env vars at all. CI explicitly sets "1" on whichever steps
    # it needs done from scratch.
    INITIAL = os.getenv('INITIAL', '0') == '1'
    FORMAT_A = os.getenv('FORMAT_A', '0') == '1'
    UPLOAD = os.getenv('UPLOAD', '0') == '1'
    SERVER = os.getenv('SERVER', '0') == '1'
    PFTD = os.getenv('PFTD', '0') == '1'

    PFTD_BIN = os.path.join(PROJECT_ROOT, 'build', 'PFTD.COM')


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


def _get_status(bridge_url: str) -> dict | None:
    import urllib.request
    try:
        with urllib.request.urlopen(f"{bridge_url}/status", timeout=2) as resp:
            return json.loads(resp.read().decode())
    except Exception:
        return None


def wait_for_status(bridge_url: str, *, connected: bool | None = None,
                     pftd: bool | None = None, timeout: float = 15.0) -> None:
    """Poll /status until it reports the given connected/pftd values.

    The bridge's TCP link to MAME's smartcable device (and the ROM's
    broadcast-detection loop behind it) takes a moment to (re)settle
    after a Lua-driven state change (entering/leaving the fileserver,
    starting PFTD) - polling /status here, instead of guessing a fixed
    time.sleep(), is what replaces that guess.
    """
    deadline = time.monotonic() + timeout
    last = None
    while time.monotonic() < deadline:
        last = _get_status(bridge_url)
        if last is not None:
            if connected is not None and bool(last.get("connected")) != connected:
                pass
            elif pftd is not None and bool(last.get("pftd")) != pftd:
                pass
            else:
                return
        time.sleep(0.2)
    pytest.fail(
        f"/status didn't reach connected={connected} pftd={pftd} "
        f"within {timeout}s (last seen: {last})"
    )


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
        # A plain open(path, 'w') on a FIFO blocks forever if nothing has
        # it open for reading - if MAME isn't actually running/attached,
        # this would hang indefinitely instead of failing. Open
        # non-blocking first (raises ENXIO immediately with no reader)
        # to fail fast, then reopen blocking for actual use.
        try:
            fd = os.open(fifo_path, os.O_WRONLY | os.O_NONBLOCK)
        except OSError as exc:
            pytest.fail(
                f"Can't open {fifo_path} for writing - no reader attached "
                f"(is MAME actually running with -console on this FIFO?): {exc}"
            )
        os.close(fd)
        self._in = open(fifo_path, 'w')
        self._out = open(out_fifo_path, 'r')
        self._lines: queue.Queue[str] = queue.Queue()
        self._reader = threading.Thread(target=self._read_loop, daemon=True)
        self._reader.start()

    def _read_loop(self) -> None:
        try:
            for line in self._out:
                self._lines.put(line.rstrip('\n'))
        except ValueError:
            pass  # self._out was closed from the main thread (close()) while this blocking read was in progress - expected on teardown, not an error

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

    def send_lua_result(self, expr: str) -> str:
        """Like send_lua(), but for an expression that returns a value:
        prints it on its own line right before the completion marker,
        and returns that line instead of just blocking until done.
        """
        marker = f"MAMECTL_DONE:{uuid.uuid4().hex}"
        self._in.write(f'print({expr}) print("{marker}")\n')
        self._in.flush()
        deadline = time.monotonic() + self._timeout
        result = None
        while time.monotonic() < deadline:
            try:
                line = self._lines.get(timeout=0.5)
            except queue.Empty:
                continue
            if line == marker:
                if result is None:
                    raise TimeoutError(f"no result line before marker for: {expr!r}")
                return result
            result = line
        raise TimeoutError(f"MAME did not finish running: {expr!r}")

    def _send_helper(self, call: str) -> None:
        if not self._loaded_helpers:
            self.send_lua(f'h = dofile("{self.HELPERS_LUA}")')
            self._loaded_helpers = True
        self.send_lua(call)

    def _send_helper_result(self, expr: str) -> str:
        if not self._loaded_helpers:
            self.send_lua(f'h = dofile("{self.HELPERS_LUA}")')
            self._loaded_helpers = True
        return self.send_lua_result(expr)

    def format_a(self) -> None:
        self._send_helper('h.format_a()')

    def run_fileserver(self) -> None:
        self._send_helper('h.run_fileserver()')

    def exit_fileserver(self) -> None:
        self._send_helper('h.exit_fileserver()')

    def settle(self, seconds: float = 0.5) -> None:
        """Wait inside MAME (not a Python-side guess) for the machine to
        settle right after boot, before the first real command - keys
        pressed immediately after boot can be missed/mis-timed.
        """
        self.send_lua(f'emu.wait({seconds})')

    def run_initial(self) -> None:
        self._send_helper('h.run_initial()')

    def run_pftd(self) -> None:
        self._send_helper('h.run_pftd()')

    def soft_reboot(self) -> None:
        self._send_helper('h.soft_reboot()')
        self._loaded_helpers = False  # soft reset re-runs from a clean Lua state

    def screenshot(self, path: str | None = None) -> None:
        """Save a PNG of the emulated screen. With no path, MAME picks
        the next free name under its own -snapshot_directory; pass an
        absolute path to name it after the calling test/step.
        """
        arg = f'"{path}"' if path else ''
        self._send_helper(f'h.screenshot({arg})')

    def dump_memory(self, tag: str, space_name: str, addr: int, length: int) -> bytes:
        """Read `length` bytes from `addr` in the named address space of
        the device at `tag` (e.g. ":u1"/"program" for main CPU memory).
        """
        hexstr = self._send_helper_result(
            f'h.dump_memory("{tag}", "{space_name}", {addr}, {length})'
        )
        return bytes.fromhex(hexstr)

    def dump_vram(self, length: int = 320, save_path: str | None = None) -> bytes:
        """Read the HD61830 LCD controller's video RAM (its own
        device-local address space, distinct from main CPU memory).
        Default length is 320 (40 cols x 8 rows text mode - the visible
        display content). With save_path, also writes the raw bytes
        there (e.g. for attaching to a test failure) in addition to
        returning them.
        """
        hexstr = self._send_helper_result(f'h.dump_vram({length})')
        data = bytes.fromhex(hexstr)
        if save_path:
            with open(save_path, 'wb') as f:
                f.write(data)
        return data

    def shutdown(self) -> None:
        """Exits MAME. Fire-and-forget, unlike send_lua(): MAME exiting
        means it never gets to print a completion marker back, so
        waiting for one here would always time out.
        """
        if not self._loaded_helpers:
            self._in.write(f'h = dofile("{self.HELPERS_LUA}")\n')
        self._in.write('h.shutdown()\n')
        self._in.flush()


def _setup_screenshot(cfg, mame_ctl, name: str) -> None:
    """Save a screenshot under MAME_DIR/artifacts/mame_setup/ during
    mame_setup - a no-op if MAME_DIR somehow isn't set (mame_setup
    always has mame_ctl, so MAME_DIR is set, but guard anyway since
    this isn't the per-test `artifacts` fixture).
    """
    if not cfg.MAME_DIR:
        return
    out_dir = os.path.join(cfg.MAME_DIR, 'artifacts', 'mame_setup')
    os.makedirs(out_dir, exist_ok=True)
    mame_ctl.screenshot(os.path.join(out_dir, f'{name}.png'))


def _upload_pftd(bridge_url: str) -> None:
    """Upload the already-built build/PFTD.COM via the bridge's /upload
    endpoint - pytest never builds it, only uploads what's already there.
    """
    import urllib.parse
    import urllib.request
    import uuid as uuid_mod

    if not os.path.isfile(TestConfig.PFTD_BIN):
        pytest.fail(
            f"UPLOAD=1 but {TestConfig.PFTD_BIN} doesn't exist - "
            f"build it first (pytest never builds PFTD.COM itself)"
        )
    with open(TestConfig.PFTD_BIN, 'rb') as f:
        data = f.read()
    boundary = uuid_mod.uuid4().hex
    body = (
        f"--{boundary}\r\n"
        f'Content-Disposition: form-data; name="file"; filename="PFTD.COM"\r\n'
        f"Content-Type: application/octet-stream\r\n\r\n"
    ).encode() + data + f"\r\n--{boundary}--\r\n".encode()
    url = f"{bridge_url}/upload?{urllib.parse.urlencode({'destDir': 'C:\\'})}"
    req = urllib.request.Request(url, data=body, method="POST")
    req.add_header("Content-Type", f"multipart/form-data; boundary={boundary}")
    with urllib.request.urlopen(req, timeout=30) as resp:
        result = json.loads(resp.read().decode())
    if not result.get("ok"):
        pytest.fail(f"PFTD.COM upload failed: {result}")


@pytest.fixture(scope="session")
def mame_setup(cfg, mame_ctl):
    """Bring an externally-started MAME console up to the state tests
    expect, driven by INITIAL/FORMAT_A/UPLOAD/SERVER/PFTD - each
    defaults to "0" (already done, see TestConfig), so a manual run
    against a MAME that's already past the wizard with PFTD uploaded
    and running needs no env vars at all and this is a no-op.

    FORMAT_A=1 formats the A: memory card (needed once on a clean
    MAME environment - its ccma_ram image starts out unformatted,
    which fails any write to A: with errcode 1, not a code bug - see
    test_pftd.py's test_copy_cross_drive). Runs right after the
    INITIAL/settle step, before anything that writes to A:.

    Two distinct paths:

    - UPLOAD=1: PFTD.COM isn't on the card yet, so it needs the ROM
      fileserver up to receive it. Sequence: run_fileserver -> upload ->
      exit_fileserver -> (if PFTD=1) run_pftd -> run_fileserver.
    - UPLOAD=0: PFTD.COM is already on the card from a previous run, so
      PFTD can be started directly from the DOS prompt without ever
      entering the server menu. Sequence: (if PFTD=1) run_pftd -> (if
      SERVER=1) run_fileserver.

    Not autouse - only requested by test modules/fixtures that actually
    need MAME under control (via mame_ctl), so tests that don't touch
    MAME (e.g. test_bridge_link.py) never pull in a FIFO-not-configured
    skip that has nothing to do with them.

    Each step waits on /status before the next one starts, instead of a
    guessed time.sleep() - the bridge's TCP link to MAME needs a moment
    to (re)settle after the ROM state actually changes.
    """
    if cfg.INITIAL:
        mame_ctl.run_initial()
    else:
        # run_initial() already waits for the machine to settle on its
        # own (step_init_dip_dos.lua's 1.5s wait). Without it, this is
        # the very first command sent after boot - give MAME a moment
        # to be ready to receive input, or the first keypress can be
        # missed/mis-timed (observed: run_fileserver's Atari+S landing
        # as plain "S").
        mame_ctl.settle()

    if cfg.FORMAT_A:
        mame_ctl.format_a()

    if cfg.UPLOAD:
        mame_ctl.run_fileserver()
        wait_for_status(cfg.BRIDGE_URL, connected=True)
        _upload_pftd(cfg.BRIDGE_URL)
        mame_ctl.exit_fileserver()
        if cfg.PFTD:
            mame_ctl.run_pftd()
            _setup_screenshot(cfg, mame_ctl, "after_run_pftd_before_fileserver")
            mame_ctl.run_fileserver()
            wait_for_status(cfg.BRIDGE_URL, connected=True, pftd=True)
    else:
        if cfg.PFTD:
            mame_ctl.run_pftd()
        if cfg.SERVER:
            if cfg.PFTD:
                _setup_screenshot(cfg, mame_ctl, "after_run_pftd_before_fileserver")
            mame_ctl.run_fileserver()
            wait_for_status(cfg.BRIDGE_URL, connected=True, pftd=cfg.PFTD or None)


@pytest.fixture(scope="session")
def mame_ctl(cfg):
    """Provide MAME console control, if MAME_DIR is set.

    Skips any test that requests this fixture when the FIFO directory
    isn't configured, instead of failing - e.g. on the hardware backend,
    where there's no MAME to control.
    """
    if not cfg.MAME_DIR:
        pytest.skip(
            "MAME_DIR not set - no externally-started "
            "MAME console to control"
        )
    if not os.path.exists(cfg.MAME_FIFO) or not os.path.exists(cfg.MAME_OUT_FIFO):
        pytest.skip(
            f"MAME_DIR={cfg.MAME_DIR} is set but {cfg.MAME_FIFO}/"
            f"{cfg.MAME_OUT_FIFO} don't exist - no FIFOs to control MAME "
            f"with (stale MAME_DIR from the environment?)"
        )
    ctl = MameCtl(cfg.MAME_FIFO, cfg.MAME_OUT_FIFO)
    yield ctl
    try:
        ctl.shutdown()
    finally:
        ctl.close()


def _short_test_name(nodeid: str) -> str:
    """The part of a pytest nodeid after the last '::' (e.g. just
    "test_hello" or "TestBasic.test_hello" for a class-based test) -
    short and filesystem-safe, dropping the file path and "::"
    separators a full nodeid has.
    """
    return nodeid.rsplit("::", 1)[-1].replace("::", ".")


class _NullArtifacts:
    """No-op stand-in for Artifacts when MAME_DIR isn't set - calling
    screenshot()/dump_vram() on a test that doesn't require mame_ctl
    directly should do nothing, not fail or skip the test.
    """

    def screenshot(self) -> None:
        pass

    def dump_vram(self) -> None:
        pass

    def save_vram(self, data: bytes) -> None:
        pass


class Artifacts:
    """Saves a screenshot/VRAM dump for the current test under
    MAME_DIR/artifacts/<short test name>/ - for CI to pick up as build
    artifacts, or for a human to inspect after a failure. Only created
    when MAME_DIR is set; see the artifacts fixture.
    """

    def __init__(self, mame_ctl: "MameCtl", base_dir: str, name: str):
        self._ctl = mame_ctl
        self._dir = os.path.join(base_dir, 'artifacts', name)
        os.makedirs(self._dir, exist_ok=True)
        self._screenshot_n = 0
        self._vram_n = 0

    def screenshot(self) -> None:
        """Each call gets its own numbered file (screenshot_1.png,
        screenshot_2.png, ...) - a test calling this more than once
        (e.g. one shot before and one after some action) would
        otherwise silently overwrite the earlier capture.
        """
        self._screenshot_n += 1
        self._ctl.screenshot(os.path.join(self._dir, f'screenshot_{self._screenshot_n}.png'))

    def dump_vram(self) -> None:
        self._vram_n += 1
        self._ctl.dump_vram(save_path=os.path.join(self._dir, f'vram_{self._vram_n}.bin'))

    def save_vram(self, data: bytes) -> None:
        """Like dump_vram(), but for bytes the caller already has (e.g.
        from its own mame_ctl.dump_vram() call it needs for an assert
        anyway) - avoids reading VRAM from MAME a second time. Numbered
        the same way as dump_vram().
        """
        self._vram_n += 1
        with open(os.path.join(self._dir, f'vram_{self._vram_n}.bin'), 'wb') as f:
            f.write(data)


@pytest.fixture
def artifacts(cfg, request):
    """Provide an Artifacts helper for the current test if MAME_DIR is
    set, or a no-op stand-in otherwise - so calling artifacts.screenshot()
    is always safe, with or without MAME under control. Does NOT skip
    the test, unlike mame_ctl - a test that only optionally wants
    artifacts (e.g. normal /sendRaw tests that also run on real
    hardware) must still run without MAME_DIR.
    """
    if not cfg.MAME_DIR or not os.path.exists(cfg.MAME_FIFO) or not os.path.exists(cfg.MAME_OUT_FIFO):
        return _NullArtifacts()
    ctl = request.getfixturevalue("mame_ctl")
    return Artifacts(ctl, cfg.MAME_DIR, _short_test_name(request.node.nodeid))
