/*
 * PROJECT:     LiberNT CRT library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC implementation of _set_statfp for scalar libm
 */

#include <stdint.h>

/* Set the FPSCR sticky exception bits. The invalid-operation summary (VX)
 * cannot be written directly; VXSOFT (software-requested invalid operation)
 * sets it. */
void
_set_statfp(uintptr_t Mask)
{
    if (Mask & 0x0001) __asm__ __volatile__("mtfsb1 21"); /* invalid -> VXSOFT */
    if (Mask & 0x0004) __asm__ __volatile__("mtfsb1 5");  /* divide by zero -> ZX */
    if (Mask & 0x0008) __asm__ __volatile__("mtfsb1 3");  /* overflow -> OX */
    if (Mask & 0x0010) __asm__ __volatile__("mtfsb1 4");  /* underflow -> UX */
    if (Mask & 0x0020) __asm__ __volatile__("mtfsb1 6");  /* inexact -> XX */
}
