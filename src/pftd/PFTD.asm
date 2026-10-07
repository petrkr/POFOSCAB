; PFTD.COM - Portfolio Transfer Daemon, v1 (HELLO only).
;
; Installs a TSR hook on int 0x61 (the Portfolio's smart-cable API) so a
; client on the other end of the cable can detect that this driver is
; present and query its protocol version/capabilities, in addition to
; the stock ROM File transfer Server commands (payload[0] in [2,6] -
; see ROM_RESEARCH_NOTES.md). See hello.inc for the command layout and
; response format.
;
; Detection mechanism: the ROM server's receive (AX=0x3001, caller CS
; >= C000) is run as a subroutine via pushf + call far - the original
; entry ends in "retf word 0x2", which pops exactly that frame (a plain
; CALL without pushf would break the stack). On return DL=0 means a block
; arrived; payload[0] is then read and dispatched once. DL=06 is the ROM
; idle re-arm with nothing received and is ignored, same as the ROM
; does. The foreign buffer is never written to. Every other int 0x61
; call is passed through with JMP far to the original vector.
;
; To answer a recognized command, dispatch_* issues a REAL int 0x61
; (AH=0x30 AL=0, transmit) from inside our own hook. This re-enters
; pftd_int61_handler recursively, but AX=0x3000 (not 0x3001) so the
; recursive instance falls straight through to the chain. Delaying the
; return to the ROM costs nothing (see ROM_RESEARCH_NOTES.md).
;
; int 0x21 (DOS API) must never be called from inside pftd_int61_handler -
; DOS is not re-entrant and this crashes (observed as garbage-filled
; screen). Install time code below is not subject to this - it never
; re-enters DOS.
;
; This logic is written so it can move into a PFTD.SYS device driver
; later without changes: everything below is self-contained around the
; int 0x61 vector and does not depend on how it was installed. Same
; product, not a separate tool - see this repo's README for the
; directory-level intent (POFOSCAB: Portfolio Smart Cable tools, PFTD
; is the file-transfer-extension daemon living here, not the whole
; repo's scope).
;
; Usage:
;   PFTD                 <- install (stays resident)
;
; Assemble:  nasm -f bin PFTD.asm -o PFTD.COM

CPU 8086
ORG 0x100

section .text

start:
        jmp     install

old61     dd 0
saved_ds  dw 0
saved_dx  dw 0
payload0  db 0        ; captured payload[0] byte, read out safely below

; list_src_ds/list_src_si (used by every dispatch_* that needs more than
; payload[0] out of the foreign receive buffer) and dta_buf (shared
; Find First/Next DTA) now live in common.inc, alongside every other
; *.inc's own buffers - see that file's header for why. %include it
; before anything that uses those buffers.
%include "common.inc"

section .text

%include "gui.inc"
%include "hello.inc"
%include "list.inc"
%include "drives.inc"
%include "mkdir.inc"
%include "delete.inc"
%include "rmdir.inc"
%include "rename.inc"
%include "copy.inc"
%include "datetime.inc"
%include "drawascii.inc"
%include "critical_error.inc"
%include "residentcheck.inc"

section .text

; --- new int 0x61 handler ---
; CPU already pushed FLAGS, CS, IP of the caller. Everything except the
; ROM server's own receive is passed straight through with JMP. The ROM
; receive is run as a subroutine so its result is known immediately.
pftd_int61_handler:
        ; Must run before anything is pushed: check_already_resident
        ; returns via RET (stack still just has FLAGS/CS/IP from the
        ; int 0x61 itself underneath), and if it answered the probe we
        ; IRET immediately, right here, before pushing anything else -
        ; that stack shape is exactly what IRET expects.
        call    check_already_resident
        cmp     ax, PROBE_ANSWER
        jne     .not_probe
        iret
.not_probe:
        cmp     ax, 0x3001
        jne     .chain

        ; Only the ROM server's receive carries commands for us. A
        ; foreground client (CS in RAM) receiving its own response must
        ; pass through untouched. Measured: ROM server CS=C87A, BCC
        ; client CS=0592. Known limitation: a client executing from
        ; ROM/card memory >= C000 would be treated as the server.
        push    bp
        mov     bp, sp
        cmp     word [bp+4], 0xC000     ; caller CS: +0 BP, +2 IP, +4 CS
        pop     bp
        jb      .chain

        mov     [cs:saved_ds], ds
        mov     [cs:saved_dx], dx

        ; ROM entry exits with `retf 2`, so pushf+call far emulates INT.
        pushf
        call    far [cs:old61]
        pushf

        ; DL=0: a block was received. The ROM idle loop re-arms
        ; periodically and gets DL=06 (nothing arrived) - the ROM itself
        ; ignores the buffer then, and so do we. Each received block is
        ; therefore dispatched exactly once, and the buffer is never
        ; written to: the ROM still reads it afterwards (e.g. the
        ; download finish ack), so it must stay intact.
        or      dl, dl
        jnz     .done

        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    es

        mov     ds, [cs:saved_ds]
        mov     si, [cs:saved_dx]
        mov     al, [si]
        push    cs
        pop     ds
        mov     [cs:payload0], al

        ; dispatch_* needing more than payload[0] read the foreign buffer
        ; via list_src_ds:list_src_si (see common.inc)
        mov     ax, [cs:saved_ds]
        mov     [cs:list_src_ds], ax
        mov     ax, [cs:saved_dx]
        mov     [cs:list_src_si], ax

        mov     al, [cs:payload0]
        call    dispatch_hello
        mov     al, [cs:payload0]
        call    dispatch_list
        mov     al, [cs:payload0]
        call    dispatch_drives
        mov     al, [cs:payload0]
        call    dispatch_mkdir
        mov     al, [cs:payload0]
        call    dispatch_delete
        mov     al, [cs:payload0]
        call    dispatch_rmdir
        mov     al, [cs:payload0]
        call    dispatch_rename
        mov     al, [cs:payload0]
        call    dispatch_copy
        mov     al, [cs:payload0]
        call    dispatch_getdatetime
        mov     al, [cs:payload0]
        call    dispatch_setdatetime
        mov     al, [cs:payload0]
        call    dispatch_draw_ascii

        pop     es
        pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
.done:
        popf
        retf    2

.chain:
        jmp     far [cs:old61]

hook_end:
; NOT the TSR residency boundary - see resident_end at the end of this
; file for why. hook_end only marks where pftd_int61_handler's own code
; stops; kept as a landmark for readers, not read by any AH=0x31 call.

; ---- installer ----
%include "hexprint.inc"
%include "pofodetect.inc"

install:
        ; Refuse to install on anything that isn't a real Atari Portfolio
        ; - see pofodetect.inc for the port 0x61 probe this relies on.
        call    is_pofo
        je      .is_portfolio

        mov     dx, msg_not_portfolio
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4c01
        int     0x21

.is_portfolio:
        ; Refuse to double-install: ask any already-resident PFTD hook
        ; whether it's there (see residentcheck.inc). Only safe to try if
        ; int 0x61 actually points somewhere - on real Portfolio hardware
        ; the ROM always has its own int 0x61 handler installed, but a
        ; generic PC/DOS normally has a NULL vector there,
        ; and calling through a NULL vector hangs/crashes instead of
        ; harmlessly returning. So check the vector segment:offset isn't
        ; 0000:0000 first.
        mov     ax, 0x3561
        int     0x21
        mov     ax, es
        or      ax, bx
        jz      .not_resident           ; vector is 0000:0000 - nothing to ask

        mov     ax, PROBE_CMD
        int     0x61
        cmp     ax, PROBE_ANSWER
        jne     .not_resident

        mov     dx, msg_already_resident
        mov     ah, 0x09
        int     0x21
        mov     ax, 0x4c01
        int     0x21

.not_resident:
        ; Print "PFTD v" then VERSION_MAJOR.MINOR.PATCH, e.g.
        ; "PFTD v0.0.0" - 0.0.0 on an ordinary dev build, the real
        ; vMAJOR.MINOR.PATCH on a tagged CI release build (see
        ; version.inc).
        mov     dx, msg_pftd_v
        mov     ah, 0x09
        int     0x21

        mov     al, VERSION_MAJOR
        call    print_dec8
        mov     dx, msg_dot
        mov     ah, 0x09
        int     0x21
        mov     al, VERSION_MINOR
        call    print_dec8
        mov     dx, msg_dot
        mov     ah, 0x09
        int     0x21
        mov     al, VERSION_PATCH
        call    print_dec8

        ; Print " (" then BUILD_ID as 8 lowercase hex digits, e.g.
        ; " (ffff0005" - completes the banner to "PFTD v0.0.0 (ffff0005"
        mov     dx, msg_build_open
        mov     ah, 0x09
        int     0x21

        mov     dx, (BUILD_ID >> 16) & 0xFFFF
        mov     ax, BUILD_ID & 0xFFFF
        call    print_hex32

        ; Close the banner: ")\r\n" -> full line is
        ; "PFTD v0.0.0 (ffff0005)" - no "Installing..."/"Installed"
        ; text either side of it (exit code alone - AL=0 below, vs.
        ; AL=1 on the error paths above - already signals success/
        ; failure; a silent return to the DOS prompt is enough for a
        ; human running this interactively). Confirmed on real
        ; hardware that earlier, longer forms ("PFTD v1 release X.Y.Z
        ; (XXXXXXXX) - Installing...") wrapped mid-word on this
        ; hardware's 40-column screen at the extreme 255.255.255
        ; release version.
        mov     dx, msg_banner_end
        mov     ah, 0x09
        int     0x21

        ; Read the current int 0x61 vector (DOS Get Interrupt Vector,
        ; AH=0x35) and stash it in old61 so pftd_int61_handler can chain
        ; to it later - this must happen before we install our own hook.
        mov     ax, 0x3561
        int     0x21
        mov     [old61], bx
        mov     [old61+2], es

        ; Point int 0x61 at pftd_int61_handler (DOS Set Interrupt Vector,
        ; AH=0x25). DS must be CS (the segment pftd_int61_handler lives
        ; in) while DX holds the offset - saved/restored around the call
        ; since DS is otherwise whatever DOS gave us at program start.
        push    ds
        mov     dx, pftd_int61_handler
        mov     ax, 0x2561
        int     0x21
        pop     ds

        ; Read and save the current int 0x24 vector (critical_error.inc),
        ; then install pftd_int24_handler - same AH=0x35/AH=0x25 DOS API
        ; pair used above for int 0x61, same DS=CS/DX=offset discipline.
        ; Installed once here, resident for the life of the TSR - see
        ; critical_error.inc's header for why (needed by mkdir.inc/
        ; delete.inc, both of which do real disk I/O and can raise int 0x24
        ; on a drive with no/write-protected media).
        mov     ax, 0x3524
        int     0x21
        mov     [old24], bx
        mov     [old24+2], es

        push    ds
        mov     dx, pftd_int24_handler
        mov     ax, 0x2524
        int     0x21
        pop     ds

        ; No "Installed" confirmation message here - the exit code
        ; (AL=0 below, vs. AL=1 on the error paths above) already
        ; signals success/failure, and a silent return to the DOS
        ; prompt is enough for a human running this interactively.

        ; Terminate and Stay Resident (DOS AH=0x31): keep everything up to
        ; resident_end (code + hook state + every dispatch_*'s .bss
        ; buffers) allocated after this program exits, so
        ; pftd_int61_handler keeps working once the shell prompt
        ; returns. Size is in 16-byte paragraphs, rounded up.
        ;
        ; resident_end is a .bss label (see the bottom of this file),
        ; NOT the text_end .text landmark right after the installer -
        ; a real-hardware bug (confirmed: DOS handed the memory right
        ; after text_end to the next program it ran, silently
        ; corrupting copy_buf/critical_error_flag/etc., causing
        ; unrelated-looking hangs and "Memory full" errors later)
        ; showed that .bss buffers are NOT included in "up to
        ; text_end" the way earlier comments here assumed. NASM's
        ; -f bin format concatenates ALL .text content from every
        ; included file first, then ALL .bss content after that,
        ; regardless of source order - so a label placed in .text right
        ; after the installer only marks the end of .text, leaving
        ; every dispatch_*'s .bss buffer (dta_buf through old24)
        ; completely unreserved and fair game for the next DOS program.
        ; Fix: resident_end is now a .bss label placed after every
        ; %include, so it always lands after the true end of .bss
        ; content - automatically correct as buffers are added, no
        ; manual recalculation needed.
        mov     dx, resident_end
        add     dx, 0x0F
        mov     cl, 4
        shr     dx, cl
        mov     ax, 0x3100
        int     0x21

msg_pftd_v        db 'PFTD v$'
msg_dot           db '.$'
msg_build_open    db ' ($'
msg_banner_end    db ')', 13, 10, '$'
msg_not_portfolio db 'This is not an Atari Portfolio.', 13, 10, '$'
msg_already_resident db 'PFTD is already resident.', 13, 10, '$'

section .text
; Landmark only - marks the end of .text content, NOT the TSR
; residency boundary. Kept for readers tracing where the installer's
; own code ends; never read by any AH=0x31 call - see that call's
; comment above for why a .text label here would be wrong.
text_end:

section .bss
; This MUST be the last .bss declaration in the whole file (after
; every %include above, each of which has its own section .bss block)
; - NASM's -f bin format concatenates ALL .bss content from every
; included file into one contiguous block, in the order those blocks
; were opened; a label placed here, after all of them, always lands
; at the true end of that block regardless of what buffers exist or
; get added later. This is what AH=0x31 actually reserves - see that
; call's comment for the real-hardware bug this fixes.
resident_end:
