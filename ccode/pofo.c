#include "pofo.h"
#include <malloc.h>
#include <string.h>

//  Pofo box
static unsigned char pofo_box_style;
static unsigned char pofo_box_page;
static unsigned int pofo_box_top;
static unsigned int pofo_box_bottom;

// Pofo dialogs
static unsigned int pofo_dialog_top_left;
static unsigned int pofo_dialog_action;
static char *pofo_dialog_text;

// Pofo screen
static unsigned int pofo_screen_top_left;
static unsigned int pofo_screen_bottom_right;
static unsigned int pofo_screen_buffer_size;
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

static void pofo_dialog_call(action, top_left, text)
unsigned int action;
unsigned int top_left;
char *text;
{
    pofo_dialog_action = action;
    pofo_dialog_top_left = top_left;
    pofo_dialog_text = text;

#asm
    push ax
    push bx
    push dx
    push si
    mov ax,_pofo_dialog_action
    xor bx,bx
    mov dx,_pofo_dialog_top_left
    mov si,_pofo_dialog_text
    int 0x60
    pop si
    pop dx
    pop bx
    pop ax
#endasm
}

void pofo_message_dialog(top_left, text)
unsigned int top_left;
char *text;
{
    pofo_dialog_call(0x1200, top_left, text);
}

void pofo_error_dialog(top_left, text)
unsigned int top_left;
char *text;
{
    pofo_dialog_call(0x1400, top_left, text);
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

void pofo_screen_save(top_left, bottom_right)
unsigned int top_left;
unsigned int bottom_right;
{
    unsigned char cols, rows;

    pofo_screen_top_left = top_left;
    pofo_screen_bottom_right = bottom_right;

    cols = POFO_COORD_COL(bottom_right) - POFO_COORD_COL(top_left) + 1;
    rows = POFO_COORD_ROW(bottom_right) - POFO_COORD_ROW(top_left) + 1;

    pofo_screen_buffer_size = (unsigned int)cols * rows;
    pofo_screen_buffer = (unsigned char *)malloc(pofo_screen_buffer_size);

#asm
    mov cx,_pofo_screen_bottom_right
    mov dx,_pofo_screen_top_left
    mov ax,#$0800
    xor bx,bx
    mov si,_pofo_screen_buffer
    int $60
#endasm
}

void pofo_screen_restore()
{
#asm
    mov cx,_pofo_screen_bottom_right
    mov dx,_pofo_screen_top_left
    mov ax,#$0802
    xor bx,bx
    mov si,_pofo_screen_buffer
    int $60
#endasm

    memset(pofo_screen_buffer, 0, pofo_screen_buffer_size);
    free(pofo_screen_buffer);
}