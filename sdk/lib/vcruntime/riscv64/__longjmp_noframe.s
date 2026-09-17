/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V 64 longjmp helper
 */

    .text
    .p2align 2

    .global __longjmp_noframe
__longjmp_noframe:
    mv      t0, a0
    mv      a0, a1
    bnez    a0, .Lrestore_context
    li      a0, 1

.Lrestore_context:
    ld      ra, 8(t0)
    ld      sp, 16(t0)
    ld      s0, 24(t0)
    ld      s1, 32(t0)
    ld      s2, 40(t0)
    ld      s3, 48(t0)
    ld      s4, 56(t0)
    ld      s5, 64(t0)
    ld      s6, 72(t0)
    ld      s7, 80(t0)
    ld      s8, 88(t0)
    ld      s9, 96(t0)
    ld      s10, 104(t0)
    ld      s11, 112(t0)
    ret
