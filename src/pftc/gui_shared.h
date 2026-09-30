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

/* GET_NETIF's struct netif_info plus its WiFi client extension
   fields (channel/rssi/ssid), parsed separately in fetch_netif() from
   the bytes right after the fixed struct. ssid_length 0xFF/0xFE are
   sentinels ("invalid"/"unavailable" - see fetch_netif()). */
struct netif_wificli {
    struct netif_info info;
    unsigned char channel;
    int rssi;
    unsigned char ssid_length;
    char ssid[64];
};

/* Dashboard's own last-fetched state (GET_NETIF on interface [0], via
   do_fetch_netif()/handshake or F5) - independent of menu_netif below.
   Valid only when dashboard_netif_ok. */
extern unsigned char dashboard_netif_ok;
extern struct netif_wificli dashboard_netif;

/* Interface menu's own last-fetched state (GET_NETIF on
   selected_interface, via fetch_netif() at do_interface_menu() entry) -
   scoped to the menu's lifetime; read by Interface detail/Network
   settings/IP settings, gone once back out to Interfaces list. */
extern struct netif_wificli menu_netif;

extern unsigned char selected_interface;

extern char not_supported_dialog_text[];

/* Fetches GET_NETIF for `interface` into `*out`. Returns 0 on success,
   non-zero on error (already reported); *out is untouched on failure
   so a failed refresh keeps old data. */
int fetch_netif();

char *netif_type_label();

/* "Yes"/"No" for a boolean flag - shared between gui_iface.c's
   Network settings and gui_ip.c's IP settings IPv6 toggle. */
char *yesno();

/* Shared body for every "edit a field, validate, retry on bad input,
   ESC restores the pre-edit value" prompt in Network/IP settings. */
void edit_field_validated();

#endif
