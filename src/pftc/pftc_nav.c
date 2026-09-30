#include <stdio.h>
#include <conio.h>
#include <string.h>
#include "pofo.h"
#include "smartcable.h"
#include "pftc_proto.h"
#include "pftc_gui.h"
#include "pftc_nav.h"

#define PFTC_CTRL_Q      0x1011
#define PFTC_F9          0x4300
#define PFTC_F5          0x3F00
#define PFTC_ATARI       0x3B00

static char status_text[40];

/* Stashed from HELLO for do_info_screen(); valid only when hello_ok. */
static unsigned char hello_ok;
static unsigned char hello_version_major;
static unsigned char hello_version_minor;
static unsigned char hello_version_patch;
static unsigned char hello_build_id[4];

/* Last successful GET_NETIFS/GET_NETIF fetch; valid only when netif_ok.
   Populated by do_fetch_netif(), rendered by draw_dashboard(). */
static unsigned char netif_ok;
static unsigned char netif_interface;
static unsigned char netif_type;
static unsigned char netif_ipv4[4];
static unsigned char netif_netmask_prefix;
static unsigned char netif_gateway[4];
static unsigned char netif_dns[4];
static unsigned char netif_channel;
static int netif_rssi;
static unsigned char netif_ssid_length;
static char netif_ssid[64];

static char not_supported_dialog_text[] = "Not supported yet.";

/* Returns 0 on success, non-zero on error (already reported). netif_ok
   stays untouched on failure so a failed refresh keeps old data. */
static int fetch_netif(interface)
unsigned char interface;
{
    unsigned int received;
    unsigned int wifi_size;
    unsigned char *wifi;
    int status;
    struct netif_response *netif;

    netif_request[1] = interface;
    progress_dialog_open("Getting interface");
    status = smartcable_exchange(netif_request, sizeof(netif_request),
                           response, sizeof(response), &received);
    progress_dialog_close();
    if (status != 0) {
        show_transport_error();
        return 1;
    }
    if (received < sizeof(struct netif_response)) {
        show_protocol_error();
        return 1;
    }
    netif = (struct netif_response *)response;
    if (netif->error != 0) {
        show_protocol_error();
        return 1;
    }

    netif_interface = netif->interface;
    netif_type = netif->type;
    netif_ipv4[0] = netif->ipv4[0];
    netif_ipv4[1] = netif->ipv4[1];
    netif_ipv4[2] = netif->ipv4[2];
    netif_ipv4[3] = netif->ipv4[3];
    netif_netmask_prefix = netif->netmask_prefix;
    netif_gateway[0] = netif->gateway[0];
    netif_gateway[1] = netif->gateway[1];
    netif_gateway[2] = netif->gateway[2];
    netif_gateway[3] = netif->gateway[3];
    netif_dns[0] = netif->dns[0];
    netif_dns[1] = netif->dns[1];
    netif_dns[2] = netif->dns[2];
    netif_dns[3] = netif->dns[3];

    netif_channel = 0;
    netif_rssi = -128;
    netif_ssid_length = 0;
    if (netif->type == PFTC_WIFI_CLIENT) {
        wifi = response + sizeof(struct netif_response);
        wifi_size = received - sizeof(struct netif_response);
        if (wifi_size >= 3) {
            netif_channel = wifi[0];
            netif_rssi = (signed char)wifi[1];
            netif_ssid_length = wifi[2];
            if (netif_ssid_length > sizeof(netif_ssid) - 1)
                netif_ssid_length = sizeof(netif_ssid) - 1;
            if (wifi_size >= (unsigned int)wifi[2] + 3) {
                memcpy(netif_ssid, wifi + 3, netif_ssid_length);
                netif_ssid[netif_ssid_length] = 0;
            } else
                netif_ssid_length = 0xFF; /* sentinel: "invalid" */
        } else
            netif_ssid_length = 0xFE; /* sentinel: "unavailable" */
    }

    netif_ok = 1;
    return 0;
}

/* Returns 0 on success, non-zero on error (already reported). netif_ok
   stays untouched on failure so a failed refresh keeps old data. */
