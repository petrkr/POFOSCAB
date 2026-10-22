#ifndef PFTC_PROTO_H
#define PFTC_PROTO_H

#define PFTC_HELLO        0x01
#define PFTC_GET_NETIFS   0x02
#define PFTC_GET_NETIF    0x03
#define PFTC_SET_NETIF    0x04
#define PFTC_GET_WIFISCAN 0x07
#define PFTC_GET_DASHBOARD 0x08
#define PFTC_WIFI_CLIENT  0x01

/* Every response starts with status (PFTC_STATUS_OK) and error code (0). */
#define PFTC_STATUS_OK 0x20

#define PFTC_DASHBOARD_MODE_FULL 0x00

#define PFTC_IP_MODE_DHCP   0x00
#define PFTC_IP_MODE_STATIC 0x01

#define PFTC_SECURITY_OPEN      0x00
#define PFTC_SECURITY_WPA2_PSK  0x01
#define PFTC_SECURITY_WPA3_PSK  0x02
#define PFTC_SECURITY_WEP       0x03
#define PFTC_SECURITY_WPA_WPA2  0x04
#define PFTC_SECURITY_WPA       0x05
#define PFTC_SECURITY_WPA2_WPA3 0x06
#define PFTC_SECURITY_OWE       0x07
#define PFTC_SECURITY_EAP       0x08
#define PFTC_SECURITY_UNKNOWN   0xFF

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

struct netif_info {
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
    unsigned char ip_mode;
    unsigned char ipv6_enabled;
};

/* type=0x01 (WiFi client) extension fields right after struct
   netif_info in a GET_NETIF response - fixed offsets, since the only
   variable-length part (SSID bytes) comes after this. */
struct netif_wificli_ext {
    unsigned char channel;
    signed char rssi;
    unsigned char ssid_length;
};

extern unsigned char hello_request[1];
extern unsigned char netifs_request[1];
extern unsigned char netif_request[2];
extern unsigned char wifiscan_request[2];
extern unsigned char dashboard_request[2];

/* SET_NETIF request buffer, built in place by apply_netif_settings():
   opcode(1) + interface(1) + type(1) + enabled(1) + ip_mode(1) +
   ip(4) + netmask_prefix(1) + gateway(4) + ipv6(1) + ssid_length(1) +
   ssid(up to 32) + psk_length(1) + psk(up to 64) = 112 bytes max. */
#define SET_NETIF_REQUEST_SIZE 112
extern unsigned char set_netif_request[SET_NETIF_REQUEST_SIZE];

/* One response buffer for every exchange - each caller consumes its
   response before the next request goes out. Sized for the largest,
   GET_WIFISCAN: up to 10 entries (the protocol's own cap), each SSID
   length + up to 32 SSID bytes + RSSI + security = up to 35 bytes, plus
   the 3-byte status/error/count header: 3 + 10*35 = 353, rounded up.
   GET_DASHBOARD FULL (one entry per displayed field, each up to 4 + 38
   bytes) fits comfortably too. */
#define RESPONSE_SIZE 360
extern unsigned char response[RESPONSE_SIZE];

#endif
