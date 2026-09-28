#ifndef PFTC_NAV_H
#define PFTC_NAV_H

enum nav_state { NAV_DASHBOARD, NAV_ROOT_MENU, NAV_INTERFACES, NAV_DETAIL, NAV_INFO, NAV_OFFLINE, NAV_EXIT };

/* HELLO handshake - runs once at startup. A transport/protocol error
   here leads to NAV_OFFLINE like any later fetch failure, never an
   early return/exit; the frame stays up and only Ctrl+Q quits. */
enum nav_state do_handshake();

enum nav_state do_dashboard();
enum nav_state do_root_menu();
enum nav_state do_interfaces_list();
enum nav_state do_interface_detail();
enum nav_state do_info_screen();

/* Reached after a transport/protocol error (handshake or dashboard
   fetch) - the app never exits on its own on a failure, it just sits
   offline (frame + "Offline" status, no dashboard values) until the
   user quits explicitly. */
enum nav_state do_offline();

#endif
