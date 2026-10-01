/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC system service invocation
 *
 * ULONG_PTR KiPpcInvokeSystemService(PVOID Routine <r3>, PULONG_PTR Arguments <r4>)
 *
 * Routine is a function descriptor. Arguments holds up to 32 words: the
 * first eight go in r3-r10, the rest in this frame's parameter area at
 * 56(r1), where the service reads them.
 */

#include <asm/ksppc.h>
#include <asm/kxppc.h>

#define FRAME_SIZE (56 + 24 * 4 + 8)

LEAF_ENTRY(KiPpcInvokeSystemService)
    mflr 0
    stw 0, 8(1)
    stwu 1, -FRAME_SIZE(1)
    stw 2, 4(1)
    lwz 11, 0(3)
    lwz 12, 4(3)
    mtctr 11
    .set slot, 0
    .rept 24
    lwz 0, 32 + slot * 4(4)
    stw 0, 56 + slot * 4(1)
    .set slot, slot + 1
    .endr
    mr 2, 12
    lwz 3, 0(4)
    lwz 5, 8(4)
    lwz 6, 12(4)
    lwz 7, 16(4)
    lwz 8, 20(4)
    lwz 9, 24(4)
    lwz 10, 28(4)
    lwz 4, 4(4)
    bctrl
    lwz 2, 4(1)
    addi 1, 1, FRAME_SIZE
    lwz 0, 8(1)
    mtlr 0
    blr
LEAF_END(KiPpcInvokeSystemService)
