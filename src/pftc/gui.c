#include <stdio.h>
#include <conio.h>
#include <string.h>
#include "pofo.h"
#include "smartcable.h"
#include "pftc_proto.h"
#include "gui_core.h"
#include "gui.h"
#include "valid.h"
#include "gui_shared.h"

#define PFTC_CTRL_Q      0x1011
#define PFTC_F9          0x4300
#define PFTC_F5          0x3F00
#define PFTC_ATARI       0x3B00

#define NAV_MENU_TOP_LEFT POFO_COORD(1, 2)
/* Rows 1-7, intentionally covering the status line too; screen_push/pop
   restores it once the menu closes. */
#define NAV_MENU_HEIGHT_LIMIT 7
#define NAV_MENU_TYPE_DEPTH ((NAV_MENU_HEIGHT_LIMIT << 3) | POFO_BOX_DOUBLE)

#define INTERFACES_MENU_TOP_LEFT POFO_COORD(1, 2)
#define INTERFACES_MENU_HEIGHT_LIMIT 7
#define INTERFACES_MENU_TYPE_DEPTH \
    ((INTERFACES_MENU_HEIGHT_LIMIT << 3) | POFO_BOX_DOUBLE)
#define INTERFACES_MENU_MAX_ITEMS 8

static char status_text[40];

/* Stashed from HELLO for do_info_screen(); valid only when hello_ok. */
static unsigned char hello_ok;
static unsigned char hello_version_major;
static unsigned char hello_version_minor;
static unsigned char hello_version_patch;
static unsigned char hello_build_id[4];

/* Dashboard's own fetch - GET_DASHBOARD FULL entries, drawn as-is at
   their given position. Valid only when dashboard_ok. */
unsigned char dashboard_ok;

/* Interface menu's own fetch; scoped to the menu's lifetime. */
struct netif_state menu_netif;

unsigned char selected_interface;

/* Interface menu's editable settings, shared across Network/IP
   settings and applied together by do_interface_menu()'s Apply - see
   struct netif_settings in gui_shared.h. */
struct netif_settings interface_settings;

static char root_menu_text[] = "PFTC\0Interfaces\0Reconnect\0Info\0Exit\0\0";
static char interfaces_menu_text[INTERFACES_MENU_MAX_ITEMS * 16 + 16];
static unsigned char interfaces_menu_interface[INTERFACES_MENU_MAX_ITEMS];
static char edit_backup[65];

char not_supported_dialog_text[] = "Not supported yet.";
static char offline_dialog_text[] = "Offline - try Reconnect.";

/* Fetches GET_NETIF for `interface` into `*out`, generic across
   interface types - see struct netif_state in gui_shared.h. Returns 0
   on success, non-zero on error (already reported); *out is untouched
   on failure so a failed refresh keeps old data. */
int fetch_netif_state(interface, out)
unsigned char interface;
struct netif_state *out;
{
    unsigned int received;
    unsigned int wifi_size;
    unsigned char *ssid_bytes;
    int status;
    struct netif_info *netif;
    struct netif_wificli_ext *ext;

    netif_request[1] = interface;
    progress_dialog_open("Getting interface");
    status = smartcable_exchange(netif_request, sizeof(netif_request),
                           response, sizeof(response), &received);
    progress_dialog_close();
    if (status != 0) {
        show_transport_error();
        return 1;
    }
    if (received < sizeof(struct netif_info)) {
        show_protocol_error();
        return 1;
    }
    netif = (struct netif_info *)response;
    if (netif->error != 0) {
        show_protocol_error();
        return 1;
    }

    out->info = *netif;

