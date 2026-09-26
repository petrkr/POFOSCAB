#include "smartcable.h"

static unsigned char *smartcable_buffer;
static unsigned int smartcable_size;
static unsigned int smartcable_action;
static unsigned char smartcable_status;
static unsigned int smartcable_count;

static int smartcable_port_call(action)
unsigned int action;
{
    smartcable_action = action;

#asm
    push ax
    push bx
    push cx
    push dx
    push ds
    mov ax,_smartcable_action
    mov dx,_smartcable_buffer
    mov cx,_smartcable_size
    int 0x61
    pop ds
    mov _smartcable_status,dl
    mov _smartcable_count,cx
    pop dx
    pop cx
    pop bx
    pop ax
#endasm

    return smartcable_status;
}

int smartcable_open()
{
    return smartcable_port_call(0x3002);
}

int smartcable_close()
{
    return smartcable_port_call(0x3003);
}

int smartcable_wait_500ms()
{
    return smartcable_port_call(0x3004);
}

int smartcable_send(buffer, size)
unsigned char *buffer;
unsigned int size;
{
    smartcable_buffer = buffer;
    smartcable_size = size;
    return smartcable_port_call(0x3000);
}

int smartcable_receive(buffer, size, received)
unsigned char *buffer;
unsigned int size;
unsigned int *received;
{
    int status;

    smartcable_buffer = buffer;
    smartcable_size = size;
    status = smartcable_port_call(0x3001);
    if (smartcable_count > size)
        smartcable_count = size;
    *received = smartcable_count;
    return status;
}

int smartcable_exchange(request, request_size, response, response_size, received)
unsigned char *request;
unsigned int request_size;
unsigned char *response;
unsigned int response_size;
unsigned int *received;
{
    int status;
    int retries;

    status = smartcable_open();
    if (status != 0)
        return status;

    retries = SMARTCABLE_SYNC_RETRIES;
    do {
        status = smartcable_send(request, request_size);
        if (status != SMARTCABLE_STATUS_SYNC_MISSED)
            break;
        if (retries-- == 0)
            break;
        smartcable_wait_500ms();
    } while (1);

    if (status == 0) {
        smartcable_wait_500ms();
        status = smartcable_receive(response, response_size, received);
    }

    smartcable_close();
    return status;
}
