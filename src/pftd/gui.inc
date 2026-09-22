; gui.inc - shared on-screen status helpers, callable from any dispatch_*
; command that wants to show real-world transfer feedback directly on the
; Portfolio's own LCD, independent of the wire protocol (the client never
; sees any of this - it's a local side effect of processing a request that
; already arrived).
;
; Screen real estate: rows 5-7 (0-indexed BIOS text rows) of the File
; Transfer Server screen are never redrawn by the ROM during that screen's
; own operation or when backing out to the parent Setup menu - confirmed
; on real hardware, see project memory "pftd screen log idea". Only
; leaving Setup entirely, back to the DOS prompt, does a full-screen
; redraw that clears them, so nothing here needs its own cleanup path.
;
; Layout: a full-screen box border (CP437 box-drawing - confirmed on
; real hardware to render correctly here despite this not being a
; standard IBM CP437 font, see project memory) drawn once per Setup
; session via ensure_gui_drawn:
;   row 0: top border, "PFTD vX build XXXXXXXX" inset into it
;   row 1: side borders only, interior left blank (reserved for later)
;   rows 2-4: side borders only - interior is the ROM's own "Waiting
;             for connection"/"Receiving"/"Transmitting" box, confirmed
;             via DRAW_ASCII testing that the ROM only ever redraws
;             that box's own interior, never this screen's edges, so
;             this is safe to coexist with it untouched
;   row 5: free interior line for any dispatch_* status text (gui_print)
;   row 6: progress bar (gui_progress) - has its own left/right border
;          chars built into the bar string itself
;   row 7: bottom border
;
; Redraw-every-call design: ensure_gui_drawn always redraws (see its
; own header comment for why), so it is safe - and cheap enough - for
; every dispatch_* that wants to show status to unconditionally call
; it first, regardless of which command happens to run first. Callers
; must NOT assume HELLO (or any other specific command) already drew
; it and skip calling it themselves.
;
; Call site discipline: same as hello.inc/copy.inc's dispatch_* functions
; - preserve every register int 0x10 can plausibly touch (AX/BX/CX/DX/SI/
; DS/ES), and set DS=CS before touching any local string/table data here,
; since DS is not guaranteed to already be CS at every possible call site
; (see PFTD.asm header comment on the same discipline, and this file's
; own predecessor bug found via the HELLO screen-write test).

; build_id.inc/version.inc are %include-guarded implicitly by NASM
; erroring on a duplicate EQU redefinition with a different value -
; not an issue here since both are pure constants, so including them
; again here (in case gui.inc is %include'd before hello.inc, which
; would otherwise leave BUILD_ID/VERSION_* undefined when row 0's text
; is built below) is safe regardless of include order elsewhere in
; PFTD.asm.
%include "build_id.inc"
%include "version.inc"

GUI_ROW_TOP    equ 5
GUI_ROW_MID    equ 6
GUI_ROW_BOTTOM equ 7
GUI_COLS       equ 40

