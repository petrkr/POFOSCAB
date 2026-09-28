#!/usr/bin/env python3
"""Receive one Portfolio-initiated Smart Cable block via pofo_smartcable.

The MAME smartcable device performs the raw bit-bang state machine.  This
program supplies the byte-level server role needed by WIRETEST.COM:
send 'Z', receive the block, return its checksum acknowledgement, then send
the one-byte application response after the Portfolio calls AL=1.
"""

import argparse
import socket
import time


REQ_SEND = 0x01
REQ_RECEIVE = 0x02
REQ_WATCH_PORT_C = 0x03
REQ_CANCEL = 0x04
REPLY_SEND = 0x81
REPLY_RECEIVE = 0x82
REPLY_WATCH_PORT_C = 0x83
EVENT_PORT_C = 0x84
REPLY_CANCEL = 0x85

BYTE_TIMEOUT_S = 0.500


class SmartCable:
    def __init__(self, host: str, port: int, trace: bool):
        self.sock = socket.create_connection((host, port))
        self.sock.setblocking(True)
        self.trace = trace
        self.buffered = b""
        self.port_c_events = []

    def close(self):
        self.sock.close()

    def _message(self, timeout_s: float):
        deadline = time.monotonic() + timeout_s
        while len(self.buffered) < 3:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            self.sock.settimeout(remaining)
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                return None
            if not chunk:
                raise ConnectionError("smartcable disconnected")
            self.buffered += chunk
        reply = self.buffered[:3]
        self.buffered = self.buffered[3:]
        return reply

    def watch_port_c(self):
        self.sock.sendall(bytes([REQ_WATCH_PORT_C, 1]))
        message = self._wait_reply(REPLY_WATCH_PORT_C, BYTE_TIMEOUT_S)
        if message is None or message[1] != 1:
            raise ConnectionError("could not subscribe to Port C events")

    def wait_port_c_event(self):
        if self.port_c_events:
            return self.port_c_events.pop(0)
        while True:
            message = self._message(BYTE_TIMEOUT_S)
            if message is None:
                continue
            if message[0] == EVENT_PORT_C:
                if self.trace:
                    print(f"PORT_C event 0x{message[2]:02x}", flush=True)
                return message[2]

    def _wait_reply(self, expected_reg: int, timeout_s: float):
        deadline = time.monotonic() + timeout_s
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                return None
            message = self._message(remaining)
            if message is None:
                return None
            if message[0] == EVENT_PORT_C:
                self.port_c_events.append(message[2])
                continue
            if message[0] == expected_reg:
                return message

    def cancel(self):
        """Cancel a timed-out byte operation and consume stale replies."""
        self.sock.sendall(bytes([REQ_CANCEL, 0]))
        message = self._wait_reply(REPLY_CANCEL, BYTE_TIMEOUT_S)
        if message is None or message[1] != 1:
            raise ConnectionError("could not cancel smartcable operation")

    def send_byte(self, value: int) -> bool:
        value &= 0xFF
        if self.trace:
            print(f"send 0x{value:02x}", flush=True)
        self.sock.sendall(bytes([REQ_SEND, value]))
        reply = self._wait_reply(REPLY_SEND, BYTE_TIMEOUT_S)
        if reply is None:
            self.cancel()
            return False
        return reply[1] == 1

    def receive_byte(self):
        self.sock.sendall(bytes([REQ_RECEIVE, 0]))
        reply = self._wait_reply(REPLY_RECEIVE, BYTE_TIMEOUT_S)
        if reply is None:
            self.cancel()
            return None
        _, ok, value = reply
        if ok != 1:
            return None
        if self.trace:
            print(f"receive 0x{value:02x}", flush=True)
        return value


def receive_atari_block(link: SmartCable, max_len: int):
    """Equivalent to PortfolioLink::receiveBlock()."""
    if not link.send_byte(ord("Z")):
        return None

    if link.receive_byte() != 0xA5:
        return None

    len_l = link.receive_byte()
    len_h = link.receive_byte()
    if len_l is None or len_h is None:
        return None

    length = len_l | (len_h << 8)
    if length > max_len:
        raise ValueError(f"block length {length} exceeds limit {max_len}")

    checksum = (len_l + len_h) & 0xFF
    payload = bytearray()
    for _ in range(length):
        value = link.receive_byte()
        if value is None:
            return None
        payload.append(value)
        checksum = (checksum + value) & 0xFF

    received_checksum = link.receive_byte()
    expected_checksum = (-checksum) & 0xFF
    if received_checksum != expected_checksum:
        return None

    time.sleep(0.0001)
    if not link.send_byte(expected_checksum):
        return None
    return bytes(payload)


def send_atari_block(link: SmartCable, payload: bytes) -> bool:
    """Send one block after the Portfolio AL=1 handler emits Z sync."""
    if link.receive_byte() != ord("Z"):
        return False

    length = len(payload)
    len_l = length & 0xFF
    len_h = length >> 8
    checksum = (-(len_l + len_h + sum(payload))) & 0xFF
    frame = bytes((0xA5, len_l, len_h)) + payload + bytes((checksum,))
    for value in frame:
        if not link.send_byte(value):
            return False
    return link.receive_byte() == checksum


def send_status_response(link: SmartCable, status: int) -> bool:
    """Wait for AL=1 sync, then return the application status byte."""
    deadline = time.monotonic() + 3.0
    while time.monotonic() < deadline:
        if send_atari_block(link, bytes((status,))):
            return True
    return False


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9997)
    parser.add_argument("--max-len", type=int, default=60000)
    parser.add_argument("--status", type=lambda value: int(value, 0), default=0x20)
    parser.add_argument("--trace", action="store_true")
    args = parser.parse_args()
    if not 0 <= args.status <= 0xFF:
        parser.error("--status must be in range 0..255")

    link = SmartCable(args.host, args.port, args.trace)
    print(f"connected to {args.host}:{args.port}", flush=True)
    try:
        link.watch_port_c()
        print("waiting for a Port C event", flush=True)
        while True:
            link.wait_port_c_event()
            payload = receive_atari_block(link, args.max_len)
            if payload is not None:
                print(f"received {len(payload)} bytes: {payload!r}")
                if send_status_response(link, args.status):
                    print(f"sent status 0x{args.status:02x}")
                else:
                    print("status response failed")
                return
            print("Port C event did not start a block; waiting", flush=True)
    finally:
        link.close()


if __name__ == "__main__":
    main()
