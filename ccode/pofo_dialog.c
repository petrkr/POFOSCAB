#include "pofo.h"
#include <stdlib.h>
#include <string.h>

static unsigned int pofo_dialog_top_left;
static unsigned int pofo_dialog_action;
static char *pofo_dialog_text;

static void pofo_dialog_build(buffer, text, title)
char *buffer;
char *text;
char *title;
{
    unsigned int title_length;
    unsigned int text_length;

    title_length = strlen(title);
    text_length = strlen(text);
    memcpy(buffer, title, title_length);
    buffer[title_length] = 0;
    memcpy(buffer + title_length + 1, text, text_length);
    buffer[title_length + text_length + 1] = 0;
    buffer[title_length + text_length + 2] = 0;
}

static int pofo_dialog_call(action, top_left, text, title)
unsigned int action;
unsigned int top_left;
char *text;
char *title;
{
    unsigned int buffer_size;

    buffer_size = strlen(title) + strlen(text) + 3;
    pofo_dialog_text = malloc(buffer_size);
    if (pofo_dialog_text == 0)
        return POFO_NO_MEMORY;

    pofo_dialog_action = action;
    pofo_dialog_top_left = top_left;
    pofo_dialog_build(pofo_dialog_text, text, title);

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

    free(pofo_dialog_text);
    pofo_dialog_text = 0;
    return 0;
}

int pofo_message_dialog(top_left, text, title)
unsigned int top_left;
char *text;
char *title;
{
    return pofo_dialog_call(0x1200, top_left, text, title);
}

int pofo_error_dialog(top_left, text, title)
unsigned int top_left;
char *text;
char *title;
{
    return pofo_dialog_call(0x1400, top_left, text, title);
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
