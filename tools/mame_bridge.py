#!/usr/bin/env python3
# mame_bridge.py - MAME-based stand-in for the ESP32 PortfolioESPlink
# client, so regression_test.py can exercise PFTD's real bit-bang wire
# protocol against an emulated Atari Portfolio (MAME's `pofo` driver)
# instead of a physical Portfolio + Smart Cable.
#
# Talks two protocols:
#   - TCP to MAME's `pofo_smartcable` expansion card (see the MAME
#     fork's src/devices/bus/pofo/pofo_smartcable.{h,cpp}): a
#     byte-level request/reply wire format - the card does the Smart
#     Cable bit-bang handshake itself and this side just asks for a
#     byte to be sent or received.
#   - HTTP to regression_test.py, replicating the ESP32 client firmware's
#     /sendRaw and /status endpoints so regression_test.py runs
#     unmodified.
#
# The block/application-level protocol logic (sendBlock/receiveBlock/
# detectOnce/upload_file/download_file) is unchanged from the earlier
# bit-level bridge - only the send/receive-a-single-byte primitives
# below talk to the card differently now.
#
# Usage: python3 mame_bridge.py [--mame-host HOST] [--mame-port PORT]
#                                [--http-port PORT]

import argparse
import http.server
import json
import logging
import socket
import threading
import time
import urllib.parse

log = logging.getLogger("mame_bridge")

# Wire protocol register ids on pofo_smartcable - must match the enum
# in the MAME fork's pofo_smartcable.cpp exactly.
REQ_SEND = 0x01
REQ_RECEIVE = 0x02
REPLY_SEND = 0x81
REPLY_RECV = 0x82

CLOCK_TIMEOUT_S = 2.0
DETECT_TIMEOUT_S = 0.05
PAYLOAD_BUFSIZE = 60000


class PortfolioLinkError(Exception):
    pass


