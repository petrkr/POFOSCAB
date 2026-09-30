#ifndef PFTC_GUI_CORE_H
#define PFTC_GUI_CORE_H

#define STATUS_TOP_LEFT     POFO_COORD(7, 0)
#define STATUS_BOTTOM_RIGHT POFO_COORD(7, 39)

#define DIALOG_TOP_LEFT POFO_COORD(2, 2)

/* Width in characters of the bar print_signal_bar() draws. */
#define SIGNAL_BAR_LEVELS 10

void status_init();
void status_set();
void print_ipv4();
void print_signal_bar();
void show_transport_error();
void show_protocol_error();
void show_out_of_memory_error();

/* Titleless progress dialog around a blocking smartcable_exchange()
   call - open with the action's text, close right after. */
void progress_dialog_open();
void progress_dialog_close();

/* Menu/dialog save/restore stack - push before, pop after. Returns 0,
   or POFO_NO_MEMORY if full/OOM (fall back to pofo_error_dialog then,
   never draw without something to restore). */
int screen_push();
void screen_pop();

#endif
