#include <stdio.h>
#include <conio.h>
#include "pofo.h"
#include "smartcable.h"
#include "pftc_proto.h"
#include "pftc_gui.h"
#include "pftc_nav.h"

#define PFTC_CTRL_Q      0x1011
#define PFTC_F9          0x4300
#define PFTC_F5          0x3F00
#define PFTC_ESC         0x011B
/* Portfolio's dedicated Atari-logo key; also opens the root menu,
   alongside F9. */
#define PFTC_ATARI       0x3B00

static char status_text[40];

/* Stashed from the HELLO handshake so do_info_screen() (added in a
   later step) can show it without re-issuing HELLO. */
static unsigned char hello_version_major;
static unsigned char hello_version_minor;
static unsigned char hello_version_patch;
static unsigned char hello_build_id[4];

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
        return NAV_OFFLINE;
    }
    if (received < sizeof(struct hello_response)) {
        show_protocol_error();
        return NAV_OFFLINE;
    }
    hello = (struct hello_response *)response;
    if (hello->error != 0) {
        show_protocol_error();
        return NAV_OFFLINE;
    }

    hello_version_major = hello->version_major;
    hello_version_minor = hello->version_minor;
    hello_version_patch = hello->version_patch;
    hello_build_id[0] = hello->build_id[0];
    hello_build_id[1] = hello->build_id[1];
    hello_build_id[2] = hello->build_id[2];
    hello_build_id[3] = hello->build_id[3];

    sprintf(status_text, "Connected v%u.%u.%u (%02X%02X%02X%02X)",
            hello_version_major, hello_version_minor, hello_version_patch,
            hello_build_id[3], hello_build_id[2], hello_build_id[1],
            hello_build_id[0]);
    status_set(status_text);

    smartcable_wait_500ms();
    return NAV_DASHBOARD;
}

/* Fetches GET_NETIFS/GET_NETIF and renders the dashboard (SSID/signal,
   IP/GW/DNS, channel), then blocks on getch(): F9/Atari -> NAV_ROOT_MENU,
   Ctrl+Q -> NAV_EXIT, else stays on NAV_DASHBOARD. A transport/protocol
   error during the fetch shows the same error dialog as before and moves
   to NAV_OFFLINE - the app never exits on its own. F5 manual refresh is
   wired in a later step (PFTC.md SS6 step 7); for now it falls into the
   "else stays" case like any other key. */
enum nav_state do_dashboard()
{
    unsigned int received;
    unsigned int entries_size;
    unsigned int wifi_size;
    unsigned char *wifi;
    unsigned char ssid_length;
    unsigned char channel;
    int rssi;
    int status;
    int key;
    struct netifs_response *netifs;
    struct netif_response *netif;

    progress_dialog_open("Getting interfaces");
    status = smartcable_exchange(netifs_request, sizeof(netifs_request),
                           response, sizeof(response), &received);
    progress_dialog_close();
    if (status != 0) {
        show_transport_error();
        return NAV_OFFLINE;
    }
    if (received < 3) {
        show_protocol_error();
        return NAV_OFFLINE;
    }
    netifs = (struct netifs_response *)response;
    entries_size = received - 3;
    if (netifs->error != 0 || netifs->count == 0 ||
        netifs->count > entries_size / sizeof(struct netif_entry)) {
        show_protocol_error();
        return NAV_OFFLINE;
    }

    netif_request[1] = netifs->entries[0].interface;
    progress_dialog_open("Getting interface");
    status = smartcable_exchange(netif_request, sizeof(netif_request),
                           response, sizeof(response), &received);
    progress_dialog_close();
    if (status != 0) {
        show_transport_error();
        return NAV_OFFLINE;
    }
    if (received < sizeof(struct netif_response)) {
        show_protocol_error();
        return NAV_OFFLINE;
    }
    netif = (struct netif_response *)response;
    if (netif->error != 0) {
        show_protocol_error();
        return NAV_OFFLINE;
    }

    gotoxy(2, 1);
    channel = 0;
    rssi = -128;
    if (netif->type == PFTC_WIFI_CLIENT) {
        wifi = response + sizeof(struct netif_response);
        wifi_size = received - sizeof(struct netif_response);
        if (wifi_size >= 3) {
            channel = wifi[0];
            rssi = (signed char)wifi[1];
            ssid_length = wifi[2];
            if (wifi_size >= (unsigned int)ssid_length + 3) {
                printf("SSID: %.*s", ssid_length, wifi + 3);
                fflush(stdout);
                gotoxy(38 - SIGNAL_BAR_LEVELS - 3, 1);
                putchar(' ');
                putchar((unsigned char)0xB3);
                putchar(' ');
                print_signal_bar(rssi);
            } else
                printf("SSID: invalid");
        } else
            printf("SSID: unavailable");
    } else
        printf("Interface %02X", netif->interface);
    fflush(stdout);

    gotoxy(2, 2);
    printf("IP: ");
    print_ipv4(netif->ipv4);
    printf("/%u", netif->netmask_prefix);
    fflush(stdout);
    gotoxy(2, 3);
    printf("GW: ");
    print_ipv4(netif->gateway);
    fflush(stdout);
    gotoxy(2, 4);
    printf("DNS: ");
    print_ipv4(netif->dns);
    fflush(stdout);
    gotoxy(2, 5);
    printf("CH%u", channel);
    fflush(stdout);

    for (;;) {
        key = getch();
        if (key == PFTC_F9 || key == PFTC_ATARI)
            return NAV_ROOT_MENU;
        if (key == PFTC_CTRL_Q)
            return NAV_EXIT;
    }
}

/* Stubs for steps not yet implemented (PFTC.md SS6 steps 3-6) - each
   just bounces back to a sensible state so the loop is complete and
   testable before the real screens land. */
enum nav_state do_root_menu()
{
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

enum nav_state do_offline()
{
    while (getch() != PFTC_CTRL_Q)
        ;
    return NAV_EXIT;
}
