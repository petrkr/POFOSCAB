#ifndef SMARTCABLE_H
#define SMARTCABLE_H

#define SMARTCABLE_SYNC_RETRIES 10
#define SMARTCABLE_STATUS_SYNC_MISSED 6

int smartcable_open();
int smartcable_close();
int smartcable_wait_500ms();
int smartcable_send();
int smartcable_receive();
int smartcable_exchange();

#endif
