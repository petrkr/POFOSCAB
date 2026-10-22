#include "pofo.h"
#include "tinylib.h"

/* Longest title\0text\0\0 segment list a dialog can take. */
#define POFO_DIALOG_TEXT_MAX 96

static unsigned int pofo_dialog_top_left;
static unsigned int pofo_dialog_action;
static char pofo_dialog_text[POFO_DIALOG_TEXT_MAX];

/* Shared body of both dialogs. Builds the ROM's segment list in
   pofo_dialog_text - title\0 only when a title is given (AH=12h always
   needs one, even empty; AH=14h has no title concept, confirmed by
   direct test), then the text with '\n' turned into the \0 line
   separator ('\n' itself isn't a line break to the ROM), then the
   final \0 - and runs the INT 60h call. Returns 0, or POFO_NO_MEMORY
   if the texts don't fit. */
static int pofo_dialog_show(action, top_left, title, text)
unsigned int action;
unsigned int top_left;
char *title;
char *text;
{
    char *p;
    char c;
    unsigned int length;

    length = str_len(text) + 3;
    if (title != 0)
        length += str_len(title);
    if (length > POFO_DIALOG_TEXT_MAX)
        return POFO_NO_MEMORY;

    p = pofo_dialog_text;
    if (title != 0)
        p = str_copy(p, title) + 1;
    do {
        c = *text++;
        *p++ = (c == '\n') ? 0 : c;
    } while (c != 0);
    *p = 0;

    pofo_dialog_action = action;
    pofo_dialog_top_left = top_left;

#asm
    push si
    push di
    mov ax,_pofo_dialog_action
    xor bx,bx
    mov cx,#1
    mov dx,_pofo_dialog_top_left
    mov si,#_pofo_dialog_text
    int 0x60
    pop di
    pop si
#endasm

    return 0;
}

int pofo_message_dialog(top_left, text, title)
unsigned int top_left;
char *text;
char *title;
{
    return pofo_dialog_show(0x1200, top_left, title, text);
}

int pofo_error_dialog(top_left, text)
unsigned int top_left;
char *text;
{
    return pofo_dialog_show(0x1400, top_left, 0, text);
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
    unsigned int title_len, body_len, width;

    title_len = str_len(title);
    body_len = str_len(text);
    width = (title_len > body_len ? title_len : body_len) + 4;

    return POFO_COORD(POFO_COORD_ROW(top_left) + 3,
                       POFO_COORD_COL(top_left) + width - 1);
}
