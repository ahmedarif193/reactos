/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL-2.0-or-later - See COPYING in the top level directory
 * FILE:            hal/arch/i386/generic/setjmp_shim.c
 * PURPOSE:         Minimal setjmp/longjmp shim for freestanding HAL builds
 * REASON:         Provide freestanding setjmp coverage after moving HAL sources
 * COPYRIGHT:       Copyright (c) Ahmed ARIF (arif.ing@outlook.com)
 */

#include <setjmp.h>

/*
 * Provide minimal C setjmp/longjmp implementations for the HAL. These wrap the
 * compiler builtins so we do not depend on CRT user-mode stubs.
 */
int __cdecl _setjmp(jmp_buf buffer)
{
    return __builtin_setjmp(buffer);
}

__declspec(noreturn)
void __cdecl longjmp(jmp_buf buffer, int value)
{
    (void)value;
    __builtin_longjmp(buffer, 1);
    __builtin_unreachable();
}
