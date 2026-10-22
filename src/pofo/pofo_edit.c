#include "pofo.h"
#include "tinylib.h"

/* Exact INT 60h/AH=01h descriptor offsets. bcc pads structs to even
   offsets, and the ROM's layout has words at odd ones (HIT at 11), so
   the block is a plain byte array and words go in through
   POFO_EDIT_WORD - the 8086 doesn't mind the unaligned access. */
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

#define POFO_EDIT_WORD(offset) (*(unsigned int *)(pofo_edit_block + (offset)))

static char pofo_edit_title_prompt[POFO_EDIT_TITLE_PROMPT_SIZE];
static unsigned int pofo_edit_result;

extern unsigned char pofo_edit_block[];
extern pofo_edit_getkey();
extern pofo_edit_undelete();

/* The descriptor lives in the data segment with its constant fields
   (title, getkey and undelete offsets) filled in by the linker; the
   per-call fields and the segments are stored by pofo_line_edit(). */
#asm
    .data
_pofo_edit_block:
    .word 0                         ! 0  target offset
    .word 0                         ! 2  target segment
    .word 0                         ! 4  position
    .word 0                         ! 6  max length
    .byte 0                         ! 8  x
    .byte 0                         ! 9  y
    .byte 0                         ! 10 mode
    .word 0                         ! 11 hit key
    .word _pofo_edit_title_prompt   ! 13 title/prompt offset
    .word 0                         ! 15 title/prompt segment
    .word 0                         ! 17 exit keys offset
    .word 0                         ! 19 exit keys segment
    .word _pofo_edit_getkey         ! 21 getkey offset
    .word 0                         ! 23 getkey segment
    .byte 0                         ! 25 width
    .byte 0                         ! 26 window box
    .word 0                         ! 27 reserved
    .word 0                         ! 29 reserved
    .word _pofo_edit_undelete       ! 31 undelete offset
    .word 0                         ! 33 undelete segment
    .text
#endasm

int pofo_line_edit(top_left, title, prompt, value, max, width, mode,
                   window, exit_keys)
unsigned int top_left;
char *title;
char *prompt;
char *value;
unsigned int max;
unsigned char mode;
unsigned char width;
unsigned char window;
unsigned int *exit_keys;
{
    char *p;

    if (str_len(title) + str_len(prompt) + 3 > POFO_EDIT_TITLE_PROMPT_SIZE)
        return POFO_EDIT_TEXT_TOO_LONG;

    p = str_copy(pofo_edit_title_prompt, title) + 1;
    p = str_copy(p, prompt) + 1;
    *p = 0;

    POFO_EDIT_WORD(POFO_EDIT_TARGET_OFFSET) = (unsigned int)value;
    POFO_EDIT_WORD(POFO_EDIT_POS_OFFSET) = 0;
    POFO_EDIT_WORD(POFO_EDIT_MAX_OFFSET) = max;
    pofo_edit_block[POFO_EDIT_XPOS_OFFSET] = POFO_COORD_COL(top_left);
    pofo_edit_block[POFO_EDIT_YPOS_OFFSET] = POFO_COORD_ROW(top_left);
    pofo_edit_block[POFO_EDIT_MODE_OFFSET] = mode;
    POFO_EDIT_WORD(POFO_EDIT_HIT_OFFSET) = 0;
    POFO_EDIT_WORD(POFO_EDIT_EXIT_OFFSET) = (unsigned int)exit_keys;
    pofo_edit_block[POFO_EDIT_WIDTH_OFFSET] = width;
    pofo_edit_block[POFO_EDIT_WINDOW_OFFSET] = window;
    POFO_EDIT_WORD(POFO_EDIT_RESERVED1_OFFSET) = 0;
    POFO_EDIT_WORD(POFO_EDIT_RESERVED2_OFFSET) = 0;

#asm
    push si
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
    pop si
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
