#include <stdio.h>
#include <conio.h>
#include "pofo.h"
#include "smartcable.h"

#define PFTC_GET_NETIFS 0x02

static unsigned char netifs_request[] = { PFTC_GET_NETIFS };
static unsigned char netifs_packet[32];

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

int main()
{
    unsigned int received;
    unsigned int index;
    unsigned int entries_size;
    int status;
    struct netifs_response *netifs;

    pofo_draw_box(POFO_COORD(0, 0), POFO_COORD(7, 39));
    gotoxy(1, 1);
    puts("NETIFS:");

    status = smartcable_exchange(netifs_request, sizeof(netifs_request),
                           netifs_packet, sizeof(netifs_packet),
                           &received);
    if (status != 0) {
        printf("TRANSPORT: %02X\n", status);
        return status;
    }

    if (received < 3) {
        puts("SHORT RESPONSE");
        return 1;
    }

    netifs = (struct netifs_response *)netifs_packet;
    entries_size = received - 3;
    if (netifs->count > entries_size / sizeof(struct netif_entry)) {
        puts("MALFORMED RESPONSE");
        return 1;
    }

    printf("STATUS: %02X\n", netifs->status);
    printf("ERROR:  %02X\n", netifs->error);
    printf("COUNT:  %u\n", netifs->count);

    for (index = 0; index < netifs->count; ++index) {
        printf("IF%02X T%02X EN%02X\n",
               netifs->entries[index].interface,
               netifs->entries[index].type,
               netifs->entries[index].enabled);
    }

    return 0;
}
