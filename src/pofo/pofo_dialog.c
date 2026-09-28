#include "pofo.h"
#include <stdlib.h>
#include <string.h>

static unsigned int pofo_dialog_top_left;
static unsigned int pofo_dialog_action;
static char *pofo_dialog_text;

/* AH=12h always needs both segments - an empty title still needs its
   own \0, unlike AH=14h below (confirmed by direct test). */
int pofo_message_dialog(top_left, text, title)
unsigned int top_left;
char *text;
char *title;
{
    unsigned int title_length;
    unsigned int text_length;
    unsigned int buffer_size;

    title_length = strlen(title);
    text_length = strlen(text);
    buffer_size = title_length + text_length + 3;
    pofo_dialog_text = malloc(buffer_size);
    if (pofo_dialog_text == 0)
        return POFO_NO_MEMORY;

    memcpy(pofo_dialog_text, title, title_length);
    pofo_dialog_text[title_length] = 0;
    memcpy(pofo_dialog_text + title_length + 1, text, text_length);
    pofo_dialog_text[title_length + text_length + 1] = 0;
    pofo_dialog_text[title_length + text_length + 2] = 0;

    pofo_dialog_action = 0x1200;
    pofo_dialog_top_left = top_left;

#asm
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax,_pofo_dialog_action
    xor bx,bx
    mov cx,#1
    mov dx,_pofo_dialog_top_left
    mov si,_pofo_dialog_text
    int 0x60
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
#endasm

    free(pofo_dialog_text);
    pofo_dialog_text = 0;
    return 0;
}

/* AH=14h has no title concept, just 1-2 \0-separated lines; '\n' isn't
   a line break to the ROM (confirmed by direct test), so translate it
   to \0 here - callers just write "Line one\nLine two". */
int pofo_error_dialog(top_left, text)
unsigned int top_left;
char *text;
{
    unsigned int text_length;
    unsigned int i;

    text_length = strlen(text);
    pofo_dialog_text = malloc(text_length + 2);
    if (pofo_dialog_text == 0)
        return POFO_NO_MEMORY;

    for (i = 0; i < text_length; i++)
        pofo_dialog_text[i] = (text[i] == '\n') ? 0 : text[i];
    pofo_dialog_text[text_length] = 0;
    pofo_dialog_text[text_length + 1] = 0;

    pofo_dialog_action = 0x1400;
    pofo_dialog_top_left = top_left;

#asm
    push ax
    push bx
    push cx
    push dx
    push si
    mov ax,_pofo_dialog_action
    xor bx,bx
    mov cx,#1
    mov dx,_pofo_dialog_top_left
    mov si,_pofo_dialog_text
    int 0x60
    pop si
    pop dx
    pop cx
    pop bx
    pop ax
#endasm

    free(pofo_dialog_text);
    pofo_dialog_text = 0;
    return 0;
}

/* Returns the bottom_right corner of the box the
   ROM will draw at top_left: two text lines (title, body) plus a
   border row above and below, width is the longer line plus one
   border column on each side. */
unsigned int pofo_dialog_extent(top_left, text, title)
unsigned int top_left;
char *text;
char *title;
{
    unsigned char title_len, body_len, width;

    title_len = (unsigned char)strlen(title);
    body_len = (unsigned char)strlen(text);
    width = (title_len > body_len ? title_len : body_len) + 4;

    return POFO_COORD(POFO_COORD_ROW(top_left) + 3,
                       POFO_COORD_COL(top_left) + width - 1);
}
