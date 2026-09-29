#ifndef POFO_H
#define POFO_H

/* Generic split of a packed 16-bit value into its two bytes - several
   INT 60h calls pack unrelated things into AH/AL or CH/CL (a screen
   row+col, a menu's top_line+selected item, ...); this is the one
   macro pair for reading either byte back out, whatever it means. */
#define POFO_HIGH_BYTE(value) ((unsigned char)((value) >> 8))
#define POFO_LOW_BYTE(value) ((unsigned char)((value) & 0xff))

/* Pack a row and column into the register layout used by INT 60h/AH=09h. */
#define POFO_COORD(row, col) \
    ((((unsigned int)(row) & 0xff) << 8) | ((unsigned int)(col) & 0xff))
#define POFO_COORD_ROW(coord) POFO_HIGH_BYTE(coord)
#define POFO_COORD_COL(coord) POFO_LOW_BYTE(coord)

/* Bytes needed for a pofo_screen_save/restore buffer covering this
   inclusive top_left..bottom_right range (1 byte per cell). */
#define POFO_SCREEN_SIZE(top_left, bottom_right) \
    ((unsigned int)(POFO_COORD_COL(bottom_right) - POFO_COORD_COL(top_left) + 1) * \
     (unsigned int)(POFO_COORD_ROW(bottom_right) - POFO_COORD_ROW(top_left) + 1))

#define POFO_BOX_SINGLE 0
#define POFO_BOX_DOUBLE 1

#define POFO_EDIT_NO_BOX 0xff
#define POFO_EDIT_BOX_SINGLE 0
#define POFO_EDIT_BOX_DOUBLE 1
#define POFO_EDIT_MODE_KEEP_ON_ENTRY 0
#define POFO_EDIT_MODE_CLEAR_ON_ENTRY 2
#define POFO_EDIT_TEXT_TOO_LONG -2
#define POFO_NO_MEMORY -1

void pofo_clear_screen();
void pofo_hide_cursor();
/* INT 10h/AH=01h Set Cursor Type - sets an explicit full-block shape
   (CX=0x0007); leaving CX unset is unreliable (confirmed by direct
   test - depends on whatever was in CX from earlier code). */
void pofo_show_cursor();
/* INT 60h/AH=09h Draw Box, single line on page 0. */
void pofo_draw_box();
/* INT 60h/AH=09h Draw Box with an explicit style and video page. */
void pofo_draw_box_ex();

/* text/title are zero-terminated strings; top_left uses POFO_COORD().
   Returns 0, or POFO_NO_MEMORY. */
int pofo_message_dialog();
/* No title - text is "Line one\nLine two" or a plain sentence.
   Self-erasing on keypress, no save/restore needed. Returns 0, or
   POFO_NO_MEMORY. */
int pofo_error_dialog();
/* Bottom_right (POFO_COORD()) of the box pofo_message_dialog would draw
   - use with pofo_screen_save/restore before showing it. */
unsigned int pofo_dialog_extent();

/* top_left/bottom_right use POFO_COORD(), inclusive. buffer is caller-owned
   (static or malloc'd, at least POFO_SCREEN_SIZE(top_left, bottom_right)
   bytes) and must be passed to both calls; freeing it (if malloc'd) is the
   caller's responsibility once it's no longer needed. */
void pofo_screen_save();
void pofo_screen_restore();

/* text is title\0item1\0...\0\0; top_left uses POFO_COORD(). defaults may
   be 0 (no defaults text). Returns bytes needed for a pofo_screen_save
   buffer covering the menu; *bottom_right_out gets the menu's
   bottom_right (POFO_COORD()). No hierarchy/nesting logic - this is one
   menu, one call; see ccode/pftc.c for the submenu stack built on top. */
unsigned int pofo_menu_getsize();
/* type_depth: AL bits 0-2 = POFO_BOX_SINGLE/POFO_BOX_DOUBLE, bits 3-7 =
   visible-height limit in rows including borders (0 = no limit; always
   set one on the native 40x8 display, see INT60H.md). Blocks until ESC
   or an item is picked; returns -1 on ESC, else POFO_COORD(top_line,
   selected_item). Leaves the menu box on screen (single-line) afterward
   - caller must pofo_screen_save/restore around this, same as
   pofo_message_dialog. */
int pofo_menu_show();

/* INT 60h/AH=01h. top_left uses POFO_COORD(); title/prompt must be non-NULL;
   use "" for an empty value. value needs max + 1 B and is both input default
   and output. Its default is retained only with mode 0; mode 2 clears it on
   entry. Returns the exit key AX, or POFO_EDIT_TEXT_TOO_LONG if title +
   prompt exceeds 253 bytes. */
int pofo_line_edit();

#endif
