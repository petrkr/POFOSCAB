#include "pofo.h"

static unsigned char pofo_box_style;
static unsigned char pofo_box_page;
static unsigned int pofo_box_top;
static unsigned int pofo_box_bottom;

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

void pofo_draw_box(top, bottom)
unsigned int top;
unsigned int bottom;
{
    pofo_draw_box_ex(top, bottom, POFO_BOX_SINGLE, 0);
}

void pofo_draw_box_ex(top, bottom, style, page)
unsigned int top;
unsigned int bottom;
unsigned int style;
unsigned int page;
{
    pofo_box_style = (unsigned char)style;
    pofo_box_page = (unsigned char)page;
    pofo_box_top = top;
    pofo_box_bottom = bottom;

#asm
    push ax
    push bx
    push cx
    push dx
    mov ah,#9
    mov al,_pofo_box_style
    mov bh,_pofo_box_page
    mov cx,_pofo_box_bottom
    mov dx,_pofo_box_top
    int 0x60
    pop dx
    pop cx
    pop bx
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
