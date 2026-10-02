/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC kernel stack switch
 *
 * PVOID KeSwitchKernelStack(PVOID StackBase <r3>, PVOID StackLimit <r4>)
 *
 * Returns the old stack base. The caller's frames continue on the new stack.
 */

#include <asm/ksppc.h>
#include <asm/kxppc.h>

LEAF_ENTRY(KeSwitchKernelStack)
    mflr 0
    stw 0, 8(1)
    stwu 1, -64(1)
    stw 3, 24(1)
    mfmsr 0
    stw 0, 28(1)
    rlwinm 0, 0, 0, 17, 15
    mtmsr 0
    isync
    mr 5, 1
    bl ..KiPpcPrepareKernelStack

    /* r3 = old base, r4 = new base - old base. Nonvolatile registers that
     * point into the live old stack [r1, r3) move with it, then r1 does. */
    lwz 4, 24(1)
    subf 4, 3, 4
    .irp n,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    cmplw \n, 1
    blt 1f
    cmplw \n, 3
    bge 1f
    add \n, \n, 4
1:
    .endr
    add 1, 1, 4
    lwz 0, 28(1)
    mtmsr 0
    isync
    addi 1, 1, 64
    lwz 0, 8(1)
    mtlr 0
    blr
LEAF_END(KeSwitchKernelStack)
