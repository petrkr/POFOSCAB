#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include "pofo.h"
#include "smartcable.h"
#include "gui_core.h"

#define SCREEN_STACK_MAX 3

struct screen_frame {
    unsigned int top_left;
    unsigned int bottom_right;
    unsigned char *buffer;
};

/* Screen-save buffers. */
static unsigned char status_screen_buffer[POFO_SCREEN_SIZE(STATUS_TOP_LEFT, STATUS_BOTTOM_RIGHT)];
/* Sized for the widest progress_dialog_open() text ("Getting interfaces");
   widen if a longer one is added. */
static unsigned char dialog_screen_buffer[POFO_SCREEN_SIZE(DIALOG_TOP_LEFT, POFO_COORD(5, 23))];
static struct screen_frame screen_stack[SCREEN_STACK_MAX];
static unsigned char screen_stack_depth;

static char progress_dialog_title[] = "";
static char *progress_dialog_text;
static char transport_error_dialog_text[] = "SmartCable error\nTransport failed.";
static char protocol_error_dialog_text[] = "Invalid response.";
static char oom_error_dialog_text[] = "Out of memory.";
static char applied_dialog_text[] = "Applied.";

void status_init()
{
    pofo_screen_save(STATUS_TOP_LEFT, STATUS_BOTTOM_RIGHT, status_screen_buffer);
}

void status_set(text)
char *text;
{
    pofo_screen_restore(STATUS_TOP_LEFT, STATUS_BOTTOM_RIGHT, status_screen_buffer);
    gotoxy(2, 7);
    printf(" %.*s ", 34, text);
    fflush(stdout);
}

void show_transport_error()
{
    status_set("Offline");
    pofo_error_dialog(DIALOG_TOP_LEFT, transport_error_dialog_text);
}

void show_protocol_error()
{
    status_set("Offline");
    pofo_error_dialog(DIALOG_TOP_LEFT, protocol_error_dialog_text);
}

void show_out_of_memory_error()
{
    pofo_error_dialog(DIALOG_TOP_LEFT, oom_error_dialog_text);
}

/* Self-dismissing confirmation, unlike pofo_error_dialog() - no
   keypress wait (so it doesn't block Apply's caller) and no error
   beep (this isn't an error). Reuses progress_dialog's own save
   buffer/title since the two never overlap. */
void show_applied_message()
{
    pofo_screen_save(DIALOG_TOP_LEFT,
                      pofo_dialog_extent(DIALOG_TOP_LEFT,
                                         applied_dialog_text,
                                         progress_dialog_title),
                      dialog_screen_buffer);
    pofo_message_dialog(DIALOG_TOP_LEFT, applied_dialog_text,
                        progress_dialog_title);
    smartcable_wait_500ms();
    smartcable_wait_500ms();
    pofo_screen_restore(DIALOG_TOP_LEFT,
                         pofo_dialog_extent(DIALOG_TOP_LEFT,
                                            applied_dialog_text,
                                            progress_dialog_title),
                         dialog_screen_buffer);
}

void progress_dialog_open(text)
char *text;
{
    progress_dialog_text = text;
    pofo_screen_save(DIALOG_TOP_LEFT,
                      pofo_dialog_extent(DIALOG_TOP_LEFT,
                                         progress_dialog_text,
                                         progress_dialog_title),
                      dialog_screen_buffer);
    pofo_message_dialog(DIALOG_TOP_LEFT, progress_dialog_text,
                        progress_dialog_title);
}

void progress_dialog_close()
{
    pofo_screen_restore(DIALOG_TOP_LEFT,
                         pofo_dialog_extent(DIALOG_TOP_LEFT,
                                            progress_dialog_text,
                                            progress_dialog_title),
                         dialog_screen_buffer);
}

int screen_push(top_left, bottom_right)
unsigned int top_left;
unsigned int bottom_right;
{
    unsigned char *buffer;

    if (screen_stack_depth >= SCREEN_STACK_MAX)
        return POFO_NO_MEMORY;

    buffer = malloc(POFO_SCREEN_SIZE(top_left, bottom_right));
    if (buffer == NULL)
        return POFO_NO_MEMORY;

    pofo_screen_save(top_left, bottom_right, buffer);
    screen_stack[screen_stack_depth].top_left = top_left;
    screen_stack[screen_stack_depth].bottom_right = bottom_right;
    screen_stack[screen_stack_depth].buffer = buffer;
    screen_stack_depth++;
    return 0;
}

void screen_pop()
{
    struct screen_frame *frame;

    frame = &screen_stack[--screen_stack_depth];
    pofo_screen_restore(frame->top_left, frame->bottom_right, frame->buffer);
    free(frame->buffer);
}
