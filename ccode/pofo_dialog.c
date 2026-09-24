#include "pofo.h"
#include <string.h>

static unsigned int pofo_dialog_top_left;
static unsigned int pofo_dialog_action;
static char *pofo_dialog_text;

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

/* text is title\0body\0\0 (same format as pofo_message_dialog/
   pofo_error_dialog). Returns the bottom_right corner of the box the
   ROM will draw at top_left: two text lines (title, body) plus a
   border row above and below, width is the longer line plus one
   border column on each side. */
unsigned int pofo_dialog_extent(top_left, text)
unsigned int top_left;
char *text;
{
    unsigned char title_len, body_len, width;

    title_len = (unsigned char)strlen(text);
    body_len = (unsigned char)strlen(text + title_len + 1);
    width = (title_len > body_len ? title_len : body_len) + 4;

    return POFO_COORD(POFO_COORD_ROW(top_left) + 3,
                       POFO_COORD_COL(top_left) + width - 1);
}
