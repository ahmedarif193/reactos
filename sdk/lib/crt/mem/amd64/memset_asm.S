/*
 * COPYRIGHT:         See COPYING in the top level directory
 * PROJECT:           ReactOS C Runtime Library
 * PURPOSE:           memset implementation for amd64
 * FILE:              sdk/lib/crt/mem/amd64/memset_asm.S
 */

#include <asm.inc>

/*
 * void *memset(void *dst, int val, size_t count)
 *
 * Parameters:
 *   rcx - dst pointer
 *   edx - value (byte)
 *   r8  - count
 *
 * Returns:
 *   rax - dst pointer
 */

PUBLIC memset

/* FUNCTIONS *****************************************************************/
.code

FUNC memset
    FPO 0, 3, 2, 0, 0, FRAME_FPO

    /* Save registers */
    push rdi
    push rsi

    /* Setup registers for string operations */
    mov rdi, rcx                    /* rdi = destination */
    mov r9, rcx                     /* r9 = save original dst for return */
    movzx r11, dl                   /* r11 = save byte value */
    mov rax, r11                    /* rax = byte value for stosb/stosq */
    mov rcx, r8                     /* rcx = count */

    /* Clear direction flag */
    cld

    /* Check if count is less than 16 bytes */
    cmp rcx, 16
    jb ByteFill

    /* Expand byte value to fill entire rax (8 bytes) */
    mov r10, HEX(0101010101010101)
    imul rax, r10                   /* Replicate byte across all 8 bytes */

    /* Save count */
    mov rdx, rcx

    /* Check if destination is already 8-byte aligned */
    test rdi, 7
    je QwordFill

    /* Align destination to 8-byte boundary */
    mov rcx, rdi
    and rcx, 7                      /* Get misalignment (0-7) */
    neg rcx
    add rcx, 8                      /* Calculate bytes needed to align */
    sub rdx, rcx                    /* Adjust count */

    /* Fill alignment bytes using stosb */
    rep stosb

    /* Restore expanded qword value and count */
    mov rcx, rdx
    mov rax, r11                    /* Get back the byte value */
    mov r10, HEX(0101010101010101)
    imul rax, r10

QwordFill:
    /* Fill using qwords (rep stosq) */
    mov rdx, rcx
    shr rcx, 3                      /* rcx = count / 8 */
    jz CheckRemainder
    rep stosq

CheckRemainder:
    /* Fill remaining bytes (0-7) */
    mov rcx, rdx
    and rcx, 7
    jz Done

ByteFill:
    /* Fill remaining bytes using stosb */
    rep stosb

Done:
    /* Return original pointer */
    mov rax, r9

    /* Restore registers */
    pop rsi
    pop rdi
    ret

ENDFUNC

    /* no external references here */

END
