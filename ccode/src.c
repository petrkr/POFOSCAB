#include <stdio.h>
#include <conio.h>
#include "smartcable.h"

static unsigned char src_saved[16];

static src_save()
{
#asm
    mov cx,#$0303
    mov dx,#$0000
    mov ax,#$0800
    xor bx,bx
    mov si,#_src_saved
    int $60
#endasm
}

static src_restore()
{
#asm
    mov cx,#$0303
    mov dx,#$0000
    mov ax,#$0802
    xor bx,bx
    mov si,#_src_saved
    int $60
#endasm
}

int main()
{
    src_save();
    gotoxy(2, 2);
    printf("SRC");
    fflush(stdout);

    smartcable_wait_500ms();
    smartcable_wait_500ms();

    printf("SRC");
    fflush(stdout);
    smartcable_wait_500ms();
    smartcable_wait_500ms();
    src_restore();

    return 0;
}
