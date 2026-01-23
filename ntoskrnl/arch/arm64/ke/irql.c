/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Interrupt request level management for ARM64
 *
 * ARCHITECTURAL NOTES (from expert review):
 *
 * 1. FIXED: Per-CPU re-entrancy guard
 *    - Now using Prcb->InHighLevelTransition instead of global variable
 *    - Prevents false re-entrancy on SMP when multiple CPUs lower IRQL concurrently
 *
 * 2. TODO: GIC Priority Masking (ICC_PMR_EL1)
 *    - Current implementation uses binary DAIF masking (all IRQs on/off)
 *    - For production SMP, should use GIC priority masking via ICC_PMR_EL1
 *    - This allows high-priority interrupts (clock, IPI) to nest/preempt lower-priority ISRs
 *    - Need IRQL->GIC priority mapping table and PMR manipulation in KiApplyIrqMaskForIrqlTransition
 *
 * 3. TODO: TPIDR_EL1 for PCR access
 *    - Currently KeArm64CurrentPcr is a global variable (UP-only)
 *    - For SMP, must read TPIDR_EL1 system register per-CPU
 *    - Define: #define KeGetPcr() ((PKIPCR)__read_const_reg("tpidr_el1"))
 *
 * 4. TODO: Explicit memory barriers
 *    - Consider adding KeMemoryBarrier() or __dmb(_ARM64_BARRIER_SY) around CurrentIrql updates
 *    - Windows uses barriers for ordering IRQL writes vs interrupt unmasking
 *
 * 5. TODO: SError handling policy
 *    - Currently SError (DAIF.A bit) remains masked during normal execution
 *    - No SError handling path implemented yet
 *    - Document clearly to prevent confusion when async aborts occur
 *
 * 6. TODO: Scheduler triggers on IRQL lowering
 *    - Windows checks Prcb->QuantumEnd and Prcb->NextThread on IRQL lowering
 *    - May need reschedule IPI or software interrupt trigger
 *    - Currently only handling DPCs/Timers
 */

#include <ntoskrnl.h>
//#define NDEBUG  /* Temporarily disabled for timer debugging */
#include <debug.h>

extern PKIPCR KeArm64CurrentPcr;
extern KIRQL KeArm64CurrentIrql;

/*
 * KiHalInitialized - Flag tracking whether HAL.DLL has been initialized
 *
 * CRITICAL: This flag prevents calls to HalSetGicPriorityMask() before the HAL
 * is loaded and its import table is resolved. During early kernel boot (before
 * HalInitSystem), HAL imports are not yet available and calling them causes
 * immediate crashes (jump to NULL or unresolved address).
 *
 * Boot sequence:
 *   1. FreeLdr jumps to kernel entry point
 *   2. Kernel PE loader resolves imports from HAL.DLL
 *   3. Early kernel init (PCR setup, etc.) - KiHalInitialized = FALSE
 *   4. HalInitSystem(0) is called and returns successfully
 *   5. Phase1InitializationDiscard sets KiHalInitialized = TRUE (see ex/init.c)
 *   6. GIC priority masking becomes available
 *
 * Until KiHalInitialized is TRUE, we use DAIF masking exclusively.
 *
 * IMPORTANT: This flag is set by the kernel (ex/init.c), NOT by the HAL.
 * This avoids circular import dependencies between kernel and HAL.
 */
BOOLEAN KiHalInitialized = FALSE;

#undef KeLowerIrql
#undef KeRaiseIrql
#undef KeGetCurrentIrql

#define ARM64_MASK_IRQ()   __asm__ __volatile__("msr daifset, #0x3" ::: "memory") /* mask IRQ+FIQ */
#define ARM64_UNMASK_IRQ() __asm__ __volatile__("msr daifclr, #0x3" ::: "memory") /* unmask IRQ+FIQ */
#define ARM64_MASK_ALL()   __asm__ __volatile__("msr daifset, #0xf" ::: "memory")
/*
 * Don't unmask SError (bit 2 in immediate = A) during IRQL transitions.
 * SErrors from UEFI/FreeLdr can be stale and we want them to stay pended.
 * Unmask only D, I, F (bits 3, 1, 0 = 0xB).
 */
