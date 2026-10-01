/*
 * PROJECT:     LiberNT vcruntime library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC longjmp helper
 */

#include <kxppc.h>

/* void __longjmp_noframe(const _JUMP_BUFFER *Buffer, int Value) */
LEAF_ENTRY(__longjmp_noframe)
    mr      5, 3
    mr.     3, 4
    bne     0, .Lrestore_context
    li      3, 1
.Lrestore_context:
    lwz     6, 228(5)
    mtcrf   0xff, 6
    lwz     6, 232(5)
    mtlr    6
    lfd     14, 0(5)
    lfd     15, 8(5)
    lfd     16, 16(5)
    lfd     17, 24(5)
    lfd     18, 32(5)
    lfd     19, 40(5)
    lfd     20, 48(5)
    lfd     21, 56(5)
    lfd     22, 64(5)
    lfd     23, 72(5)
    lfd     24, 80(5)
    lfd     25, 88(5)
    lfd     26, 96(5)
    lfd     27, 104(5)
    lfd     28, 112(5)
    lfd     29, 120(5)
    lfd     30, 128(5)
    lfd     31, 136(5)
    lwz     1, 144(5)
    lwz     2, 148(5)
    lwz     13, 152(5)
    lwz     14, 156(5)
    lwz     15, 160(5)
    lwz     16, 164(5)
    lwz     17, 168(5)
    lwz     18, 172(5)
    lwz     19, 176(5)
    lwz     20, 180(5)
    lwz     21, 184(5)
    lwz     22, 188(5)
    lwz     23, 192(5)
    lwz     24, 196(5)
    lwz     25, 200(5)
    lwz     26, 204(5)
    lwz     27, 208(5)
    lwz     28, 212(5)
    lwz     29, 216(5)
    lwz     30, 220(5)
    lwz     31, 224(5)
    blr
LEAF_END(__longjmp_noframe)
