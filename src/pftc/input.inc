; input.inc - keyboard input helpers. Only a global Ctrl+Q exit wait
; for now (no menu navigation yet - see the design plan for the menu
; keys ESC/etc. planned for later iterations, once there's a menu to
; navigate).

; Ctrl+Q = ASCII 0x11 (DC1) - standard control-key encoding (Ctrl
; clears bits 5-6 of the letter's code: 'Q' 0x51 & 0x1F = 0x11).

; Blocks until Ctrl+Q is pressed, discarding every other key. Uses
; int 21h AH=08h (character input without echo) rather than AH=01h
; (which echoes to the screen - would corrupt the frame drawn by
; draw_frame).
wait_for_quit:
        push    ax
.loop:
        mov     ah, 0x08
        int     0x21
        cmp     al, 0x11
        jne     .loop
        pop     ax
        ret
