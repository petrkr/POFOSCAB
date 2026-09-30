#include <stdio.h>
#include <conio.h>
#include <string.h>
#include "pofo.h"
#include "smartcable.h"
#include "pftc_proto.h"
#include "pftc_gui.h"
#include "pftc_nav.h"
#include "pftc_valid.h"
#include "pftc_nav_shared.h"

#define INTERFACE_MENU_TOP_LEFT POFO_COORD(1, 2)
#define INTERFACE_MENU_HEIGHT_LIMIT 7
#define INTERFACE_MENU_TYPE_DEPTH \
    ((INTERFACE_MENU_HEIGHT_LIMIT << 3) | POFO_BOX_DOUBLE)

static char interface_menu_text[64];
static char interface_menu_suffix[] = "Status\0Network settings\0IP settings\0\0";

/* Fetches selected_interface fresh, then shows the Status/Network
   settings/IP settings menu - all three read the same fetch, no
   further re-fetch until back out to Interfaces and in again. */
enum nav_state do_interface_menu()
{
    unsigned int bottom_right;
    int result;
    char *p;

    if (fetch_netif(selected_interface) != 0)
        return NAV_INTERFACES;

    p = interface_menu_text;
    strcpy(p, netif_type_label(netif_type));
    p += strlen(p) + 1;
    memcpy(p, interface_menu_suffix, sizeof(interface_menu_suffix));

    pofo_menu_getsize(INTERFACE_MENU_TOP_LEFT, interface_menu_text, 0,
                       &bottom_right);
    if (screen_push(INTERFACE_MENU_TOP_LEFT, bottom_right) != 0) {
        show_out_of_memory_error();
        return NAV_INTERFACES;
    }

    pofo_show_cursor();
    result = pofo_menu_show(INTERFACE_MENU_TOP_LEFT, interface_menu_text,
                            0, 0, 0, INTERFACE_MENU_TYPE_DEPTH);
    pofo_hide_cursor();
    screen_pop();

    if (result == -1)
        return NAV_INTERFACES;

    switch (POFO_LOW_BYTE(result)) {
    case 0: return NAV_DETAIL;
    case 1: return NAV_NETWORK_SETTINGS;
    case 2: return NAV_IP_SETTINGS;
    }
    return NAV_INTERFACES;
}

#define DETAIL_MENU_TOP_LEFT POFO_COORD(1, 2)
#define DETAIL_MENU_HEIGHT_LIMIT 7
#define DETAIL_MENU_TYPE_DEPTH \
    ((DETAIL_MENU_HEIGHT_LIMIT << 3) | POFO_BOX_DOUBLE)

static char detail_menu_text[256];

static void build_detail_text()
{
    char *p;

    p = detail_menu_text;
    p += sprintf(p, "Interface Detail") + 1;

    if (netif_type == PFTC_WIFI_CLIENT) {
        if (netif_ssid_length == 0xFF)
            strcpy(p, "SSID: invalid");
        else if (netif_ssid_length == 0xFE)
            strcpy(p, "SSID: unavailable");
        else
            sprintf(p, "SSID: %s", netif_ssid);
    } else
        sprintf(p, "Interface %02X", netif_interface);
    p += strlen(p) + 1;

    if (netif_type == PFTC_WIFI_CLIENT &&
        netif_ssid_length != 0xFF && netif_ssid_length != 0xFE) {
        p += sprintf(p, "RSSI: %d dBm", netif_rssi) + 1;
        p += sprintf(p, "Channel: %u", netif_channel) + 1;
    }

    p += sprintf(p, "IP: %u.%u.%u.%u/%u", netif_ipv4[0], netif_ipv4[1],
            netif_ipv4[2], netif_ipv4[3], netif_netmask_prefix) + 1;
    p += sprintf(p, "GW: %u.%u.%u.%u", netif_gateway[0], netif_gateway[1],
            netif_gateway[2], netif_gateway[3]) + 1;
    p += sprintf(p, "DNS: %u.%u.%u.%u", netif_dns[0], netif_dns[1],
            netif_dns[2], netif_dns[3]) + 1;

    *p = 0; /* double zero terminator */
}

/* Reads whatever do_interface_menu() last fetched - no re-fetch of its
   own. A scrollable menu-as-window (see INT60H.md AH=0Fh depth bits)
   stands in for a real info window - the ROM has no passive scrolling
   text primitive. ESC or an item pick both just close it ->
   NAV_INTERFACE_MENU. */
enum nav_state do_interface_detail()
{
    unsigned int bottom_right;

