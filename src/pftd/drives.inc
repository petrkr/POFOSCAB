; drives.inc - DRIVES (0x87) command: how many logical drives DOS
; knows about (A=1, B=2, ...)
;
; Request layout: payload[0]=0x87, nothing else needed (same as HELLO).
;
; Response layout (fixed 1 byte):
;   offset 0  drive count (number of logical drives DOS knows about,
;             1..N maps to A:..Nth letter)
;

DRIVES_CMD equ 0x87

; dispatch_drives: called from pftd_int61_handler with AL = payload[0]
; (no extra payload needed, same as dispatch_hello). Preserves
; AX/CX/DX/DS - see dispatch_hello's header comment for why (the
; caller's .chain forwards these straight to old61/the real ROM).
dispatch_drives:
        cmp     al, DRIVES_CMD
        jne     .not_drives

        push    ax
        push    cx
        push    dx
        push    ds

        push    cs
        pop     ds

        mov     ah, 0x19                ; Get Current Default Drive
        int     0x21
        mov     dl, al
        mov     ah, 0x0E                ; Select Default Drive - DL is
        int     0x21                    ; the drive we just read, so
                                         ; this doesn't actually change
                                         ; anything, only returns the
                                         ; count in AL as a side effect
        mov     [drives_response], al

        mov     dx, drives_response
        mov     cx, 1
        mov     ax, 0x3000              ; AH=0x30 AL=0 = transmit block
        int     0x61

        pop     ds
        pop     dx
        pop     cx
        pop     ax
.not_drives:
        ret

section .bss

drives_response: resb 1

section .text
