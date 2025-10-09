/*
 * PROJECT:           ReactOS C Runtime Library
 * PURPOSE:           Optimized memmove implementation for amd64
 * COPYRIGHT:         Copyright 2025 Ahmed ARIF (arif.ing@outlook.com)
 */

#include <asm.inc>

/*
 * void *memmove(void *dst, const void *src, size_t count)
 *
 * Parameters (Windows x64 ABI):
 *   rcx - destination pointer
 *   rdx - source pointer
 *   r8  - count (number of bytes)
 *
 * Returns:
 *   rax - destination pointer
 *
 * Optimized implementation without SSE (bootloader-safe)
 * Uses only general-purpose registers for early boot compatibility
 * Properly handles overlapping regions
 */

PUBLIC memmove

/* FUNCTIONS *****************************************************************/
.code

FUNC memmove
    FPO 0, 3, 0, 0, 0, FRAME_FPO

    /* Save destination for return value */
    mov rax, rcx

    /* Check for zero count */
    test r8, r8
    jz memmove_done

    /* Check for overlap: if (dest > src) AND (dest < src + count) */
    cmp rcx, rdx
    jbe copy_forward                /* dest <= src, safe to copy forward */

    /* dest > src: check if dest < src + count (overlap check) */
    mov r9, rdx
    add r9, r8
    jc copy_backward                /* Overflow: use backward copy as safe default */
    cmp rcx, r9
    jae copy_forward                /* dest >= src + count, no overlap */

copy_backward:
    /* Overlapping and dest > src: must copy backward */
    /* Move to end of buffers */
    lea rdx, [rdx + r8 - 1]        /* src = src + n - 1 */
    lea rcx, [rcx + r8 - 1]        /* dest = dest + n - 1 */

    mov r9, r8                      /* r9 = count */

    /* Copy 8 bytes at a time backward */
    cmp r9, 8
    jb backward_small

backward_8byte_loop:
    cmp r9, 8
    jb backward_small
    sub rdx, 7
    sub rcx, 7
    mov r10, [rdx]
    mov [rcx], r10
    sub rdx, 1
    sub rcx, 1
    sub r9, 8
    jmp backward_8byte_loop

backward_small:
    /* Copy remaining bytes */
    test r9, r9
    jz memmove_done

backward_byte_loop:
    mov r10b, [rdx]
    mov [rcx], r10b
    dec rdx
    dec rcx
    dec r9
    jnz backward_byte_loop
    ret

copy_forward:
    /* Non-overlapping or safe to copy forward */
    mov r9, r8                      /* r9 = count */

    /* Copy 8 bytes at a time forward */
    cmp r9, 8
    jb forward_small

forward_8byte_loop:
    cmp r9, 8
    jb forward_small
    mov r10, [rdx]
    mov [rcx], r10
    add rdx, 8
    add rcx, 8
    sub r9, 8
    jmp forward_8byte_loop

forward_small:
    /* Handle remaining bytes (0-7) */
    test r9, r9
    jz memmove_done

    /* Try 4 bytes */
    cmp r9, 4
    jb forward_2byte
    mov r10d, [rdx]
    mov [rcx], r10d
    add rdx, 4
    add rcx, 4
    sub r9, 4

forward_2byte:
    /* Try 2 bytes */
    cmp r9, 2
    jb forward_1byte
    mov r10w, [rdx]
    mov [rcx], r10w
    add rdx, 2
    add rcx, 2
    sub r9, 2

forward_1byte:
    /* Try 1 byte */
    test r9, r9
    jz memmove_done
    mov r10b, [rdx]
    mov [rcx], r10b

memmove_done:
    ret

ENDFUNC

END