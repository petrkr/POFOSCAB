; gui.inc - thin wrappers around int 60h's message/error window
; primitives (AH=12h/AH=14h), used for every transient status dialog
; PFTC shows ("Connecting...", "SmartCable: offline", etc.) instead of
; hand-rolling box drawing - see the "int 60h GUI reference" section of
; the design plan for the full register documentation these are based
; on. Both functions render a double-line box; first line of the text
; is the title, everything after is the body - the text block is two
; zero-terminated strings back to back, double-zero terminated overall.
;
; Without saving/restoring the screen, dialogs pile up on screen since
; int 60h AH=12h/14h only draw - nothing ever erases them. Save/restore
; is done with int 60h AH=08h around every dialog, the same mechanism
; and 0,0-39,7 full-screen extent PFTCMENU.asm's save_screen/cleanup
; already use - there is no documented way to query the ROM's own
; dialog dismissal, so this rolls PFTC's own using the one documented
; primitive built for it.

GUI_ROW         equ 2
GUI_COL         equ 2

; Full screen, chars-only: 40 columns x 8 rows (see PFTCMENU.asm's
; save_screen: AL=0, CX=0x0727, DX=0), plus 2 bytes at the end of the
; same buffer for the cursor position (int 10h AH=03h's DX result) -
; PFTCMENU.asm's own save/cleanup pattern saves both together, since a
; dialog can move the cursor and it needs restoring same as the text.
GUI_SCREEN_BYTES equ 40*8
GUI_CURSOR_OFFSET equ GUI_SCREEN_BYTES
GUI_BUF_BYTES   equ GUI_SCREEN_BYTES + 2
GUI_BUF_PARAS   equ (GUI_BUF_BYTES + 15) / 16

; Saves the full screen and cursor position into a freshly allocated
; buffer.
; Returns: carry clear and AX = segment of the saved state on success
;          (caller must pass it to gui_restore_screen when done);
;          carry set if the allocation failed (nothing saved).
gui_save_screen:
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es

        mov     ah, 0x48                ; DOS: allocate memory block
        mov     bx, GUI_BUF_PARAS
        int     0x21
        jc      .done                   ; carry already set

        mov     di, ax                  ; di = buffer segment, kept across
                                         ; every call below regardless of
                                         ; what each leaves in ax

        push    ds
        mov     ds, ax
        xor     si, si
        mov     cx, 0x0727              ; bottom-right: row 7, col 39
        xor     dx, dx                  ; top-left: row 0, col 0
        mov     ah, 0x08                ; int 60h AH=08h: screen save/restore
        xor     al, al                  ; AL=0: save characters only
        xor     bh, bh
        int     0x60
        pop     ds

        mov     es, di
        mov     ah, 0x03                ; int 10h AH=03h: get cursor position
        xor     bh, bh
        int     0x10
        mov     [es:GUI_CURSOR_OFFSET], dx

        mov     ax, di                  ; ax = buffer segment for the caller
        clc

.done:
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret

; AX = segment returned by gui_save_screen. Restores the screen and
; cursor position, then frees the buffer.
gui_restore_screen:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    ds
        push    es

        mov     es, ax                  ; keep the segment to free afterward
        mov     ds, ax
        xor     si, si
        mov     cx, 0x0727
        xor     dx, dx
        mov     ah, 0x08
        mov     al, 2                   ; AL=2: restore characters only
        xor     bh, bh
        int     0x60

        mov     dx, [es:GUI_CURSOR_OFFSET]
        mov     ah, 0x02                ; int 10h AH=02h: set cursor position
        xor     bh, bh
        int     0x10

        mov     ah, 0x49                ; DOS: free memory block (ES = segment)
        int     0x21

        pop     es
        pop     ds
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; DS:SI = title\0body\0\0 (see above). Shows an int 60h "message window"
; (AH=12h) - the ROM's own mechanism for transient info dialogs.
; Returns: AX = screen-save segment for gui_restore_screen (0 if the
;          save failed - the dialog is still shown either way, just
;          without a way to clean it back up later).
show_message:
        push    bx
        push    cx
        push    dx
        push    si
        push    di

        call    gui_save_screen
        jnc     .saved
        xor     ax, ax
.saved:
        mov     di, ax                  ; keep the save-segment result across AH=12h (not touched by it)

        mov     ah, 0x12
        xor     bh, bh
        mov     dh, GUI_ROW
        mov     dl, GUI_COL
        int     0x60
        mov     ax, di                  ; ax = save-segment result for the caller

        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        ret

; DS:SI = title\0body\0\0 (see above). Shows an int 60h "error window"
; (AH=14h). Unlike show_message/AH=12h, AH=14h blocks until the user
; presses a key and then erases itself - confirmed by direct test
; (tools/wiretest/DLG.asm): the box stays up until a keypress, then the
; ROM clears it back to what was underneath on its own. So there is no
; screen save/restore here (nothing to clean up afterward) and no
; return value - the call has already fully resolved by the time this
; returns.
show_error:
        push    ax
        push    bx
        push    cx
        push    dx
        push    si

        mov     ah, 0x14
        xor     bh, bh
        mov     dh, GUI_ROW
        mov     dl, GUI_COL
        mov     cx, 1
        int     0x60

        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
