    .text
    .align 4

    .global memcpy
    .global memmove

// Naturally-aligned accesses only (MMU-off safe): doubleword, word,
// halfword or byte blocks according to common alignment. Overlap is handled
// via direction and block-distance guards. No FP/SIMD state is touched.
memcpy:
memmove:
    mov     x15, x0
    cbz     x2, .Lcpy_ret
    cmp     x0, x1
    b.eq    .Lcpy_ret
    b.lo    .Lcpy_fwd
    add     x3, x1, x2
    cmp     x0, x3
    b.lo    .Lcpy_bwd                // dst inside [src, src+n): copy backward

.Lcpy_fwd:
    cmp     x0, x1
    b.lo    1f
    mov     x3, #-1                  // disjoint: no forward hazard
    b       2f
1:  sub     x3, x1, x0               // overlap distance
2:  eor     x4, x0, x1
    tst     x4, #7
    b.ne    .Lcpy_fwd_c4
    ands    x5, x0, #7
    b.eq    .Lcpy_fwd_a8
    mov     x6, #8
    sub     x5, x6, x5               // head bytes to reach 8-alignment
    cmp     x2, x5
    b.lo    .Lcpy_fwd_bytes
3:  ldrb    w7, [x1], #1
    strb    w7, [x0], #1
    sub     x2, x2, #1
    subs    x5, x5, #1
    b.ne    3b

.Lcpy_fwd_a8:
    cmp     x3, #64
    b.lo    .Lcpy_fwd_a8_small       // close overlap: 8B granules only
