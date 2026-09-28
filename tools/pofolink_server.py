#!/usr/bin/env python3
# Server-side (Atari-peer) mirror of PortfolioLink.cpp's sendByte()/
# receiveByte() (ESP32 client, /home/petrkr/Arduino/PortfolioESPlink/
# src/PortfolioLink.cpp lines 705-784), talking to the emulated
# Portfolio over MAME's pofo_bridge TCP socket (raw i8255 Port A/Port C
# passthrough - see pofo_bridge.cpp/.h).
#
# Wire model:
# the two sides do NOT share one half-duplex wire. Each side drives its
# own independent clock+data pair and reads the other side's pair
# separately - exactly like PortfolioLink.cpp's 4 separate GPIO pins
# (outClock/outData it drives, inClock/inData it reads), just folded
# onto a single i8255 on the Portfolio side:
#
#   Portfolio Port A (input)  == what WE drive   == ESP's inClock/inData,
#                                                    as READ BY the Portfolio
#   Portfolio Port C (output) == what THEY drive == ESP's outClock/outData,
#                                                    as WRITTEN BY the Portfolio
#
# The cable crosses the two signal pairs.  The Portfolio reads its input
# clock/data from Port A bits 1/0, but drives its output clock/data on
# Port C bits 0/1 respectively.  This was verified by port_a_probe.py:
# holding Port A bit 1 high stops the Portfolio after Port C = 0x01,
# while Port C itself clocks only between 0x00 and 0x01.
PORT_A_CLOCK_BIT = 0x02
PORT_A_DATA_BIT = 0x01
PORT_C_CLOCK_BIT = 0x01
PORT_C_DATA_BIT = 0x02
#
# This module implements the two functions PortfolioLink.cpp's peer
# (i.e. the Portfolio's own hardware/ROM, from the *other* side) must
# behave like for sendByte()/receiveByte() to work at all - not by
# reading PortfolioLink.cpp's code and reimplementing its side (that's
# already what runs on the Portfolio, we can't and don't touch that),
# but by implementing what WE must do so it doesn't matter which side
# is playing which historical role. Concretely:
#
# - atari_receive_byte(): mirrors what PortfolioLink.cpp's sendByte()
#   expects its peer to do. sendByte() drives clock+data outward (its
#   own pins) and calls waitClockLow()/waitClockHigh() to poll the
#   PEER's clock pin for each of 4 clock-pairs. The peer's expected
#   behavior (per the Atari Portfolio Technical Reference Guide's
#   description of a synchronous serial transfer, and per the
#   documented wire format in PROTOCOL.md) is to sample the data bit
#   and echo its own clock back - this is the "the other end always
#   answers by mirroring clock state" pattern.
# - atari_send_byte(): the reverse - mirrors what PortfolioLink.cpp's
#   receiveByte() expects: it waits for the peer's clock to go low,
#   samples a data bit, sets its own clock low as acknowledgement,
#   waits for the peer's clock to go high, samples again, sets its own
#   clock high. So to SEND a byte, we drive our own clock/data pins
#   through that same 4-pair sequence and expect the Portfolio (playing
#   receiveByte()'s role) to answer each edge.
#
# For WIRETEST's AL=0 transfer, the Portfolio sends the block and this
# server receives it.

import sys
import argparse
import socket
import time

HOST = "127.0.0.1"
PORT = 9999

REG_PORT_A = 0
REG_PORT_C = 1

DEFAULT_TIMEOUT_S = 2.0
TRACE = "--trace" in sys.argv


