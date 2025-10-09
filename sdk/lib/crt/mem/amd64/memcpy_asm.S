/*
 * PROJECT:           ReactOS C Runtime Library
 * PURPOSE:           Optimized memcpy implementation for amd64
 * COPYRIGHT:         Copyright 2025 Ahmed ARIF (arif.ing@outlook.com)
 */

#include <asm.inc>

/*
 * void *memcpy(void *dst, const void *src, size_t count)
 *
 * Parameters:
 *   rcx - destination pointer
 *   rdx - source pointer
 *   r8  - count (number of bytes)
 *
 * Returns:
 *   rax - destination pointer
 *
 * Strategy:
 *   1. Handle small copies (< 32 bytes) with simple moves
 *   2. Align destination to 16-byte boundary
 *   3. Use SSE2 movdqa/movdqu for bulk copy (16 bytes per iteration)
 *   4. Use non-temporal stores (movntdq) for large copies (>= 2KB)
 *   5. Handle remaining bytes
 */

PUBLIC memcpy

/* FUNCTIONS *****************************************************************/
.code

FUNC memcpy
    FPO 0, 3, 0, 0, 0, FRAME_FPO

    /* Save destination for return value */
    mov rax, rcx

    /* Check for small copy (< 32 bytes) */
    cmp r8, 32
    jb SmallCopy

    /* Save non-volatile registers */
    mov r9, rsi
    mov r10, rdi

    /* Setup pointers */
    mov rdi, rcx                    /* rdi = destination */
    mov rsi, rdx                    /* rsi = source */
    mov rcx, r8                     /* rcx = count */

    /* Check if destination is 16-byte aligned */
    test rdi, 15
    jz AlignedCopy

    /* Align destination to 16-byte boundary */
    mov r8, rdi
    and r8, 15                      /* r8 = misalignment (0-15) */
    mov r11, 16
    sub r11, r8                     /* r11 = bytes to align */
    sub rcx, r11                    /* Adjust remaining count */

    /* Copy alignment bytes */
AlignLoop:
    mov al, [rsi]
    mov [rdi], al
    inc rsi
    inc rdi
    dec r11
    jnz AlignLoop

AlignedCopy:
    /* Now rdi is 16-byte aligned */
    /* Check if we should use non-temporal stores (>= 256KB) */
    cmp rcx, 262144
    jae LargeCopy

    /* Regular SSE2 copy for medium sizes */
    mov r11, rcx
    shr r11, 7                      /* r11 = count / 128 (8 x 16-byte chunks) */
    jz CopyRemaining64

    /* Main loop: copy 128 bytes per iteration (8 x 16-byte SSE2 moves) */
Copy128Loop:
    movdqu xmm0, [rsi]
    movdqu xmm1, [rsi + 16]
    movdqu xmm2, [rsi + 32]
    movdqu xmm3, [rsi + 48]
    movdqu xmm4, [rsi + 64]
    movdqu xmm5, [rsi + 80]
    movdqu xmm6, [rsi + 96]
    movdqu xmm7, [rsi + 112]

    movdqa [rdi], xmm0
    movdqa [rdi + 16], xmm1
    movdqa [rdi + 32], xmm2
    movdqa [rdi + 48], xmm3
    movdqa [rdi + 64], xmm4
    movdqa [rdi + 80], xmm5
    movdqa [rdi + 96], xmm6
    movdqa [rdi + 112], xmm7

    add rsi, 128
    add rdi, 128
    dec r11
    jnz Copy128Loop

CopyRemaining64:
    /* Copy remaining 64-byte chunks */
    mov r11, rcx
    and r11, 127                    /* r11 = remaining bytes */
    shr r11, 6                      /* r11 = remaining / 64 */
    jz CopyRemaining16

