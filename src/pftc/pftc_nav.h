#ifndef PFTC_NAV_H
#define PFTC_NAV_H

enum nav_state {
    NAV_DASHBOARD, NAV_ROOT_MENU, NAV_INTERFACES, NAV_INTERFACE_MENU,
    NAV_DETAIL, NAV_NETWORK_SETTINGS, NAV_IP_SETTINGS, NAV_INFO, NAV_EXIT
};

/* HELLO handshake - runs once at startup, and again from the root
   menu's Reconnect item. A transport/protocol error here never exits
   the app; do_dashboard() just has no data to show until it succeeds. */
enum nav_state do_handshake();

/* Renders the dashboard from whatever do_handshake()/do_fetch_netif()
   last fetched - "Offline" (frame + status only) if nothing has
   succeeded yet. Never fetches on its own; F5 or the root menu's
   Reconnect trigger a fetch explicitly. */
enum nav_state do_dashboard();
enum nav_state do_root_menu();
enum nav_state do_interfaces_list();
enum nav_state do_interface_menu();
enum nav_state do_interface_detail();
enum nav_state do_network_settings();
enum nav_state do_ip_settings();
enum nav_state do_info_screen();

#endif
