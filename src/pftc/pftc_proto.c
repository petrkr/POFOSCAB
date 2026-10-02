#include "pftc_proto.h"

unsigned char hello_request[] = { PFTC_HELLO };
unsigned char netifs_request[] = { PFTC_GET_NETIFS };
unsigned char netif_request[] = { PFTC_GET_NETIF, 0x00 };
unsigned char wifiscan_request[] = { PFTC_GET_WIFISCAN, 0x00 };
unsigned char dashboard_request[] = { PFTC_GET_DASHBOARD, PFTC_DASHBOARD_MODE_FULL };
unsigned char response[64];
unsigned char set_netif_request[SET_NETIF_REQUEST_SIZE];
unsigned char wifiscan_response[WIFISCAN_RESPONSE_SIZE];
unsigned char dashboard_response[DASHBOARD_RESPONSE_SIZE];
