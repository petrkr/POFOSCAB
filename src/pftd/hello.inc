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
;   offset 4-7  build id (4 bytes, binary), from build_id.inc (kept
;               separate from the version below since CI regenerates
;               it wholesale on every build, tag or not - see
;               build_id.inc's header).
;   offset 8    version major (0-255). A breaking protocol/format
;                             change bumps this, same as any other
;                             breaking software change - there is no
;                             separate "response format version"
;                             distinct from the software version (see
;                             version.inc).
;   offset 9    version minor (0-255)
;   offset 10   version patch (0-255)
;   offset 11   reserved (0x00)
;
; Version major/minor/patch (offset 8-10) is a human-facing "vX.Y.Z"
; software version - see version.inc's own header. 0.0.0 means "not a
; tagged release" (an ordinary dev/master build); there is no separate
; snapshot/pre-release flag - a client that cares about that
; distinction can tell a real release from a dev build by checking for
; 0.0.0, and GitHub itself is the source of truth for which builds are
; "real" (Releases) vs. not (workflow artifacts).
;
; No capabilities bitmask: this protocol has never been released (no
; version has ever shipped with only some commands supported - every
; command that exists has always shipped together, as a development
; convenience for the web client to hook into during incremental
; buildout, not a real "partial support" signal), so there is nothing
; for a bitmask to distinguish yet. What a given PFTD build supports is
; simply whatever this file's own command list (above) plus each
; command's .inc file documents for that version - a per-command
; capability bit would only earn its keep once some real build
; actually ships without the full set, which hasn't happened.

%include "build_id.inc"
%include "version.inc"

HELLO_CMD equ 0x80

hello_response:
        db      'PFD1'          ; magic
        dd      BUILD_ID        ; build id / dev snapshot marker
        db      VERSION_MAJOR
        db      VERSION_MINOR
        db      VERSION_PATCH
        db      0x00            ; reserved
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
