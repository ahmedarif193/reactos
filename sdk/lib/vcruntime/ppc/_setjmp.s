/*
 * PROJECT:     LiberNT vcruntime library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC setjmp
 */

#include <kxppc.h>

/* int _setjmp(jmp_buf Buffer) */
LEAF_ENTRY(_setjmp)
    li      4, 0
    b       .Lsave_context
LEAF_END(_setjmp)

LEAF_ENTRY(__intrinsic_setjmp)
    li      4, 0
    b       .Lsave_context
LEAF_END(__intrinsic_setjmp)

/* The NT PPC extended entry takes one argument.  Its first two words are
 * the establisher frame and continuation used by RtlUnwind, not saved FPRs.
 * The caller's back chain names the establisher frame of the function that
 * called us; Type marks this buffer as the extended form. */
LEAF_ENTRY(_setjmpex)
    b       .Lsave_extended
LEAF_END(_setjmpex)

LEAF_ENTRY(__intrinsic_setjmpex)
.Lsave_extended:
    lwz     5, 0(1)
    stw     5, 0(3)
    mflr    5
    stw     5, 4(3)
    stw     1, 236(3)
    li      3, 0
    blr

.Lsave_context:
    stfd    14, 0(3)
    stfd    15, 8(3)
    stfd    16, 16(3)
    stfd    17, 24(3)
    stfd    18, 32(3)
    stfd    19, 40(3)
    stfd    20, 48(3)
    stfd    21, 56(3)
    stfd    22, 64(3)
    stfd    23, 72(3)
    stfd    24, 80(3)
    stfd    25, 88(3)
    stfd    26, 96(3)
    stfd    27, 104(3)
    stfd    28, 112(3)
    stfd    29, 120(3)
    stfd    30, 128(3)
    stfd    31, 136(3)
    stw     1, 144(3)
    stw     2, 148(3)
    stw     13, 152(3)
    stw     14, 156(3)
    stw     15, 160(3)
    stw     16, 164(3)
    stw     17, 168(3)
    stw     18, 172(3)
    stw     19, 176(3)
    stw     20, 180(3)
    stw     21, 184(3)
    stw     22, 188(3)
    stw     23, 192(3)
    stw     24, 196(3)
    stw     25, 200(3)
    stw     26, 204(3)
    stw     27, 208(3)
    stw     28, 212(3)
    stw     29, 216(3)
    stw     30, 220(3)
    stw     31, 224(3)
    mfcr    5
    stw     5, 228(3)
    mflr    5
    stw     5, 232(3)
    stw     4, 236(3)
    li      3, 0
    blr
LEAF_END(__intrinsic_setjmpex)
