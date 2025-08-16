/*
 * ARM64 setjmp/longjmp implementation
 */

.text

/* __wine_setjmpex - Save context for exception handling */
.global __wine_setjmpex
__wine_setjmpex:
    /* Save registers to jmp_buf pointed by x0 */
    stp x19, x20, [x0, #0]
    stp x21, x22, [x0, #16]
    stp x23, x24, [x0, #32]
    stp x25, x26, [x0, #48]
    stp x27, x28, [x0, #64]
    stp x29, x30, [x0, #80]  /* FP and LR */
    mov x1, sp
    str x1, [x0, #96]        /* SP */
    
    /* Store floating point registers */
    stp d8, d9, [x0, #104]
    stp d10, d11, [x0, #120]
    stp d12, d13, [x0, #136]
    stp d14, d15, [x0, #152]
    
    /* Return 0 for direct call */
    mov x0, #0
    ret

/* longjmp - Restore context */
.global longjmp
longjmp:
    /* Restore registers from jmp_buf pointed by x0 */
    ldp x19, x20, [x0, #0]
    ldp x21, x22, [x0, #16]
    ldp x23, x24, [x0, #32]
    ldp x25, x26, [x0, #48]
    ldp x27, x28, [x0, #64]
    ldp x29, x30, [x0, #80]  /* FP and LR */
    ldr x2, [x0, #96]        /* SP */
    mov sp, x2
    
    /* Restore floating point registers */
    ldp d8, d9, [x0, #104]
    ldp d10, d11, [x0, #120]
    ldp d12, d13, [x0, #136]
    ldp d14, d15, [x0, #152]
    
    /* Return value from x1, or 1 if 0 */
    cmp x1, #0
    cset x0, ne
    csinc x0, x1, xzr, ne
    ret

/* _setjmp - Standard setjmp */
.global _setjmp
_setjmp:
    b __wine_setjmpex

/* setjmp - Standard setjmp */
.global setjmp
setjmp:
    b __wine_setjmpex