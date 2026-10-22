# Changelog

## [Unreleased] (0.1.2)

- Added Changelog
- DATETIME: simplified packed date/time unpacking.
- Added documentation
- Added HTML report in tests
- PFTC: binary cut from 14216 to 7408 bytes with no feature change:
  own minimal libc (`tinylib`) instead of stdio/printf/malloc, own
  .COM startup, bcc `-C-c -C-f` calling convention, static buffers
  and shared menu/request/screen-stack helpers.

## [0.1.1]

- DRAW_ASCII (`0x8F`) now returns a response instead of being
  fire-and-forget.
- Fixed PFTD intercepting byte 0 of non-ROM (foreground client)
  receives, then a follow-up fix for ROM receive buffers still being
  eaten in some cases, reworked to chain to the ROM handler via
  `call far`.

## [0.1.0]

- Initial PFTD command set: HELLO, LIST extended, DRIVES, MKDIR,
  DELETE, RMDIR, RENAME, COPY, GETDATETIME/SETDATETIME, DRAW_ASCII.
- GUI: border/progress bar on the Portfolio LCD during transfers.
- Protocol documented (`PROTOCOL.md`).
- CI generates `version.inc`/`build_id.inc` per build.
- MAME Smart Cable bridge and pytest integration tests.
