/*
 * PROJECT:     ReactOS C runtime library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     AMD64 memcpy and memmove
 */

#include <asm.inc>

.code64

PUBLIC memcpy
PUBLIC memmove
memcpy:
FUNC memmove
    .endprolog

    mov rax, rcx
    test r8, r8
    jz .Done
    cmp rcx, rdx
    je .Done
    jb .CopyForward
    lea r9, [rdx + r8]
    cmp rcx, r9
    jae .CopyForward

    /* The destination starts inside the source range. Copy from the end. */
    add rcx, r8
    add rdx, r8
.CopyBackwardBlocks:
    cmp r8, 64
    jb .CopyBackwardPairs
    sub rcx, 64
    sub rdx, 64
    mov r9, [rdx + 56]
    mov [rcx + 56], r9
    mov r10, [rdx + 48]
    mov [rcx + 48], r10
    mov r9, [rdx + 40]
    mov [rcx + 40], r9
    mov r10, [rdx + 32]
    mov [rcx + 32], r10
    mov r9, [rdx + 24]
    mov [rcx + 24], r9
    mov r10, [rdx + 16]
    mov [rcx + 16], r10
    mov r9, [rdx + 8]
    mov [rcx + 8], r9
    mov r10, [rdx]
    mov [rcx], r10
    sub r8, 64
    jmp .CopyBackwardBlocks

.CopyBackwardPairs:
    cmp r8, 16
    jb .CopyBackwardWord
    sub rcx, 16
    sub rdx, 16
    mov r9, [rdx]
    mov r10, [rdx + 8]
    mov [rcx], r9
    mov [rcx + 8], r10
    sub r8, 16
    jmp .CopyBackwardPairs

.CopyBackwardWord:
    cmp r8, 8
    jb .CopyBackwardBytes
    sub rcx, 8
    sub rdx, 8
    mov r9, [rdx]
    mov [rcx], r9
    sub r8, 8

.CopyBackwardBytes:
    test r8, r8
    jz .Done
    dec rcx
    dec rdx
    mov r9b, [rdx]
    mov [rcx], r9b
    dec r8
    jmp .CopyBackwardBytes

.CopyForward:
    cmp r8, 64
    jb .CopyForwardPairs
    mov r9, [rdx]
    mov [rcx], r9
    mov r10, [rdx + 8]
    mov [rcx + 8], r10
    mov r9, [rdx + 16]
    mov [rcx + 16], r9
    mov r10, [rdx + 24]
    mov [rcx + 24], r10
    mov r9, [rdx + 32]
    mov [rcx + 32], r9
    mov r10, [rdx + 40]
    mov [rcx + 40], r10
    mov r9, [rdx + 48]
    mov [rcx + 48], r9
    mov r10, [rdx + 56]
    mov [rcx + 56], r10
    add rcx, 64
    add rdx, 64
    sub r8, 64
    jmp .CopyForward

.CopyForwardPairs:
    cmp r8, 16
    jb .CopyForwardWord
    mov r9, [rdx]
    mov r10, [rdx + 8]
    mov [rcx], r9
    mov [rcx + 8], r10
    add rcx, 16
    add rdx, 16
    sub r8, 16
    jmp .CopyForwardPairs

.CopyForwardWord:
    cmp r8, 8
    jb .CopyForwardBytes
    mov r9, [rdx]
    mov [rcx], r9
    add rcx, 8
    add rdx, 8
    sub r8, 8

.CopyForwardBytes:
    test r8, r8
    jz .Done
    mov r9b, [rdx]
    mov [rcx], r9b
    inc rcx
    inc rdx
    dec r8
    jmp .CopyForwardBytes

.Done:
    ret
ENDFUNC

END
