#include <asm.h>
#include <ksarm64.h>

    .text
    .align 4

	.global KiThreadStartup
	.global KiSwapContext
	.global KiSwapContextInternal
	.extern KfLowerIrql
	.extern KiRetireDpcList
	.extern KiTrapReturn

    .global KiRetireDpcListInDpcStack
    .seh_proc KiRetireDpcListInDpcStack
KiRetireDpcListInDpcStack:
    stp     x29, x30, [sp, #-16]!
    .seh_save_fplr_x 16
    mov     x29, sp
    .seh_set_fp
    .seh_endprologue

    cbz     x1, .Lretire_dpc_current_stack

    mov     sp, x1
    sub     sp, sp, #16
    bl      KiRetireDpcList
    mov     sp, x29
    b       .Lretire_dpc_done

.Lretire_dpc_current_stack:
    bl      KiRetireDpcList

.Lretire_dpc_done:
    ldp     x29, x30, [sp], #16
    ret
    .seh_endproc

/*
 * KiThreadStartup
 *
 * This is the entry point for all new threads on ARM64. When a thread is first
 * scheduled, KiSwapContext returns to this function (via the ReturnAddress
 * stored in KSWITCH_FRAME).
 *
 * On entry:
 *   - SP points to KSWITCH_FRAME
 *   - KSTART_FRAME is immediately above KSWITCH_FRAME
 *   - IRQL is typically HIGH_LEVEL (from idle loop context switch)
 *
 * This function must:
 *   1. Lower IRQL to APC_LEVEL (matching AMD64 behavior)
 *   2. Load startup parameters from KSTART_FRAME
 *   3. Call SystemRoutine(StartRoutine, StartContext)
 *
 * For system threads, SystemRoutine is PspSystemThreadStartup which will
 * further lower IRQL to PASSIVE_LEVEL before calling StartRoutine.
 */
    .seh_proc KiThreadStartup
KiThreadStartup:
    /*
     * SEH: This is a special entry point - no prologue in traditional sense.
     * SP already points to KSWITCH_FRAME set up by KiInitializeContextThread.
     * We declare no frame here since we consume the existing switch frame.
     */
    .seh_endprologue

    /*
     * ARM64 ABI: keep x18 as the kernel platform register while still in EL1.
     *
     * User-mode TEB state is programmed only in KiTrapReturn immediately
     * before ERET to EL0. Clobbering x18 with KTHREAD.Teb here corrupts the
     * EL1 platform register and can redirect reserved x18-based accesses into
     * the user TEB mapping.
     */
    mrs     x18, tpidr_el1

    /* Clear non-volatile registers to prevent stale data leaking to new thread */
    mov     x19, xzr
    mov     x20, xzr
    mov     x21, xzr
    mov     x22, xzr
    mov     x23, xzr
    mov     x24, xzr
    mov     x25, xzr
    mov     x26, xzr
    mov     x27, xzr
    mov     x28, xzr
    mov     x29, xzr

    /* ARM64: When we enter here, SP points to KSWITCH_FRAME.
     * The KSTART_FRAME is immediately above it (higher address).
     * First, skip past the KSWITCH_FRAME to point SP at KSTART_FRAME.
     */

    msr     daifset, #0x4
    msr     daifclr, #0xB

    add     sp, sp, #KSWITCH_FRAME_LENGTH

    /* Load startup parameters from KSTART_FRAME (SP now points to it).
     * Preserve them in callee-saved registers across KfLowerIrql. */
    ldr     x21, [sp, #SfSystemRoutine]
    ldr     x19, [sp, #SfStartRoutine]
    ldr     x20, [sp, #SfStartContext]

    /* Pop the start frame before invoking the system routine; SP stays aligned */
    add     sp, sp, #KSTART_FRAME_LENGTH

    /*
     * CRITICAL: Lower IRQL to APC_LEVEL before calling the system routine.
     *
     * On AMD64, KiThreadStartup does: mov cr8, APC_LEVEL
     * We must do the equivalent on ARM64 by calling KfLowerIrql.
     *
     * The idle loop switches to us at HIGH_LEVEL (after KfRaiseIrql(HIGH_LEVEL)
     * in KiInitializeKernel). We need to lower to APC_LEVEL so that:
     * 1. The system routine can execute at a safe IRQL
     * 2. PspSystemThreadStartup can further lower to PASSIVE_LEVEL
     * 3. Kernel APCs can be delivered when appropriate
     */
    mov     w0, #APC_LEVEL
    bl      KfLowerIrql

    /* ARM64: x2 = SystemRoutine, x0 = StartRoutine, x1 = StartContext */
    mov     x0, x19
    mov     x1, x20
    mov     x2, x21

    /* Validate SystemRoutine is non-NULL before calling */
    cbz     x21, .Lbad_sysroutine

    /* Start the thread - call SystemRoutine(StartRoutine, StartContext) */
    blr     x2

	    /*
	     * If the system routine returned, this must be a user thread.
	     * Return to user-mode by restoring the prepared trap/exception frames.
	     *
	     * For system threads, returning is a fatal error.
	     */
	    mrs     x16, tpidr_el1
	    cbz     x16, .Lthreadstart_bad_return
	    ldr     x16, [x16, #PcCurrentThread]
	    cbz     x16, .Lthreadstart_bad_return
	    ldr     x0, [x16, #ThTrapFrame]     /* x0 = TrapFrame */
	    cbz     x0, .Lthreadstart_bad_return

	    mov     x1, sp                      /* x1 = ExceptionFrame (user threads) */

	    b       KiTrapReturn                /* Never returns (ERET) */

.Lthreadstart_bad_return:
	    brk     #0xf000

.Lbad_sysroutine:
    /* x21 was NULL - print error and hang */
    ldr     x9, =0xFFFFFC0009000000
    mov     w10, #'N'
    strb    w10, [x9]
    mov     w10, #'U'
    strb    w10, [x9]
    mov     w10, #'L'
    strb    w10, [x9]
    mov     w10, #'L'
    strb    w10, [x9]
    mov     w10, #'\n'
    strb    w10, [x9]
    brk     #0xf001

    /* Should never return */
    brk     #0xf000
    .seh_endproc


    .extern KiSwapContextResume

/*
 * BOOLEAN
 * KiSwapContext(
 *     _In_ KIRQL WaitIrql,
 *     _Inout_ PKTHREAD CurrentThread);
 */
    .seh_proc KiSwapContext
KiSwapContext:
    /* Reserve space for the switch frame and save non-volatile registers */
    sub     sp, sp, #KSWITCH_FRAME_LENGTH
    .seh_stackalloc KSWITCH_FRAME_LENGTH
    stp     x19, x20, [sp, #SwX19]
    .seh_save_regp x19, SwX19
    stp     x21, x22, [sp, #SwX21]
    .seh_save_regp x21, SwX21
    stp     x23, x24, [sp, #SwX23]
    .seh_save_regp x23, SwX23
    stp     x25, x26, [sp, #SwX25]
    .seh_save_regp x25, SwX25
    stp     x27, x28, [sp, #SwX27]
    .seh_save_regp x27, SwX27
    str     x29, [sp, #SwFp]
    .seh_save_reg x29, SwFp
    .seh_endprologue

    /*
     * ARM64 CRITICAL: We need to save TWO return addresses:
     * 1. SwReturn: The final return address (to our caller) - used by ret x9 at the end
     * 2. SwLr: The return address for KiSwapContextInternal to use after stack switch
     *
     * On AMD64, 'call' pushes the return address on the stack, so after stack switch,
     * 'ret' naturally pops from the new stack. On ARM64, 'bl' uses LR register, so
     * after stack switch we need to manually load the return address from the new stack.
     *
     * SwLr must contain the address of .Lrestore_context so that after stack switch,
     * KiSwapContextInternal can return there to restore registers.
     */
    str     x30, [sp, #SwReturn]   /* Save caller's return address */
    adr     x9, .Lrestore_context  /* Get address of restore label */
    str     x9, [sp, #SwLr]        /* Save for KiSwapContextInternal */

    /*
     * Locate the target (new) thread from the per-CPU PCR.
     * TPIDR_EL1 holds the per-CPU PCR pointer (set in KiInitializePcr).
     * Do NOT use the global KeArm64CurrentPcr - it's shared and wrong on SMP.
     */
    mrs     x9, tpidr_el1
    cbz     x9, .Lbad_current_thread
    ldr     x2, [x9, #PcCurrentThread]
    cbz     x2, .Lbad_current_thread

    bl      KiSwapContextInternal
    b       .Lrestore_context

.Lbad_current_thread:
    brk     #0xf000
    b       .Lbad_current_thread

.Lrestore_context:
    /* Restore the saved non-volatile register bank */
    ldp     x19, x20, [sp, #SwX19]
    ldp     x21, x22, [sp, #SwX21]
    ldp     x23, x24, [sp, #SwX23]
    ldp     x25, x26, [sp, #SwX25]
    ldp     x27, x28, [sp, #SwX27]
    ldr     x29, [sp, #SwFp]

    /*
     * ARM64 ABI: x18 remains the EL1 platform register across context
     * switches. User-mode TEB state is restored only by KiTrapReturn.
     */
    mrs     x18, tpidr_el1

    msr     daifclr, #0x2

    ldr     x9,  [sp, #SwReturn]   /* Load caller's return address */
    add     sp, sp, #KSWITCH_FRAME_LENGTH
    ret     x9                      /* Return to our caller */
    .seh_endproc

/*
 * BOOLEAN
 * KiSwapContextInternal(
 *     _In_ KIRQL WaitIrql,
 *     _Inout_ PKTHREAD OldThread,
 *     _Inout_ PKTHREAD NewThread);
 *
 * This function switches from the old thread's stack to the new thread's stack.
 *
 * IMPORTANT: The caller (KiSwapContext) has already created a KSWITCH_FRAME
 * on the stack with all callee-saved registers and the return address.
 * This function operates directly on that frame - it does NOT create its own.
 *
 * The KSWITCH_FRAME layout for ARM64:
 *   [sp + 0x00..0x4F]: X19-X28 (10 registers * 8 bytes = 80 bytes)
 *   [sp + 0x50]: Fp (X29)
 *   [sp + 0x58]: Lr (X30)
 *   [sp + 0x60]: ReturnAddress (where to go after context switch)
 *   [sp + 0x68]: ApcBypass
 *   Total: 0x70 bytes
 *
 * After stack switch, this function returns to KiSwapContext which will
 * restore x19-x28 from the NEW thread's KSWITCH_FRAME and then return
 * to the NEW thread's saved return address.
 */
    .align 4
    .seh_proc KiSwapContextInternal
KiSwapContextInternal:
    /*
     * SEH: This function operates on the caller's stack frame (KSWITCH_FRAME).
     * It doesn't create its own frame - it modifies SP to switch stacks.
     * We declare a minimal frame to allow unwinding.
     */
    .seh_endprologue

    msr     daifset, #0x2

    /*
     * The outgoing thread was marked busy by KiSetThreadSwapBusy under the
     * scheduler lock before it was queued. Wait until the incoming thread is
     * no longer being saved on another CPU before reusing its kernel stack.
     * Skip when the scheduler reselected the running thread (x1 == x2).
     */
    cmp     x1, x2
    b.eq    .Lswapbusy_clear

.Lswapbusy_wait:
    ldrb    w3, [x2, #ThSwapBusy]
    cbz     w3, .Lswapbusy_clear
    yield
    wfe
    b       .Lswapbusy_wait

.Lswapbusy_clear:
    /* Record the wait IRQL in the outgoing switch frame */
    strb    w0, [sp, #SwApcBypass]

    /* Publish the outgoing kernel stack */
    mov     x11, sp
    str     x11, [x1, #ThKernelStack]

    /* Switch to the incoming kernel stack */
    ldr     x3, [x2, #ThKernelStack]
    cbz     x3, .Lresume_call      /* Should not happen, but avoid faulting */
    mov     sp, x3

    /* Reload the wait IRQL requested by the target thread */
    ldrb    w0, [sp, #SwApcBypass]

.Lresume_call:
    /*
     * Load the return address from the NEW thread's KSWITCH_FRAME.
     * SwLr contains the address of .Lrestore_context in KiSwapContext.
     * This was saved when this thread previously called KiSwapContext.
     *
     * For NEW threads: KiInitializeContextThread now sets SwLr = KiThreadStartup,
     * so the cbz check is eliminated from the hot path. New threads take the same
     * resume path as existing threads, with SwLr pointing to KiThreadStartup
     * which expects SP to point to KSWITCH_FRAME.
     */
    ldr     x10, [sp, #SwLr]     /* Load return address to KiSwapContext */

    /* Resumed/new thread: Call KiSwapContextResume and return */
    str     x10, [sp, #-16]!     /* Save return address */
    bl      KiSwapContextResume
    ldr     x30, [sp], #16       /* Restore return address to LR */

    ret                          /* Return (new threads: to KiThreadStartup) */
    .seh_endproc
