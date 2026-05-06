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
     *
     * CRITICAL: Must disable ALL interrupts (DAIF) and synchronize with ISB
     * before modifying SPSR_EL1. Without ISB, an IRQ could fire AFTER daifset
     * but BEFORE the CPU recognizes the mask. When the IRQ is taken, the
     * hardware AUTOMATICALLY overwrites SPSR_EL1 with the current PSTATE
     * (EL1h=5), destroying the mode=0 value we're about to set for EL0 return.
     */
    msr     daifset, #0xF                /* Disable D(8), A(4), I(2), F(1) */
    isb                                  /* Ensure mask takes effect NOW */

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
     * CRITICAL: Save trap frame pointer (x20) to stack so we can access it
     * after exception frame restoration to re-load SPSR/ELR just before ERET.
     *
     * Reserve 16 bytes on stack (16-byte aligned):
     *   [sp+0]  = trap frame pointer (x20)
     *   [sp+8]  = padding
     */
    str     x20, [sp, #-16]!             /* Save trap frame pointer */

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

    /* Select appropriate return path based on SPSR mode bits.
     * x22 was loaded with TfSpsr above. SPSR.M[3:0] == 0 means EL0. */
    ands    w27, w22, #0xF
    cbnz    w27, 1f

    cbnz    x23, .Luser_pc_ready

    /*
     * Keep the system alive when the debug-print BRK frame got clobbered.
     * BRK #0xF003 resume address can safely fall back to LR.
     */
    ldr     w16, [x20, #TfEsr]
    lsr     w17, w16, #26
    cmp     w17, #0x3C
    b.ne    .Luser_pc_ready
    and     w17, w16, #0xFFFF
    mov     w18, #0xF003
    cmp     w17, w18
    b.ne    .Luser_pc_ready
    ldr     x16, [x20, #TfLr]
    cbz     x16, .Luser_pc_ready
    mov     x23, x16
    str     x23, [x20, #TfPc]
.Luser_pc_ready:

    msr     sp_el0, x26              /* Returning to EL0 */

    /* User I-cache maintenance is handled on fault/map paths. */

    /* Ensure x18/TPIDR_EL0 track the current TEB for user-mode ABI */
    mrs     x16, tpidr_el1
    cbz     x16, .Lno_teb
    ldr     x16, [x16, #PcCurrentThread]
    cbz     x16, .Lno_teb

    ldr     x18, [x16, #ThTeb]
    msr     tpidr_el0, x18
    b       .Lteb_done
.Lno_teb:
    ldr     x16, [x20, #TfX16]
    b       3f
.Lteb_done:
    ldr     x16, [x20, #TfX16]
    b       3f
1:
    /* EL1 return path - falls through to common path */
3:
    /*
     * ARM64 CRITICAL FIX: Set SPSR_EL1/ELR_EL1 BEFORE exception frame restore.
     *
     * Previous code set SPSR/ELR AFTER exception frame restore, using x29/x27/x28
     * as scratch. This clobbered the exception-frame-restored values of FP (x29),
     * x27, and x28 for EL1 returns (data abort page faults). The corrupted FP
     * caused stack frame corruption, leading to SPIN_LOCK_NOT_OWNED bugchecks.
     *
     * Fix: Pop trap frame pointer and set SPSR/ELR system registers FIRST,
     * then restore exception frame (which correctly sets ALL GPRs including
     * x27, x28, x29). ERET immediately after - no GPR clobber.
     *
     * Safety: Interrupts are disabled (daifset #0xF from entry), and the
     * exception frame is on the kernel stack (always mapped), so no exceptions
     * can occur between MSR and ERET to corrupt SPSR_EL1/ELR_EL1.
     */
    dsb     sy
    isb

    /* Pop trap frame pointer from stack (saved at entry) */
    ldr     x29, [sp], #16

    /* Load SPSR and ELR from trap frame, set system registers */
    ldr     w27, [x29, #TfSpsr]
    ldr     x28, [x29, #TfPc]
    msr     spsr_el1, x27
    msr     elr_el1, x28
    dsb     sy
    isb

    /* Restore non-volatiles from exception frame (if present).
     * This restores x19-x30, x27 to their correct pre-exception values.
     * SPSR_EL1/ELR_EL1 are already set above, so clobbering GPRs is fine. */
    cbz     x21, 5f
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
    /* No exception frame - use fallback FP/LR */
    mov     x29, x24
    mov     x30, x25
4:
    /* All GPRs now have correct values. SPSR/ELR already set. ERET. */

    dsb     sy
    isb
    eret

    .seh_endproc


    .align 4
    .global KiArm64BugCheckEntry
    .seh_proc KiArm64BugCheckEntry
KiArm64BugCheckEntry:
    /* SEH: Simple tail-call trampoline, no frame */
    .seh_endprologue
    ldr     x16, =KiArm64BugCheckSynchronousException
    br      x16
    .seh_endproc

    .align 4
    .global KiArm64BugCheckOnPanicStack
    .seh_proc KiArm64BugCheckOnPanicStack
KiArm64BugCheckOnPanicStack:
    /* x0 = panic stack top, x1 = code, x2-x5 = bugcheck parameters */
    .seh_endprologue
    mov     sp, x0
    mov     x0, x1
    mov     x1, x2
    mov     x2, x3
    mov     x3, x4
    mov     x4, x5
    ldr     x16, =KeBugCheckEx
    blr     x16
1:
    wfe
    b       1b
    .seh_endproc

    .data
    .ltorg
