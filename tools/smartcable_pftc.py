#!/usr/bin/env python3
"""Mock server for the Portfolio File Transfer Configuration client.

Implements HELLO (0x01), GET_NETIFS (0x02), and GET_NETIF (0x03) per the
design plan (mame-tu-novy-ukol-lovely-book.md). SET_NETIF/GET_IPCFG/
SET_IPCFG/GET_WIFISCAN are not implemented yet. GET_STATUS (0x08) is an
ESP-wide device status opcode with no designed response shape yet - not
implemented.

NetifSlot is written MicroPython-portable (no dataclasses, no typing
imports used at runtime, only stdlib struct/bytes operations) since the
same class is meant to run on the ESP side, not just in this test mock.
"""

import argparse
import struct

from smartcable_server import SmartCable, receive_atari_block

PFTC_HELLO = 0x01
PFTC_GET_NETIFS = 0x02
PFTC_GET_NETIF = 0x03

PFTC_OK = 0x20
PFTC_ERR = 0x10
PFTC_ERR_UNKNOWN_COMMAND = 0x01
PFTC_ERR_MALFORMED = 0x02

MOCK_BUILD_ID = 0xFFFF0000
MOCK_VERSION = (0, 0, 0)

TYPE_RESERVED = 0x00
TYPE_WIFI_CLIENT = 0x01

CONN_DISCONNECTED = 0x00
CONN_CONNECTING = 0x01
CONN_CONNECTED = 0x02


class NetifSlot:
    """One interface slot (see the design plan's GET_NETIFS/GET_NETIF
    sections for the exact wire layout this mirrors). `interface` is a
    stable slot number, `type` is the interface kind (0x01 = WiFi
    client - the only kind implemented so far).

    to_bytes(full=False) returns just the GET_NETIFS list-entry form
    (interface/type/enabled, 3 bytes). to_bytes(full=True) returns the
    complete GET_NETIF response body (status+error code are NOT
    included - the caller prepends those): common header (interface/
    type/enabled/connection state/IPv4/netmask prefix/gateway/DNS)
    followed by the type-specific section (channel/RSSI/SSID for
    type=0x01 - the fixed-size fields come first so the client can read
    them at a constant offset without first parsing the variable-length
    SSID; a future type would need its own to_bytes branch here).
    """

    def __init__(
        self,
        interface,
        type_,
        enabled,
        connection_state=CONN_DISCONNECTED,
        ipv4=(0, 0, 0, 0),
        netmask_prefix=0,
        gateway=(0, 0, 0, 0),
        dns=(0, 0, 0, 0),
        ssid="",
        channel=0,
        rssi=-128,
    ):
        self.interface = interface
        self.type = type_
        self.enabled = enabled
        self.connection_state = connection_state
        self.ipv4 = ipv4
        self.netmask_prefix = netmask_prefix
        self.gateway = gateway
        self.dns = dns
        self.ssid = ssid
        self.channel = channel
        self.rssi = rssi

    def to_bytes(self, full=False):
        if not full:
            return bytes((self.interface, self.type, self.enabled))

        common_header = bytes(
            (self.interface, self.type, self.enabled, self.connection_state)
        )
        common_header += bytes(self.ipv4)
        common_header += bytes((self.netmask_prefix,))
        common_header += bytes(self.gateway)
        common_header += bytes(self.dns)

        if self.type == TYPE_WIFI_CLIENT:
            ssid_bytes = self.ssid.encode("ascii")
            type_section = struct.pack("<Bb", self.channel, self.rssi)
            type_section += bytes((len(ssid_bytes),)) + ssid_bytes
        else:
            type_section = b""

        return common_header + type_section


# Mock state: a single interface slot, interface=0x00, type=0x01 (WiFi
# client). Static/fake - no real radio.
mock_netif = NetifSlot(
    interface=0x00,
    type_=TYPE_WIFI_CLIENT,
    enabled=0x01,
    connection_state=CONN_CONNECTED,
    ipv4=(192, 168, 1, 42),
    netmask_prefix=24,
    gateway=(192, 168, 1, 1),
    dns=(192, 168, 1, 1),
    ssid="MockSSID",
    channel=6,
    rssi=-45,
)


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


def build_netifs_response() -> bytes:
    return bytes((PFTC_OK, 0x00, 1)) + mock_netif.to_bytes(full=False)


def build_netif_response(interface: int) -> bytes:
    if interface != mock_netif.interface:
        return bytes((PFTC_ERR, PFTC_ERR_MALFORMED))
    return bytes((PFTC_OK, 0x00)) + mock_netif.to_bytes(full=True)


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

    if payload == bytes((PFTC_GET_NETIFS,)):
        print("PFTC GET_NETIFS", flush=True)
        return build_netifs_response()

    if len(payload) == 2 and payload[0] == PFTC_GET_NETIF:
        interface = payload[1]
        print(f"PFTC GET_NETIF interface={interface}", flush=True)
        return build_netif_response(interface)

    print(f"PFTC unknown opcode: {payload!r}", flush=True)
    return bytes((PFTC_ERR, PFTC_ERR_UNKNOWN_COMMAND))


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
