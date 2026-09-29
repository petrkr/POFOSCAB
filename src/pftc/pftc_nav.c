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
#define PFTC_ESC         0x011B
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

    selected = POFO_COORD_COL(result);
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

    selected_interface = interfaces_menu_interface[POFO_COORD_COL(result)];
    return NAV_INTERFACE_MENU;
}

#define INTERFACE_MENU_TOP_LEFT POFO_COORD(1, 2)
#define INTERFACE_MENU_HEIGHT_LIMIT 7
#define INTERFACE_MENU_TYPE_DEPTH \
    ((INTERFACE_MENU_HEIGHT_LIMIT << 3) | POFO_BOX_DOUBLE)

static char interface_menu_text[64];

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
    strcpy(p, "Status");
    p += strlen(p) + 1;
    strcpy(p, "Network settings");
    p += strlen(p) + 1;
    strcpy(p, "IP settings");
    p += strlen(p) + 1;
    *p = 0;

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

    switch (POFO_COORD_COL(result)) {
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
    strcpy(p, "Interface Detail");
    p += strlen(p) + 1;

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
        sprintf(p, "RSSI: %d dBm", netif_rssi);
        p += strlen(p) + 1;
        sprintf(p, "Channel: %u", netif_channel);
        p += strlen(p) + 1;
    }

    sprintf(p, "IP: %u.%u.%u.%u/%u", netif_ipv4[0], netif_ipv4[1],
            netif_ipv4[2], netif_ipv4[3], netif_netmask_prefix);
    p += strlen(p) + 1;
    sprintf(p, "GW: %u.%u.%u.%u", netif_gateway[0], netif_gateway[1],
            netif_gateway[2], netif_gateway[3]);
    p += strlen(p) + 1;
    sprintf(p, "DNS: %u.%u.%u.%u", netif_dns[0], netif_dns[1],
            netif_dns[2], netif_dns[3]);
    p += strlen(p) + 1;

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

static char not_supported_dialog_text[] = "Not supported yet.";

#define SETTINGS_MENU_TOP_LEFT POFO_COORD(1, 2)
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

static void build_network_settings_text()
{
    char *p;
    char line[SETTINGS_FIELD_MAX];

    p = network_settings_menu_text;
    strcpy(p, "Network settings");
    p += strlen(p) + 1;
    sprintf(line, "Enabled: %s", yesno(network_settings_enabled));
    strcpy(p, line);
    p += strlen(p) + 1;
    sprintf(line, "SSID: %s", network_settings_ssid);
    strcpy(p, line);
    p += strlen(p) + 1;
    sprintf(line, "PSK: %s", network_settings_psk);
    strcpy(p, line);
    p += strlen(p) + 1;
    strcpy(p, "Apply");
    p += strlen(p) + 1;
    *p = 0;
}

/* Skeleton only - fields are edited and held in memory, but Apply has
   no SET_NETIF wired up yet (the backend doesn't support it either) -
   reports "Not supported yet." instead of sending anything. Reads
   whatever do_interface_menu() last fetched into netif_*; no re-fetch
   of its own. */
enum nav_state do_network_settings()
{
    unsigned int bottom_right;
    int result;
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
    for (;;) {
        build_network_settings_text();

        pofo_menu_getsize(SETTINGS_MENU_TOP_LEFT, network_settings_menu_text,
                          0, &bottom_right);
        if (screen_push(SETTINGS_MENU_TOP_LEFT, bottom_right) != 0) {
            pofo_hide_cursor();
            show_out_of_memory_error();
            return NAV_INTERFACE_MENU;
        }

        result = pofo_menu_show(SETTINGS_MENU_TOP_LEFT,
                                network_settings_menu_text, 0, 0, 0,
                                SETTINGS_MENU_TYPE_DEPTH);
        screen_pop();

        if (result == -1) {
            pofo_hide_cursor();
            return NAV_INTERFACE_MENU;
        }

        switch (POFO_COORD_COL(result)) {
        case 0:
            network_settings_enabled = !network_settings_enabled;
            break;
        case 1:
            pofo_line_edit(SETTINGS_MENU_TOP_LEFT, "SSID", "",
                          network_settings_ssid,
                          sizeof(network_settings_ssid) - 1, 34,
                          POFO_EDIT_MODE_CLEAR_ON_ENTRY, POFO_EDIT_BOX_DOUBLE,
                          exit_keys);
            break;
        case 2:
            pofo_line_edit(SETTINGS_MENU_TOP_LEFT, "PSK", "",
                          network_settings_psk,
                          sizeof(network_settings_psk) - 1, 34,
                          POFO_EDIT_MODE_CLEAR_ON_ENTRY, POFO_EDIT_BOX_DOUBLE,
                          exit_keys);
            break;
        case 3:
            pofo_hide_cursor();
            pofo_error_dialog(DIALOG_TOP_LEFT, not_supported_dialog_text);
            return NAV_INTERFACE_MENU;
        }
    }
}

