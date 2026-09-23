; WIREREAD.COM - continuous receive-server probe for AH=30h AL=1.
;
; Opens the Smart Cable ports once, then loops: AL=4 wait 500ms, AL=1
; Receive block. Only successful receives (DL=0) are printed, as
; "DL=00 CX=xxxx <hex bytes>" on their own line - other DL codes
; (e.g. DL=6, no data) are silently ignored and the loop just retries.
; If the first received byte is 0x80 (bridge/ESP hello request),
; immediately replies with a fake PFD1 hello payload via AL=0 so the
; peer's hello() succeeds instead of timing out. Pressing 's' sends an
; unsolicited push packet (AL=0 transmit) with no preceding receive
; request from the peer - tests the Portfolio-initiated push direction
; rather than the pull/request-response direction the rest of the
; loop exercises. Runs until Ctrl-C, then closes ports and exits.

BITS 16
ORG 0x100

start:
        ; Clear screen (scroll whole page up, AL=0) so old ROM menu
        ; text doesn't obscure the DL=/CX= output written at row 0.
        mov     ax, 0x0600
        mov     bh, 0x07
        xor     cx, cx
        mov     dx, 0x184f              ; DH=24,DL=79 (bottom-right)
        int     0x10
        mov     ax, 0x0200
        xor     bh, bh
        xor     dx, dx
        int     0x10                    ; cursor to (0,0)

        ; Diagnostic: print a restart counter (stored in our own data
        ; segment, not shared low memory) to tell apart "jumped back to
        ; start:" within one run (counter keeps climbing) from "DOS
        ; re-executed the program" (counter always shows 0001, since
        ; this is a fresh load each time).
        push    cs
        pop     ds
        inc     word [restart_count]
        mov     al, 'R'
        call    video_putc
        mov     al, '='
        call    video_putc
        mov     ax, [restart_count]
        call    video_print_hex16
        mov     al, ' '
        call    video_putc

        ; AL=2: Open ports
        mov     ax, 0x3002
        int     0x61

        ; AL=4: wait 500ms, once, before entering the receive loop.
        ; AL=4 was found to reprogram the 8255 PPI mode control word
        ; (MAME log: "I8255 Mode Control Word: 92" etc.), not just
        ; sleep - calling it every iteration re-primes the port mid
        ; handshake and corrupts the peer's sync byte. AL=1 alone did
        ; not return at all without an initial AL=4 having run first.
        mov     ax, 0x3004
        int     0x61

        mov     byte [cs:row], 0

.loop:
        push    cs
        pop     ds

        ; Key check (non-blocking): Ctrl-C (0x03) quits; 's'/'S' sends
        ; an unsolicited push packet (AL=0 transmit) with no preceding
        ; receive request from the peer.
        mov     ah, 0x01
        int     0x16
        jz      .no_key
        xor     ah, ah
        int     0x16
        cmp     al, 0x03
        je      .done
        cmp     al, 's'
        je      .push_send
        cmp     al, 'S'
        je      .push_send
        jmp     .no_key
.push_send:
        push    cs
        pop     ds
        mov     dx, push_payload
        mov     cx, push_payload_len
        mov     ax, 0x3000              ; AL=0: transmit block
        int     0x61
        mov     [cs:result], dl
        mov     word [cs:rx_count], 0
        call    print_push_line
.no_key:

        push    cs
        pop     ds
        mov     dx, receive_buffer
        mov     cx, receive_buffer_size
        mov     ax, 0x3001              ; AL=1: receive block
        int     0x61
        mov     [cs:result], dl
        cmp     cx, receive_buffer_size
        jbe     .cx_ok
        mov     cx, receive_buffer_size
.cx_ok:
        mov     [cs:rx_count], cx

        cmp     byte [cs:result], 0
        jne     .loop                   ; error/no data (e.g. DL=6): ignore, don't print

        ; Reply to a hello (first byte 0x80) with a fake PFD1 payload.
        cmp     word [cs:rx_count], 0
        je      .print_line
        cmp     byte [cs:receive_buffer], 0x80
        jne     .print_line
        push    cs
        pop     ds
        mov     dx, pfd1_reply
        mov     cx, pfd1_reply_len
        mov     ax, 0x3000              ; AL=0: transmit block
        int     0x61

