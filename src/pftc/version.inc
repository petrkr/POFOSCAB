; version.inc - PFTC's software version (offset 10-12 of the HELLO
; response, see hello.inc): VERSION_MAJOR.MINOR.PATCH, e.g. "v0.0.1".
; Shared numbering with PFTD (src/pftd/version.inc) - both tools are
; tagged and released together from one repo-wide vMAJOR.MINOR.PATCH
; git tag, so a version bump doesn't by itself say whether PFTD, PFTC,
; or both changed, or whether the bump is breaking for either wire
; format - see the changelog for that. Distinct from BUILD_ID
; (build_id.inc, the exact git commit).
;
; CI-generated on a tag build only, same wholesale-replace convention
; as PFTD's version.inc. An ordinary push to master (or a local dev
; build, like this committed copy) leaves this at 0.0.0.

VERSION_MAJOR equ 0
VERSION_MINOR equ 0
VERSION_PATCH equ 0
