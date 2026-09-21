#!/usr/bin/env python3
# regression_test.py - end-to-end smoke test for PFTD, run over a
# client's /sendRaw debug endpoint (POST, form param "data" = hex
# string). Talks to a real Portfolio over a live Smart Cable (ESP32
# client, see PortfolioESPlink's handleSendRaw) or to an emulated one
# (MAME's smartcable, via mame_bridge.py - see run_mame_regression.sh)
# through the same HTTP API - it is meant to be run after flashing a
# new PFTD.COM build, or after rebuilding the MAME fork, as a fast
# regression check across every command before doing anything more
# targeted by hand.
#
# Deliberately uses /sendRaw (raw wire-protocol bytes) instead of the
# client's higher-level per-command endpoints (/mkdirAtari etc.):
#   - it tests PFTD itself, not the client's translation layer - a
#     failure here points at this repo, not the other one
#   - COPY (0x8C) has no higher-level endpoint at all yet, so raw bytes
#     are the only way to exercise it externally
#
# This is NOT exhaustive (write-protected media, no-media
# critical-error paths, large-file COPY looping, etc. are not covered)
# - it only checks that the common/happy-path shape of each command
# still works after a change, fast enough to run after every rebuild.
# Creates and deletes its own throwaway files/dirs (prefixed R) on
# C:/A: - never touches anything else on the card.
#
# Usage: python3 regression_test.py <base_url>
#   e.g. python3 regression_test.py http://10.220.179.55

import sys
import json
import urllib.request
import urllib.parse

if len(sys.argv) != 2:
    print(f"Usage: {sys.argv[0]} <base_url>  (e.g. http://10.220.179.55)")
    sys.exit(2)

BASE = sys.argv[1]


def send_raw(hexdata: str) -> str:
    # /sendRaw's success response is JSON ({"ok":true,"response":"<hex>"}),
    # not a bare hex string - changed on the client firmware side at some
    # point after this script was first written. Handle both shapes so
    # this keeps working regardless of which firmware build is live.
    data = urllib.parse.urlencode({"data": hexdata}).encode()
    req = urllib.request.Request(f"{BASE}/sendRaw", data=data, method="POST")
    with urllib.request.urlopen(req, timeout=20) as resp:
        body = resp.read().decode().strip()
    try:
        parsed = json.loads(body)
    except json.JSONDecodeError:
        return body
    if isinstance(parsed, dict) and "response" in parsed:
        return parsed["response"]
    return body


def status() -> dict:
    with urllib.request.urlopen(f"{BASE}/status", timeout=10) as resp:
        return json.loads(resp.read().decode())


def path_bytes(p: str) -> bytes:
    return p.encode("ascii") + b"\x00"


def req(cmd: int, *paths: str) -> str:
    payload = bytes([cmd, 0x00, 0x70])
    for p in paths:
        payload += path_bytes(p)
    return payload.hex()


def pack_date(year: int, month: int, day: int) -> int:
    return ((year - 1980) << 9) | (month << 5) | day