#define ARM64_UNMASK_ALL() __asm__ __volatile__("msr daifclr, #0xb" ::: "memory")

FORCEINLINE
KIRQL
KiQueryCurrentIrql(VOID)
{
    if (KeArm64CurrentPcr != NULL)
    {
        return KeArm64CurrentPcr->CurrentIrql;
    }

    return KeArm64CurrentIrql;
}

VOID
KiSetCurrentIrql(
    _In_ KIRQL Irql)
{
    if (KeArm64CurrentPcr != NULL)
    {
        KeArm64CurrentPcr->CurrentIrql = Irql;
    }

    KeArm64CurrentIrql = Irql;
}

/*
 * KiHalInitialized flag is set by the kernel's Phase1InitializationDiscard()
 * right after HalInitSystem(0) returns successfully. This ensures HAL exports
 * are available before we start using GIC priority masking.
 *
 * Prior design (REMOVED): HAL called KeArmInitializeGicSupport() to flip this flag.
 * This created a circular import dependency (HAL imports kernel, kernel imports HAL).
 *
 * New design: Kernel owns the flag and sets it after HalInitSystem(0) completes.
 * This eliminates the circular dependency and gives the kernel explicit control
 * over when GIC priority masking becomes active.
 */

ULONG
NTAPI
KeGetCurrentProcessorNumber(VOID)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    return Prcb ? Prcb->Number : 0;
}

ULONG
NTAPI
KeGetCurrentProcessorNumberEx(
    _Out_opt_ PPROCESSOR_NUMBER ProcNumber)
{
    PKPRCB Prcb = KeGetCurrentPrcb();
    ULONG Processor = Prcb ? Prcb->Number : 0;

    if (ProcNumber)
    {
        ProcNumber->Group = 0;
        ProcNumber->Number = (UCHAR)Processor;
        ProcNumber->Reserved = 0;
    }

    return Processor;
}

/*
 * KiApplyIrqMaskForIrqlTransition - Apply ARM64 GIC priority mask for IRQL changes
 *
 * ARCHITECTURAL DESIGN:
 * ====================
 * ARM64 uses the GIC (Generic Interrupt Controller) priority masking to implement
 * Windows IRQL semantics. Unlike x86 where the PIC/APIC mask individual interrupt
 * vectors, ARM64 uses a priority threshold register (ICC_PMR_EL1) that blocks
 * interrupts below a certain priority level.
 *
 * GIC Priority Mapping (lower priority number = higher urgency):
 *   IRQL 15 (HIGH_LEVEL)  -> GIC priority 0x00 (highest, masks all)
 *   IRQL 14 (IPI_LEVEL)   -> GIC priority 0x10
 *   IRQL 13 (CLOCK_LEVEL) -> GIC priority 0x20
 *   IRQL 12-3 (DEVICE)    -> GIC priority 0x30-0xC0
 *   IRQL 2-0 (PASSIVE/DISPATCH) -> GIC priority 0xFF (lowest, allows all)
 *
 * CRITICAL FIX FOR TIMER INTERRUPTS:
 * ===================================
 * The old implementation used binary DAIF masking (all IRQs on/off) which meant
 * that raising IRQL to DISPATCH_LEVEL would mask ALL interrupts, including the
 * timer interrupt at CLOCK_LEVEL. This caused timer interrupts to never fire,
 * preventing waiting threads from waking up.
 *
 * The new implementation uses GIC priority masking, which allows high-priority
 * interrupts (timer, IPI) to preempt low-priority code (device ISRs, DPCs).
 *
 * For example:
 *   - At PASSIVE_LEVEL (IRQL=0): PMR=0xFF, all interrupts allowed
 *   - At DISPATCH_LEVEL (IRQL=2): PMR=0xFF, all hardware interrupts still allowed
 *   - At DEVICE_LEVEL (IRQL=5): PMR=0x80, blocks lower device interrupts, allows timer/IPI
 *   - At CLOCK_LEVEL (IRQL=13): PMR=0x20, blocks all except IPI
 *   - At HIGH_LEVEL (IRQL=15): PMR=0x00, blocks all interrupts (via DAIF.I)
 *
 * DAIF USAGE:
 * ===========
 * DAIF.I (bit 7 in DAIF) is only used at HIGH_LEVEL to completely disable interrupt
 * delivery at the CPU core. At all other IRQLs, DAIF.I is cleared and the GIC
 * priority mask controls which interrupts are delivered.
 *
 * This matches Windows 11 ARM64 behavior where:
 *   - HIGH_LEVEL: DAIF.I=1 (CPU-level masking, no interrupts at all)
 *   - All other levels: DAIF.I=0 (CPU accepts interrupts), ICC_PMR_EL1 controls filtering
 *
 * MEMORY ORDERING:
 * ================
 * We use DSB SY barriers to ensure:
 *   1. IRQL value writes are visible before ICC_PMR_EL1 writes
 *   2. ICC_PMR_EL1 writes complete before any subsequent code executes
 *
 * This prevents race conditions where an interrupt fires before the new IRQL
 * is visible, causing incorrect IRQL checks in the ISR.
 */

