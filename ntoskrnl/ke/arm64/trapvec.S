#include <asm.h>
#include <ksarm64.h>

    .text
    .align  4

    .extern KiArm64HandleSynchronousException
    .extern KiArm64BugCheckEntry
    .extern KiTrapReturn
    .extern KiArm64VectorLogOnly

.equ ARM64_EARLY_STATE_VECTORID,      0x0
.equ ARM64_EARLY_STATE_ESR,           0x8
.equ ARM64_EARLY_STATE_FAR,           0x10
.equ ARM64_EARLY_STATE_ELR,           0x18
.equ ARM64_EARLY_STATE_SPSR,          0x20
.equ ARM64_EARLY_STATE_REGS,          0x28
.equ ARM64_EARLY_STATE_REG_SP,        0x120   /* X[0..30] = 0x28 + (31*8) = 0x120 */
.equ ARM64_EARLY_STATE_REG_PC,        0x128   /* Sp + 8 = 0x128 */
.equ ARM64_EARLY_STATE_REG_PSTATE,    0x130   /* Pc + 8 = 0x130 */
.equ ARM64_EARLY_STATE_TRAP_PTR,      0x138
.equ ARM64_EARLY_STATE_EXC_PTR,       0x140
.equ ARM64_EARLY_STATE_TRAP_FRAME,    0x148
.equ ARM64_EARLY_STATE_NEON,          0x380
.equ ARM64_EARLY_STATE_FPCR,          0x580
.equ ARM64_EARLY_STATE_FPSR,          0x588
.equ ARM64_EARLY_STATE_SIZE,          0x590

.equ ARM64_IRQ_NEON,                  0xE0
.equ ARM64_IRQ_FPCR,                  0x2E0
.equ ARM64_IRQ_FPSR,                  0x2E8
.equ ARM64_IRQ_FRAME_SIZE,            0x2F0

/*
 * The ARM64 compiler can use FP/SIMD registers for ordinary C struct copies and
 * zeroing. Preserve Q0-Q31 plus FPCR/FPSR around exception/interrupt C handlers
 * so an asynchronous kernel entry cannot corrupt interrupted user or kernel
 * SIMD state.
 */
