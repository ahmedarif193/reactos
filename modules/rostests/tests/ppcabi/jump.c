/*
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Compile ordinary setjmp with Clang's NT PowerPC builtin enabled so the
 * resulting binary imports the name used by the original NT4 PPC MSVCRT.
 */
#include <setjmp.h>

static __declspec(noinline) void jump_with(jmp_buf Buffer, int Value)
{
    longjmp(Buffer, Value);
}

unsigned ppc_jump_zero_probe(void)
{
    jmp_buf Buffer;
    volatile unsigned Marker = 1;
    int Value = setjmp(Buffer);

    if (!Value)
    {
        Marker = 7;
        jump_with(Buffer, 0);
    }
    return (Value == 1 ? 1u : 0u) | (Marker == 7 ? 2u : 0u);
}

unsigned ppc_jump_value_probe(void)
{
    jmp_buf Buffer;
    volatile unsigned Marker = 3;
    int Value = setjmp(Buffer);

    if (!Value)
    {
        Marker = 9;
        jump_with(Buffer, 37);
    }
    return (Value == 37 ? 1u : 0u) | (Marker == 9 ? 2u : 0u);
}
