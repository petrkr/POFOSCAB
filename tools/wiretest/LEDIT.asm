; LEDIT.asm - isolated test of int 60h AH=01h (Line Editor), per the
; PROVEN working descriptor layout in int60h01.md (PFTC SSID field test
; evidence) - NOT the earlier INT60H.md-only attempt, which misread
; ep_exit as a near offset. int60h01.md confirms ep_exit is a FAR
; pointer (4 bytes) like ep_targ/ep_tit/ep_fn/ep_udel.

BITS 16
ORG 0x100

        jmp     start

target:     times 33 db 0       ; ep_max+1 = 33 bytes, zero-terminated on entry
title:      db 'PFTC', 0, 'SSID', 0, 0
exit_keys:  dw 0x000D, 0x001B, 0x0003, 0x0000  ; Enter, Esc, Ctrl-C, terminator

editblk:
ep_targ_off:    dw target
ep_targ_seg:    dw 0            ; patched at runtime with CS (tiny model: CS=DS=SS)
ep_pos:         dw 0
ep_max:         dw 32
ep_xpos:        db 3
ep_ypos:        db 2
ep_mode:        db 2
ep_hit:         dw 0
ep_tit_off:     dw title
ep_tit_seg:     dw 0            ; patched at runtime
ep_exit_off:    dw exit_keys
ep_exit_seg:    dw 0            ; patched at runtime - FAR, per int60h01.md
ep_fn_off:      dw getkey
ep_fn_seg:      dw 0            ; patched at runtime
ep_wid:         db 34
ep_wind:        db 1            ; double line box
ep_res1:        dw 0
ep_res2:        dw 0
ep_udel_off:    dw undel
ep_udel_seg:    dw 0            ; patched at runtime

result:     dw 0
sizemsg:    db 'sz=', 0
resmsg:     db ' res=', 0

start:
        ; .COM tiny model: CS=DS=SS=ES, one segment for everything -
        ; use CS for every "far" pointer's segment half, patched here
        ; since NASM can't know the runtime segment at assemble time.
        mov     ax, cs
        mov     [ep_targ_seg], ax
        mov     [ep_tit_seg], ax
        mov     [ep_exit_seg], ax
        mov     [ep_fn_seg], ax
        mov     [ep_udel_seg], ax

        ; Clear screen, print struct size as a sanity check before
        ; risking the int 60h call.
        mov     ah, 0x06
        xor     al, al
        mov     bh, 0x07
        xor     cx, cx
        mov     dx, 0x0727
        int     0x10

        mov     ah, 0x02
        xor     bh, bh
        xor     dx, dx
        int     0x10
        mov     si, sizemsg
        call    print_si
        mov     ax, result - editblk   ; = struct size, assembled constant
        call    print_ax_hex

        ; Wait for a keypress before the risky call, so the size check
        ; above is visible even if int 60h AH=01h hangs/crashes.
        mov     ah, 0x08
        int     0x21

        mov     ah, 0x01
        mov     si, editblk
        int     0x60
        mov     [result], ax

        mov     ah, 0x02
        xor     bh, bh
        mov     dx, 0x0100
        int     0x10
        mov     si, resmsg
        call    print_si
        mov     ax, [result]
        call    print_ax_hex

        mov     ah, 0x02
        xor     bh, bh
        mov     dx, 0x0200
        int     0x10
        mov     si, target
        call    print_si

.wait_exit:
        mov     ah, 0x08
        int     0x21
        cmp     al, 0x11        ; Ctrl+Q
        jne     .wait_exit

        mov     ax, 0x4c00
        int     0x21

; ---- getkey routine: far proc, returns 16-bit keycode in AX. Uses
; BIOS int 16h AH=00h, per int60h01.md's proven-working callback (not
; DOS int 21h AH=08h). ----
getkey:
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    ds
        push    es
        mov     ah, 0x00
        int     0x16
        or      al, al
        jnz     .ascii
        mov     al, ah
        mov     ah, 0x01
        jmp     short .done
.ascii:
        xor     ah, ah
.done:
        pop     es
        pop     ds
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        retf

; ---- undelete routine: far proc, dummy ----
undel:
        retf

; ---- print_si: DS:SI = zero-terminated string, writes at current
; cursor via AH=0x09 (no auto-wrap), advances cursor manually ----
print_si:
        push    ax
        push    bx
        push    cx
        push    dx
        mov     ah, 0x03
        xor     bh, bh
        int     0x10            ; dh=row, dl=col
.loop:
        lodsb
        or      al, al
        jz      .done
        push    ax
        mov     ah, 0x09
        mov     bl, 0x07
        xor     bh, bh
        mov     cx, 1
        int     0x10
        pop     ax
        inc     dl
        mov     ah, 0x02
        xor     bh, bh
        int     0x10
        jmp     .loop
.done:
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret

; ---- print_ax_hex: prints AX as 4 hex digits at current cursor ----
print_ax_hex:
        push    ax
        push    bx
        push    cx
        push    dx
        mov     cx, 4
        mov     bx, ax
.digit:
        mov     al, 4
        push    cx
        mov     cl, al
        rol     bx, cl
        pop     cx
        mov     al, bl
        and     al, 0x0F
        cmp     al, 10
        jl      .num
        add     al, 'A' - 10 - '0'
.num:
        add     al, '0'
        push    bx
        push    cx
        mov     ah, 0x03
        xor     bh, bh
        int     0x10
        mov     ah, 0x09
        mov     bl, 0x07
        xor     bh, bh
        mov     cx, 1
        int     0x10
        inc     dl
        mov     ah, 0x02
        xor     bh, bh
        int     0x10
        pop     cx
        pop     bx
        loop    .digit
        pop     dx
        pop     cx
        pop     bx
        pop     ax
        ret
