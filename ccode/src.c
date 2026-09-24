#include <stdio.h>
#include <conio.h>
#include "pofo.h"
#include "smartcable.h"

static unsigned char src_screen_buffer[POFO_SCREEN_SIZE(POFO_COORD(0, 0), POFO_COORD(3, 3))];

int main()
{
    pofo_screen_save(POFO_COORD(0, 0), POFO_COORD(3, 3), src_screen_buffer);
    gotoxy(2, 2);
    printf("SRC");
    fflush(stdout);

    smartcable_wait_500ms();
    smartcable_wait_500ms();

    printf("SRC");
    fflush(stdout);
    smartcable_wait_500ms();
    smartcable_wait_500ms();
    pofo_screen_restore(POFO_COORD(0, 0), POFO_COORD(3, 3), src_screen_buffer);

    return 0;
}
