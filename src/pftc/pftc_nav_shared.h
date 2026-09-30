#ifndef PFTC_NAV_SHARED_H
#define PFTC_NAV_SHARED_H

/* Internal cross-module state and helpers shared between pftc_nav.c,
   pftc_iface.c and pftc_ip.c. Not part of the public pftc_nav.h API. */

/* Network/IP settings screens share the same menu geometry and field
   size limit. */
#define SETTINGS_MENU_TOP_LEFT POFO_COORD(1, 2)
#define SETTINGS_EDIT_TOP_LEFT POFO_COORD(3, 12)
#define SETTINGS_MENU_HEIGHT_LIMIT 7
#define SETTINGS_MENU_TYPE_DEPTH \
    ((SETTINGS_MENU_HEIGHT_LIMIT << 3) | POFO_BOX_DOUBLE)
#define SETTINGS_FIELD_MAX 64

/* Last successful GET_NETIFS/GET_NETIF fetch; valid only when netif_ok.
   Populated by do_fetch_netif()/fetch_netif() in pftc_nav.c. */
extern unsigned char netif_ok;
extern unsigned char netif_interface;
extern unsigned char netif_type;
extern unsigned char netif_ipv4[4];
extern unsigned char netif_netmask_prefix;
extern unsigned char netif_gateway[4];
extern unsigned char netif_dns[4];
extern unsigned char netif_channel;
extern int netif_rssi;
extern unsigned char netif_ssid_length;
extern char netif_ssid[64];

extern unsigned char selected_interface;

extern char not_supported_dialog_text[];

/* Re-fetches selected_interface. Returns 0 on success, non-zero on
   error (already reported). netif_ok stays untouched on failure. */
int fetch_netif();

char *netif_type_label();

/* "Yes"/"No" for a boolean flag - shared between pftc_iface.c's
   Network settings and pftc_ip.c's IP settings IPv6 toggle. */
char *yesno();

/* Shared body for every "edit a field, validate, retry on bad input,
   ESC restores the pre-edit value" prompt in Network/IP settings. */
void edit_field_validated();

#endif