class MameLink:
    """Byte-level client for MAME's pofo_smartcable bridge socket.

    The card does the Smart Cable bit-bang handshake itself; this side
    just asks it to send or receive one byte at a time. Everything above
    single-byte granularity (block framing/checksum, application
    protocol) is unchanged from the earlier bit-level bridge.
    """

    def __init__(self, mame_host: str, mame_port: int):
        self._sock = socket.create_connection((mame_host, mame_port), timeout=5)
        self._sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self._sock.setblocking(True)
        self._lock = threading.Lock()
        self._recv_buf = b""

    def close(self):
        self._sock.close()

    # -- byte-level handshake, delegated to pofo_smartcable ------------------

    def _recv_reply(self, timeout_s: float):
        # Reassembles {reg, ok, value} reply triples out of _recv_buf,
        # blocking up to timeout_s for more data if a full triple isn't
        # buffered yet.
        deadline = time.monotonic() + timeout_s
        while len(self._recv_buf) < 3:
            remaining = max(deadline - time.monotonic(), 0)
            self._sock.settimeout(remaining if remaining > 0 else 0.001)
            try:
                chunk = self._sock.recv(4096)
            except (socket.timeout, BlockingIOError):
                chunk = b""
            if chunk:
                self._recv_buf += chunk
            elif remaining <= 0:
                return None
        reg, ok, value = self._recv_buf[0], self._recv_buf[1], self._recv_buf[2]
        self._recv_buf = self._recv_buf[3:]
        return reg, ok, value

    def _receive_byte(self, timeout_s: float):
        self._sock.sendall(bytes([REQ_RECEIVE, 0x00]))
        reply = self._recv_reply(timeout_s)
        if reply is None or reply[0] != REPLY_RECV or reply[1] != 1:
            return None
        return reply[2]

    def _send_byte(self, data: int) -> bool:
        self._sock.sendall(bytes([REQ_SEND, data & 0xFF]))
        reply = self._recv_reply(CLOCK_TIMEOUT_S)
        if reply is None or reply[0] != REPLY_SEND:
            return False
        return reply[1] == 1

    def _send_block(self, data: bytes) -> bool:
        if not data:
            return True

        recv = self._receive_byte(CLOCK_TIMEOUT_S)
        if recv is None or recv != ord('Z'):
            return False

        time.sleep(0.05)
        if not self._send_byte(0xA5):
            return False

        length = len(data)
        len_l = length & 0xFF
        len_h = (length >> 8) & 0xFF
        checksum = 0

        if not self._send_byte(len_l):
            return False
        checksum = (checksum - len_l) & 0xFF

        if not self._send_byte(len_h):
            return False
        checksum = (checksum - len_h) & 0xFF

        for byte in data:
            if not self._send_byte(byte):
                return False
            checksum = (checksum - byte) & 0xFF

        if not self._send_byte(checksum):
            return False

        recv = self._receive_byte(CLOCK_TIMEOUT_S)
        return recv is not None and recv == checksum

    def _receive_block(self, max_len: int = PAYLOAD_BUFSIZE):
        checksum = 0

        if not self._send_byte(ord('Z')):
            return None

        recv = self._receive_byte(CLOCK_TIMEOUT_S)
        if recv is None or recv != 0xA5:
            return None

        len_l = self._receive_byte(CLOCK_TIMEOUT_S)
        len_h = self._receive_byte(CLOCK_TIMEOUT_S)
        if len_l is None or len_h is None:
            return None
        checksum = (checksum + len_l) & 0xFF
        checksum = (checksum + len_h) & 0xFF
        length = (len_h << 8) | len_l

        if length > max_len:
            return None

        data = bytearray()
        for _ in range(length):
            recv = self._receive_byte(CLOCK_TIMEOUT_S)
            if recv is None:
                return None
            checksum = (checksum + recv) & 0xFF
            data.append(recv)

        recv = self._receive_byte(CLOCK_TIMEOUT_S)
        if recv is None or ((256 - recv) & 0xFF) != checksum:
            return None

        time.sleep(0.0001)
        if not self._send_byte((256 - checksum) & 0xFF):
            return None

        return bytes(data)

    def detect_once(self) -> bool:
        ok, _attempts = self.detect_once_instrumented()
        return ok

    def detect_once_instrumented(self):
        # The ROM's idle 'Z' broadcast isn't synchronized to when we
        # happen to connect/poll - a single attempt can catch it
        # mid-byte and read a mix of two consecutive broadcasts, which
        # decodes to garbage even though the link is fine (mirrors
        # PortfolioLink.cpp's own comment on this). Retry a few times
        # before reporting disconnected, same spirit as the ESP32
        # client's DETECT_MISSES_TO_DISCONNECT loop, just condensed into
        # one synchronous call instead of a background poll.
        #
        # _receive_byte(timeout) applies that timeout independently to
        # EACH of its 8 edge-waits (4 bit pairs x 2 edges), so a single
        # call can take up to 8x timeout in the worst case - budget an
        # overall wall-clock deadline here instead of a fixed retry
        # count, so this can never balloon past ~a couple seconds
        # regardless of how many attempts that allows.
        #
        # Returns (ok, attempts) - attempts lets callers instrument how
        # many tries the retry loop actually needed, without changing
        # detect_once()'s bool-only contract used by /status.
        deadline = time.monotonic() + 2.0
        attempts = 0
        with self._lock:
            while time.monotonic() < deadline:
                attempts += 1
                self._drain_stale()
                recv = self._receive_byte(DETECT_TIMEOUT_S)
                if recv is not None and recv == ord('Z'):
                    return True, attempts
                time.sleep(0.02)
            return False, attempts

    def _drain_stale(self) -> None:
        self._recv_buf = b""
        self._sock.settimeout(0)
        try:
            while self._sock.recv(4096):
                pass
        except (BlockingIOError, socket.timeout, OSError):
            pass

    def send_raw(self, data: bytes) -> bytes:
        response, _attempts = self.send_raw_instrumented(data)
        return response

    def send_raw_instrumented(self, data: bytes):
        # Same "caught the ROM's idle 'Z' broadcast mid-byte" risk as
        # detect_once() (see its comment) applies to _send_block's
        # initial 'Z' sync - a real GPIO handshake on the ESP32 client
        # this replaces is fast enough that this is rare there, but the
        # extra latency of a network socket + MAME's polling timer makes
        # it common enough here to need the same retry treatment.
        #
        # Only the initial 'Z' sync inside _send_block is retried here -
        # once that lands, the rest of the transfer runs at
        # CLOCK_TIMEOUT_S per edge, same as a real transfer would. Cap
        # the total time spent retrying the initial sync so a string of
        # bad-luck misses can't compound into a long hang (each failed
        # attempt costs at most CLOCK_TIMEOUT_S). 3s covers several
        # retries of the sync handshake itself - it's not a budget for
        # slow ROM-side work (disk I/O for LIST/receive etc still gets
        # its own CLOCK_TIMEOUT_S per edge once a transfer is under way).
        #
        # Returns (response, attempts) - see detect_once_instrumented()
        # for why this exists alongside the plain send_raw().
        deadline = time.monotonic() + 3.0
        attempts = 0
        with self._lock:
            last_error = "send_block failed"
            while time.monotonic() < deadline:
                attempts += 1
                self._drain_stale()
                if not self._send_block(data):
                    continue
                response = self._receive_block()
                if response is None:
                    last_error = "receive_block failed"
                    continue
                return response, attempts
            raise PortfolioLinkError(last_error)

    def hello(self):
        response = self.send_raw(bytes([0x80]))
        if len(response) < 12 or response[:4] != b"PFD1":
            return None
        build_id = int.from_bytes(response[4:8], "little")
        version = response[8]
        capabilities = response[9]
        return {"buildId": build_id, "version": version, "capabilities": capabilities}

    # -- ROM-native transmit (0x03, upload) / receive (0x02, download) -------
    #
    # payload[0] in [0x00, 0x06] is handled directly by the ROM's File
    # Transfer Server, independent of PFTD - see PROTOCOL.md's "ROM
    # commands" table. The ROM can't be extended, so PROTOCOL.md only
    # lists the command codes, not their payload shape - the byte
    # layout here is ported from PortfolioESPlink's PortfolioLink.cpp
    # (runUpload/runDownload), the only known-working reference
    # implementation (verified on real hardware).
    #
    # Unlike send_raw() (one send_block + one receive_block pair), a
    # multi-chunk transfer needs several send_block() calls in a row
    # with no receive_block() in between - the ROM only replies once,
    # after the LAST data chunk - so these bypass send_raw()/send_raw_instrumented()
    # and drive _send_block()/_receive_block() directly.

    def upload_file(self, path: str, data: bytes) -> bool:
        # Transmit init (0x03): cmd, 0x00, 0x70, packed time (2B LE),
        # packed date (2B LE), file size (3B LE), 0x00, then ASCIIZ
        # dest path in a fixed 79-byte field (90-byte block total).
        now_date = 0  # 1980-01-01 - exact value doesn't matter, ROM just stores it as the file's timestamp
        now_time = 0
        size = len(data)
        name_field = (path.encode("ascii") + b"\x00").ljust(79, b"\x00")
        init = bytes([0x03, 0x00, 0x70])
        init += bytes([now_time & 0xFF, now_time >> 8])
        init += bytes([now_date & 0xFF, now_date >> 8])
        init += bytes([size & 0xFF, (size >> 8) & 0xFF, (size >> 16) & 0xFF])
        init += b"\x00"
        init += name_field
        assert len(init) == 90, len(init)

        with self._lock:
            response, _ = self._send_raw_locked(init)
            if len(response) < 3:
                return False

            # Ported from PortfolioLink.cpp's runUpload(): only 0x10
            # (invalid path) is a hard failure. 0x20 means the
            # destination already exists and needs an explicit overwrite
            # confirm (0x05) before the ROM will accept data chunks -
            # any other value (e.g. destination doesn't exist yet) skips
            # straight to sending chunks, no confirm needed. Earlier code
            # here treated anything but 0x20 as failure, which broke on
            # multi-chunk uploads to a fresh path (see tests/tests.md).
            if response[0] == 0x10:
                return False
            if response[0] == 0x20:
                if not self._send_block(bytes([0x05, 0x00, 0x70])):
                    return False

            blocksize = response[1] | (response[2] << 8)

            offset = 0
            while offset < size:
                chunk = data[offset:offset + blocksize]
                if not self._send_block(chunk):
                    return False
                offset += len(chunk)

            final = self._receive_block()
            return final is not None and len(final) >= 1 and final[0] == 0x20

    def download_file(self, path: str) -> bytes | None:
        # Receive init (0x02): cmd, 0x00, 0x70, then ASCIIZ source path
        # in a fixed 79-byte field (82-byte block total).
        name_field = (path.encode("ascii") + b"\x00").ljust(79, b"\x00")
        init = bytes([0x02, 0x00, 0x70]) + name_field
        assert len(init) == 82, len(init)

        with self._lock:
            response, _ = self._send_raw_locked(init)
            if len(response) < 11 or response[0] != 0x20:
                return None
            size = response[7] | (response[8] << 8) | (response[9] << 16) | (response[10] << 24)

            data = bytearray()
            while len(data) < size:
                chunk = self._receive_block()
                if chunk is None:
                    return None
                data += chunk

            # Fixed 3-byte finish ack, per PortfolioLink.cpp's
            # RECEIVE_FINISH constant - meaning of bytes 1-2 beyond
            # "0x20 = done" isn't documented anywhere, but this exact
            # sequence is verified working on real hardware.
            self._send_block(bytes([0x20, 0x00, 0x03]))
            return bytes(data[:size])

    def _send_raw_locked(self, data: bytes):
        # Same retry shape and 3s rationale as send_raw_instrumented(),
        # for callers that already hold self._lock and need more
        # send_block/receive_block calls afterward within the same
        # locked section.
        deadline = time.monotonic() + 3.0
        attempts = 0
        last_error = "send_block failed"
        while time.monotonic() < deadline:
            attempts += 1
            self._drain_stale()
            if not self._send_block(data):
                continue
            response = self._receive_block()
            if response is None:
                last_error = "receive_block failed"
                continue
            return response, attempts
        raise PortfolioLinkError(last_error)


