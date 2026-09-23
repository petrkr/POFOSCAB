; frame.inc - full-screen box border, title, and status line, drawn
; at app startup. Adapted from src/pftd/gui.inc's ensure_gui_drawn
; (same hardware-proven primitives - int 10h AH=06h to clear, int 60h
; AH=09h to draw the box, AH=09h write-char + manual cursor advance
; instead of AH=0Eh teletype to avoid the column-39 scroll bug
; documented there).
;
; Layout (40x8 display, box occupies the whole screen):
;   row 0: top border, "PFTC vX.Y.Z (buildid)" inset - PFTC's OWN
;          version/build (src/pftc/version.inc, build_id.inc), not
;          the server's - HELLO's version/build describes the ESP/
;          mock backend, a different thing, shown elsewhere once
;          there's a place for it.
;   rows 1-6: interior, free for future content (SSID/IP/RSSI, etc.)
;   row 7: bottom border, "Status: ..." inset - set_status rewrites
;          this whenever connection state changes.
;
; Title is drawn once at startup regardless of HELLO's outcome (it's
; PFTC's own identity, independent of whether the server answers) -
; unlike PFTD's ensure_gui_drawn, which redraws every request since
; it's a resident hook coexisting with ROM-redrawn screens, PFTC draws
; its frame exactly once per session.

%include "version.inc"
%include "build_id.inc"

FRAME_ROW_BOTTOM equ 7
FRAME_COLS       equ 40

; Clears the screen and draws the full-screen box with the version
; title in the top edge. Called once at startup.
draw_frame:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    es

        push    cs
        pop     ds
        push    cs
        pop     es

        ; Clear the whole screen first - whatever DOS/the previous
        ; program left behind, not just what PFTC itself will draw.
        mov     ah, 0x06
        mov     al, 0                   ; blank the whole window
        mov     bh, 0x07                ; fill attribute (normal)
        mov     ch, 0
        mov     cl, 0
        mov     dh, FRAME_ROW_BOTTOM
        mov     dl, FRAME_COLS-1
        int     0x10

        mov     ax, 0x0900              ; int 60h AH=09h: draw box, AL=0 single line
        xor     bx, bx
        mov     cx, (FRAME_ROW_BOTTOM << 8) | (FRAME_COLS-1)
        xor     dx, dx
        int     0x60

        ; Save the bare bottom border row (just corners + the
        ; horizontal line, no text yet) so set_status can restore
        ; exactly this - not blank-and-redraw, which left stale
        ; characters behind when a shorter status replaced a longer
        ; one (confirmed visually). int 60h AH=08h AL=0 (save chars
        ; only), one row: top-left = bottom-left = bottom-right's row.
        mov     si, frame_status_blank
        mov     cx, (FRAME_ROW_BOTTOM << 8) | (FRAME_COLS-1)
        mov     dx, (FRAME_ROW_BOTTOM << 8)
        mov     ah, 0x08
        xor     al, al
        xor     bh, bh
        int     0x60

        cld                             ; stosb below must go forward -
                                         ; DF is not guaranteed clear on
                                         ; entry, see src/pftd/gui.inc's
                                         ; own cld comment
        mov     di, frame_title_buf
        mov     si, frame_title_text
        call    frame_append_si
        mov     al, VERSION_MAJOR
        call    frame_append_dec8
        mov     al, '.'
        stosb
        mov     al, VERSION_MINOR
        call    frame_append_dec8
        mov     al, '.'
        stosb
        mov     al, VERSION_PATCH
        call    frame_append_dec8
        mov     si, frame_build_text
        call    frame_append_si
        mov     ax, BUILD_ID >> 16
        call    frame_append_hex16
        mov     ax, BUILD_ID & 0xFFFF
        call    frame_append_hex16
        mov     al, ')'
        stosb
        mov     al, ' '
        stosb
        mov     al, 0
        stosb

        mov     ah, 0x02
        xor     bh, bh
        mov     dh, 0
        mov     dl, 2
        int     0x10
        mov     si, frame_title_buf
        call    frame_print_si

        pop     es
        pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; frame_append_si: append ASCIIZ string at DS:SI to DS:DI (di advances
; past the copied bytes, NOT including a NUL). Same as src/pftd/gui.inc's
; own .append_si, but a global label here (not local to draw_frame) so
; set_status/other callers building their own buffers can reuse it too.
; Clobbers AX/SI/DI.
frame_append_si:
        lodsb
        cmp     al, 0
        je      .ret
        stosb
        jmp     frame_append_si
.ret:
        ret

; frame_append_hex16: append AX as 4 lowercase hex digits to DS:DI.
; Same as src/pftd/gui.inc's own .append_hex16, global here for reuse.
; Clobbers AX/BX/CX/DI.
frame_append_hex16:
        push    dx
        mov     dx, ax
        mov     cx, 4
