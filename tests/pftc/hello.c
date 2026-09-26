#include <stdio.h>
#include <conio.h>
#include "pofo.h"
#include "smartcable.h"

static unsigned char hello_request[] = { 0x01 };
static unsigned char hello_packet[32];
static char hex[] = "0123456789ABCDEF";

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

static print_hex(value)
unsigned int value;
{
    putchar(hex[(value >> 4) & 0x0f]);
    putchar(hex[value & 0x0f]);
}

static print_build_id(value)
unsigned char *value;
{
    print_hex(value[3]);
    print_hex(value[2]);
    print_hex(value[1]);
    print_hex(value[0]);
}

int main()
{
    unsigned int received;
    int status;
    struct hello_response *hello;

    pofo_draw_box(POFO_COORD(0, 0), POFO_COORD(7, 39));
    gotoxy(1, 1);
    puts("HELLO:");

    status = smartcable_exchange(hello_request, 1,
                           hello_packet, sizeof(hello_packet),
                           &received);
    if (status != 0) {
        puts("ERROR:");
        print_hex(status);
        return status;
    }

    if (received < sizeof(struct hello_response)) {
        puts("SHORT RESPONSE");
        return 1;
    }

    hello = (struct hello_response *)hello_packet;

    printf("STATUS: %02X\n", hello->status);
    printf("ERROR:  %02X\n", hello->error);
    printf("MAGIC:  %.4s\n", hello->magic);
    printf("BUILD:  ");
    print_build_id(hello->build_id);
    putchar('\n');
    printf("VERSION:%u.%u.%u\n", hello->version_major,
           hello->version_minor, hello->version_patch);
    printf("RESVD:  %02X\n", hello->reserved);

    return 0;
}
