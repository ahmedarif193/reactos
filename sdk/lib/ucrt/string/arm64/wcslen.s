.text
.p2align 2
.global wcslen
.global wcsnlen

/*
 * Minimal ARM64 implementations of wcslen/wcsnlen in GAS syntax.
 * Kept simple to guarantee assembler compatibility in this tree.
 */

wcslen:
        mov     x1, x0
1:
        ldrh    w2, [x1], #2
        cbnz    w2, 1b
        sub     x1, x1, #2
        sub     x0, x1, x0
        lsr     x0, x0, #1
        ret

wcsnlen:
        mov     x2, x1          /* remaining count */
        cbz     x2, .wcsnlen_ret0
        mov     x3, x0          /* walker */
2:
        ldrh    w4, [x3], #2
        cbz     w4, .wcsnlen_found
        subs    x2, x2, #1
        b.gt    2b
        /* exhausted limit */
        mov     x0, x1
        ret

.wcsnlen_found:
        sub     x3, x3, #2
        sub     x0, x3, x0
        lsr     x0, x0, #1
        ret

.wcsnlen_ret0:
        mov     x0, #0
        ret
