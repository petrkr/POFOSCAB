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

void pofo_clear_screen();
void pofo_hide_cursor();
/* INT 60h/AH=09h Draw Box, single line on page 0. */
void pofo_draw_box();
/* INT 60h/AH=09h Draw Box with an explicit style and video page. */
void pofo_draw_box_ex();

/* text is title\0body\0\0; top_left uses POFO_COORD(). */
void pofo_message_dialog();
void pofo_error_dialog();
/* Returns bottom_right (POFO_COORD()) of the box a message/error dialog
   with this top_left/text would draw - use with pofo_screen_save/restore
   to snapshot exactly the area the dialog covers before showing it. */
unsigned int pofo_dialog_extent();

/* top_left/bottom_right use POFO_COORD(), inclusive. buffer is caller-owned
   (static or malloc'd, at least POFO_SCREEN_SIZE(top_left, bottom_right)
   bytes) and must be passed to both calls; freeing it (if malloc'd) is the
   caller's responsibility once it's no longer needed. */
void pofo_screen_save();
void pofo_screen_restore();

#endif
