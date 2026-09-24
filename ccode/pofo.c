#include "pofo.h"
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
static unsigned char *pofo_screen_buffer;

// Pofo menu
static unsigned int pofo_menu_top_left;
static unsigned int pofo_menu_bottom_right;
static char *pofo_menu_text;
static char *pofo_menu_defaults;
static unsigned int pofo_menu_bytes;
static unsigned int pofo_menu_show_ax;
static unsigned int pofo_menu_show_cx;
static unsigned char pofo_menu_type_depth;

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

/* INT 60h/AH=10h Box Area Calculation. text is title\0item1\0...\0\0;
   defaults may be 0 (no defaults text). Returns bytes needed for a
   pofo_screen_save buffer covering this menu; *bottom_right_out gets
   the menu's bottom_right (POFO_COORD()). Caller must still call
   pofo_screen_save/restore around pofo_menu_show - the ROM does not
   auto-restore the screen underneath (see INT60H.md). */
unsigned int pofo_menu_getsize(top_left, text, defaults, bottom_right_out)
unsigned int top_left;
char *text;
char *defaults;
unsigned int *bottom_right_out;
{
    pofo_menu_top_left = top_left;
    pofo_menu_text = text;
    pofo_menu_defaults = defaults;

#asm
    mov ax,#0x1000
    xor bx,bx
    mov dx,_pofo_menu_top_left
    mov si,_pofo_menu_text
    mov di,_pofo_menu_defaults
    or di,di
    jnz .getsize_have_defaults
    mov di,#0xFFFF
.getsize_have_defaults:
    mov es,di
    int 0x60
    mov _pofo_menu_bytes,bx
    mov _pofo_menu_bottom_right,cx
#endasm

    *bottom_right_out = pofo_menu_bottom_right;
    return pofo_menu_bytes;
}

/* INT 60h/AH=0Fh Menu Handling. text/defaults as in pofo_menu_getsize.
   type_depth: AL bits 0-2 = box type (POFO_BOX_SINGLE/POFO_BOX_DOUBLE),
   bits 3-7 = visible-height limit in rows including borders (0 = no
   limit - overflows past the display on the native 40x8 mode, see
   INT60H.md; always set an explicit limit there). Blocks until ESC or
   an item is picked; returns -1 on ESC, else POFO_COORD(top_line,
   selected_item). Leaves the menu box on screen afterward (single-line)
   - caller is responsible for save/restore and for any submenu/nesting
   flow (the ROM has none of its own). */
int pofo_menu_show(top_left, text, defaults, top_line, selected, type_depth)
unsigned int top_left;
char *text;
char *defaults;
unsigned char top_line;
unsigned char selected;
unsigned char type_depth;
{
    pofo_menu_top_left = top_left;
    pofo_menu_text = text;
    pofo_menu_defaults = defaults;
    pofo_menu_show_cx = ((unsigned int)top_line << 8) | selected;
    pofo_menu_type_depth = type_depth;

#asm
    mov al,_pofo_menu_type_depth
    mov ah,#0x0F
    xor bx,bx
    mov cx,_pofo_menu_show_cx
    mov dx,_pofo_menu_top_left
    mov si,_pofo_menu_text
    mov di,_pofo_menu_defaults
    or di,di
    jnz .show_have_defaults
    mov di,#0xFFFF
.show_have_defaults:
    mov es,di
    int 0x60
    mov _pofo_menu_show_ax,ax
#endasm

    return (int)pofo_menu_show_ax;
}