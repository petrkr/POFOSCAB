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

/* Dashboard's drawn/not-drawn state - set by do_fetch_dashboard()
   (GET_DASHBOARD FULL, via handshake/Reconnect/F5). The dashboard
   itself carries no client-side copy of the fetched fields anymore -
   entries are drawn as received, at the ESP-given position. */
extern unsigned char dashboard_ok;

/* Interface menu's own last-fetched state (GET_NETIF on
   selected_interface, via fetch_netif_state() at do_interface_menu()
   entry) - scoped to the menu's lifetime; read by Interface
   detail/Network settings/IP settings, gone once back out to
   Interfaces list.

   Generic across interface types: info.type (from GET_NETIF's common
   header, always present) says which ext union member is valid - only
   wificli exists today, since WiFi client (type=0x01) is the only type
   with an extension section so far. A future AP/Ethernet extension
   would add its own member here without disturbing this one. */
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

/* Interface menu's editable settings (enabled/ssid/psk from Network
   settings, ip_mode/ip/prefix/gateway/ipv6 from IP settings) - read by
   Interface menu's Apply, all editing a single SET_NETIF, so there is
   one copy, not two. Filled from menu_netif at do_interface_menu()
   entry and overwritten only by each screen's Done (never by a
   mid-edit ESC, which just discards that screen's own malloc'd
   working copy - see struct net_edit/ip_edit in gui_iface.c/
   gui_ip.c). ip/prefix/gateway are kept as text (not yet parsed to
   bytes) since they're edited as text fields; Apply parses them via
   parse_ipv4()/prefix_to_netmask(). */
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

/* Fills interface_settings from menu_netif - called once at
   do_interface_menu() entry, before Network/IP settings can be
   opened. */
void init_interface_settings();

/* Builds and sends SET_NETIF from interface_settings for
   selected_interface. Returns 0 on success (already reported via
   status dialog), non-zero on error (already reported). */
int apply_netif_settings();

extern char not_supported_dialog_text[];

/* Fetches GET_NETIF for `interface` into `*out`, generic across
   interface types - see struct netif_state. */
int fetch_netif_state();

char *netif_type_label();

/* "Yes"/"No" for a boolean flag - shared between gui_iface.c's
   Network settings and gui_ip.c's IP settings IPv6 toggle. */
char *yesno();

/* Shared body for every "edit a field, validate, retry on bad input,
   ESC restores the pre-edit value" prompt in Network/IP settings. */
void edit_field_validated();

#endif
