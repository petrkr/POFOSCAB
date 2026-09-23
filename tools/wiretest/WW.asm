; WIRETEST.COM - standalone probe for int 0x61 AH=0x30 (File Transfer
; services). Calls AL=2 (Open ports), then AL=0 (Transmit block) with a
; small fixed payload, then AL=3 (Close ports), then exits.
;
; Purpose: observe, via MAME's i8255 VERBOSE log, whether opening ports
; for transmit reconfigures the 8255 Mode Control Word (0x807B) compared
; to the idle/Server-mode baseline (which is Mode Control Word 0x92,
; Port A=input, Port C=output, set once at boot and never rewritten).
;
; Calling convention taken from src/pftd/hello.inc's dispatch_hello:
;   DS:DX = buffer, CX = length, AX = 0x3000 (AH=30h AL=0, transmit).

BITS 16
ORG 0x100

start:
        mov     word [cs:rx_count], 0

        ; AL=2: Open ports
        mov     ax, 0x3002
        int     0x61

.transmit:
        ; AL=0: Transmit block - send test_data
        inc     byte [cs:attempt_count]
        push    cs
        pop     ds
        mov     dx, test_data
        mov     cx, test_data_len
        mov     ax, 0x3000
        int     0x61
        mov     [cs:result], dl
        or      dl, dl
        jz      .receive_response
        cmp     dl, 6
        jne     .print_result
        cmp     byte [cs:retries_left], 0
        je      .print_result
        dec     byte [cs:retries_left]

        ; DL=6 means the peer sync was not caught.  Re-prime the
        ; Portfolio's cable service, wait 500 ms, then try again.
        mov     ax, 0x3004
        int     0x61
        jmp     .transmit

.receive_response:
        ; AL=4: re-prime the cable receive service before AL=1.  The
        ; WIREREAD probe established that AL=1 alone need not enter its
        ; sync loop without this call.
        mov     ax, 0x3004
        int     0x61

        ; AL=1: receive the bridge's application-level response.
        push    cs
        pop     ds
        mov     dx, receive_buffer
        mov     cx, receive_buffer_size
        mov     ax, 0x3001
        int     0x61
        mov     [cs:result], dl
        cmp     cx, receive_buffer_size
        jbe     .rx_count_ok
        mov     cx, receive_buffer_size
.rx_count_ok:
        mov     [cs:rx_count], cx
        call    print_receive_result
        jmp     .close_ports

.print_result:
        call    print_result

.close_ports:
        ; AL=3: Close ports
        mov     ax, 0x3003
        int     0x61

        ; Exit to DOS
        mov     ax, 0x4c00
        int     0x21

test_data:      db      "WIRETEST", 0
test_data_len   equ     $ - test_data

; Print "DL=xx N=xx" at the current video cursor.  The Portfolio CPU is
; 8088-compatible: video_print_hex8 therefore shifts through CL.
print_result:
        mov     al, 'D'
        call    video_putc
        mov     al, 'L'
        call    video_putc
        mov     al, '='
        call    video_putc
        mov     al, [cs:result]
        call    video_print_hex8
        mov     al, ' '
        call    video_putc
        mov     al, 'N'
        call    video_putc
        mov     al, '='
        call    video_putc
        mov     al, [cs:attempt_count]
        call    video_print_hex8
        ret

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

; Print the AL=1 result and up to 16 response bytes as " RX=xx ...".
print_receive_result:
        call    print_result
        cmp     byte [cs:result], 0
        jne     .done
        mov     al, ' '
        call    video_putc
        mov     al, 'R'
        call    video_putc
        mov     al, 'X'
        call    video_putc
        mov     al, '='
        call    video_putc

        push    cs
        pop     ds
        mov     si, receive_buffer
        mov     cx, [cs:rx_count]
        or      cx, cx
        jz      .done
.loop:
        push    cx
        lodsb
        call    video_print_hex8
        mov     al, ' '
        call    video_putc
        pop     cx
        loop    .loop
.done:
        ret

result:         db      0
retries_left:   db      10
attempt_count:  db      0
rx_count:       dw      0
receive_buffer_size equ 16
receive_buffer: times receive_buffer_size db 0
