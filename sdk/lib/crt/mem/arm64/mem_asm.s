    .text
    .align 4

    .global memcpy
    .global memmove

memcpy:
memmove:
    mov     x3, x0
    cbz     x2, .Lcopy_done
    cmp     x0, x1
    b.eq    .Lcopy_done
    b.lo    .Lcopy_forward
    add     x4, x1, x2
    cmp     x0, x4
    b.hs    .Lcopy_forward

    add     x0, x0, x2
    add     x1, x1, x2

.Lcopy_backward_select:
    cmp     x2, #8
    b.lo    .Lcopy_backward_try32
    orr     x4, x0, x1
    tst     x4, #7
    b.eq    .Lcopy_backward64

.Lcopy_backward_try32:
    cmp     x2, #4
    b.lo    .Lcopy_backward_tail
    orr     x4, x0, x1
    tst     x4, #3
    b.eq    .Lcopy_backward32
    b       .Lcopy_backward_tail

.Lcopy_backward64:
    ldr     x5, [x1, #-8]!
    str     x5, [x0, #-8]!
    sub     x2, x2, #8
    b       .Lcopy_backward_select

.Lcopy_backward32:
    ldr     w5, [x1, #-4]!
    str     w5, [x0, #-4]!
    sub     x2, x2, #4
    b       .Lcopy_backward_select

.Lcopy_backward_tail:
    cbz     x2, .Lcopy_done
    ldrb    w4, [x1, #-1]!
    strb    w4, [x0, #-1]!
    sub     x2, x2, #1
    b       .Lcopy_backward_select

.Lcopy_forward:
.Lcopy_forward_select:
    cmp     x2, #8
    b.lo    .Lcopy_forward_try32
    orr     x4, x0, x1
    tst     x4, #7
    b.eq    .Lcopy_forward64

.Lcopy_forward_try32:
    cmp     x2, #4
    b.lo    .Lcopy_forward_tail
    orr     x4, x0, x1
    tst     x4, #3
    b.eq    .Lcopy_forward32
    b       .Lcopy_forward_tail

.Lcopy_forward64:
    ldr     x5, [x1], #8
    str     x5, [x0], #8
    sub     x2, x2, #8
    b       .Lcopy_forward_select

.Lcopy_forward32:
    ldr     w5, [x1], #4
    str     w5, [x0], #4
    sub     x2, x2, #4
    b       .Lcopy_forward_select

.Lcopy_forward_tail:
    cbz     x2, .Lcopy_done
    ldrb    w4, [x1], #1
    strb    w4, [x0], #1
    sub     x2, x2, #1
    b       .Lcopy_forward_select

.Lcopy_done:
    mov     x0, x3
    ret

    .global memset

memset:
    mov     x3, x0
    cbz     x2, .Lset_done

    and     x1, x1, #0xff
    orr     x1, x1, x1, lsl #8
    orr     x1, x1, x1, lsl #16
    orr     x1, x1, x1, lsl #32

.Lset_align:
    cmp     x2, #8
    b.lo    .Lset_try32
    ands    x4, x0, #7
    b.eq    .Lset_words
    strb    w1, [x0], #1
    sub     x2, x2, #1
    b       .Lset_align

.Lset_try32:
    cmp     x2, #4
    b.lo    .Lset_tail
    ands    x4, x0, #3
    b.eq    .Lset_dwords
    strb    w1, [x0], #1
    sub     x2, x2, #1
    b       .Lset_align

.Lset_words:
    cmp     x2, #8
    b.lo    .Lset_try32
    str     x1, [x0], #8
    sub     x2, x2, #8
    b       .Lset_words

.Lset_dwords:
    cmp     x2, #4
    b.lo    .Lset_tail
    str     w1, [x0], #4
    sub     x2, x2, #4
    b       .Lset_try32

.Lset_tail:
    cbz     x2, .Lset_done
    strb    w1, [x0], #1
    sub     x2, x2, #1
    b       .Lset_align

.Lset_done:
    mov     x0, x3
    ret
