#include <asm.h>
#include <ksarm64.h>

    .text
    .align 4

	.global KiThreadStartup
	.global KiSwapContext
	.global KiSwapContextInternal
	.extern KfLowerIrql
	.extern KiTrapReturn

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
     * ARM64 FIX: Set x18 (TEB platform register) for the new thread.
     * Read current thread from per-CPU PRCB via TPIDR_EL1 (not the global).
     * For system threads, Teb is NULL so NtCurrentTeb() returns NULL safely.
     * For user threads, Teb is the user-mode TEB address.
     */
    mrs     x18, tpidr_el1
    cbz     x18, .Lskip_teb_startup
    ldr     x18, [x18, #PcCurrentThread]
    cbz     x18, .Lskip_teb_startup
    ldr     x18, [x18, #ThTeb]
.Lskip_teb_startup:

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
    ldr     x9, =0xFFFF800009000000
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
    .extern KeArm64CurrentPcr

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
     * ARM64 FIX: Initialize ApcBypass to prevent IRQL corruption.
     * When a thread switches away, it creates a KSWITCH_FRAME on its stack.
     * The ApcBypass field (at offset 0x68) stores the WaitIrql for this thread.
     * However, the stack memory is not zeroed when we allocate the frame (line 67).
     * If ApcBypass is not explicitly initialized, it contains garbage from whatever
     * was previously on the stack.
     *
     * The corruption sequence:
     * 1. Thread switches away: saves WaitIrql to ApcBypass, switches to another thread
     * 2. Thread switches back: restores registers, POPS the switch frame (line 101)
     * 3. Thread executes, then switches away again later
     * 4. NEW switch frame created at current SP - ApcBypass contains garbage!
     * 5. Another thread switches TO this thread, reads garbage from ApcBypass
     * 6. KfLowerIrql called with corrupted IRQL value (e.g., 0xC0 = 192)
     *
     * The fix: Initialize ApcBypass to 0 when creating the switch frame.
     * This will be immediately overwritten by KiSwapContextInternal (line 133),
     * but ensures no garbage is read if something goes wrong.
     */
    strb    wzr, [sp, #SwApcBypass]

    /*
     * Locate the target (new) thread from the per-CPU PCR.
     * TPIDR_EL1 holds the per-CPU PCR pointer (set in KiInitializePcr).
     * Do NOT use the global KeArm64CurrentPcr - it's shared and wrong on SMP.
     */
    mrs     x9, tpidr_el1
    cbz     x9, .Lfallback_thread
    ldr     x2, [x9, #PcCurrentThread]
    cbz     x2, .Lfallback_thread
    b       .Lcall_internal

.Lfallback_thread:
    /* Fallback to the caller's thread if the PCR path is unavailable */
    mov     x2, x1

.Lcall_internal:
    bl      KiSwapContextInternal

.Lrestore_context:
    /* Restore the saved non-volatile register bank */
    ldp     x19, x20, [sp, #SwX19]
    ldp     x21, x22, [sp, #SwX21]
    ldp     x23, x24, [sp, #SwX23]
    ldp     x25, x26, [sp, #SwX25]
    ldp     x27, x28, [sp, #SwX27]
    ldr     x29, [sp, #SwFp]
    ldr     x30, [sp, #SwLr]       /* Load (but don't use - we use SwReturn) */

    /*
     * ARM64 FIX: Restore x18 (platform TEB register) from the new thread's
     * KTHREAD.Teb field via per-CPU PRCB (not the global).
     * KiSwapContextResume has already updated Prcb->CurrentThread.
     */
    mrs     x9, tpidr_el1
    cbz     x9, .Lskip_teb_restore
    ldr     x9, [x9, #PcCurrentThread]
    cbz     x9, .Lskip_teb_restore
    ldr     x18, [x9, #ThTeb]
.Lskip_teb_restore:

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

    /* Wait until the incoming thread clears its SwapBusy bit */
.Lswapbusy_wait:
    ldrb    w3, [x2, #ThSwapBusy]
    cbz     w3, .Lswapbusy_clear
    yield
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
     * For NEW threads: SwLr is 0 (from RtlZeroMemory). But that's OK because
     * for new threads, SwReturn = KiThreadStartup, and KiSwapContext will
     * use ret x9 with x9 = SwReturn. However, returning to address 0 would
     * crash before reaching that point!
     *
     * FIX NEEDED: KiInitializeContextThread must set SwLr for new threads.
     * For now, check if SwLr is zero and handle it specially.
     */
    ldr     x10, [sp, #SwLr]     /* Load return address to KiSwapContext */
    cbz     x10, .Lnew_thread    /* If zero, this is a new thread */

    /* Resumed thread: Call KiSwapContextResume and return to KiSwapContext */
    str     x10, [sp, #-16]!     /* Save return address */
    bl      KiSwapContextResume
    ldr     x30, [sp], #16       /* Restore return address to LR */

    ret                          /* Return to .Lrestore_context in KiSwapContext */

.Lnew_thread:
    /*
     * NEW thread: SwLr was not set by KiInitializeContextThread.
     * For new threads, SwReturn = KiThreadStartup.
     *
     * We call KiSwapContextResume first, then jump to KiThreadStartup.
     * KiThreadStartup expects SP to point to KSWITCH_FRAME, so we
     * do NOT pop the frame here.
     *
     * NOTE: At this point, registers contain:
     *   x0 = WaitIrql (reloaded from SwApcBypass)
     *   x1 = OldThread (the thread that switched away)
     *   x2 = NewThread (this new thread)
     */

    bl      KiSwapContextResume

    ldr     x9, [sp, #SwReturn]  /* Load KiThreadStartup address */
    br      x9                   /* Jump to KiThreadStartup (SP points to KSWITCH_FRAME) */
    .seh_endproc
