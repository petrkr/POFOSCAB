#!/usr/bin/env python3
# regression_test.py - live-hardware smoke test for PFTD, run over an
# ESP client's /sendRaw debug endpoint (POST, form param "data" = hex
# string, see the client firmware's handleSendRaw).
#
# Unlike every other tool in tests/ (DOSBox-only, run T*.COM inside an
# emulator with no real Portfolio attached), this one talks to a real
# Portfolio over a live cable, through a running client's HTTP API - it
# is meant to be run from a shell after flashing a new PFTD.COM build,
# as a fast regression check across every command before doing anything
# more targeted by hand.
#
# Deliberately uses /sendRaw (raw wire-protocol bytes) instead of the
# client's higher-level per-command endpoints (/mkdirAtari etc.):
#   - it tests PFTD itself, not the client's translation layer - a
#     failure here points at this repo, not the other one
#   - COPY (0x8C) has no higher-level endpoint at all yet, so raw bytes
#     are the only way to exercise it externally
#
# This is NOT a substitute for the test matrix in STATUS.md (write-
# protected media, no-media critical-error paths, large-file COPY
# looping, etc.) - it only checks that the common/happy-path shape of
# each command still works after a change, fast enough to run after
# every rebuild. Creates and deletes its own throwaway files/dirs
# (prefixed R) on C:/A: - never touches anything else on the card.
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
    data = urllib.parse.urlencode({"data": hexdata}).encode()
    req = urllib.request.Request(f"{BASE}/sendRaw", data=data, method="POST")
    with urllib.request.urlopen(req, timeout=20) as resp:
        return resp.read().decode().strip()


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

    # COPY (0x8C): copies an existing file (PFTD.COM, always present
    # after a fresh install) same-drive and cross-drive, plus a
    # nonexistent-source check
    r = send_raw(req(0x8C, "C:\\PFTD.COM", "C:\\RCOPY1.COM"))
    check("COPY same-drive", r, 0x20, 0)
    r = send_raw(req(0x8C, "C:\\PFTD.COM", "A:\\RCOPY2.COM"))
    check("COPY cross-drive", r, 0x20, 0)
    r = send_raw(req(0x8C, "C:\\RNOTEXIST.TXT", "C:\\RCOPY3.COM"))
    check("COPY nonexistent source", r, 0x10, 1)

    # cleanup - best effort, ignore failures
    send_raw(req(0x89, "C:\\RCOPY1.COM"))
    send_raw(req(0x89, "A:\\RCOPY2.COM"))

    print()
    passed = sum(1 for _, ok in results if ok)
    total = len(results)
    print(f"{passed}/{total} checks passed")
    if passed != total:
        sys.exit(1)


if __name__ == "__main__":
    main()
