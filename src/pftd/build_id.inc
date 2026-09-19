; build_id.inc - build identity for PFTD (offset 4-7 of the HELLO
; response, see hello.inc).
;
; CI-generated: `.github/workflows/build.yml` overwrites this entire
; file with the commit's short git hash before assembling, so a
; published build's BUILD_ID always identifies the exact commit - it
; does not patch/sed the existing line, it replaces the whole file
; wholesale (avoids any dependency on this file's exact formatting).
; The value committed here is a local dev snapshot marker, bumped by
; hand between CI runs - never meaningful outside a local build.

BUILD_ID  equ 0xFFFF0018