class PofoBridge:
    """Thin wrapper over the pofo_bridge TCP protocol: tracks the last
    known Port A (what we drive, i.e. what the Portfolio reads) and
    Port C (what the Portfolio drives, i.e. what we read) values, and
    lets callers wait for edges on Port C the same way
    PortfolioLink.cpp's waitClockLow()/waitClockHigh() wait on inClock.
    """

    def __init__(self, host=HOST, port=PORT):
        self.sock = socket.create_connection((host, port))
        self.sock.setblocking(True)
        self.sock.settimeout(0.05)
        self.port_a_out = 0xFF  # what we're currently driving
        self.port_c_in = None   # last value the Portfolio wrote, None = unknown yet
        self._buf = b""

    def close(self):
        self.sock.close()

    def write_port_a(self, value):
        self.port_a_out = value & 0xFF
        if TRACE:
            print(f"PORT_A -> 0x{self.port_a_out:02x}", flush=True)
        self.sock.sendall(bytes([REG_PORT_A, self.port_a_out]))

    def _pump(self, deadline):
        """Read any pending {reg, value} pairs, updating port_c_in.
        Returns True if at least one Port C update was seen this call."""
        saw_update = False
        while True:
            try:
                chunk = self.sock.recv(4096)
            except socket.timeout:
                chunk = b""
            if chunk:
                self._buf += chunk
            while len(self._buf) >= 2:
                reg, value = self._buf[0], self._buf[1]
                self._buf = self._buf[2:]
                if reg == REG_PORT_C:
                    self.port_c_in = value
                    if TRACE:
                        print(f"PORT_C <- 0x{value:02x}", flush=True)
                    saw_update = True
            if saw_update or not chunk:
                break
            if time.monotonic() >= deadline:
                break
        return saw_update

    def wait_clock(self, level, timeout_s=DEFAULT_TIMEOUT_S):
        """Wait until Port C's clock bit equals `level` (0 or 1).
        Mirrors PortfolioLink.cpp's waitClockLow()/waitClockHigh()."""
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            if self.port_c_in is not None:
                clock_bit = 1 if (self.port_c_in & PORT_C_CLOCK_BIT) else 0
                if clock_bit == level:
                    return True
            self._pump(min(deadline, time.monotonic() + 0.05))
        return False

    def get_data_bit(self):
        """Mirrors PortfolioLink.cpp's getBit(): the peer's current data
        bit (0x01) on Port C."""
        if self.port_c_in is None:
            return 0
        return 1 if (self.port_c_in & PORT_C_DATA_BIT) else 0


def atari_receive_byte(bridge: PofoBridge, timeout_s=DEFAULT_TIMEOUT_S):
    """Mirror of PortfolioLink.cpp's receiveByte() (lines 725-750), but
    playing the OTHER side: we drive Port A's clock/data (what the
    Portfolio reads) and wait for the Portfolio's own Port C clock
    edges, sampling its data bit at each edge - same 4-clock-pair, MSB
    first shape, just with our role and receiveByte()'s role swapped.
    Returns (True, byte) or (False, None) on timeout.
    """
    recv = 0
    for _ in range(4):
        if not bridge.wait_clock(0, timeout_s):
            return False, None
        bit = bridge.get_data_bit()
        recv = ((recv << 1) | bit) & 0xFF
        bridge.write_port_a(0)  # echo clock LOW

        if not bridge.wait_clock(1, timeout_s):
            return False, None
        bit = bridge.get_data_bit()
        recv = ((recv << 1) | bit) & 0xFF
        bridge.write_port_a(PORT_A_CLOCK_BIT)  # echo clock HIGH

    return True, recv


def atari_send_byte(bridge: PofoBridge, data: int, timeout_s=DEFAULT_TIMEOUT_S):
    """Mirror of PortfolioLink.cpp's sendByte() (lines 752-784): WE drive
    the clock+data pair outward on Port A and wait for the Portfolio's
    Port C clock to answer each edge - same 4-clock-pair, MSB-first
    shape as sendByte(), just running on our side instead of the ESP's.
    Returns True on success, False on timeout.
    """
    # Match PortfolioLink::sendByte().  The delay gives the peer's
    # receiveByte() loop time to enter its first waitClockLow().
    time.sleep(0.00025)
    data &= 0xFF
    for _ in range(4):
        bit = (data & 0x80) >> 7
        bridge.write_port_a((PORT_A_DATA_BIT if bit else 0) | PORT_A_CLOCK_BIT)
        bridge.write_port_a(PORT_A_DATA_BIT if bit else 0)
        data = (data << 1) & 0xFF
        if not bridge.wait_clock(0, timeout_s):
            return False

        bit = (data & 0x80) >> 7
        bridge.write_port_a(PORT_A_DATA_BIT if bit else 0)
        bridge.write_port_a((PORT_A_DATA_BIT if bit else 0) | PORT_A_CLOCK_BIT)
        data = (data << 1) & 0xFF
        if not bridge.wait_clock(1, timeout_s):
            return False

    return True


