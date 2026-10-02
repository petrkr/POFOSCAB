#include <stdio.h>
#include <conio.h>
#include <string.h>
#include "pofo.h"
#include "smartcable.h"
#include "pftc_proto.h"
#include "gui_core.h"
#include "gui.h"
#include "valid.h"
#include "gui_shared.h"

static char ip_settings_menu_text[SETTINGS_FIELD_MAX * 5 + 32];

static char invalid_ipv4_dialog_text[] = "Invalid IPv4 address.";
static char invalid_prefix_dialog_text[] = "Prefix must be 0-32.";

/* Loose check: 4 dot-separated 1-3 digit groups, no numeric range check
   per octet (so "999.999.999.999" slips through) - parse_ipv4() just
   takes whatever is there; stricter validation isn't worth it for a
   field that already passed is_valid_ipv4(). "" doesn't parse as
   valid either, same as any other bad value. */
/* Under DHCP, only Mode/IPv6 are shown - no point offering IP/Prefix/
   Gateway for fields DHCP overwrites anyway. No Apply here - see
   do_interface_menu()'s Apply, which sends the single shared
   SET_NETIF for both this screen and Network settings. */
static void build_ip_settings_text()
{
    char *p;

    p = ip_settings_menu_text;
    p += sprintf(p, "IP settings") + 1;
    p += sprintf(p, "Mode: %s",
                pending_settings.ip_mode == PFTC_IP_MODE_STATIC ? "Static" : "DHCP") + 1;
    if (pending_settings.ip_mode == PFTC_IP_MODE_STATIC) {
        p += sprintf(p, "IP: %s", pending_settings.ip) + 1;
        p += sprintf(p, "Prefix: %s", pending_settings.prefix) + 1;
        p += sprintf(p, "Gateway: %s", pending_settings.gateway) + 1;
    }
    p += sprintf(p, "IPv6: %s", yesno(pending_settings.ipv6_enabled)) + 1;
    *p = 0;
}

/* Edits pending_settings.ip_mode/ip/prefix/gateway/ipv6_enabled in
   place - no Apply of its own, see do_interface_menu()'s Apply. ESC
   always returns to Interface menu, keeping whatever was edited so
   far (same shared-state model as Network settings). */
enum nav_state do_ip_settings()
{
    int result;
    unsigned char last_item;
    unsigned int exit_keys[3];

    exit_keys[0] = 0x000D;
    exit_keys[1] = 0x001B;
    exit_keys[2] = 0;

    pofo_show_cursor();
    last_item = 0;

    for (;;) {
        if (screen_push(POFO_COORD(1, 1), POFO_COORD(7, 38)) != 0) {
            show_out_of_memory_error();
            return NAV_INTERFACE_MENU;
        }

        build_ip_settings_text();

        result = pofo_menu_show(SETTINGS_MENU_TOP_LEFT,
                                ip_settings_menu_text, 0, 0, last_item,
                                SETTINGS_MENU_TYPE_DEPTH);

        if (result == -1) {
            pofo_hide_cursor();
            screen_pop();
            return NAV_INTERFACE_MENU;
        }

        last_item = POFO_LOW_BYTE(result);
        if (pending_settings.ip_mode != PFTC_IP_MODE_STATIC) {
            /* DHCP: only Mode(0)/IPv6(1) are on screen. */
            switch (last_item) {
            case 0:
                pending_settings.ip_mode = PFTC_IP_MODE_STATIC;
                break;
            case 1:
                pending_settings.ipv6_enabled = !pending_settings.ipv6_enabled;
                break;
            }
            screen_pop();
            continue;
        }

        switch (last_item) {
        case 0:
            pending_settings.ip_mode = PFTC_IP_MODE_DHCP;
            break;
        case 1:
            edit_field_validated("IP", pending_settings.ip,
                                 sizeof(pending_settings.ip) - 1, 18,
                                 is_valid_ipv4, invalid_ipv4_dialog_text,
                                 exit_keys);
            break;
        case 2:
            edit_field_validated("Prefix", pending_settings.prefix,
                                 sizeof(pending_settings.prefix) - 1, 12,
                                 is_valid_prefix, invalid_prefix_dialog_text,
                                 exit_keys);
            break;
        case 3:
            edit_field_validated("Gateway", pending_settings.gateway,
                                 sizeof(pending_settings.gateway) - 1, 18,
                                 is_valid_ipv4, invalid_ipv4_dialog_text,
                                 exit_keys);
            break;
        case 4:
            pending_settings.ipv6_enabled = !pending_settings.ipv6_enabled;
            break;
        }
        screen_pop();
    }
}

enum nav_state do_info_screen()
{
    pofo_hide_cursor();
    pofo_error_dialog(DIALOG_TOP_LEFT, not_supported_dialog_text);
    return NAV_ROOT_MENU;
}
