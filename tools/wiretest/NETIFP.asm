; NETIFP.COM - static-buffer GET_NETIF parser probe.
; Uses the IF.asm transport sequence exactly: no DOS call occurs after AL=0.

BITS 16
ORG 0x100

RECV_MAX    equ 2048
MAX_RETRIES equ 10

start:
        mov     ax, 0x3002
        int     0x61
        mov     byte [cs:retries_left], MAX_RETRIES

.send:
        push    cs
        pop     ds
        mov     dx, request
        mov     cx, request_len
        mov     ax, 0x3000
        int     0x61
        mov     [cs:result_dl], dl
        or      dl, dl
        jz      .receive
        cmp     dl, 6
        jne     .show
        cmp     byte [cs:retries_left], 0
        je      .show
        dec     byte [cs:retries_left]
        mov     ax, 0x3004
        int     0x61
        jmp     .send

.receive:
        mov     ax, 0x3004
        int     0x61
        push    cs
        pop     ds
        mov     dx, response
        mov     cx, RECV_MAX
        mov     ax, 0x3001
        int     0x61
        mov     [cs:result_dl], dl
        cmp     cx, RECV_MAX
        jbe     .count_ok
        mov     cx, RECV_MAX
.count_ok:
        mov     [cs:response_len], cx
        or      dl, dl
        jnz     .show
        call    parse

.show:
        push    cs
        pop     ds
        call    report
        mov     ax, 0x3003
        int     0x61
        mov     ax, 0x4c00
        int     0x21

; Parse the mock's GET_NETIF response into static fields.
parse:
        mov     byte [cs:parse_ok], 0
        mov     cx, [cs:response_len]
        cmp     cx, 21                  ; status/error + common header + SSID length
        jb      .done
        cmp     byte [cs:response], 0x20
        jne     .done
        cmp     byte [cs:response+3], 0x01
        jne     .done
        mov     al, [cs:response+3]
        mov     [cs:netif_type], al
        mov     al, [cs:response+19]
        cmp     al, 32
        jbe     .len_ok
        mov     al, 32
.len_ok:
        mov     [cs:ssid_len], al
        xor     ah, ah
        mov     bx, ax
        add     bx, 22                  ; bytes through RSSI
        cmp     cx, bx
        jb      .done
        push    cs
        pop     ds
        push    cs
        pop     es
        mov     si, response+20
        mov     di, ssid
        mov     cx, ax
        cld
        rep     movsb
        mov     byte [cs:ssid+32], 0
        mov     byte [cs:parse_ok], 1
.done:
        ret

; One compact diagnostic line: D=<DL> C=<CX> P=<ok> T=<type> S=<ssid>
report:
        mov     al, 'D'
        call    putc
        mov     al, '='
        call    putc
        mov     al, [cs:result_dl]
        call    hex8
        mov     al, ' '
        call    putc
        mov     al, 'C'
        call    putc
        mov     al, '='
        call    putc
        mov     ax, [cs:response_len]
        call    hex16
        mov     al, ' '
        call    putc
        mov     al, 'P'
        call    putc
        mov     al, '='
        call    putc
        mov     al, [cs:parse_ok]
        call    hex8
        cmp     byte [cs:parse_ok], 0
        je      .done
        mov     al, ' '
        call    putc
        mov     al, 'T'
        call    putc
        mov     al, '='
        call    putc
        mov     al, [cs:netif_type]
        call    hex8
        mov     al, ' '
        call    putc
        mov     al, 'S'
        call    putc
        mov     al, '='
        call    putc
        mov     si, ssid
.ssid:
        lodsb
        or      al, al
        jz      .done
        call    putc
        jmp     .ssid
.done:
        ret

; AL = character.
putc:
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

; AL = byte.
hex8:
        push    ax
        push    bx
        push    cx
        mov     bl, al
        mov     cl, 4
        shr     al, cl
        call    .nibble
        mov     al, bl
        and     al, 0x0f
        call    .nibble
        pop     cx
        pop     bx
        pop     ax
        ret
.nibble:
        cmp     al, 10
        jb      .digit
        add     al, 'A' - 10 - '0'
.digit:
        add     al, '0'
        call    putc
        ret

; AX = word.
hex16:
        push    ax
        mov     al, ah
        call    hex8
        pop     ax
        call    hex8
        ret

request:        db 0x03, 0x00
request_len     equ $ - request
result_dl:      db 0
response_len:   dw 0
retries_left:   db 0
parse_ok:       db 0
netif_type:     db 0
ssid_len:       db 0
ssid:           times 33 db 0

; Shared response capacity used for every foreground request.  NASM's
; flat-binary BSS reserves runtime memory without adding 2048 zero bytes to
; NETIFP.COM itself.
section .bss
response:       resb RECV_MAX
