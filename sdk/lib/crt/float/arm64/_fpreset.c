/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM64 implementation of _fpreset
 * COPYRIGHT:   Copyright 2025 ReactOS Project
 *
 * On AArch64, _fpreset resets the FPCR (Floating-Point Control Register) and
 * FPSR (Floating-Point Status Register) to their default values (both zero).
 * FPCR=0 means round-to-nearest, all exception traps disabled.
 * FPSR=0 means no cumulative exception flags set.
 */

#include <precomp.h>

void __cdecl _fpreset(void)
{
    /* Reset FPCR to default (all exceptions disabled, round-to-nearest) */
    __asm__ __volatile__("msr fpcr, xzr");
    /* Clear all FPSR sticky flags */
    __asm__ __volatile__("msr fpsr, xzr");
}
