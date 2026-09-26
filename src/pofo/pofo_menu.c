#include "pofo.h"

static unsigned int pofo_menu_top_left;
static unsigned int pofo_menu_bottom_right;
static char *pofo_menu_text;
static char *pofo_menu_defaults;
static unsigned int pofo_menu_bytes;
static unsigned int pofo_menu_show_ax;
static unsigned int pofo_menu_show_cx;
static unsigned char pofo_menu_type_depth;

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
