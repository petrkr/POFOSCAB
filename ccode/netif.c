#include <stdio.h>
#include <conio.h>
#include "pofo.h"
#include "smartcable.h"

#define PFTC_GET_NETIF 0x03
#define PFTC_NETIF_WIFI_CLIENT 0x01

static unsigned char netif_request[] = { PFTC_GET_NETIF, 0x00 };
static unsigned char netif_packet[64];

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

int main()
{
    unsigned int received;
    unsigned int wifi_size;
    unsigned char *wifi;
    unsigned char ssid_length;
    int status;
    struct netif_response *netif;

    pofo_draw_box(POFO_COORD(0, 0), POFO_COORD(7, 39));
    gotoxy(1, 1);
    puts("NETIF 00:");

    status = smartcable_exchange(netif_request, sizeof(netif_request),
                           netif_packet, sizeof(netif_packet),
                           &received);
    if (status != 0) {
        printf("TRANSPORT: %02X\n", status);
        return status;
    }

    if (received < sizeof(struct netif_response)) {
        puts("SHORT RESPONSE");
        return 1;
    }

    netif = (struct netif_response *)netif_packet;
    printf("S%02X E%02X I%02X T%02X\n", netif->status, netif->error,
           netif->interface, netif->type);
    printf("EN%02X CS%02X /%u\n", netif->enabled,
           netif->connection_state, netif->netmask_prefix);
    printf("IP: ");
    print_ipv4(netif->ipv4);
    putchar('\n');
    printf("GW: ");
    print_ipv4(netif->gateway);
    putchar('\n');
    printf("DNS:");
    print_ipv4(netif->dns);
    putchar('\n');

    if (netif->type != PFTC_NETIF_WIFI_CLIENT)
        return 0;

    wifi = netif_packet + sizeof(struct netif_response);
    wifi_size = received - sizeof(struct netif_response);
    if (wifi_size < 3) {
        puts("SHORT WIFI DATA");
        return 1;
    }

    ssid_length = wifi[0];
    if (wifi_size < (unsigned int)ssid_length + 3) {
        puts("BAD SSID LENGTH");
        return 1;
    }

    printf("SSID: %.*s\n", ssid_length, wifi + 1);
    printf("CH%u RSSI%d\n", wifi[ssid_length + 1],
           (signed char)wifi[ssid_length + 2]);
    return 0;
}
