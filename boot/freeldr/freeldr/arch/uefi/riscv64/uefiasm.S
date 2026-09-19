/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V64 UEFI stack and NT handoff helpers
 */

    .option push
    .option norelax
    .text

    .extern UefiServiceStack
    .extern BasicStack
    .extern ExecuteLoaderCleanly
    .extern UefiExitBootServices

    .globl _changestack
_changestack:
    /* a0 is the firmware-owned stack passed to ExecuteLoaderCleanly. */
    mv a0, sp
    la t0, BasicStack
    ld sp, 0(t0)
    andi sp, sp, -16
    tail ExecuteLoaderCleanly

    .globl _exituefi
_exituefi:
    /* Keep the BasicStack frame address and return PC in callee-saved
     * registers while the firmware call runs on its original stack. */
    addi sp, sp, -32
    sd s0, 0(sp)
    sd s1, 8(sp)
    sd ra, 16(sp)
    mv s0, sp
    mv s1, ra

    la t0, UefiServiceStack
    ld t0, 0(t0)
    beqz t0, 1f
    andi sp, t0, -16
1:
    call UefiExitBootServices
    fence.i

    mv sp, s0
    mv ra, s1
    ld s1, 8(sp)
    ld s0, 0(sp)
    addi sp, sp, 32
    ret

    .globl RiscvJumpToKernel
RiscvJumpToKernel:
    /* C arguments are entry, loader block, and initial stack. The private
     * NT handoff keeps only the loader block in a0. */
    mv t0, a0
    mv sp, a2
    andi sp, sp, -16
    mv a0, a1
    la ra, .Lhandoff_fault
    la t1, .Lhandoff_fault
    csrw stvec, t1
    fence.i
    jr t0

.Lhandoff_fault:
    csrci sstatus, 2
    csrw sie, zero
2:
    wfi
    j 2b

    .option pop
