#include <stdio.h>
#include <stdlib.h>
#include <conio.h>
#include "pofo.h"
#include "pftc_gui.h"

static unsigned char status_screen_buffer[POFO_SCREEN_SIZE(STATUS_TOP_LEFT, STATUS_BOTTOM_RIGHT)];
/* Sized for the widest progress text actually passed to
   progress_dialog_open() - "Getting interfaces" (18 chars) with an
   empty title, width = 18 + 4 = 22, so bottom_right col = 2 + 22 - 1 =
   23; height is always top_left row + 3. Widen this if a longer text
   is ever added. */
static unsigned char dialog_screen_buffer[POFO_SCREEN_SIZE(DIALOG_TOP_LEFT, POFO_COORD(5, 23))];

static char progress_dialog_title[] = "";
static char *progress_dialog_text;
static char transport_error_dialog_title[] = "SmartCable error";
static char transport_error_dialog_text[] = "Transport failed.";
static char protocol_error_dialog_title[] = "PFTC error";
static char protocol_error_dialog_text[] = "Invalid response.";

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

void print_ipv4(address)
unsigned char *address;
{
    printf("%u.%u.%u.%u", address[0], address[1], address[2], address[3]);
}

#define SIGNAL_BAR_FULL   0xDB
#define SIGNAL_BAR_EMPTY  0xB0
#define SIGNAL_BAR_MIN   -90
#define SIGNAL_BAR_MAX   -40

void print_signal_bar(rssi)
int rssi;
{
    unsigned char level, filled;

    if (rssi <= SIGNAL_BAR_MIN)
        filled = 0;
    else if (rssi >= SIGNAL_BAR_MAX)
        filled = SIGNAL_BAR_LEVELS;
    else
        filled = (unsigned char)((rssi - SIGNAL_BAR_MIN) * SIGNAL_BAR_LEVELS /
                                  (SIGNAL_BAR_MAX - SIGNAL_BAR_MIN));

    for (level = 0; level < SIGNAL_BAR_LEVELS; level++)
        putchar((unsigned char)(level < filled ? SIGNAL_BAR_FULL : SIGNAL_BAR_EMPTY));
}

void show_transport_error()
{
    status_set("Offline");
    pofo_error_dialog(DIALOG_TOP_LEFT, transport_error_dialog_text,
                      transport_error_dialog_title);
}

void show_protocol_error()
{
    status_set("Offline");
    pofo_error_dialog(DIALOG_TOP_LEFT, protocol_error_dialog_text,
                      protocol_error_dialog_title);
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

#define SCREEN_STACK_MAX 3

struct screen_frame {
    unsigned int top_left;
    unsigned int bottom_right;
    unsigned char *buffer;
};

static struct screen_frame screen_stack[SCREEN_STACK_MAX];
static unsigned char screen_stack_depth;

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