static char ip_settings_menu_text[SETTINGS_FIELD_MAX * 5 + 32];
static char ip_settings_ip[16];
static char ip_settings_netmask[16];
static char ip_settings_gateway[16];
static unsigned char ip_settings_mode_static;
static unsigned char ip_settings_ipv6;

static void build_ip_settings_text()
{
    char *p;
    char line[SETTINGS_FIELD_MAX];

    p = ip_settings_menu_text;
    strcpy(p, "IP settings");
    p += strlen(p) + 1;
    sprintf(line, "Mode: %s", ip_settings_mode_static ? "Static" : "DHCP");
    strcpy(p, line);
    p += strlen(p) + 1;
    sprintf(line, "IP: %s", ip_settings_ip);
    strcpy(p, line);
    p += strlen(p) + 1;
    sprintf(line, "Netmask: %s", ip_settings_netmask);
    strcpy(p, line);
    p += strlen(p) + 1;
    sprintf(line, "Gateway: %s", ip_settings_gateway);
    strcpy(p, line);
    p += strlen(p) + 1;
    sprintf(line, "IPv6: %s", yesno(ip_settings_ipv6));
    strcpy(p, line);
    p += strlen(p) + 1;
    strcpy(p, "Apply");
    p += strlen(p) + 1;
    *p = 0;
}

/* Skeleton only - same as do_network_settings(): fields are held in
   memory, Apply reports "Not supported yet." instead of sending
   SET_IPCFG. Starts from netif_*'s runtime IP/netmask/gateway as a
   read-only-looking default, not from an actual GET_IPCFG fetch (no
   IPCFG wire code exists yet). */
enum nav_state do_ip_settings()
{
    unsigned int bottom_right;
    int result;
    unsigned int exit_keys[3];

    exit_keys[0] = 0x000D;
    exit_keys[1] = 0x001B;
    exit_keys[2] = 0;

    sprintf(ip_settings_ip, "%u.%u.%u.%u", netif_ipv4[0], netif_ipv4[1],
            netif_ipv4[2], netif_ipv4[3]);
    sprintf(ip_settings_netmask, "%u", netif_netmask_prefix);
    sprintf(ip_settings_gateway, "%u.%u.%u.%u", netif_gateway[0],
            netif_gateway[1], netif_gateway[2], netif_gateway[3]);
    ip_settings_mode_static = 0;
    ip_settings_ipv6 = 0;

    pofo_show_cursor();
    for (;;) {
        build_ip_settings_text();

        pofo_menu_getsize(SETTINGS_MENU_TOP_LEFT, ip_settings_menu_text,
                          0, &bottom_right);
        

        if (screen_push(SETTINGS_MENU_TOP_LEFT, bottom_right) != 0) {
            pofo_hide_cursor();
            show_out_of_memory_error();
            return NAV_INTERFACE_MENU;
        }

        result = pofo_menu_show(SETTINGS_MENU_TOP_LEFT,
                                ip_settings_menu_text, 0, 0, 0,
                                SETTINGS_MENU_TYPE_DEPTH);
        screen_pop();

        if (result == -1) {
            pofo_hide_cursor();
            return NAV_INTERFACE_MENU;
        }

        switch (POFO_COORD_COL(result)) {
        case 0:
            ip_settings_mode_static = !ip_settings_mode_static;
            break;
        case 1:
            pofo_line_edit(SETTINGS_MENU_TOP_LEFT, "IP", "",
                          ip_settings_ip, sizeof(ip_settings_ip) - 1, 17,
                          POFO_EDIT_MODE_CLEAR_ON_ENTRY, POFO_EDIT_BOX_DOUBLE,
                          exit_keys);
            break;
        case 2:
            pofo_line_edit(SETTINGS_MENU_TOP_LEFT, "Netmask", "",
                          ip_settings_netmask,
                          sizeof(ip_settings_netmask) - 1, 17,
                          POFO_EDIT_MODE_CLEAR_ON_ENTRY, POFO_EDIT_BOX_DOUBLE,
                          exit_keys);
            break;
        case 3:
            pofo_line_edit(SETTINGS_MENU_TOP_LEFT, "Gateway", "",
                          ip_settings_gateway,
                          sizeof(ip_settings_gateway) - 1, 17,
                          POFO_EDIT_MODE_CLEAR_ON_ENTRY, POFO_EDIT_BOX_DOUBLE,
                          exit_keys);
            break;
        case 4:
            ip_settings_ipv6 = !ip_settings_ipv6;
            break;
        case 5:
            pofo_hide_cursor();
            pofo_error_dialog(DIALOG_TOP_LEFT, not_supported_dialog_text);
            return NAV_INTERFACE_MENU;
        }
    }
}

enum nav_state do_info_screen()
{
    return NAV_ROOT_MENU;
}