# Same cadence as PortfolioLink.cpp's DETECT_INTERVAL (100ms) - a
# background loop polls detect_once()/hello() and publishes the result,
# so /status is a plain read of cached state (matching
# PortfolioESPlink's handleStatus(), which never probes the link
# itself - see PortfolioLink::status()/isConnected()/hasPFTD()) instead
# of a blocking round-trip on every request. All other endpoints check
# this cached connected flag first and refuse outright when it's false,
# rather than attempting a handshake blind.
DETECT_POLL_INTERVAL_S = 0.1


class BridgeState:
    def __init__(self, link: MameLink):
        self.link = link
        self.lock = threading.Lock()
        self.connected = False
        self.pftd = None
        self._stop = threading.Event()

    def start_detect_loop(self) -> None:
        thread = threading.Thread(target=self._detect_loop, daemon=True)
        thread.start()

    def stop_detect_loop(self) -> None:
        self._stop.set()

    def _detect_loop(self) -> None:
        while not self._stop.is_set():
            with self.lock:
                connected = self.link.detect_once()
                pftd = None
                if connected:
                    # hello() (0x80) only succeeds once PFTD is resident
                    # and running - the ROM-native File Transfer Server
                    # alone (no PFTD) has no handler for it and this
                    # raises. That's expected, not a link failure: catch
                    # it here so one unanswered probe can't take down
                    # this whole background thread (an uncaught
                    # exception here would silently kill it, freezing
                    # `connected` at whatever it last was forever).
                    try:
                        pftd = self.link.hello()
                    except PortfolioLinkError:
                        pftd = None
            self.connected = connected
            self.pftd = pftd
            self._stop.wait(DETECT_POLL_INTERVAL_S)