VOID
KiApplyIrqMaskForIrqlTransition(
    _In_ KIRQL OldIrql,
    _In_ KIRQL NewIrql)
{

    /*
     * EARLY BOOT GUARD: Before HAL is initialized, use DAIF masking exclusively.
     *
     * During early kernel boot (before HalInitSystem), HAL.DLL imports are not
     * yet resolved. Calling HalSetGicPriorityMask() at this stage would jump to
     * an unresolved address (0 or garbage), causing an immediate crash.
     *
     * Until KiHalInitialized is TRUE, we use the legacy binary DAIF masking
     * (all IRQs on/off). This is safe because:
     *   1. During early boot, interrupts should remain masked (HIGH_LEVEL)
     *   2. HalInitSystem(0) completes and kernel sets KiHalInitialized = TRUE
     *   3. After that point, timer interrupts can preempt DPC/device code
     */
    if (!KiHalInitialized)
    {
        /* Fallback to binary DAIF masking before HAL is initialized */
        if (NewIrql >= HIGH_LEVEL)
        {
            ARM64_MASK_ALL();
            ARM64_SYNC_BARRIER();
        }
        else if (OldIrql >= HIGH_LEVEL && NewIrql < HIGH_LEVEL)
        {
            ARM64_UNMASK_ALL();
            ARM64_SYNC_BARRIER();
        }
        /* For other transitions, do nothing (keep current DAIF state) */
        return;
    }

    /*
     * HAL IS INITIALIZED: Use GIC priority masking for fine-grained IRQL control.
     *
     * For HIGH_LEVEL transitions, we still use DAIF masking to completely
     * disable interrupts at the CPU core. This is the most restrictive level.
     */
    if (NewIrql >= HIGH_LEVEL && OldIrql < HIGH_LEVEL)
    {
        /* Raising to HIGH_LEVEL: Mask all interrupts via DAIF */
        ARM64_MASK_ALL();
        ARM64_SYNC_BARRIER();
        /* Also set GIC PMR to most restrictive for defense in depth */
        HalSetGicPriorityMask(HIGH_LEVEL);
        return;
    }

    if (OldIrql >= HIGH_LEVEL && NewIrql < HIGH_LEVEL)
    {
        PKPRCB Prcb = KeGetCurrentPrcb();

        /*
         * Lowering from HIGH_LEVEL: Unmask interrupts via DAIF and set GIC PMR.
         *
         * Re-entrancy guard: If we're already in a HIGH_LEVEL->lower transition,
         * skip the unmask to prevent interrupt storms. The outer transition will
         * complete the unmask.
         *
         * CRITICAL: Only the thread that successfully sets the flag (wins the
         * CompareExchange) is responsible for clearing it. Re-entrant threads
         * must NOT touch the flag.
         */
        if (Prcb)
        {
            LONG OldFlag = InterlockedCompareExchange(&Prcb->InHighLevelTransition, 1, 0);
            if (OldFlag != 0)
            {
                /* Re-entrancy detected - just update GIC PMR, don't touch DAIF or flag */
                HalSetGicPriorityMask(NewIrql);
                return;
            }

            /* We won the race - perform full unmask sequence and clear flag */
            HalSetGicPriorityMask(NewIrql);
            ARM64_SYNC_BARRIER();
            ARM64_UNMASK_ALL();  /* Clear DAIF.I to allow interrupts */
            ARM64_SYNC_BARRIER();

            /*
             * Clear re-entrancy flag unconditionally. We MUST clear it because
             * we're the thread that set it. No early returns between setting
             * and clearing to prevent flag leakage.
             */
            InterlockedExchange(&Prcb->InHighLevelTransition, 0);
        }
        else
        {
            /*
             * No PRCB available (very early boot). Just do the transition
             * without re-entrancy protection. This is safe because we're
             * single-threaded at this stage.
             */
            HalSetGicPriorityMask(NewIrql);
            ARM64_SYNC_BARRIER();
            ARM64_UNMASK_ALL();
            ARM64_SYNC_BARRIER();
        }
        return;
    }

    /*
     * For all other IRQL transitions (not involving HIGH_LEVEL), use GIC
     * priority masking exclusively.
     *
     * This allows high-priority interrupts (timer at CLOCK_LEVEL=13) to
     * preempt lower-priority code (device ISRs, DPCs at DISPATCH_LEVEL=2).
     *
     * ARM64 CRITICAL FIX: When LOWERING IRQL, we must ensure DAIF.I is cleared!
     *
     * Various code paths may have called _disable() (which sets DAIF.I=1) for
     * critical sections, spinlocks, or atomic operations. Unlike x86 where
     * cli/sti is automatically restored on function return via RFLAGS, ARM64's
     * DAIF.I persists across function calls.
     *
     * If DAIF.I is left set after lowering IRQL, the timer interrupt (PPI 27)
     * cannot be delivered to the CPU even though the GIC has it pending. This
     * causes catastrophic timer stalls (100+ second gaps between timer ticks).
     *
     * We clear DAIF.I when lowering IRQL to ensure interrupts can be delivered
     * according to the GIC priority mask. Code that needs interrupts disabled
     * must use proper IRQL raising (KfRaiseIrql to HIGH_LEVEL), not just _disable().
     */
    HalSetGicPriorityMask(NewIrql);
    ARM64_SYNC_BARRIER();

    /*
     * Clear DAIF.I when lowering IRQL to allow interrupts to be delivered.
     * Only do this when actually lowering (NewIrql < OldIrql) to avoid
     * unnecessary interrupt window creation when raising IRQL.
     */
    if (NewIrql < OldIrql)
    {
        ARM64_UNMASK_IRQ();
        ARM64_SYNC_BARRIER();
    }
}

