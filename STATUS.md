# Implementation status

Real-hardware verification status per command. This is the living
summary; the full test logs and reasoning behind each result are in
`rom/ROM_RESEARCH_NOTES.md`. Source comments (`*.inc`) describe wire
format and mechanism, not verification state - that belongs here so it
doesn't go stale in the code as testing progresses.

## HELLO (`0x80`)

Verified on real hardware. Response bytes checked via DOSBox
(`tests/THELLO.asm`, static inspection of assembled `hello_response`)
and end-to-end over the cable.

## LIST extended (`0x86`)

Verified on real hardware, including the free/total drive space
calculation appended after entries.

## DRIVES (`0x87`)

Verified on real hardware. `AH=0x0E`/`AH=0x19` confirmed I/O-free and
the `1..count` contiguity confirmed architecturally (no DOS 2.x/DIP DOS
mechanism can create a gap) and empirically (4th test drive landed on
`D:` as expected). See `ROM_RESEARCH_NOTES.md`'s "Kontiguita" section.

## MKDIR (`0x88`)

Verified on real hardware (build `0xFFFF0012`, after a CF-ordering bug
fix - see `ROM_RESEARCH_NOTES.md`'s MKDIR/DELETE section). Confirmed:

- New directory: success.
- Already-existing directory: `errcode=4` (access denied) - DOS 2.x's
  `AH=0x39` has no distinct "already exists" code, folds into the same
  code as other access-denied cases. `errcode=2` in the shared enum is
  therefore unreachable on this DOS/hardware; kept reserved for a future
  DOS version, not actively expected.
- No media (`A:`, empty card slot): confirmed via isolated
  `TMKDIRA.COM` - raises a real critical error (`int 0x24`), handled
  correctly by `critical_error.inc`.
- Write-protected media (`D:`): `errcode=1` (not found), *not*
  `errcode=4` as expected - unexplained, not investigated further
  (either `D:`-specific or a general AH=0x39 not-found/access-denied
  ambiguity on DOS 2.x).

## DELETE (`0x89`)

Verified on real hardware (build `0xFFFF0012`+). Confirmed:

- Existing file: success (checked via listing + free-space delta).
- Nonexistent file: `errcode=1` (not found).
- No media (`A:`): critical error fired, handled correctly.
- Write-protected media (`D:`): critical error fired (unlike MKDIR on
  the same drive, which got a plain DOS error) - `AH=0x41`/Unlink is
  more critical-error-prone than `AH=0x39` on this hardware.
- Directory instead of file (`AH=0x41` on a dir): plain DOS error
  (`CF=1, AX=3`), confirmed via isolated `TUNLKDIR.COM` - the earlier
  `errcode=0xFF` seen through PFTD for this case was NOT a real critical
  error; root cause unexplained but deemed not worth chasing (DELETE is
  file-only by design, calling it on a directory is out of supported
  use anyway).

## RMDIR (`0x8A`)

Verified on real hardware (build `0xFFFF0013`). Confirmed:

- Empty directory: success.
- Non-empty directory: `errcode=4` (access denied) - same
  no-distinct-code situation as MKDIR's "already exists".
- Nonexistent path: `errcode=1` (not found).

Not separately tested: no-media / write-protected scenarios for RMDIR -
assumed to behave like MKDIR by analogy, not verified.

## RENAME (`0x8B`)

Partially verified on real hardware (build `0xFFFF0014`, after fixing a
client-side transport bug that sent the full local buffer instead of
the actual request length - see `ROM_RESEARCH_NOTES.md`). Confirmed:

- Rename within the same directory: success.
- Move across directories on the same drive: success (`AH=0x56` works
  as a cross-directory move, not just same-directory rename).
- Nonexistent source: `errcode=1` (not found).
- Cross-drive rename (`C:\XTEST` -> `A:\XTEST`, build `6B635F18`):
  `errcode=4` (access denied) - confirms the existing mapping in
  `rename.inc` (DOS error 5 or 17, both folded into errcode 4).
  Confirmed on both a directory and a file (`C:\XTEST.TXT` ->
  `A:\XTEST.TXT`) - same result either way. Source was left untouched
  on `C:` in both cases, device stayed responsive - no critical error
  on this path.

**Not yet verified - open items:**

- Rename onto an existing destination: `errcode=4` expected by analogy
  with MKDIR/RMDIR, not confirmed for `AH=0x56` specifically.
- No-media / write-protected media during rename: expected to raise a
  critical error by analogy with MKDIR/DELETE/RMDIR, not confirmed.

## COPY (`0x8C`)

Partially verified on real hardware (build `0xFFFF0015`). Confirmed:

- Same-drive copy (`C:\CPYTEST.TXT` -> `C:\CPYOUT.TXT`, 27 bytes):
  `status=0x20, errcode=0`. Destination created with matching size;
  content verified byte-for-byte identical to source via download and
  diff, not just size/existence. Source left untouched. Device stayed
  responsive.
- Cross-drive copy (`C:\CPYTEST.TXT` -> `A:\CPYOUT.TXT`, 27 bytes) -
  the actual point of this command over RENAME: `status=0x20,
  errcode=0`. Destination created on `A:` with matching size and
  byte-for-byte identical content (verified via download and diff).
  Source left untouched on `C:`. Device stayed responsive.

- Copy from a nonexistent source (`C:\RNOTEXIST.TXT`, build
  `0xFFFF0016`, via `tests/regression_test.py`): `errcode=1` (not
  found), as expected.

**Not yet verified - open items:**

- Copy onto an existing destination (overwrite behavior).
- Copy to a full or write-protected destination (disk-full short-write
  detection and the partial-destination cleanup-on-failure path -
  the highest-risk new logic in this command, no analogue elsewhere
  in the codebase).
- A file larger than `COPY_BUFSIZE` (512 bytes), to confirm the
  read/write loop actually iterates more than once.
- Destination attribute preservation (read-only/hidden/system byte
  copied from source via Find First) - not yet checked against actual
  DOS attribute output.

## GETDATETIME/SETDATETIME (`0x8D`/`0x8E`)

Verified on real hardware (build `0xFFFF0017`, via
`tests/regression_test.py`). Confirmed:

- `GETDATETIME`: returns a plausible packed date/time (4 bytes,
  `status`/`errcode`-free as designed). Before any `SETDATETIME` call,
  decoded to `1980-01-01 01:03:44` - DOS's default/unset clock value,
  not a real-time-clock reading; this Portfolio does not appear to
  retain time across power cycles on its own (or DOS starts from this
  epoch regardless) - `AH=0x2A`/`AH=0x2C` themselves work correctly,
  this is a fact about the device's clock state, not a bug.
- `SETDATETIME` with a valid date/time (`2026-09-19 14:30:00`):
  `status=0x20, errcode=0`, and a following `GETDATETIME` confirmed
  the value actually stuck (packed date and hour matched exactly).
- `SETDATETIME` with an out-of-range month (13): `status=0x10,
  errcode=4` - confirms `AH=0x2B` returns `AL=0xFF` on DIP DOS as RBIL
  documents, no divergence found here (unlike `AH=0x32`/`AH=0x36`).
- No critical error fired for any of the above, and the device stayed
  fully responsive throughout - confirms the `AH=0x0E`/`AH=0x19`
  analogy (DRIVES) extends to `AH=0x2A/0x2B/0x2C/0x2D`: none of the
  four touch disk/media, all four are critical-error-free on this
  hardware.
- Retested after a Portfolio restart: `GETDATETIME` still returned a
  sane value - the clock survived the restart, it was not reset back
  to the 1980 epoch. `SETDATETIME` to a new value, followed by
  `GETDATETIME`, round-tripped correctly again.

**Not yet verified - open items:**

- `SETDATETIME` with other out-of-range values (invalid day, hour,
  minute, second) - only an invalid month was tested.
- Whether the clock surviving a restart is battery-backed RTC
  persistence or something else - not investigated further, out of
  scope for this driver.
- Whether the Portfolio's System Setup clock display actually reflects
  a `SETDATETIME` call (not cross-checked visually, only via
  `GETDATETIME` round-trip).
- Behavior across a real power cycle (does the clock reset to the
  1980 epoch again, or does `SETDATETIME`'s effect persist?).

## Known dead ends / non-issues

- `errcode=2` ("already exists") is defined in the shared MKDIR/DELETE/
  RMDIR/RENAME enum but not currently reachable on real DOS 2.x/DIP DOS
  hardware for any of them - kept reserved in case a future DOS version
  or code path needs it.
