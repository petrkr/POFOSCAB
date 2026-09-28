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
static int do_fetch_netif()
{
    unsigned int received;
    unsigned int entries_size;
    unsigned int wifi_size;
    unsigned char *wifi;
    int status;
    struct netifs_response *netifs;
    struct netif_response *netif;

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

    netif_request[1] = netifs->entries[0].interface;
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

enum nav_state do_interfaces_list()
{
    return NAV_ROOT_MENU;
}

enum nav_state do_interface_detail()
{
    return NAV_INTERFACES;
}

enum nav_state do_info_screen()
{
    return NAV_ROOT_MENU;
}
