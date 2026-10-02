/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC kernel entry point
 *
 * FreeLoader enters through the KiSystemStartup descriptor with r3 = the
 * loader block, r1 = the KSEG0 boot stack and translation on (see arc.h).
 * External interrupts stay off until the HAL has installed its sources.
 */

#include <asm/ksppc.h>
#include <asm/kxppc.h>

LEAF_ENTRY(KiSystemStartup)
    mfmsr 0
    rlwinm 0, 0, 0, 17, 15
    mtmsr 0
    isync
    li 0, 0
    stwu 0, -64(1)
    b ..KiPpcSystemStartup
LEAF_END(KiSystemStartup)
