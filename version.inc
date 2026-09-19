; version.inc - protocol version for PFTD.
; Hand-maintained - not touched by CI/build tooling. BUILD_ID lives in
; the separate build_id.inc, which CI regenerates wholesale (see that
; file's header for why it's split out).

VERSION   equ 1                ; HELLO response protocol version (offset 8)