def receive_atari_block(bridge: PofoBridge, max_len=60000,
                        timeout_s=DEFAULT_TIMEOUT_S, sync_byte=ord("Z")):
    """Receive one block sent by the Portfolio.

    This is the server side of int 61h/AH=30h/AL=0.  Although the
    Portfolio is the sender, the receiver starts the transaction by
    sending 'Z'.  The Portfolio answers with A5, then its little-endian
    length, payload and additive-complement checksum.  We acknowledge
    the checksum by sending that same complement byte back.
    """
    def send_byte(value, label):
        """Log completion or timeout of a labelled outgoing byte."""
        ok = atari_send_byte(bridge, value, timeout_s)
        status = "" if ok else " TIMEOUT (incomplete byte)"
        print(f"TX {value:02X}  {label}{status}", flush=True)
        return ok

    def receive_byte(label):
        """Log an incoming byte before the protocol validates its value."""
        ok, value = atari_receive_byte(bridge, timeout_s)
        if ok:
            print(f"RX {value:02X}  {label}", flush=True)
        else:
            print(f"RX --  {label} TIMEOUT (incomplete byte)", flush=True)
        return ok, value

    if not send_byte(sync_byte, "sync"):
        return False, None

    ok, value = receive_byte("block marker")
    if not ok or value != 0xA5:
        if ok:
            print(f"unexpected block marker: expected A5, got {value:02X}", flush=True)
        return False, None

    ok, len_l = receive_byte("length low")
    if not ok:
        return False, None
    ok, len_h = receive_byte("length high")
    if not ok:
        return False, None

    length = len_l | (len_h << 8)
    if length > max_len:
        print(f"block length {length} exceeds limit {max_len}", flush=True)
        return False, None

    checksum = (len_l + len_h) & 0xFF
    payload = bytearray()
    for index in range(length):
        ok, value = receive_byte(f"payload[{index}]")
        if not ok:
            return False, None
        payload.append(value)
        checksum = (checksum + value) & 0xFF

    ok, received_checksum = receive_byte("checksum")
    expected_checksum = (-checksum) & 0xFF
    if not ok or received_checksum != expected_checksum:
        if ok:
            print(f"checksum mismatch: expected {expected_checksum:02X}, "
                  f"got {received_checksum:02X}", flush=True)
        return False, None

    # Same inter-byte settling delay as PortfolioLink::receiveBlock().
    time.sleep(0.0001)
    if not send_byte(expected_checksum, "checksum ACK"):
        return False, None

    return True, bytes(payload)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--trace", action="store_true")
    parser.add_argument("--sync-byte", type=lambda value: int(value, 0),
                        default=ord("Z"), metavar="BYTE",
                        help="byte sent before receiving a block (default: 0x5a)")
    args = parser.parse_args()
    if not 0 <= args.sync_byte <= 0xff:
        parser.error("--sync-byte must be in range 0..255")

    global TRACE
    TRACE = args.trace
    bridge = PofoBridge()
    print(f"connected to {HOST}:{PORT}; sync byte {args.sync_byte:02X}")

    try:
        while True:
            # For AL=0 the Portfolio is the sender, but its peer (us)
            # initiates the block by sending the 'Z' sync byte.  Retrying
            # lets this server start before WIRETEST.COM reaches AL=0.
            ok, payload = receive_atari_block(bridge, sync_byte=args.sync_byte)
            if ok:
                print(f"received {len(payload)} bytes: {payload!r}")
                return
            else:
                print("no active Atari transmit; retrying")
                time.sleep(0.05)
    except KeyboardInterrupt:
        pass
    finally:
        bridge.close()


if __name__ == "__main__":
    main()
