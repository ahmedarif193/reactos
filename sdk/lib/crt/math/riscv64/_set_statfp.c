/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V64 implementation of _set_statfp for scalar libm
 */

#include <stdint.h>

void
_set_statfp(uintptr_t Mask)
{
    uintptr_t Flags;
    uintptr_t Raised = 0;

    if (Mask & 0x0001) Raised |= 0x10; /* invalid operation -> NV */
    if (Mask & 0x0004) Raised |= 0x08; /* divide by zero -> DZ */
    if (Mask & 0x0008) Raised |= 0x04; /* overflow -> OF */
    if (Mask & 0x0010) Raised |= 0x02; /* underflow -> UF */
    if (Mask & 0x0020) Raised |= 0x01; /* inexact -> NX */

    __asm__ __volatile__("frflags %0" : "=r"(Flags));
    Flags |= Raised;
    __asm__ __volatile__("fsflags %0" :: "r"(Flags));
}
