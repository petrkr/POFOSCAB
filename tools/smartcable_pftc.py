#!/usr/bin/env python3
"""Mock server for the Portfolio File Transfer Configuration client.

Implements HELLO (0x01), GET_NETIFS (0x02), GET_NETIF (0x03),
SET_NETIF (0x04), GET_WIFISCAN (0x07), and GET_DASHBOARD (0x08, mode
FULL only) per PFTC_PROTOCOL.md. 0x05/0x06 (formerly GET_IPCFG/
SET_IPCFG) were folded into GET_NETIF/SET_NETIF's common header and no
longer exist. GET_DASHBOARD's mode=PARTIAL is a protocol draft only,
not implemented here.

NetifSlot/WifiScanResult are written MicroPython-portable (no
dataclasses, no typing imports used at runtime, only stdlib
struct/bytes operations) since the same classes are meant to run on
the ESP side, not just in this test mock.
"""

import argparse
import struct

from smartcable_server import SmartCable, receive_atari_block

PFTC_HELLO = 0x01
PFTC_GET_NETIFS = 0x02
PFTC_GET_NETIF = 0x03
PFTC_SET_NETIF = 0x04
PFTC_GET_WIFISCAN = 0x07
PFTC_GET_DASHBOARD = 0x08

DASHBOARD_MODE_FULL = 0x00
DASHBOARD_MODE_PARTIAL = 0x01

PFTC_OK = 0x20
PFTC_ERR = 0x10
PFTC_ERR_UNKNOWN_COMMAND = 0x01
PFTC_ERR_MALFORMED = 0x02
PFTC_ERR_NOT_CONNECTED = 0x03
PFTC_ERR_NOT_SUPPORTED = 0x04

IP_MODE_DHCP = 0x00
IP_MODE_STATIC = 0x01

SECURITY_OPEN = 0x00
SECURITY_WPA2_PSK = 0x01
SECURITY_WPA3_PSK = 0x02

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
    type/enabled/connection state/IPv4/netmask prefix/gateway/DNS/
    IP mode/IPv6) followed by the type-specific section (channel/RSSI/
    SSID for type=0x01 - the fixed-size fields come first so the client
    can read them at a constant offset without first parsing the
    variable-length SSID; a future type would need its own to_bytes
    branch here).

    `ip_mode`/`ipv6_enabled` are addressing-mode config, not tied to
    any one type - formerly their own GET_IPCFG/SET_IPCFG opcodes
    (0x05/0x06), folded into this common header since addressing mode
    never needed its own round trip. When `ip_mode` is static, `ipv4`/
    `netmask_prefix`/`gateway` above already hold the active static
    values - there is no separate static-address echo.
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
        ip_mode=IP_MODE_DHCP,
        ipv6_enabled=0x00,
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
        self.ip_mode = ip_mode
        self.ipv6_enabled = ipv6_enabled
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
        common_header += bytes((self.ip_mode, self.ipv6_enabled))

        if self.type == TYPE_WIFI_CLIENT:
            ssid_bytes = self.ssid.encode("ascii")
            type_section = struct.pack("<Bb", self.channel, self.rssi)
            type_section += bytes((len(ssid_bytes),)) + ssid_bytes
        else:
            type_section = b""

        return common_header + type_section


class WifiScanResult:
    """One GET_WIFISCAN entry (see PFTC_PROTOCOL.md's GET_WIFISCAN
    section). SSID bytes are transmitted exactly as given, unmodified -
    the protocol allows arbitrary non-ASCII SSIDs, so `ssid` here is
    already raw bytes, not a str, unlike NetifSlot's ASCII-only ssid.
    """

    def __init__(self, ssid: bytes, rssi: int, security: int):
        self.ssid = ssid
        self.rssi = rssi
        self.security = security

    def to_bytes(self) -> bytes:
        return (
            bytes((len(self.ssid),))
            + self.ssid
            + struct.pack("<b", self.rssi)
            + bytes((self.security,))
        )


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

