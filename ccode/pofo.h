#ifndef POFO_H
#define POFO_H

/* Pack a row and column into the register layout used by INT 60h/AH=09h. */
#define POFO_COORD(row, col) \
    ((((unsigned int)(row) & 0xff) << 8) | ((unsigned int)(col) & 0xff))

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

#endif
