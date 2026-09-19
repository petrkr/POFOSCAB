; TDATETIME.asm - DOSBox-only test helper, NEVER for real Portfolio
; hardware. Exercises PFTD's GETDATETIME (0x8D)/SETDATETIME (0x8E, see
; datetime.inc) detection and dispatch path the same way TDRIVES/TCOPY
; exercise their own commands.
;
; What this can and can't verify:
;   - CAN verify: PFTD's "watch the next int 0x61 call" detection trick
;     correctly recognizes payload[0]=0x8D/0x8E and calls
;     dispatch_getdatetime/dispatch_setdatetime without crashing/
;     hanging, and (via STUB61's transmit log) the actual response
;     bytes (packed date/time for GETDATETIME, status+errcode for
;     SETDATETIME).
;   - CANNOT verify: DOS 2.x/DIP DOS's actual behavior for
;     AH=0x2A/0x2B/0x2C/0x2D, or whether either call can raise a
;     critical error at all - this repo has zero prior usage of any of
;     these four DOS functions to compare against, more unverified
;     than any other command so far (even COPY's AH=0x3D/0x3C/0x3F/
;     0x40 at least had no prior real-hardware assumption to break).
;     This only proves dispatch plumbing runs without hanging, same
;     scope as every other test tool here.
;
; SETDATETIME's payload here is a fixed-size 4-byte binary value (not
; ASCIIZ, unlike the path-based commands) - packed date 0x2345 /
; packed time 0x6400 decode to a plausible but arbitrary date/time,
; picked only to exercise the dispatch path, not to set anything
; meaningful.
;
; Usage in DOSBox:
;   STUB61                          <- installs transmit-logging int 0x61 stub
;   PFTDN                           <- must be built with -dCHECK_POFO=0
;   TDATETIME                       <- runs this test
;
; Assemble: nasm -f bin TDATETIME.asm -o TDATETIME.COM

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
        ; GETDATETIME: simulate the ROM's receive-block call (AH=0x30
        ; AL=1) with payload_get holding payload[0]=0x8D, then any
        ; further int 0x61 makes PFTD dispatch it. Same two-step
        ; pattern every other test tool here uses.
        mov     dx, payload_get
        mov     ax, 0x3001
        int     0x61
        mov     dx, dummy_buf
        mov     cx, 1
        mov     ax, 0x3000
        int     0x61

        mov     dx, msg_get_done
        mov     ah, 0x09
        int     0x21

        ; SETDATETIME: same two-step pattern, payload_set carries
        ; payload[0]=0x8E followed by the 4-byte packed date+time.
        mov     dx, payload_set
        mov     ax, 0x3001
        int     0x61
        mov     dx, dummy_buf
        mov     cx, 1
        mov     ax, 0x3000
        int     0x61

        ; If we get here, both dispatch paths ran to completion without
        ; hanging or crashing - that's the thing under test. The actual
        ; response bytes were logged by STUB61 above, if loaded.
        mov     dx, msg_set_done
        mov     ah, 0x09
        int     0x21

        mov     ax, 0x4c00
        int     0x21

msg_no_vector db 'int 0x61 vector is NULL - load STUB61 and PFTDN first.', 13, 10, '$'
msg_get_done  db 'GETDATETIME dispatch completed - check STUB61 TX= log above.', 13, 10, '$'
msg_set_done  db 'SETDATETIME dispatch completed - check STUB61 TX= log above.', 13, 10, '$'

payload_get db 0x8D, 0x00, 0x70, 0
payload_set db 0x8E, 0x00, 0x70, 0x45, 0x23, 0x00, 0x64
dummy_buf   db 0
