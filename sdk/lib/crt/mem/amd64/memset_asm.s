/*
 * PROJECT:     ReactOS C runtime library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     AMD64 memset
 * COPYRIGHT:   Copyright 2005-2020 Rich Felker, et al.
 */

#include <asm.inc>

.code64

PUBLIC memset
FUNC memset
    push rdi
    .pushreg rdi
    .endprolog

    mov rdi, rcx
    movzx eax, dl
    mov r9, HEX(0101010101010101)
    imul rax, r9

    cmp r8, 126
    ja .Large

    test r8d, r8d
    jz .Done

    mov [rdi], dl
    mov [rdi + r8 - 1], dl
    cmp r8d, 2
    jbe .Done

    mov [rdi + 1], ax
    mov [rdi + r8 - 3], ax
    cmp r8d, 6
    jbe .Done

    mov [rdi + 3], eax
    mov [rdi + r8 - 7], eax
    cmp r8d, 14
    jbe .Done

    mov [rdi + 7], rax
    mov [rdi + r8 - 15], rax
    cmp r8d, 30
    jbe .Done

    mov [rdi + 15], rax
    mov [rdi + 23], rax
    mov [rdi + r8 - 31], rax
    mov [rdi + r8 - 23], rax
    cmp r8d, 62
    jbe .Done

    mov [rdi + 31], rax
    mov [rdi + 39], rax
    mov [rdi + 47], rax
    mov [rdi + 55], rax
    mov [rdi + r8 - 63], rax
    mov [rdi + r8 - 55], rax
    mov [rdi + r8 - 47], rax
    mov [rdi + r8 - 39], rax

.Done:
    mov rax, rdi
    pop rdi
    ret

.Large:
    test edi, 15
    mov r9, rdi
    mov [rdi + r8 - 8], rax
    mov rcx, r8
    jnz .Align

.Fill:
    shr rcx, 3
    cld
    rep stosq
    mov rax, r9
    pop rdi
    ret

.Align:
    xor edx, edx
    sub edx, edi
    and edx, 15
    mov [rdi], rax
    mov [rdi + 8], rax
    sub rcx, rdx
    add rdi, rdx
    jmp .Fill
ENDFUNC

END
