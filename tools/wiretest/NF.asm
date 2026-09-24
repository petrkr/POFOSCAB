; NF.COM - isolates do_get_netifs (GET_NETIFS only, no GET_NETIF) to
; find whether it alone caused the NVRAM corruption/reboot seen when
; PFTC.COM ran both GET_NETIFS and GET_NETIF back to back. No frame.inc/
; gui.inc - just transport.inc + netif.inc + a minimal hex dump.

BITS 16
ORG 0x100

        jmp     start

%include "transport.inc"
%include "netif.inc"

start:
        mov     bx, 0x1000              ; shrink like PFTC.asm does
        mov     ah, 0x4a
        int     0x21

        mov     ax, 0x3002              ; AL=2: open ports
        int     0x61

        call    do_get_netifs
        mov     byte [cs:carry_flag], 0
        jnc     .no_carry
        mov     byte [cs:carry_flag], 1
.no_carry:

        mov     al, 'C'
        call    video_putc
        mov     al, '='
        call    video_putc
        mov     al, [cs:carry_flag]
        call    video_print_hex8
        mov     al, ' '
        call    video_putc

        mov     al, 'O'
        call    video_putc
        mov     al, '='
        call    video_putc
        mov     al, [cs:netifs_ok]
        call    video_print_hex8
        mov     al, ' '
        call    video_putc

        mov     al, 'N'
        call    video_putc
        mov     al, '='
        call    video_putc
        mov     al, [cs:netifs_count]
        call    video_print_hex8
        mov     al, ' '
        call    video_putc

        mov     al, 'I'
        call    video_putc
        mov     al, '='
        call    video_putc
        mov     al, [cs:netifs_interface]
        call    video_print_hex8
        mov     al, ' '
        call    video_putc

        mov     al, 'T'
        call    video_putc
        mov     al, '='
        call    video_putc
        mov     al, [cs:netifs_type]
        call    video_print_hex8

        mov     ax, 0x3003              ; AL=3: close ports
        int     0x61

        mov     ax, 0x4c00
        int     0x21

; AL = character. Write it at the cursor and advance one column.
video_putc:
        push    bx
        push    cx
        push    dx
        push    ax
        mov     ah, 0x03
        xor     bh, bh
        int     0x10
        pop     ax
        push    dx
        mov     ah, 0x09
        xor     bh, bh
        mov     bl, 0x07
        mov     cx, 1
        int     0x10
        pop     dx
        inc     dl
        mov     ah, 0x02
        xor     bh, bh
        int     0x10
        pop     dx
        pop     cx
        pop     bx
        ret

; AL = byte to print as two hexadecimal digits.
video_print_hex8:
        push    ax
        push    bx
        mov     bl, al
        mov     cl, 4
        shr     al, cl
        call    .nibble
        mov     al, bl
        and     al, 0x0f
        call    .nibble
        pop     bx
        pop     ax
        ret
.nibble:
        cmp     al, 10
        jb      .digit
        add     al, 'A' - 10 - '0'
.digit:
        add     al, '0'
        call    video_putc
        ret

carry_flag: db 0
