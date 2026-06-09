    .text
    .align 4

    .global memcpy
    .global memmove

// Naturally-aligned accesses only (MMU-off safe): 64B ldp/stp blocks when co-aligned to 8, word pairs when co-aligned to 4, bytes otherwise; overlap handled via direction + block-distance guards.
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
    b.ne    .Lcpy_fwd_bytes
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

.Lcpy_fwd_bytes:
    cbz     x2, .Lcpy_ret
    ldrb    w4, [x1], #1
    strb    w4, [x0], #1
    sub     x2, x2, #1
    b       .Lcpy_fwd_bytes

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
    b.ne    .Lcpy_bwd_bytes
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

.Lcpy_bwd_bytes:
    cbz     x2, .Lcpy_ret
    ldrb    w4, [x1, #-1]!
    strb    w4, [x0, #-1]!
    sub     x2, x2, #1
    b       .Lcpy_bwd_bytes

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