Copy64Loop:
    movdqu xmm0, [rsi]
    movdqu xmm1, [rsi + 16]
    movdqu xmm2, [rsi + 32]
    movdqu xmm3, [rsi + 48]

    movdqa [rdi], xmm0
    movdqa [rdi + 16], xmm1
    movdqa [rdi + 32], xmm2
    movdqa [rdi + 48], xmm3

    add rsi, 64
    add rdi, 64
    dec r11
    jnz Copy64Loop

CopyRemaining16:
    /* Copy remaining 16-byte chunks */
    mov r11, rcx
    and r11, 63                     /* r11 = remaining bytes */
    shr r11, 4                      /* r11 = remaining / 16 */
    jz CopyRemainingBytes

Copy16Loop:
    movdqu xmm0, [rsi]
    movdqa [rdi], xmm0
    add rsi, 16
    add rdi, 16
    dec r11
    jnz Copy16Loop

CopyRemainingBytes:
    /* Copy remaining bytes (0-15) */
    mov r11, rcx
    and r11, 15
    jz CopyDone

CopyByteLoop:
    mov r8b, [rsi]
    mov [rdi], r8b
    inc rsi
    inc rdi
    dec r11
    jnz CopyByteLoop

CopyDone:
    /* Restore non-volatile registers */
    mov rdi, r10
    mov rsi, r9
    ret

LargeCopy:
    /* Non-temporal copy for large buffers (>= 2KB) */
    /* This bypasses cache for better performance on large copies */
    mov r11, rcx
    shr r11, 7                      /* r11 = count / 128 */
    jz CopyRemaining64

LargeCopy128Loop:
    /* Prefetch next iteration's data (256 bytes ahead) */
    prefetchnta [rsi + 256]
    prefetchnta [rsi + 320]

    movdqu xmm0, [rsi]
    movdqu xmm1, [rsi + 16]
    movdqu xmm2, [rsi + 32]
    movdqu xmm3, [rsi + 48]
    movdqu xmm4, [rsi + 64]
    movdqu xmm5, [rsi + 80]
    movdqu xmm6, [rsi + 96]
    movdqu xmm7, [rsi + 112]

    movntdq [rdi], xmm0             /* Non-temporal stores */
    movntdq [rdi + 16], xmm1
    movntdq [rdi + 32], xmm2
    movntdq [rdi + 48], xmm3
    movntdq [rdi + 64], xmm4
    movntdq [rdi + 80], xmm5
    movntdq [rdi + 96], xmm6
    movntdq [rdi + 112], xmm7

    add rsi, 128
    add rdi, 128
    dec r11
    jnz LargeCopy128Loop

    sfence                          /* Ensure non-temporal stores complete */
    jmp CopyRemaining64

SmallCopy:
    /* Handle small copies (< 32 bytes) without SSE2 overhead */
    mov r9, r8
    cmp r9, 16
    jb SmallCopy8

    /* Copy 16 bytes */
    mov r10, [rdx]
    mov r11, [rdx + 8]
    mov [rcx], r10
    mov [rcx + 8], r11
    add rcx, 16
    add rdx, 16
    sub r9, 16

SmallCopy8:
    cmp r9, 8
    jb SmallCopy4

    /* Copy 8 bytes */
    mov r10, [rdx]
    mov [rcx], r10
    add rcx, 8
    add rdx, 8
    sub r9, 8

SmallCopy4:
    cmp r9, 4
    jb SmallCopy2

    /* Copy 4 bytes */
    mov r10d, [rdx]
    mov [rcx], r10d
    add rcx, 4
    add rdx, 4
    sub r9, 4

SmallCopy2:
    cmp r9, 2
    jb SmallCopy1

    /* Copy 2 bytes */
    mov r10w, [rdx]
    mov [rcx], r10w
    add rcx, 2
    add rdx, 2
    sub r9, 2

SmallCopy1:
    cmp r9, 1
    jb SmallCopyDone

    /* Copy 1 byte */
    mov r10b, [rdx]
    mov [rcx], r10b

SmallCopyDone:
    ret

ENDFUNC

END
