#include "pofo.h"

static unsigned int pofo_screen_top_left;
static unsigned int pofo_screen_bottom_right;
static unsigned char *pofo_screen_buffer;

void pofo_clear_screen()
{
#asm
    push ax
    push bx
    push cx
    push dx
    mov ah,#$06
    xor al,al
    mov bh,#$07
    xor cx,cx
    mov dx,#$0727
    int $10
    pop dx
    pop cx
    pop bx
    pop ax
#endasm
}

void pofo_hide_cursor()
{
#asm
    push ax
    push cx
    mov ah,#$01
    mov ch,#$20
    xor cl,cl
    int $10
    pop cx
    pop ax
#endasm
}

/* INT 10h/AH=01h Set Cursor Type, explicit CX=0x0007 (full block) -
   leaving CX unset is unreliable (depends on whatever was in CX from
   earlier code; confirmed by direct test: worked when CX happened to
   hold a prior pofo_hide_cursor() value, silently failed/showed no
   cursor from a fresh, undetermined CX otherwise). Shapes other than a
   full block draw nothing on the Portfolio's LCD, so this is also the
   only shape worth setting. */
void pofo_show_cursor()
{
#asm
    push ax
    push cx
    mov ah,#$01
    mov cx,#$0007
    int $10
    pop cx
    pop ax
#endasm
}

/* Export the upstream BCC gotoxy() implementation. */
int gotoxy(x, y)
{
#asm
#if __FIRST_ARG_IN_AX__
    mov bx,sp
    mov dl,al
    mov ax,[bx+2]
    mov dh,al
#else
    mov bx,sp
    mov ax,[bx+4]
    mov dh,al
    mov ax,[bx+2]
    mov dl,al
#endif
    mov ah,#$02
    mov bx,#7
    int $10
#endasm
}

/* buffer must be at least POFO_SCREEN_SIZE(top_left, bottom_right) bytes;
   caller owns it (static or malloc'd) and is responsible for freeing it. */
void pofo_screen_save(top_left, bottom_right, buffer)
unsigned int top_left;
unsigned int bottom_right;
unsigned char *buffer;
{
    pofo_screen_top_left = top_left;
    pofo_screen_bottom_right = bottom_right;
    pofo_screen_buffer = buffer;

#asm
    mov cx,_pofo_screen_bottom_right
    mov dx,_pofo_screen_top_left
    mov ax,#$0800
    xor bx,bx
    mov si,_pofo_screen_buffer
    int $60
#endasm
}

void pofo_screen_restore(top_left, bottom_right, buffer)
unsigned int top_left;
unsigned int bottom_right;
unsigned char *buffer;
{
    pofo_screen_top_left = top_left;
    pofo_screen_bottom_right = bottom_right;
    pofo_screen_buffer = buffer;

#asm
    mov cx,_pofo_screen_bottom_right
    mov dx,_pofo_screen_top_left
    mov ax,#$0802
    xor bx,bx
    mov si,_pofo_screen_buffer
    int $60
#endasm
}
