# POFOSCAB - Atari Portfolio Smart Cable tools

Tools for talking to an Atari Portfolio over its Smart Cable interface
(`int 0x61`, the same API the Portfolio ROM's own File Transfer Server
mode uses). Name is 8 characters on purpose - it's meant to be clonable
as a DOS 8.3 directory name if you ever need to check this out from
inside DOS itself.

This is the Atari-side (real-mode 8086 assembly, NASM) half of the
[PortfolioESPlink](https://github.com/petrkr/PortfolioESPlink) project
- the wire protocol these tools implement (`PROTOCOL.md`) and the
reverse-engineering research behind it (`ROM_RESEARCH_NOTES.md`, plus
the raw `ROM_A.bin`/`ROM_B.bin` dumps it references) live here, since
this is the protocol's server-side reference implementation. The
ESP32-side client library lives in the PortfolioESPlink repo, not here.

## Why a separate repo, and why this name

What lives here started as one thing: `PFTD` (PortFolio Transfer
Daemon), a TSR that extends the Portfolio ROM's file-transfer protocol
with extra commands (directory listing with attributes, mkdir, delete,
rmdir, rename/move) that the stock ROM doesn't support. `PFTD` is still
the name of that daemon - it hasn't changed.

But the `int 0x61` Smart Cable interface it hooks into isn't inherently
about file transfer - it's a general bidirectional command channel
between the ESP32 and the Portfolio. The direction planned next is the
reverse of what's here today: the ESP32 driving/configuring the
Portfolio (e.g. pushing WiFi settings), not just the Portfolio pulling
files through PFTD. That's a broader scope than "file transfer daemon,"
so the repo itself is named for the interface it's built on (Portfolio
+ Smart Cable), with PFTD as the first resident tool inside it, not the
whole story.

## Files

- `PFTD.asm` - the TSR itself. Hooks `int 0x61`, detects PFTD-space
  commands (`payload[0] >= 0x80`) alongside the stock ROM protocol
  (`payload[0]` in `[2,6]`), and dispatches to the per-command `.inc`
  files below.
- `hello.inc` - HELLO (`0x80`): presence/capability discovery.
- `list.inc` - LIST extended (`0x86`): directory listing with
  attributes/size/date/time, plus free/total drive space.
- `drives.inc` - DRIVES (`0x87`): logical drive count.
- `mkdir.inc` - MKDIR (`0x88`).
- `delete.inc` - DELETE (`0x89`, files only - see `rmdir.inc` for
  directories).
- `rmdir.inc` - RMDIR (`0x8A`).
- `rename.inc` - RENAME (`0x8B`): rename/move within the same drive.
- `critical_error.inc` - resident `int 0x24` (DOS critical error)
  handler, needed by any command that does real disk I/O (mkdir/
  delete/rmdir/rename) so a missing/write-protected disk doesn't hang
  on "Abort, Retry, Ignore?".
- `residentcheck.inc` - "already resident" probe, so the TSR refuses to
  double-install.
- `pofodetect.inc` - real-hardware detection (`is_pofo`), used to
  refuse installing on anything that isn't an actual Portfolio.
- `hexprint.inc` - install-time-only hex/decimal print helpers (banner
  output), never called from inside the resident hook.
- `version.inc` - `VERSION` (wire format version), hand-maintained,
  never touched by CI.
- `build_id.inc` - `BUILD_ID`, a dev snapshot marker bumped by hand
  locally; CI regenerates this file wholesale (not a patch) with the
  commit's short git hash on every build - see the file's own header.
- `loadtest.bat` - DOSBox loader: installs `tests/STUB61.COM` then
  `PFTDN.COM` (see below), so the individual `tests/T*.COM` tools can
  be run against a live instance.
- `tests/` - standalone DOSBox-only test tools, one per command
  (`TDRIVES`, `TLISTEXT`, `TMKDIR`, `TDELETE`, plus a couple of
  isolated probes - `TMKDIRA`, `TUNLKDIR` - that call `int 0x21`
  directly, without PFTD, to characterize real-hardware DOS behavior
  in isolation). None of these `%include` anything from the driver;
  each is fully self-contained. `STUB61.asm` is a minimal stand-in
  `int 0x61` handler PFTD's chain can safely jump to under DOSBox
  (which has no real ROM handler there).

## Building

```
nasm -f bin PFTD.asm -o PFTD.COM                    # real Portfolio hardware
nasm -f bin -dCHECK_POFO=0 PFTD.asm -o PFTDN.COM     # DOSBox / testing
```

`CHECK_POFO=0` skips the hardware detection in `pofodetect.inc` -
DOSBox's port `0x61` doesn't echo back like a real Portfolio's does, so
the check would always fail there. Never ship a `CHECK_POFO=0` build to
real hardware.

## CI / Releases

`.github/workflows/build.yml` builds `PFTD.COM` (real-hardware variant
only, no `PFTDN.COM`/DOSBox variant yet, no automated tests yet) on
every push to `master` and on `v*.*.*` tags. In both cases it
regenerates `build_id.inc` from scratch with the commit's short git
hash before assembling - so any published build's `BUILD_ID` (visible
in the HELLO response, see `PROTOCOL.md`) always identifies the exact
commit, never a hand-bumped dev marker. `VERSION` (`version.inc`) is
untouched by CI. Master builds are uploaded as a build artifact named
`PFTD-<shorthash>` (containing plain `PFTD.COM`); tag builds
additionally publish a GitHub Release with `PFTD-<tag>.zip` attached -
only the release asset is zipped, since Actions artifacts are already
downloaded as a zip by GitHub itself.

Each `tests/T*.asm` assembles the same way, e.g.:

```
nasm -f bin tests/TDRIVES.asm -o tests/TDRIVES.COM
```

## DOSBox test workflow

```
STUB61          <- tests/STUB61.COM, installs a transmit-logging int 0x61 stub
PFTDN           <- the -dCHECK_POFO=0 build
TDRIVES         <- or any other tests/T*.COM, exercises one command's dispatch
```

`loadtest.bat` runs the first two steps. This proves dispatch plumbing
works (no hang/crash, response bytes visible via STUB61's log) - it
does **not** validate real DIP DOS behavior (critical errors, exact DOS
error codes, media-access quirks). Everything here has repeatedly
turned out to diverge from what DOSBox alone would suggest; real
Portfolio hardware testing is required before trusting any of it,
particularly anything touching `int 0x21` disk I/O (`AH=0x39/0x3A/0x41/
0x56`) or RBIL-documented DOS function contracts.

## Wire protocol

The full request/response byte layout for every command, the HELLO
capability bitmask, and the status/errcode conventions are documented
in [`PROTOCOL.md`](PROTOCOL.md) - that's the source of truth for the
protocol itself. `ROM_RESEARCH_NOTES.md` has the underlying
reverse-engineering (ROM disassembly, real-hardware DOS 2.x/DIP DOS
behavior findings) that the protocol design and this driver's
implementation choices are based on.
