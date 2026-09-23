; transport.inc - framed send/receive over int 61h AH=30h (the PUSH
; transport), shared by every opcode call PFTC makes. Adapted from the
; wiretest-proven pattern in tools/wiretest/WW.asm and the original
; PFTC.asm prototype's send_and_ack: transmit (AL=0) with a DL=6
; retry-via-AL=4 loop, then re-arm (AL=4) and pull back one response
; block (AL=1) sized to whatever the peer actually sends.
;
; Response buffers are allocated with DOS int 21h AH=48h and freed with
; AH=49h rather than reserved statically - PFTC is a foreground .COM,
; not a resident hook competing for a fixed footprint, so there is no
; reason to size every response buffer for the worst case up front.

TRANSPORT_MAX_RETRIES equ 10

; Largest response body this protocol defines is GET_NETIF's ~74 bytes
; (see the design plan's GET_NETIF response layout); 128 leaves headroom
; without allocating a paragraph-wasteful amount.
TRANSPORT_RECV_MAX equ 128

; DS:DX/CX = request packet to send.
; Returns: carry clear on success, with ES:DI = far pointer to a
;          freshly allocated buffer holding the response bytes and
;          CX = number of response bytes received (caller must free
;          the segment in ES via int 21h AH=49h once done with it).
;          Carry set on any transport failure (no response segment is
;          left allocated in that case).
transport_call:
        push    ax
        push    bx
        push    si

        mov     si, dx                  ; si = request buffer offset
        mov     bx, cx                  ; bx = request length
        mov     byte [cs:retries_left], TRANSPORT_MAX_RETRIES

.send:
        push    cs
        pop     ds
        mov     dx, si
        mov     cx, bx
        mov     ax, 0x3000              ; AH=30h AL=0: transmit (push)
        int     0x61
        or      dl, dl
        jz      .send_ok
        cmp     dl, 6                   ; DL=6: peer sync missed, retry
        jne     .fail
        cmp     byte [cs:retries_left], 0
        je      .fail
        dec     byte [cs:retries_left]
        mov     ax, 0x3004              ; AH=30h AL=4: re-arm
        int     0x61
        jmp     .send

.send_ok:
        ; Allocate the response buffer *before* re-arming/receiving -
        ; WW.asm's proven sequence calls AL=1 immediately after AL=4
        ; with nothing in between. TRANSPORT_RECV_MAX is generous
        ; enough for every response shape this protocol defines (see
        ; the design plan's opcode table); a real varint/negotiated
        ; size isn't needed since every response body is bounded and
        ; small.
        mov     ah, 0x48                ; DOS: allocate memory block
        mov     bx, (TRANSPORT_RECV_MAX + 15) / 16
        int     0x21
        jc      .fail
        mov     es, ax                  ; es = allocated segment (kept for the result)

        mov     bx, es                  ; stash the buffer segment across AL=4 in bx
        mov     ax, 0x3004              ; re-arm before receiving
        int     0x61
        mov     ds, bx                  ; ds:dx = the buffer, for the receive call
        xor     dx, dx
        mov     cx, TRANSPORT_RECV_MAX
        mov     ax, 0x3001              ; AH=30h AL=1: receive (pull)
        int     0x61
        push    cs
        pop     ds                      ; restore ds = cs
        or      dl, dl
        jnz     .fail_free
        xor     di, di                  ; es:di = start of response buffer
        clc
        jmp     .done

.fail_free:
        push    ax
        mov     ah, 0x49                ; DOS: free memory block
        int     0x21
        pop     ax
.fail:
        stc
.done:
        pop     si
        pop     bx
        pop     ax
        ret

retries_left: db 0
