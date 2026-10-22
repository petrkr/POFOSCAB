#include "pofo.h"
#include "tinylib.h"
#include "pftc_proto.h"
#include "gui_core.h"
#include "gui.h"
#include "valid.h"
#include "gui_shared.h"

#define WIFISCAN_MENU_MAX_ITEMS 10
#define WIFISCAN_SSID_MAX 32
#define WIFISCAN_SSID_WIDTH 18
#define WIFISCAN_SECURITY_WIDTH 8
#define SIGNAL_BAR_LEVELS 5

static char interface_menu_suffix[] =
    "Status\0Network settings\0IP settings\0Apply\0\0";

static char invalid_psk_dialog_text[] = "PSK must be 8-63 chars, or empty.";

static char security_labels[][WIFISCAN_SECURITY_WIDTH + 1] = {
    "    open", "    WPA2", "    WPA3", "     WEP", "WPA/WPA2",
    "     WPA", "  WPA2/3", "     OWE", "     EAP"
};
static char signal_bars[6][6] = {
    { 0xB0, 0xB0, 0xB0, 0xB0, 0xB0, 0 },
    { 0xDB, 0xB0, 0xB0, 0xB0, 0xB0, 0 },
    { 0xDB, 0xDB, 0xB0, 0xB0, 0xB0, 0 },
    { 0xDB, 0xDB, 0xDB, 0xB0, 0xB0, 0 },
    { 0xDB, 0xDB, 0xDB, 0xDB, 0xB0, 0 },
    { 0xDB, 0xDB, 0xDB, 0xDB, 0xDB, 0 }
};
static signed char signal_thresholds[] = { -80, -70, -60, -50, -40 };

/* Avoids refetching and discarding confirmed edits after a sub-menu. */
static unsigned char interface_menu_initialized;

/* Local copy makes ESC discard all edits from this screen. */
static struct net_settings edit;

enum nav_state do_interface_menu()
{
    char *p;

    if (!interface_menu_initialized) {
        if (fetch_netif_state() != 0)
            return NAV_INTERFACES;
        init_interface_settings();
        interface_menu_initialized = 1;
    }

    p = str_copy(menu_text, netif_type_label(menu_netif.info.type)) + 1;
    mem_copy(p, interface_menu_suffix, sizeof(interface_menu_suffix));