static int do_fetch_netif()
{
    unsigned int received;
    unsigned int entries_size;
    int status;
    struct netifs_response *netifs;

    progress_dialog_open("Getting interfaces");
    status = smartcable_exchange(netifs_request, sizeof(netifs_request),
                           response, sizeof(response), &received);
    progress_dialog_close();
    if (status != 0) {
        show_transport_error();
        return 1;
    }
    if (received < 3) {
        show_protocol_error();
        return 1;
    }
    netifs = (struct netifs_response *)response;
    entries_size = received - 3;
    if (netifs->error != 0 || netifs->count == 0 ||
        netifs->count > entries_size / sizeof(struct netif_entry)) {
        show_protocol_error();
        return 1;
    }

    return fetch_netif(netifs->entries[0].interface);
}

enum nav_state do_handshake()
{
    unsigned int received;
    int status;
    struct hello_response *hello;

    status_set("Connecting");
    progress_dialog_open("Connecting");

    status = smartcable_exchange(hello_request, sizeof(hello_request),
                           response, sizeof(response), &received);

    progress_dialog_close();

    if (status != 0) {
        show_transport_error();
        return NAV_DASHBOARD;
    }
    if (received < sizeof(struct hello_response)) {
        show_protocol_error();
        return NAV_DASHBOARD;
    }
    hello = (struct hello_response *)response;
    if (hello->error != 0) {
        show_protocol_error();
        return NAV_DASHBOARD;
    }

    hello_version_major = hello->version_major;
    hello_version_minor = hello->version_minor;
    hello_version_patch = hello->version_patch;
    hello_build_id[0] = hello->build_id[0];
    hello_build_id[1] = hello->build_id[1];
    hello_build_id[2] = hello->build_id[2];
    hello_build_id[3] = hello->build_id[3];
    hello_ok = 1;

    sprintf(status_text, "Connected v%u.%u.%u (%02X%02X%02X%02X)",
            hello_version_major, hello_version_minor, hello_version_patch,
            hello_build_id[3], hello_build_id[2], hello_build_id[1],
            hello_build_id[0]);
    status_set(status_text);

    smartcable_wait_500ms();
    do_fetch_netif();
    return NAV_DASHBOARD;
}

static void draw_dashboard()
{
    if (!netif_ok) {
        status_set("Offline");
        return;
    }

    gotoxy(2, 1);
    if (netif_type == PFTC_WIFI_CLIENT) {
        if (netif_ssid_length == 0xFF)
            printf("SSID: invalid");
        else if (netif_ssid_length == 0xFE)
            printf("SSID: unavailable");
        else {
            printf("SSID: %s", netif_ssid);
            fflush(stdout);
            gotoxy(38 - SIGNAL_BAR_LEVELS - 3, 1);
            putchar(' ');
            putchar((unsigned char)0xB3);
            putchar(' ');
            print_signal_bar(netif_rssi);
        }
    } else
        printf("Interface %02X", netif_interface);
    fflush(stdout);

    gotoxy(2, 2);
    printf("IP: ");
    print_ipv4(netif_ipv4);
    printf("/%u", netif_netmask_prefix);
    fflush(stdout);
    gotoxy(2, 3);
    printf("GW: ");
    print_ipv4(netif_gateway);
    fflush(stdout);
    gotoxy(2, 4);
    printf("DNS: ");
    print_ipv4(netif_dns);
    fflush(stdout);
    gotoxy(2, 5);
    printf("CH%u", netif_channel);
    fflush(stdout);
}

/* Never fetches on its own - F5 re-fetches, do_handshake()/Reconnect
   populate the initial data. */
enum nav_state do_dashboard()
{
    int key;

    draw_dashboard();

    for (;;) {
        key = getch();
        if (key == PFTC_F9 || key == PFTC_ATARI)
            return NAV_ROOT_MENU;
        if (key == PFTC_F5) {
            do_fetch_netif();
            draw_dashboard();
            continue;
        }
        if (key == PFTC_CTRL_Q)
            return NAV_EXIT;
    }
}

#define NAV_MENU_TOP_LEFT POFO_COORD(1, 2)
/* Rows 1-7, intentionally covering the status line too; screen_push/pop
   restores it once the menu closes. */
#define NAV_MENU_HEIGHT_LIMIT 7
#define NAV_MENU_TYPE_DEPTH ((NAV_MENU_HEIGHT_LIMIT << 3) | POFO_BOX_DOUBLE)

