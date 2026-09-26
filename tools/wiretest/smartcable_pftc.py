#!/usr/bin/env python3
"""Mock server for the Portfolio File Transfer Configuration client.

Iteration 0.0: only HELLO (0x01) is implemented, returning the 14-byte
version block PFTC.asm's hello.inc parses. Later iterations add
GET_NETIFS/GET_NETIF/SET_NETIF/GET_IPCFG/SET_IPCFG/GET_WIFISCAN - see
the design plan (mame-tu-novy-ukol-lovely-book.md) for their wire
layouts.
"""

import argparse
import struct

from smartcable_server import SmartCable, receive_atari_block

PFTC_HELLO = 0x01

PFTC_OK = 0x20
PFTC_ERR_UNKNOWN_COMMAND = 0x01

MOCK_BUILD_ID = 0xFFFF0000
MOCK_VERSION = (0, 0, 0)


def build_hello_response() -> bytes:
    major, minor, patch = MOCK_VERSION
    return struct.pack(
        "<BB4sIBBBB",
        PFTC_OK,
        0x00,
        b"PFC1",
        MOCK_BUILD_ID,
        major,
        minor,
        patch,
        0x00,
    )


def send_block_after_sync(link: SmartCable, payload: bytes) -> bool:
    """Send a block after the Atari's Z sync was already received.

    Same two-step split as mame_bridge_v3.py's run_server: the Z byte
    that the Portfolio's AL=1 handler emits is read as its own explicit
    step (see main()'s loop below), and this function only builds and
    sends the frame - it does not wait for Z itself. This whole
    exchange is PUSH throughout (WW.asm's AL=0 transmit followed by
    AL=1 receive), never PULL - see wiretest.md.
    """
    length = len(payload)
    len_l = length & 0xFF
    len_h = length >> 8
    checksum = (-(len_l + len_h + sum(payload))) & 0xFF
    frame = bytes((0xA5, len_l, len_h)) + payload + bytes((checksum,))
    for value in frame:
        if not link.send_byte(value):
            return False
    return link.receive_byte() == checksum


def handle_packet(payload: bytes) -> bytes:
    if payload == bytes((PFTC_HELLO,)):
        print("PFTC HELLO", flush=True)
        return build_hello_response()

    print(f"PFTC unknown opcode: {payload!r}", flush=True)
    return bytes((0x10, PFTC_ERR_UNKNOWN_COMMAND))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=9997)
    parser.add_argument("--max-len", type=int, default=60000)
    parser.add_argument("--trace", action="store_true")
    args = parser.parse_args()

    link = SmartCable(args.host, args.port, args.trace)
    print(f"connected to {args.host}:{args.port}", flush=True)
    try:
        link.watch_port_c()
        print("waiting for PFTC packets", flush=True)
        while True:
            link.wait_port_c_event()
            payload = receive_atari_block(link, args.max_len)
            if payload is None:
                print("Port C event did not start a block; waiting", flush=True)
                continue
            response = handle_packet(payload)
            # WIRETEST/PFTC follows a successful AL=0 with AL=1. Its
            # AL=1 handler emits the repeated Z sync; consume it before
            # sending the response block - see mame_bridge_v3.py's
            # run_server for the identical two-step pattern.
            sync = link.receive_byte()
            if sync == ord("Z"):
                if send_block_after_sync(link, response):
                    print(f"sent response {response.hex()}", flush=True)
                else:
                    print("response send failed", flush=True)
            else:
                print(f"did not receive response Z sync (got {sync!r})", flush=True)
    finally:
        link.close()


if __name__ == "__main__":
    main()