    if (netif->type == PFTC_WIFI_CLIENT) {
        out->ext.wificli.fields.channel = 0;
        out->ext.wificli.fields.rssi = -128;
        out->ext.wificli.fields.ssid_length = 0;
        wifi_size = received - sizeof(struct netif_info);
        if (wifi_size >= sizeof(struct netif_wificli_ext)) {
            ext = (struct netif_wificli_ext *)(response + sizeof(struct netif_info));
            out->ext.wificli.fields = *ext;
            if (out->ext.wificli.fields.ssid_length > sizeof(out->ext.wificli.ssid) - 1)
                out->ext.wificli.fields.ssid_length = sizeof(out->ext.wificli.ssid) - 1;
            if (wifi_size >= sizeof(struct netif_wificli_ext) + ext->ssid_length) {
                ssid_bytes = response + sizeof(struct netif_info) +
                             sizeof(struct netif_wificli_ext);
                memcpy(out->ext.wificli.ssid, ssid_bytes,
                       out->ext.wificli.fields.ssid_length);
                out->ext.wificli.ssid[out->ext.wificli.fields.ssid_length] = 0;
            } else
                out->ext.wificli.fields.ssid_length = 0xFF; /* sentinel: "invalid" */
        } else
            out->ext.wificli.fields.ssid_length = 0xFE; /* sentinel: "unavailable" */
    }

    return 0;
}

/* Draws one GET_DASHBOARD FULL entry at its given position - no
   layout/formatting decisions here, the ESP already picked position
   and content; this is just a dumb writer. */
static void draw_dashboard_entry(row, col, len, payload)
unsigned char row;
unsigned char col;
unsigned char len;
unsigned char *payload;
{
    gotoxy(col, row);
    fwrite(payload, 1, len, stdout);
    fflush(stdout);
}

/* Fetches GET_DASHBOARD (mode=FULL) and draws every entry as received.
   Returns 0 on success, non-zero on error (already reported);
   dashboard_ok stays untouched on failure so a failed refresh keeps
   whatever was last drawn on screen. */
static int do_fetch_dashboard()
{
    unsigned int received;
    unsigned char *p, *end;
    unsigned char id, row, col, len;
    int status;

    progress_dialog_open("Getting dashboard");
    status = smartcable_exchange(dashboard_request, sizeof(dashboard_request),
                           dashboard_response, sizeof(dashboard_response), &received);
    progress_dialog_close();
    if (status != 0) {
        show_transport_error();
        return 1;
    }
    if (received < 2 || dashboard_response[0] != 0x20) {
        show_protocol_error();
        return 1;
    }

    p = dashboard_response + 2;
    end = dashboard_response + received;
    while (p < end) {
        if (p + 4 > end)
            break; /* truncated entry header */
        id = p[0];
        row = p[1];
        col = p[2];
        len = p[3];
        p += 4;
        if (p + len > end)
            break; /* truncated payload */

        draw_dashboard_entry(row, col, len, p);
        p += len;
        (void)id; /* not used yet - no PARTIAL refresh/targeting so far */
    }

    dashboard_ok = 1;
    return 0;
}

enum nav_state do_handshake()
{
    unsigned int received;
    int status;
    struct hello_response *hello;

    status_set("Connecting");
    progress_dialog_open("Connecting");

    status = smartcable_exchange(hello_request, sizeof(hello_request),
                           response, sizeof(response), &received);

    progress_dialog_close();

    if (status != 0) {
        show_transport_error();
        return NAV_DASHBOARD;
    }
    if (received < sizeof(struct hello_response)) {
        show_protocol_error();
        return NAV_DASHBOARD;
    }
    hello = (struct hello_response *)response;
    if (hello->error != 0) {
        show_protocol_error();
        return NAV_DASHBOARD;
    }

    hello_version_major = hello->version_major;
    hello_version_minor = hello->version_minor;
    hello_version_patch = hello->version_patch;
    memcpy(hello_build_id, hello->build_id, sizeof(hello_build_id));
    hello_ok = 1;

    sprintf(status_text, "Connected v%u.%u.%u (%02X%02X%02X%02X)",
            hello_version_major, hello_version_minor, hello_version_patch,
            hello_build_id[3], hello_build_id[2], hello_build_id[1],
            hello_build_id[0]);
    status_set(status_text);

    smartcable_wait_500ms();
    do_fetch_dashboard();
    return NAV_DASHBOARD;
}

/* Never fetches on its own - F5 re-fetches, do_handshake()/Reconnect
   populate the initial data. */
enum nav_state do_dashboard()
{
    int key;

    if (!dashboard_ok)
        status_set("Offline");

    for (;;) {
        key = getch();
        if (key == PFTC_F9 || key == PFTC_ATARI)
            return NAV_ROOT_MENU;
        if (key == PFTC_F5) {
            do_fetch_dashboard();
            continue;
        }
        if (key == PFTC_CTRL_Q)
            return NAV_EXIT;
    }
}

