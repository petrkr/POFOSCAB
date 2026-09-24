#include "pofo.h"

static unsigned char pofo_box_style;
static unsigned char pofo_box_page;
static unsigned int pofo_box_top;
static unsigned int pofo_box_bottom;

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