.macro ARM64_SAVE_NEON Base, Offset
    str q0,  [\Base, #(\Offset + 0x000)]
    str q1,  [\Base, #(\Offset + 0x010)]
    str q2,  [\Base, #(\Offset + 0x020)]
    str q3,  [\Base, #(\Offset + 0x030)]
    str q4,  [\Base, #(\Offset + 0x040)]
    str q5,  [\Base, #(\Offset + 0x050)]
    str q6,  [\Base, #(\Offset + 0x060)]
    str q7,  [\Base, #(\Offset + 0x070)]
    str q8,  [\Base, #(\Offset + 0x080)]
    str q9,  [\Base, #(\Offset + 0x090)]
    str q10, [\Base, #(\Offset + 0x0A0)]
    str q11, [\Base, #(\Offset + 0x0B0)]
    str q12, [\Base, #(\Offset + 0x0C0)]
    str q13, [\Base, #(\Offset + 0x0D0)]
    str q14, [\Base, #(\Offset + 0x0E0)]
    str q15, [\Base, #(\Offset + 0x0F0)]
    str q16, [\Base, #(\Offset + 0x100)]
    str q17, [\Base, #(\Offset + 0x110)]
    str q18, [\Base, #(\Offset + 0x120)]
    str q19, [\Base, #(\Offset + 0x130)]
    str q20, [\Base, #(\Offset + 0x140)]
    str q21, [\Base, #(\Offset + 0x150)]
    str q22, [\Base, #(\Offset + 0x160)]
    str q23, [\Base, #(\Offset + 0x170)]
    str q24, [\Base, #(\Offset + 0x180)]
    str q25, [\Base, #(\Offset + 0x190)]
    str q26, [\Base, #(\Offset + 0x1A0)]
    str q27, [\Base, #(\Offset + 0x1B0)]
    str q28, [\Base, #(\Offset + 0x1C0)]
    str q29, [\Base, #(\Offset + 0x1D0)]
    str q30, [\Base, #(\Offset + 0x1E0)]
    str q31, [\Base, #(\Offset + 0x1F0)]
.endm

.macro ARM64_RESTORE_NEON Base, Offset
    ldr q0,  [\Base, #(\Offset + 0x000)]
    ldr q1,  [\Base, #(\Offset + 0x010)]
    ldr q2,  [\Base, #(\Offset + 0x020)]
    ldr q3,  [\Base, #(\Offset + 0x030)]
    ldr q4,  [\Base, #(\Offset + 0x040)]
    ldr q5,  [\Base, #(\Offset + 0x050)]
    ldr q6,  [\Base, #(\Offset + 0x060)]
    ldr q7,  [\Base, #(\Offset + 0x070)]
    ldr q8,  [\Base, #(\Offset + 0x080)]
    ldr q9,  [\Base, #(\Offset + 0x090)]
    ldr q10, [\Base, #(\Offset + 0x0A0)]
    ldr q11, [\Base, #(\Offset + 0x0B0)]
    ldr q12, [\Base, #(\Offset + 0x0C0)]
    ldr q13, [\Base, #(\Offset + 0x0D0)]
    ldr q14, [\Base, #(\Offset + 0x0E0)]
    ldr q15, [\Base, #(\Offset + 0x0F0)]
    ldr q16, [\Base, #(\Offset + 0x100)]
    ldr q17, [\Base, #(\Offset + 0x110)]
    ldr q18, [\Base, #(\Offset + 0x120)]
    ldr q19, [\Base, #(\Offset + 0x130)]
    ldr q20, [\Base, #(\Offset + 0x140)]
    ldr q21, [\Base, #(\Offset + 0x150)]
    ldr q22, [\Base, #(\Offset + 0x160)]
    ldr q23, [\Base, #(\Offset + 0x170)]
    ldr q24, [\Base, #(\Offset + 0x180)]
    ldr q25, [\Base, #(\Offset + 0x190)]
    ldr q26, [\Base, #(\Offset + 0x1A0)]
    ldr q27, [\Base, #(\Offset + 0x1B0)]
    ldr q28, [\Base, #(\Offset + 0x1C0)]
    ldr q29, [\Base, #(\Offset + 0x1D0)]
    ldr q30, [\Base, #(\Offset + 0x1E0)]
    ldr q31, [\Base, #(\Offset + 0x1F0)]
.endm

.macro ARM64_SAVE_FP_CONTROL FpcrOffset, FpsrOffset
    mrs x9, fpcr
    str x9, [sp, #\FpcrOffset]
    mrs x9, fpsr
    str x9, [sp, #\FpsrOffset]
.endm

.macro ARM64_RESTORE_FP_CONTROL FpcrOffset, FpsrOffset
    ldr x9, [sp, #\FpcrOffset]
    msr fpcr, x9
    ldr x9, [sp, #\FpsrOffset]
    msr fpsr, x9
.endm

/*
 * ============================================================================
 * FUNCTION DEFINITIONS
 * All functions with SEH directives must be defined BEFORE the vector table
 * because .space directives interfere with SEH function size calculation.
 * ============================================================================
 */

/*
 * KiArm64SyncBugcheckLoop - Bugcheck loop for unrecoverable sync exceptions.
 * Entry: x0 = pointer to early state on stack
 * This function never returns.
 */
    .global KiArm64SyncBugcheckLoop
    .seh_proc KiArm64SyncBugcheckLoop
KiArm64SyncBugcheckLoop:
    .seh_endprologue
.Lbugcheck_loop:
    bl KiArm64BugCheckEntry
    b .Lbugcheck_loop
    .seh_endproc

/*
 * KiArm64SyncVectorCommon - Common synchronous exception handler
 *
 * Entry:
 *   x0 = ORIGINAL x0 from faulting code (PRESERVED by caller!)
 *   x1 = vector ID (caller set this after saving original x1)
 *   x2-x8, x12-x27 = original values from faulting code (unchanged by caller)
 *   x9, x10, x11, x28, x29, x30 = saved by caller to stack
 *
 *   Caller's stack frame at [sp] (80 bytes):
 *     [sp+0]  = saved original x0
 *     [sp+8]  = saved original x1
 *     [sp+16] = saved original x9
 *     [sp+24] = saved original x10
 *     [sp+32] = saved original x11
 *     [sp+40] = saved original x28
 *     [sp+48] = padding
 *     [sp+56] = padding
 *     [sp+64] = saved original x29
 *     [sp+72] = saved original x30
 *   Original SP = current SP + 80
 *
 * This function sets up the early state and dispatches to C handler.
 * Never returns - either goes to KiTrapReturn or bugcheck loop.
 *
 * BUG FIX: x9, x10, x11 are now saved by the caller BEFORE this function
 * can clobber them. This ensures page fault retries have correct register
 * values even when the faulting instruction uses volatile registers x9-x11.
 */
    .global KiArm64SyncVectorCommon
    .seh_proc KiArm64SyncVectorCommon
KiArm64SyncVectorCommon:
    .seh_endprologue

    /*
     * Entry state:
     *   x1 = vector ID (caller set this)
     *   x2-x8, x12-x27 = original values from faulting code (unchanged by caller)
     *   x0 = ALSO original (caller preserved it by saving to stack before mov w1, #id)
     *   x9, x10, x11, x28, x29, x30 = caller saved these to stack
     *
     *   Caller's stack frame at [sp] (80 bytes):
     *     [sp+0]  = saved original x0
     *     [sp+8]  = saved original x1
     *     [sp+16] = saved original x9
     *     [sp+24] = saved original x10
     *     [sp+32] = saved original x11
     *     [sp+40] = saved original x28
     *     [sp+48] = padding
     *     [sp+56] = padding
     *     [sp+64] = saved original x29
     *     [sp+72] = saved original x30
     *
     * x0 is still original. x1 is vector ID.
     * x9/x10/x11/x28 can be freely used as scratch (originals on stack).
     */

    /* Save vector ID (currently in x1) to x28 temp register.
     * x28's original was already saved by caller at [sp+40].
     */
    mov x28, x1                  /* Temporarily save vector ID */

    /* Allocate space for early state */
    sub sp, sp, #ARM64_EARLY_STATE_SIZE

    ARM64_SAVE_NEON sp, ARM64_EARLY_STATE_NEON
    ARM64_SAVE_FP_CONTROL ARM64_EARLY_STATE_FPCR, ARM64_EARLY_STATE_FPSR

    /* Store vector ID */
    str x28, [sp, #ARM64_EARLY_STATE_VECTORID]

    /*
     * Save x0 - this is the ORIGINAL value from faulting code!
     * The caller saved it to stack but didn't clobber x0 itself.
     */
    str x0,  [sp, #(ARM64_EARLY_STATE_REGS + 0 * 8)]

    /*
     * Get original x1 from caller's stack frame.
     * Caller's frame is at sp + ARM64_EARLY_STATE_SIZE (80 bytes now)
     */
    add x9, sp, #ARM64_EARLY_STATE_SIZE
    ldr x10, [x9, #8]            /* Load original x1 from [caller_sp + 8] */
    str x10, [sp, #(ARM64_EARLY_STATE_REGS + 1 * 8)]

    /* Save x2-x8 (these are original, untouched by caller) */
    str x2,  [sp, #(ARM64_EARLY_STATE_REGS + 2 * 8)]
    str x3,  [sp, #(ARM64_EARLY_STATE_REGS + 3 * 8)]
    str x4,  [sp, #(ARM64_EARLY_STATE_REGS + 4 * 8)]
    str x5,  [sp, #(ARM64_EARLY_STATE_REGS + 5 * 8)]
    str x6,  [sp, #(ARM64_EARLY_STATE_REGS + 6 * 8)]
    str x7,  [sp, #(ARM64_EARLY_STATE_REGS + 7 * 8)]
    str x8,  [sp, #(ARM64_EARLY_STATE_REGS + 8 * 8)]

    /*
     * x9-x11 are volatile/caller-saved but CRITICAL for page fault handling!
     * The caller has saved their original values to the stack frame.
     * Load them from caller's frame and save to early state.
     * x9 currently points to caller's frame.
     */
    ldr x10, [x9, #16]           /* Load original x9 from caller's frame */
    str x10, [sp, #(ARM64_EARLY_STATE_REGS + 9 * 8)]
    ldr x10, [x9, #24]           /* Load original x10 from caller's frame */
    str x10, [sp, #(ARM64_EARLY_STATE_REGS + 10 * 8)]
    ldr x10, [x9, #32]           /* Load original x11 from caller's frame */
    str x10, [sp, #(ARM64_EARLY_STATE_REGS + 11 * 8)]

    /* Save x12-x18 (original, untouched by caller) */
    str x12, [sp, #(ARM64_EARLY_STATE_REGS + 12 * 8)]
    str x13, [sp, #(ARM64_EARLY_STATE_REGS + 13 * 8)]
    str x14, [sp, #(ARM64_EARLY_STATE_REGS + 14 * 8)]
    str x15, [sp, #(ARM64_EARLY_STATE_REGS + 15 * 8)]
    str x16, [sp, #(ARM64_EARLY_STATE_REGS + 16 * 8)]
    str x17, [sp, #(ARM64_EARLY_STATE_REGS + 17 * 8)]
    str x18, [sp, #(ARM64_EARLY_STATE_REGS + 18 * 8)]

    mrs x18, tpidr_el1

    /* Save x19-x27 (callee-saved, original - these were NOT touched by caller) */
    str x19, [sp, #(ARM64_EARLY_STATE_REGS + 19 * 8)]
    str x20, [sp, #(ARM64_EARLY_STATE_REGS + 20 * 8)]
    str x21, [sp, #(ARM64_EARLY_STATE_REGS + 21 * 8)]
    str x22, [sp, #(ARM64_EARLY_STATE_REGS + 22 * 8)]
    str x23, [sp, #(ARM64_EARLY_STATE_REGS + 23 * 8)]
    str x24, [sp, #(ARM64_EARLY_STATE_REGS + 24 * 8)]
    str x25, [sp, #(ARM64_EARLY_STATE_REGS + 25 * 8)]
    str x26, [sp, #(ARM64_EARLY_STATE_REGS + 26 * 8)]
    str x27, [sp, #(ARM64_EARLY_STATE_REGS + 27 * 8)]

    /* Get original x28 from caller's frame - at offset 40 in 80-byte frame */
    ldr x11, [x9, #40]           /* Load original x28 from [caller_sp + 40] */
    str x11, [sp, #(ARM64_EARLY_STATE_REGS + 28 * 8)]

    /* Get original x29/x30 from caller's frame - at offsets 64, 72 */
    ldr x10, [x9, #64]           /* Original x29 at [caller_sp + 64] */
    str x10, [sp, #(ARM64_EARLY_STATE_REGS + 29 * 8)]
    ldr x11, [x9, #72]           /* Original x30 at [caller_sp + 72] */
    str x11, [sp, #(ARM64_EARLY_STATE_REGS + 30 * 8)]

    /*
     * Save the interrupted code's stack pointer.
     *
     * For lower-EL exceptions (vectors 8+), the CPU switched from EL0 to
     * EL1 on entry.  The user stack pointer lives in SP_EL0 — the kernel
     * stack (SP_EL1) that we have been using is irrelevant for the saved
     * user context.
     *
     * For same-EL exceptions (vectors 0-7), the interrupted code was
     * running at EL1 using SP_ELx, so the original SP is the kernel
     * stack pointer before the caller's 80-byte frame was pushed.
     *
     * BUG FIX: Previously this always computed the kernel SP, causing
     * user-mode TrapFrame->Sp to contain a kernel address.  After the
     * page fault handler returned, KiTrapReturn wrote the kernel SP
     * into SP_EL0, corrupting the user stack pointer.
     */
    cmp x28, #8
    b.lt .Lsave_kernel_sp
    mrs x12, sp_el0
    b .Lstore_saved_sp
.Lsave_kernel_sp:
    add x12, x9, #80
.Lstore_saved_sp:
    str x12, [sp, #ARM64_EARLY_STATE_REG_SP]

    /* Save exception registers */
    mrs x13, elr_el1
    str x13, [sp, #ARM64_EARLY_STATE_ELR]
    str x13, [sp, #ARM64_EARLY_STATE_REG_PC]
    mrs x14, spsr_el1
    str x14, [sp, #ARM64_EARLY_STATE_SPSR]
    str x14, [sp, #ARM64_EARLY_STATE_REG_PSTATE]
    mrs x15, esr_el1
    str x15, [sp, #ARM64_EARLY_STATE_ESR]
    mrs x16, far_el1
    str x16, [sp, #ARM64_EARLY_STATE_FAR]

    /* Initialize trap/exc pointers to NULL */
    str xzr, [sp, #ARM64_EARLY_STATE_TRAP_PTR]
    str xzr, [sp, #ARM64_EARLY_STATE_EXC_PTR]

    /* Memory barriers */
    dsb sy
    isb

    /* Allow IRQs during the C handler only when the interrupted context had them enabled (SPSR.I clear) */
    tbnz x14, #7, .Lsync_keep_irq_masked
    msr daifclr, #0x2
.Lsync_keep_irq_masked:
    isb

    /* Call C handler */
    mov x0, sp
    bl KiArm64HandleSynchronousException
    dsb sy
    isb

    /* Check result */
    cbz x0, .Lsync_bugcheck

    /* Success path */
    dsb ishst
    ldr x2, [sp, #ARM64_EARLY_STATE_TRAP_PTR]
    ldr x3, [sp, #ARM64_EARLY_STATE_EXC_PTR]
    dsb ld

    /*
     * CRITICAL: Disable ALL interrupts BEFORE popping the stack.
     *
     * The trap frame and exception frame are embedded in the early state
     * context on the kernel stack. After the `add sp` instructions below,
     * this memory lies below SP and is effectively deallocated. If an IRQ
     * fires between the stack pop and KiTrapReturn's daifset, the IRQ
     * handler (0xD0-byte frame + C call stack for timer/DPC processing)
     * overwrites the deallocated trap frame data. KiTrapReturn then reads
     * corrupted SPSR/ELR values and ERETs to a garbage address — typically
     * a kernel stack address, causing a kernel instruction abort.
     *
     * The trap frame sits ~0x318 bytes below the restored SP. The IRQ
     * handler's 0xD0-byte save area plus ~500+ bytes of C call stack
     * easily reaches that depth, corrupting TrapFrame->Spsr and ->Pc.
     */
    msr daifset, #0xF
    isb

    ldr x9, [sp, #ARM64_EARLY_STATE_ESR]
    lsr x9, x9, #26
    cmp x9, #0x07                  /* FP/ASIMD trap: handler owns restored state */
    b.eq .Lsync_skip_neon_restore
    cmp x9, #0x19                  /* SVE trap */
    b.eq .Lsync_skip_neon_restore
    cmp x9, #0x1D                  /* SME trap */
    b.eq .Lsync_skip_neon_restore
    ARM64_RESTORE_FP_CONTROL ARM64_EARLY_STATE_FPCR, ARM64_EARLY_STATE_FPSR
    ARM64_RESTORE_NEON sp, ARM64_EARLY_STATE_NEON
.Lsync_skip_neon_restore:

    add sp, sp, #ARM64_EARLY_STATE_SIZE

    /* Pop caller's 80-byte frame (x0, x1, x9, x10, x11, x28, padding*2, x29, x30) */
    add sp, sp, #80

    /* Set up args for KiTrapReturn */
    mov x0, x2
    mov x1, x3
    b KiTrapReturn

.Lsync_bugcheck:
    mov x0, sp
    b KiArm64SyncBugcheckLoop
    .seh_endproc

/*
 * Sync handler: SyncSp0 (vector 0)
 *
 * CRITICAL: We must save ALL volatile registers that the common handler uses
 * as scratch BEFORE the common handler can clobber them. This includes:
 * - x0, x1: Used by common handler for vector ID and frame pointer
 * - x9, x10, x11: Used as scratch registers in common handler
 * - x28: Used as temp to hold vector ID
 * - x29, x30: Frame pointer and link register
 *
 * Stack frame layout (80 bytes, 16-byte aligned):
 *   [sp+0]  = original x0
 *   [sp+8]  = original x1
 *   [sp+16] = original x9  (volatile, used as scratch in common handler)
 *   [sp+24] = original x10 (volatile, used as scratch in common handler)
 *   [sp+32] = original x11 (volatile, used as scratch in common handler)
 *   [sp+40] = original x28 (callee-saved, used as temp for vector ID)
 *   [sp+48] = padding (for 16-byte alignment)
 *   [sp+56] = padding
 *   [sp+64] = original x29
 *   [sp+72] = original x30
 *
 * BUG FIX: Previously x9-x11 were not saved by the caller, so when the common
 * handler used them as scratch, their original values were lost. This caused
 * page fault retries to fail when the faulting instruction used x9-x11.
 */
    .global KiArm64PermVectorSyncSp0_Handler
    .seh_proc KiArm64PermVectorSyncSp0_Handler
KiArm64PermVectorSyncSp0_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x0, x1, [sp, #-80]!       /* Save original x0, x1 FIRST */
    .seh_stackalloc 80
    stp x9, x10, [sp, #16]        /* Save x9, x10 before common handler uses them */
    .seh_nop
    stp x11, x28, [sp, #32]       /* Save x11, x28 */
    .seh_nop
    stp x29, x30, [sp, #64]       /* Save x29, x30 */
    .seh_save_fplr 64
    add x29, sp, #64
    .seh_add_fp 64
    .seh_endprologue
    mov w1, #0                    /* Vector ID in x1, preserving x0 */
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* Sync handler: SyncSpx (vector 4) - MAIN KERNEL SYNC EXCEPTION VECTOR
 *
 * Stack frame layout (80 bytes, 16-byte aligned):
 *   [sp+0]  = original x0
 *   [sp+8]  = original x1
 *   [sp+16] = original x9  (volatile, used as scratch in common handler)
 *   [sp+24] = original x10 (volatile, used as scratch in common handler)
 *   [sp+32] = original x11 (volatile, used as scratch in common handler)
 *   [sp+40] = original x28 (callee-saved, used as temp for vector ID)
 *   [sp+48] = padding
 *   [sp+56] = padding
 *   [sp+64] = original x29
 *   [sp+72] = original x30
 */
    .global KiArm64PermVectorSyncSpx_Handler
    .seh_proc KiArm64PermVectorSyncSpx_Handler
KiArm64PermVectorSyncSpx_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x0, x1, [sp, #-80]!       /* Save original x0, x1 FIRST */
    .seh_stackalloc 80
    stp x9, x10, [sp, #16]        /* Save x9, x10 before common handler uses them */
    .seh_nop
    stp x11, x28, [sp, #32]       /* Save x11, x28 */
    .seh_nop
    stp x29, x30, [sp, #64]       /* Save x29, x30 */
    .seh_save_fplr 64
    add x29, sp, #64
    .seh_add_fp 64
    .seh_endprologue
    mov w1, #4                    /* Vector ID in x1, preserving x0 */
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* Sync handler: SyncLowerA64 (vector 8) - same 80-byte frame as SyncSpx */
    .global KiArm64PermVectorSyncLowerA64_Handler
    .seh_proc KiArm64PermVectorSyncLowerA64_Handler
KiArm64PermVectorSyncLowerA64_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x0, x1, [sp, #-80]!       /* Save original x0, x1 FIRST */
    .seh_stackalloc 80
    stp x9, x10, [sp, #16]        /* Save x9, x10 before common handler uses them */
    .seh_nop
    stp x11, x28, [sp, #32]       /* Save x11, x28 */
    .seh_nop
    stp x29, x30, [sp, #64]       /* Save x29, x30 */
    .seh_save_fplr 64
    add x29, sp, #64
    .seh_add_fp 64
    .seh_endprologue

    mov w1, #8                    /* Vector ID in x1, preserving x0 */
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* Sync handler: SyncLowerA32 (vector 12) - same 80-byte frame as SyncSpx */
    .global KiArm64PermVectorSyncLowerA32_Handler
    .seh_proc KiArm64PermVectorSyncLowerA32_Handler
KiArm64PermVectorSyncLowerA32_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x0, x1, [sp, #-80]!       /* Save original x0, x1 FIRST */
    .seh_stackalloc 80
    stp x9, x10, [sp, #16]        /* Save x9, x10 before common handler uses them */
    .seh_nop
    stp x11, x28, [sp, #32]       /* Save x11, x28 */
    .seh_nop
    stp x29, x30, [sp, #64]       /* Save x29, x30 */
    .seh_save_fplr 64
    add x29, sp, #64
    .seh_add_fp 64
    .seh_endprologue
    mov w1, #12                   /* Vector ID in x1, preserving x0 */
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* SError handler: SErrorSp0 (vector 3) - same 80-byte frame as SyncSpx */
    .global KiArm64PermVectorSErrorSp0_Handler
    .seh_proc KiArm64PermVectorSErrorSp0_Handler
KiArm64PermVectorSErrorSp0_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x0, x1, [sp, #-80]!       /* Save original x0, x1 FIRST */
    .seh_stackalloc 80
    stp x9, x10, [sp, #16]        /* Save x9, x10 before common handler uses them */
    .seh_nop
    stp x11, x28, [sp, #32]       /* Save x11, x28 */
    .seh_nop
    stp x29, x30, [sp, #64]       /* Save x29, x30 */
    .seh_save_fplr 64
    add x29, sp, #64
    .seh_add_fp 64
    .seh_endprologue
    mov w1, #3                    /* Vector ID in x1, preserving x0 */
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* SError handler: SErrorSpx (vector 7) - same 80-byte frame as SyncSpx */
    .global KiArm64PermVectorSErrorSpx_Handler
    .seh_proc KiArm64PermVectorSErrorSpx_Handler
KiArm64PermVectorSErrorSpx_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x0, x1, [sp, #-80]!       /* Save original x0, x1 FIRST */
    .seh_stackalloc 80
    stp x9, x10, [sp, #16]        /* Save x9, x10 before common handler uses them */
    .seh_nop
    stp x11, x28, [sp, #32]       /* Save x11, x28 */
    .seh_nop
    stp x29, x30, [sp, #64]       /* Save x29, x30 */
    .seh_save_fplr 64
    add x29, sp, #64
    .seh_add_fp 64
    .seh_endprologue
    mov w1, #7                    /* Vector ID in x1, preserving x0 */
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* SError handler: SErrorLowerA64 (vector 11) - same 80-byte frame as SyncSpx */
    .global KiArm64PermVectorSErrorLowerA64_Handler
    .seh_proc KiArm64PermVectorSErrorLowerA64_Handler
KiArm64PermVectorSErrorLowerA64_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x0, x1, [sp, #-80]!       /* Save original x0, x1 FIRST */
    .seh_stackalloc 80
    stp x9, x10, [sp, #16]        /* Save x9, x10 before common handler uses them */
    .seh_nop
    stp x11, x28, [sp, #32]       /* Save x11, x28 */
    .seh_nop
    stp x29, x30, [sp, #64]       /* Save x29, x30 */
    .seh_save_fplr 64
    add x29, sp, #64
    .seh_add_fp 64
    .seh_endprologue
    mov w1, #11                   /* Vector ID in x1, preserving x0 */
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* SError handler: SErrorLowerA32 (vector 15) - same 80-byte frame as SyncSpx */
    .global KiArm64PermVectorSErrorLowerA32_Handler
    .seh_proc KiArm64PermVectorSErrorLowerA32_Handler
KiArm64PermVectorSErrorLowerA32_Handler:
    msr daifset, #0x3
    .seh_nop
    stp x0, x1, [sp, #-80]!       /* Save original x0, x1 FIRST */
    .seh_stackalloc 80
    stp x9, x10, [sp, #16]        /* Save x9, x10 before common handler uses them */
    .seh_nop
    stp x11, x28, [sp, #32]       /* Save x11, x28 */
    .seh_nop
    stp x29, x30, [sp, #64]       /* Save x29, x30 */
    .seh_save_fplr 64
    add x29, sp, #64
    .seh_add_fp 64
    .seh_endprologue
    mov w1, #15                   /* Vector ID in x1, preserving x0 */
    bl KiArm64SyncVectorCommon
    brk #0xf001
    .seh_endproc

/* IRQ handler: IrqSp0 (vector 1) */
    .global KiArm64PermVectorIrqSp0_Handler
    .seh_proc KiArm64PermVectorIrqSp0_Handler
KiArm64PermVectorIrqSp0_Handler:
    sub sp, sp, #ARM64_IRQ_FRAME_SIZE
    .seh_stackalloc ARM64_IRQ_FRAME_SIZE
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    ARM64_SAVE_NEON sp, ARM64_IRQ_NEON
    ARM64_SAVE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR
    dsb sy
    isb
    mov w0, #1
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    /*
     * CRITICAL: Mask ALL exceptions before restoring ELR_EL1/SPSR_EL1.
     * If an IRQ fires between msr elr_el1 and eret, the hardware overwrites
     * ELR_EL1/SPSR_EL1 with the current context, losing the return-to-user
     * values. The nested handler's eret would return here, but our subsequent
     * eret would use wrong ELR/SPSR — causing return to a garbage address.
    */
    msr daifset, #0xF
    isb
    ARM64_RESTORE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR
    ARM64_RESTORE_NEON sp, ARM64_IRQ_NEON
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #ARM64_IRQ_FRAME_SIZE
    mrs x18, tpidr_el1
    dsb sy
    isb
    eret
    .seh_endproc

/* FIQ handler: FiqSp0 (vector 2) */
    .global KiArm64PermVectorFiqSp0_Handler
    .seh_proc KiArm64PermVectorFiqSp0_Handler
KiArm64PermVectorFiqSp0_Handler:
    sub sp, sp, #ARM64_IRQ_FRAME_SIZE
    .seh_stackalloc ARM64_IRQ_FRAME_SIZE
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    ARM64_SAVE_NEON sp, ARM64_IRQ_NEON
    ARM64_SAVE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR
    dsb sy
    isb
    mov w0, #2
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    msr daifset, #0xF
    isb
    ARM64_RESTORE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR
    ARM64_RESTORE_NEON sp, ARM64_IRQ_NEON
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #ARM64_IRQ_FRAME_SIZE
    mrs x18, tpidr_el1
    dsb sy
    isb
    eret
    .seh_endproc

/* IRQ handler: IrqSpx (vector 5) - MAIN KERNEL IRQ VECTOR */
    .global KiArm64PermVectorIrqSpx_Handler
    .seh_proc KiArm64PermVectorIrqSpx_Handler
KiArm64PermVectorIrqSpx_Handler:
    sub sp, sp, #ARM64_IRQ_FRAME_SIZE
    .seh_stackalloc ARM64_IRQ_FRAME_SIZE
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    ARM64_SAVE_NEON sp, ARM64_IRQ_NEON
    ARM64_SAVE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR
    dsb sy
    isb
    mov w0, #5
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    msr daifset, #0xF
    isb
    ARM64_RESTORE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR
    ARM64_RESTORE_NEON sp, ARM64_IRQ_NEON
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #ARM64_IRQ_FRAME_SIZE
    mrs x18, tpidr_el1
    dsb sy
    isb
    eret
    .seh_endproc

/* FIQ handler: FiqSpx (vector 6) */
    .global KiArm64PermVectorFiqSpx_Handler
    .seh_proc KiArm64PermVectorFiqSpx_Handler
KiArm64PermVectorFiqSpx_Handler:
    sub sp, sp, #ARM64_IRQ_FRAME_SIZE
    .seh_stackalloc ARM64_IRQ_FRAME_SIZE
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    ARM64_SAVE_NEON sp, ARM64_IRQ_NEON
    ARM64_SAVE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR
    dsb sy
    isb
    mov w0, #6
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    msr daifset, #0xF
    isb
    ARM64_RESTORE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR
    ARM64_RESTORE_NEON sp, ARM64_IRQ_NEON
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #ARM64_IRQ_FRAME_SIZE
    mrs x18, tpidr_el1
    dsb sy
    isb
    eret
    .seh_endproc

/* IRQ handler: IrqLowerA64 (vector 9)
 *
 * CRITICAL: This is a lower-EL handler — the interrupted code was running
 * at EL0 using SP_EL0 as its stack pointer.  SP_EL0 is NOT part of the
 * normal GPR save set but MUST be preserved across context switches.
 *
 * If a timer IRQ triggers a scheduler context switch while we're in
 * KiArm64InterruptDispatchEntry, the new thread may write a different
 * value to SP_EL0 (via KiTrapReturn's `msr sp_el0`).  When we resume
 * and eret back to EL0, the CPU would use the WRONG SP_EL0 — causing
 * the non-deterministic SP corruption observed in smss.exe.
 *
 * Save the interrupted GPRs before reading SP_EL0 through a scratch GPR,
 * otherwise the scratch register itself is corrupted on return to EL0.
 * Restore SP_EL0 before restoring the saved GPRs.
 */
    .global KiArm64PermVectorIrqLowerA64_Handler
    .seh_proc KiArm64PermVectorIrqLowerA64_Handler
KiArm64PermVectorIrqLowerA64_Handler:
    sub sp, sp, #ARM64_IRQ_FRAME_SIZE
    .seh_stackalloc ARM64_IRQ_FRAME_SIZE
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    mrs x9, sp_el0
    str x9, [sp, #0xD0]           /* Save user SP_EL0 after preserving x9 */
    mrs x18, tpidr_el1
    ARM64_SAVE_NEON sp, ARM64_IRQ_NEON
    ARM64_SAVE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR

    dsb sy
    isb
    mov w0, #9
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    msr daifset, #0xF
    isb
    ARM64_RESTORE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR
    ARM64_RESTORE_NEON sp, ARM64_IRQ_NEON
    ldr x9, [sp, #0xD0]           /* Restore user SP_EL0 BEFORE GPRs */
    msr sp_el0, x9
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #ARM64_IRQ_FRAME_SIZE
    dsb sy
    isb
    eret
    .seh_endproc

/* FIQ handler: FiqLowerA64 (vector 10) — save/restore SP_EL0 (see vector 9) */
    .global KiArm64PermVectorFiqLowerA64_Handler
    .seh_proc KiArm64PermVectorFiqLowerA64_Handler
KiArm64PermVectorFiqLowerA64_Handler:
    sub sp, sp, #ARM64_IRQ_FRAME_SIZE
    .seh_stackalloc ARM64_IRQ_FRAME_SIZE
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    mrs x9, sp_el0
    str x9, [sp, #0xD0]           /* Save user SP_EL0 after preserving x9 */
    mrs x18, tpidr_el1
    ARM64_SAVE_NEON sp, ARM64_IRQ_NEON
    ARM64_SAVE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR

    dsb sy
    isb
    mov w0, #10
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    msr daifset, #0xF
    isb
    ARM64_RESTORE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR
    ARM64_RESTORE_NEON sp, ARM64_IRQ_NEON
    ldr x9, [sp, #0xD0]           /* Restore user SP_EL0 BEFORE GPRs */
    msr sp_el0, x9
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #ARM64_IRQ_FRAME_SIZE
    dsb sy
    isb
    eret
    .seh_endproc

/* IRQ handler: IrqLowerA32 (vector 13) — save/restore SP_EL0 (see vector 9) */
    .global KiArm64PermVectorIrqLowerA32_Handler
    .seh_proc KiArm64PermVectorIrqLowerA32_Handler
KiArm64PermVectorIrqLowerA32_Handler:
    sub sp, sp, #ARM64_IRQ_FRAME_SIZE
    .seh_stackalloc ARM64_IRQ_FRAME_SIZE
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    mrs x9, sp_el0
    str x9, [sp, #0xD0]           /* Save user SP_EL0 after preserving x9 */
    mrs x18, tpidr_el1
    ARM64_SAVE_NEON sp, ARM64_IRQ_NEON
    ARM64_SAVE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR
    dsb sy
    isb
    mov w0, #13
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    msr daifset, #0xF
    isb
    ARM64_RESTORE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR
    ARM64_RESTORE_NEON sp, ARM64_IRQ_NEON
    ldr x9, [sp, #0xD0]           /* Restore user SP_EL0 BEFORE GPRs */
    msr sp_el0, x9
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #ARM64_IRQ_FRAME_SIZE
    dsb sy
    isb
    eret
    .seh_endproc

/* FIQ handler: FiqLowerA32 (vector 14) — save/restore SP_EL0 (see vector 9) */
    .global KiArm64PermVectorFiqLowerA32_Handler
    .seh_proc KiArm64PermVectorFiqLowerA32_Handler
KiArm64PermVectorFiqLowerA32_Handler:
    sub sp, sp, #ARM64_IRQ_FRAME_SIZE
    .seh_stackalloc ARM64_IRQ_FRAME_SIZE
    stp x20, x21, [sp, #0xA0]
    .seh_save_regp x20, 0xA0
    stp x29, x30, [sp, #0xB0]
    .seh_save_fplr 0xB0
    .seh_endprologue
    mrs x20, elr_el1
    mrs x21, spsr_el1
    stp x20, x21, [sp, #0xC0]
    stp x0,  x1,  [sp, #0x00]
    stp x2,  x3,  [sp, #0x10]
    stp x4,  x5,  [sp, #0x20]
    stp x6,  x7,  [sp, #0x30]
    stp x8,  x9,  [sp, #0x40]
    stp x10, x11, [sp, #0x50]
    stp x12, x13, [sp, #0x60]
    stp x14, x15, [sp, #0x70]
    stp x16, x17, [sp, #0x80]
    stp x18, x19, [sp, #0x90]
    mrs x9, sp_el0
    str x9, [sp, #0xD0]           /* Save user SP_EL0 after preserving x9 */
    mrs x18, tpidr_el1
    ARM64_SAVE_NEON sp, ARM64_IRQ_NEON
    ARM64_SAVE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR
    dsb sy
    isb
    mov w0, #14
    bl KiArm64InterruptDispatchEntry
    dsb sy
    isb
    msr daifset, #0xF
    isb
    ARM64_RESTORE_FP_CONTROL ARM64_IRQ_FPCR, ARM64_IRQ_FPSR
    ARM64_RESTORE_NEON sp, ARM64_IRQ_NEON
    ldr x9, [sp, #0xD0]           /* Restore user SP_EL0 BEFORE GPRs */
    msr sp_el0, x9
    ldp x18, x19, [sp, #0x90]
    ldp x16, x17, [sp, #0x80]
    ldp x14, x15, [sp, #0x70]
    ldp x12, x13, [sp, #0x60]
    ldp x10, x11, [sp, #0x50]
    ldp x8,  x9,  [sp, #0x40]
    ldp x6,  x7,  [sp, #0x30]
    ldp x4,  x5,  [sp, #0x20]
    ldp x2,  x3,  [sp, #0x10]
    ldp x0,  x1,  [sp, #0x00]
    ldp x29, x30, [sp, #0xB0]
    ldp x20, x21, [sp, #0xC0]
    msr elr_el1, x20
    msr spsr_el1, x21
    ldp x20, x21, [sp, #0xA0]
    add sp, sp, #ARM64_IRQ_FRAME_SIZE
    dsb sy
    isb
    eret
    .seh_endproc

/* Legacy symbols for external references */
.global KiArm64PermVectorSyncSp0
.set KiArm64PermVectorSyncSp0, KiArm64PermVectorSyncSp0_Handler
.global KiArm64PermVectorSyncSpx
.set KiArm64PermVectorSyncSpx, KiArm64PermVectorSyncSpx_Handler

/*
 * ============================================================================
 * ARM64 EXCEPTION VECTOR TABLE
 * This MUST come AFTER all SEH functions because .space interferes with
 * SEH function size calculations.
 *
 * CRITICAL: Each vector entry MUST be at a 0x80 (128) byte boundary!
 * The ARM64 architecture requires vectors to be aligned at 128-byte intervals.
 * Total table size = 16 entries * 128 bytes = 0x800 bytes
 *
 * The vector table is placed in a separate section ".pvec" with 2048-byte
 * alignment to ensure VBAR_EL1 requirements are met (bits [10:0] must be 0).
 * ============================================================================
 */

    .section .pvec, "ax"
    .balign 2048   /* Ensure 2048-byte alignment for VBAR_EL1 */

    .global KiArm64VectorTable
KiArm64VectorTable:

/* Entry 0 - 0x000: Sync, Current EL SP0 */
    b KiArm64PermVectorSyncSp0_Handler
    .space 0x7C

/* Entry 1 - 0x080: IRQ, Current EL SP0 */
    b KiArm64PermVectorIrqSp0_Handler
    .space 0x7C

/* Entry 2 - 0x100: FIQ, Current EL SP0 */
    b KiArm64PermVectorFiqSp0_Handler
    .space 0x7C

/* Entry 3 - 0x180: SError, Current EL SP0 */
    b KiArm64PermVectorSErrorSp0_Handler
    .space 0x7C

/* Entry 4 - 0x200: Sync, Current EL SPx */
    b KiArm64PermVectorSyncSpx_Handler
    .space 0x7C

/* Entry 5 - 0x280: IRQ, Current EL SPx - MAIN KERNEL IRQ VECTOR */
    b KiArm64PermVectorIrqSpx_Handler
    .space 0x7C

/* Entry 6 - 0x300: FIQ, Current EL SPx */
    b KiArm64PermVectorFiqSpx_Handler
    .space 0x7C

/* Entry 7 - 0x380: SError, Current EL SPx */
    b KiArm64PermVectorSErrorSpx_Handler
    .space 0x7C

/* Entry 8 - 0x400: Sync, Lower EL A64 */
    b KiArm64PermVectorSyncLowerA64_Handler
    .space 0x7C

/* Entry 9 - 0x480: IRQ, Lower EL A64 */
    b KiArm64PermVectorIrqLowerA64_Handler
    .space 0x7C

/* Entry 10 - 0x500: FIQ, Lower EL A64 */
    b KiArm64PermVectorFiqLowerA64_Handler
    .space 0x7C

/* Entry 11 - 0x580: SError, Lower EL A64 */
    b KiArm64PermVectorSErrorLowerA64_Handler
    .space 0x7C

/* Entry 12 - 0x600: Sync, Lower EL A32 */
    b KiArm64PermVectorSyncLowerA32_Handler
    .space 0x7C

/* Entry 13 - 0x680: IRQ, Lower EL A32 */
    b KiArm64PermVectorIrqLowerA32_Handler
    .space 0x7C

/* Entry 14 - 0x700: FIQ, Lower EL A32 */
    b KiArm64PermVectorFiqLowerA32_Handler
    .space 0x7C

/* Entry 15 - 0x780: SError, Lower EL A32 */
    b KiArm64PermVectorSErrorLowerA32_Handler
    .space 0x7C
