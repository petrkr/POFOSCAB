; HELONIF.COM - probe: does a second transport_call (GET_NETIF) hang when
; issued right after a first one (HELLO) on the same open smartcable
; connection? NETIFP.asm only ever sends one request per run; PFTC.asm
; sends HELLO then GET_NETIFS then GET_NETIF on one connection and hangs.
; This isolates that "two requests in a row" difference with otherwise the
; exact same transport sequence as NETIFP.asm (which is proven to work).

BITS 16
ORG 0x100

RECV_MAX    equ 2048
MAX_RETRIES equ 10

start:
        mov     ax, 0x3002
        int     0x61

        ; --- Step 1: HELLO ---
        mov     dx, hello_request
        mov     cx, hello_request_len
        call    send_and_receive
        mov     al, [cs:result_dl]
        mov     [cs:hello_dl], al
        mov     ax, [cs:response_len]
        mov     [cs:hello_len], ax

        push    cs
        pop     ds
        call    report_hello

        ; No DOS allocation between requests - avoid dynamic memory
        ; entirely instead of trying to make int 21h AH=48h/49h safe.

        ; --- Step 2: GET_NETIF, same open connection, no port close in
        ; between - mirrors PFTC.asm's do_hello -> do_get_netifs ->
        ; do_get_netif sequence (collapsed to two calls here since
        ; GET_NETIFS/GET_NETIF share the same transport_call shape).
        mov     dx, netif_request
        mov     cx, netif_request_len
        call    send_and_receive
        mov     al, [cs:result_dl]
        mov     [cs:netif_dl], al
        mov     ax, [cs:response_len]
        mov     [cs:netif_len], ax

        push    cs
        pop     ds
        call    report_netif

        mov     ax, 0x3003
        int     0x61
        mov     ax, 0x4c00
        int     0x21

; DS:DX/CX = request. Fills result_dl/response_len/response (shared
; buffer, overwritten each call - fine here since we report after each
; step before the next call clobbers it).
send_and_receive:
        push    cs
        pop     ds
        mov     byte [cs:retries_left], MAX_RETRIES
.send:
        push    cs
        pop     ds
        mov     ax, 0x3000
        int     0x61
        mov     [cs:result_dl], dl
        or      dl, dl
        jz      .receive
        cmp     dl, 6
        jne     .out
        cmp     byte [cs:retries_left], 0
        je      .out
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
.out:
        ret

; One line: H:D=<dl> C=<len>
report_hello:
        mov     al, 'H'
        call    putc
        mov     al, ':'
        call    putc
        mov     al, 'D'
        call    putc
        mov     al, '='
        call    putc
        mov     al, [cs:hello_dl]
        call    hex8
        mov     al, ' '
        call    putc
        mov     al, 'C'
        call    putc
        mov     al, '='
        call    putc
        mov     ax, [cs:hello_len]
        call    hex16
        call    newline
        ret

; One line: N:D=<dl> C=<len>
report_netif:
        mov     al, 'N'
        call    putc
        mov     al, ':'
        call    putc
        mov     al, 'D'
        call    putc
        mov     al, '='
        call    putc
        mov     al, [cs:netif_dl]
        call    hex8
        mov     al, ' '
        call    putc
        mov     al, 'C'
        call    putc
        mov     al, '='
        call    putc
        mov     ax, [cs:netif_len]
        call    hex16
        call    newline
        ret

newline:
        push    ax
        push    dx
        inc     byte [cs:cur_row]
        mov     dh, [cs:cur_row]
        mov     dl, 0
        mov     ah, 0x02
        xor     bh, bh
        int     0x10
        pop     dx
        pop     ax
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

hello_request:  db 0x01
hello_request_len equ $ - hello_request
netif_request:  db 0x03, 0x00
netif_request_len equ $ - netif_request

result_dl:      db 0
response_len:   dw 0
retries_left:   db 0
hello_dl:       db 0
hello_len:      dw 0
netif_dl:       db 0
netif_len:      dw 0
cur_row:        db 0

section .bss
response:       resb RECV_MAX
