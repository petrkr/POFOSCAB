#include "pofo.h"
#include "tinylib.h"
#include "smartcable.h"
#include "pftc_proto.h"
#include "gui_core.h"

#define SCREEN_STACK_MAX 3
/* A whole 40x8 screen is 320 cells; a menu wider than the display can
   report a larger area, hence the headroom. */
#define SCREEN_FRAME_SIZE 512

struct screen_frame {
    unsigned int top_left;
    unsigned int bottom_right;
    unsigned char cells[SCREEN_FRAME_SIZE];
};

static unsigned char status_screen_buffer[POFO_SCREEN_SIZE(STATUS_TOP_LEFT, STATUS_BOTTOM_RIGHT)];
static struct screen_frame screen_stack[SCREEN_STACK_MAX];
static unsigned char screen_stack_depth;
/* Whether the open progress dialog got its own screen_push() frame. */
static unsigned char progress_dialog_pushed;

char menu_text[MENU_TEXT_SIZE];

static char progress_dialog_title[] = "";
static char transport_error_dialog_text[] = "SmartCable error\nTransport failed.";
static char protocol_error_dialog_text[] = "Invalid response.";
static char oom_error_dialog_text[] = "Out of memory.";
static char applied_dialog_text[] = "Applied.";
static char offline_status_text[] = "Offline";
static char status_pad[] = " ";

void status_init()
{
    pofo_screen_save(STATUS_TOP_LEFT, STATUS_BOTTOM_RIGHT, status_screen_buffer);
}

/* " text " at column 2 of the status row, text cut to 34 characters. */
void status_set(text)
char *text;
{
    unsigned int length;

    pofo_screen_restore(STATUS_TOP_LEFT, STATUS_BOTTOM_RIGHT, status_screen_buffer);
    gotoxy(2, 7);
    length = str_len(text);
    if (length > 34)
        length = 34;
    con_write(status_pad, 1);
    con_write(text, length);
    con_write(status_pad, 1);
}

void status_offline()
{
    status_set(offline_status_text);
}

static void show_error(text)
char *text;
{
    status_offline();
    pofo_error_dialog(DIALOG_TOP_LEFT, text);
}

void show_protocol_error()
{
    show_error(protocol_error_dialog_text);
}

void show_out_of_memory_error()
{
    pofo_error_dialog(DIALOG_TOP_LEFT, oom_error_dialog_text);
}

void progress_dialog_open(text)
char *text;
{
    progress_dialog_pushed =
        screen_push(DIALOG_TOP_LEFT,
                    pofo_dialog_extent(DIALOG_TOP_LEFT, text,
                                       progress_dialog_title)) == 0;
    pofo_message_dialog(DIALOG_TOP_LEFT, text, progress_dialog_title);
}

void progress_dialog_close()
{
    if (progress_dialog_pushed)
        screen_pop();
}

/* Self-dismissing confirmation, unlike pofo_error_dialog() - no
   keypress wait (so it doesn't block Apply's caller) and no error
   beep (this isn't an error). */
void show_applied_message()
{
    progress_dialog_open(applied_dialog_text);
    smartcable_wait_500ms();
    smartcable_wait_500ms();
    progress_dialog_close();
}

int screen_push(top_left, bottom_right)
unsigned int top_left;
unsigned int bottom_right;
{
    struct screen_frame *frame;

    if (screen_stack_depth >= SCREEN_STACK_MAX ||
        POFO_SCREEN_SIZE(top_left, bottom_right) > SCREEN_FRAME_SIZE)
        return POFO_NO_MEMORY;

    frame = &screen_stack[screen_stack_depth++];
    frame->top_left = top_left;
    frame->bottom_right = bottom_right;
    pofo_screen_save(top_left, bottom_right, frame->cells);
    return 0;
}

void screen_pop()
{
    struct screen_frame *frame;

    frame = &screen_stack[--screen_stack_depth];
    pofo_screen_restore(frame->top_left, frame->bottom_right, frame->cells);
}

int menu_show(text, selected)
char *text;
unsigned char selected;
{
    int result;

    result = pofo_menu_show(MENU_TOP_LEFT, text, 0, 0, selected,
                            MENU_TYPE_DEPTH);
    if (result == -1)
        return -1;
    return POFO_LOW_BYTE(result);
}

int menu_run(text)
char *text;
{
    unsigned int bottom_right;
    int result;

    pofo_menu_getsize(MENU_TOP_LEFT, text, 0, &bottom_right);
    if (screen_push(MENU_TOP_LEFT, bottom_right) != 0) {
        show_out_of_memory_error();
        return -1;
    }

    pofo_set_cursor_mode(POFO_CURSOR_BLOCK);
    result = menu_show(text, 0);
    pofo_set_cursor_mode(POFO_CURSOR_OFF);
    screen_pop();
    return result;
}

unsigned int request(progress_text, req, req_size, min_length, receive_waits)
char *progress_text;
unsigned char *req;
unsigned int req_size;
unsigned int min_length;
unsigned char receive_waits;
{
    unsigned int received;
    int status;

    progress_dialog_open(progress_text);
    status = smartcable_exchange(req, req_size, response, sizeof(response),
                                 &received, receive_waits);
    progress_dialog_close();
    if (status != 0) {
        show_error(transport_error_dialog_text);
        return 0;
    }
    if (received < min_length || response[0] != PFTC_STATUS_OK ||
        response[1] != 0) {
        show_protocol_error();
        return 0;
    }
    return received;
}
