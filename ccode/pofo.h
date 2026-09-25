#ifndef POFO_H
#define POFO_H

/* Pack a row and column into the register layout used by INT 60h/AH=09h. */
#define POFO_COORD(row, col) \
    ((((unsigned int)(row) & 0xff) << 8) | ((unsigned int)(col) & 0xff))
#define POFO_COORD_ROW(coord) ((unsigned char)((coord) >> 8))
#define POFO_COORD_COL(coord) ((unsigned char)((coord) & 0xff))

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

void pofo_clear_screen();
void pofo_hide_cursor();
/* INT 60h/AH=09h Draw Box, single line on page 0. */
void pofo_draw_box();
/* INT 60h/AH=09h Draw Box with an explicit style and video page. */
void pofo_draw_box_ex();

/* title/text are separate zero-terminated strings; top_left uses
   POFO_COORD(). */
void pofo_message_dialog();
void pofo_error_dialog();
/* Returns bottom_right (POFO_COORD()) of the box a message/error dialog
   with this top_left/title/text would draw - use with pofo_screen_save/restore
   to snapshot exactly the area the dialog covers before showing it. */
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
