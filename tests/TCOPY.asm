; TCOPY.asm - DOSBox-only test helper, NEVER for real Portfolio hardware.
; Exercises PFTD's COPY (0x8C, see copy.inc) detection and dispatch path
; the same way TMKDIR/TDELETE exercise their own commands.
;
; What this can and can't verify:
;   - CAN verify: PFTD's "watch the next int 0x61 call" detection trick
;     correctly recognizes payload[0]=0x8C, copies both ASCIIZ paths out
;     of the receive buffer, runs the open/create/read/write/close
;     sequence against the real DOSBox filesystem, and calls dispatch_copy
;     without crashing/hanging. With STUB61 loaded below PFTD, the 2-byte
;     status+errcode response is logged as hex and can be eyeballed.
;   - CANNOT verify: DOS 2.x/DIP DOS's actual extended error code behavior
;     on AH=0x3D/0x3C/0x3F/0x40, the disk-full short-write detection, or
;     the int 0x24 critical-error path at all - DOSBox's own DOS does not
;     diverge from RBIL/raise critical errors on media conditions the way
;     DIP DOS does - same caveat as TMKDIR/TDELETE, but more pronounced
;     here since this repo has zero prior usage of these five DOS
;     functions to compare against (unlike AH=0x39/0x41/0x3A/0x56, each
;     individually confirmed for MKDIR/DELETE/RMDIR/parts of RENAME).
;
; This test targets TESTCP.SRC as the source. Create that file in the
; DOSBox mount before running for a status=0x20 (success) response
; copying to TESTCP.DST, or run without it present to instead exercise
; the "file not found" (status=0x10, errcode=1) path; either way the
; test only checks "completed without hanging", not the specific
; response bytes.
;
; Usage in DOSBox:
;   STUB61                          <- installs transmit-logging int 0x61 stub
;   PFTDN                           <- must be built with -dCHECK_POFO=0
;   TCOPY                           <- runs this test, copies TESTCP.SRC
;
; Assemble: nasm -f bin TCOPY.asm -o TCOPY.COM

CPU 8086
ORG 0x100

start:
        ; Bail out early with a clear message if int 0x61 is NULL - means
        ; STUB61 (or PFTD) isn't loaded, and the test below would hang.
        mov     ax, 0x3561
        int     0x21
        mov     ax, es
        or      ax, bx
        jnz     .vector_ok

        mov     dx, msg_no_vector
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4c01
        int     0x21

.vector_ok:
        ; Step 1: simulate the ROM's receive-block call (AH=0x30 AL=1)
        ; with payload_buf holding payload[0]=0x8C followed by the two
        ; consecutive ASCIIZ paths at offset 3 (same request layout COPY
        ; uses: [cmd][0][0x70] + ASCIIZ source + ASCIIZ dest). This makes
        ; PFTD remember DS:DX and set pending=1, then fall through to
        ; .chain (STUB61's IRET) - safe because STUB61 is loaded.
        mov     dx, payload_buf
        mov     ax, 0x3001
        int     0x61

        ; Step 2: any further int 0x61 call makes PFTD check pending,
        ; read payload_buf[0]=0x8C, copy both paths, and call
        ; dispatch_copy. AX=0x3000 here (transmit, harmless/no-op via
        ; STUB61) rather than something PFTD might special-case.
        mov     dx, dummy_buf
        mov     cx, 1
        mov     ax, 0x3000
        int     0x61

        ; If we get here, PFTD's COPY detection/dispatch path ran to
        ; completion without hanging or crashing - that's the thing under
        ; test. The actual response bytes (status+errcode) were logged by
        ; STUB61 above, if loaded.
        mov     dx, msg_done
        mov     ah, 0x09
        int     0x21

        mov     ax, 0x4c00
        int     0x21

msg_no_vector db 'int 0x61 vector is NULL - load STUB61 and PFTDN first.', 13, 10, '$'
msg_done      db 'COPY dispatch completed without hanging - check STUB61 TX= log above.', 13, 10, '$'

payload_buf db 0x8C, 0x00, 0x70, 'TESTCP.SRC', 0, 'TESTCP.DST', 0
dummy_buf   db 0
