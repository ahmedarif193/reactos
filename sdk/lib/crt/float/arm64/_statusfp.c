/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Minimal ARM64 implementation of _statusfp
 */

#include <precomp.h>

unsigned int
_statusfp(void)
{
    unsigned long fpsr = 0;

    __asm__ __volatile__("mrs %0, fpsr" : "=r"(fpsr));
    return (unsigned int)fpsr;
}
