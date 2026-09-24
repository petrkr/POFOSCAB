#include <stdio.h>
#include <conio.h>
#include "pofo.h"
#include "smartcable.h"

#define PFTC_HELLO       0x01
#define PFTC_GET_NETIFS  0x02
#define PFTC_GET_NETIF   0x03
#define PFTC_WIFI_CLIENT 0x01

static unsigned char hello_request[] = { PFTC_HELLO };
static unsigned char netifs_request[] = { PFTC_GET_NETIFS };
static unsigned char netif_request[] = { PFTC_GET_NETIF, 0x00 };
static unsigned char response[64];

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

static show_transport_error(status)
int status;
{
    gotoxy(2, 1);
    printf("Transport error %02X", status);
    fflush(stdout);
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
    gotoxy(2, 0);
    printf("PFTC");
    fflush(stdout);
    gotoxy(2, 1);
    printf("Connecting...");
    fflush(stdout);

    status = smartcable_exchange(hello_request, sizeof(hello_request),
                           response, sizeof(response), &received);
    if (status != 0) {
        show_transport_error(status);
        return status;
    }
    if (received < sizeof(struct hello_response)) {
        puts("Short HELLO response");
        return 1;
    }
    hello = (struct hello_response *)response;
    if (hello->error != 0) {
        printf("HELLO error %02X", hello->error);
        return 1;
    }

    gotoxy(2, 1);
    printf("Connected v%u.%u.%u", hello->version_major,
           hello->version_minor, hello->version_patch);
    fflush(stdout);

    smartcable_wait_500ms();

    status = smartcable_exchange(netifs_request, sizeof(netifs_request),
                           response, sizeof(response), &received);
    if (status != 0) {
        show_transport_error(status);
        return status;
    }
    if (received < 3) {
        puts("Short NETIFS response");
        return 1;
    }
    netifs = (struct netifs_response *)response;
    entries_size = received - 3;
    if (netifs->error != 0 || netifs->count == 0 ||
        netifs->count > entries_size / sizeof(struct netif_entry)) {
        puts("No interface data");
        return 1;
    }

    netif_request[1] = netifs->entries[0].interface;
    status = smartcable_exchange(netif_request, sizeof(netif_request),
                           response, sizeof(response), &received);
    if (status != 0) {
        show_transport_error(status);
        return status;
    }
    if (received < sizeof(struct netif_response)) {
        puts("Short NETIF response");
        return 1;
    }
    netif = (struct netif_response *)response;
    if (netif->error != 0) {
        printf("NETIF error %02X", netif->error);
        return 1;
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

    return 0;
}
