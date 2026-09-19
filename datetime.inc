; datetime.inc - GETDATETIME (0x8D) and SETDATETIME (0x8E) commands:
; read/write the Portfolio's system date and time (int 0x21
; AH=0x2A/0x2C - Get Date/Get Time, and AH=0x2B/0x2D - Set Date/Set
; Time). Both commands handle date AND time together in one request/
; response, not four separate GETDATE/SETDATE/GETTIME/SETTIME commands
; - nobody realistically wants to set only one of the two, and this
; saves two command bytes.
;
; Command bytes 0x8D/0x8E continue the 0x80+ PFTD command space - see
; hello.inc's header comment for why that space is safe.
;
; Date/time wire format: packed DOS format, identical to what LIST
; extended already sends for a file's date/time (list.inc's
; dta_buf+0x18/+0x16 fields, passed through from Find First's DTA
; unmodified):
;   date (2 bytes): bits 15-9 = year-1980, bits 8-5 = month, bits 4-0 = day
;   time (2 bytes): bits 15-11 = hour, bits 10-5 = minute, bits 4-0 = second/2
; Chosen for consistency with LIST extended - a client parsing LIST
; responses already has a packed-date/time parser and can reuse it here
; without a second format. Accepted trade-off, not an oversight: packed
; format has no hundredths-of-second field (AH=0x2C/AH=0x2D both have
; one) and a narrower year range (1980-2107) than AH=0x2A's full 16-bit
; CX value - both losses are deliberate for wire-format consistency.
;
; GETDATETIME (0x8D): request is a single byte, no path (same shape as
; DRIVES/HELLO - see drives.inc). Response (fixed 4 bytes): packed
; date(2B) + packed time(2B). No status/errcode byte - AH=0x2A/AH=0x2C
; have no documented failure mode (see critical-error note below), same
; as DRIVES's plain 1-byte response.
;
; SETDATETIME (0x8E): request is 0x8E, 0x00, 0x70 (mirrors the ROM's
; reserved-byte convention, kept for consistency with every other
; command even though there's no path here) + packed date(2B) + packed
; time(2B) - a fixed-size binary payload, not an ASCIIZ string, so no
; NUL-terminator scanning like the path-based commands do. Response
; (fixed 2 bytes), same status/errcode shape as mkdir.inc/delete.inc/
; rmdir.inc/rename.inc/copy.inc:
;   offset 0  status  (0x20 = ok, 0x10 = error)
;   offset 1  errcode (0 on success; 4 = access denied/invalid - AH=0x2B
;                       or AH=0x2D returned AL=0xFF, the only documented
;                       failure mode for either call, i.e. an
;                       out-of-range date or time value; 0xFF = critical
;                       error fired - see below)
;
; critical_error.inc dependency: AH=0x2A/0x2B/0x2C/0x2D are documented
; (RBIL) as pure DOS-internal variable read/writes with no disk/media
; access at all - the same category as drives.inc's AH=0x0E/AH=0x19,
; which this driver confirmed critical-error-free on real Portfolio
; hardware. By analogy, these four calls are expected to need no
; critical_error_flag handling either - but this is NOT independently
; confirmed on real hardware (unlike AH=0x0E/AH=0x19, which was
; specifically tested); this repo has zero prior usage of any of these
; four DOS functions. SETDATETIME still clears/checks
; critical_error_flag around both AH=0x2B and AH=0x2D defensively
; (cheap insurance, consistent with every command that touches DOS
; state) even though it is expected to never fire - see STATUS.md for
; current verification state.

GETDATETIME_CMD equ 0x8D
SETDATETIME_CMD equ 0x8E

; dispatch_getdatetime: called from pftd_int61_handler with AL =
; payload[0] and CS=DS. No extra payload needed (same as
; dispatch_hello/dispatch_drives). Preserves AX/CX/DX/DS - see
; dispatch_hello's header comment for why.
dispatch_getdatetime:
        cmp     al, GETDATETIME_CMD
        jne     .not_getdatetime

        push    ax
        push    bx
        push    cx
        push    dx
        push    ds

        push    cs
        pop     ds

        ; AH=0x2A Get Date: CX=year (1980-2099+), DH=month, DL=day.
        mov     ah, 0x2A
        int     0x21

        ; Pack into date bits 15-9=year-1980/8-5=month/4-0=day. Built
        ; up bit-field by bit-field (day, then month<<5, then
        ; year-1980<<9) rather than computing year's shift directly,
        ; since CPU 8086 only has single-bit shl/shr (no shl reg,imm8)
        ; - same constraint hexprint.inc's header documents.
        sub     cx, 1980                ; cx = year-1980
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1
        shl     cx, 1                   ; cx = (year-1980) << 9

        xor     ax, ax
        mov     al, dh                  ; al = month
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1                   ; ax = month << 5
        or      cx, ax

        xor     ax, ax
        mov     al, dl                  ; al = day (bits 4-0, no shift)
        or      cx, ax

        mov     [datetime_response], cx

        ; AH=0x2C Get Time: CH=hour, CL=minute, DH=second, DL=hundredths
        ; (dropped - packed format has no field for it). Pack into time
        ; bits 15-11=hour/10-5=minute/4-0=second/2.
        mov     ah, 0x2C
        int     0x21
        xor     ah, ah
        mov     al, ch                  ; al = hour
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1                   ; ax = hour << 11
        mov     bx, ax
        xor     ax, ax
        mov     al, cl                  ; al = minute
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1
        shl     ax, 1                   ; ax = minute << 5
        or      bx, ax
        xor     ax, ax
        mov     al, dh                  ; al = second
        shr     ax, 1                   ; second/2 (packed format's
                                         ; 5-bit field only holds 0-29,
                                         ; each unit = 2 real seconds)
        or      bx, ax
        mov     [datetime_response+2], bx

        mov     dx, datetime_response
        mov     cx, 4
        mov     ax, 0x3000              ; AH=0x30 AL=0 = transmit block
        int     0x61

        pop     ds
        pop     dx
        pop     cx
        pop     bx
        pop     ax
.not_getdatetime:
        ret

; dispatch_setdatetime: called from pftd_int61_handler with AL =
; payload[0] and CS=DS. Needs 4 bytes (packed date+time) out of the
; foreign receive buffer, via list_src_ds/list_src_si (set generically
; by pftd_int61_handler - see list.inc's dispatch_list header comment)
; - a fixed-size binary copy, not an ASCIIZ scan, since there's no
; terminator to look for.
dispatch_setdatetime:
        cmp     al, SETDATETIME_CMD
        jne     .not_setdatetime

        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    ds

        mov     ax, [cs:list_src_ds]
        mov     ds, ax
        mov     si, [cs:list_src_si]
        add     si, 3                   ; skip the 3-byte request header
        mov     ax, [si]                ; packed date
        mov     bx, [si+2]              ; packed time

        push    cs
        pop     ds                      ; DS=CS from here on - nothing
                                         ; below this line ever uses the
                                         ; foreign segment again

        ; Stash packed time to memory - every register gets reused by
        ; the date-unpacking arithmetic below, same "more live values
        ; than 8086 has registers for" situation list.inc's header
        ; documents for its own free-space scratch cells.
        mov     [setdatetime_packed_time], bx

        ; Unpack date (ax = packed date) into CX=year, DH=month, DL=day
        ; for AH=0x2B. Packed layout: bits 15-9=year-1980, 8-5=month,
        ; 4-0=day.
        mov     dl, al
        and     dl, 0x1F                ; dl = day (bits 4-0)

        mov     bx, ax
        shr     bx, 1
        shr     bx, 1
        shr     bx, 1
        shr     bx, 1
        shr     bx, 1                   ; bx = packed >> 5 (month in low
                                         ; 4 bits, year-1980 above that)
        mov     dh, bl
        and     dh, 0x0F                ; dh = month

        shr     bx, 1
        shr     bx, 1
        shr     bx, 1
        shr     bx, 1                   ; bx = year-1980
        add     bx, 1980
        mov     cx, bx                  ; cx = year

        mov     byte [cs:critical_error_flag], 0
        mov     ah, 0x2B                ; Set Date
        int     0x21
        cmp     byte [cs:critical_error_flag], 0
        jne     .critical_fired
        cmp     al, 0xFF
        je      .invalid

        ; Unpack time (stashed earlier) into CH=hour, CL=minute,
        ; DH=second, DL=0 for AH=0x2D. Packed layout: bits
        ; 15-11=hour, 10-5=minute, 4-0=second/2.
        mov     ax, [setdatetime_packed_time]
        mov     dh, al
        and     dh, 0x1F                ; dh = second/2 units
        add     dh, dh                  ; dh = second (units * 2)

        shr     ax, 1
        shr     ax, 1
        shr     ax, 1
        shr     ax, 1
        shr     ax, 1                   ; ax = packed >> 5 (minute in
                                         ; low 6 bits, hour above that)
        mov     cl, al
        and     cl, 0x3F                ; cl = minute

        shr     ax, 1
        shr     ax, 1
        shr     ax, 1
        shr     ax, 1
        shr     ax, 1
        shr     ax, 1                   ; ax = hour
        mov     ch, al                  ; ch = hour
        mov     dl, 0                   ; hundredths - no source field,
                                         ; always 0

        mov     byte [cs:critical_error_flag], 0
        mov     ah, 0x2D                ; Set Time
        int     0x21
        cmp     byte [cs:critical_error_flag], 0
        jne     .critical_fired
        cmp     al, 0xFF
        je      .invalid

        ; success
        mov     byte [datetime_response], 0x20
        mov     byte [datetime_response+1], 0
        jmp     .respond

.critical_fired:
        mov     byte [datetime_response], 0x10
        mov     byte [datetime_response+1], 0xFF
        jmp     .respond

.invalid:
        mov     byte [datetime_response], 0x10
        mov     byte [datetime_response+1], 4

.respond:
        mov     dx, datetime_response
        mov     cx, 2
        mov     ax, 0x3000              ; AH=0x30 AL=0 = transmit block
        int     0x61

        pop     ds
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
.not_setdatetime:
        ret

section .bss

; Shared by both dispatchers - GETDATETIME uses all 4 bytes as packed
; date(2B)+time(2B); SETDATETIME only ever writes/reads the first 2
; bytes as status(1B)+errcode(1B). Safe to share: the two dispatchers
; never run in the same int 0x61 hook invocation (payload[0] can only
; match one of GETDATETIME_CMD/SETDATETIME_CMD at a time).
datetime_response: resb 4

; dispatch_setdatetime's packed time, stashed while bx/cx/dx are reused
; for date unpacking - see that routine's comment.
setdatetime_packed_time: resw 1

section .text