KIRQL
NTAPI
KeGetCurrentIrql(VOID)
{
    return KiQueryCurrentIrql();
}

KIRQL
FASTCALL
KfRaiseIrql(
    _In_ KIRQL NewIrql)
{
    KIRQL OldIrql = KiQueryCurrentIrql();

    if (NewIrql > HIGH_LEVEL)
    {
        DPRINT1("KfRaiseIrql: invalid IRQL %u\n", NewIrql);
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, NewIrql, OldIrql, 0, 0);
    }

    if (NewIrql <= OldIrql)
    {
        return OldIrql;
    }

    KiApplyIrqMaskForIrqlTransition(OldIrql, NewIrql);
    KiSetCurrentIrql(NewIrql);
    return OldIrql;
}

VOID
FASTCALL
KfLowerIrql(
    _In_ KIRQL NewIrql)
{
    KIRQL OldIrql = KiQueryCurrentIrql();
    CHAR Buf[128];

    if (NewIrql > HIGH_LEVEL)
    {
        PVOID RetAddr = _ReturnAddress();
        PVOID FramePtr;
        ULONG64 LinkReg;

        /* Get frame pointer and link register for stack analysis */
        __asm__ __volatile__("mov %0, x29" : "=r"(FramePtr));
        __asm__ __volatile__("mov %0, x30" : "=r"(LinkReg));

        DPRINT1("[arm64] KfLowerIrql: invalid NewIrql=%lu (OldIrql=%lu) Caller=%p FP=%p LR=%p\n",
                (ULONG)NewIrql, (ULONG)OldIrql, RetAddr, FramePtr, (PVOID)LinkReg);
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, NewIrql, OldIrql, (ULONG_PTR)RetAddr, (ULONG_PTR)LinkReg);
    }

    if (NewIrql > OldIrql)
    {
        if (NT_SUCCESS(RtlStringCbPrintfA(Buf,
                                          sizeof(Buf),
                                          "[arm64] KfLowerIrql: BUGCHECK OldIrql=%lu NewIrql=%lu",
                                          (ULONG)OldIrql,
                                          (ULONG)NewIrql)))
        {
            DPRINT1("%s\n", Buf);
        }

        DPRINT1("KfLowerIrql: raising IRQL via lower request (%u -> %u)\n", OldIrql, NewIrql);
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, NewIrql, OldIrql, 0, 0);
    }

    /*
     * CRITICAL ARM64 IRQL LOWERING SEQUENCE:
     *
     * On ARM64, we must set the new IRQL value BEFORE unmasking interrupts.
     * This is critical because:
     *
     * 1. When we unmask interrupts (via KiApplyIrqMaskForIrqlTransition), any
     *    pending hardware interrupts (including SGIs for DPC/APC) may fire
     *    immediately.
     *
     * 2. The interrupt handler (KiArm64InterruptDispatchEntry) calls
     *    HalBeginSystemInterrupt, which calls KfRaiseIrql to raise to the
     *    interrupt's synchronization level.
     *
     * 3. KfRaiseIrql returns immediately if NewIrql <= OldIrql. If we haven't
     *    updated the IRQL value yet, it will see the OLD (high) IRQL and fail
     *    to raise, leaving the ISR executing at the wrong IRQL.
     *
     * The correct sequence is:
     * 1. Set the new IRQL value (KiSetCurrentIrql)
     * 2. Unmask interrupts (KiApplyIrqMaskForIrqlTransition)
     * 3. Check for pending software DPC delivery
     *
     * This ensures that if a hardware interrupt fires after step 2, the IRQL
     * state is already consistent and HalBeginSystemInterrupt will work correctly.
     *
     * For software interrupt delivery (via DpcInterruptRequested flag), we also
     * check after unmasking to handle the case where DPCs were queued but no
     * hardware SGI was sent.
     */

    /* Set the new IRQL FIRST, before unmasking interrupts */
    KiSetCurrentIrql(NewIrql);

    /* Now unmask interrupts - any pending hardware interrupts may fire here */
    KiApplyIrqMaskForIrqlTransition(OldIrql, NewIrql);

    /*
     * After lowering IRQL, check for pending DPC interrupt.
     * Only deliver it if we're now at a low enough IRQL (< DISPATCH_LEVEL).
     *
     * ARM64 FIX: Must also check TimerRequest, not just DpcInterruptRequested!
     * The timer ISR sets TimerRequest (not DpcInterruptRequested) when timer
     * expiration DPCs need to be processed. On AMD64/x86, the DPC interrupt
     * handler checks both flags. On ARM64, we must check both here since
     * we don't have a hardware DPC interrupt - we rely on IRQL lowering.
     */
    if (NewIrql < DISPATCH_LEVEL)
    {
        PKPRCB Prcb = KeGetCurrentPrcb();

        /* Debug: Periodically log when we check for DPC delivery (disabled for performance) */
#if 0
        static ULONG DpcCheckCounter = 0;
        DpcCheckCounter++;
        if (DpcCheckCounter % 10000 == 1 && Prcb)
        {
            DPRINT1("[arm64] KfLowerIrql DPC check: Old=%u New=%u TimerReq=%p DpcReq=%d DpcDepth=%lu\n",
                    OldIrql, NewIrql, (PVOID)Prcb->TimerRequest,
                    Prcb->DpcInterruptRequested, Prcb->DpcData[0].DpcQueueDepth);
        }
#endif
        if (Prcb && (Prcb->DpcInterruptRequested ||
                     Prcb->TimerRequest ||
                     Prcb->DpcData[0].DpcQueueDepth != 0))
        {
            /*
             * Clear the DpcInterruptRequested flag FIRST to prevent re-delivery.
             * Note: TimerRequest is cleared by KiDispatchInterrupt itself.
             * If DPCs are queued again during KiDispatchInterrupt,
             * the flag will be set again and we'll deliver on the next IRQL lowering.
             */
            Prcb->DpcInterruptRequested = FALSE;

            /*
             * Dispatch the DPC interrupt.
             * This will call KiDispatchInterrupt() which processes the DPC queue.
             * KiDispatchInterrupt also checks TimerRequest and calls KiTimerExpiration.
             *
             * NOTE: We call this directly at the newly lowered IRQL.
             * KiDispatchInterrupt() will raise IRQL to DISPATCH_LEVEL internally
             * before processing DPCs, then restore it back to the current level.
             * This is critical to avoid IRQL violations when DPCs execute.
             *
             * See thrdini.c KiDispatchInterrupt() for the IRQL management logic.
             */
            extern VOID NTAPI KiDispatchInterrupt(VOID);
            KiDispatchInterrupt();
        }
    }

    /*
     * APC delivery is handled by the kernel's thread dispatch/return path,
     * not here. When lowering to APC_LEVEL or PASSIVE_LEVEL, the kernel
     * will check for pending APCs in KiThreadStartup, trap return, etc.
     */
}

