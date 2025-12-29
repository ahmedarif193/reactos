/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Minimal ARM64 implementation of _clearfp
 */

#include <precomp.h>

unsigned int
_clearfp(void)
{
    unsigned long fpsr = 0;

    __asm__ __volatile__("mrs %0, fpsr" : "=r"(fpsr));
    __asm__ __volatile__("msr fpsr, xzr");

    /* No precise mapping yet; return previous raw status for diagnostics */
    return (unsigned int)fpsr;
}
