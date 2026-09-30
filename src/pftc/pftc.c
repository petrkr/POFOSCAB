#include <stdio.h>
#include <conio.h>
#include "pofo.h"
#include "gui_core.h"
#include "gui.h"

int main()
{
    enum nav_state state;

    pofo_clear_screen();
    pofo_draw_box(POFO_COORD(0, 0), POFO_COORD(7, 39));
    status_init();

    gotoxy(2, 0);
    printf("PFTC");
    fflush(stdout);
    pofo_hide_cursor();

    state = do_handshake();
    for (;;) {
        switch (state) {
        case NAV_DASHBOARD:
            state = do_dashboard();
            break;
        case NAV_ROOT_MENU:
            state = do_root_menu();
            break;
        case NAV_INTERFACES:
            state = do_interfaces_list();
            break;
        case NAV_INTERFACE_MENU:
            state = do_interface_menu();
            break;
        case NAV_DETAIL:
            state = do_interface_detail();
            break;
        case NAV_NETWORK_SETTINGS:
            state = do_network_settings();
            break;
        case NAV_IP_SETTINGS:
            state = do_ip_settings();
            break;
        case NAV_INFO:
            state = do_info_screen();
            break;
        case NAV_EXIT:
            pofo_clear_screen();
            return 0;
        }
    }
}