; ensure_gui_drawn: (re)draws the full-screen box border every time
; it's called - unconditionally, not just once per PFTD session. This
; used to be a one-shot (guarded by a gui_drawn flag), but PFTD is a
; resident TSR that outlives any single trip through the File Transfer
; Server screen: the ROM redraws the whole screen from scratch each
; time Setup re-enters that screen, which would otherwise leave a
; stale "already drawn, never again" flag with nothing on screen to
; show for it. Safe to call unconditionally now that PFTD.asm's own
; payload-buffer-clear fix stops the ROM's idle-loop traffic from
; re-firing dispatch_hello (and therefore this) repeatedly for a
; single real HELLO - see project memory "pftd screen log idea".
; Preserves AX/BX/CX/DX/SI/DI/DS/ES.
ensure_gui_drawn:
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

        ; Clear the WHOLE screen before drawing our border - the
        ; underlying screen can have leftover content from wherever
        ; the user was before landing here (the System Setup submenu
        ; this File Transfer Server screen is entered from, possibly
        ; even a DOS prompt's last output), not just whatever the ROM
        ; itself last drew. Confirmed on real hardware: a leftover
        ; "File transfer..." submenu line was still visible on row 5
        ; under our own content before this was added - see project
        ; memory "pftd screen log idea". Deliberately also clears rows
        ; 2-4 (the ROM's own "Waiting for connection"/etc box) - the
        ; ROM redraws that box's content on its own periodic update
        ; (confirmed via this project's earlier repeated-int-0x61
        ; debugging), so it reappears within roughly the same ~0.5s
        ; cadence rather than needing to be preserved here.
        ; int 0x10 AH=0x06 (scroll up, AL=0 = blank instead of scroll)
        ; clears CH,CL to DH,DL in one BIOS call instead of looping
        ; per-row.
        mov     ah, 0x06
        mov     al, 0                   ; blank the whole window
        mov     bh, 0x07                ; fill attribute (normal)
        mov     ch, 0
        mov     cl, 0
        mov     dh, GUI_ROW_BOTTOM
        mov     dl, GUI_COLS-1
        int     0x10

        ; Row 0: build "<0xDA><0xC4> PFTD vX.Y.Z (XXXXXXXX)<0xC4>...<0xBF>"
        ; into gui_row0_buf, filling to GUI_COLS-1 chars (leaves the
        ; trailing NUL) before printing. Version leads (the human-
        ; facing identity: 0.0.0 on an ordinary dev build, the real
        ; tag version on a release build - see version.inc), build
        ; hash trails in parens - both %include'd via hello.inc
        ; already (so VERSION_MAJOR/MINOR/PATCH and BUILD_ID are
        ; defined by the time PFTD.asm reaches this file's %include -
        ; see PFTD.asm's include order).
        cld                             ; stosb below must go forward -
                                         ; DF is not guaranteed clear on
                                         ; entry (found the hard way: a
                                         ; set DF here silently corrupts
                                         ; memory past gui_row0_buf/
                                         ; gui_progress_buf instead of
                                         ; filling them, since stosb
                                         ; walks backward when DF=1 -
                                         ; see project memory "pftd
                                         ; screen log idea")
        mov     di, gui_row0_buf
        mov     al, 0xDA
        stosb
        mov     al, 0xC4
        stosb
        mov     si, gui_title_text
        call    .append_si

        mov     al, VERSION_MAJOR
        call    .append_dec8
        mov     al, '.'
        stosb
        mov     al, VERSION_MINOR
        call    .append_dec8
        mov     al, '.'
        stosb
        mov     al, VERSION_PATCH
        call    .append_dec8

        mov     si, gui_build_text
        call    .append_si
        mov     ax, BUILD_ID >> 16
        call    .append_hex16
        mov     ax, BUILD_ID & 0xFFFF
        call    .append_hex16
        mov     al, ')'
        stosb

.row0_pad:
        ; di currently points past the last content byte written above
        ; (compare to gui_row0_buf, not a fixed literal - the title/hex
        ; text length is fixed at assembly time via gui_title_text, but
        ; keeping this relative avoids silently mis-padding if that
        ; string is ever edited without updating a hardcoded count).
        mov     ax, di
        sub     ax, gui_row0_buf
        cmp     ax, GUI_COLS-1
        jae     .row0_close
        mov     al, 0xC4
        stosb
        jmp     .row0_pad
.row0_close:
        mov     al, 0xBF
        stosb
        mov     al, 0
        stosb

        mov     ah, 0x02
        mov     bh, 0
        mov     dh, 0
        mov     dl, 0
        int     0x10
        mov     si, gui_row0_buf
        call    gui_print_si

        ; Rows 1-6: side borders only, interior untouched (row 1 blank,
        ; rows 2-4 are the ROM's own box, row 5 is free for gui_print
        ; status text, row 6 gets overwritten by gui_progress's own
        ; border chars whenever that's called - drawing plain side
        ; borders here too keeps row 6 looking consistent even before
        ; the first gui_progress call happens).
        mov     cx, 6
        mov     dh, 1
.side_rows:
        push    cx
        mov     ah, 0x02
        mov     bh, 0
        mov     dl, 0
        int     0x10
        mov     al, 0xB3
        mov     ah, 0x09
        mov     bh, 0
        mov     bl, 0x07
        mov     cx, 1
        int     0x10

        mov     ah, 0x02
        mov     bh, 0
        mov     dl, GUI_COLS-1
        int     0x10
        mov     al, 0xB3
        mov     ah, 0x09
        mov     bh, 0
        mov     bl, 0x07
        mov     cx, 1
        int     0x10

        pop     cx
        inc     dh
        loop    .side_rows

        ; Row 7: bottom border.
        mov     ah, 0x02
        mov     bh, 0
        mov     dh, GUI_ROW_BOTTOM
        mov     dl, 0
        int     0x10
        mov     si, gui_border_bottom
        call    gui_print_si

.done:
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; .append_si: append ASCIIZ string at DS:SI to DS:DI (di advances past
; the copied bytes, NOT including a NUL - caller adds its own
; terminator later). Local to ensure_gui_drawn. Clobbers AX/SI/DI.
.append_si:
        lodsb
        cmp     al, 0
        je      .append_si_ret
        stosb
        jmp     .append_si
.append_si_ret:
        ret

; .append_hex16: append AX as 4 lowercase hex digits to DS:DI (di
; advances past them). Local to ensure_gui_drawn. Clobbers AX/BX/CX/DI.
.append_hex16:
        push    dx
        mov     dx, ax
        mov     cx, 4
.append_hex16_digit:
        rol     dx, 1
        rol     dx, 1
        rol     dx, 1
        rol     dx, 1
        mov     ax, dx
        and     al, 0x0F
        xor     ah, ah
        mov     bx, ax
        mov     al, [cs:gui_hexdig+bx]
        stosb
        loop    .append_hex16_digit
        pop     dx
        ret

; .append_dec8: append AL (0-255) as decimal, no leading zeros, to
; DS:DI (di advances past it). Local to ensure_gui_drawn. Same
; algorithm as hexprint.inc's print_dec8, adapted to append into a
; buffer via stosb instead of printing straight to the screen via
; int 0x21 (not callable from here - see PFTD.asm header comment on
; int 0x21 reentrancy). Clobbers AX/BX/CX/DX/DI.
.append_dec8:
        xor     ah, ah          ; ax = 0..255
        mov     bx, 10
        xor     cx, cx          ; cx = digit count pushed so far
.append_dec8_divide:
        xor     dx, dx          ; dx:ax is the dividend - dx must be 0,
                                 ; ax alone is <= 255 so this is safe
        div     bx              ; ax = ax/10, dx = ax%10
        push    dx              ; remainder = next digit, least
        inc     cx              ; significant first
        cmp     ax, 0
        jne     .append_dec8_divide
.append_dec8_print:
        pop     dx
        mov     al, dl
        add     al, '0'
        stosb
        loop    .append_dec8_print
        ret

; gui_print: position cursor at (DH=row, DL=col) and print the ASCIIZ
; string at CS:SI. Preserves AX/BX/CX/DX/SI/DS/ES.
gui_print:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    ds
        push    es

        push    cs
        pop     ds
        mov     ah, 0x02
        mov     bh, 0
        int     0x10
        call    gui_print_si

        pop     es
        pop     ds
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; gui_print_si: internal helper - print the ASCIIZ string at CS:SI,
; starting at the current cursor position. Requires DS=CS (callers
; above set it). Clobbers AX/BX/CX/DX/SI.
;
; Uses AH=0x09 (write char at cursor, does NOT advance the cursor) plus
; a manual AH=0x02 cursor-position bump between characters, instead of
; the simpler AH=0x0E BIOS teletype used elsewhere in this driver
; (hello.inc's screen-write tests). Teletype auto-advances/wraps the
; cursor past column 39, which triggers a full-screen scroll on this
; hardware - confirmed via DRAW_ASCII testing (col 40 overflow visibly
; scrolled the whole File Transfer Server screen up by one line). That
; would silently corrupt this driver's whole fixed-row layout AND is
; the one case (a border character deliberately placed at the last
; column, e.g. the bottom-right corner) this file cannot just avoid by
; keeping strings short - it must not use AH=0x0E for ANY position
; that can land on column 39. AH=0x09 has no such wrap risk since it
; never moves the cursor at all - this function moves it explicitly
; instead, one column at a time, which is also what lets it stop
; safely at column 39 without ever asking BIOS to advance past it.
gui_print_si:
        push    cx
        push    dx
        cld                     ; lodsb below must go forward - see
                                 ; ensure_gui_drawn's cld comment
        mov     ah, 0x03        ; get current cursor position (row,col)
        mov     bh, 0
        int     0x10            ; -> dh=row, dl=col
.loop:
        lodsb
        cmp     al, 0
        je      .ret
        mov     cx, ax          ; stash the character in cl across the
                                 ; position-query call below
        push    dx
        mov     ah, 0x09        ; write char at cursor, no cursor move
        mov     al, cl
        mov     bh, 0
        mov     bl, 0x07        ; normal attribute
        mov     cx, 1           ; one repetition
        int     0x10
        pop     dx

        inc     dl
        cmp     dl, GUI_COLS
        jae     .ret            ; stop at the edge - never step past
                                 ; column 39, no wrap
        mov     ah, 0x02
        mov     bh, 0
        int     0x10
        jmp     .loop
.ret:
        pop     dx
        pop     cx
        ret

; gui_progress: draw a progress bar on row 6 from AL = percent (0-100,
; clamped). Bar is GUI_COLS-4 characters wide (leaves room for the box's
; left/right border column and one space of padding each side), filled
; with 0xDB (solid block) up to the percentage, 0xB0 (light shade) for
; the remainder. Preserves AX/BX/CX/DX/SI/DS/ES.
; -2, not -3: the bar starts at col=1 with no left padding (fill
; begins immediately, right up against the screen's own left border
; at col=0 - confirmed on real hardware this reads fine, no visual
; collision with that border character). gui_print_si CAN write at
; col 39 (the last safe column) but stops before col 40, so at most
; 39 characters fit starting from col=1 (cols 1..39).
; fill(GUI_BAR_WIDTH) + space(1) = GUI_BAR_WIDTH+1 must be <= 39, so
; GUI_BAR_WIDTH <= 38 - GUI_COLS-2 (38) fits exactly.
GUI_BAR_WIDTH equ GUI_COLS-2

; Builds the bar into gui_progress_buf first, then prints it via
; gui_print_si in one go - see gui_print_si's own header comment for
; why direct AH=0x0E teletype calls are unsafe for anything that can
; land on column 39. No border characters of its own (just padding
; spaces) - it prints starting at col=1, inside ensure_gui_drawn's
; own screen-wide border (0xB3 at col=0 and col=39), so a border
; character here would just duplicate the screen border right next to
; it - confirmed visually on real hardware as a redundant "||".
gui_progress:
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
        cld                             ; stosb below must go forward -
                                         ; see ensure_gui_drawn's cld
                                         ; comment for why this matters

        cmp     al, 100
        jbe     .clamped
        mov     al, 100
.clamped:
        ; filled = al * GUI_BAR_WIDTH / 100, 8086-safe 16-bit divide
        xor     ah, ah
        mov     bx, GUI_BAR_WIDTH
        mul     bx              ; dx:ax = al * GUI_BAR_WIDTH
        mov     bx, 100
        div     bx              ; ax = filled count, dx = remainder (unused)
        mov     [gui_progress_filled], ax  ; stashed to memory - AX
                                 ; itself gets clobbered below by
                                 ; 'mov al, 0xDB' (found the hard way:
                                 ; that silently turns the "ax still
                                 ; holds filled count" assumption false,
                                 ; underflowing the subtraction below
                                 ; into a ~65000-iteration stosb loop
                                 ; that stomped memory well past this
                                 ; 41-byte buffer - see project memory
                                 ; "pftd screen log idea")
        mov     cx, ax          ; cx = filled count

        mov     di, gui_progress_buf
        mov     al, 0xDB        ; solid block, filled portion
.fill_loop:
        cmp     cx, 0
        je      .empty_setup
        stosb
        dec     cx
        jmp     .fill_loop

.empty_setup:
        mov     cx, GUI_BAR_WIDTH
        sub     cx, [gui_progress_filled]
.empty_loop2:
        cmp     cx, 0
        je      .end_bar
        mov     al, 0xB0        ; light shade, empty portion
        stosb
        dec     cx
        jmp     .empty_loop2

.end_bar:
        ; No right padding: with GUI_BAR_WIDTH=GUI_COLS-1, the fill
        ; ends exactly at col 39 - the outer screen border's own right
        ; 0xB3 (drawn by ensure_gui_drawn) - so a trailing space here
        ; would silently overwrite that border character (confirmed
        ; on real hardware: the right border was getting erased).
        mov     al, 0
        stosb

        mov     ah, 0x02
        mov     bh, 0
        mov     dh, GUI_ROW_MID
        mov     dl, 1
        int     0x10
        mov     si, gui_progress_buf
        call    gui_print_si

        pop     es
        pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

section .bss

gui_progress_buf:     resb GUI_COLS+1  ; built bar string, NUL-terminated
gui_row0_buf:         resb GUI_COLS+1  ; built row-0 title string, same
gui_progress_filled:  resw 1           ; see gui_progress's own comment

section .text

; CP437 box-drawing: 0xC0 bottom-left, 0xD9 bottom-right, 0xC4
; horizontal, 0xB3 vertical, 0xDA/0xBF top corners (used inline by
; ensure_gui_drawn above while building gui_row0_buf, not through a
; static table - row 0's content is built dynamically to fit the
; version/build-id text).
gui_border_bottom:
        db      0xC0
        times (GUI_COLS-2) db 0xC4
        db      0xD9, 0

gui_title_text: db ' PFTD v', 0
gui_build_text: db ' (', 0
gui_hexdig:     db '0123456789abcdef'
