#include <stdio.h>
#include <conio.h>
#include "pofo.h"
#include "smartcable.h"

#define PFTC_HELLO       0x01
#define PFTC_GET_NETIFS  0x02
#define PFTC_GET_NETIF   0x03
#define PFTC_WIFI_CLIENT 0x01
#define PFTC_CTRL_Q      0x1011

static unsigned char hello_request[] = { PFTC_HELLO };
static unsigned char netifs_request[] = { PFTC_GET_NETIFS };
static unsigned char netif_request[] = { PFTC_GET_NETIF, 0x00 };
static unsigned char response[64];
static char connecting_dialog[] = "Connecting\0Talking to SmartCable...\0\0";
static char transport_error_dialog[] =
    "SmartCable error\0Transport failed.\0\0";
static char protocol_error_dialog[] =
    "PFTC error\0Invalid response.\0\0";
static char status_text[40];
#define STATUS_TOP_LEFT     POFO_COORD(7, 0)
#define STATUS_BOTTOM_RIGHT POFO_COORD(7, 39)
static unsigned char status_screen_buffer[POFO_SCREEN_SIZE(STATUS_TOP_LEFT, STATUS_BOTTOM_RIGHT)];

/* Only pofo_message_dialog (AH=12h) needs manual save/restore - the ROM
   erases pofo_error_dialog (AH=14h) itself on keypress. Sized for the
   widest/tallest message dialog actually shown (connecting_dialog). */
#define DIALOG_TOP_LEFT POFO_COORD(2, 2)
static unsigned char dialog_screen_buffer[POFO_SCREEN_SIZE(DIALOG_TOP_LEFT, POFO_COORD(5, 27))];

static status_set(text)
char *text;
{
    pofo_screen_restore(STATUS_TOP_LEFT, STATUS_BOTTOM_RIGHT, status_screen_buffer);
    gotoxy(2, 7);
    printf(" %.*s ", 34, text);
    fflush(stdout);
}

static wait_and_exit(status)
int status;
{
    while (getch() != PFTC_CTRL_Q)
        ;
    pofo_clear_screen();
    return status;
}

struct hello_response {
    unsigned char status;
    unsigned char error;
    unsigned char magic[4];
    unsigned char build_id[4];
    unsigned char version_major;
    unsigned char version_minor;
    unsigned char version_patch;
    unsigned char reserved;
};

struct netif_entry {
    unsigned char interface;
    unsigned char type;
    unsigned char enabled;
};

struct netifs_response {
    unsigned char status;
    unsigned char error;
    unsigned char count;
    struct netif_entry entries[1];
};

struct netif_response {
    unsigned char status;
    unsigned char error;
    unsigned char interface;
    unsigned char type;
    unsigned char enabled;
    unsigned char connection_state;
    unsigned char ipv4[4];
    unsigned char netmask_prefix;
    unsigned char gateway[4];
    unsigned char dns[4];
};

static print_ipv4(address)
unsigned char *address;
{
    printf("%u.%u.%u.%u", address[0], address[1], address[2], address[3]);
}

static show_transport_error()
{
    status_set("Offline");
    pofo_error_dialog(POFO_COORD(3, 4), transport_error_dialog);
}

static show_protocol_error()
{
    status_set("Offline");
    pofo_error_dialog(POFO_COORD(3, 4), protocol_error_dialog);
}