/* ESC -> NAV_DASHBOARD. Interfaces without a fetch yet shows an
   "Offline" dialog and stays in the menu instead of navigating in. */
enum nav_state do_root_menu()
{
    unsigned int bottom_right;
    int result;
    unsigned char selected;

    pofo_menu_getsize(NAV_MENU_TOP_LEFT, root_menu_text, 0, &bottom_right);
    if (screen_push(NAV_MENU_TOP_LEFT, bottom_right) != 0) {
        show_out_of_memory_error();
        return NAV_DASHBOARD;
    }

    pofo_show_cursor();
    result = pofo_menu_show(NAV_MENU_TOP_LEFT, root_menu_text, 0, 0, 0,
                            NAV_MENU_TYPE_DEPTH);
    pofo_hide_cursor();
    screen_pop();

    if (result == -1)
        return NAV_DASHBOARD;

    selected = POFO_LOW_BYTE(result);
    switch (selected) {
    case 0:
        if (!dashboard_ok) {
            pofo_error_dialog(NAV_MENU_TOP_LEFT, offline_dialog_text);
            return NAV_ROOT_MENU;
        }
        return NAV_INTERFACES;
    case 1:
        return do_handshake();
    case 2:
        return NAV_INFO;
    case 3:
        pofo_clear_screen();
        return NAV_EXIT;
    }
    return NAV_DASHBOARD;
}

char *netif_type_label(type)
unsigned char type;
{
    switch (type) {
    case 0x01: return "WiFi client";
    case 0x02: return "WiFi AP";
    case 0x03: return "Ethernet";
    }
    return "Unknown";
}

enum nav_state do_interfaces_list()
{
    unsigned int received;
    int status;
    struct netifs_response *netifs;
    unsigned char i, count;
    char *p;
    unsigned int bottom_right;
    int result;

    progress_dialog_open("Getting interfaces");
    status = smartcable_exchange(netifs_request, sizeof(netifs_request),
                           response, sizeof(response), &received);
    progress_dialog_close();
    if (status != 0) {
        show_transport_error();
        return NAV_ROOT_MENU;
    }
    if (received < 3) {
        show_protocol_error();
        return NAV_ROOT_MENU;
    }
    netifs = (struct netifs_response *)response;
    if (netifs->error != 0 || netifs->count == 0 ||
        netifs->count > (received - 3) / sizeof(struct netif_entry)) {
        show_protocol_error();
        return NAV_ROOT_MENU;
    }

    count = netifs->count;
    if (count > INTERFACES_MENU_MAX_ITEMS)
        count = INTERFACES_MENU_MAX_ITEMS;

    p = interfaces_menu_text;
    *p++ = 0; /* no title */
    for (i = 0; i < count; i++) {
        interfaces_menu_interface[i] = netifs->entries[i].interface;
        p += sprintf(p, netif_type_label(netifs->entries[i].type)) + 1;
    }
    *p = 0; /* double zero terminator */

    pofo_menu_getsize(INTERFACES_MENU_TOP_LEFT, interfaces_menu_text, 0,
                       &bottom_right);
    if (screen_push(INTERFACES_MENU_TOP_LEFT, bottom_right) != 0) {
        show_out_of_memory_error();
        return NAV_ROOT_MENU;
    }

    pofo_show_cursor();
    result = pofo_menu_show(INTERFACES_MENU_TOP_LEFT, interfaces_menu_text,
                            0, 0, 0, INTERFACES_MENU_TYPE_DEPTH);
    pofo_hide_cursor();
    screen_pop();

    if (result == -1)
        return NAV_ROOT_MENU;

    selected_interface = interfaces_menu_interface[POFO_LOW_BYTE(result)];
    return NAV_INTERFACE_MENU;
}

/* Fills interface_settings from menu_netif - called once at
   do_interface_menu() entry, before Network/IP settings can be
   opened. DHCP/static mode and IPv6 default to off since GET_NETIF's
   ip_mode/ipv6_enabled fields didn't exist until this wire-format
   change; once the ESP side reports them for real, read them here
   instead. */
