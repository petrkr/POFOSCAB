; hello.inc - PFTC opcode 0x01 HELLO: builds the request, sends it via
; transport_call, and parses the 14-byte response (magic/status/error
; code/build id/version). See the design plan's "HELLO response
; (14 bytes)" section for the exact byte layout this mirrors.

%include "build_id.inc"
%include "version.inc"

PFTC_HELLO equ 0x01

hello_request: db PFTC_HELLO
hello_request_len equ $ - hello_request

; Result of the last do_hello call.
hello_ok:      db 0            ; non-zero on success
hello_major:   db 0
hello_minor:   db 0
hello_patch:   db 0

; Sends HELLO and parses the response.
; Returns: carry clear and hello_ok<>0 on a well-formed "PFC1" ack,
;          carry set (transport failure) or hello_ok=0 (bad/short
;          reply, or peer-reported error) otherwise. hello_major/
;          minor/patch are only valid when hello_ok<>0.
do_hello:
        push    ax
        push    cx
        push    si
        push    di
        push    es

        mov     byte [cs:hello_ok], 0

        push    cs
        pop     ds
        mov     dx, hello_request
        mov     cx, hello_request_len
        call    transport_call
        jc      .done                   ; carry propagates to caller

        cmp     cx, 14
        jb      .bad

        mov     al, [es:di]             ; status
        cmp     al, 0x20
        jne     .bad
        mov     al, [es:di+2]           ; magic "PFC1"
        cmp     al, 'P'
        jne     .bad
        mov     al, [es:di+3]
        cmp     al, 'F'
        jne     .bad
        mov     al, [es:di+4]
        cmp     al, 'C'
        jne     .bad
        mov     al, [es:di+5]
        cmp     al, '1'
        jne     .bad

        mov     al, [es:di+10]
        mov     [cs:hello_major], al
        mov     al, [es:di+11]
        mov     [cs:hello_minor], al
        mov     al, [es:di+12]
        mov     [cs:hello_patch], al
        mov     byte [cs:hello_ok], 1

.bad:
        mov     ah, 0x49                ; DOS: free memory block
        int     0x21
        clc
.done:
        pop     es
        pop     di
        pop     si
        pop     cx
        pop     ax
        ret