    switch (menu_run(menu_text)) {
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

enum nav_state do_interface_detail()
{
    char *p;
    struct netif_wificli_state *wifi;

    wifi = &menu_netif.ext.wificli;
    p = str_copy(menu_text, "Interface Detail") + 1;

    if (menu_netif.info.type == PFTC_WIFI_CLIENT) {
        if (wifi->fields.ssid_length == 0xFF)
            p = str_copy(p, "SSID: invalid");
        else if (wifi->fields.ssid_length == 0xFE)
            p = str_copy(p, "SSID: unavailable");
        else {
            p = fmt(p, "SSID: %s", wifi->ssid) + 1;
            p = fmt(p, "RSSI: %d dBm", wifi->fields.rssi) + 1;
            p = fmt(p, "Channel: %u", wifi->fields.channel);
        }
    } else
        p = fmt(p, "Interface %x", menu_netif.info.interface);
    p++;

    p = fmt(p, "IP: %i/%u", menu_netif.info.ipv4,
            menu_netif.info.netmask_prefix) + 1;
    p = fmt(p, "GW: %i", menu_netif.info.gateway) + 1;
    p = fmt(p, "DNS: %i", menu_netif.info.dns) + 1;
    *p = 0; /* double zero terminator */

    menu_run(menu_text);
    return NAV_INTERFACE_MENU;
}

char *yesno(flag)
unsigned char flag;
{
    return flag ? "Yes" : "No";
}

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

    len = str_len(text);
    return len == 0 || (len >= 8 && len <= 63);
}

static void build_network_settings_text()
{
    char *p;

    p = str_copy(menu_text, "Network settings") + 1;
    p = fmt(p, "Enabled: %s", yesno(edit.enabled)) + 1;
    if (edit.enabled) {
        p = str_copy(p, "Scan") + 1;
        p = fmt(p, "SSID: %s", edit.ssid) + 1;
        p = fmt(p, "PSK: %s", edit.psk) + 1;
    }
    p = str_copy(p, "Done") + 1;
    *p = 0;
}

static char *security_label(security)
unsigned char security;
{
    if (security <= PFTC_SECURITY_EAP)
        return security_labels[security];
    return "     UNK";
}

static char *signal_bar(rssi)
signed char rssi;
{
    unsigned char level;

    level = 0;
    while (level < SIGNAL_BAR_LEVELS && rssi >= signal_thresholds[level])
        level++;
    return signal_bars[level];
}

static int do_wifi_scan_menu(ssid_out, security_out)
char *ssid_out;
unsigned char *security_out;
{
    unsigned char *p;
    int count, i, ssid_len;
    signed char rssi;
    char *out;
    char *bar;
    unsigned char security;
    unsigned char cells;
    int result;
    unsigned char *entries[WIFISCAN_MENU_MAX_ITEMS];

    wifiscan_request[1] = selected_interface;
    pofo_set_cursor_mode(POFO_CURSOR_OFF);
    result = request("Scanning", wifiscan_request, sizeof(wifiscan_request),
                     3, 30);
    pofo_set_cursor_mode(POFO_CURSOR_BLOCK);
    if (result == 0)
        return 0;

    count = response[2];
    if (count > WIFISCAN_MENU_MAX_ITEMS)
        count = WIFISCAN_MENU_MAX_ITEMS;

    /* Entries of ssid_len, ssid bytes, rssi, security. */
    out = str_copy(menu_text, "Scan results") + 1;
    p = response + 3;
    for (i = 0; i < count; i++) {
        entries[i] = p;
        ssid_len = p[0];
        if (ssid_len > WIFISCAN_SSID_MAX)
            ssid_len = 32;
        rssi = (signed char)p[1 + ssid_len];
        security = p[1 + ssid_len + 1];
        bar = signal_bar(rssi);
        if (ssid_len == 0) {
            mem_copy(out, "<hidden>", 8);
            cells = 8;
        } else {
            cells = ssid_len;
            if (cells > WIFISCAN_SSID_WIDTH)
                cells = WIFISCAN_SSID_WIDTH;
            mem_copy(out, p + 1, cells);
        }
        while (cells < WIFISCAN_SSID_WIDTH)
            out[cells++] = ' ';
        out += WIFISCAN_SSID_WIDTH;
        *out++ = ' ';
        mem_copy(out, security_label(security), WIFISCAN_SECURITY_WIDTH);
        out += WIFISCAN_SECURITY_WIDTH;
        *out++ = ' ';
        mem_copy(out, bar, SIGNAL_BAR_LEVELS);
        out += SIGNAL_BAR_LEVELS;
        *out++ = 0;
        p += 1 + ssid_len + 2;
    }
    *out = 0;

    result = menu_run(menu_text);
    /* menu_run() hid the cursor; the settings menu underneath (and an
       edit field right after) still run with the block cursor. */
    pofo_set_cursor_mode(POFO_CURSOR_BLOCK);
    if (result == -1)
        return 0;

    p = entries[result];
    ssid_len = p[0];
    if (ssid_len > WIFISCAN_SSID_MAX)
        ssid_len = WIFISCAN_SSID_MAX;
    mem_copy(ssid_out, p + 1, ssid_len);
    ssid_out[ssid_len] = 0;
    *security_out = p[1 + ssid_len + 1];
    return 1;
}

static void edit_psk()
{
    edit_field_validated("PSK", edit.psk, sizeof(edit.psk) - 1, 20,
                         is_valid_psk, invalid_psk_dialog_text);
}

enum nav_state do_network_settings()
{
    int item;
    int last_item;
    int done_item;
    unsigned char scanned_security;

    mem_copy(&edit, &interface_settings.net, sizeof(edit));

    pofo_set_cursor_mode(POFO_CURSOR_BLOCK);
    last_item = 0;

    for (;;) {
        if (screen_push(SETTINGS_SCREEN_TOP_LEFT,
                        SETTINGS_SCREEN_BOTTOM_RIGHT) != 0) {
            show_out_of_memory_error();
            return NAV_INTERFACE_MENU;
        }

        build_network_settings_text();
        done_item = edit.enabled ? 4 : 1;

        item = menu_show(menu_text, last_item);

        if (item == -1 || item == done_item) {
            if (item == done_item)
                mem_copy(&interface_settings.net, &edit, sizeof(edit));
            pofo_set_cursor_mode(POFO_CURSOR_OFF);
            screen_pop();
            return NAV_INTERFACE_MENU;
        }

        last_item = item;
        if (!edit.enabled) {
            if (last_item == 0)
                edit.enabled = 1;
            screen_pop();
            continue;
        }

        switch (last_item) {
        case 0:
            edit.enabled = 0;
            break;
        case 1:
            if (do_wifi_scan_menu(edit.ssid, &scanned_security)) {
                last_item = 2;
                if (scanned_security != PFTC_SECURITY_OPEN) {
                    screen_pop();
                    if (screen_push(SETTINGS_SCREEN_TOP_LEFT,
                                    SETTINGS_SCREEN_BOTTOM_RIGHT) != 0) {
                        show_out_of_memory_error();
                        return NAV_INTERFACE_MENU;
                    }
                    edit_psk();
                    last_item = 3;
                }
            }
            break;
        case 2:
            edit_field_validated("SSID", edit.ssid, sizeof(edit.ssid) - 1, 20,
                                 is_valid_always, "");
            break;
        case 3:
            edit_psk();
            break;
        }
        screen_pop();
    }
}
