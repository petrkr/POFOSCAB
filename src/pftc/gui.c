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

#define PFTC_CTRL_Q      0x1011
#define PFTC_F9          0x4300
#define PFTC_F5          0x3F00
#define PFTC_ATARI       0x3B00

#define NAV_MENU_TOP_LEFT POFO_COORD(1, 2)
/* Rows 1-7, intentionally covering the status line too; screen_push/pop
   restores it once the menu closes. */
#define NAV_MENU_HEIGHT_LIMIT 7
#define NAV_MENU_TYPE_DEPTH ((NAV_MENU_HEIGHT_LIMIT << 3) | POFO_BOX_DOUBLE)

#define INTERFACES_MENU_TOP_LEFT POFO_COORD(1, 2)
#define INTERFACES_MENU_HEIGHT_LIMIT 7
#define INTERFACES_MENU_TYPE_DEPTH \
    ((INTERFACES_MENU_HEIGHT_LIMIT << 3) | POFO_BOX_DOUBLE)
#define INTERFACES_MENU_MAX_ITEMS 8

static char status_text[40];

/* Stashed from HELLO for do_info_screen(); valid only when hello_ok. */
static unsigned char hello_ok;
static unsigned char hello_version_major;
static unsigned char hello_version_minor;
static unsigned char hello_version_patch;
static unsigned char hello_build_id[4];

/* Dashboard's own fetch; see struct netif_wificli in gui_shared.h. */
unsigned char dashboard_netif_ok;
struct netif_wificli dashboard_netif;

/* Interface menu's own fetch; scoped to the menu's lifetime. */
struct netif_wificli menu_netif;

unsigned char selected_interface;

static char root_menu_text[] = "PFTC\0Interfaces\0Reconnect\0Info\0Exit\0\0";
static char interfaces_menu_text[INTERFACES_MENU_MAX_ITEMS * 16 + 16];
static unsigned char interfaces_menu_interface[INTERFACES_MENU_MAX_ITEMS];
static char edit_backup[65];

char not_supported_dialog_text[] = "Not supported yet.";
static char offline_dialog_text[] = "Offline - try Reconnect.";

/* Fetches GET_NETIF for `interface` into `*out`. Returns 0 on success,
   non-zero on error (already reported); *out is untouched on failure
   so a failed refresh keeps old data. */
int fetch_netif(interface, out)
unsigned char interface;
struct netif_wificli *out;
{
    unsigned int received;
    unsigned int wifi_size;
    unsigned char *wifi;
    int status;
    struct netif_info *netif;

    netif_request[1] = interface;
    progress_dialog_open("Getting interface");
    status = smartcable_exchange(netif_request, sizeof(netif_request),
                           response, sizeof(response), &received);
    progress_dialog_close();
    if (status != 0) {
        show_transport_error();
        return 1;
    }
    if (received < sizeof(struct netif_info)) {
        show_protocol_error();
        return 1;
    }
    netif = (struct netif_info *)response;
    if (netif->error != 0) {
        show_protocol_error();
        return 1;
    }

    out->info = *netif;

    out->channel = 0;
    out->rssi = -128;
    out->ssid_length = 0;
    if (netif->type == PFTC_WIFI_CLIENT) {
        wifi = response + sizeof(struct netif_info);
        wifi_size = received - sizeof(struct netif_info);
        if (wifi_size >= 3) {
            out->channel = wifi[0];
            out->rssi = (signed char)wifi[1];
            out->ssid_length = wifi[2];
            if (out->ssid_length > sizeof(out->ssid) - 1)
                out->ssid_length = sizeof(out->ssid) - 1;
            if (wifi_size >= (unsigned int)wifi[2] + 3) {
                memcpy(out->ssid, wifi + 3, out->ssid_length);
                out->ssid[out->ssid_length] = 0;
            } else
                out->ssid_length = 0xFF; /* sentinel: "invalid" */
        } else
            out->ssid_length = 0xFE; /* sentinel: "unavailable" */
    }

    return 0;
}

/* Returns 0 on success, non-zero on error (already reported).
   dashboard_netif_ok stays untouched on failure so a failed refresh
   keeps old data. */
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

    if (fetch_netif(netifs->entries[0].interface, &dashboard_netif) != 0)
        return 1;
    dashboard_netif_ok = 1;
    return 0;
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
    memcpy(hello_build_id, hello->build_id, sizeof(hello_build_id));
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
    if (!dashboard_netif_ok) {
        status_set("Offline");
        return;
    }

    gotoxy(2, 1);
    if (dashboard_netif.info.type == PFTC_WIFI_CLIENT) {
        if (dashboard_netif.ssid_length == 0xFF)
            printf("SSID: invalid");
        else if (dashboard_netif.ssid_length == 0xFE)
            printf("SSID: unavailable");
        else {
            printf("SSID: %s", dashboard_netif.ssid);
            fflush(stdout);
            gotoxy(38 - SIGNAL_BAR_LEVELS - 3, 1);
            putchar(' ');
            putchar((unsigned char)0xB3);
            putchar(' ');
            print_signal_bar(dashboard_netif.rssi);
        }
    } else
        printf("Interface %02X", dashboard_netif.info.interface);
    fflush(stdout);

    gotoxy(2, 2);
    printf("IP: ");
    print_ipv4(dashboard_netif.info.ipv4);
    printf("/%u", dashboard_netif.info.netmask_prefix);
    fflush(stdout);
    gotoxy(2, 3);
    printf("GW: ");
    print_ipv4(dashboard_netif.info.gateway);
    fflush(stdout);
    gotoxy(2, 4);
    printf("DNS: ");
    print_ipv4(dashboard_netif.info.dns);
    fflush(stdout);
    gotoxy(2, 5);
    printf("CH%u", dashboard_netif.channel);
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
        if (!dashboard_netif_ok) {
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

char *netif_type_label(type)
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
        p += sprintf(p, netif_type_label(netifs->entries[i].type)) + 1;
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

/* Shared body for every "edit a field, validate, retry on bad input,
   ESC restores the pre-edit value" prompt in Network/IP settings -
   replaces five near-identical copies of the same loop. validator
   returns non-zero for an acceptable value; error_text is shown and
   the same field re-opened (KEEP_ON_ENTRY, so the bad text stays
   visible) until it passes or the user backs out with ESC. */
void edit_field_validated(title, value, max, width, validator,
                                 error_text, exit_keys)
char *title;
char *value;
unsigned int max;
unsigned char width;
int (*validator)();
char *error_text;
unsigned int *exit_keys;
{
    int result;

    strcpy(edit_backup, value);
    for (;;) {
        result = pofo_line_edit(SETTINGS_EDIT_TOP_LEFT, title, "", value,
                                max, width, POFO_EDIT_MODE_CLEAR_ON_ENTRY,
                                POFO_EDIT_BOX_DOUBLE, exit_keys);
        if (result == 0x001B) {
            strcpy(value, edit_backup);
            return;
        }
        if (validator(value))
            return;
        pofo_error_dialog(DIALOG_TOP_LEFT, error_text);
    }
}