    build_detail_text();

    pofo_menu_getsize(DETAIL_MENU_TOP_LEFT, detail_menu_text, 0,
                       &bottom_right);
    if (screen_push(DETAIL_MENU_TOP_LEFT, bottom_right) != 0) {
        show_out_of_memory_error();
        return NAV_INTERFACE_MENU;
    }

    pofo_show_cursor();
    pofo_menu_show(DETAIL_MENU_TOP_LEFT, detail_menu_text, 0, 0, 0,
                   DETAIL_MENU_TYPE_DEPTH);
    pofo_hide_cursor();
    screen_pop();

    return NAV_INTERFACE_MENU;
}

static char network_settings_menu_text[SETTINGS_FIELD_MAX * 3 + 32];
static char network_settings_ssid[33];
static char network_settings_psk[65];
static unsigned char network_settings_enabled;

char *yesno(flag)
unsigned char flag;
{
    return flag ? "Yes" : "No";
}

/* SSID has no format rule beyond the field's own max length. */
static int is_valid_always(text)
char *text;
{
    return 1;
}

static char invalid_psk_dialog_text[] = "PSK must be 8-63 chars, or empty.";

/* Empty is valid (open network, or "leave unset"); anything else must
   meet WPA2/WPA3's 8-63 printable-ASCII passphrase length. */
static int is_valid_psk(text)
char *text;
{
    unsigned int len;

    len = strlen(text);
    return len == 0 || (len >= 8 && len <= 63);
}

/* When disabled, only Enabled/Apply are shown - no point offering
   Scan/SSID/PSK for a slot that's off. */
static void build_network_settings_text()
{
    char *p;

    p = network_settings_menu_text;
    p += sprintf(p, "Network settings") + 1;
    p += sprintf(p, "Enabled: %s", yesno(network_settings_enabled)) + 1;
    if (network_settings_enabled) {
        p += sprintf(p, "Scan") + 1;
        p += sprintf(p, "SSID: %s", network_settings_ssid) + 1;
        p += sprintf(p, "PSK: %s", network_settings_psk) + 1;
    }
    p += sprintf(p, "Apply") + 1;
    *p = 0;
}

#define WIFISCAN_MENU_MAX_ITEMS 10

static char wifiscan_menu_text[WIFISCAN_MENU_MAX_ITEMS * (32 + 8) + 32];
static unsigned char wifiscan_menu_security[WIFISCAN_MENU_MAX_ITEMS];

/* Scans on selected_interface, shows results as a menu (title +
   "SSID (security, RSSI dBm)" per entry), and on a pick copies the
   SSID into network_settings_ssid and returns the chosen entry's
   security in *security_out. ESC picks nothing (returns 0, leaves
   network_settings_ssid untouched). SSID bytes are shown as-is per
   PFTC_PROTOCOL.md (no filtering) - non-ASCII SSIDs may render as
   garbled glyphs on the Portfolio's charset, which is expected. */
static int do_wifi_scan_menu(security_out)
unsigned char *security_out;
{
    unsigned int received;
    int status;
    unsigned char *p;
    unsigned char count, i, ssid_len;
    signed char rssi;
    unsigned char security;
    char *out;
    char label[33];
    unsigned int bottom_right;
    int result;

    wifiscan_request[1] = selected_interface;
    pofo_hide_cursor();
    progress_dialog_open("Scanning");
    status = smartcable_exchange(wifiscan_request, sizeof(wifiscan_request),
                           wifiscan_response, sizeof(wifiscan_response),
                           &received);
    progress_dialog_close();
    pofo_show_cursor();
    if (status != 0) {
        show_transport_error();
        return 0;
    }
    if (received < 3) {
        show_protocol_error();
        return 0;
    }
    if (wifiscan_response[0] != 0x20 || wifiscan_response[1] != 0x00) {
        show_protocol_error();
        return 0;
    }

    count = wifiscan_response[2];
    if (count > WIFISCAN_MENU_MAX_ITEMS)
        count = WIFISCAN_MENU_MAX_ITEMS;

    out = wifiscan_menu_text;
    out += sprintf(out, "Scan results") + 1;

    p = wifiscan_response + 3;
    for (i = 0; i < count; i++) {
        ssid_len = p[0];
        if (ssid_len > 32)
            ssid_len = 32;
        memcpy(label, p + 1, ssid_len);
        label[ssid_len] = 0;
        rssi = (signed char)p[1 + ssid_len];
        security = p[1 + ssid_len + 1];
        wifiscan_menu_security[i] = security;

        out += sprintf(out, "%s (%s, %d)", label,
                security == PFTC_SECURITY_OPEN ? "open" :
                security == PFTC_SECURITY_WPA3_PSK ? "WPA3" : "WPA2",
                rssi) + 1;

        p += 1 + ssid_len + 2;
    }
    *out = 0;

    pofo_menu_getsize(SETTINGS_MENU_TOP_LEFT, wifiscan_menu_text, 0,
                       &bottom_right);
    if (screen_push(SETTINGS_MENU_TOP_LEFT, bottom_right) != 0) {
        show_out_of_memory_error();
        return 0;
    }

    result = pofo_menu_show(SETTINGS_MENU_TOP_LEFT, wifiscan_menu_text,
                            0, 0, 0, SETTINGS_MENU_TYPE_DEPTH);
    screen_pop();

    if (result == -1)
        return 0;

    i = POFO_LOW_BYTE(result);
    p = wifiscan_response + 3;
    while (i > 0) {
        ssid_len = p[0];
        if (ssid_len > 32)
            ssid_len = 32;
        p += 1 + ssid_len + 2;
        i--;
    }
    ssid_len = p[0];
    if (ssid_len > 32)
        ssid_len = 32;
    memcpy(network_settings_ssid, p + 1, ssid_len);
    network_settings_ssid[ssid_len] = 0;
    *security_out = wifiscan_menu_security[POFO_LOW_BYTE(result)];
    return 1;
}

