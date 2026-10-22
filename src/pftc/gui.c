#include <conio.h>
#include "pofo.h"
#include "tinylib.h"
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

#define INTERFACES_MENU_MAX_ITEMS 8

static char status_text[40];

unsigned char dashboard_ok;

struct netif_state menu_netif;

unsigned char selected_interface;

struct netif_settings interface_settings;

static char root_menu_text[] = "PFTC\0Interfaces\0Reconnect\0Info\0Exit\0\0";
static unsigned char interfaces_menu_interface[INTERFACES_MENU_MAX_ITEMS];
static char edit_backup[65];

char not_supported_dialog_text[] = "Not supported yet.";
static char offline_dialog_text[] = "Offline - try Reconnect.";

/* Enter confirms, ESC backs out of every edit field. */
static unsigned int edit_exit_keys[3] = { 0x000D, 0x001B, 0 };

static char *netif_type_labels[] = { "WiFi client", "WiFi AP", "Ethernet" };

int fetch_netif_state()
{
    unsigned int received;
    unsigned int wifi_size;
    struct netif_wificli_ext *ext;
    struct netif_wificli_state *wifi;

    netif_request[1] = selected_interface;
    received = request("Getting interface", netif_request,
                       sizeof(netif_request), sizeof(struct netif_info), 1);
    if (received == 0)
        return 1;

    mem_copy(&menu_netif.info, response, sizeof(struct netif_info));
    if (menu_netif.info.type != PFTC_WIFI_CLIENT)
        return 0;

    wifi = &menu_netif.ext.wificli;
    wifi->fields.channel = 0;
    wifi->fields.rssi = -128;
    wifi->fields.ssid_length = 0xFE; /* sentinel: "unavailable" */
    wifi_size = received - sizeof(struct netif_info);
    if (wifi_size < sizeof(struct netif_wificli_ext))
        return 0;

    ext = (struct netif_wificli_ext *)(response + sizeof(struct netif_info));
    mem_copy(&wifi->fields, ext, sizeof(struct netif_wificli_ext));
    if (wifi->fields.ssid_length > sizeof(wifi->ssid) - 1)
        wifi->fields.ssid_length = sizeof(wifi->ssid) - 1;
    if (wifi_size < sizeof(struct netif_wificli_ext) + ext->ssid_length) {
        wifi->fields.ssid_length = 0xFF; /* sentinel: "invalid" */
        return 0;
    }
    mem_copy(wifi->ssid, (char *)ext + sizeof(struct netif_wificli_ext),
             wifi->fields.ssid_length);
    wifi->ssid[wifi->fields.ssid_length] = 0;
    return 0;
}

int netif_has_ssid()
{
    return menu_netif.info.type == PFTC_WIFI_CLIENT &&
           menu_netif.ext.wificli.fields.ssid_length != 0xFF &&
           menu_netif.ext.wificli.fields.ssid_length != 0xFE;
}

/* Leaves dashboard_ok unchanged on failure. */
static int do_fetch_dashboard()
{
    unsigned int received;
    unsigned char *p, *end;
    unsigned char len;

    received = request("Getting dashboard", dashboard_request,
                       sizeof(dashboard_request), 2, 1);
    if (received == 0)
        return 1;

    /* Entries of id, row, col, len and len payload bytes; stop at the
       first truncated header or payload. id is not used yet - no
       PARTIAL refresh/targeting so far. */
    p = response + 2;
    end = response + received;
    while (p + 4 <= end) {
        len = p[3];
        if (p + 4 + len > end)
            break;
        gotoxy(p[2], p[1]);
        con_write(p + 4, len);
        p += 4 + len;
    }

    dashboard_ok = 1;
    return 0;
}

enum nav_state do_handshake()
{
    struct hello_response *hello;

    status_set("Connecting");
    if (request("Connecting", hello_request, sizeof(hello_request),
                sizeof(struct hello_response), 1) == 0)
        return NAV_DASHBOARD;

    hello = (struct hello_response *)response;
    fmt(status_text, "Connected v%u.%u.%u (%x%x%x%x)",
        hello->version_major, hello->version_minor, hello->version_patch,
        hello->build_id[3], hello->build_id[2], hello->build_id[1],
        hello->build_id[0]);
    status_set(status_text);

    smartcable_wait_500ms();
    do_fetch_dashboard();
    return NAV_DASHBOARD;
}

enum nav_state do_dashboard()
{
    int key;