# Mock scan results for interface=0x00. Fixed/fake list, capped at 10
# entries per the protocol - includes the currently-connected SSID plus
# a few fabricated neighbors covering each security type and an
# intentionally non-ASCII SSID to exercise the "raw bytes, no
# filtering" rule.
mock_wifiscan_results = [
    WifiScanResult(ssid=b"MockSSID", rssi=-45, security=SECURITY_WPA2_PSK),
    WifiScanResult(ssid=b"OpenGuest", rssi=-60, security=SECURITY_OPEN),
    WifiScanResult(ssid=b"Neighbour5G", rssi=-72, security=SECURITY_WPA3_PSK),
    WifiScanResult(ssid="Caf\xe9WiFi".encode("latin-1"), rssi=-80, security=SECURITY_WPA2_PSK),
]


class DashboardEntry:
    """One GET_DASHBOARD FULL entry (see PFTC_PROTOCOL.md's
    GET_DASHBOARD section). `row`/`col` are packed on the wire as
    POFO_COORD(row, col) - high byte row, low byte col - screen-
    absolute, matching pofo.h's INT 60h coordinate packing so the
    client can gotoxy() straight off these bytes. `payload` is raw
    bytes (not ASCII-only) - CP437 glyphs like the 0xB3 separator and
    0xDB/0xB0 signal-bar blocks are >0x7F, same as WifiScanResult's
    SSID handling above.
    """

    def __init__(self, id_: int, row: int, col: int, payload: bytes):
        self.id = id_
        self.row = row
        self.col = col
        self.payload = payload

    def to_bytes(self) -> bytes:
        return (
            bytes((self.id, self.row, self.col, len(self.payload)))
            + self.payload
        )


def _signal_bar_bytes(rssi: int) -> bytes:
    """Mirrors gui_core.c's print_signal_bar() exactly (same
    SIGNAL_BAR_FULL/EMPTY glyphs, same MIN/MAX/LEVELS thresholds) -
    the mock sends the finished bar bytes so the client stays a dumb
    writer, never recomputing bar fill from RSSI itself.
    """
    SIGNAL_BAR_FULL = 0xDB
    SIGNAL_BAR_EMPTY = 0xB0
    SIGNAL_BAR_MIN = -90
    SIGNAL_BAR_MAX = -40
    LEVELS = 10

    if rssi <= SIGNAL_BAR_MIN:
        filled = 0
    elif rssi >= SIGNAL_BAR_MAX:
        filled = LEVELS
    else:
        filled = (rssi - SIGNAL_BAR_MIN) * LEVELS // (SIGNAL_BAR_MAX - SIGNAL_BAR_MIN)
    return bytes(SIGNAL_BAR_FULL if i < filled else SIGNAL_BAR_EMPTY for i in range(LEVELS))


# Fixed/fake dashboard layout for timing/render measurement only - ids
# and text are placeholders, not a finalized field-id scheme (that's
# still open per PFTC_PROTOCOL.md). Mirrors draw_dashboard()'s rows 1-5
# in gui.c, including the 0xB3 CP437 separator and the signal-bar
# glyphs print_signal_bar() draws - not a plain ASCII pipe/number.
_mock_rssi = -45
mock_dashboard_entries = [
    DashboardEntry(1, 1, 2, b"SSID: MockSSID"),
    DashboardEntry(2, 1, 38 - 10 - 3, b" " + bytes((0xB3,)) + b" " + _signal_bar_bytes(_mock_rssi)),
    DashboardEntry(3, 2, 2, b"IP: 192.168.1.42/24"),
    DashboardEntry(4, 3, 2, b"GW: 192.168.1.1"),
    DashboardEntry(5, 4, 2, b"DNS: 192.168.1.1"),
    DashboardEntry(6, 5, 2, b"CH6"),
]


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


def build_status_response(status: int, error_code: int) -> bytes:
    return bytes((status, error_code))


