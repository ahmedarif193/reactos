/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM64 implementation of _set_statfp for libm
 */

#include <stdint.h>

void _set_statfp(uintptr_t mask)
{
    uintptr_t fpsr;
    uintptr_t flags = 0;

    if (mask & 0x0001) flags |= 0x01; /* invalid operation */
    if (mask & 0x0002) flags |= 0x80; /* denormal input */
    if (mask & 0x0004) flags |= 0x02; /* divide by zero */
    if (mask & 0x0008) flags |= 0x04; /* overflow */
    if (mask & 0x0010) flags |= 0x08; /* underflow */
    if (mask & 0x0020) flags |= 0x10; /* inexact */

    __asm__ __volatile__("mrs %0, fpsr" : "=r"(fpsr));
    fpsr |= flags;
    __asm__ __volatile__("msr fpsr, %0" :: "r"(fpsr));
}