static char root_menu_text[] = "PFTC\0Interfaces\0Reconnect\0Info\0Exit\0\0";

static char offline_dialog_text[] = "Offline - try Reconnect.";

/* ESC -> NAV_DASHBOARD. Interfaces without a fetch yet shows an
   "Offline" dialog and stays in the menu instead of navigating in. */
enum nav_state do_root_menu()
{
    unsigned int bottom_right;
    int result;
    unsigned char selected;

    pofo_menu_getsize(NAV_MENU_TOP_LEFT, root_menu_text, 0, &bottom_right);
    if (screen_push(NAV_MENU_TOP_LEFT, bottom_right) != 0) {
        show_out_of_memory_error();
        return NAV_DASHBOARD;
    }

    pofo_show_cursor();
    result = pofo_menu_show(NAV_MENU_TOP_LEFT, root_menu_text, 0, 0, 0,
                            NAV_MENU_TYPE_DEPTH);
    pofo_hide_cursor();
    screen_pop();

    if (result == -1)
        return NAV_DASHBOARD;

    selected = POFO_LOW_BYTE(result);
    switch (selected) {
    case 0:
        if (!netif_ok) {
            pofo_error_dialog(NAV_MENU_TOP_LEFT, offline_dialog_text);
            return NAV_ROOT_MENU;
        }
        return NAV_INTERFACES;
    case 1:
        return do_handshake();
    case 2:
        return NAV_INFO;
    case 3:
        pofo_clear_screen();
        return NAV_EXIT;
    }
    return NAV_DASHBOARD;
}

#define INTERFACES_MENU_TOP_LEFT POFO_COORD(1, 2)
#define INTERFACES_MENU_HEIGHT_LIMIT 7
#define INTERFACES_MENU_TYPE_DEPTH \
    ((INTERFACES_MENU_HEIGHT_LIMIT << 3) | POFO_BOX_DOUBLE)
#define INTERFACES_MENU_MAX_ITEMS 8

static char interfaces_menu_text[INTERFACES_MENU_MAX_ITEMS * 16 + 16];
static unsigned char interfaces_menu_interface[INTERFACES_MENU_MAX_ITEMS];
static unsigned char selected_interface;

static char *netif_type_label(type)
unsigned char type;
{
    switch (type) {
    case 0x01: return "WiFi client";
    case 0x02: return "WiFi AP";
    case 0x03: return "Ethernet";
    }
    return "Unknown";
}

enum nav_state do_interfaces_list()
{
    unsigned int received;
    int status;
    struct netifs_response *netifs;
    unsigned char i, count;
    char *p;
    unsigned int bottom_right;
    int result;

    progress_dialog_open("Getting interfaces");
    status = smartcable_exchange(netifs_request, sizeof(netifs_request),
                           response, sizeof(response), &received);
    progress_dialog_close();
    if (status != 0) {
        show_transport_error();
        return NAV_ROOT_MENU;
    }
    if (received < 3) {
        show_protocol_error();
        return NAV_ROOT_MENU;
    }
    netifs = (struct netifs_response *)response;
    if (netifs->error != 0 || netifs->count == 0 ||
        netifs->count > (received - 3) / sizeof(struct netif_entry)) {
        show_protocol_error();
        return NAV_ROOT_MENU;
    }

    count = netifs->count;
    if (count > INTERFACES_MENU_MAX_ITEMS)
        count = INTERFACES_MENU_MAX_ITEMS;

    p = interfaces_menu_text;
    *p++ = 0; /* no title */
    for (i = 0; i < count; i++) {
        interfaces_menu_interface[i] = netifs->entries[i].interface;
        strcpy(p, netif_type_label(netifs->entries[i].type));
        p += strlen(p) + 1;
    }
    *p = 0; /* double zero terminator */

    pofo_menu_getsize(INTERFACES_MENU_TOP_LEFT, interfaces_menu_text, 0,
                       &bottom_right);
    if (screen_push(INTERFACES_MENU_TOP_LEFT, bottom_right) != 0) {
        show_out_of_memory_error();
        return NAV_ROOT_MENU;
    }

