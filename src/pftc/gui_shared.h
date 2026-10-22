#ifndef PFTC_GUI_SHARED_H
#define PFTC_GUI_SHARED_H

/* Internal cross-module state and helpers shared between gui.c,
   gui_iface.c and gui_ip.c. Not part of the public gui.h API. */

/* Network/IP settings screens save this whole area for the duration
   of one menu round trip (menu plus any edit/error dialog over it). */
#define SETTINGS_SCREEN_TOP_LEFT     POFO_COORD(1, 1)
#define SETTINGS_SCREEN_BOTTOM_RIGHT POFO_COORD(7, 38)
#define SETTINGS_EDIT_TOP_LEFT       POFO_COORD(3, 12)

extern unsigned char dashboard_ok;

struct netif_wificli_state {
    struct netif_wificli_ext fields;
    char ssid[64];
};

/* Last GET_NETIF result; valid until returning to Interfaces. */
struct netif_state {
    struct netif_info info;
    union {
        struct netif_wificli_state wificli;
    } ext;
};
extern struct netif_state menu_netif;

extern unsigned char selected_interface;

/* Shared pending SET_NETIF; screen edits commit here only on Done.
   Split per settings screen, so each screen works on a plain copy of
   its own part (one mem_copy in, one back on Done, ESC just drops it). */
struct net_settings {
    unsigned char enabled;
    char ssid[33];
    char psk[65];
};

struct ip_settings {
    unsigned char ip_mode;
    char ip[16];
    char prefix[4];
    char gateway[16];
    unsigned char ipv6_enabled;
};

struct netif_settings {
    struct net_settings net;
    struct ip_settings ip;
};
extern struct netif_settings interface_settings;

void init_interface_settings();

int apply_netif_settings();

extern char not_supported_dialog_text[];

/* GET_NETIF for selected_interface into menu_netif. Does not modify
   menu_netif on failure (already reported). Returns 0 on success. */
int fetch_netif_state();

/* menu_netif is a WiFi client whose SSID was received intact. */
int netif_has_ssid();

char *netif_type_label();

char *yesno();

void edit_field_validated();

#endif
