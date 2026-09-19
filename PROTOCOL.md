# PFTD protocol reference

PFTD is a command protocol for the Atari Portfolio's Smart Cable
interface (`int 0x61`). It extends the Portfolio ROM's built-in file
transfer protocol with additional commands - directory listing with
attributes, directory/file management, copying, and clock access.

This document specifies the wire format. The reference implementation
in this repository is the source of truth if anything here is unclear
or ambiguous.

## Transport

Every command is sent as a block over `int 0x61 AH=0x30` (the
Portfolio's Smart Cable send/receive primitive):

- **Send** (client -> Portfolio): `AH=0x30 AL=1`, a length-prefixed
  block containing the request bytes described below.
- **Receive** (Portfolio -> client): `AH=0x30 AL=0`, a length-prefixed
  block containing the response bytes.

The first byte of every request, `payload[0]`, selects the command:

| Range | Meaning |
|---|---|
| `0x00`-`0x06` | ROM-native commands, built into the Portfolio's File Transfer Server. |
| `0x80`-`0xFF` | PFTD commands, described in this document. |

## ROM commands (`0x00`-`0x06`)

These are handled directly by the Portfolio ROM and cannot be
extended. Included here for completeness, since PFTD commands share
the same transport and some response conventions.

| Code | Purpose |
|---|---|
| `0x00` | Cancel transfer (sent when an overwrite prompt is declined). |
| `0x02` | Receive file (Portfolio -> client). |
| `0x03` | Transmit init (client -> Portfolio). |
| `0x05` | Transmit overwrite confirm. |
| `0x06` | List directory (names only). |

Response control byte: `0x10` = error (bad path, disk full, not
found), `0x20` = ok (or "file exists" during upload, or "transfer
done").

## PFTD commands (`0x80`+)

### HELLO - `0x80`

Presence and capability discovery. Always the first command a client
should send after connecting.

**Request:** 1 byte - `0x80`.

**Response:** 12 bytes, fixed length.

| Offset | Size | Field | Description |
|---|---|---|---|
| 0 | 4 | Magic | ASCII `"PFD1"`. |
| 4 | 4 | Build ID | Binary, little-endian. Identifies the exact build of the server. |
| 8 | 1 | Version | `1` = this format. `0xFF` = extended format; see below. |
| 9 | 1 | Capabilities | Bitmask, see [Capabilities](#capabilities-bitmask). |
| 10 | 2 | Reserved | Always `0x00 0x00`. |

If offset 8 is `0xFF`, the response format from offset 9 onward is
redefined by whatever extended version number follows at offset 9 -
none is defined at the time of writing.

### LIST - `0x86`

Directory listing with per-entry attributes, size, and timestamp, plus
free/total drive space. A superset of ROM command `0x06` (names only).

**Request:** `0x86, 0x00, 0x70` followed by an ASCIIZ search pattern
(e.g. `"C:\*.*"`).

**Response:** variable length.

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | Entry count, little-endian. |
| 2 | *(count entries)* | Entries - see below. |
| ... | 4 | Free bytes on the pattern's drive, little-endian. |
| ... | 4 | Total bytes on the pattern's drive, little-endian. |

Each entry:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | Attribute byte (standard DOS: bit 0 read-only, bit 1 hidden, bit 2 system, bit 4 directory, bit 5 archive). |
| 1 | 4 | File size, little-endian. `0` for directories. |
| 5 | 2 | Date, packed DOS format - see [Packed date/time](#packed-datetime-format). |
| 7 | 2 | Time, packed DOS format. |
| 9 | *(variable)* | Name, ASCIIZ (8.3 format, up to 12 bytes including terminator). |

The free/total drive space fields follow immediately after the last
entry - they are not counted in the entry count and are always
present.

### DRIVES - `0x87`

Number of logical drives the Portfolio knows about.

**Request:** 1 byte - `0x87`.

**Response:** 1 byte - drive count `N`. Drives map to letters `A`
through the `N`th letter of the alphabet, contiguous with no gaps.

### MKDIR - `0x88`

Create a directory.

**Request:** `0x88, 0x00, 0x70` followed by an ASCIIZ target path.

**Response:** see [Status/errcode response](#statuserrcode-response).

### DELETE - `0x89`

Delete a file. Files only - use RMDIR (`0x8A`) for directories.

**Request:** `0x89, 0x00, 0x70` followed by an ASCIIZ target path.

**Response:** see [Status/errcode response](#statuserrcode-response).

### RMDIR - `0x8A`

Remove an empty directory.

**Request:** `0x8A, 0x00, 0x70` followed by an ASCIIZ target path.

**Response:** see [Status/errcode response](#statuserrcode-response).
`errcode 4` covers both "access denied" and "directory not empty" -
the underlying DOS call does not distinguish these cases.

### RENAME - `0x8B`

Rename or move a file or directory within the same drive. Renaming
across drives is not supported - use COPY (`0x8C`) followed by DELETE
or RMDIR instead.

**Request:** `0x8B, 0x00, 0x70` followed by two consecutive ASCIIZ
strings: the current path, then the new path (the new path begins
immediately after the current path's NUL terminator).

**Response:** see [Status/errcode response](#statuserrcode-response).
`errcode 4` covers "access denied", "destination already exists", and
"cross-drive rename attempted".

### COPY - `0x8C`

Copy a file from a source path to a destination path. Files only, no
directory recursion. Unlike RENAME, this works across drives.

The destination is always overwritten if it already exists. Its DOS
attribute byte is set to match the source's. If the copy fails
partway through, the partially-written destination is removed - a
failed COPY never leaves a corrupt file behind.

**Request:** `0x8C, 0x00, 0x70` followed by two consecutive ASCIIZ
strings: the source path, then the destination path (immediately
following the source path's NUL terminator).

**Response:** see [Status/errcode response](#statuserrcode-response).

This command has no progress reporting - the response is sent only
once the entire copy has completed or failed.

### GETDATETIME - `0x8D`

Read the Portfolio's current system date and time.

**Request:** 1 byte - `0x8D`.

**Response:** 4 bytes, fixed length.

| Offset | Size | Field |
|---|---|---|
| 0 | 2 | Date, packed DOS format - see [Packed date/time](#packed-datetime-format). |
| 2 | 2 | Time, packed DOS format. |

This command cannot fail and has no status/errcode byte.

### SETDATETIME - `0x8E`

Set the Portfolio's system date and time.

**Request:** `0x8E, 0x00, 0x70` followed by 4 bytes: packed date (2
bytes), then packed time (2 bytes) - same format as GETDATETIME's
response, sent as fixed-size binary rather than ASCIIZ.

**Response:** see [Status/errcode response](#statuserrcode-response).
`errcode 4` means the date or time value was out of range.

## Status/errcode response

MKDIR, DELETE, RMDIR, RENAME, COPY, and SETDATETIME share this
response shape - it is always exactly 2 bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | 1 | Status: `0x20` = ok, `0x10` = error. |
| 1 | 1 | Error code (`0` on success; see table below on failure). |

| Code | Meaning |
|---|---|
| `0` | Success. |
| `1` | Path or file not found. |
| `2` | Already exists. Reserved - not currently produced by any command. |
| `3` | Disk full. |
| `4` | Access denied - covers write-protected media, read-only files, "already exists", "not empty", "destination exists", "cross-drive operation attempted", and out-of-range date/time values, depending on command. |
| `0xFF` | An unrecoverable error occurred; the specific cause could not be determined. |

## Capabilities bitmask

The capabilities byte in the HELLO response (offset 9) is grouped by
command *family*, not one bit per individual command - most bits are
reserved for families that don't exist yet.

| Bit | Mask | Meaning |
|---|---|---|
| 0 | `0x01` | Core file operations: LIST, DRIVES, MKDIR, DELETE, RMDIR, RENAME, COPY (`0x86`-`0x8C`). |
| 1 | `0x02` | Clock access: GETDATETIME, SETDATETIME (`0x8D`-`0x8E`). |
| 2-7 | - | Reserved for future command families. |

## Packed date/time format

Dates and times are represented in the same packed 16-bit format DOS
uses in directory entries:

**Date** (2 bytes):

| Bits | Field |
|---|---|
| 15-9 | Year, offset from 1980 (range 1980-2107). |
| 8-5 | Month (1-12). |
| 4-0 | Day (1-31). |

**Time** (2 bytes):

| Bits | Field |
|---|---|
| 15-11 | Hour (0-23). |
| 10-5 | Minute (0-59). |
| 4-0 | Second, divided by 2 (range 0-29; odd seconds are not representable). |

## Reserved command codes

`0x81`-`0x85` and `0x8F`-`0xFF` are unassigned.
