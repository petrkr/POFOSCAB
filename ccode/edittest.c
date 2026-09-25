#include <stdio.h>
#include <conio.h>
#include "pofo.h"

/* INT 60h/AH=01h requires this exact 35-byte layout.  Do not use a C
   struct: PCC aligns its unsigned int fields after ep_mode. */
static unsigned char pofo_edit_block[35];
static char target[33];
static char title[] = "PFTC\0SSID\0\0";
static unsigned int exit_keys[] = { 0x000d, 0x001b, 0x0003, 0x0000 };
static unsigned int pofo_edit_result;

extern pofo_edit_getkey();
extern pofo_edit_undelete();

static void pofo_edit_word(offset, value)
unsigned int offset;
unsigned int value;
{
    pofo_edit_block[offset] = (unsigned char)value;
    pofo_edit_block[offset + 1] = (unsigned char)(value >> 8);
}

/* Returns the editor's AX result.  title must be title\0prompt\0\0 and
   target must reserve max + 1 bytes for its terminating zero. */
static int pofo_line_edit(target, max, title, exit_keys, xpos, ypos,
                          mode, width, window)
char *target;
unsigned int max;
char *title;
unsigned int *exit_keys;
unsigned char xpos;
unsigned char ypos;
unsigned char mode;
unsigned char width;
unsigned char window;
{
    pofo_edit_word(0, (unsigned int)target);
    pofo_edit_word(4, 0);
    pofo_edit_word(6, max);
    pofo_edit_block[8] = xpos;
    pofo_edit_block[9] = ypos;
    pofo_edit_block[10] = mode;
    pofo_edit_word(11, 0);
    pofo_edit_word(13, (unsigned int)title);
    pofo_edit_word(17, (unsigned int)exit_keys);
    pofo_edit_word(21, (unsigned int)pofo_edit_getkey);
    pofo_edit_block[25] = width;
    pofo_edit_block[26] = window;
    pofo_edit_word(27, 0);
    pofo_edit_word(29, 0);
    pofo_edit_word(31, (unsigned int)pofo_edit_undelete);

#asm
    push bx
    mov ax,ds
    mov _pofo_edit_block+2,ax
    mov _pofo_edit_block+15,ax
    mov _pofo_edit_block+19,ax
    mov bx,cs
    mov _pofo_edit_block+23,bx
    mov _pofo_edit_block+33,bx
    mov si,#_pofo_edit_block
    mov ax,#0x0100
    int 0x60
    mov _pofo_edit_result,ax
    pop bx
#endasm

    return (int)pofo_edit_result;
}

int main()
{
    int result;

    pofo_clear_screen();
    gotoxy(0, 0);
    printf("INT60 editor");
    fflush(stdout);

    result = pofo_line_edit(target, 32, title, exit_keys, 3, 2, 2, 34,
                            POFO_BOX_DOUBLE);

    gotoxy(0, 5);
    printf("result=%04X", (unsigned int)result);
    gotoxy(0, 6);
    printf("text=%s", target);
    fflush(stdout);

    while (getch() != 0x1011)
        ;

    return 0;
}

/* Far callback required by INT 60h/AH=01h.  The ROM expects normal ASCII
   in AL and extended BIOS keys as AH=1, AL=scancode. */
#asm
_pofo_edit_getkey:
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov ah,#0
    int 0x16
    or al,al
    jnz pofo_edit_ascii
    mov al,ah
    mov ah,#1
    jmp pofo_edit_done
pofo_edit_ascii:
    xor ah,ah
pofo_edit_done:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

_pofo_edit_undelete:
    retf
#endasm
