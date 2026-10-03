#include <stdio.h>
#include <conio.h>
#include <string.h>
#include <stdlib.h>
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

/* Local copy makes ESC discard all edits from this screen. */
struct ip_edit {
    unsigned char ip_mode;
    char ip[16];
    char prefix[3];
    char gateway[16];
    unsigned char ipv6_enabled;
};

static void build_ip_settings_text(edit)
struct ip_edit *edit;
{
    char *p;

    p = ip_settings_menu_text;
    p += sprintf(p, "IP settings") + 1;
    p += sprintf(p, "Mode: %s",
                edit->ip_mode == PFTC_IP_MODE_STATIC ? "Static" : "DHCP") + 1;
    if (edit->ip_mode == PFTC_IP_MODE_STATIC) {
        p += sprintf(p, "IP: %s", edit->ip) + 1;
        p += sprintf(p, "Prefix: %s", edit->prefix) + 1;
        p += sprintf(p, "Gateway: %s", edit->gateway) + 1;
    }
    p += sprintf(p, "IPv6: %s", yesno(edit->ipv6_enabled)) + 1;
    p += sprintf(p, "Done") + 1;
    *p = 0;
}

enum nav_state do_ip_settings()
{
    struct ip_edit *edit;
    int result;
    unsigned char last_item;
    unsigned char done_item;
    unsigned int exit_keys[3];

    exit_keys[0] = 0x000D;
    exit_keys[1] = 0x001B;
    exit_keys[2] = 0;

    edit = malloc(sizeof(struct ip_edit));
    if (edit == NULL) {
        show_out_of_memory_error();
        return NAV_INTERFACE_MENU;
    }
    edit->ip_mode = interface_settings.ip_mode;
    strcpy(edit->ip, interface_settings.ip);
    strcpy(edit->prefix, interface_settings.prefix);
    strcpy(edit->gateway, interface_settings.gateway);
    edit->ipv6_enabled = interface_settings.ipv6_enabled;

    pofo_show_cursor();
    last_item = 0;

    for (;;) {
        if (screen_push(POFO_COORD(1, 1), POFO_COORD(7, 38)) != 0) {
            free(edit);
            show_out_of_memory_error();
            return NAV_INTERFACE_MENU;
        }

        build_ip_settings_text(edit);
        done_item = edit->ip_mode == PFTC_IP_MODE_STATIC ? 5 : 2;

        result = pofo_menu_show(SETTINGS_MENU_TOP_LEFT,
                                ip_settings_menu_text, 0, 0, last_item,
                                SETTINGS_MENU_TYPE_DEPTH);

        if (result == -1) {
            free(edit);
            pofo_hide_cursor();
            screen_pop();
            return NAV_INTERFACE_MENU;
        }

        last_item = POFO_LOW_BYTE(result);
        if (last_item == done_item) {
            interface_settings.ip_mode = edit->ip_mode;
            strcpy(interface_settings.ip, edit->ip);
            strcpy(interface_settings.prefix, edit->prefix);
            strcpy(interface_settings.gateway, edit->gateway);
            interface_settings.ipv6_enabled = edit->ipv6_enabled;
            free(edit);
            pofo_hide_cursor();
            screen_pop();
            return NAV_INTERFACE_MENU;
        }

        if (edit->ip_mode != PFTC_IP_MODE_STATIC) {
            switch (last_item) {
            case 0:
                edit->ip_mode = PFTC_IP_MODE_STATIC;
                break;
            case 1:
                edit->ipv6_enabled = !edit->ipv6_enabled;
                break;
            }
            screen_pop();
            continue;
        }

        switch (last_item) {
        case 0:
            edit->ip_mode = PFTC_IP_MODE_DHCP;
            break;
        case 1:
            edit_field_validated("IP", edit->ip,
                                 sizeof(edit->ip) - 1, 18,
                                 is_valid_ipv4, invalid_ipv4_dialog_text,
                                 exit_keys);
            break;
        case 2:
            edit_field_validated("Prefix", edit->prefix,
                                 sizeof(edit->prefix) - 1, 12,
                                 is_valid_prefix, invalid_prefix_dialog_text,
                                 exit_keys);
            break;
        case 3:
            edit_field_validated("Gateway", edit->gateway,
                                 sizeof(edit->gateway) - 1, 18,
                                 is_valid_ipv4, invalid_ipv4_dialog_text,
                                 exit_keys);
            break;
        case 4:
            edit->ipv6_enabled = !edit->ipv6_enabled;
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
