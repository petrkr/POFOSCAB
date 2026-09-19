# PFTD protocol reference

Command byte layout is source-of-truth in code (`*.inc` in this repo)
- this is a summary to keep in sync with it, not a replacement for
reading the code.

This repo implements the Atari-side (server) half of the protocol
described here - `PFTD.asm` + its `.inc` modules. The client side
consuming this protocol lives elsewhere and is out of scope for this
repo.

## Overview

`payload[0]` selects the command:

- `[2, 6]` - built into the Portfolio ROM's "File transfer Server" mode,
  fixed dispatch, cannot be extended (see `rom/ROM_RESEARCH_NOTES.md`
  for the reverse-engineering that established this).
- `0x80+` - PFTD-only command space, handled entirely by the PFTD TSR
  (this repo), no ROM involvement.

Transport for both ranges is the same `sendBlock`/`receiveBlock`
handshake over `int 0x61 AH=0x30`.

## ROM commands (2-6)

| Code | Purpose |
|---|---|
| `0x02` | Receive file (Portfolio -> client) |
| `0x03` | Transmit init (client -> Portfolio) |
| `0x05` | Transmit overwrite confirm |
| `0x06` | List (legacy, names only) |
| `0x00` | Cancel transfer, sent when overwrite is declined |

Response control byte conventions: `0x10` = error (bad path/disk
full/not found), `0x20` = ok / file exists (upload) / transfer done.

## PFTD commands (0x80+)

### HELLO (`0x80`)

Presence/capability discovery. Request: single byte `0x80`. Response
(fixed 12 bytes):

| Offset | Size | Content |
|---|---|---|
| 0-3 | 4B | magic `"PFD1"` |
| 4-7 | 4B | build id (binary, LE) |
| 8 | 1B | version (`1` = this format; `0xFF` = extended, offset 9 carries an extended version number and everything after is redefined) |
| 9 | 1B | capabilities bitmask |
| 10-11 | 2B | reserved (`0x00, 0x00`) |

- Atari: `hello.inc` (`HELLO_CMD`, `hello_response`, `dispatch_hello`).

### LIST extended (`0x86`)

Same request prefix as ROM's `list` (`0x86, 0x00, 0x70` + ASCIIZ
pattern), response adds attributes/size/date/time per entry plus
free/total drive space at the end.

Response: `count(2B LE)` + N x `[attr(1B) + size(4B LE) + date(2B
packed DOS) + time(2B packed DOS) + name(ASCIIZ)]` + `free(4B LE) +
total(4B LE)`. Free/total is per-drive (from the pattern's drive
letter, or the current default drive), always present regardless of
capability bit.

- Atari: `list.inc` (`LIST_CMD`, `dispatch_list`).

### DRIVES (`0x87`)

How many logical DOS drives exist (`A=1, B=2, ...`). Request: single
byte `0x87`. Response: single byte, drive count.

- Atari: `drives.inc` (`DRIVES_CMD`, `dispatch_drives`).

### MKDIR (`0x88`)

Create a directory on the Portfolio (`int 0x21 AH=0x39`). Request:
`0x88, 0x00, 0x70` + ASCIIZ target path. Response (fixed 2 bytes): see
"Response status/errcode convention" below.

- Atari: `mkdir.inc` (`MKDIR_CMD`, `dispatch_mkdir`).

### DELETE (`0x89`)

Delete a file on the Portfolio (`int 0x21 AH=0x41`, Unlink - files only;
directory removal is RMDIR, `0x8A`, below). Request: `0x89, 0x00, 0x70` +
ASCIIZ target path.
Response (fixed 2 bytes): see "Response status/errcode convention" below.

- Atari: `delete.inc` (`DELETE_CMD`, `dispatch_delete`).

### RMDIR (`0x8A`)

