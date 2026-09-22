; copy.inc - COPY (0x8C) command: copy a file on the Portfolio, source to
; destination, via DOS handle-based I/O (int 0x21 AH=0x3D/0x3C/0x3F/0x40/
; 0x3E). Files only - no directory recursion (DOS's own COPY command
; doesn't recurse either; that's a file-manager feature, not a single DOS
; API call).
;
; Unlike RENAME (rename.inc, AH=0x56 - a directory-entry rewrite), this
; works cross-drive: it performs a real read/write data copy, so there is
; no "same device" requirement. This is the fix RENAME's own header
; comment names as missing ("that would need a copy+delete sequence at
; the client level, not a single DOS call").
;
; Command byte 0x8C continues the 0x80+ PFTD command space - see hello.inc's
; header comment for why that space is safe.
;
; Request layout: payload[0]=0x8C, payload[1..2] unused/reserved (0x00,
; 0x70 - mirrors the ROM's own convention), payload[3..] TWO consecutive
; ASCIIZ strings: source path, then destination path (each NUL-terminated,
; destination immediately following source's NUL) - identical shape to
; RENAME's request, same extraction loop reused as-is.
;
; Response layout (fixed 2 bytes), identical shape to mkdir.inc/delete.inc/
; rmdir.inc/rename.inc's:
;   offset 0  status  (0x20 = ok, 0x10 = error)
;   offset 1  errcode (0 on success; on failure: 1=not found, 3=disk full,
;                       4=access denied, 0xFF=critical error fired - see
;                       PROTOCOL.md's response status/errcode convention
;                       section, shared with mkdir.inc/delete.inc/
;                       rmdir.inc/rename.inc. errcode 2 "already exists"
;                       is never produced here - COPY always overwrites an
;                       existing destination, same as DOS's own COPY.)
;
; Destination is always overwritten if it exists (AH=0x3C truncates on
; create - no pre-check needed). The destination's DOS attribute byte is
; set to match the source's (read via Find First before opening it, same
; DTA layout list.inc's dispatch_list already reads - dta_buf+0x15).
;
; If the read/write loop fails partway (disk full, media pulled, critical
; error), both handles are closed and the partially-written destination
; file is deleted (AH=0x41 Unlink, best effort) before responding - the
; client never sees a corrupt "success" artifact sitting on disk.
;
; AH=0x40 (Write) gotcha with no analogue anywhere else in this codebase:
; a short write (AX returned < CX requested) signals disk-full via CF=0,
; NOT via CF=1 - every existing command only ever checks CF because each
; does one fixed-size int 0x21 call, never a byte-count-sensitive
; streaming loop. This dispatcher checks AX against CX after every write,
; independent of CF, and treats a mismatch as disk full (errcode 3).
;
; Same reentrancy/critical_error_flag/DS discipline as mkdir.inc/
; delete.inc/rmdir.inc/rename.inc - not repeated in detail here. Unlike
; those, this dispatcher makes multiple int 0x21 calls per invocation
; (Find First, Open, Create, N x Read, N x Write, Close x2, maybe Unlink)
; - critical_error_flag is cleared before EACH call that can plausibly
; raise int 0x24 (no/write-protected media), not just once at the top.
;
; DOS 2.x/DIP DOS's actual extended error codes reachable from
; AH=0x3D/0x3C/0x3F/0x40 are unverified on real hardware - this repo has
; no prior usage of these functions to compare against (unlike AH=0x39/
; 0x41/0x3A/0x56, each individually confirmed for MKDIR/DELETE/RMDIR/
; parts of RENAME). Treat the mapping below as a best-effort RBIL-derived
; guess until tested - see PROTOCOL.md's "Adding a new command" checklist.

COPY_CMD equ 0x8C
COPY_BUFSIZE equ 512

; dispatch_copy: called from pftd_int61_handler with AL = payload[0] and
; CS=DS. Uses list_src_ds/list_src_si to pull both paths out of the
; foreign buffer, same as dispatch_rename - but copies data, not just a
; directory entry, so the actual work happens in a read/write loop below
; the path extraction instead of a single int 0x21 call.
dispatch_copy:
        cmp     al, COPY_CMD
        jne     .not_copy

        push    ax
        push    bx
        push    cx
        push    dx
        push    si
        push    di
        push    es
        push    ds

        ; Copy both ASCIIZ paths out of the foreign buffer - identical
        ; shape to rename.inc's .copy_old/.copy_new loop.
        push    cs
        pop     es
        mov     ax, [cs:list_src_ds]
        mov     ds, ax
        mov     si, [cs:list_src_si]
        add     si, 3
        mov     di, copy_src_path
.copy_src:
        mov     al, [si]
        mov     [es:di], al
        inc     si
        inc     di
        cmp     al, 0
        jne     .copy_src

        mov     di, copy_dst_path
.copy_dst:
        mov     al, [si]
        mov     [es:di], al
        inc     si
        inc     di
        cmp     al, 0
        jne     .copy_dst

        push    cs
        pop     ds              ; DS=CS from here on - nothing below this
                                 ; line ever uses the foreign segment again

        ; Read the source's DOS attribute byte via Find First, so the
        ; destination can be created with matching attributes. Uses the
        ; same dta_buf/DTA layout as list.inc's dispatch_list
        ; (dta_buf+0x15 = attribute) - dta_buf is list.inc's buffer,
        ; reused here since dispatch_list and dispatch_copy never run
        ; concurrently (both are called sequentially from the same
        ; int 0x61 hook).
        mov     dx, dta_buf
        mov     ah, 0x1A        ; Set DTA
        int     0x21

        mov     dx, copy_src_path
        mov     cx, 0x16        ; attribute mask: include subdirectories
                                 ; + normal files, same mask list.inc uses
        mov     ah, 0x4E        ; Find First
        int     0x21
        jc      .err_src_notfound

        mov     al, [dta_buf+0x15]
        mov     [copy_src_attr], al

        ; Progress bar setup: total size (low word only - real-world
        ; Portfolio storage/transfer sizes fit comfortably, and losing
        ; precision above 64K only blurs the displayed percentage, it
        ; never affects the actual copy loop below) and reset the
        ; running byte counter. Deliberately does NOT call
        ; ensure_gui_drawn here - HELLO already guarantees the border
        ; exists before any real client sends a second command (see
        ; hello.inc), and calling it again here, mid-DOS-call-sequence
        ; (between Find First above and Open File below), hung the
        ; whole transfer on real hardware - its int 0x10 AH=0x06 full-
        ; screen clear apparently doesn't coexist safely with DOS
        ; mid-file-op state the way it does when called standalone
        ; from dispatch_hello. gui_progress alone (below) is safe here
        ; since it never touches AH=0x06, only AH=0x02/0x09.
        mov     ax, [dta_buf+0x1A]
        mov     [copy_total_size], ax
        mov     word [copy_bytes_done], 0

        ; Open source, read-only.
        mov     byte [cs:critical_error_flag], 0
        mov     dx, copy_src_path
        mov     al, 0           ; AL=0 = read-only access
        mov     ah, 0x3D        ; Open File
        int     0x21
        jc      .open_src_failed
        cmp     byte [cs:critical_error_flag], 0
        jne     .open_src_critical

        mov     [copy_src_handle], ax

        ; Create destination (truncates if it already exists), attribute
        ; byte matching the source.
        mov     byte [cs:critical_error_flag], 0
        mov     dx, copy_dst_path
        xor     cx, cx
        mov     cl, [copy_src_attr]
        mov     ah, 0x3C        ; Create File
        int     0x21
        jc      .create_dst_failed
        cmp     byte [cs:critical_error_flag], 0
        jne     .create_dst_critical

        mov     [copy_dst_handle], ax

.copy_loop:
        mov     byte [cs:critical_error_flag], 0
        mov     bx, [copy_src_handle]
        mov     cx, COPY_BUFSIZE
        mov     dx, copy_buf
        mov     ah, 0x3F        ; Read File
        int     0x21
        jc      .read_failed
        cmp     byte [cs:critical_error_flag], 0
        jne     .read_critical

        cmp     ax, 0
        je      .copy_done      ; AX=0 -> EOF, nothing more to read

        mov     cx, ax          ; cx = bytes actually read = bytes to write
        mov     byte [cs:critical_error_flag], 0
        mov     bx, [copy_dst_handle]
        mov     dx, copy_buf
        mov     ah, 0x40        ; Write File
        int     0x21
        jc      .write_failed
        cmp     byte [cs:critical_error_flag], 0
        jne     .write_critical

        cmp     ax, cx          ; short write (AX < CX) = disk full,
        jne     .disk_full      ; signalled via CF=0, not CF=1

        ; Progress update: bytes_done += cx (bytes just written),
        ; percent = bytes_done * 100 / total_size (16-bit, same
        ; low-word-only precision tradeoff as the size capture above).
        ; total_size=0 (empty file, or captured size overflowed 64K)
        ; is skipped - .copy_done below still fires normally either
        ; way, this only affects the live bar.
        add     [copy_bytes_done], cx
        cmp     word [copy_total_size], 0
        je      .copy_loop
        mov     ax, [copy_bytes_done]
        mov     dx, 100
        mul     dx              ; dx:ax = bytes_done * 100
        div     word [copy_total_size]  ; ax = percent
        call    gui_progress

        jmp     .copy_loop

.copy_done:
        ; success - close both handles, respond ok
        mov     bx, [copy_src_handle]
        mov     ah, 0x3E        ; Close File
        int     0x21
        mov     bx, [copy_dst_handle]
        mov     ah, 0x3E
        int     0x21

        mov     byte [copy_response], 0x20
        mov     byte [copy_response+1], 0
        jmp     .respond

.err_src_notfound:
        mov     byte [copy_response], 0x10
        mov     byte [copy_response+1], 1
        jmp     .respond

.open_src_critical:
        mov     byte [copy_response], 0x10
        mov     byte [copy_response+1], 0xFF
        jmp     .respond

.open_src_failed:
        ; AX = DOS extended error code from AH=0x3D.
        mov     byte [copy_response], 0x10
        cmp     ax, 2                    ; file not found
        je      .err_src_dos_notfound
        cmp     ax, 3                    ; path not found
        je      .err_src_dos_notfound
        mov     byte [copy_response+1], 4   ; access denied / too many
        jmp     .respond                     ; open files / invalid code
.err_src_dos_notfound:
        mov     byte [copy_response+1], 1
        jmp     .respond

.create_dst_critical:
        mov     bx, [copy_src_handle]
        mov     ah, 0x3E
        int     0x21
        mov     byte [copy_response], 0x10
        mov     byte [copy_response+1], 0xFF
        jmp     .respond

.create_dst_failed:
        ; AX = DOS extended error code from AH=0x3C. Close the source
        ; handle before responding - nothing was created on the
        ; destination side to clean up.
        push    ax
        mov     bx, [copy_src_handle]
        mov     ah, 0x3E
        int     0x21
        pop     ax

        mov     byte [copy_response], 0x10
        cmp     ax, 3                    ; path not found
        je      .err_dst_notfound
        mov     byte [copy_response+1], 4   ; access denied (read-only
        jmp     .respond                     ; dest, write-protected, etc.)
.err_dst_notfound:
        mov     byte [copy_response+1], 1
        jmp     .respond

.read_critical:
        mov     byte [copy_response+1], 0xFF
        jmp     .cleanup_fail

.read_failed:
        ; AX = DOS extended error code from AH=0x3F.
        mov     byte [copy_response+1], 4   ; access denied / invalid
        jmp     .cleanup_fail                ; handle

.write_critical:
        mov     byte [copy_response+1], 0xFF
        jmp     .cleanup_fail

.write_failed:
        mov     byte [copy_response+1], 4   ; access denied / invalid
        jmp     .cleanup_fail                ; handle

.disk_full:
        mov     byte [copy_response+1], 3
        ; fall through to .cleanup_fail

.cleanup_fail:
        ; Close both handles (best effort - ignore any error, there is
        ; nothing more useful to do if a close itself fails here), then
        ; delete the partially-written destination so the client never
        ; sees a corrupt "success" artifact.
        mov     byte [copy_response], 0x10
        mov     bx, [copy_src_handle]
        mov     ah, 0x3E
        int     0x21
        mov     bx, [copy_dst_handle]
        mov     ah, 0x3E
        int     0x21
        mov     dx, copy_dst_path
        mov     ah, 0x41        ; Unlink (best effort)
        int     0x21

.respond:
        mov     dx, copy_response
        mov     cx, 2
        mov     ax, 0x3000      ; AH=0x30 AL=0 = transmit block
        int     0x61

        pop     ds
        pop     es
        pop     di
        pop     si
        pop     dx
        pop     cx
        pop     bx
        pop     ax
.not_copy:
        ret

section .bss

copy_response:   resb 2             ; status(1B) + errcode(1B)
copy_src_path:   resb 80            ; same 80-byte convention as every
copy_dst_path:   resb 80            ; other path buffer in this driver
copy_buf:        resb COPY_BUFSIZE
copy_src_handle: resw 1             ; spilled to memory, not kept in a
copy_dst_handle: resw 1             ; register - bx is reused per
                                     ; read/write call in the loop
copy_src_attr:   resb 1             ; attribute byte read via Find First,
                                     ; passed as CX to AH=0x3C so the
                                     ; destination's attributes match
copy_total_size: resw 1             ; source file size (low word only,
                                     ; see the progress-bar comment
                                     ; above), for the gui_progress
                                     ; percent calculation in .copy_loop
copy_bytes_done: resw 1             ; running total of bytes written so
                                     ; far this copy, same width/caveat

section .text
