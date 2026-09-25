#include "pofo.h"
#include <string.h>

/* Exact INT 60h/AH=01h descriptor offsets; PCC structs add padding. */
#define POFO_EDIT_BLOCK_SIZE       35
#define POFO_EDIT_TARGET_OFFSET     0
#define POFO_EDIT_TARGET_SEGMENT    2
#define POFO_EDIT_POS_OFFSET        4
#define POFO_EDIT_MAX_OFFSET        6
#define POFO_EDIT_XPOS_OFFSET       8
#define POFO_EDIT_YPOS_OFFSET       9
#define POFO_EDIT_MODE_OFFSET      10
#define POFO_EDIT_HIT_OFFSET       11
#define POFO_EDIT_TITLE_OFFSET     13
#define POFO_EDIT_TITLE_SEGMENT    15
#define POFO_EDIT_EXIT_OFFSET      17
#define POFO_EDIT_EXIT_SEGMENT     19
#define POFO_EDIT_GETKEY_OFFSET    21
#define POFO_EDIT_GETKEY_SEGMENT   23
#define POFO_EDIT_WIDTH_OFFSET     25
#define POFO_EDIT_WINDOW_OFFSET    26
#define POFO_EDIT_RESERVED1_OFFSET 27
#define POFO_EDIT_RESERVED2_OFFSET 29
#define POFO_EDIT_UNDELETE_OFFSET  31
#define POFO_EDIT_UNDELETE_SEGMENT 33
#define POFO_EDIT_TITLE_PROMPT_SIZE 256

static unsigned char pofo_edit_block[POFO_EDIT_BLOCK_SIZE];
static char pofo_edit_title_prompt[POFO_EDIT_TITLE_PROMPT_SIZE];
static unsigned int pofo_edit_result;

extern pofo_edit_getkey();
extern pofo_edit_undelete();

static void pofo_edit_word(offset, value)
unsigned int offset;
unsigned int value;
{
    pofo_edit_block[offset] = (unsigned char)value;
    pofo_edit_block[offset + 1] = (unsigned char)(value >> 8);
}

int pofo_line_edit(xpos, ypos, title, prompt, value, max, width, mode,
                   window, exit_keys)
unsigned char xpos;
unsigned char ypos;
char *title;
char *prompt;
char *value;
unsigned int max;
unsigned char mode;
unsigned char width;
unsigned char window;
unsigned int *exit_keys;
{
    unsigned int title_length;
    unsigned int prompt_length;

    title_length = strlen(title);
    prompt_length = strlen(prompt);
    if (title_length + prompt_length + 3 > POFO_EDIT_TITLE_PROMPT_SIZE)
        return POFO_EDIT_TEXT_TOO_LONG;

    memcpy(pofo_edit_title_prompt, title, title_length);
    pofo_edit_title_prompt[title_length] = 0;
    memcpy(pofo_edit_title_prompt + title_length + 1, prompt, prompt_length);
    pofo_edit_title_prompt[title_length + prompt_length + 1] = 0;
    pofo_edit_title_prompt[title_length + prompt_length + 2] = 0;

    pofo_edit_word(POFO_EDIT_TARGET_OFFSET, (unsigned int)value);
    pofo_edit_word(POFO_EDIT_POS_OFFSET, 0);
    pofo_edit_word(POFO_EDIT_MAX_OFFSET, max);
    pofo_edit_block[POFO_EDIT_XPOS_OFFSET] = xpos;
    pofo_edit_block[POFO_EDIT_YPOS_OFFSET] = ypos;
    pofo_edit_block[POFO_EDIT_MODE_OFFSET] = mode;
    pofo_edit_word(POFO_EDIT_HIT_OFFSET, 0);
    pofo_edit_word(POFO_EDIT_TITLE_OFFSET, (unsigned int)pofo_edit_title_prompt);
    pofo_edit_word(POFO_EDIT_EXIT_OFFSET, (unsigned int)exit_keys);
    pofo_edit_word(POFO_EDIT_GETKEY_OFFSET, (unsigned int)pofo_edit_getkey);
    pofo_edit_block[POFO_EDIT_WIDTH_OFFSET] = width;
    pofo_edit_block[POFO_EDIT_WINDOW_OFFSET] = window;
    pofo_edit_word(POFO_EDIT_RESERVED1_OFFSET, 0);
    pofo_edit_word(POFO_EDIT_RESERVED2_OFFSET, 0);
    pofo_edit_word(POFO_EDIT_UNDELETE_OFFSET, (unsigned int)pofo_edit_undelete);

#asm
    push bx
    mov ax,ds
    mov _pofo_edit_block+POFO_EDIT_TARGET_SEGMENT,ax
    mov _pofo_edit_block+POFO_EDIT_TITLE_SEGMENT,ax
    mov _pofo_edit_block+POFO_EDIT_EXIT_SEGMENT,ax
    mov bx,cs
    mov _pofo_edit_block+POFO_EDIT_GETKEY_SEGMENT,bx
    mov _pofo_edit_block+POFO_EDIT_UNDELETE_SEGMENT,bx
    mov si,#_pofo_edit_block
    mov ax,#0x0100
    int 0x60
    mov _pofo_edit_result,ax
    pop bx
#endasm

    return (int)pofo_edit_result;
}

#asm
_pofo_edit_getkey:
    push bx
    push cx
    push dx
    push si
    push di
    push ds
    push es
    mov ah,#0
    int 0x16
    or al,al
    jnz pofo_edit_ascii
    mov al,ah
    mov ah,#1
    jmp pofo_edit_done
pofo_edit_ascii:
    xor ah,ah
pofo_edit_done:
    pop es
    pop ds
    pop di
    pop si
    pop dx
    pop cx
    pop bx
    retf

_pofo_edit_undelete:
    retf
#endasm
