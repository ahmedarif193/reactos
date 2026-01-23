#include <asm.h>
#include <ksarm64.h>

    .text
    .align 4

    .global KiTrapReturn

    .seh_proc KiTrapReturn
KiTrapReturn:
    /*
     * SEH: This is a special function that restores context from trap frame.
     * It doesn't have a normal prologue/epilogue - it consumes parameters
     * (x0 = TrapFrame, x1 = ExceptionFrame) and returns via eret.
     */
    .seh_endprologue
    mov     x20, x0                  /* Trap frame */
    mov     x21, x1                  /* Exception frame */

    /*
     * Prevent IRQ/FIQ from clobbering ELR_EL1/SPSR_EL1 while we are preparing
     * the exception return state. When KiTrapReturn is invoked directly (e.g.
     * from thread startup), interrupts may still be enabled at EL1.
     */
    msr     daifset, #0x3

    /* Preserve fallback frame pointer/link register */
    ldr     x24, [x20, #TfFp]
    ldr     x25, [x20, #TfLr]

    /* Load critical exception state FIRST before any other loads */
    ldr     w22, [x20, #TfSpsr]
    ldr     x23, [x20, #TfPc]
    ldr     x26, [x20, #TfSp]

    /* Program exception state to system registers */
    msr     spsr_el1, x22
    msr     elr_el1,  x23

    /* Memory barrier to ensure exception state is committed */
    dsb     sy
    isb

    /*
     * DEBUG: One-shot marker for the first return to EL0.
     * Print before restoring registers so we don't clobber user state.
     */
#ifdef KDBG
    ands    w27, w22, #0xF
    b.ne    9f
    adrp    x9, KiArm64TrapReturnOnce
    ldr     w10, [x9, #:lo12:KiArm64TrapReturnOnce]
    cbnz    w10, 9f
    mov     w10, #1
    str     w10, [x9, #:lo12:KiArm64TrapReturnOnce]
    ldr     x9, =0xFFFF800009000000
    mov     w10, #'T'
    strb    w10, [x9]
    mov     w10, #'R'
    strb    w10, [x9]
    mov     w10, #'U'
    strb    w10, [x9]
    mov     w10, #'\n'
    strb    w10, [x9]
9:
#endif /* KDBG */

    /* Restore volatile registers from the trap frame */
    ldr     x18, [x20, #TfX18]
    ldp     x16, x17, [x20, #TfX16]
    ldp     x14, x15, [x20, #TfX14]
    ldp     x12, x13, [x20, #TfX12]
    ldp     x10, x11, [x20, #TfX10]
    ldp     x8,  x9,  [x20, #TfX8]
    ldp     x6,  x7,  [x20, #TfX6]
    ldp     x4,  x5,  [x20, #TfX4]
    ldp     x2,  x3,  [x20, #TfX2]
    ldp     x0,  x1,  [x20, #TfX0]

    /* Select appropriate stack pointer */
    ands    w27, w22, #0xF
    cbnz    w27, 1f
    msr     sp_el0, x26              /* Returning to EL0 */
    /* Ensure x18/TPIDR_EL0 track the current TEB for user-mode ABI */
    adrp    x16, KeArm64CurrentThread
    ldr     x16, [x16, #:lo12:KeArm64CurrentThread]
    cbz     x16, 2f
    ldr     x18, [x16, #ThTeb]
    msr     tpidr_el0, x18
2:
    ldr     x16, [x20, #TfX16]
    b       3f
1:
    /*
     * Returning to EL1: Restore SP from trap frame if it differs.
     *
     * SAFETY FIX: The old code assumed SP_EL1 was already correct from the
     * vector stub, but KiTrapReturn can be called from non-vector paths
     * (e.g., KiThreadStartup). Those callers may expect TfSp to be honored.
     *
     * If TfSp (x26) is non-zero and differs from current SP, use TfSp.
     * This maintains correctness for both vector-based returns (where SP
     * is already correct) and direct calls (where SP needs restoration).
     */
    cbz     x26, 3f              /* If TfSp is 0, don't touch SP */
    mov     x27, sp
    cmp     x26, x27
    b.eq    3f                   /* If TfSp == current SP, nothing to do */
    mov     sp, x26              /* Restore SP from trap frame */
3:
    /*
     * DEBUG: One-shot marker just before restoring the exception frame and ERET.
     * Reload the volatile register bank afterwards to preserve user state.
     */
#ifdef KDBG
    ands    w27, w22, #0xF
    b.ne    8f
    adrp    x9, KiArm64TrapReturnOnce2
    ldr     w10, [x9, #:lo12:KiArm64TrapReturnOnce2]
    cbnz    w10, 8f
    mov     w10, #1
    str     w10, [x9, #:lo12:KiArm64TrapReturnOnce2]
    ldr     x9, =0xFFFF800009000000
    mov     w10, #'T'
    strb    w10, [x9]
    mov     w10, #'R'
    strb    w10, [x9]
    mov     w10, #'2'
    strb    w10, [x9]
    mov     w10, #'\n'
    strb    w10, [x9]

    /* Re-load volatile registers from trap frame (printing clobbered them). */
    ldr     x18, [x20, #TfX18]
    ldp     x16, x17, [x20, #TfX16]
    ldp     x14, x15, [x20, #TfX14]
    ldp     x12, x13, [x20, #TfX12]
    ldp     x10, x11, [x20, #TfX10]
    ldp     x8,  x9,  [x20, #TfX8]
    ldp     x6,  x7,  [x20, #TfX6]
    ldp     x4,  x5,  [x20, #TfX4]
    ldp     x2,  x3,  [x20, #TfX2]
    ldp     x0,  x1,  [x20, #TfX0]
8:
#endif /* KDBG */

    /* Restore non-volatiles when an exception frame is present */
    cbz     x21, 5f
    /*
     * ARM64 BUG FIX: Use x27 as scratch instead of x11.
     * x11 was already restored from trap frame at line 42.
     * If we use x11 here, we would clobber its correct value.
     *
     * x27 is a callee-saved register that we're about to restore from the
     * exception frame anyway, so it's safe to use it as a temporary pointer.
     * We must load x27's real value AFTER we're done using it as a pointer.
     *
     * Order matters: load x27 LAST among the registers we access via it.
     */
    mov     x27, x21             /* Use x27 as exception frame pointer */
    ldr     x19, [x27, #ExX19]
    ldr     x20, [x27, #ExX20]
    ldr     x21, [x27, #ExX21]
    ldr     x22, [x27, #ExX22]
    ldr     x23, [x27, #ExX23]
    ldr     x24, [x27, #ExX24]
    ldr     x25, [x27, #ExX25]
    ldr     x26, [x27, #ExX26]
    ldr     x28, [x27, #ExX28]
    ldr     x29, [x27, #ExFp]
    ldr     x30, [x27, #ExLr]
    ldr     x27, [x27, #ExX27]   /* Load x27 LAST (clobbers pointer) */
    b       4f
5:
    mov     x29, x24
    mov     x30, x25
4:
    /* Full memory barrier before exception return */
    dsb     sy
    isb
    eret
    .seh_endproc

#ifdef KDBG
    .bss
    .align 4
KiArm64TrapReturnOnce:
    .word 0
KiArm64TrapReturnOnce2:
    .word 0
    .text
#endif /* KDBG */


    .align 4
    .global KiArm64BugCheckEntry
    .seh_proc KiArm64BugCheckEntry
KiArm64BugCheckEntry:
    /* SEH: Simple tail-call trampoline, no frame */
    .seh_endprologue
    ldr     x16, =KiArm64BugCheckSynchronousException
    br      x16
    .seh_endproc

    .ltorg