void init_interface_settings()
{
    interface_settings.enabled = menu_netif.info.enabled;
    strcpy(interface_settings.ssid,
           (menu_netif.info.type == PFTC_WIFI_CLIENT &&
            menu_netif.ext.wificli.fields.ssid_length != 0xFF &&
            menu_netif.ext.wificli.fields.ssid_length != 0xFE) ?
           menu_netif.ext.wificli.ssid : "");
    interface_settings.psk[0] = 0;

    interface_settings.ip_mode = PFTC_IP_MODE_DHCP;
    sprintf(interface_settings.ip, "%u.%u.%u.%u", menu_netif.info.ipv4[0],
            menu_netif.info.ipv4[1], menu_netif.info.ipv4[2],
            menu_netif.info.ipv4[3]);
    sprintf(interface_settings.prefix, "%u", menu_netif.info.netmask_prefix);
    sprintf(interface_settings.gateway, "%u.%u.%u.%u", menu_netif.info.gateway[0],
            menu_netif.info.gateway[1], menu_netif.info.gateway[2],
            menu_netif.info.gateway[3]);
    interface_settings.ipv6_enabled = 0;
}

/* Builds and sends SET_NETIF from interface_settings for
   selected_interface, per PFTC_PROTOCOL.md's merged SET_NETIF layout:
   interface/type/enabled/ip_mode/ip/netmask/gateway/ipv6/ssid/psk.
   Returns 0 on success, non-zero on error (already reported). */
int apply_netif_settings()
{
    unsigned char *p;
    unsigned char ssid_len, psk_len;
    unsigned int received;
    int status;

    p = set_netif_request;
    *p++ = PFTC_SET_NETIF;
    *p++ = selected_interface;
    *p++ = menu_netif.info.type;
    *p++ = interface_settings.enabled;
    *p++ = interface_settings.ip_mode;

    if (interface_settings.ip_mode == PFTC_IP_MODE_STATIC) {
        parse_ipv4(interface_settings.ip, p);
        p += 4;
        *p++ = (unsigned char)parse_prefix(interface_settings.prefix);
        parse_ipv4(interface_settings.gateway, p);
        p += 4;
    } else {
        memset(p, 0, 9);
        p += 9;
    }
    *p++ = interface_settings.ipv6_enabled;

    ssid_len = strlen(interface_settings.ssid);
    *p++ = ssid_len;
    memcpy(p, interface_settings.ssid, ssid_len);
    p += ssid_len;

    psk_len = strlen(interface_settings.psk);
    *p++ = psk_len;
    memcpy(p, interface_settings.psk, psk_len);
    p += psk_len;

    progress_dialog_open("Applying");
    status = smartcable_exchange(set_netif_request,
                           (unsigned int)(p - set_netif_request),
                           response, sizeof(response), &received);
    progress_dialog_close();
    if (status != 0) {
        show_transport_error();
        return 1;
    }
    if (received < 2) {
        show_protocol_error();
        return 1;
    }
    if (response[0] != 0x20) {
        show_protocol_error();
        return 1;
    }

    show_applied_message();
    return 0;
}

/* Shared body for every "edit a field, validate, retry on bad input,
   ESC restores the pre-edit value" prompt in Network/IP settings -
   replaces five near-identical copies of the same loop. validator
   returns non-zero for an acceptable value; error_text is shown and
   the same field re-opened (KEEP_ON_ENTRY, so the bad text stays
   visible) until it passes or the user backs out with ESC. */
void edit_field_validated(title, value, max, width, validator,
                                 error_text, exit_keys)
char *title;
char *value;
unsigned int max;
unsigned char width;
int (*validator)();
char *error_text;
unsigned int *exit_keys;
{
    int result;

    strcpy(edit_backup, value);
    for (;;) {
        result = pofo_line_edit(SETTINGS_EDIT_TOP_LEFT, title, "", value,
                                max, width, POFO_EDIT_MODE_CLEAR_ON_ENTRY,
                                POFO_EDIT_BOX_DOUBLE, exit_keys);
        if (result == 0x001B) {
            strcpy(value, edit_backup);
            return;
        }
        if (validator(value))
            return;
        pofo_error_dialog(DIALOG_TOP_LEFT, error_text);
    }
}