Remove an empty directory on the Portfolio (`int 0x21 AH=0x3A`). Request:
`0x8A, 0x00, 0x70` + ASCIIZ target path. Response (fixed 2 bytes): see
"Response status/errcode convention" below. "Not empty" is not
distinguished from "access denied" (errcode `4`) - DOS 2.x has no separate
code for this, same coarse-granularity situation as MKDIR's "already
exists" (confirmed on real hardware, see `ROM_RESEARCH_NOTES.md`).

- Atari: `rmdir.inc` (`RMDIR_CMD`, `dispatch_rmdir`).

### RENAME (`0x8B`)

Rename or move a file/directory on the Portfolio (`int 0x21 AH=0x56`).
Works as a move within the same drive (DOS rename is a directory-entry
rewrite, not a data copy) but NOT across drives - confirmed on real
hardware (`errcode=4`, both for a file and a directory; see
`STATUS.md`). For a cross-drive move, use COPY (`0x8C`) followed by
DELETE/RMDIR.

Request: `0x8B, 0x00, 0x70` + TWO consecutive ASCIIZ strings (old path,
then new path, new path immediately following old path's NUL). Response
(fixed 2 bytes): see "Response status/errcode convention" below.

- Atari: `rename.inc` (`RENAME_CMD`, `dispatch_rename`).

### COPY (`0x8C`)

Copy a file on the Portfolio, source to destination (`int 0x21
AH=0x3D/0x3C/0x3F/0x40/0x3E` - Open/Create/Read/Write/Close). Files
only - no directory recursion. Unlike RENAME, works cross-drive since
it performs a real read/write data copy, not a directory-entry
rewrite. The destination is always overwritten if it exists (no "fail
if exists" mode - matches DOS's own `COPY` command); its DOS attribute
byte is set to match the source's. If the copy fails partway (disk
full, media pulled, critical error), the partially-written destination
file is deleted before responding.

Request: `0x8C, 0x00, 0x70` + TWO consecutive ASCIIZ strings (source
path, then destination path, destination immediately following
source's NUL) - identical shape to RENAME's request. Response (fixed 2
bytes): see "Response status/errcode convention" below.

No progress reporting - `dispatch_copy` runs the entire copy loop
synchronously and sends exactly one response at the end, same as every
other PFTD command (see `copy.inc`'s header for why intermediate
progress packets are out of scope for now).

- Atari: `copy.inc` (`COPY_CMD`, `dispatch_copy`).

### Response status/errcode convention (MKDIR/DELETE/RMDIR/RENAME/COPY)

MKDIR, DELETE, RMDIR, RENAME and COPY are the PFTD commands that can
genuinely fail (bad path, already exists, disk full, no media,
write-protected), and perform real disk I/O from inside the `int 0x61`
dispatch hook - which requires a resident `int 0x24` (DOS critical error) handler
(`critical_error.inc`) to avoid blocking on "Abort, Retry,
Ignore?" on a drive with no media (see `ROM_RESEARCH_NOTES.md`'s DIP DOS
critical-error findings from DRIVES development).

Response is always exactly 2 bytes:

| Offset | Size | Content |
|---|---|---|
| 0 | 1B | status: `0x20` = ok, `0x10` = error (reusing the ROM commands' convention) |
| 1 | 1B | errcode: `0` on success; on failure, one of the table below |

| errcode | Meaning | Used by |
|---|---|---|
| `1` | file/path not found | MKDIR, DELETE, RMDIR, RENAME, COPY |
| `2` | already exists (reserved - see note below, not currently reachable) | MKDIR only |
| `3` | disk full | MKDIR, COPY (also covers a short write - see `copy.inc`) |
| `4` | access denied (write-protected, read-only, or DOS 2.x's coarse catch-all - also covers "already exists" (MKDIR), "not empty" (RMDIR), "destination exists"/"cross-drive" (RENAME, confirmed on real hardware), too-many-open-files (COPY)) | MKDIR, DELETE, RMDIR, RENAME, COPY |
| `0xFF` | critical error fired (`int 0x24` Ignore path taken) - `AX` not trustworthy, cause unknown | MKDIR, DELETE, RMDIR, RENAME, COPY |

**Confirmed on real hardware** (see `ROM_RESEARCH_NOTES.md`'s MKDIR/DELETE
test results): DOS 2.x/DIP DOS's `AH=0x39` returns the same code (5, access
denied) for "directory already exists" as for other access-denied cases -
no distinct code exists on this DOS version. Errcode `2` is therefore not
currently produced by `mkdir.inc`; it remains reserved in this enum in case
a future DOS version or code path needs it, not because it's expected soon.
`AH=0x3A` (RMDIR)'s "not empty" case is assumed to behave the same way
(access denied, errcode `4`) by analogy, not separately confirmed.
RENAME's cross-drive behavior (`errcode=4`) is confirmed on real
hardware; other RENAME scenarios remain unverified - see `STATUS.md`.

COPY's `AH=0x3D/0x3C/0x3F/0x40` error mapping in `copy.inc` is
entirely unverified on real hardware - this repo has no prior usage of
these DOS functions to compare against. See `STATUS.md` and
`copy.inc`'s header for what's confirmed vs. still open.

## Capabilities bitmask (HELLO response, offset 9)

| Bit | Constant | Meaning |
|---|---|---|
| 0 (`0x01`) | `CAP_LIST_EXT` | LIST extended (`0x86`) supported |
| 1 (`0x02`) | `CAP_DRIVES` | DRIVES (`0x87`) supported |
| 2 (`0x04`) | `CAP_MKDIR` | MKDIR (`0x88`) supported |
| 3 (`0x08`) | `CAP_DELETE` | DELETE (`0x89`) supported |
| 4 (`0x10`) | `CAP_RMDIR` | RMDIR (`0x8A`) supported |
| 5 (`0x20`) | `CAP_RENAME` | RENAME (`0x8B`) supported |
| 6 (`0x40`) | `CAP_COPY` | COPY (`0x8C`) supported |

Defined in `hello.inc`; currently all seven bits are always set
(`CAP_LIST_EXT | CAP_DRIVES | CAP_MKDIR | CAP_DELETE | CAP_RMDIR | CAP_RENAME | CAP_COPY`).

## Version / BUILD_ID

`version.inc`:

```asm
VERSION   equ 1
```

`build_id.inc`:

```asm
BUILD_ID  equ 0xFFFF0010   ; dev snapshot marker
```

`BUILD_ID` lives in its own file (`build_id.inc`) because CI
regenerates it wholesale on every build (see that file's header and
`.github/workflows/build.yml`) with the commit's short git hash - the
value committed in the repo is only a local dev snapshot placeholder,
never a real release marker.

## Adding a new command

1. Pick the next free code (`0x8D+` - `0x81`-`0x85` are reserved,
   unused so far; `0x88`/`0x89`/`0x8A`/`0x8B`/`0x8C` are taken by
   MKDIR/DELETE/RMDIR/RENAME/COPY).
2. Give it its own capability bit in the HELLO response (offset 9),
   same pattern as `CAP_LIST_EXT`/`CAP_DRIVES`.
3. Implement `dispatch_<name>` in a new or existing `*.inc` file here,
   wire it into `PFTD.asm`'s command detection.
4. Add a standalone DOSBox test tool in `tests/` (see
   `TLISTEXT.COM`/`TDRIVES.COM`) and verify on real hardware before
   trusting any RBIL-documented DOS function contract - DIP DOS
   diverges from PC MS-DOS behavior in ways DOSBox won't reveal (see
   `ROM_RESEARCH_NOTES.md`'s DIP DOS critical error section).

## Reserved / not yet implemented

- `0x81`-`0x85`, `0x8D+`: reserved, unused.
- Planned ideas (SETTIME via `AH=0x2D`/`AH=0x2B`): rationale and
  DOS-version caveats are in `ROM_RESEARCH_NOTES.md`.
