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

**Not yet verified - open items:**

- Cross-drive rename (`C:` -> `D:`): expected to fail, exact DOS 2.x
  error code unknown. The `errcode=17` ("not same device") mapping in
  `rename.inc` is an unverified guess.
- Rename onto an existing destination: `errcode=4` expected by analogy
  with MKDIR/RMDIR, not confirmed for `AH=0x56` specifically.
- No-media / write-protected media during rename: expected to raise a
  critical error by analogy with MKDIR/DELETE/RMDIR, not confirmed.

## Known dead ends / non-issues

- `errcode=2` ("already exists") is defined in the shared MKDIR/DELETE/
  RMDIR/RENAME enum but not currently reachable on real DOS 2.x/DIP DOS
  hardware for any of them - kept reserved in case a future DOS version
  or code path needs it.