def parse_set_netif(payload: bytes):
    """Parse a SET_NETIF request body (after the opcode byte), per
    PFTC_PROTOCOL.md's SET_NETIF layout for type=0x01 WiFi client.
    Returns (interface, type_, enabled, ip_mode, ip, netmask_prefix,
    gateway, ipv6_enabled, ssid, psk) or None if malformed (too short /
    length-prefixed fields run past the end). netmask_prefix is a CIDR
    length (0-32), not a dotted mask - expanding it to a literal mask
    is the ESP's job, not the client's.
    """
    if len(payload) < 14:
        return None
    interface, type_, enabled, ip_mode = payload[0], payload[1], payload[2], payload[3]
    ip = tuple(payload[4:8])
    netmask_prefix = payload[8]
    gateway = tuple(payload[9:13])
    ipv6_enabled = payload[13]
    offset = 14
    if offset >= len(payload):
        return None
    ssid_len = payload[offset]
    offset += 1
    if offset + ssid_len > len(payload):
        return None
    ssid = payload[offset:offset + ssid_len].decode("ascii", errors="replace")
    offset += ssid_len
    if offset >= len(payload):
        return None
    psk_len = payload[offset]
    offset += 1
    if offset + psk_len > len(payload):
        return None
    psk = payload[offset:offset + psk_len].decode("ascii", errors="replace")
    offset += psk_len
    if offset != len(payload):
        return None
    return interface, type_, enabled, ip_mode, ip, netmask_prefix, gateway, ipv6_enabled, ssid, psk


def handle_set_netif(payload: bytes) -> bytes:
    """Log-only mock: prints what SET_NETIF received and always
    acknowledges success. Does NOT mutate mock_netif - this mock is
    for exercising the client's request-building/wire format, not for
    simulating a stateful ESP that remembers what was applied.
    """
    parsed = parse_set_netif(payload)
    if parsed is None:
        print(f"PFTC SET_NETIF malformed payload: {payload!r}", flush=True)
        return build_status_response(PFTC_ERR, PFTC_ERR_MALFORMED)
    interface, type_, enabled, ip_mode, ip, netmask_prefix, gateway, ipv6_enabled, ssid, psk = parsed
    print(f"PFTC SET_NETIF interface={interface} type={type_} enabled={enabled} "
          f"ip_mode={ip_mode} ip={ip} netmask_prefix={netmask_prefix} gateway={gateway} "
          f"ipv6={ipv6_enabled} ssid={ssid!r} psk={'*' * len(psk)}", flush=True)
    return build_status_response(PFTC_OK, 0x00)


def build_wifiscan_response(interface: int) -> bytes:
    if interface != mock_netif.interface:
        return bytes((PFTC_ERR, PFTC_ERR_MALFORMED))
    if mock_netif.type != TYPE_WIFI_CLIENT:
        return bytes((PFTC_ERR, PFTC_ERR_NOT_SUPPORTED))

    results = mock_wifiscan_results[:10]
    body = bytes((PFTC_OK, 0x00, len(results)))
    for result in results:
        body += result.to_bytes()
    return body


def build_dashboard_response(mode: int) -> bytes:
    if mode == DASHBOARD_MODE_PARTIAL:
        # Draft only per PFTC_PROTOCOL.md - no shape decided yet.
        return bytes((PFTC_ERR, PFTC_ERR_NOT_SUPPORTED))
    if mode != DASHBOARD_MODE_FULL:
        return bytes((PFTC_ERR, PFTC_ERR_MALFORMED))

    body = bytes((PFTC_OK, 0x00))
    for entry in mock_dashboard_entries:
        body += entry.to_bytes()
    return body


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

    if len(payload) >= 1 and payload[0] == PFTC_SET_NETIF:
        return handle_set_netif(payload[1:])

    if len(payload) == 2 and payload[0] == PFTC_GET_WIFISCAN:
        interface = payload[1]
        print(f"PFTC GET_WIFISCAN interface={interface}", flush=True)
        return build_wifiscan_response(interface)

    if len(payload) == 2 and payload[0] == PFTC_GET_DASHBOARD:
        mode = payload[1]
        print(f"PFTC GET_DASHBOARD mode={mode}", flush=True)
        return build_dashboard_response(mode)

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
