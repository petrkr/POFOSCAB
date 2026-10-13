; build_id.inc - build identity for PFTD (offset 4-7 of the HELLO
; response, see hello.inc).
;
; CI-generated: `.github/workflows/build.yml` overwrites this entire
; file with the commit's short git hash before assembling.
;
; The value committed here is a local dev snapshot marker, bumped by
; hand between CI runs.

BUILD_ID  equ 0xFFFF0002
