; common.inc - buffers shared by more than one dispatch_* command.
;
; Each of these was originally private to one command's .inc file but
; got reused from another (dta_buf lived in list.inc, borrowed by
; copy.inc's Find First call; list_src_ds/list_src_si lived directly in
; PFTD.asm) - both were then silently depended on by a second file with
; nothing here to say so. That's a trap: change the "owning" file's
; buffer without noticing the second user, and the second command
; breaks in a way nothing catches until it's tested on real hardware.
; Anything used by more than one dispatch_* routine belongs here
; instead, with an explicit list of who uses it, so a future change
; can grep this file's callers before touching the layout.
;
; All entries are declared uninitialized (see the "Resident memory
; footprint" note below for why that matters) - none of these need a
; specific starting value, since every dispatch_* that reads one always
; writes it first.

section .bss

; dta_buf: DOS Find First/Find Next DTA (Disk Transfer Area) record,
; set via int 0x21 AH=0x1A before AH=0x4E/0x4F. Used by:
;   - list.inc's dispatch_list (directory listing, full DTA fields)
;   - copy.inc's dispatch_copy (reads dta_buf+0x15, the attribute byte
;     only, to match the destination's attributes to the source's)
; Safe to share: dispatch_list and dispatch_copy are both CALLed
; sequentially from the same int 0x61 hook (pftd_int61_handler) and
; never run concurrently - see PFTD.asm.
dta_buf: resb 43

; list_src_ds/list_src_si: copy of the foreign receive buffer's DS:DX,
; taken by pftd_int61_handler at the same time payload0 is read (see
; PFTD.asm), so any dispatch_* needing more than just payload[0] out of
; that buffer can pull it from here. Despite the "list_" name (kept for
; historical reasons - this existed before any command besides LIST
; needed it), it is generic and used by:
;   - list.inc's dispatch_list (ASCIIZ search pattern)
;   - mkdir.inc/delete.inc/rmdir.inc's dispatch_mkdir/dispatch_delete/
;     dispatch_rmdir (single ASCIIZ path each)
;   - rename.inc/copy.inc's dispatch_rename/dispatch_copy (two
;     consecutive ASCIIZ paths each)
list_src_ds: resw 1
list_src_si: resw 1

; Resident memory footprint note (applies to every *.inc's own
; section .bss, not just this file): NASM's -f bin output format does
; not write .bss contents to the assembled .COM file - only .text/.data
; bytes land on disk. Buffers that only ever need to start at some
; known-safe value written by the code itself (never a specific
; hardcoded constant) belong in .bss: same resident RAM footprint after
; PFTD.asm's AH=0x31 TSR install (DOS still zeroes-on-allocate at
; install time in practice, and every reader here writes before
; reading anyway), but zero bytes added to the .COM file on disk -
; meaningful on a Portfolio, where drive C: is a RAM disk and an
; uninitialized buffer would otherwise be stored twice: once as zero
; bytes in the file, once again in resident RAM. Buffers that DO need a
; specific starting value physically present in the file (e.g.
; PFTD.asm's `pending db 0`, which pftd_int61_handler trusts as a real
; zero before any receive-block handshake has happened) must stay in
; .text/.data, not .bss - do not move those without checking why they
; were initialized in the first place.
