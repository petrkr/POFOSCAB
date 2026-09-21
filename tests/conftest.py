"""Pytest configuration and fixtures for POFOSCAB tests."""

import os
import subprocess
import time
import socket
import json
import pytest


# ============================================================================
# Configuration
# ============================================================================

class TestConfig:
    """Test configuration from environment variables."""

    # Backend: 'mame_auto' (headless), 'mame_manual' (UI), 'hardware'
    BACKEND = os.getenv('POFOSCAB_BACKEND', 'mame_auto')

    # Steps to skip/run
    SKIP_BUILD = os.getenv('SKIP_BUILD', '0') == '1'
    SKIP_UPLOAD = os.getenv('SKIP_UPLOAD', '0') == '1'
    SKIP_ESCAPE = os.getenv('SKIP_ESCAPE', '0') == '1'

    # Paths
    PROJECT_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    MAME_PATH = os.getenv('MAME_PATH', '/home/petrkr/git/mame')
    MAME_BIN = os.path.join(MAME_PATH, 'pofo')

    # URLs and ports
    BRIDGE_URL = os.getenv('POFOSCAB_BRIDGE_URL', 'http://localhost:9000')
    BRIDGE_PORT = int(os.getenv('POFOSCAB_BRIDGE_PORT', '9000'))
    SMARTCABLE_PORT = 9997  # Fixed: smartcable device TCP port in MAME


config = TestConfig()


def wait_for_socket(host: str, port: int, timeout: int = 30) -> bool:
    """Wait for a TCP socket to be available."""
    start = time.time()
    while time.time() - start < timeout:
        try:
            sock = socket.create_connection((host, port), timeout=1)
            sock.close()
            return True
        except (ConnectionRefusedError, socket.timeout):
            time.sleep(0.1)
    return False


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


@pytest.fixture(scope="session")
def mame_process(cfg):
    """Start MAME emulator (session-scoped)."""
    if cfg.BACKEND == 'mame_manual':
        return None  # User runs MAME manually

    if cfg.BACKEND == 'hardware':
        return None  # Real hardware, no MAME

    if cfg.BACKEND != 'mame_auto':
        pytest.fail(f"Unknown backend: {cfg.BACKEND}")

    # mame_auto: start MAME with smartcable and autoboot script
    lua_script = os.path.join(cfg.PROJECT_ROOT, 'tests', 'lua', 'flow_build_upload_pftd.lua')

    proc = subprocess.Popen(
        [
            cfg.MAME_BIN, 'pofo',
            '-ccma', 'ram',
            '-exp', 'smartcable',
            '-autoboot_script', lua_script,
            '-seconds_to_run', '300',
            '-skip_gameinfo',
            '-window', '-nomax'
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True
    )

    # Wait for smartcable TCP socket to be ready
    # In GUI mode, MAME may take longer to initialize
    if not wait_for_socket('localhost', cfg.SMARTCABLE_PORT, timeout=60):
        pytest.fail(
            f"MAME smartcable device not ready after 60s. "
            f"Check that MAME window opened and Lua script ran."
        )

    yield proc

    # Cleanup: terminate MAME
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


@pytest.fixture(scope="session")
def bridge_process(cfg):
    """Start Python bridge to smartcable (session-scoped)."""
    if cfg.BACKEND == 'mame_manual':
        # User runs bridge manually; don't start it here
        yield None
        return

    if cfg.BACKEND == 'hardware':
        # Real hardware: no bridge process
        yield None
        return

    if cfg.BACKEND != 'mame_auto':
        yield None
        return

    # mame_auto: start Python bridge on configured port
    bridge_script = os.path.join(cfg.PROJECT_ROOT, 'tools', 'mame_bridge.py')

    proc = subprocess.Popen(
        [
            'python3', bridge_script,
            '--http-port', str(cfg.BRIDGE_PORT),
            '--mame-port', str(cfg.SMARTCABLE_PORT)
        ],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL
    )

    # Wait for bridge HTTP server to be ready
    bridge_url = cfg.BRIDGE_URL
    if not wait_for_http(bridge_url, timeout=10):
        proc.terminate()
        pytest.fail(f"Bridge not ready at {bridge_url} after 10s")

    yield proc

    # Cleanup: terminate bridge
    proc.terminate()
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()


@pytest.fixture(scope="session", autouse=True)
def build_pftd(cfg):
    """Build PFTD.COM (runs for MAME backends unless SKIP_BUILD=1)."""
    # Only build for MAME backends, not for hardware
    if cfg.BACKEND == 'hardware':
        return None

    if cfg.SKIP_BUILD:
        return None

    result = subprocess.run(
        ['make', 'pftd'],
        cwd=cfg.PROJECT_ROOT,
        capture_output=True,
        text=True
    )
    if result.returncode != 0:
        pytest.fail(f"Build failed:\n{result.stderr}")

    pftd_path = os.path.join(cfg.PROJECT_ROOT, 'build', 'PFTD.COM')
    assert os.path.exists(pftd_path), f"PFTD.COM not found at {pftd_path}"
    return pftd_path


@pytest.fixture(scope="session")
def escape_to_pftd_step(cfg):
    """Exit fileserver, run PFTD, restart fileserver (if SKIP_ESCAPE not set)."""
    if cfg.SKIP_ESCAPE:
        return None

    pytest.skip("escape not yet implemented")
    # TODO: implement escape sequence
