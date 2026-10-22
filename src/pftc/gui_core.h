#ifndef PFTC_GUI_CORE_H
#define PFTC_GUI_CORE_H

#define STATUS_TOP_LEFT     POFO_COORD(7, 0)
#define STATUS_BOTTOM_RIGHT POFO_COORD(7, 39)

#define DIALOG_TOP_LEFT POFO_COORD(2, 2)

/* Every menu in the app shares one position and box style: rows 1-7,
   intentionally covering the status line too; screen_push/pop restores
   it once the menu closes. */
#define MENU_TOP_LEFT POFO_COORD(1, 2)
#define MENU_HEIGHT_LIMIT 7
#define MENU_TYPE_DEPTH ((MENU_HEIGHT_LIMIT << 3) | POFO_BOX_DOUBLE)

/* Shared title/items build buffer for whichever menu is being shown -
   every screen rebuilds its text right before showing it, so nothing
   survives in here across screens. Sized for the largest, the WiFi
   scan list: "Scan results" + 10 entries of up to 32 SSID bytes plus
   " (WPA/WPA2, -128)", each zero-terminated, plus the final zero. */
#define MENU_TEXT_SIZE 528
extern char menu_text[MENU_TEXT_SIZE];

void status_init();
void status_set();
void status_offline();
void show_protocol_error();
void show_out_of_memory_error();
void show_applied_message();

/* Titleless progress dialog around a blocking request() - open with
   the action's text, close right after. */
void progress_dialog_open();
void progress_dialog_close();

/* Menu/dialog save/restore stack - push before, pop after. Returns 0,
   or POFO_NO_MEMORY if the stack is full or the area is larger than a
   frame (fall back to pofo_error_dialog then, never draw without
   something to restore). */
int screen_push();
void screen_pop();

/* pofo_menu_show() of text at MENU_TOP_LEFT with the given item
   preselected. Returns the picked item index, or -1 on ESC. */
int menu_show();
/* Complete one-shot menu: saves the screen under it, shows a block
   cursor, runs menu_show() with item 0 preselected, hides the cursor
   and restores the screen. Returns the item index, or -1 on ESC (also
   -1, after an "Out of memory" dialog, if the screen could not be
   saved - the caller goes back the same way it would on ESC). */
int menu_run();

/* One request/response exchange behind a progress dialog, with the
   common checks: transport failure, a response shorter than min_length
   (at least 2) and a status/error header that is not ok all show their
   error dialog and return 0. Otherwise returns the number of bytes now
   in response[]. */
unsigned int request();

#endif
