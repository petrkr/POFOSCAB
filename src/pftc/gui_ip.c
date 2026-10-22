#include "pofo.h"
#include "tinylib.h"
#include "pftc_proto.h"
#include "gui_core.h"
#include "gui.h"
#include "valid.h"
#include "gui_shared.h"

static char invalid_ipv4_dialog_text[] = "Invalid IPv4 address.";
static char invalid_prefix_dialog_text[] = "Prefix must be 0-32.";

/* Local copy makes ESC discard all edits from this screen. */
static struct ip_settings edit;

static void build_ip_settings_text()
{
    char *p;

    p = str_copy(menu_text, "IP settings") + 1;
    p = fmt(p, "Mode: %s",
            edit.ip_mode == PFTC_IP_MODE_STATIC ? "Static" : "DHCP") + 1;
    if (edit.ip_mode == PFTC_IP_MODE_STATIC) {
        p = fmt(p, "IP: %s", edit.ip) + 1;
        p = fmt(p, "Prefix: %s", edit.prefix) + 1;
        p = fmt(p, "Gateway: %s", edit.gateway) + 1;
    }
    p = fmt(p, "IPv6: %s", yesno(edit.ipv6_enabled)) + 1;
    p = str_copy(p, "Done") + 1;
    *p = 0;
}

static void edit_address(title, value)
char *title;
char *value;
{
    edit_field_validated(title, value, sizeof(edit.ip) - 1, 18,
                         is_valid_ipv4, invalid_ipv4_dialog_text);
}

enum nav_state do_ip_settings()
{
    int item;
    int last_item;
    int done_item;

    mem_copy(&edit, &interface_settings.ip, sizeof(edit));

    pofo_set_cursor_mode(POFO_CURSOR_BLOCK);
    last_item = 0;

    for (;;) {
        if (screen_push(SETTINGS_SCREEN_TOP_LEFT,
                        SETTINGS_SCREEN_BOTTOM_RIGHT) != 0) {
            show_out_of_memory_error();
            return NAV_INTERFACE_MENU;
        }

        build_ip_settings_text();
        done_item = edit.ip_mode == PFTC_IP_MODE_STATIC ? 5 : 2;

        item = menu_show(menu_text, last_item);

        if (item == -1 || item == done_item) {
            if (item == done_item)
                mem_copy(&interface_settings.ip, &edit, sizeof(edit));
            pofo_set_cursor_mode(POFO_CURSOR_OFF);
            screen_pop();
            return NAV_INTERFACE_MENU;
        }

        last_item = item;
        if (edit.ip_mode != PFTC_IP_MODE_STATIC) {
            switch (last_item) {
            case 0:
                edit.ip_mode = PFTC_IP_MODE_STATIC;
                break;
            case 1:
                edit.ipv6_enabled = !edit.ipv6_enabled;
                break;
            }
            screen_pop();
            continue;
        }

        switch (last_item) {
        case 0:
            edit.ip_mode = PFTC_IP_MODE_DHCP;
            break;
        case 1:
            edit_address("IP", edit.ip);
            break;
        case 2:
            /* Two digits: the validator only accepts 0-32 anyway. */
            edit_field_validated("Prefix", edit.prefix, 2, 12,
                                 is_valid_prefix, invalid_prefix_dialog_text);
            break;
        case 3:
            edit_address("Gateway", edit.gateway);
            break;
        case 4:
            edit.ipv6_enabled = !edit.ipv6_enabled;
            break;
        }
        screen_pop();
    }
}

enum nav_state do_info_screen()
{
    pofo_set_cursor_mode(POFO_CURSOR_OFF);
    pofo_error_dialog(DIALOG_TOP_LEFT, not_supported_dialog_text);
    return NAV_ROOT_MENU;
}
