/* SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF <arif193@gmail.com>
 * ARM64 user-mode copy entry. Paired SIMD loads improve Normal-NC/WC reads
 * on Cortex-A53. Keep this out of the shared MMU-off/kernel CRT routine.
 * Small and overlapping copies use that routine. Userspace Normal memory
 * permits unaligned SIMD loads; align stores without requiring matching
 * source alignment. No implementation-specific instructions are used.
 */
    .text
    .align 4
    .global RtlpUserMemmove
RtlpUserMemmove:
    cmp     x2, #256
    b.lo    memmove
    // Subtract in address order to avoid overflowing src + count.
    cmp     x0, x1
    b.lo    .Luser_dst_below
    sub     x3, x0, x1
    b       .Luser_check_overlap
.Luser_dst_below:
    sub     x3, x1, x0
.Luser_check_overlap:
    cmp     x3, x2
    b.lo    memmove

    // Leaf routine: x9 and v0-v3 are volatile in the Windows ARM64 ABI.
    mov     x9, x0
    ands    x3, x0, #15
    b.eq    .Luser_blocks
    neg     x3, x3
    add     x3, x3, #16
.Luser_head:
    ldrb    w4, [x1], #1
    strb    w4, [x0], #1
    sub     x2, x2, #1
    subs    x3, x3, #1
    b.ne    .Luser_head

.Luser_blocks:
    ldp     q0, q1, [x1]
    ldp     q2, q3, [x1, #32]
    stp     q0, q1, [x0]
    stp     q2, q3, [x0, #32]
    add     x1, x1, #64
    add     x0, x0, #64
    sub     x2, x2, #64
    cmp     x2, #64
    b.hs    .Luser_blocks

.Luser_words:
    cmp     x2, #8
    b.lo    .Luser_tail
    ldr     x4, [x1], #8
    str     x4, [x0], #8
    sub     x2, x2, #8
    b       .Luser_words
.Luser_tail:
    cbz     x2, .Luser_return
.Luser_bytes:
    ldrb    w4, [x1], #1
    strb    w4, [x0], #1
    subs    x2, x2, #1
    b.ne    .Luser_bytes
.Luser_return:
    mov     x0, x9
    ret
