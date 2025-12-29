.text
.p2align 2
.global strlen
.global strnlen

.equ __strnlen_forceAlignThreshold, 448

/*
 * Optimized strlen/strnlen for ARM64 (GAS syntax), derived from the
 * Microsoft implementation. Kept structure and vector scanning logic;
 * only the macro layer was removed for GAS compatibility.
 */

strlen:
        /* check for empty string to avoid huge perf degradation in this case. */
        ldrb    w2, [x0], #0
        cbz     w2, EmptyStr

        mov     x5, x0                                      /* keep original x0 value for the final 'sub' */
        /* calculate number of bytes until first 16-byte alignment point */

        ands    x1, x5, #15                                 /* x1 = (addr mod 16) */
        beq     StrlenMainLoop                              /* no need to force alignment if already aligned */

        /* we need to align, check whether we are within 16 bytes of the end of the page */

        ands    x2, x5, #4095
        cmp     x2, #4080
        bgt     AlignByteByByte                             /* too close to end of page, must align byte-by-byte */

        /* safe to do one unaligned 16-byte vector load to force alignment */

        ld1     {v0.16b}, [x5]                              /* don't post-increment x5 */
        uminv   b1, v0.16b
        fmov    w2, s1                                      /* fmov is sometimes 1 cycle faster than 'umov w2, v1.b[0]' */
        cbz     w2, FindNullInVector                        /* jump when string <= 15 bytes long & not near end of page */
        add     x5, x5, #16                                 /* move x5 forward only to aligned address */
        and     x5, x5, 0xFFFFFFFFFFFFFFF0                  /* first iter of StrlenMainLoop will retest some bytes we already tested */

StrlenMainLoop:                                             /* test 16 bytes at a time until we find it */
        ld1     {v0.16b}, [x5], #16
        uminv   b1, v0.16b                                  /* use unsigned min to look for a zero byte; too bad it doesn't set CC */
        fmov    w2, s1                                      /* need to move min byte into gpr to test it */
        cbnz    w2, StrlenMainLoop                          /* fall through when any one of the bytes in v0 is zero */

        sub     x5, x5, #16                                 /* undo the last #16 post-increment of x5 */

FindNullInVector:                                           /* this label is also the target of a jump from strnlen */
        ldr     q1, ReverseBytePos                          /* load the position indicator mask */

        cmeq    v0.16b, v0.16b, #0                          /* +---- */
        and     v0.16b, v0.16b, v1.16b                      /* | */
        umaxv   b0, v0.16b                                  /* | see big comment below */
        fmov    w2, s0                                      /* | */
        eor     w2, w2, #15                                 /* +---- */

        add     x5, x5, x2                                  /* which is the offset we need to add to x5 to point at the null byte */
        sub     x0, x5, x0                                  /* subtract ptr to null char from ptr to first char to get the strlen */
        ret

ByteByByteFoundIt:                                          /* this label is also the target of a jump from strnlen */
        sub     x5, x5, #1                                  /* Undo the final post-increment that happened on the load of the null char. */
        sub     x0, x5, x0                                  /* With x5 pointing at the null char, x5-x0 is the strlen */
        ret

AlignByteByByte:
        sub     x1, x1, #16                                 /* x1 = (addr mod 16) - 16 */
        neg     x1, x1                                      /* x1 = 16 - (addr mod 16) = count for byte-by-byte loop */
ByteByByteLoop:                                             /* test one byte at a time until we are 16-byte aligned */
        ldrb    w2, [x5], #1
        cbz     w2, ByteByByteFoundIt                       /* branch if byte-at-a-time testing finds the null */
        subs    x1, x1, #1
        bgt     ByteByByteLoop                              /* fall through when not found and 16-byte aligned */
        b       StrlenMainLoop

EmptyStr:
        mov     x0, 0
        ret

        /*
         * The challenge is to find a way to efficiently determine which of the 16 bytes we loaded is the end of the string.
         * The trick is to load a position indicator mask and generate the position of the rightmost null from that.
         * Little-endian order means when we load the mask below v1.16b[0] has 0x0F, and v0.16b[0] is the byte of the string
         * that comes first of the 16 we loaded. We do a cmeq, mapping all the characters we loaded to either 0xFF (for nulls)
         * or 0x00 for non-nulls. Then we and with the mask below. SIMD lanes corresponding to a non-null character will be 0,
         * and SIMD lanes corresponding to null bytes will have a byte from the mask. We take the max across the bytes of the
         * vector to find the highest position that corresponds to a null character. The numbering order means we find the
         * rightmost null in the vector, which is the null that occurred first in memory due to little endian loading.
         * Exclusive oring the position indicator byte with 15 inverts the order, which gives us the offset of the null
         * counting from the first character we loaded into the v0 SIMD reg.
         */

        /* ---------------------------------------------------------------------- */
        /* strnlen entry point                                                   */

