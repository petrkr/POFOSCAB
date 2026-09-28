#ifndef PFTC_GUI_H
#define PFTC_GUI_H

#define STATUS_TOP_LEFT     POFO_COORD(7, 0)
#define STATUS_BOTTOM_RIGHT POFO_COORD(7, 39)

/* Only pofo_message_dialog (AH=12h) needs manual save/restore - the ROM
   erases pofo_error_dialog (AH=14h) itself on keypress. Sized for the
   widest/tallest message dialog actually shown (connecting_dialog). */
#define DIALOG_TOP_LEFT POFO_COORD(2, 2)

/* Width in characters of the bar print_signal_bar() draws - callers need
   this to lay out text before it (see do_dashboard()'s gotoxy). */
#define SIGNAL_BAR_LEVELS 10

/* Saves the status line's blank starting state so status_set() can
   restore it before printing new text - call once at startup, before
   the first status_set(). */
void status_init();
void status_set();
void print_ipv4();
void print_signal_bar();
void show_transport_error();
void show_protocol_error();
void show_out_of_memory_error();

/* Progress message dialog (AH=12h, needs manual save/restore unlike the
   error dialogs above), titleless - open with the action's text (e.g.
   "Connecting", "Getting interfaces") right before a blocking
   smartcable_exchange() call, close right after. Owns
   dialog_screen_buffer, so calls must nest one at a time - no other
   dialog while one of these is open. */
void progress_dialog_open();
void progress_dialog_close();

/* Generic save/restore stack for menu navigation (see PFTC.md SS6) - the
   ROM has no nesting/auto-restore of its own, so each nav level pushes
   before showing its menu/dialog and pops after it closes. Depth 3 is a
   conservative ceiling; real depth is ~1-2 (root menu -> interfaces list
   -> detail).

   Mallocs a buffer sized for top_left..bottom_right, saves that region
   into it, and pushes the frame. Returns 0, or POFO_NO_MEMORY if the
   stack is full or malloc fails - caller must not draw anything over the
   region in that case (nothing to restore it with) and should fall back
   to pofo_error_dialog (self-erasing, needs no stack slot) instead. */
int screen_push();
/* Restores and frees the top frame. Caller must only call this after a
   matching screen_push() returned 0. */
void screen_pop();

#endif