def pack_time(hour: int, minute: int, second: int) -> int:
    return (hour << 11) | (minute << 5) | (second // 2)


def req_bin(cmd: int, payload: bytes) -> str:
    return (bytes([cmd, 0x00, 0x70]) + payload).hex()


# ROM-native transmit (0x03, upload)/receive (0x02, download) live in
# mame_bridge.py (MameLink.upload_file/download_file) - they need
# several send_block()-without-receive calls in a row (the ROM only
# replies once, after the LAST data chunk), which /sendRaw's
# send+receive-pair-per-call shape can't express. Exposed here over
# /uploadFile and /downloadFile.


def upload_file(path: str, data: bytes) -> bool:
    body = urllib.parse.urlencode({"path": path, "data": data.hex()}).encode()
    req = urllib.request.Request(f"{BASE}/uploadFile", data=body, method="POST")
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.loads(resp.read().decode())["ok"]


def download_file(path: str) -> bytes | None:
    body = urllib.parse.urlencode({"path": path}).encode()
    req = urllib.request.Request(f"{BASE}/downloadFile", data=body, method="POST")
    with urllib.request.urlopen(req, timeout=30) as resp:
        parsed = json.loads(resp.read().decode())
    return bytes.fromhex(parsed["data"]) if parsed["ok"] else None


results = []


def check(name: str, hexresp: str, expect_status: int, expect_errcode=None):
    ok_status = hexresp[:2].lower() == f"{expect_status:02x}"
    ok_errcode = True
    if expect_errcode is not None:
        ok_errcode = len(hexresp) >= 4 and hexresp[2:4].lower() == f"{expect_errcode:02x}"
    ok = ok_status and ok_errcode
    results.append((name, ok))
    print(f"[{'PASS' if ok else 'FAIL'}] {name}: got {hexresp!r}")


def main():
    st = status()
    pftd = st.get("pftd") or {}
    print(f"pftd buildId={pftd.get('buildId')} capabilities={pftd.get('capabilities')}")
    if not st.get("connected"):
        print("ERROR: Portfolio not connected, aborting")
        sys.exit(1)

    # HELLO (0x80) - single byte request, no path
    r = send_raw("80")
    check("HELLO magic ('PFD1')", "20" if r[:8].lower() == "50464431" else "00", 0x20)

    # DRIVES (0x87) - single byte response
    r = send_raw("87")
    check("DRIVES single byte", "20" if len(r) == 2 else "00", 0x20)

    # MKDIR (0x88): fresh dir succeeds, same dir again is access denied
    r = send_raw(req(0x88, "C:\\RTEST1"))
    check("MKDIR new dir", r, 0x20, 0)
    r = send_raw(req(0x88, "C:\\RTEST1"))
    check("MKDIR existing dir", r, 0x10, 4)

    # LIST extended (0x86): just confirm it returns a count prefix
    # without hanging/crashing
    r = send_raw(req(0x86) + path_bytes("C:\\*.*").hex())
    check("LIST extended basic", "20" if len(r) >= 4 else "00", 0x20)

    # RENAME (0x8B): same-drive rename succeeds
    r = send_raw(req(0x8B, "C:\\RTEST1", "C:\\RTEST2"))
    check("RENAME same-drive dir", r, 0x20, 0)

    # DELETE (0x89) on a nonexistent file: not found
    r = send_raw(req(0x89, "C:\\RNOTEXIST.TXT"))
    check("DELETE nonexistent", r, 0x10, 1)

    # RMDIR (0x8A): clean up the renamed dir
    r = send_raw(req(0x8A, "C:\\RTEST2"))
    check("RMDIR cleanup", r, 0x20, 0)

    # Upload/download (ROM-native 0x03/0x02): create our own source
    # file rather than assuming PFTD.COM or anything else already
    # exists on the card - this test must not depend on card contents
    # it didn't create itself.
    test_content = b"POFOSCAB REGRESSION TEST FILE\r\n"
    ok = upload_file("C:\\RSRC.TXT", test_content)
    results.append(("UPLOAD test source file", ok))
    print(f"[{'PASS' if ok else 'FAIL'}] UPLOAD test source file")

    downloaded = download_file("C:\\RSRC.TXT")
    roundtrip_ok = downloaded == test_content
    results.append(("DOWNLOAD round-trip matches upload", roundtrip_ok))
    print(f"[{'PASS' if roundtrip_ok else 'FAIL'}] DOWNLOAD round-trip matches upload: got {downloaded!r}")

    # COPY (0x8C): copies our own uploaded file same-drive and
    # cross-drive, plus a nonexistent-source check
    r = send_raw(req(0x8C, "C:\\RSRC.TXT", "C:\\RCOPY1.TXT"))
    check("COPY same-drive", r, 0x20, 0)
    r = send_raw(req(0x8C, "C:\\RSRC.TXT", "A:\\RCOPY2.TXT"))
    check("COPY cross-drive", r, 0x20, 0)
    r = send_raw(req(0x8C, "C:\\RNOTEXIST.TXT", "C:\\RCOPY3.TXT"))
    check("COPY nonexistent source", r, 0x10, 1)

    # cleanup - best effort, ignore failures
    send_raw(req(0x89, "C:\\RSRC.TXT"))
    send_raw(req(0x89, "C:\\RCOPY1.TXT"))
    send_raw(req(0x89, "A:\\RCOPY2.TXT"))

    # GETDATETIME (0x8D): single-byte request, 4-byte packed date+time
    # response, no status/errcode. Just confirm it returns 4 bytes and
    # decodes to a plausible date.
    r = send_raw("8d")
    ok = len(r) == 8
    results.append(("GETDATETIME basic", ok))
    print(f"[{'PASS' if ok else 'FAIL'}] GETDATETIME basic: got {r!r}")
    if ok:
        raw = bytes.fromhex(r)
        packed_date = raw[0] | (raw[1] << 8)
        packed_time = raw[2] | (raw[3] << 8)
        year = 1980 + (packed_date >> 9)
        month = (packed_date >> 5) & 0x0F
        day = packed_date & 0x1F
        hour = packed_time >> 11
        minute = (packed_time >> 5) & 0x3F
        second = (packed_time & 0x1F) * 2
        print(f"    decoded: {year:04d}-{month:02d}-{day:02d} {hour:02d}:{minute:02d}:{second:02d}")
        plausible = 1980 <= year <= 2107 and 1 <= month <= 12 and 1 <= day <= 31 and hour <= 23 and minute <= 59
        results.append(("GETDATETIME decodes to plausible values", plausible))
        print(f"[{'PASS' if plausible else 'FAIL'}] GETDATETIME decodes to plausible values")

    # SETDATETIME (0x8E) round-trip: set a known date/time, read it
    # back via GETDATETIME, confirm it stuck.
    set_date = pack_date(2026, 9, 19)
    set_time = pack_time(23, 00, 0)
    payload = bytes([set_date & 0xFF, set_date >> 8, set_time & 0xFF, set_time >> 8])
    r = send_raw(req_bin(0x8E, payload))
    check("SETDATETIME valid value", r, 0x20, 0)

    r = send_raw("8d")
    if len(r) == 8:
        raw = bytes.fromhex(r)
        got_date = raw[0] | (raw[1] << 8)
        got_time_hour = (raw[2] | (raw[3] << 8)) >> 11
        roundtrip_ok = got_date == set_date and got_time_hour == 23
        results.append(("SETDATETIME round-trip", roundtrip_ok))
        print(f"[{'PASS' if roundtrip_ok else 'FAIL'}] SETDATETIME round-trip: date={got_date:#06x} (expected {set_date:#06x}), hour={got_time_hour} (expected 23)")

    # SETDATETIME with an out-of-range month (13): expect errcode=4,
    # per RBIL's documented AL=0xFF failure mode.
    bad_date = pack_date(2026, 13, 1)
    payload = bytes([bad_date & 0xFF, bad_date >> 8, 0x00, 0x00])
    r = send_raw(req_bin(0x8E, payload))
    check("SETDATETIME invalid month", r, 0x10, 4)

    print()
    passed = sum(1 for _, ok in results if ok)
    total = len(results)
    print(f"{passed}/{total} checks passed")
    if passed != total:
        sys.exit(1)


if __name__ == "__main__":
    main()
