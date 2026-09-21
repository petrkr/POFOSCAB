; drawascii.inc - DRAW_ASCII (0x8F) command: writes arbitrary text at an
; arbitrary row/col on the Portfolio's own LCD via gui_print (see
; gui.inc). Diagnostic/debug tool, not a file-transfer command - built to
; interactively map out which screen regions the ROM's File Transfer
; Server screen does/doesn't redraw during normal operation, without
; needing a PFTD rebuild for every test position (see project memory
; "pftd screen log idea"). Left in the command set since it stays useful
; for any future on-screen layout work, not removed after the mapping
; question above is answered.
;
; Command byte 0x8F continues the 0x80+ PFTD command space - see
; hello.inc's header comment for why that space is safe.
;
; Request layout: payload[0]=0x8F, payload[1..2] unused/reserved (0x00,
; 0x70 - mirrors the ROM's own convention), payload[3]=row (0-7),
; payload[4]=col (0-39), payload[5..]=ASCIIZ text to print. No bounds
; checking on row/col/text length - this is a debug tool poked at
; directly over the wire by whoever is testing, not a client-facing
; command with untrusted input.
;
; No response - fire-and-forget, matching how HELLO's own screen-write
; side effect works (no protocol change, purely a local side effect of
; processing a request that already arrived over the wire).

DRAW_ASCII_CMD equ 0x8F

; dispatch_draw_ascii: called from pftd_int61_handler with AL =
; payload[0], CS=DS. Uses list_src_ds/list_src_si (set by
; pftd_int61_handler alongside payload0, see PFTD.asm) to pull row/col/
; text out of the foreign buffer, same mechanism as every other
; multi-byte dispatch_* command (list.inc/copy.inc/etc).
dispatch_draw_ascii:
        cmp     al, DRAW_ASCII_CMD
        jne     .not_draw_ascii

        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        push    ds

        push    cs
        pop     es
        mov     ax, [cs:list_src_ds]
        mov     ds, ax
        mov     si, [cs:list_src_si]
        add     si, 3
        mov     al, [si]        ; row
        inc     si
        mov     [cs:draw_ascii_row], al
        mov     al, [si]        ; col
        inc     si
        mov     [cs:draw_ascii_col], al

        mov     di, draw_ascii_text
.copy_text:
        mov     al, [si]
        mov     [es:di], al
        inc     si
        inc     di
        cmp     al, 0
        jne     .copy_text

        push    cs
        pop     ds              ; DS=CS from here on

        mov     dh, [cs:draw_ascii_row]
        mov     dl, [cs:draw_ascii_col]
        mov     si, draw_ascii_text
        call    gui_print

        pop     ds
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
.not_draw_ascii:
        ret

section .bss

draw_ascii_row:  resb 1
draw_ascii_col:  resb 1
draw_ascii_text: resb 80        ; same 80-byte convention as other
                                 ; ASCIIZ buffers in this driver

section .text
