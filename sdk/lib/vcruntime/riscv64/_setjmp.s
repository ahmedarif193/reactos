/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Implementation of setjmp for RISC-V 64
 */

    .text
    .p2align 2

    .global _setjmp
_setjmp:
    li      t0, 0
    j       .Lsave_context

    .global __intrinsic_setjmp
__intrinsic_setjmp:
    li      t0, 0
    j       .Lsave_context

    .global _setjmpex
_setjmpex:
    mv      t0, a1
    j       .Lsave_context

    .global __intrinsic_setjmpex
__intrinsic_setjmpex:
    mv      t0, a1

.Lsave_context:
    sd      t0, 0(a0)
    sd      ra, 8(a0)
    sd      sp, 16(a0)
    sd      s0, 24(a0)
    sd      s1, 32(a0)
    sd      s2, 40(a0)
    sd      s3, 48(a0)
    sd      s4, 56(a0)
    sd      s5, 64(a0)
    sd      s6, 72(a0)
    sd      s7, 80(a0)
    sd      s8, 88(a0)
    sd      s9, 96(a0)
    sd      s10, 104(a0)
    sd      s11, 112(a0)
    sd      zero, 120(a0)
    li      a0, 0
    ret