    pofo_show_cursor();
    result = pofo_menu_show(INTERFACES_MENU_TOP_LEFT, interfaces_menu_text,
                            0, 0, 0, INTERFACES_MENU_TYPE_DEPTH);
    pofo_hide_cursor();
    screen_pop();

    if (result == -1)
        return NAV_ROOT_MENU;

    selected_interface = interfaces_menu_interface[POFO_LOW_BYTE(result)];
    return NAV_INTERFACE_MENU;
}

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


#define SETTINGS_MENU_TOP_LEFT POFO_COORD(1, 2)
#define SETTINGS_EDIT_TOP_LEFT POFO_COORD(3, 12)

static char edit_backup[65];

/* Shared body for every "edit a field, validate, retry on bad input,
   ESC restores the pre-edit value" prompt in Network/IP settings -
   replaces five near-identical copies of the same loop. validator
   returns non-zero for an acceptable value; error_text is shown and
   the same field re-opened (KEEP_ON_ENTRY, so the bad text stays
   visible) until it passes or the user backs out with ESC. */
static void edit_field_validated(title, value, max, width, validator,
                                 error_text, exit_keys)
char *title;
char *value;
unsigned int max;
unsigned char width;
int (*validator)();
char *error_text;
unsigned int *exit_keys;
{
    unsigned char mode;
    int result;

    strcpy(edit_backup, value);
    mode = POFO_EDIT_MODE_CLEAR_ON_ENTRY;
    for (;;) {
        result = pofo_line_edit(SETTINGS_EDIT_TOP_LEFT, title, "", value,
                                max, width, mode, POFO_EDIT_BOX_DOUBLE,
                                exit_keys);
        if (result == 0x001B) {
            strcpy(value, edit_backup);
            return;
        }
        if (validator(value))
            return;
        pofo_error_dialog(DIALOG_TOP_LEFT, error_text);
        mode = POFO_EDIT_MODE_KEEP_ON_ENTRY;
    }
}

#define SETTINGS_MENU_HEIGHT_LIMIT 7
#define SETTINGS_MENU_TYPE_DEPTH \
    ((SETTINGS_MENU_HEIGHT_LIMIT << 3) | POFO_BOX_DOUBLE)
#define SETTINGS_FIELD_MAX 64

static char network_settings_menu_text[SETTINGS_FIELD_MAX * 3 + 32];
static char network_settings_ssid[33];
static char network_settings_psk[65];
static unsigned char network_settings_enabled;

static char *yesno(flag)
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
/* Full parse: 4 dot-separated octets, each 0-255. Hand-written in asm -
   the equivalent C loop (digit accumulation, octet/digit counters,
   three exit conditions) costs noticeably more in the small memory
   model's calling convention than the same logic written directly. */
static int is_valid_ipv4(text)
char *text;
{
#asm
    push si
    push di
    push bp
    mov bx,sp
    mov si,8[bx]
    xor bx,bx
.ipv4_octet:
    xor bp,bp
    xor cx,cx
.ipv4_digit:
    mov al,[si]
    cmp al,#$30
    jb .ipv4_digit_done
    cmp al,#$39
    ja .ipv4_digit_done
    cmp cl,#3
    jae .ipv4_fail
    sub al,#$30
    xor ah,ah
    mov di,ax
    mov ax,bp
    mov bp,#10
    mul bp
    add ax,di
    mov bp,ax
    inc cl
    inc si
    jmp .ipv4_digit
.ipv4_digit_done:
    or cl,cl
    jz .ipv4_fail
    cmp bp,#255
    ja .ipv4_fail
    inc bl
    mov al,[si]
    or al,al
    jz .ipv4_done
    cmp al,#$2E
    jne .ipv4_fail
    cmp bl,#4
    je .ipv4_fail
    inc si
    jmp .ipv4_octet
.ipv4_done:
    cmp bl,#4
    jne .ipv4_fail
    mov ax,#1
    jmp .ipv4_exit
.ipv4_fail:
    xor ax,ax
.ipv4_exit:
    pop bp
    pop di
    pop si
#endasm
}

static int is_valid_prefix(text)
char *text;
{
    unsigned int value;
    unsigned char digit_count;

    value = 0;
    digit_count = 0;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        digit_count++;
        text++;
    }
    return digit_count > 0 && *text == 0 && value <= 32;
}

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
