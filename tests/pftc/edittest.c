#include <stdio.h>
#include <conio.h>
#include "pofo.h"

static char value[33] = "aaa";
static char title[] = "PFTC";
static char prompt[] = "SSID: ";
static char dialog_title[] = "";
static char dialog_text[] = "INT60 Editor";
static unsigned int exit_keys[] = { 0x000d, 0x001b, 0x0003, 0x0000 };

int main()
{
    int result;

    pofo_clear_screen();
    gotoxy(0, 0);
    printf("INT60 editor");
    fflush(stdout);

    pofo_hide_cursor();
    pofo_message_dialog(POFO_COORD(2, 2), dialog_text, dialog_title);

    while (getch() != 0x1011)
        ;

    result = pofo_line_edit(POFO_COORD(2, 3), title, prompt, value, 32, 34,
                            POFO_EDIT_MODE_CLEAR_ON_ENTRY,
                            POFO_EDIT_BOX_DOUBLE, exit_keys);

    gotoxy(0, 5);
    printf("result=%04X", (unsigned int)result);
    gotoxy(0, 6);
    printf("text=%s", value);
    fflush(stdout);

    while (getch() != 0x1011)
        ;

    return 0;
}
