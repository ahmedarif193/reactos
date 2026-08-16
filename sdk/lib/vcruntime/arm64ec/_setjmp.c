/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64EC setjmp register capture
 * COPYRIGHT:   Copyright 2023 Alexandre Julliard
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Adapted from Wine's dlls/ntdll/signal_arm64ec.c.
 */

__attribute__((naked, returns_twice))
int __cdecl
__intrinsic_setjmp(void *JumpBuffer, void *Frame)
{
    __asm__ volatile(
        "stp x1, x27, [x0, #0x00]\n"
        "mov x3, sp\n"
        "stp x3, x29, [x0, #0x10]\n"
        "stp x25, x26, [x0, #0x20]\n"
        "stp x19, x20, [x0, #0x30]\n"
        "stp x21, x22, [x0, #0x40]\n"
        "str x30, [x0, #0x50]\n"
        "stp d8, d9, [x0, #0x80]\n"
        "stp d10, d11, [x0, #0xa0]\n"
        "stp d12, d13, [x0, #0xc0]\n"
        "stp d14, d15, [x0, #0xe0]\n"
        "mrs x1, fpcr\n"
        "mrs x2, fpsr\n"
        "b \"#ChpepSetJmpFinalize\"\n");
}

extern __typeof(__intrinsic_setjmp) _setjmp __attribute__((alias("__intrinsic_setjmp")));
extern __typeof(__intrinsic_setjmp) _setjmpex __attribute__((alias("__intrinsic_setjmp")));
extern __typeof(__intrinsic_setjmp) __intrinsic_setjmpex __attribute__((alias("__intrinsic_setjmp")));
