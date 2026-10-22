#include "pofo.h"
#include "tinylib.h"
#include "gui_core.h"
#include "gui.h"
#include "build_id.h"
#include "version.h"

/* Indexed by enum nav_state; NAV_EXIT has no handler. */
static int (*nav_handlers[])() = {
    do_dashboard, do_root_menu, do_interfaces_list, do_interface_menu,
    do_interface_detail, do_network_settings, do_ip_settings, do_info_screen
};
static char title[32];

int main()
{
    int state;
    int saved_cursor_mode;

    pofo_clear_screen();
    pofo_draw_box(POFO_COORD(0, 0), POFO_COORD(7, 39));
    status_init();

    gotoxy(2, 0);
    fmt(title, "PFTC v%u.%u.%u (%x%x%x%x)", PFTC_VERSION_MAJOR,
        PFTC_VERSION_MINOR, PFTC_VERSION_PATCH,
        (unsigned int)(PFTC_BUILD_ID >> 24),
        (unsigned int)(PFTC_BUILD_ID >> 16),
        (unsigned int)(PFTC_BUILD_ID >> 8),
        (unsigned int)PFTC_BUILD_ID);
    con_puts(title);
    saved_cursor_mode = pofo_set_cursor_mode(POFO_CURSOR_OFF);

    state = do_handshake();
    while (state != NAV_EXIT)
        state = nav_handlers[state]();

    pofo_clear_screen();
    pofo_set_cursor_mode(saved_cursor_mode);
    return 0;
}
