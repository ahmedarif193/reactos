/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Helpers for converting between virtual and physical addresses
 */

#include <freeldr.h>

PVOID
VaToPa(PVOID Va)
{
#ifndef _ZOOM2_
    return (PVOID)((ULONG_PTR)Va & ~KSEG0_BASE);
#else
    return Va;
#endif
}

PVOID
PaToVa(PVOID Pa)
{
#ifndef _ZOOM2_
    return (PVOID)((ULONG_PTR)Pa | KSEG0_BASE);
#else
    return Pa;
#endif
}
