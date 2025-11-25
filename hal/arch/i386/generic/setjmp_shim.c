/*
 * PROJECT:         ReactOS HAL
 * LICENSE:         GPL-2.0-or-later - See COPYING in the top level directory
 * FILE:            hal/arch/i386/generic/setjmp_shim.c
 * PURPOSE:         Minimal setjmp/longjmp shim for freestanding HAL builds
 * REASON:         Provide freestanding setjmp coverage after moving HAL sources
 * COPYRIGHT:       Copyright (c) Ahmed ARIF (arif.ing@outlook.com)
 */

#include <setjmp.h>
#include <stddef.h>

/* Track the last longjmp target/value so we can honor the value argument. */
static volatile void *HalpLongjmpTarget;
static volatile int HalpLongjmpValue;

/*
 * Provide minimal C setjmp/longjmp implementations for the HAL. These wrap the
 * compiler builtins so we do not depend on CRT user-mode stubs.
 */
int __cdecl _setjmp(jmp_buf buffer)
{
    int ret = __builtin_setjmp((void **)buffer);

    if (ret == 0)
        return 0;

    if (HalpLongjmpTarget == buffer)
    {
        ret = HalpLongjmpValue;
        HalpLongjmpTarget = NULL;
    }

    return ret;
}

__declspec(noreturn)
void __cdecl longjmp(jmp_buf buffer, int value)
{
    const int jump_value = value ? value : 1;

    HalpLongjmpTarget = buffer;
    HalpLongjmpValue = jump_value;

    __builtin_longjmp((void **)buffer, 1);
    __builtin_unreachable();
}
