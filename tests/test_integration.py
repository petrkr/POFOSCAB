"""PFTD integration tests via smartcable bridge.

Tests run against the /sendRaw API,
allowing runs against either MAME (via smartcable device) or real Portfolio
hardware (ESP32 Smart Cable client).

Usage:
  pytest tests/test_integration.py -v

  # Skip upload step (PFTD already on card)
  SKIP_UPLOAD=1 pytest tests/test_integration.py -v

  # Manual MAME UI mode (start mame manually first)
  POFOSCAB_BACKEND=mame_manual pytest tests/test_integration.py -v

  # Real hardware
  POFOSCAB_BACKEND=hardware POFOSCAB_BRIDGE_URL=http://10.220.179.55 pytest tests/test_integration.py -v
"""

import json
import os
import uuid
import urllib.request
import urllib.parse
import pytest
from conftest import config, wait_for_http, wait_for_socket


# ============================================================================
# Helper functions
# ============================================================================

def send_raw(base_url: str, hexdata: str) -> str:
    """Send raw hex data to /sendRaw endpoint."""
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


def get_status(base_url: str) -> dict:
    """Fetch /status endpoint."""
    with urllib.request.urlopen(f"{base_url}/status", timeout=10) as resp:
        return json.loads(resp.read().decode())


def path_bytes(p: str) -> bytes:
    """Encode path as null-terminated ASCII."""
    return p.encode("ascii") + b"\x00"


def req(cmd: int, *paths: str) -> str:
    """Build wire protocol request."""
    payload = bytes([cmd, 0x00, 0x70])
    for p in paths:
        payload += path_bytes(p)
    return payload.hex()


def req_bin(cmd: int, payload: bytes) -> str:
    """Build wire protocol request with binary payload."""
    return (bytes([cmd, 0x00, 0x70]) + payload).hex()


def unique_path(drive: str, suffix: str = "") -> str:
    """Return a unique FAT 8.3 test path."""
    return f"{drive}:\\T{uuid.uuid4().hex[:7]}{suffix}"


def remove_path(base_url: str, path: str, command: int) -> None:
    """Best-effort cleanup for an artifact created by the current test."""
    try:
        send_raw(base_url, req(command, path))
    except Exception:
        pass


def upload_file(base_url: str, path: str, data: bytes) -> bool:
    """Upload file via /uploadFile endpoint."""
    body = urllib.parse.urlencode({"path": path, "data": data.hex()}).encode()
    req_obj = urllib.request.Request(f"{base_url}/uploadFile", data=body, method="POST")
    with urllib.request.urlopen(req_obj, timeout=30) as resp:
        return json.loads(resp.read().decode())["ok"]


def download_file(base_url: str, path: str) -> bytes | None:
    """Download file via /downloadFile endpoint."""
    body = urllib.parse.urlencode({"path": path}).encode()
    req_obj = urllib.request.Request(f"{base_url}/downloadFile", data=body, method="POST")
    with urllib.request.urlopen(req_obj, timeout=30) as resp:
        parsed = json.loads(resp.read().decode())
    return bytes.fromhex(parsed["data"]) if parsed["ok"] else None


def pack_date(year: int, month: int, day: int) -> int:
    """Pack date into DOS format."""
    return ((year - 1980) << 9) | (month << 5) | day