/* Skeleton only - fields are edited and held in memory, but Apply has
   no SET_NETIF wired up yet (the backend doesn't support it either) -
   reports "Not supported yet." instead of sending anything. Reads
   whatever do_interface_menu() last fetched into netif_*; no re-fetch
   of its own. */
enum nav_state do_network_settings()
{
    int result;
    unsigned char last_item;
    unsigned char scanned_security;
    unsigned int exit_keys[3];
    exit_keys[0] = 0x000D;
    exit_keys[1] = 0x001B;
    exit_keys[2] = 0;

    network_settings_enabled = 1;
    strcpy(network_settings_ssid,
           (netif_ssid_length != 0xFF && netif_ssid_length != 0xFE) ?
           netif_ssid : "");
    network_settings_psk[0] = 0;

    pofo_show_cursor();
    last_item = 0;

    for (;;) {
        if (screen_push(POFO_COORD(1, 1), POFO_COORD(7, 38)) != 0) {
            show_out_of_memory_error();
            return NAV_INTERFACE_MENU;
        }

        build_network_settings_text();

        result = pofo_menu_show(SETTINGS_MENU_TOP_LEFT,
                                network_settings_menu_text, 0, 0, last_item,
                                SETTINGS_MENU_TYPE_DEPTH);

        if (result == -1) {
            pofo_hide_cursor();
            screen_pop();
            return NAV_INTERFACE_MENU;
        }

        last_item = POFO_LOW_BYTE(result);
        if (!network_settings_enabled) {
            /* Disabled: only Enabled(0)/Apply(1) are on screen at all. */
            switch (last_item) {
            case 0:
                network_settings_enabled = 1;
                break;
            case 1:
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
            network_settings_enabled = 0;
            break;
        case 1:
            if (do_wifi_scan_menu(&scanned_security)) {
                last_item = 2;
                if (scanned_security != PFTC_SECURITY_OPEN) {
                    screen_pop();
                    if (screen_push(POFO_COORD(1, 1), POFO_COORD(7, 38)) != 0) {
                        show_out_of_memory_error();
                        return NAV_INTERFACE_MENU;
                    }
                    edit_field_validated("PSK", network_settings_psk,
                                         sizeof(network_settings_psk) - 1, 20,
                                         is_valid_psk, invalid_psk_dialog_text,
                                         exit_keys);
                    last_item = 3;
                }
            }
            break;
        case 2:
            edit_field_validated("SSID", network_settings_ssid,
                                 sizeof(network_settings_ssid) - 1, 20,
                                 is_valid_always, "", exit_keys);
            break;
        case 3:
            edit_field_validated("PSK", network_settings_psk,
                                 sizeof(network_settings_psk) - 1, 20,
                                 is_valid_psk, invalid_psk_dialog_text,
                                 exit_keys);
            break;
        case 4:
            pofo_hide_cursor();
            pofo_error_dialog(DIALOG_TOP_LEFT, not_supported_dialog_text);
            screen_pop();
            return NAV_INTERFACE_MENU;
        }
        screen_pop();
    }
}
