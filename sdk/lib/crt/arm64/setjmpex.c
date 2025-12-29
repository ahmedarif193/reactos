/*
 * PROJECT:     ReactOS CRT support
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM64 wrapper for __wine_setjmpex
 */

#include <setjmp.h>

int __cdecl __intrinsic_setjmpex(jmp_buf buffer, void *context);

int __cdecl __wine_setjmpex(jmp_buf buffer, void *context)
{
    return __intrinsic_setjmpex(buffer, context);
}
