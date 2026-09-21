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

`src/pftd/` holds PFTD's source - as POFOSCAB grows to host more than
one tool, each gets its own `src/<name>/` directory alongside it.

- `src/pftd/PFTD.asm` - the TSR itself. Hooks `int 0x61`, detects
  PFTD-space commands (`payload[0] >= 0x80`) alongside the stock ROM
  protocol (`payload[0]` in `[2,6]`), and dispatches to the
  per-command `.inc` files below.
- `src/pftd/hello.inc` - HELLO (`0x80`): presence/capability discovery.
- `src/pftd/list.inc` - LIST extended (`0x86`): directory listing with
  attributes/size/date/time, plus free/total drive space.
- `src/pftd/drives.inc` - DRIVES (`0x87`): logical drive count.
- `src/pftd/mkdir.inc` - MKDIR (`0x88`).
- `src/pftd/delete.inc` - DELETE (`0x89`, files only - see
  `rmdir.inc` for directories).
- `src/pftd/rmdir.inc` - RMDIR (`0x8A`).
- `src/pftd/rename.inc` - RENAME (`0x8B`): rename/move within the same
  drive.
- `src/pftd/copy.inc` - COPY (`0x8C`): copy a file, source to
  destination, works cross-drive (unlike RENAME) since it does a real
  data copy.
- `src/pftd/datetime.inc` - GETDATETIME (`0x8D`)/SETDATETIME (`0x8E`):
  read/set the Portfolio's system date and time together, packed DOS
  format (same as LIST extended's per-file date/time).
- `src/pftd/critical_error.inc` - resident `int 0x24` (DOS critical
  error) handler, needed by any command that does real disk I/O
  (mkdir/delete/rmdir/rename/copy) so a missing/write-protected disk
  doesn't hang on "Abort, Retry, Ignore?". SETDATETIME uses it too,
  defensively, though its DOS calls aren't expected to need it.
- `src/pftd/residentcheck.inc` - "already resident" probe, so the TSR
  refuses to double-install.
- `src/pftd/pofodetect.inc` - real-hardware detection (`is_pofo`),
  used to refuse installing on anything that isn't an actual
  Portfolio.
- `src/pftd/hexprint.inc` - install-time-only hex/decimal print
  helpers (banner output), never called from inside the resident hook.
- `src/pftd/version.inc` - `VERSION` (wire format version),
  hand-maintained, never touched by CI.
- `src/pftd/build_id.inc` - `BUILD_ID`, a dev snapshot marker bumped by
  hand locally; CI regenerates this file wholesale (not a patch) with
  the commit's short git hash on every build - see the file's own
  header.
- `tests/regression_test.py` - a fast smoke test over a live client's
  `/sendRaw` debug endpoint, covering every command's happy-path
  shape. Meant to be run after flashing a new build, before anything
  more targeted - see the script's own header.

## Building

```bash
# Build PFTD.COM to build/PFTD.COM (uses root Makefile)
make pftd

# Or directly:
cd src/pftd
nasm -f bin -i . PFTD.asm -o ../../build/PFTD.COM
```

## Testing

Tests run against the Smart Cable bridge API, supporting multiple backends:

### Prerequisites

- MAME build with smartcable support (local: `/home/petrkr/git/mame`)
- Python 3 with pytest: `pip install pytest` (in a venv, not system-wide)
- For MAME backend: `~/.mame/nvram/pofo/ccma_ram` - FAT12 memory card with PFTD.COM

### Running tests

```bash
# Headless MAME with automatic upload (default)
pytest tests/ -v

# Skip upload step (PFTD already on card)
SKIP_UPLOAD=1 pytest tests/ -v

# Skip build step (binary already built)
SKIP_BUILD=1 pytest tests/ -v

# Manual MAME UI mode (start MAME and bridge in separate terminals first)
POFOSCAB_BACKEND=mame_manual pytest tests/ -v

# Real hardware (Portfolio over ESP32 Smart Cable)
POFOSCAB_BACKEND=hardware POFOSCAB_BRIDGE_URL=http://10.220.179.55 pytest tests/ -v
```

### Backend modes

- **`mame_auto`** (default): Starts MAME and Python bridge automatically
- **`mame_manual`**: Assumes MAME and bridge already running (start manually)
- **`hardware`**: Targets real Portfolio over Smart Cable (ESP32 client)

## CI / Releases

`.github/workflows/build.yml` builds `src/pftd/PFTD.COM` (no automated
tests yet) on every push to `master` and on `v*.*.*` tags. In both
cases it regenerates `build_id.inc` from scratch with the commit's
short git hash before assembling - so any published build's `BUILD_ID`
(visible in the HELLO response, see `PROTOCOL.md`) always identifies
the exact commit, never a hand-bumped dev marker. `VERSION`
(`version.inc`) is untouched by CI. Master builds are uploaded as a
build artifact named `PFTD-<shorthash>` (containing plain `PFTD.COM`);
tag builds additionally publish a GitHub Release with
`PFTD-<tag>.zip` attached - only the release asset is zipped, since
Actions artifacts are already downloaded as a zip by GitHub itself.

## Wire protocol

The full request/response byte layout for every command, the HELLO
capability bitmask, and the status/errcode conventions are documented
in [`PROTOCOL.md`](PROTOCOL.md) - that's the source of truth for the
protocol itself, written as a standalone specification (no
implementation details). `ROM_RESEARCH_NOTES.md` has the underlying
reverse-engineering (ROM disassembly, real-hardware DOS 2.x/DIP DOS
behavior findings) that the protocol design and this driver's
implementation choices are based on.

## Adding a new command

1. Pick the next free code (see `PROTOCOL.md`'s "Reserved command
   codes" section for what's currently free).
2. Decide whether it belongs to an existing capability group or needs
   a new bit for a new command family (see `hello.inc`'s header for
   the reasoning behind grouping bits by family rather than one per
   command) - don't add a new bit for a command that's just another
   file operation.
3. Document the wire format in `PROTOCOL.md` first, in the same style
   as the existing commands - request/response byte layout only, no
   implementation details.
4. Implement `dispatch_<name>` in a new or existing `*.inc` file,
   wire it into `PFTD.asm`'s command detection (`%include` plus the
   `mov al, [cs:payload0]` / `call dispatch_<name>` pair in
   `pftd_int61_handler`).
5. Verify on real hardware before trusting any RBIL-documented DOS
   function contract - DIP DOS diverges from PC MS-DOS behavior in
   ways that are easy to miss otherwise (see `ROM_RESEARCH_NOTES.md`'s
   DIP DOS critical error section).
6. Add checks to `tests/regression_test.py` covering the new command's
   happy path, so future changes get a fast regression check.
