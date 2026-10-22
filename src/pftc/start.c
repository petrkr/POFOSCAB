/* Minimal .COM startup, replacing libdos's crt0 + msdos startup (which
   also dragged in its exit handlers, errno and the heap module). Must
   be the first object on the link line: DOS enters a .COM at offset
   0x100, i.e. the first byte of text.

   Same memory layout as the libdos startup it replaces: the linker
   puts the data segment right after the text (__segoff paragraphs
   from CS), DS = ES = SS point there, the stack runs down from the top
   of that segment (capped at 64K, or at the top of the memory DOS gave
   us, PSP:2), and the BSS (__edata..__end) is zeroed. main() returning
   terminates the program with exit code 0.

   The label name matters: ld86 links by reachability from its entry
   symbol - "_start" when something defines it, "_main" otherwise -
   and silently drops every object nothing reachable refers to, even
   one named on the link line. Defining _start here makes this object
   the root (and it is laid out first because it is linked first). */

#asm
    .text
    export _start
_start:
    mov bx,[2]
    mov dx,cs
    add dx,#__segoff
    sub bx,dx
    cmp bx,#$1000
    jbe start_stack_fits
    mov bx,#$1000
start_stack_fits:
    mov cl,#4
    shl bx,cl
    dec bx
    dec bx
    mov ds,dx
    mov es,dx
    mov ss,dx
    mov sp,bx
    mov di,#__edata
    mov cx,#__end
    sub cx,di
    xor ax,ax
    cld
    rep
    stosb
    call _main
    mov ax,#$4C00
    int $21
#endasm
