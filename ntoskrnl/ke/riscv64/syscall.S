/*
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* Invoke an NT service using eight register and 24 possible stack slots.
 * The C dispatcher supplies a fully initialized 32-slot argument vector,
 * covering the descriptor's byte-sized argument-length field. */
    .text
    .balign 4
    .global KiRiscvInvokeSystemService
KiRiscvInvokeSystemService:
    addi sp, sp, -208
    sd ra, 200(sp)
    mv t0, a0
    mv t1, a1
    .set slot, 0
    .rept 24
    ld t2, 64 + slot * 8(t1)
    sd t2, slot * 8(sp)
    .set slot, slot + 1
    .endr
    .set slot, 0
    .irp reg, a0,a1,a2,a3,a4,a5,a6,a7
    ld \reg, slot * 8(t1)
    .set slot, slot + 1
    .endr
    jalr t0
    ld ra, 200(sp)
    addi sp, sp, 208
    ret