.Lcpy_fwd_a8_blocks:
    cmp     x2, #64
    b.lo    .Lcpy_fwd_a8_small
    ldp     x4, x5, [x1]
    ldp     x6, x7, [x1, #16]
    ldp     x8, x9, [x1, #32]
    ldp     x10, x11, [x1, #48]
    stp     x4, x5, [x0]
    stp     x6, x7, [x0, #16]
    stp     x8, x9, [x0, #32]
    stp     x10, x11, [x0, #48]
    add     x1, x1, #64
    add     x0, x0, #64
    sub     x2, x2, #64
    b       .Lcpy_fwd_a8_blocks
.Lcpy_fwd_a8_small:
    cmp     x2, #8
    b.lo    .Lcpy_fwd_bytes
    ldr     x4, [x1], #8
    str     x4, [x0], #8
    sub     x2, x2, #8
    b       .Lcpy_fwd_a8_small

.Lcpy_fwd_c4:
    tst     x4, #3
    b.ne    .Lcpy_fwd_c2
    ands    x5, x0, #3
    b.eq    .Lcpy_fwd_a4
    mov     x6, #4
    sub     x5, x6, x5
    cmp     x2, x5
    b.lo    .Lcpy_fwd_bytes
1:  ldrb    w7, [x1], #1
    strb    w7, [x0], #1
    sub     x2, x2, #1
    subs    x5, x5, #1
    b.ne    1b
.Lcpy_fwd_a4:
    // Amortize loop overhead without requiring 8-byte alignment.
    cmp     x3, #32
    b.lo    .Lcpy_fwd_a4_short
.Lcpy_fwd_a4_blocks:
    cmp     x2, #32
    b.lo    .Lcpy_fwd_a4_short
    ldp     w4, w5, [x1]
    ldp     w6, w7, [x1, #8]
    ldp     w8, w9, [x1, #16]
    ldp     w10, w11, [x1, #24]
    stp     w4, w5, [x0]
    stp     w6, w7, [x0, #8]
    stp     w8, w9, [x0, #16]
    stp     w10, w11, [x0, #24]
    add     x1, x1, #32
    add     x0, x0, #32
    sub     x2, x2, #32
    b       .Lcpy_fwd_a4_blocks
.Lcpy_fwd_a4_short:
    cmp     x3, #8
    b.lo    .Lcpy_fwd_a4_single
.Lcpy_fwd_a4_pairs:
    cmp     x2, #8
    b.lo    .Lcpy_fwd_a4_single
    ldp     w4, w5, [x1], #8
    stp     w4, w5, [x0], #8
    sub     x2, x2, #8
    b       .Lcpy_fwd_a4_pairs
.Lcpy_fwd_a4_single:
    cmp     x2, #4
    b.lo    .Lcpy_fwd_bytes
    ldr     w4, [x1], #4
    str     w4, [x0], #4
    sub     x2, x2, #4
    b       .Lcpy_fwd_a4_single

.Lcpy_fwd_c2:
    // Differing word alignment can still share halfword alignment.
    tst     x4, #1
    b.ne    .Lcpy_fwd_bytes
    tst     x0, #1
    b.eq    .Lcpy_fwd_c2_aligned
    ldrb    w7, [x1], #1
    strb    w7, [x0], #1
    sub     x2, x2, #1
.Lcpy_fwd_c2_aligned:
    cmp     x3, #16
    b.lo    .Lcpy_fwd_c2_small
.Lcpy_fwd_c2_blocks:
    cmp     x2, #16
    b.lo    .Lcpy_fwd_c2_small
    ldrh    w4, [x1]
    ldrh    w5, [x1, #2]
    ldrh    w6, [x1, #4]
    ldrh    w7, [x1, #6]
    ldrh    w8, [x1, #8]
    ldrh    w9, [x1, #10]
    ldrh    w10, [x1, #12]
    ldrh    w11, [x1, #14]
    strh    w4, [x0]
    strh    w5, [x0, #2]
    strh    w6, [x0, #4]
    strh    w7, [x0, #6]
    strh    w8, [x0, #8]
    strh    w9, [x0, #10]
    strh    w10, [x0, #12]
    strh    w11, [x0, #14]
    add     x1, x1, #16
    add     x0, x0, #16
    sub     x2, x2, #16
    b       .Lcpy_fwd_c2_blocks
.Lcpy_fwd_c2_small:
    cmp     x2, #2
    b.lo    .Lcpy_fwd_bytes
    ldrh    w4, [x1], #2
    strh    w4, [x0], #2
    sub     x2, x2, #2
    b       .Lcpy_fwd_c2_small

.Lcpy_fwd_bytes:
    cbz     x2, .Lcpy_ret
    // Keep byte accesses for differing alignments and MMU-off callers.
    cmp     x2, #8
    b.lo    .Lcpy_fwd_bytes_single
    cmp     x3, #8
    b.lo    .Lcpy_fwd_bytes_single
.Lcpy_fwd_bytes_blocks:
    ldrb    w4, [x1]
    ldrb    w5, [x1, #1]
    ldrb    w6, [x1, #2]
    ldrb    w7, [x1, #3]
    ldrb    w8, [x1, #4]
    ldrb    w9, [x1, #5]
    ldrb    w10, [x1, #6]
    ldrb    w11, [x1, #7]
    strb    w4, [x0]
    strb    w5, [x0, #1]
    strb    w6, [x0, #2]
    strb    w7, [x0, #3]
    strb    w8, [x0, #4]
    strb    w9, [x0, #5]
    strb    w10, [x0, #6]
    strb    w11, [x0, #7]
    add     x1, x1, #8
    add     x0, x0, #8
    sub     x2, x2, #8
    cmp     x2, #8
    b.hs    .Lcpy_fwd_bytes_blocks
.Lcpy_fwd_bytes_tail:
    cbz     x2, .Lcpy_ret
.Lcpy_fwd_bytes_single:
    ldrb    w4, [x1], #1
    strb    w4, [x0], #1
    subs    x2, x2, #1
    b.ne    .Lcpy_fwd_bytes_single
    b       .Lcpy_ret

.Lcpy_bwd:
    sub     x3, x0, x1               // overlap distance (> 0 here)
    add     x1, x1, x2
    add     x0, x0, x2
    eor     x4, x0, x1
    tst     x4, #7
    b.ne    .Lcpy_bwd_c4
    ands    x5, x0, #7
    b.eq    .Lcpy_bwd_a8
    cmp     x2, x5
    b.lo    .Lcpy_bwd_bytes
1:  ldrb    w7, [x1, #-1]!
    strb    w7, [x0, #-1]!
    sub     x2, x2, #1
    subs    x5, x5, #1
    b.ne    1b

.Lcpy_bwd_a8:
    cmp     x3, #64
    b.lo    .Lcpy_bwd_a8_small
.Lcpy_bwd_a8_blocks:
    cmp     x2, #64
    b.lo    .Lcpy_bwd_a8_small
    ldp     x4, x5, [x1, #-16]
    ldp     x6, x7, [x1, #-32]
    ldp     x8, x9, [x1, #-48]
    ldp     x10, x11, [x1, #-64]
    stp     x4, x5, [x0, #-16]
    stp     x6, x7, [x0, #-32]
    stp     x8, x9, [x0, #-48]
    stp     x10, x11, [x0, #-64]
    sub     x1, x1, #64
    sub     x0, x0, #64
    sub     x2, x2, #64
    b       .Lcpy_bwd_a8_blocks
.Lcpy_bwd_a8_small:
    cmp     x2, #8
    b.lo    .Lcpy_bwd_bytes
    ldr     x4, [x1, #-8]!
    str     x4, [x0, #-8]!
    sub     x2, x2, #8
    b       .Lcpy_bwd_a8_small

.Lcpy_bwd_c4:
    tst     x4, #3
    b.ne    .Lcpy_bwd_c2
    ands    x5, x0, #3
    b.eq    .Lcpy_bwd_a4
    cmp     x2, x5
    b.lo    .Lcpy_bwd_bytes
1:  ldrb    w7, [x1, #-1]!
    strb    w7, [x0, #-1]!
    sub     x2, x2, #1
    subs    x5, x5, #1
    b.ne    1b
.Lcpy_bwd_a4:
    // Amortize loop overhead without requiring 8-byte alignment.
    cmp     x3, #32
    b.lo    .Lcpy_bwd_a4_short
.Lcpy_bwd_a4_blocks:
    cmp     x2, #32
    b.lo    .Lcpy_bwd_a4_short
    ldp     w4, w5, [x1, #-8]
    ldp     w6, w7, [x1, #-16]
    ldp     w8, w9, [x1, #-24]
    ldp     w10, w11, [x1, #-32]
    stp     w4, w5, [x0, #-8]
    stp     w6, w7, [x0, #-16]
    stp     w8, w9, [x0, #-24]
    stp     w10, w11, [x0, #-32]
    sub     x1, x1, #32
    sub     x0, x0, #32
    sub     x2, x2, #32
    b       .Lcpy_bwd_a4_blocks
.Lcpy_bwd_a4_short:
    cmp     x3, #8
    b.lo    .Lcpy_bwd_a4_single
.Lcpy_bwd_a4_pairs:
    cmp     x2, #8
    b.lo    .Lcpy_bwd_a4_single
    ldp     w4, w5, [x1, #-8]!
    stp     w4, w5, [x0, #-8]!
    sub     x2, x2, #8
    b       .Lcpy_bwd_a4_pairs
.Lcpy_bwd_a4_single:
    cmp     x2, #4
    b.lo    .Lcpy_bwd_bytes
    ldr     w4, [x1, #-4]!
    str     w4, [x0, #-4]!
    sub     x2, x2, #4
    b       .Lcpy_bwd_a4_single

.Lcpy_bwd_c2:
    // Differing word alignment can still share halfword alignment.
    tst     x4, #1
    b.ne    .Lcpy_bwd_bytes
    tst     x0, #1
    b.eq    .Lcpy_bwd_c2_aligned
    ldrb    w7, [x1, #-1]!
    strb    w7, [x0, #-1]!
    sub     x2, x2, #1
.Lcpy_bwd_c2_aligned:
    cmp     x3, #16
    b.lo    .Lcpy_bwd_c2_small
.Lcpy_bwd_c2_blocks:
    cmp     x2, #16
    b.lo    .Lcpy_bwd_c2_small
    ldrh    w4, [x1, #-2]
    ldrh    w5, [x1, #-4]
    ldrh    w6, [x1, #-6]
    ldrh    w7, [x1, #-8]
    ldrh    w8, [x1, #-10]
    ldrh    w9, [x1, #-12]
    ldrh    w10, [x1, #-14]
    ldrh    w11, [x1, #-16]
    strh    w4, [x0, #-2]
    strh    w5, [x0, #-4]
    strh    w6, [x0, #-6]
    strh    w7, [x0, #-8]
    strh    w8, [x0, #-10]
    strh    w9, [x0, #-12]
    strh    w10, [x0, #-14]
    strh    w11, [x0, #-16]
    sub     x1, x1, #16
    sub     x0, x0, #16
    sub     x2, x2, #16
    b       .Lcpy_bwd_c2_blocks
.Lcpy_bwd_c2_small:
    cmp     x2, #2
    b.lo    .Lcpy_bwd_bytes
    ldrh    w4, [x1, #-2]!
    strh    w4, [x0, #-2]!
    sub     x2, x2, #2
    b       .Lcpy_bwd_c2_small

.Lcpy_bwd_bytes:
    cbz     x2, .Lcpy_ret
    // Keep byte accesses for differing alignments and MMU-off callers.
    cmp     x2, #8
    b.lo    .Lcpy_bwd_bytes_single
    cmp     x3, #8
    b.lo    .Lcpy_bwd_bytes_single
.Lcpy_bwd_bytes_blocks:
    ldrb    w4, [x1, #-1]
    ldrb    w5, [x1, #-2]
    ldrb    w6, [x1, #-3]
    ldrb    w7, [x1, #-4]
    ldrb    w8, [x1, #-5]
    ldrb    w9, [x1, #-6]
    ldrb    w10, [x1, #-7]
    ldrb    w11, [x1, #-8]
    strb    w4, [x0, #-1]
    strb    w5, [x0, #-2]
    strb    w6, [x0, #-3]
    strb    w7, [x0, #-4]
    strb    w8, [x0, #-5]
    strb    w9, [x0, #-6]
    strb    w10, [x0, #-7]
    strb    w11, [x0, #-8]
    sub     x1, x1, #8
    sub     x0, x0, #8
    sub     x2, x2, #8
    cmp     x2, #8
    b.hs    .Lcpy_bwd_bytes_blocks
.Lcpy_bwd_bytes_tail:
    cbz     x2, .Lcpy_ret
.Lcpy_bwd_bytes_single:
    ldrb    w4, [x1, #-1]!
    strb    w4, [x0, #-1]!
    subs    x2, x2, #1
    b.ne    .Lcpy_bwd_bytes_single
    b       .Lcpy_ret

.Lcpy_ret:
    mov     x0, x15
    ret

    .global memset

// void *memset(void *dst x0, int c x1, size_t n x2)
memset:
    mov     x15, x0
    cbz     x2, .Lset_ret
    and     x1, x1, #0xff
    orr     x1, x1, x1, lsl #8
    orr     x1, x1, x1, lsl #16
    orr     x1, x1, x1, lsl #32
    mov     x3, x1

    // Large zero fills use the architected cache-block zero operation when
    // the processor permits it.  The block size is reported by DCZID_EL0, so
    // this remains correct across ARM64 implementations instead of assuming
    // the 64-byte block used by Cortex-A53.
    cbnz    x1, .Lset_head
    cmp     x2, #256
    b.lo    .Lset_head
    mrs     x4, dczid_el0
    tbnz    x4, #4, .Lset_head       // DZP: DC ZVA is prohibited
    and     x4, x4, #0xf
    mov     x5, #4
    lsl     x5, x5, x4               // bytes = 4 << BS
    sub     x6, x5, #1
    neg     x7, x0
    and     x7, x7, x6               // bytes to the next ZVA boundary
    cbz     x7, .Lset_zva_blocks
    cmp     x2, x7
    b.lo    .Lset_head

.Lset_zva_head:
    strb    wzr, [x0], #1
    sub     x2, x2, #1
    subs    x7, x7, #1
    b.ne    .Lset_zva_head

.Lset_zva_blocks:
    cmp     x2, x5
    b.lo    .Lset_words
    dc      zva, x0
    add     x0, x0, x5
    sub     x2, x2, x5
    b       .Lset_zva_blocks

.Lset_head:
    tst     x0, #7
    b.eq    .Lset_blocks
    strb    w1, [x0], #1
    subs    x2, x2, #1
    b.ne    .Lset_head
    b       .Lset_ret

.Lset_blocks:
    cmp     x2, #64
    b.lo    .Lset_words
    stp     x1, x3, [x0]
    stp     x1, x3, [x0, #16]
    stp     x1, x3, [x0, #32]
    stp     x1, x3, [x0, #48]
    add     x0, x0, #64
    sub     x2, x2, #64
    b       .Lset_blocks
.Lset_words:
    cmp     x2, #8
    b.lo    .Lset_tail
    str     x1, [x0], #8
    sub     x2, x2, #8
    b       .Lset_words
.Lset_tail:
    cbz     x2, .Lset_ret
    strb    w1, [x0], #1
    sub     x2, x2, #1
    b       .Lset_tail

.Lset_ret:
    mov     x0, x15
    ret
