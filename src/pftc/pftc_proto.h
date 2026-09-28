#ifndef PFTC_PROTO_H
#define PFTC_PROTO_H

#define PFTC_HELLO       0x01
#define PFTC_GET_NETIFS  0x02
#define PFTC_GET_NETIF   0x03
#define PFTC_WIFI_CLIENT 0x01

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

extern unsigned char hello_request[1];
extern unsigned char netifs_request[1];
extern unsigned char netif_request[2];
extern unsigned char response[64];

#endif
