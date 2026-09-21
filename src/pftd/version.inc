; version.inc - PFTD's software version (offset 8-10 of the HELLO
; response, see hello.inc): VERSION_MAJOR.MINOR.PATCH, e.g. "v0.0.1".
; A breaking protocol/format change bumps VERSION_MAJOR, same as any
; other breaking change - there is no separate "response format
; version" distinct from the software version. Distinct from BUILD_ID
; (build_id.inc, the exact git commit).
;
; CI-generated on a tag build only: .github/workflows/build.yml
; overwrites this entire file (same wholesale-replace convention as
; build_id.inc, for the same reason - no dependency on this file's
; exact formatting) with MAJOR/MINOR/PATCH parsed from the pushed
; `vMAJOR.MINOR.PATCH` git tag. An ordinary push to master (or a local
; dev build, like this committed copy) leaves this at 0.0.0 - a plain
; "not a tagged release" signal, nothing fancier (no snapshot/pre-
; release flag - that distinction is left to GitHub itself: tag builds
; land in Releases, everything else is just a workflow artifact).
; BUILD_ID (the git hash) always identifies the exact source either
; way; this field exists purely for a human to read something
; meaningful once git history itself is no longer the source of truth
; (e.g. this repo moves/is mirrored elsewhere), which a hash alone
; doesn't provide.

VERSION_MAJOR equ 0
VERSION_MINOR equ 0
VERSION_PATCH equ 0
