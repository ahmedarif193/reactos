/*
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Exercise NT PowerPC's extended setjmp import during a collided SEH unwind.
 * This object links only system imports, so the same image can run on NT4.
 */
#include <setjmpex.h>

unsigned ppc_jump_collided_probe(void)
{
    volatile unsigned Flags = 0;
    volatile unsigned Count = 0;
    jmp_buf Buffer;
    int Value = setjmp(Buffer);

    if (!Value)
    {
        __try
        {
            __try
            {
                __try
                {
                    Flags |= 1;
                    *(volatile int *)-1 = 123;
                }
                __finally
                {
                    ++Count;
                    Flags |= 2;
                    longjmp(Buffer, 1);
                }
            }
            __finally
            {
                ++Count;
                Flags |= 8;
            }
        }
        __except (1)
        {
            Flags |= 16;
        }
    }

    return (Value == 1 ? 1u : 0u) |
           (Flags == (1u | 2u | 8u) ? 2u : 0u) |
           (Count == 2 ? 4u : 0u);
}
