; hello.inc - HELLO (0x80) command: presence/capability discovery response
; and its dispatch inside the int 0x61 hook.
;
; Command space: this driver only ever reacts to payload[0] >= 0x80, a
; block far outside the ROM's own dispatch range ([2,6], with 4 unused),
; so there is no collision with anything the ROM itself understands now or
; could plausibly understand in a future revision.
;
;   0x80  HELLO     -> replies with the 12-byte hello block described below
;   0x86  LIST_EXT  -> see list.inc
;   0x87  DRIVES    -> see drives.inc
;   0x88  MKDIR     -> see mkdir.inc
;   0x89  DELETE    -> see delete.inc
;   0x8A  RMDIR     -> see rmdir.inc
;   0x8B  RENAME    -> see rename.inc
;   0x8C  COPY      -> see copy.inc
;   0x8D  GETDATETIME -> see datetime.inc
;   0x8E  SETDATETIME -> see datetime.inc
;   0x8F  DRAW_ASCII  -> see drawascii.inc (debug/diagnostic tool, not
;                        a file-transfer command - see that file's
;                        header for why it's kept in the command set
;                        anyway)
;   0x81-0x85 (reserved for future subcommands)
;
; HELLO response layout (fixed 12 bytes):
;   offset 0-3  magic "PFD1"
;   offset 4-7  build id (4 bytes, binary). VERSION comes from
;               version.inc, BUILD_ID from build_id.inc (kept separate
;               since CI regenerates the latter wholesale per build -
;               see build_id.inc's header).
;   offset 8    version   (1 = this format; 0xFF = extended - offset 9 is
;                          then an extended version number and everything
;                          from there on is redefined per that version,
;                          capabilities included)
;   offset 9    capabilities bitmask - see CAP_* constants below
;   offset 10-11 reserved (0x00, 0x00)
;
; Capabilities bitmask (offset 9): defined here (not in the .inc file of
; the command each bit describes) since this is the one place a client
; can discover them all at once, before knowing which other commands
; exist.
;
; Grouped by category, not one bit per command: this protocol has never
; been released (no version has ever shipped with only some of these
; bits set - every command that exists has always shipped together, as
; a development convenience for the web client to hook into during
; incremental buildout, not a real "partial support" signal), so there
; is no compatibility reason to keep 1-bit-per-command granularity, and
; every bit spent that way is one that can never be reclaimed once a
; version genuinely ships. Group by category instead, leaving room for
; unrelated future command families to get their own bit without
; exhausting this single byte (see hello.inc's own "extended version"
; escape at offset 8 for what happens if even that isn't enough).
;
;   bit 0  CAP_CORE     - LIST extended/DRIVES/MKDIR/DELETE/RMDIR/
;                         RENAME/COPY (0x86-0x8C) all supported - the
;                         complete file-op command set that exists as
;                         of this writing, see list.inc/drives.inc/
;                         mkdir.inc/delete.inc/rmdir.inc/rename.inc/
;                         copy.inc
;   bit 1  CAP_DATETIME - GETDATETIME/SETDATETIME (0x8D/0x8E) supported,
;                         see datetime.inc

%include "version.inc"
%include "build_id.inc"

HELLO_CMD equ 0x80
CAP_CORE     equ 0x01
CAP_DATETIME equ 0x02

hello_response:
        db      'PFD1'          ; magic
        dd      BUILD_ID        ; build id / dev snapshot marker
        db      VERSION
        db      CAP_CORE | CAP_DATETIME ; capabilities - see list.inc/drives.inc/mkdir.inc/delete.inc/rmdir.inc/rename.inc/copy.inc/datetime.inc
        db      0x00, 0x00      ; reserved
hello_response_len equ $ - hello_response

; dispatch_hello: called from pftd_int61_handler with AL = payload[0],
; CS=DS. Returns (ret) either way - caller must treat this as a normal
; subroutine call, never fall into it via %include placement, or the
; missing ret unbalances the stack on every HELLO hit. Preserves
; AX/CX/DX/DS - pftd_int61_handler's .chain forwards whatever's in those
; registers straight to old61 (the real ROM on real hardware), so this
; must not leak the transmit call's own DX=hello_response/CX=
; hello_response_len into that forwarded call. (Found via TESTHELLO in
; DOSBox: a stray second "transmit" appeared downstream, made of leftover
; register bytes from here rather than a real second call - see
; dosbox-test/ - the caller wasn't clobbering CX/DX either, so both sides
; needed the same discipline.) Answers over the smart cable using the
; same AH=0x30 API the ROM itself uses - see PFTD.asm header comment for
; why this is safe to do from inside the int 0x61 hook.
dispatch_hello:
        cmp     al, HELLO_CMD
        jne     .not_hello

        push    ax
        push    cx
        push    dx
        push    ds

        ; HELLO is the first command any well-behaved client sends, so
        ; it's the natural place to guarantee the GUI border exists -
        ; but ensure_gui_drawn is idempotent and every command that
        ; wants to show status calls it too (see gui.inc), so this is
        ; a convenience for the common case, not something other
        ; dispatch_* handlers may rely on having already happened.
        call    ensure_gui_drawn

        push    cs
        pop     ds
        mov     dx, hello_response
        mov     cx, hello_response_len
        mov     ax, 0x3000              ; AH=0x30 AL=0 = transmit block
        int     0x61
        ; DL = error code on return (ignored here)

        pop     ds
        pop     dx
        pop     cx
        pop     ax
.not_hello:
        ret
