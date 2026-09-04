/*
 * PROJECT:     ReactOS C runtime library
 * LICENSE:     BSD-2-Clause (https://spdx.org/licenses/BSD-2-Clause)
 * PURPOSE:     AMD64 memcpy and memmove
 * COPYRIGHT:   Copyright 2018 The FreeBSD Foundation
 *              Developed by Mateusz Guzik <mjg@FreeBSD.org>
 */

#include <asm.inc>

.code64

PUBLIC memcpy
PUBLIC memmove
memcpy:
FUNC memmove
    push rdi
    .pushreg rdi
    push rsi
    .pushreg rsi
    .endprolog

    mov rdi, rcx
    mov rsi, rdx
    mov rdx, r8
    mov rax, rdi
    mov rcx, rdx

    cmp rcx, 32
    jbe .L32

    mov r8, rdi
    sub r8, rsi
    cmp r8, rcx
    jb .Bwd

    cmp rcx, 512
    ja .FwdBig

    .align 16
.Fwd32Loop:
    mov rdx, [rsi]
    mov [rdi], rdx
    mov rdx, [rsi + 8]
    mov [rdi + 8], rdx
    mov rdx, [rsi + 16]
    mov [rdi + 16], rdx
    mov rdx, [rsi + 24]
    mov [rdi + 24], rdx
    lea rsi, [rsi + 32]
    lea rdi, [rdi + 32]
    sub rcx, 32
    cmp rcx, 32
    jae .Fwd32Loop
    cmp cl, 0
    jne .L32
    pop rsi
    pop rdi
    ret

    .align 16
.L32:
    cmp cl, 16
    jl .L16
    mov rdx, [rsi]
    mov r8, [rsi + 8]
    mov r9, [rsi + rcx - 16]
    mov r10, [rsi + rcx - 8]
    mov [rdi], rdx
    mov [rdi + 8], r8
    mov [rdi + rcx - 16], r9
    mov [rdi + rcx - 8], r10
    pop rsi
    pop rdi
    ret

    .align 16
.L16:
    cmp cl, 8
    jl .L8
    mov rdx, [rsi]
    mov r8, [rsi + rcx - 8]
    mov [rdi], rdx
    mov [rdi + rcx - 8], r8
    pop rsi
    pop rdi
    ret

    .align 16
.L8:
    cmp cl, 4
    jl .L4
    mov edx, [rsi]
    mov r8d, [rsi + rcx - 4]
    mov [rdi], edx
    mov [rdi + rcx - 4], r8d
    pop rsi
    pop rdi
    ret

    .align 16
.L4:
    cmp cl, 2
    jl .L2
    movzx edx, word ptr [rsi]
    movzx r8d, word ptr [rsi + rcx - 2]
    mov [rdi], dx
    mov [rdi + rcx - 2], r8w
    pop rsi
    pop rdi
    ret

    .align 16
.L2:
    cmp cl, 1
    jl .L0
    mov dl, [rsi]
    mov [rdi], dl
.L0:
    pop rsi
    pop rdi
    ret

    .align 16
.FwdBig:
    mov r8, rsi
    sub r8, rdi
    cmp r8, rcx
    jb .Fwd32Loop
    test dil, 15
    jnz .FwdBigUnaligned
    shr rcx, 3
    cld
    rep movsq
    mov rcx, rdx
    and ecx, 7
    jne .L8
    pop rsi
    pop rdi
    ret

.FwdBigUnaligned:
    mov r8, [rsi]
    mov r9, [rsi + 8]
    mov r10, rdi
    mov rcx, rdi
    and rcx, 15
    lea rdx, [rdx + rcx - 16]
    neg rcx
    lea rdi, [rdi + rcx + 16]
    lea rsi, [rsi + rcx + 16]
    mov rcx, rdx
    shr rcx, 3
    cld
    rep movsq
    mov [r10], r8
    mov [r10 + 8], r9
    mov rcx, rdx
    and ecx, 7
    jne .L8
    pop rsi
    pop rdi
    ret

    .align 16
.Bwd:
    lea rdi, [rdi + rcx - 8]
    lea rsi, [rsi + rcx - 8]
    cmp rcx, 32
    jb .B16

    .align 16
.Bwd32Loop:
    mov rdx, [rsi]
    mov [rdi], rdx
    mov rdx, [rsi - 8]
    mov [rdi - 8], rdx
    mov rdx, [rsi - 16]
    mov [rdi - 16], rdx
    mov rdx, [rsi - 24]
    mov [rdi - 24], rdx
    lea rsi, [rsi - 32]
    lea rdi, [rdi - 32]
    sub rcx, 32
    cmp rcx, 32
    jae .Bwd32Loop
    cmp cl, 0
    jne .B16
    pop rsi
    pop rdi
    ret

    .align 16
.B16:
    cmp cl, 16
    jl .B8
    mov rdx, [rsi]
    mov [rdi], rdx
    mov rdx, [rsi - 8]
    mov [rdi - 8], rdx
    sub cl, 16
    jz .BDone
    lea rsi, [rsi - 16]
    lea rdi, [rdi - 16]
.B8:
    cmp cl, 8
    jl .B4
    mov rdx, [rsi]
    mov [rdi], rdx
    sub cl, 8
    jz .BDone
    lea rsi, [rsi - 8]
    lea rdi, [rdi - 8]
.B4:
    cmp cl, 4
    jl .B2
    mov edx, [rsi + 4]
    mov [rdi + 4], edx
    sub cl, 4
    jz .BDone
    lea rsi, [rsi - 4]
    lea rdi, [rdi - 4]
.B2:
    cmp cl, 2
    jl .B1
    mov dx, [rsi + 6]
    mov [rdi + 6], dx
    sub cl, 2
    jz .BDone
    lea rsi, [rsi - 2]
    lea rdi, [rdi - 2]
.B1:
    cmp cl, 1
    jl .BDone
    mov dl, [rsi + 7]
    mov [rdi + 7], dl
.BDone:
    pop rsi
    pop rdi
    ret
ENDFUNC

END
