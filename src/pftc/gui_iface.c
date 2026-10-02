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

#define INTERFACE_MENU_TOP_LEFT POFO_COORD(1, 2)
#define INTERFACE_MENU_HEIGHT_LIMIT 7
#define INTERFACE_MENU_TYPE_DEPTH \
    ((INTERFACE_MENU_HEIGHT_LIMIT << 3) | POFO_BOX_DOUBLE)

#define DETAIL_MENU_TOP_LEFT POFO_COORD(1, 2)
#define DETAIL_MENU_HEIGHT_LIMIT 7
#define DETAIL_MENU_TYPE_DEPTH \
    ((DETAIL_MENU_HEIGHT_LIMIT << 3) | POFO_BOX_DOUBLE)

#define WIFISCAN_MENU_MAX_ITEMS 10

/* Per-screen menu text buffers. */
static char interface_menu_text[64];
static char detail_menu_text[256];
static char network_settings_menu_text[SETTINGS_FIELD_MAX * 3 + 32];
static char wifiscan_menu_text[WIFISCAN_MENU_MAX_ITEMS * (32 + 8) + 32];

static char interface_menu_suffix[] =
    "Status\0Network settings\0IP settings\0Apply\0\0";
static unsigned char wifiscan_menu_security[WIFISCAN_MENU_MAX_ITEMS];

static char invalid_psk_dialog_text[] = "PSK must be 8-63 chars, or empty.";

/* Set once do_interface_menu() has fetched+initialized for the
   current selected_interface; cleared on the way back out to
   Interfaces so the next entry (possibly a different interface)
   fetches fresh. Without this, every re-entry from Network/IP
   settings (both return NAV_INTERFACE_MENU) would re-fetch and
   clobber whatever the user just edited in pending_settings. */
static unsigned char interface_menu_initialized;

/* Fetches selected_interface fresh on first entry only, then shows
   the Status/Network settings/IP settings/Apply menu - all screens
   read/edit the same fetch and pending_settings, no further re-fetch
   until back out to Interfaces and in again (see
   interface_menu_initialized above). */
enum nav_state do_interface_menu()
{
    unsigned int bottom_right;
    int result;
    char *p;

    if (!interface_menu_initialized) {
        if (fetch_netif_state(selected_interface, &menu_netif) != 0)
            return NAV_INTERFACES;
        init_pending_settings();
        interface_menu_initialized = 1;
    }

    p = interface_menu_text;
    p += sprintf(p, netif_type_label(menu_netif.info.type)) + 1;
    memcpy(p, interface_menu_suffix, sizeof(interface_menu_suffix));

    pofo_menu_getsize(INTERFACE_MENU_TOP_LEFT, interface_menu_text, 0,
                       &bottom_right);
    if (screen_push(INTERFACE_MENU_TOP_LEFT, bottom_right) != 0) {
        show_out_of_memory_error();
        interface_menu_initialized = 0;
        return NAV_INTERFACES;
    }

    pofo_show_cursor();
    result = pofo_menu_show(INTERFACE_MENU_TOP_LEFT, interface_menu_text,
                            0, 0, 0, INTERFACE_MENU_TYPE_DEPTH);
    pofo_hide_cursor();
    screen_pop();

    if (result == -1) {
        interface_menu_initialized = 0;
        return NAV_INTERFACES;
    }

    switch (POFO_LOW_BYTE(result)) {
    case 0: return NAV_DETAIL;
    case 1: return NAV_NETWORK_SETTINGS;
    case 2: return NAV_IP_SETTINGS;
    case 3:
        apply_netif_settings();
        return NAV_INTERFACE_MENU;
    }
    interface_menu_initialized = 0;
    return NAV_INTERFACES;
}