strnlen:
        cbz     x1, AnyRet

        mov     x2, x1

        mov     x5, x0                                      /* keep original x0 value for final subtraction */
        /* calculate number of bytes until first 16-byte alignment point */

        cmp     x2, #__strnlen_forceAlignThreshold
        bhs     StrnlenLongLoop                             /* we will align only if string is reasonably long */

        ands    x1, x5, #15                                 /* x1 = (addr mod 16) */
        beq     NoNeedToAlign                               /* no need to align vector loads to 16-byte boundary */

        add     x3, x5, #16
        and     x3, x3, 0xFFFFFFFFFFFFFFF0                  /* x3 = address of next higher 16-byte boundary */
        sub     x3, x3, x5                                  /* x3 = #bytes to next 16-byte boundary */
        cmp     x3, x2
        bge     NoNeedToAlign                               /* must not cross buffer boundary to align */

        /* we need to align, check whether we are within 16 bytes of the end of the page */
        ands    x4, x5, #4095
        cmp     x4, #4080
        bgt     AlignStrnByteByByte                         /* too close to end of page, must align byte-by-byte */

        /* safe to do one unaligned 16-byte vector load to force alignment */
        ld1     {v0.16b}, [x5]                              /* don't post-increment x5 */
        uminv   b1, v0.16b
        fmov    w2, s1                                      /* fmov is sometimes 1 cycle faster than 'umov w2, v1.b[0]' */
        cbz     w2, FindNullInVector                        /* jump when string <= 15 bytes long & not near end of page */

        add     x5, x5, #16                                 /* move x5 forward only to aligned address */
        sub     x2, x2, #16                                 /* reduce remaining length */
        b       StrnlenMainLoop

NoNeedToAlign:
        mov     x4, x5
        ands    x4, x4, #16
        cbz     x4, StrnlenMainLoop                         /* branch if lower 4 bits of address were 0 */

AlignStrnByteByByte:
StrnByteByByteLoop:                                         /* test one byte at a time until we are 16-byte aligned */
        ldrb    w2, [x5], #1
        cbz     w2, ByteByByteFoundIt
        subs    x3, x3, #1                                  /* counter --# bytes left to alignment point. Could be zero at entry */
        subs    x2, x2, #1                                  /* decrement remaining buffer length */
        beq     AnyRet                                      /* exhausted the buffer before finding a null */
        bgt     StrnByteByByteLoop                          /* fall through when not found and 16-byte aligned */

        /* AlignStrnByteByByte: fall through directly to StrnlenMainLoop -- you must set x3 and x2 first */

StrnlenMainLoop:                                            /* test 16 bytes at a time until we find it */
        ld1     {v0.16b}, [x5], #16
        uminv   b1, v0.16b                                  /* use unsigned min to look for a zero byte; too bad it doesn't set CC */
        fmov    w2, s1                                      /* need to move min byte into gpr to test it */
        cbnz    w2, StrnlenMainLoop                         /* fall through when any one of the bytes in v0 is zero */

        sub     x5, x5, #16                                 /* undo the last #16 post-increment of x5 */

FindStrnNullInVector:
        ldr     q1, ReverseBytePos                          /* load the position indicator mask */

        cmeq    v0.16b, v0.16b, #0                          /* +---- */
        and     v0.16b, v0.16b, v1.16b                      /* | */
        umaxv   b0, v0.16b                                  /* | see big comment below */
        fmov    w2, s0                                      /* | */
        eor     w2, w2, #15                                 /* +---- */

        add     x5, x5, x2                                  /* offset to null byte */
        bics    x2, x5, x5                                  /* set zero flag */
        b.ne    AnyRet                                      /* zero flag clear => null not in buffer */
        sub     x0, x5, x0                                  /* return string length */
        ret

StrnlenLongLoop:                                            /* string and buffer lengths >= 512 bytes */
        and     x1, x5, #4095
        cbnz    x1, StrnlenLongLoopStart
StrnlenLongLoopBody:
        ldr     w2, [x5]
        add     x5, x5, #4                                  /* this has to match how many bytes were loaded above */
        cbz     w2, StrnByteByByteLoop
StrnlenLongLoopStart:
        subs    x2, x2, #4
        bhi     StrnlenLongLoopBody

        cbz     x2, AnyRet
        and     x5, x5, 0xFFFFFFFFFFFFFFF0                  /* now aligned */
        b       StrnlenMainLoop                             /* zeros in last up-to-3 bytes will be caught */

AnyRet:
        mov     x0, x4
        ret

.balign 16
ReverseBytePos:
        .byte   15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0
