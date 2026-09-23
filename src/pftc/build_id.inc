; build_id.inc - build identity for PFTC (offset 6-9 of the HELLO
; response, see hello.inc).
;
; CI-generated: overwritten wholesale with the commit's short git hash
; before assembling, same convention as src/pftd/build_id.inc - kept as
; PFTC's own file (not shared with PFTD's) since each program's build
; step only touches its own copy, even though both point at the same
; commit on a given build. The value committed here is a local dev
; snapshot marker, never meaningful outside a local build.

BUILD_ID  equ 0xFFFF0000
