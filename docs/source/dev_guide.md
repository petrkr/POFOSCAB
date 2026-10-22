# Dev Guide

## Adding a new command

1. Pick any free command code in `0x80`-`0xFF` not yet used in
   `protocol.md`. Stay in this range: `0x00`-`0x06` is the ROM's own
   dispatch table, and `0x07`-`0x7F` is unverified safe - only `0x80`+
   is confirmed collision-free with the ROM.
2. Decide whether it belongs to an existing capability group or needs
   a new bit for a new command family (see `hello.inc`'s header) -
   don't add a new bit for just another file operation.
3. Document the wire format in `protocol.md` first - request/response
   byte layout only, no implementation details.
4. Implement `dispatch_<name>` in a new or existing `*.inc` file, wire
   it into `PFTD.asm`'s command detection (`%include` plus the
   `mov al, [cs:payload0]` / `call dispatch_<name>` pair in
   `pftd_int61_handler`).
5. Verify on real hardware before trusting any RBIL-documented DOS
   function contract - DIP DOS diverges from PC MS-DOS behavior.
6. Add a pytest case in `tests/test_pftd.py` covering the happy path.

## PFTC build notes (size)

PFTC.COM is built by bcc (dev86) for the 8086 and is kept small on
purpose - check `make pftc` output size after every change:

- `src/pofo/tinylib.c` is the whole C library PFTC links: `str_len`,
  `str_copy`, `mem_copy`, `con_write`/`con_puts` and the tiny
  `fmt()` formatter (`%s %u %d %x %i`). Never call libdos's `printf`,
  `sprintf`, `strlen`, `memcpy`, `malloc`... - they pull in kilobytes of
  stdio/heap code, and the Makefile compiles PFTC with `-C-c -C-f`
  (first argument in AX), a calling convention libdos.a does not
  follow, so a call into it with arguments would silently break.
- Hand-written `#asm` functions that take arguments must handle both
  conventions, see the `__FIRST_ARG_IN_AX__` blocks in `tinylib.c`,
  `valid.c` and `pofo_screen.c`.
- `src/pftc/start.c` is the .COM startup (defines `_start`, which is
  also what makes ld86 keep it - it links by reachability from that
  symbol). It must stay first in `PFTC_SRC`.
- No malloc: menus share `menu_text`, every exchange shares
  `response[]`, screens are saved into the fixed frames of
  `screen_push()`/`screen_pop()`.
