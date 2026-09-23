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
        call    set_status

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

        mov     si, status_connected
        call    set_status
        jmp     .wait

.offline:
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

msg_connecting:    db "Connecting", 0, "Talking to SmartCable...", 0, 0
status_connecting: db "Status: Connecting...", 0
status_connected:  db "Status: Connected", 0
status_offline:    db "Status: Offline", 0
dialog_seg:        dw 0
app_screen_seg:    dw 0
hello_failed:      db 0
