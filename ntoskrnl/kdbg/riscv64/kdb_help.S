/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     KDBG helper to switch to the debugger stack and call a routine
 */

    .text
    .balign 4
    .globl KdbpStackSwitchAndCall

/*
 * VOID NTAPI KdbpStackSwitchAndCall(PVOID NewStack, VOID (*Function)(VOID))
 *   a0 = NewStack (16-byte aligned top of the debugger stack)
 *   a1 = Function
 *
 * The original sp is kept in s1 (callee-saved) across the call. A frame
 * record is pushed on the new stack so the frame-pointer chain stays intact.
 */
KdbpStackSwitchAndCall:
    addi    sp, sp, -32
    sd      ra, 24(sp)
    sd      s0, 16(sp)
    sd      s1, 8(sp)
    addi    s0, sp, 32

    mv      s1, sp
    andi    a0, a0, -16
    mv      sp, a0

    addi    sp, sp, -16
    sd      ra, 8(sp)
    sd      s0, 0(sp)
    addi    s0, sp, 16

    jalr    ra, a1, 0

    ld      s0, 0(sp)
    ld      ra, 8(sp)
    addi    sp, sp, 16

    mv      sp, s1
    ld      s1, 8(sp)
    ld      s0, 16(sp)
    ld      ra, 24(sp)
    addi    sp, sp, 32
    ret