    if (!dashboard_ok)
        status_offline();

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

enum nav_state do_root_menu()
{
    switch (menu_run(root_menu_text)) {
    case 0:
        if (!dashboard_ok) {
            pofo_error_dialog(MENU_TOP_LEFT, offline_dialog_text);
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
    if (type >= 1 && type <= 3)
        return netif_type_labels[type - 1];
    return "Unknown";
}

enum nav_state do_interfaces_list()
{
    unsigned int received;
    struct netifs_response *netifs;
    unsigned char i, count;
    char *p;
    int result;

    received = request("Getting interfaces", netifs_request,
                       sizeof(netifs_request), 3, 1);
    if (received == 0)
        return NAV_ROOT_MENU;
    netifs = (struct netifs_response *)response;
    count = netifs->count;
    if (count == 0 || count * sizeof(struct netif_entry) + 3 > received) {
        show_protocol_error();
        return NAV_ROOT_MENU;
    }

    if (count > INTERFACES_MENU_MAX_ITEMS)
        count = INTERFACES_MENU_MAX_ITEMS;

    p = menu_text;
    *p++ = 0; /* no title */
    for (i = 0; i < count; i++) {
        interfaces_menu_interface[i] = netifs->entries[i].interface;
        p = str_copy(p, netif_type_label(netifs->entries[i].type)) + 1;
    }
    *p = 0; /* double zero terminator */

    result = menu_run(menu_text);
    if (result == -1)
        return NAV_ROOT_MENU;

    selected_interface = interfaces_menu_interface[result];
    return NAV_INTERFACE_MENU;
}

/* Defaults for fields not yet supplied by GET_NETIF. */
void init_interface_settings()
{
    interface_settings.net.enabled = menu_netif.info.enabled;
    str_copy(interface_settings.net.ssid,
             netif_has_ssid() ? menu_netif.ext.wificli.ssid : "");
    interface_settings.net.psk[0] = 0;

    interface_settings.ip.ip_mode = PFTC_IP_MODE_DHCP;
    fmt(interface_settings.ip.ip, "%i", menu_netif.info.ipv4);
    fmt(interface_settings.ip.prefix, "%u", menu_netif.info.netmask_prefix);
    fmt(interface_settings.ip.gateway, "%i", menu_netif.info.gateway);
    interface_settings.ip.ipv6_enabled = 0;
}

/* Length byte followed by the text itself; returns the end. */
static unsigned char *put_counted(p, text)
unsigned char *p;
char *text;
{
    unsigned char length;

    length = str_len(text);
    *p++ = length;
    mem_copy(p, text, length);
    return p + length;
}

/* Builds and sends SET_NETIF from interface_settings for
   selected_interface, per PFTC_PROTOCOL.md's merged SET_NETIF layout:
   interface/type/enabled/ip_mode/ip/netmask/gateway/ipv6/ssid/psk.
   Returns 0 on success, non-zero on error (already reported). */
int apply_netif_settings()
{
    unsigned char *p;
    int i;

    set_netif_request[0] = PFTC_SET_NETIF;
    set_netif_request[1] = selected_interface;
    set_netif_request[2] = menu_netif.info.type;
    set_netif_request[3] = interface_settings.net.enabled;
    set_netif_request[4] = interface_settings.ip.ip_mode;

    /* [5..8] ip, [9] prefix, [10..13] gateway - all zero for DHCP. */
    if (interface_settings.ip.ip_mode == PFTC_IP_MODE_STATIC) {
        parse_ipv4(interface_settings.ip.ip, set_netif_request + 5);
        set_netif_request[9] = parse_prefix(interface_settings.ip.prefix);
        parse_ipv4(interface_settings.ip.gateway, set_netif_request + 10);
    } else {
        for (i = 5; i < 14; i++)
            set_netif_request[i] = 0;
    }
    set_netif_request[14] = interface_settings.ip.ipv6_enabled;

    p = put_counted(set_netif_request + 15, interface_settings.net.ssid);
    p = put_counted(p, interface_settings.net.psk);

    if (request("Applying", set_netif_request,
                (unsigned int)(p - set_netif_request), 2, 1) == 0)
        return 1;

    show_applied_message();
    return 0;
}

/* ESC restores value; validation retries the same editor. */
void edit_field_validated(title, value, max, width, validator, error_text)
char *title;
char *value;
unsigned int max;
unsigned char width;
int (*validator)();
char *error_text;
{
    int result;

    str_copy(edit_backup, value);
    for (;;) {
        result = pofo_line_edit(SETTINGS_EDIT_TOP_LEFT, title, "", value,
                                max, width, POFO_EDIT_MODE_CLEAR_ON_ENTRY,
                                POFO_EDIT_BOX_DOUBLE, edit_exit_keys);
        if (result == 0x001B) {
            str_copy(value, edit_backup);
            return;
        }
        if (validator(value))
            return;
        pofo_error_dialog(DIALOG_TOP_LEFT, error_text);
    }
}