static void build_detail_text()
{
    char *p;

    p = detail_menu_text;
    p += sprintf(p, "Interface Detail") + 1;

    if (menu_netif.info.type == PFTC_WIFI_CLIENT) {
        if (menu_netif.ext.wificli.fields.ssid_length == 0xFF)
            strcpy(p, "SSID: invalid");
        else if (menu_netif.ext.wificli.fields.ssid_length == 0xFE)
            strcpy(p, "SSID: unavailable");
        else
            sprintf(p, "SSID: %s", menu_netif.ext.wificli.ssid);
    } else
        sprintf(p, "Interface %02X", menu_netif.info.interface);
    p += strlen(p) + 1;

    if (menu_netif.info.type == PFTC_WIFI_CLIENT &&
        menu_netif.ext.wificli.fields.ssid_length != 0xFF &&
        menu_netif.ext.wificli.fields.ssid_length != 0xFE) {
        p += sprintf(p, "RSSI: %d dBm", menu_netif.ext.wificli.fields.rssi) + 1;
        p += sprintf(p, "Channel: %u", menu_netif.ext.wificli.fields.channel) + 1;
    }

    p += sprintf(p, "IP: %u.%u.%u.%u/%u", menu_netif.info.ipv4[0],
            menu_netif.info.ipv4[1], menu_netif.info.ipv4[2],
            menu_netif.info.ipv4[3], menu_netif.info.netmask_prefix) + 1;
    p += sprintf(p, "GW: %u.%u.%u.%u", menu_netif.info.gateway[0],
            menu_netif.info.gateway[1], menu_netif.info.gateway[2],
            menu_netif.info.gateway[3]) + 1;
    p += sprintf(p, "DNS: %u.%u.%u.%u", menu_netif.info.dns[0],
            menu_netif.info.dns[1], menu_netif.info.dns[2],
            menu_netif.info.dns[3]) + 1;

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

/* Empty is valid (open network, or "leave unset"); anything else must
   meet WPA2/WPA3's 8-63 printable-ASCII passphrase length. */
static int is_valid_psk(text)
char *text;
{
    unsigned int len;

    len = strlen(text);
    return len == 0 || (len >= 8 && len <= 63);
}

/* When disabled, only Enabled is shown - no point offering Scan/SSID/
   PSK for a slot that's off. No Apply here - see do_interface_menu()'s
   Apply, which sends the single shared SET_NETIF for both this screen
   and IP settings. */
static void build_network_settings_text()
{
    char *p;

    p = network_settings_menu_text;
    p += sprintf(p, "Network settings") + 1;
    p += sprintf(p, "Enabled: %s", yesno(pending_settings.enabled)) + 1;
    if (pending_settings.enabled) {
        p += sprintf(p, "Scan") + 1;
        p += sprintf(p, "SSID: %s", pending_settings.ssid) + 1;
        p += sprintf(p, "PSK: %s", pending_settings.psk) + 1;
    }
    *p = 0;
}

/* Scans on selected_interface, shows results as a menu (title +
   "SSID (security, RSSI dBm)" per entry), and on a pick copies the
   SSID into pending_settings.ssid and returns the chosen entry's
   security in *security_out. ESC picks nothing (returns 0, leaves
   pending_settings.ssid untouched). SSID bytes are shown as-is per
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
    memcpy(pending_settings.ssid, p + 1, ssid_len);
    pending_settings.ssid[ssid_len] = 0;
    *security_out = wifiscan_menu_security[POFO_LOW_BYTE(result)];
    return 1;
}

/* Edits pending_settings.enabled/ssid/psk in place - no Apply of its
   own, see do_interface_menu()'s Apply. ESC always returns to
   Interface menu, keeping whatever was edited so far (same
   shared-state model as IP settings). */
enum nav_state do_network_settings()
{
    int result;
    unsigned char last_item;
    unsigned char scanned_security;
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
        if (!pending_settings.enabled) {
            /* Disabled: only Enabled(0) is on screen at all. */
            if (last_item == 0)
                pending_settings.enabled = 1;
            screen_pop();
            continue;
        }

        switch (last_item) {
        case 0:
            pending_settings.enabled = 0;
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
                    edit_field_validated("PSK", pending_settings.psk,
                                         sizeof(pending_settings.psk) - 1, 20,
                                         is_valid_psk, invalid_psk_dialog_text,
                                         exit_keys);
                    last_item = 3;
                }
            }
            break;
        case 2:
            edit_field_validated("SSID", pending_settings.ssid,
                                 sizeof(pending_settings.ssid) - 1, 20,
                                 is_valid_always, "", exit_keys);
            break;
        case 3:
            edit_field_validated("PSK", pending_settings.psk,
                                 sizeof(pending_settings.psk) - 1, 20,
                                 is_valid_psk, invalid_psk_dialog_text,
                                 exit_keys);
            break;
        }
        screen_pop();
    }
}
