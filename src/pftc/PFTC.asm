; PFTC.COM - Portfolio File Transfer Configuration client.
;
; Iteration 0.0: launch, connect, stay resident. Opens the smart-cable
; ports, draws a full-screen frame (title carries PFTC's own version/
; build, bottom border carries a live status line), sends HELLO with
; a retry loop, sets the status to Connected/Offline, then waits for
; Ctrl+Q to exit (both connected and offline stay up in the frame -
; offline is meant to eventually gain an F5/menu reconnect action in a
; later iteration, not just exit). No menu, no GET_NETIFS, no config
; commands yet - see the design plan (mame-tu-novy-ukol-lovely-book.md)
; for what later iterations add on top of this skeleton.
;
; PFTC uses the INT 61h AH=30h PUSH transport - the Portfolio initiates
; every exchange, unlike PFTD's host-driven int 61h hook. See
; transport.inc for the framing this relies on.

BITS 16
ORG 0x100

        jmp     start

%include "transport.inc"
%include "gui.inc"
%include "hello.inc"
%include "frame.inc"
%include "input.inc"

start:
        ; A .COM program owns its entire segment at startup - DOS
        ; int 21h AH=48h (allocate memory, used by transport.inc's
        ; per-response buffers) fails until this block is shrunk
        ; first, freeing paragraphs for later allocation calls.
        mov     bx, 0x1000              ; keep 64KB, free the rest
        mov     ah, 0x4a
        int     0x21

        ; Application-level save: PFTC's whole run is its own
        ; "dialog" from the outside - the screen this program draws on
        ; (the frame, transient dialogs on top of it) is never the
        ; user's own content, so it's saved once here and restored
        ; once at exit, on top of (not instead of) each individual
        ; dialog's own save/restore around itself while the app runs.
        call    gui_save_screen
        mov     [cs:app_screen_seg], ax

        call    draw_frame              ; frame (with PFTC's own
                                         ; version/build in the title)
                                         ; goes up before anything else
        call    hide_cursor             ; PFTC has no text entry yet -
                                         ; the default blinking DOS
                                         ; cursor has nothing to point
                                         ; at (restored in show_cursor
                                         ; before returning to DOS)

        push    cs
        pop     ds
        mov     si, status_connecting
        call    set_status              ; short: "Connecting" (no ellipsis
                                         ; needed - the "Connecting..."
                                         ; dialog above already spells
                                         ; out what's happening)

        mov     ax, 0x3002              ; AH=30h AL=2: open ports
        int     0x61

        push    cs
        pop     ds
        mov     si, msg_connecting
        call    show_message
        mov     [cs:dialog_seg], ax     ; "Connecting..." save-segment

        call    do_hello
        mov     byte [cs:hello_failed], 0
        jnc     .hello_no_carry
        mov     byte [cs:hello_failed], 1
.hello_no_carry:

        mov     ax, [cs:dialog_seg]     ; dismiss "Connecting..." before
        call    gui_restore_screen      ; the status line updates

        push    cs
        pop     ds
        cmp     byte [cs:hello_failed], 0
        jne     .offline
        cmp     byte [cs:hello_ok], 0
        je      .offline

        call    build_connected_status
        mov     si, status_buf
        call    set_status
        jmp     .wait

.offline:
        ; show_error (AH=14h) blocks until a keypress and erases
        ; itself - nothing to restore here, unlike show_message/AH=12h.
        push    cs
        pop     ds
        mov     si, msg_offline
        call    show_error

        push    cs
        pop     ds
        mov     si, status_offline
        call    set_status
        ; A later iteration adds an F5/menu reconnect action here
        ; instead of just waiting for Ctrl+Q.

.wait:
        call    wait_for_quit

.close:
        mov     ax, 0x3003              ; AH=30h AL=3: close ports
        int     0x61

        call    show_cursor             ; restore before handing the
                                         ; screen back to DOS

        mov     ax, [cs:app_screen_seg] ; restore whatever was on screen
        call    gui_restore_screen      ; before PFTC ever drew anything

        mov     ax, 0x4c00
        int     0x21

; Builds "Connected vX.Y.Z (buildid)" into status_buf from the
; server's HELLO response (hello_major/minor/patch/build_id -
; hello.inc) - the SERVER's identity, distinct from PFTC's own
; version/build shown in the frame's title (draw_frame, PFTC's own
; version.inc/build_id.inc). Worst case ("v255.255.255 (ffffffff)")
; is 33 characters, fits within the ~36 usable columns set_status
; allows.
build_connected_status:
        push    ax
        push    di
        push    si
        push    ds
        push    es

        push    cs
        pop     ds
        push    cs
        pop     es
        cld

        mov     di, status_buf
        mov     si, status_connected_prefix
        call    frame_append_si
        mov     al, [cs:hello_major]
        call    frame_append_dec8
        mov     al, '.'
        stosb
        mov     al, [cs:hello_minor]
        call    frame_append_dec8
        mov     al, '.'
        stosb
        mov     al, [cs:hello_patch]
        call    frame_append_dec8
        mov     si, status_build_prefix
        call    frame_append_si
        mov     ax, [cs:hello_build_id+2]
        call    frame_append_hex16
        mov     ax, [cs:hello_build_id]
        call    frame_append_hex16
        mov     al, ')'
        stosb
        mov     al, 0
        stosb

        pop     es
        pop     ds
        pop     si
        pop     di
        pop     ax
        ret

msg_connecting:    db "Connecting", 0, "Talking to SmartCable...", 0, 0
msg_offline:       db "SmartCable: offline", 0, "No response from bridge.", 0, 0
status_connecting: db "Connecting", 0
status_offline:    db "Offline", 0
status_connected_prefix: db "Connected v", 0
status_build_prefix:     db " (", 0
status_buf:        times 40 db 0
dialog_seg:        dw 0
app_screen_seg:    dw 0
hello_failed:      db 0
