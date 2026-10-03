#ifndef PFTC_GUI_SHARED_H
#define PFTC_GUI_SHARED_H

/* Internal cross-module state and helpers shared between gui.c,
   gui_iface.c and gui_ip.c. Not part of the public gui.h API. */

/* Network/IP settings screens share the same menu geometry and field
   size limit. */
#define SETTINGS_MENU_TOP_LEFT POFO_COORD(1, 2)
#define SETTINGS_EDIT_TOP_LEFT POFO_COORD(3, 12)
#define SETTINGS_MENU_HEIGHT_LIMIT 7
#define SETTINGS_MENU_TYPE_DEPTH \
    ((SETTINGS_MENU_HEIGHT_LIMIT << 3) | POFO_BOX_DOUBLE)
#define SETTINGS_FIELD_MAX 64

extern unsigned char dashboard_ok;

/* Last GET_NETIF result; valid until returning to Interfaces. */
struct netif_state {
    struct netif_info info;
    union {
        struct {
            struct netif_wificli_ext fields;
            char ssid[64];
        } wificli;
    } ext;
};
extern struct netif_state menu_netif;

extern unsigned char selected_interface;

/* Shared pending SET_NETIF; screen edits commit here only on Done. */
struct netif_settings {
    unsigned char enabled;
    char ssid[33];
    char psk[65];
    unsigned char ip_mode;
    char ip[16];
    char prefix[3];
    char gateway[16];
    unsigned char ipv6_enabled;
};
extern struct netif_settings interface_settings;

void init_interface_settings();

int apply_netif_settings();

extern char not_supported_dialog_text[];

int fetch_netif_state();

char *netif_type_label();

char *yesno();

void edit_field_validated();

#endif