def make_handler(state: BridgeState):
    class Handler(http.server.BaseHTTPRequestHandler):
        def log_message(self, fmt, *args):
            pass  # keep test output quiet; errors still surface via responses

        def _send_json(self, obj, status=200):
            body = json.dumps(obj).encode()
            self.send_response(status)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def do_GET(self):
            if self.path == "/status":
                self._send_json({"connected": state.connected, "pftd": state.pftd})
            else:
                self.send_error(404)

        def do_POST(self):
            length = int(self.headers.get("Content-Length", 0))
            body = self.rfile.read(length).decode()
            params = urllib.parse.parse_qs(body)

            if self.path not in ("/sendRaw", "/uploadFile", "/downloadFile"):
                self.send_error(404)
                return

            if not state.connected:
                self._send_json({"ok": False, "error": "not connected"}, status=503)
                return

            if self.path == "/sendRaw":
                hexdata = params.get("data", [""])[0]
                try:
                    data = bytes.fromhex(hexdata)
                except ValueError:
                    self._send_json({"ok": False, "error": "bad hex"}, status=400)
                    return

                with state.lock:
                    try:
                        response = state.link.send_raw(data)
                        self._send_json({"ok": True, "response": response.hex()})
                    except PortfolioLinkError as exc:
                        self._send_json({"ok": False, "error": str(exc)}, status=502)

            elif self.path == "/uploadFile":
                path = params.get("path", [""])[0]
                hexdata = params.get("data", [""])[0]
                try:
                    data = bytes.fromhex(hexdata)
                except ValueError:
                    self._send_json({"ok": False, "error": "bad hex"}, status=400)
                    return

                with state.lock:
                    try:
                        ok = state.link.upload_file(path, data)
                    except PortfolioLinkError as exc:
                        self._send_json({"ok": False, "error": str(exc)}, status=502)
                        return
                self._send_json({"ok": ok})

            elif self.path == "/downloadFile":
                path = params.get("path", [""])[0]
                with state.lock:
                    try:
                        data = state.link.download_file(path)
                    except PortfolioLinkError as exc:
                        self._send_json({"ok": False, "error": str(exc)}, status=502)
                        return
                if data is None:
                    self._send_json({"ok": False})
                else:
                    self._send_json({"ok": True, "data": data.hex()})

    return Handler


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--mame-host", default="127.0.0.1")
    parser.add_argument("--mame-port", type=int, default=9997)
    parser.add_argument("--http-port", type=int, default=8080)
    args = parser.parse_args()

    print(f"Connecting to MAME dummy_i8255 bridge at {args.mame_host}:{args.mame_port} ...")
    link = MameLink(args.mame_host, args.mame_port)
    state = BridgeState(link)
    state.start_detect_loop()

    server = http.server.ThreadingHTTPServer(("127.0.0.1", args.http_port), make_handler(state))
    print(f"mame_bridge listening on http://127.0.0.1:{args.http_port}")
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        state.stop_detect_loop()
        link.close()


if __name__ == "__main__":
    main()