NTKERNELAPI
KIRQL
KxGetCurrentIrql(VOID)
{
    return KeGetCurrentIrql();
}

NTKERNELAPI
VOID
KxLowerIrql(
    _In_ KIRQL NewIrql)
{
    KfLowerIrql(NewIrql);
}

NTKERNELAPI
KIRQL
KxRaiseIrql(
    _In_ KIRQL NewIrql)
{
    return KfRaiseIrql(NewIrql);
}

NTKERNELAPI
KIRQL
KxRaiseIrqlToDpcLevel(VOID)
{
    return KeRaiseIrqlToDpcLevel();
}

KIRQL
NTAPI
KeRaiseIrqlToDpcLevel(VOID)
{
    return KfRaiseIrql(DISPATCH_LEVEL);
}

KIRQL
NTAPI
KeRaiseIrqlToSynchLevel(VOID)
{
    return KfRaiseIrql(SYNCH_LEVEL);
}

VOID
NTAPI
KeLowerIrql(
    _In_ KIRQL NewIrql)
{
    KfLowerIrql(NewIrql);
}

VOID
NTAPI
KeRaiseIrql(
    _In_ KIRQL NewIrql,
    _Out_ PKIRQL OldIrql)
{
    *OldIrql = KfRaiseIrql(NewIrql);
}
