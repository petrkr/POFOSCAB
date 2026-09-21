"""Unit tests for mame_bridge.py's link-loss/reconnect behavior.

These don't need MAME or a Portfolio at all - they stand in a bare TCP
listener as a stand-in for MAME's pofo_smartcable socket, so the
"cable unplugged" (no MAME) and "server switched off" (MAME up, ROM not
answering) cases can be exercised deterministically and fast, without
booting an emulator.

Run: pytest tests/test_bridge_link.py -v
"""

import os
import socket
import sys
import threading
import time

import pytest

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "tools"))
from mame_bridge import (  # noqa: E402
    BridgeState,
    MameLink,
    MameLinkDown,
    REPLY_RECV,
)


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return s.getsockname()[1]


class FakeSmartcable:
    """Bare TCP listener standing in for MAME's pofo_smartcable device.

    By default never replies (simulates the ROM's File Transfer Server
    being off - ROM connected to nothing, so requests time out).
    Call `answer_z()` to make it reply as if the ROM were idle-broadcasting.
    """

    def __init__(self, port: int):
        self.port = port
        self._srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self._srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self._srv.bind(("127.0.0.1", port))
        self._srv.listen(1)
        self._conn = None
        self._stop = threading.Event()
        self._reply_z = threading.Event()
        self._thread = threading.Thread(target=self._run, daemon=True)
        self._thread.start()

    def _run(self):
        self._srv.settimeout(0.2)
        while not self._stop.is_set():
            try:
                conn, _ = self._srv.accept()
            except socket.timeout:
                continue
            self._conn = conn
            self._serve(conn)

    def _serve(self, conn: socket.socket):
        conn.settimeout(0.2)
        while not self._stop.is_set():
            try:
                req = conn.recv(2)
            except socket.timeout:
                continue
            except OSError:
                return
            if not req:
                return
            if self._reply_z.is_set():
                try:
                    conn.sendall(bytes([REPLY_RECV, 1, ord("Z")]))
                except OSError:
                    return
            # else: swallow the request, never reply -> caller times out

    def answer_z(self):
        self._reply_z.set()

    def drop_client(self):
        """Simulates MAME's process dying/socket closing mid-session."""
        if self._conn is not None:
            self._conn.close()
            self._conn = None

    def close(self):
        self._stop.set()
        if self._conn is not None:
            self._conn.close()
        self._srv.close()
        self._thread.join(timeout=2)


@pytest.fixture
def port():
    return free_port()


def test_link_not_connected_before_connect(port):
    link = MameLink("127.0.0.1", port)
    assert not link.connected


def test_connect_fails_when_nothing_listening(port):
    link = MameLink("127.0.0.1", port)
    with pytest.raises(OSError):
        link.connect(timeout=0.5)
    assert not link.connected


def test_bridge_state_reports_disconnected_with_no_mame(port):
    """'Cable unplugged': no MAME listening at all. Detect loop must not
    crash and must keep reporting connected=False instead of raising."""
    link = MameLink("127.0.0.1", port)
    state = BridgeState(link)
    state.start_detect_loop()
    try:
        time.sleep(0.5)
        assert state.connected is False
        assert state.pftd is None
        assert not state.mame_linked
    finally:
        state.stop_detect_loop()
        link.close()


def test_bridge_reconnects_once_mame_appears(port):
    """Bridge started before MAME: once the TCP port appears, the
    detect loop should pick it up on its own without restarting."""
    link = MameLink("127.0.0.1", port)
    state = BridgeState(link)
    state.start_detect_loop()
    try:
        time.sleep(0.3)
        assert not state.mame_linked

        fake = FakeSmartcable(port)
        try:
            fake.answer_z()
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline and not state.connected:
                time.sleep(0.05)
            assert state.mame_linked, "bridge never reconnected to MAME"
            assert state.connected, "bridge never detected the idle 'Z' broadcast"
        finally:
            fake.close()
    finally:
        state.stop_detect_loop()
        link.close()


def test_bridge_survives_mame_disappearing_mid_session(port):
    """MAME process dies/restarts while the bridge holds a connection:
    the detect loop must notice, drop the dead socket, and go back to
    reconnect-retrying instead of raising out of the background thread."""
    fake = FakeSmartcable(port)
    fake.answer_z()
    link = MameLink("127.0.0.1", port)
    state = BridgeState(link)
    state.start_detect_loop()
    try:
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and not state.connected:
            time.sleep(0.05)
        assert state.connected, "bridge never detected initial link"

        fake.close()  # MAME "process" goes away entirely

        deadline = time.monotonic() + 5
        while time.monotonic() < deadline and state.mame_linked:
            time.sleep(0.05)
        assert not state.mame_linked, "bridge kept believing MAME was linked"
        assert state.connected is False
        assert state.pftd is None

        # And it should be trying to reconnect, not have died - bring
        # MAME "back" on the same port and confirm it re-links.
        fake2 = FakeSmartcable(port)
        fake2.answer_z()
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline and not state.connected:
                time.sleep(0.05)
            assert state.connected, "bridge never recovered after MAME restarted"
        finally:
            fake2.close()
    finally:
        state.stop_detect_loop()
        link.close()
        fake.close()


def test_portfolio_disconnected_but_mame_linked(port):
    """MAME running, TCP up, but ROM fileserver not answering (Portfolio
    'server' off): must report connected=False while mame_linked stays
    True - this is the "timeout, not cable-unplugged" case."""
    fake = FakeSmartcable(port)  # never answers -> handshake times out
    link = MameLink("127.0.0.1", port)
    state = BridgeState(link)
    state.start_detect_loop()
    try:
        time.sleep(3)  # >= detect_once_instrumented's ~2s deadline
        assert state.mame_linked, "TCP link to MAME should still be up"
        assert state.connected is False, "Portfolio should read as disconnected, not linked-down"
        assert state.pftd is None
    finally:
        state.stop_detect_loop()
        link.close()
        fake.close()


def test_recv_reply_raises_link_down_on_eof(port):
    """A closed socket during a request must surface as MameLinkDown,
    not be silently treated as a handshake timeout."""
    fake = FakeSmartcable(port)
    link = MameLink("127.0.0.1", port)
    link.connect(timeout=1)
    time.sleep(0.1)  # let the server thread's accept() land before we drop it
    fake.drop_client()
    fake.close()
    with pytest.raises(MameLinkDown):
        link._receive_byte(1.0)
