; pofodetect.inc - check whether we're actually running on an Atari
; Portfolio.
;
; Technique: read I/O port 0x61 and check that it echoes back 0x61.
;
; is_pofo: returns with ZF set (use "je") if running on a real Portfolio,
; ZF clear otherwise. Preserves AX.
is_pofo:
        push    ax
        in      al, 0x61
        cmp     al, 0x61
        pop     ax
        ret
