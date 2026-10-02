#include "valid.h"

/* Hand-written in asm - the equivalent C loop (digit accumulation,
   octet/digit counters, three exit conditions) costs noticeably more
   in the small memory model's calling convention than the same logic
   written directly. */
int is_valid_ipv4(text)
char *text;
{
#asm
    push si
    push di
    push bp
    mov bx,sp
    mov si,8[bx]
    xor bx,bx
.ipv4_octet:
    xor bp,bp
    xor cx,cx
.ipv4_digit:
    mov al,[si]
    cmp al,#$30
    jb .ipv4_digit_done
    cmp al,#$39
    ja .ipv4_digit_done
    cmp cl,#3
    jae .ipv4_fail
    sub al,#$30
    xor ah,ah
    mov di,ax
    mov ax,bp
    mov bp,#10
    mul bp
    add ax,di
    mov bp,ax
    inc cl
    inc si
    jmp .ipv4_digit
.ipv4_digit_done:
    or cl,cl
    jz .ipv4_fail
    cmp bp,#255
    ja .ipv4_fail
    inc bl
    mov al,[si]
    or al,al
    jz .ipv4_done
    cmp al,#$2E
    jne .ipv4_fail
    cmp bl,#4
    je .ipv4_fail
    inc si
    jmp .ipv4_octet
.ipv4_done:
    cmp bl,#4
    jne .ipv4_fail
    mov ax,#1
    jmp .ipv4_exit
.ipv4_fail:
    xor ax,ax
.ipv4_exit:
    pop bp
    pop di
    pop si
#endasm
}

int is_valid_prefix(text)
char *text;
{
#asm
    push si
    push bp
    mov bx,sp
    mov si,6[bx]
    xor bp,bp
    xor cx,cx
.prefix_digit:
    mov al,[si]
    cmp al,#$30
    jb .prefix_digit_done
    cmp al,#$39
    ja .prefix_digit_done
    sub al,#$30
    xor ah,ah
    push ax
    mov ax,bp
    mov bp,#10
    mul bp
    pop bx
    add ax,bx
    mov bp,ax
    inc cx
    inc si
    jmp .prefix_digit
.prefix_digit_done:
    or cx,cx
    jz .prefix_fail
    cmp byte ptr [si],#0
    jne .prefix_fail
    cmp bp,#32
    ja .prefix_fail
    mov ax,#1
    jmp .prefix_exit
.prefix_fail:
    xor ax,ax
.prefix_exit:
    pop bp
    pop si
#endasm
}

/* Hand-written in asm, same rationale as is_valid_ipv4() above - only
   takes two pointer params (char* in, unsigned char* out), no calls
   to other C functions from inside. Runs only after is_valid_ipv4()
   already validated the string, so no range/format checking here:
   just the same digit-accumulation loop, writing each octet to out[]
   and skipping the '.' instead of checking it. */
void parse_ipv4(text, out)
char *text;
unsigned char *out;
{
#asm
    push si
    push di
    push bp
    mov bx,sp
    mov si,8[bx]
    mov di,10[bx]
    mov bx,#4
.pip4_octet:
    xor bp,bp
.pip4_digit:
    mov al,[si]
    cmp al,#$30
    jb .pip4_digit_done
    cmp al,#$39
    ja .pip4_digit_done
    sub al,#$30
    xor ah,ah
    xchg ax,bp
    mov cx,#10
    mul cx
    add ax,bp
    mov bp,ax
    inc si
    jmp .pip4_digit
.pip4_digit_done:
    mov ax,bp
    mov [di],al
    inc di
    inc si
    dec bx
    jnz .pip4_octet
    pop bp
    pop di
    pop si
#endasm
}

/* Hand-written in asm, same rationale as is_valid_ipv4() above - only
   takes one char* param, no calls to other C functions from inside.
   Runs only after is_valid_prefix() already validated the string:
   parses the 0-32 decimal prefix length into a byte. Expanding this
   to a literal dotted netmask is the ESP's job, not the client's -
   SET_NETIF carries the prefix length as-is (see PFTC_PROTOCOL.md). */
int parse_prefix(text)
char *text;
{
#asm
    push si
    push bp
    mov bx,sp
    mov si,6[bx]
    xor bp,bp
.pprefix_digit:
    mov al,[si]
    cmp al,#$30
    jb .pprefix_done
    cmp al,#$39
    ja .pprefix_done
    sub al,#$30
    xor ah,ah
    xchg ax,bp
    mov cx,#10
    mul cx
    add ax,bp
    mov bp,ax
    inc si
    jmp .pprefix_digit
.pprefix_done:
    mov ax,bp
    pop bp
    pop si
#endasm
}