int main()
{
    unsigned int received;
    unsigned int entries_size;
    unsigned int wifi_size;
    unsigned char *wifi;
    unsigned char ssid_length;
    unsigned char channel;
    int status;
    struct hello_response *hello;
    struct netifs_response *netifs;
    struct netif_response *netif;

    pofo_clear_screen();
    pofo_draw_box(POFO_COORD(0, 0), POFO_COORD(7, 39));
    pofo_screen_save(STATUS_TOP_LEFT, STATUS_BOTTOM_RIGHT, status_screen_buffer);

    gotoxy(2, 0);
    printf("PFTC");
    fflush(stdout);
    pofo_hide_cursor();
    status_set("Connecting");
    pofo_screen_save(DIALOG_TOP_LEFT,
                      pofo_dialog_extent(DIALOG_TOP_LEFT, connecting_dialog),
                      dialog_screen_buffer);
    pofo_message_dialog(DIALOG_TOP_LEFT, connecting_dialog);

    status = smartcable_exchange(hello_request, sizeof(hello_request),
                           response, sizeof(response), &received);

    pofo_screen_restore(DIALOG_TOP_LEFT,
                         pofo_dialog_extent(DIALOG_TOP_LEFT, connecting_dialog),
                         dialog_screen_buffer);

    if (status != 0) {
        show_transport_error();
        return wait_and_exit(status);
    }
    if (received < sizeof(struct hello_response)) {
        show_protocol_error();
        return wait_and_exit(1);
    }
    hello = (struct hello_response *)response;
    if (hello->error != 0) {
        show_protocol_error();
        return wait_and_exit(1);
    }

    gotoxy(2, 1);
    printf("Connected v%u.%u.%u", hello->version_major,
           hello->version_minor, hello->version_patch);
    fflush(stdout);
    sprintf(status_text, "Connected v%u.%u.%u (%02X%02X%02X%02X)",
            hello->version_major, hello->version_minor, hello->version_patch,
            hello->build_id[3], hello->build_id[2], hello->build_id[1],
            hello->build_id[0]);
    status_set(status_text);

    smartcable_wait_500ms();

    status = smartcable_exchange(netifs_request, sizeof(netifs_request),
                           response, sizeof(response), &received);
    if (status != 0) {
        show_transport_error();
        return wait_and_exit(status);
    }
    if (received < 3) {
        show_protocol_error();
        return wait_and_exit(1);
    }
    netifs = (struct netifs_response *)response;
    entries_size = received - 3;
    if (netifs->error != 0 || netifs->count == 0 ||
        netifs->count > entries_size / sizeof(struct netif_entry)) {
        show_protocol_error();
        return wait_and_exit(1);
    }

    netif_request[1] = netifs->entries[0].interface;
    status = smartcable_exchange(netif_request, sizeof(netif_request),
                           response, sizeof(response), &received);
    if (status != 0) {
        show_transport_error();
        return wait_and_exit(status);
    }
    if (received < sizeof(struct netif_response)) {
        show_protocol_error();
        return wait_and_exit(1);
    }
    netif = (struct netif_response *)response;
    if (netif->error != 0) {
        show_protocol_error();
        return wait_and_exit(1);
    }

    gotoxy(2, 2);
    channel = 0;
    if (netif->type == PFTC_WIFI_CLIENT) {
        wifi = response + sizeof(struct netif_response);
        wifi_size = received - sizeof(struct netif_response);
        if (wifi_size >= 3) {
            ssid_length = wifi[0];
            if (wifi_size >= (unsigned int)ssid_length + 3) {
                printf("SSID: %.*s", ssid_length, wifi + 1);
                channel = wifi[ssid_length + 1];
            } else
                printf("SSID: invalid");
        } else
            printf("SSID: unavailable");
    } else
        printf("Interface %02X", netif->interface);
    fflush(stdout);

    gotoxy(2, 3);
    printf("IP: ");
    print_ipv4(netif->ipv4);
    fflush(stdout);
    gotoxy(2, 4);
    printf("GW: ");
    print_ipv4(netif->gateway);
    fflush(stdout);
    gotoxy(2, 5);
    printf("DNS: ");
    print_ipv4(netif->dns);
    fflush(stdout);
    gotoxy(2, 6);
    printf("IF%02X /%u CH%u", netif->interface, netif->netmask_prefix,
           channel);
    fflush(stdout);

    return wait_and_exit(0);
}