.print_line:
        call    print_result_line
        jmp     .loop

.done:
        ; AL=3: Close ports
        mov     ax, 0x3003
        int     0x61

        mov     ax, 0x4c00
        int     0x21

; Print one diagnostic line for an unsolicited push send: "PUSH DL=xx"
; and advance to the next screen row.
print_push_line:
        push    cs
        pop     ds
        mov     dh, [cs:row]
        mov     dl, 0
        mov     ah, 0x02
        xor     bh, bh
        int     0x10                    ; position cursor at start of row

        mov     al, 'P'
        call    video_putc
        mov     al, 'U'
        call    video_putc
        mov     al, 'S'
        call    video_putc
        mov     al, 'H'
        call    video_putc
        mov     al, ' '
        call    video_putc
        mov     al, 'D'
        call    video_putc
        mov     al, 'L'
        call    video_putc
        mov     al, '='
        call    video_putc
        mov     al, [cs:result]
        call    video_print_hex8

        inc     byte [cs:row]
        cmp     byte [cs:row], 24
        jb      .row_ok
        mov     byte [cs:row], 0
.row_ok:
        ret

; Print one diagnostic line: "DL=xx CX=xxxx <hex bytes...>" and advance
; to the next screen row (wraps to row 0 after row 24).
print_result_line:
        push    cs
        pop     ds
        mov     dh, [cs:row]
        mov     dl, 0
        mov     ah, 0x02
        xor     bh, bh
        int     0x10                    ; position cursor at start of row

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
        mov     al, 'C'
        call    video_putc
        mov     al, 'X'
        call    video_putc
        mov     al, '='
        call    video_putc
        mov     ax, [cs:rx_count]
        call    video_print_hex16

        mov     al, ' '
        call    video_putc

        push    cs
        pop     ds
        mov     si, receive_buffer
        mov     cx, [cs:rx_count]
        cmp     cx, 16
        jbe     .dump_len_ok
        mov     cx, 16
.dump_len_ok:
        or      cx, cx
        jz      .advance_row
.dump_loop:
        push    cx
        lodsb
        call    video_print_hex8
        mov     al, ' '
        call    video_putc
        pop     cx
        loop    .dump_loop

.advance_row:
        inc     byte [cs:row]
        cmp     byte [cs:row], 24
        jb      .row_ok
        mov     byte [cs:row], 0
.row_ok:
        ret

; AL = character.  Write it at the cursor and move one column right.
video_putc:
        push    bx
        push    cx
        push    dx
        push    ax
        mov     ah, 0x03
        xor     bh, bh
        int     0x10                    ; DH=row, DL=column
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

; AL = byte to print as two hex digits.
; NOTE: shr reg, imm (other than 1) is an 80186+ opcode - the
; Portfolio's 8088-class CPU does not have it, so the shift count must
; go through CL (shr al, cl), not "shr al, 4".
video_print_hex8:
        push    ax
        push    bx
        mov     bl, al          ; save original byte (bx survives video_putc)
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

; AX = word to print as four hex digits.
video_print_hex16:
        push    ax
        mov     al, ah
        call    video_print_hex8
        pop     ax
        call    video_print_hex8
        ret

restart_count:       dw      0
result:              db      0
rx_count:             dw      0
row:                  db      0
; Fake PFD1 hello reply: magic "PFD1", buildId=0 (uint32 LE), version
; 0.0.1, plus one pad byte to satisfy the 12-byte minimum.
pfd1_reply:           db      "PFD1", 0, 0, 0, 0, 0, 0, 1, 0
pfd1_reply_len        equ     $ - pfd1_reply
; Unsolicited push payload, sent on 's' key press.
push_payload:         db      "WIREPUSH", 0
push_payload_len      equ     $ - push_payload
receive_buffer_size   equ     256
receive_buffer:       times receive_buffer_size db 0
