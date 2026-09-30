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
static char ip_settings_ip[16];
static char ip_settings_prefix[3];
static char ip_settings_gateway[16];
static unsigned char ip_settings_mode_static;
static unsigned char ip_settings_ipv6;

static char invalid_ipv4_dialog_text[] = "Invalid IPv4 address.";
static char invalid_prefix_dialog_text[] = "Prefix must be 0-32.";

/* Loose check: 4 dot-separated 1-3 digit groups, no numeric range check
   per octet (so "999.999.999.999" slips through) - Apply is "Not
   supported yet" regardless, so a stricter parse buys nothing here.
   "" doesn't parse as valid either, same as any other bad value. */
/* Under DHCP, only Mode/IPv6/Apply are shown - no point offering
   IP/Prefix/Gateway for fields DHCP overwrites anyway. */
static void build_ip_settings_text()
{
    char *p;

    p = ip_settings_menu_text;
    p += sprintf(p, "IP settings") + 1;
    p += sprintf(p, "Mode: %s", ip_settings_mode_static ? "Static" : "DHCP") + 1;
    if (ip_settings_mode_static) {
        p += sprintf(p, "IP: %s", ip_settings_ip) + 1;
        p += sprintf(p, "Prefix: %s", ip_settings_prefix) + 1;
        p += sprintf(p, "Gateway: %s", ip_settings_gateway) + 1;
    }
    p += sprintf(p, "IPv6: %s", yesno(ip_settings_ipv6)) + 1;
    p += sprintf(p, "Apply") + 1;
    *p = 0;
}

/* Skeleton only - same as do_network_settings(): fields are held in
   memory, Apply reports "Not supported yet." instead of sending
   SET_IPCFG. Starts from netif_*'s runtime IP/netmask/gateway as a
   read-only-looking default, not from an actual GET_IPCFG fetch (no
   IPCFG wire code exists yet). */
enum nav_state do_ip_settings()
{
    int result;
    unsigned char last_item;
    unsigned int exit_keys[3];

    exit_keys[0] = 0x000D;
    exit_keys[1] = 0x001B;
    exit_keys[2] = 0;

    sprintf(ip_settings_ip, "%u.%u.%u.%u", netif_ipv4[0], netif_ipv4[1],
            netif_ipv4[2], netif_ipv4[3]);
    sprintf(ip_settings_prefix, "%u", netif_netmask_prefix);
    sprintf(ip_settings_gateway, "%u.%u.%u.%u", netif_gateway[0],
            netif_gateway[1], netif_gateway[2], netif_gateway[3]);
    ip_settings_mode_static = 0;
    ip_settings_ipv6 = 0;

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
        if (!ip_settings_mode_static) {
            /* DHCP: only Mode(0)/IPv6(1)/Apply(2) are on screen. */
            switch (last_item) {
            case 0:
                ip_settings_mode_static = 1;
                break;
            case 1:
                ip_settings_ipv6 = !ip_settings_ipv6;
                break;
            case 2:
                pofo_hide_cursor();
                pofo_error_dialog(DIALOG_TOP_LEFT, not_supported_dialog_text);
                screen_pop();
                return NAV_INTERFACE_MENU;
            }
            screen_pop();
            continue;
        }

        switch (last_item) {
        case 0:
            ip_settings_mode_static = 0;
            break;
        case 1:
            edit_field_validated("IP", ip_settings_ip,
                                 sizeof(ip_settings_ip) - 1, 18,
                                 is_valid_ipv4, invalid_ipv4_dialog_text,
                                 exit_keys);
            break;
        case 2:
            edit_field_validated("Prefix", ip_settings_prefix,
                                 sizeof(ip_settings_prefix) - 1, 12,
                                 is_valid_prefix, invalid_prefix_dialog_text,
                                 exit_keys);
            break;
        case 3:
            edit_field_validated("Gateway", ip_settings_gateway,
                                 sizeof(ip_settings_gateway) - 1, 18,
                                 is_valid_ipv4, invalid_ipv4_dialog_text,
                                 exit_keys);
            break;
        case 4:
            ip_settings_ipv6 = !ip_settings_ipv6;
            break;
        case 5:
            pofo_hide_cursor();
            pofo_error_dialog(DIALOG_TOP_LEFT, not_supported_dialog_text);
            screen_pop();
            return NAV_INTERFACE_MENU;
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
