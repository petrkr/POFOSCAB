#include <stdio.h>
#include <conio.h>
#include <string.h>
#include "pofo.h"

static char menu_title[] = "Sample menu";
static char *menu_items[] = { "Item1", "Item2", "Item3" };
#define MENU_ITEM_COUNT 3

static char submenu_title[] = "Submenu";
static char *submenu_items[] = { "SubA", "SubB" };
#define SUBMENU_ITEM_COUNT 2

static char menu_blob[128];
static char submenu_blob[64];
static unsigned char menu_save_buffer[128];
static unsigned char submenu_save_buffer[128];

static unsigned int menu_build(buffer, buffer_size, title, items, count)
char *buffer;
unsigned int buffer_size;
char *title;
char **items;
unsigned char count;
{
    unsigned int pos;
    unsigned char i;
    unsigned int len;

    pos = 0;

    len = strlen(title);
    memcpy(buffer + pos, title, len + 1);
    pos += len + 1;

    for (i = 0; i < count; i++) {
        len = strlen(items[i]);
        memcpy(buffer + pos, items[i], len + 1);
        pos += len + 1;
    }

    buffer[pos] = 0;
    pos += 1;

    return pos;
}

int main()
{
    unsigned int menu_bottom_right;
    unsigned int submenu_bottom_right;
    unsigned int menu_bytes;
    unsigned int submenu_bytes;
    int result;
    int subresult;

    pofo_clear_screen();

    menu_build(menu_blob, sizeof(menu_blob), menu_title,
               menu_items, MENU_ITEM_COUNT);
    menu_build(submenu_blob, sizeof(submenu_blob), submenu_title,
               submenu_items, SUBMENU_ITEM_COUNT);

    menu_bytes = pofo_menu_getsize(POFO_COORD(2, 2), menu_blob, 0,
                                    &menu_bottom_right);
    gotoxy(0, 0);
    printf("menu by=%u br=%04X", menu_bytes, menu_bottom_right);
    fflush(stdout);

    while (getch() != 0x1011)
        ;

    pofo_screen_save(POFO_COORD(2, 2), menu_bottom_right, menu_save_buffer);
    result = pofo_menu_show(POFO_COORD(2, 2), menu_blob, 0, 0, 0,
                             POFO_BOX_DOUBLE);

    gotoxy(0, 1);
    printf("result=%04X", (unsigned int)result);
    fflush(stdout);

    if (result != -1) {
        submenu_bytes = pofo_menu_getsize(POFO_COORD(4, 18), submenu_blob, 0,
                                           &submenu_bottom_right);
        pofo_screen_save(POFO_COORD(4, 18), submenu_bottom_right,
                          submenu_save_buffer);
        subresult = pofo_menu_show(POFO_COORD(4, 18), submenu_blob, 0, 0, 0,
                                    POFO_BOX_DOUBLE);

        gotoxy(0, 2);
        printf("subresult=%04X", (unsigned int)subresult);
        fflush(stdout);

        while (getch() != 0x1011)
            ;

        pofo_screen_restore(POFO_COORD(4, 18), submenu_bottom_right,
                             submenu_save_buffer);
    }

    pofo_screen_restore(POFO_COORD(2, 2), menu_bottom_right, menu_save_buffer);

    while (getch() != 0x1011)
        ;
    pofo_clear_screen();

    return 0;
}
