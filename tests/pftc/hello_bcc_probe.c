#include <stdio.h>
#include "smartcable.h"

#define PFTC_HELLO 0x01
#define PFTC_STATUS_OK 0x20

struct test_case {
    unsigned char request;
    unsigned char status;
    unsigned char error;
};

static struct test_case tests[] = {
    { PFTC_HELLO, PFTC_STATUS_OK, 0x00 }
};

/*
 * PFTD examines the receive buffer on the next INT 61h invocation.  Keep
 * the response header in local variables before close() becomes that next
 * invocation.
 */
static int exchange_preserving_header(test, received_out, status_out, error_out)
struct test_case *test;
unsigned int *received_out;
unsigned char *status_out;
unsigned char *error_out;
{
    unsigned char request[1];
    unsigned char response[64];
    unsigned int received;
    int status;
    int retries;

    received = 0;
    request[0] = test->request;
    status = smartcable_open();
    if (status != 0)
        return status;

    retries = SMARTCABLE_SYNC_RETRIES;
    do {
        status = smartcable_send(request, sizeof(request));
        if (status != SMARTCABLE_STATUS_SYNC_MISSED)
            break;
        if (retries-- == 0)
            break;
        smartcable_wait_500ms();
    } while (1);

    if (status == 0) {
        smartcable_wait_500ms();
        status = smartcable_receive(response, sizeof(response), &received);
        if (status == 0 && received >= 2) {
            *status_out = response[0];
            *error_out = response[1];
            printf("before=%02X,%02X ", response[0], response[1]);
        }
    }

    smartcable_close();
    if (status == 0 && received >= 2)
        printf("after=%02X,%02X\n", response[0], response[1]);
    *received_out = received;
    return status;
}

int main()
{
    unsigned int i;
    unsigned int received;
    unsigned char response_status;
    unsigned char response_error;
    int status;
    int pass;
    int fail;

    pass = 0;
    fail = 0;
    for (i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        received = 0;
        response_status = 0xff;
        response_error = 0xff;
        status = exchange_preserving_header(&tests[i], &received,
                                            &response_status, &response_error);
        if (status == 0 && received >= 2 &&
            response_status == tests[i].status &&
            response_error == tests[i].error)
            pass++;
        else {
            printf("fail request=%02X transport=%d received=%u header=%02X,%02X\n",
                   tests[i].request, status, received,
                   response_status, response_error);
            fail++;
        }
    }

    printf("pass=%d fail=%d\n", pass, fail);
    return fail != 0;
}