.digit:
        rol     dx, 1
        rol     dx, 1
        rol     dx, 1
        rol     dx, 1
        mov     ax, dx
        and     al, 0x0F
        xor     ah, ah
        mov     bx, ax
        mov     al, [cs:frame_hexdig+bx]
        stosb
        loop    .digit
        pop     dx
        ret

; frame_append_dec8: append AL (0-255) as decimal, no leading zeros, to
; DS:DI. Same algorithm as src/pftd/gui.inc's own .append_dec8, global
; here for reuse. Clobbers AX/BX/CX/DX/DI.
frame_append_dec8:
        xor     ah, ah
        mov     bx, 10
        xor     cx, cx
.divide:
        xor     dx, dx
        div     bx
        push    dx
        inc     cx
        cmp     ax, 0
        jne     .divide
.print:
        pop     dx
        mov     al, dl
        add     al, '0'
        stosb
        loop    .print
        ret

; DS:SI = ASCIIZ status text, with DS=CS (same convention as
; frame_print_si and everywhere else in this file). Rewrites the
; bottom border's inset text (row 7) - caller must keep the text short
; enough to fit (well under 38 columns; the border corners and at
; least one filler dash on each side must remain). Does not touch the
; interior or title.
;
; Implementation: restores the bare border row draw_frame saved
; (frame_status_blank, via int 60h AH=08h) before writing the new
; text, exactly like gui.inc's dialog save/restore - blanking and
; redrawing the border line by hand left stale characters behind
; whenever a shorter status replaced a longer one, since a
; single-row "box" (zero height) has no interior for AH=09h to fill.
set_status:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si                      ; caller's status text pointer
        push    di
        push    es

        push    cs
        pop     es                      ; es:di targets frame_status_buf below

        push    si                      ; stash the caller's SI across
                                         ; the AH=08h call below, which
                                         ; needs SI for its own buffer
        mov     si, frame_status_blank  ; restore the bare border row
        mov     cx, (FRAME_ROW_BOTTOM << 8) | (FRAME_COLS-1)
        mov     dx, (FRAME_ROW_BOTTOM << 8)
        mov     ah, 0x08
        mov     al, 2                   ; AL=2: restore characters only
        xor     bh, bh
        int     0x60
        pop     si                      ; caller's status text pointer back

        cld
        mov     di, frame_status_buf
        mov     al, ' '
        stosb
.copy:
        lodsb
        cmp     al, 0
        je      .copied
        stosb
        jmp     .copy
.copied:
        mov     al, ' '
        stosb
        xor     al, al
        stosb

        mov     ah, 0x02
        xor     bh, bh
        mov     dh, FRAME_ROW_BOTTOM
        mov     dl, 2
        int     0x10
        mov     si, frame_status_buf
        call    frame_print_si

        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; frame_print_si: position cursor at (DH=row, DL=col, set by caller
; via int 10h AH=02h before calling) and print CS:SI. Uses AH=0x09
; write-char + manual cursor-advance, never AH=0x0E teletype - see
; src/pftd/gui.inc's gui_print_si for the column-39 scroll bug this
; avoids. Stops at column 39, no wrap. Clobbers AX/BX/CX/DX/SI.
frame_print_si:
        push    cx
        push    dx
        cld                     ; lodsb below must go forward - DF is
                                 ; not guaranteed clear on entry, see
                                 ; src/pftd/gui.inc's own cld comment
        mov     ah, 0x03
        xor     bh, bh
        int     0x10
.loop:
        lodsb
        cmp     al, 0
        je      .ret
        mov     cx, ax
        push    dx
        mov     ah, 0x09
        mov     al, cl
        xor     bh, bh
        mov     bl, 0x07
        mov     cx, 1
        int     0x10
        pop     dx

        inc     dl
        cmp     dl, FRAME_COLS
        jae     .ret
        mov     ah, 0x02
        xor     bh, bh
        int     0x10
        jmp     .loop
.ret:
        pop     dx
        pop     cx
        ret

; Hides the text cursor (int 10h AH=01h, CH bit 5 set = cursor
; disabled - standard CGA/MDA/VGA BIOS convention). PFTC's frame is a
; fixed display, not a text-entry prompt, so the default blinking DOS
; cursor has nothing meaningful to point at until a later iteration's
; Line INPUT fields need it back.
hide_cursor:
        push    ax
        push    cx
        mov     ah, 0x01
        mov     ch, 0x20
        xor     cl, cl
        int     0x10
        pop     cx
        pop     ax
        ret

; Restores the standard underline cursor shape (CH=0x06, CL=0x07 -
; typical CGA/VGA text-mode default). Call before returning to DOS.
show_cursor:
        push    ax
        push    cx
        mov     ah, 0x01
        mov     ch, 0x06
        mov     cl, 0x07
        int     0x10
        pop     cx
        pop     ax
        ret

frame_title_text: db " PFTC v", 0
frame_build_text: db " (", 0
frame_hexdig:      db "0123456789abcdef"
frame_title_buf:   times 40 db 0
frame_status_buf:  times 40 db 0
frame_status_blank: times FRAME_COLS db 0
