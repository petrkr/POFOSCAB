# AGENTS.md

Conventions for coding agents working on POFOSCAB

## Toolchain (BCC / dev86)

- No `inp()`/`outp()`; port I/O via `#asm ... #endasm`.
- A function whose body is only `#asm` gets NO `push bp; mov bp,sp`
  prologue. Add `push bp` / `mov bp,sp` ... `pop bp` manually; then
  `4[bp]` = 1st arg, `6[bp]` = 2nd arg.

## Upload through the bridge

Upload a binary using the verified multipart POST command:

```sh
curl -sS -i -F "file=@build/PROGRAM.COM" \
  "http://127.0.0.1:9000/uploadFile"
```

The bridge must be running and the Portfolio must be in File Transfer Server
mode. With no `destDir` parameter, the bridge uses its `C:\` default. A
successful bridge response is HTTP 200 with `{"ok": true, "message":
"Uploaded"}`.

Pass a non-default target directory as a query parameter. URL-encode
backslashes in DOS paths as `%5C`:

```sh
curl -sS -i -F "file=@build/PROGRAM.COM" \
  "http://127.0.0.1:9000/uploadFile?destDir=C:%5CCODE%5C"
```

Keep the Makefile variables `HOST` (default `127.0.0.1`), `PORT` (default
`9000`), and optional `DEST_DIR`.

Test `.COM` filenames must be ≤8 chars before `.COM` (DOS 8.3 limit) or
they get mangled/truncated on the target filesystem.

# The ROM/firmware is not the bug

When a smart-cable transport test shows wrong first-byte data (e.g. a
response's status byte reads 0x00 instead of the peer's actual 0x20,
while the rest of the payload is correct), do NOT suspect the Portfolio
ROM, the int 0x61 firmware, or the wire protocol. WIRETEST.COM (pure
asm, no BCC C runtime involved) reads the identical response correctly
byte-for-byte. The ROM and protocol are proven correct — the bug, if
reproduced only from BCC-compiled C, is in the C/BCC layer
(smartcable_port_call's #asm block, calling convention, or how the
caller reads the buffer afterward), never lower. Stop proposing
ROM/firmware causes; go find the actual bug in the C code instead.

# Testing `#asm` blocks

Before shipping a new/changed inline `#asm` block, write an isolated
`.c` test with a table of test cases (valid/invalid/edge), run on real
HW or MAME, require `pass=N fail=0`. Don't trust a read-through — a
digit-accumulation bug (`value += digit*10` instead of `value =
value*10 + digit`) looked fine but silently accepted `256.1.1.1`.

# BCC/8086 size optimization (`-Md` small model)

Always verify by rebuilding and running `stat -c%s`, never estimate
from source. What works:

- `p += sprintf(p, "...") + 1;` instead of `sprintf(p, "..."); p +=
  strlen(p) + 1;` (sprintf already returns the char count). Same trick
  for a bare-literal `strcpy` immediately followed by `strlen` — but
  only when nothing branches between them.
- Merge 2+ immediately-adjacent *static* string entries into one
  `"a\0b\0c\0\0"` blob + one `memcpy`, instead of N `strcpy`+`strlen`
  pairs. Only pays off for 2+ merged calls; collapsing just one is
  worse (memcpy takes 3 args vs. strcpy's 2).
- `-O` peephole flag (`$(BCC) -Md -O ...`): safe, deterministic,
  instruction-merging only, no calling-convention change.
- Hand-asm is worth it for functions taking only a `char *` param (no
  calls to other C functions from inside the asm). Prologue for K&R
  `-Md` (`__FIRST_ARG_IN_AX__` false): `push <regs>; mov bx,sp; mov
  si,N[bx]` where N = 2×(pushed regs) + 2. Char literals need `#$XX`
  hex, not `#'0'`.

What does NOT help (measured, don't repeat without re-measuring):
function-pointer/struct-table abstraction (extra
parameter/indirection costs more than inline duplication saves, small
model passes args on the stack); a shared helper to dedupe 2+ calls
(e.g. wrapping a repeated `sprintf`/`screen_push` pattern) — fixed
call/param overhead usually cancels the saved duplication, netting
~0 B; an offset-lookup array to skip a second parsing pass — the
array's own storage+write cancels the saved loop; `memcpy` as a 1:1
`strcpy` replacement (only helps merging 2+, see above); `-Mc`/`-Mf`
flags — change calling convention, break libc linking and existing
`#asm` stack-offset assumptions, too risky to use.

## Versioning

- `src/version.h`: MAJOR/MINOR/PATCH. `src/build_id.h`:
  `POFOUDSK_BUILD_ID`.
- Bump the build id in every commit, so a build on HW can always be
  identified. High half `FFFF` = local build; CI overwrites
  `build_id.h` with the git short hash.
- Any change of MAJOR/MINOR/PATCH resets the low half of the build id
  to 1 (high half unchanged).

## Forks

- If you fork this repository, prefer contributing back via pull
  requests to the original repository.
- If you keep a separate fork, keep a visible reference to the original
  repository and its author (README, license headers, credits).

## Docs

- Code, comments and docs must stand on their own: never reference other
  projects or things outside this repository ("same as ...", "like in
  ...").

- Code and docs in English.
