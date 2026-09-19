/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* Native kernel-stack migration. SP changes only after the C helper returns. */
#include <asm/ksriscv64.h>

    .section .text$KeSwitchKernelStack, "xr"
    .balign 4
    .global KeSwitchKernelStack
KeSwitchKernelStack:
    addi sp, sp, -48
    sd ra, 40(sp)
    sd s0, 32(sp)
    addi s0, sp, 48
    sd a0, 0(sp)
    /* A guarded region masks APC delivery, not clock preemption. */
    csrrci t0, sstatus, 2
    andi t0, t0, 2
    sd t0, 16(sp)
    mv a2, sp
    mv a3, s0
    call KiRiscvPrepareKernelStack

    /* a0 is the old allocation base, returned unchanged to the caller. */
    ld t1, 0(sp)
    sub t1, t1, a0
    add sp, sp, t1
    ld ra, 40(sp)
    /* The copied frame record already contains the relocated caller s0. */
    ld s0, 32(sp)
    ld t0, 16(sp)
    addi sp, sp, 48
    csrs sstatus, t0
    ret