def pack_time(hour: int, minute: int, second: int) -> int:
    """Pack time into DOS format."""
    return (hour << 11) | (minute << 5) | (second // 2)


# ============================================================================
# Test fixtures
# ============================================================================

@pytest.fixture(scope="module", autouse=True)
def setup_tests(cfg, build_pftd, bridge_process):
    """Setup all tests: ensure bridge is ready and Portfolio is connected.

    Only waits for the Portfolio link itself (/status "connected") - does
    NOT gate on PFTD being present, so test_status (the one test that
    only checks the raw link) can still run and report a clear result
    when PFTD isn't up. PFTD-dependent tests are gated separately by the
    require_pftd fixture below.
    """
    if not wait_for_http(cfg.BRIDGE_URL, timeout=10):
        pytest.fail(
            f"Bridge not available at {cfg.BRIDGE_URL}. "
            f"Backend: {cfg.BACKEND}. "
            f"If manual mode, start MAME and bridge first."
        )

    # Wait for Portfolio to be connected (Lua script runs fileserver).
    import time
    start = time.time()
    while time.time() - start < 15:
        try:
            with urllib.request.urlopen(f"{cfg.BRIDGE_URL}/status", timeout=2) as resp:
                st = json.loads(resp.read().decode())
            if st.get("connected"):
                # Portfolio is connected; optionally upload PFTD if needed
                if not cfg.SKIP_UPLOAD and cfg.BACKEND != 'hardware':
                    _do_upload_pftd(cfg)
                return  # Ready!
        except Exception:
            pass
        time.sleep(0.5)

    pytest.fail(
        f"Portfolio not connected after 15s. "
        f"Check MAME window - fileserver may not have started."
    )


@pytest.fixture(autouse=True)
def require_pftd(request, setup_tests, cfg):
    """Skip PFTD-dependent tests up front when PFTD isn't answering,
    instead of letting each one run into its own multi-second
    send_block/receive_block timeout against the plain ROM fileserver.

    test_status is exempt (it only checks the raw Portfolio link, not
    PFTD) via the no_pftd_required marker.
    """
    if request.node.get_closest_marker("no_pftd_required"):
        return
    try:
        with urllib.request.urlopen(f"{cfg.BRIDGE_URL}/status", timeout=2) as resp:
            st = json.loads(resp.read().decode())
    except Exception as exc:
        pytest.fail(f"Could not reach {cfg.BRIDGE_URL}/status: {exc}")
    if not st.get("pftd"):
        pytest.skip("PFTD not detected on the Portfolio - skipping PFTD-dependent test")


def _do_upload_pftd(cfg):
    """Helper to upload PFTD.COM if needed."""
    import urllib.request
    import urllib.parse

    pftd_path = os.path.join(cfg.PROJECT_ROOT, 'build', 'PFTD.COM')
    if not os.path.exists(pftd_path):
        pytest.fail(f"PFTD.COM not found at {pftd_path}")

    with open(pftd_path, 'rb') as f:
        pftd_data = f.read()

    try:
        body = urllib.parse.urlencode({
            "path": "C:\\PFTD.COM",
            "data": pftd_data.hex()
        }).encode()
        req = urllib.request.Request(
            f"{cfg.BRIDGE_URL}/uploadFile",
            data=body,
            method="POST"
        )
        with urllib.request.urlopen(req, timeout=60) as resp:
            result = json.loads(resp.read().decode())
        if not result.get("ok"):
            pytest.fail(f"Upload failed: {result}")
    except Exception as e:
        pytest.fail(f"Upload error: {e}")


@pytest.fixture(scope="module")
def base_url(cfg):
    """Provide bridge URL for tests."""
    return cfg.BRIDGE_URL


# ============================================================================
# Tests
# ============================================================================

class TestBasic:
    """Basic command tests."""

    @pytest.mark.no_pftd_required
    def test_status(self, base_url):
        """Check /status endpoint."""
        st = get_status(base_url)
        assert st.get("connected"), "Portfolio not connected"

    def test_hello(self, base_url):
        """HELLO (0x80) - magic number check."""
        r = send_raw(base_url, "80")
        assert r[:8].lower() == "50464431", f"Expected 'PFD1', got {r}"

    def test_drives(self, base_url):
        """DRIVES (0x87) - single byte response."""
        r = send_raw(base_url, "87")
        assert len(r) == 2, f"Expected 2 hex chars (1 byte), got {len(r)}"


class TestFileOperations:
    """File system operations."""

    def test_mkdir_new(self, base_url):
        """MKDIR (0x88) - create new directory."""
        path = unique_path("C")
        try:
            r = send_raw(base_url, req(0x88, path))
            assert r[:2].lower() == "20", f"MKDIR failed: {r}"
        finally:
            remove_path(base_url, path, 0x8A)

    def test_mkdir_existing(self, base_url):
        """MKDIR (0x88) - existing directory returns access denied."""
        path = unique_path("C")
        try:
            send_raw(base_url, req(0x88, path))
            r = send_raw(base_url, req(0x88, path))
            assert r[:2].lower() == "10", f"Expected error status, got {r}"
            assert r[2:4].lower() == "04", f"Expected errcode 4, got {r[2:4]}"
        finally:
            remove_path(base_url, path, 0x8A)

    def test_rmdir(self, base_url):
        """RMDIR (0x8A) - remove directory."""
        path = unique_path("C")
        try:
            send_raw(base_url, req(0x88, path))
            r = send_raw(base_url, req(0x8A, path))
            assert r[:2].lower() == "20", f"RMDIR failed: {r}"
        finally:
            remove_path(base_url, path, 0x8A)

    def test_list(self, base_url):
        """LIST extended (0x86) - directory listing."""
        r = send_raw(base_url, req(0x86, "C:\\*.*"))
        assert len(r) >= 4, f"LIST response too short: {r}"

    def test_rename(self, base_url):
        """RENAME (0x8B) - rename file/dir."""
        source = unique_path("C")
        destination = unique_path("C")
        try:
            send_raw(base_url, req(0x88, source))
            r = send_raw(base_url, req(0x8B, source, destination))
            assert r[:2].lower() == "20", f"RENAME failed: {r}"
        finally:
            remove_path(base_url, source, 0x8A)
            remove_path(base_url, destination, 0x8A)

    def test_delete_nonexistent(self, base_url):
        """DELETE (0x89) - nonexistent file returns not found."""
        r = send_raw(base_url, req(0x89, "C:\\PYTESTNOTEXIST.TXT"))
        assert r[:2].lower() == "10", f"Expected error status, got {r}"
        assert r[2:4].lower() == "01", f"Expected errcode 1, got {r[2:4]}"


class TestFileTransfer:
    """Upload/download (ROM-native 0x03/0x02)."""

    def test_upload_download_roundtrip(self, base_url):
        """Upload file and download to verify round-trip."""
        test_content = b"PYTEST ROUNDTRIP TEST\r\n"
        path = unique_path("C", ".TXT")

        try:
            ok = upload_file(base_url, path, test_content)
            assert ok, "Upload failed"

            downloaded = download_file(base_url, path)
            assert downloaded == test_content, f"Download mismatch: {downloaded!r}"
        finally:
            remove_path(base_url, path, 0x89)

    def test_copy_same_drive(self, base_url):
        """COPY (0x8C) - same-drive copy."""
        # Upload source
        test_content = b"PYTEST COPY SOURCE\r\n"
        src = unique_path("C", ".TXT")
        dst = unique_path("C", ".TXT")
        try:
            ok = upload_file(base_url, src, test_content)
            assert ok, "Upload source failed"

            r = send_raw(base_url, req(0x8C, src, dst))
            assert r[:2].lower() == "20", f"COPY failed: {r}"

            downloaded = download_file(base_url, dst)
            assert downloaded == test_content, "Copy destination mismatch"
        finally:
            remove_path(base_url, src, 0x89)
            remove_path(base_url, dst, 0x89)

    def test_copy_cross_drive(self, base_url):
        """COPY (0x8C) - cross-drive copy."""
        # Upload to C:
        test_content = b"PYTEST CROSS-DRIVE\r\n"
        src = unique_path("C", ".TXT")
        dst = unique_path("A", ".TXT")
        try:
            ok = upload_file(base_url, src, test_content)
            assert ok, "Upload source failed"

            r = send_raw(base_url, req(0x8C, src, dst))
            assert r[:2].lower() == "20", f"COPY cross-drive failed: {r}"
        finally:
            remove_path(base_url, src, 0x89)
            remove_path(base_url, dst, 0x89)

    def test_copy_nonexistent_source(self, base_url):
        """COPY (0x8C) - nonexistent source returns not found."""
        r = send_raw(base_url, req(0x8C, "C:\\PYTESTNOTEXIST.TXT", "C:\\PYTEST_DST.TXT"))
        assert r[:2].lower() == "10", f"Expected error status, got {r}"
        assert r[2:4].lower() == "01", f"Expected errcode 1, got {r[2:4]}"


class TestDateTime:
    """Date/time operations."""

    def test_getdatetime(self, base_url):
        """GETDATETIME (0x8D) - read current date/time."""
        r = send_raw(base_url, "8d")
        assert len(r) == 8, f"Expected 4 bytes (8 hex chars), got {len(r)}"

        # Parse and validate
        raw = bytes.fromhex(r)
        packed_date = raw[0] | (raw[1] << 8)
        packed_time = raw[2] | (raw[3] << 8)

        year = 1980 + (packed_date >> 9)
        month = (packed_date >> 5) & 0x0F
        day = packed_date & 0x1F
        hour = packed_time >> 11
        minute = (packed_time >> 5) & 0x3F
        second = (packed_time & 0x1F) * 2

        assert 1980 <= year <= 2107, f"Invalid year: {year}"
        assert 1 <= month <= 12, f"Invalid month: {month}"
        assert 1 <= day <= 31, f"Invalid day: {day}"
        assert hour <= 23, f"Invalid hour: {hour}"
        assert minute <= 59, f"Invalid minute: {minute}"

    def test_setdatetime_valid(self, base_url):
        """SETDATETIME (0x8E) - set valid date/time."""
        set_date = pack_date(2026, 9, 21)
        set_time = pack_time(14, 30, 0)
        payload = bytes([set_date & 0xFF, set_date >> 8, set_time & 0xFF, set_time >> 8])

        r = send_raw(base_url, req_bin(0x8E, payload))
        assert r[:2].lower() == "20", f"SETDATETIME failed: {r}"

        r = send_raw(base_url, "8d")
        assert len(r) == 8, f"Expected 4 bytes (8 hex chars), got {len(r)}"
        raw = bytes.fromhex(r)
        assert raw[:2] == payload[:2], f"Date did not persist: {r}"
        assert raw[2:] == payload[2:], f"Time did not persist: {r}"

    def test_setdatetime_invalid_month(self, base_url):
        """SETDATETIME (0x8E) - invalid month returns error."""
        bad_date = pack_date(2026, 13, 1)
        payload = bytes([bad_date & 0xFF, bad_date >> 8, 0x00, 0x00])

        r = send_raw(base_url, req_bin(0x8E, payload))
        assert r[:2].lower() == "10", f"Expected error status, got {r}"
        assert r[2:4].lower() == "04", f"Expected errcode 4, got {r[2:4]}"
