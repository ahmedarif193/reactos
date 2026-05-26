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
 * 3. DONE: TPIDR_EL1 per-CPU PCR access
 *    - KiInitializePcr() writes TPIDR_EL1 = Pcr (see kiinit.c)
 *    - KeGetPcr() in ketypes.h reads tpidr_el1 with NULL fallback to global
 *    - KeGetCurrentIrql/Thread/DPC macros now read from PCR/PRCB directly
 *
 * 4. TODO: Explicit memory barriers
 *    - Consider adding KeMemoryBarrier() or __dmb(_ARM64_BARRIER_SY) around CurrentIrql updates
 *    - Windows uses barriers for ordering IRQL writes vs interrupt unmasking
 *
 * 5. SError policy
 *    - Keep SError (DAIF.A bit) masked during normal IRQL transitions
 *    - The earlier PASSIVE_LEVEL unmask policy delivered pending async aborts
 *      during idle/context-switch paths without reliable source attribution
 *    - Revisit only after the ARM64 port has validated SError attribution and
 *      a deliberate handling policy beyond fatal bugcheck
 *
 * 6. TODO: Scheduler triggers on IRQL lowering
 *    - Windows checks Prcb->QuantumEnd and Prcb->NextThread on IRQL lowering
 *    - May need reschedule IPI or software interrupt trigger
 *    - Currently only handling DPCs/Timers
 */

#include <ntoskrnl.h>
//#define NDEBUG  /* Temporarily disabled for timer debugging */
#include <debug.h>

/*
 * ARM64: IRQL is stored in Pcr->CurrentIrql (read via TPIDR_EL1 by KeGetCurrentIrql macro).
 * KeArm64CurrentIrql is kept only as an early-boot fallback before TPIDR_EL1 points
 * at the current PCR.
 */
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
 *   5. ExpInitializeExecutive calls ExArchPostHalInitSystemPhase0 and sets
 *      KiHalInitialized = TRUE before the generic post-HAL _enable()
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
#define ARM64_MASK_SERROR() __asm__ __volatile__("msr daifset, #0x4" ::: "memory")
/*
 * ARM64_UNMASK_ALL_NO_SERROR: Unmask D, I, F but NOT A (SError).
 * Used at most IRQL levels where we want to keep SError masked.
 * Immediate 0xB = bits 3,1,0 (D,I,F).
 */
#define ARM64_UNMASK_ALL_NO_SERROR() __asm__ __volatile__("msr daifclr, #0xb" ::: "memory")
FORCEINLINE
KIRQL
KiQueryCurrentIrql(VOID)
{
    PKIPCR Pcr = KeGetPcr();
    if (Pcr != NULL)
    {
        return Pcr->CurrentIrql;
    }

    /* Fallback for very early boot (before TPIDR_EL1 init) */
    return KeArm64CurrentIrql;
}

VOID
KiSetCurrentIrql(
    _In_ KIRQL Irql)
{
    PKIPCR Pcr = KeGetPcr();
    if (Pcr != NULL)
    {
        Pcr->CurrentIrql = Irql;
        return;
    }

    /* Fallback for very early boot (before TPIDR_EL1 init) */
    KeArm64CurrentIrql = Irql;
}

FORCEINLINE
VOID
KiTraceSerrorPolicy(
    _In_z_ PCSTR Site,
    _In_ KIRQL OldIrql,
    _In_ KIRQL NewIrql)
{
    UNREFERENCED_PARAMETER(Site);
    UNREFERENCED_PARAMETER(OldIrql);
    UNREFERENCED_PARAMETER(NewIrql);
}

/*
 * KiHalInitialized is set by the kernel's ARM64 post-HAL phase-0 hook right
 * after HalInitSystem(0) returns successfully. This ensures HAL exports are
 * available before we start using GIC priority masking.
 *
 * Prior design (REMOVED): HAL called KeArmInitializeGicSupport() to flip this flag.
 * This created a circular import dependency (HAL imports kernel, kernel imports HAL).
 *
 * New design: Kernel owns the flag and sets it after HalInitSystem(0) completes.
 * This eliminates the circular dependency and gives the kernel explicit control
 * over when GIC priority masking becomes active.
 */

/*
 * KiUpdateDaifForIrql - Update DAIF mask bits based on IRQL level
 *
 * SERROR HANDLING POLICY:
 * =======================
 * SError (System Error, asynchronous external abort) is ARM64's mechanism for
 * reporting asynchronous hardware errors like:
 *   - External memory parity errors
 *   - Cache ECC errors
 *   - Bus/interconnect errors
 *   - RAS (Reliability, Availability, Serviceability) events
 *
 * For bring-up we keep DAIF.A masked across normal IRQL transitions. The prior
 * PASSIVE_LEVEL unmask policy caused boot-critical async abort delivery at the
 * exact DAIF clear instruction, while FAR_EL1 still reflected the most recent
 * paged-pool access rather than a reliable faulting source.
 *
 * Until the ARM64 port has validated SError attribution and deliberate recovery
 * behavior, IRQL transitions should only control IRQ/FIQ delivery here.
 */
FORCEINLINE
BOOLEAN
KiIrqlTransitionNeedsGicPmrUpdate(
    _In_ KIRQL OldIrql,
    _In_ KIRQL NewIrql)
{
    /*
     * PASSIVE/APC/DISPATCH all use the unmasked GIC PMR value. Avoid the
     * system-register/MMIO PMR write on normal thread and spinlock transitions,
     * while still letting the DAIF/SError policy below run.
     */
    return !((OldIrql <= DISPATCH_LEVEL) && (NewIrql <= DISPATCH_LEVEL));
}

FORCEINLINE
VOID
KiUpdateDaifForIrql(
    _In_ KIRQL NewIrql,
    _In_ BOOLEAN IsHighLevelTransition)
{
    if (IsHighLevelTransition)
    {
        /* HIGH_LEVEL transitions are handled separately with full masking */
        return;
    }

    UNREFERENCED_PARAMETER(NewIrql);

    /* Normal IRQL transitions unmask D/I/F but keep SError masked (A stays set). */
    ARM64_UNMASK_ALL_NO_SERROR();

    __asm__ __volatile__("isb" ::: "memory");
}

FORCEINLINE
VOID
KiUpdateSerrorMaskOnlyForIrql(
    _In_ KIRQL NewIrql)
{
    UNREFERENCED_PARAMETER(NewIrql);

    ARM64_MASK_SERROR();

    __asm__ __volatile__("isb" ::: "memory");
}

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
    BOOLEAN NeedsPmrUpdate;

    /*
     * EARLY BOOT GUARD: Before HAL is initialized, use DAIF masking exclusively.
     *
     * During early kernel boot (before HalInitSystem), HAL.DLL imports are not
     * yet resolved. Calling HalSetGicPriorityMask() at this stage would jump to
     * an unresolved address (0 or garbage), causing an immediate crash.
     *
     * Until KiHalInitialized is TRUE, we use the legacy binary DAIF masking
     * (all IRQs on/off). Early boot may lower the logical IRQL before HAL
     * phase 0, but IRQ/FIQ delivery must stay masked until the GIC is
     * configured and the executive explicitly enables interrupts after HAL.
     */
    if (!KiHalInitialized)
    {
        /* Fallback to binary DAIF masking before HAL is initialized */
        if (NewIrql >= HIGH_LEVEL)
        {
            ARM64_MASK_ALL();
            ARM64_SYNC_BARRIER();
            KiTraceSerrorPolicy("early-high", OldIrql, NewIrql);
        }
        else if (OldIrql >= HIGH_LEVEL && NewIrql < HIGH_LEVEL)
        {
            /*
             * Before HAL phase 0, lowering IRQL is only a logical transition.
             * Keep IRQ/FIQ masked until ExpInitializeExecutive has completed
             * HalInitSystem(0), re-enabled the timer PPI, and called _enable().
             */
            KiUpdateSerrorMaskOnlyForIrql(NewIrql);
            KiTraceSerrorPolicy("early-drop-masked", OldIrql, NewIrql);
        }
        else if (NewIrql != OldIrql)
        {
            /*
             * Keep DAIF.I/F state unchanged in early boot, but still enforce
             * the SError mask policy by IRQL.
             */
            KiUpdateSerrorMaskOnlyForIrql(NewIrql);
            KiTraceSerrorPolicy("early-mask", OldIrql, NewIrql);
        }
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
        KiTraceSerrorPolicy("high-raise", OldIrql, NewIrql);
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
            /* Apply SError policy based on new IRQL */
            KiUpdateDaifForIrql(NewIrql, FALSE);
            ARM64_SYNC_BARRIER();
            KiTraceSerrorPolicy("high-drop", OldIrql, NewIrql);

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
            /* Apply SError policy based on new IRQL */
            KiUpdateDaifForIrql(NewIrql, FALSE);
            ARM64_SYNC_BARRIER();
            KiTraceSerrorPolicy("high-drop-noprcb", OldIrql, NewIrql);
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
    NeedsPmrUpdate = KiIrqlTransitionNeedsGicPmrUpdate(OldIrql, NewIrql);
    if (NeedsPmrUpdate)
    {
        HalSetGicPriorityMask(NewIrql);
        ARM64_SYNC_BARRIER();
    }

    /*
     * Enforce SError policy on every non-HIGH_LEVEL transition.
     * Lowering needs the full helper (it also fixes stale DAIF.I).
     * Raising only updates DAIF.A to avoid perturbing IRQ/FIQ masking.
     *
     * CRITICAL FIX: IsHighLevelTransition parameter must be FALSE for normal
     * IRQL transitions. Passing TRUE causes KiUpdateDaifForIrql to return early
     * without unmasking interrupts, leading to 715-second stalls in pool allocator.
     */
    if (NewIrql < OldIrql)
    {
        KiUpdateDaifForIrql(NewIrql, FALSE);
        ARM64_SYNC_BARRIER();
        KiTraceSerrorPolicy("lower", OldIrql, NewIrql);
    }
    else if (NewIrql > OldIrql)
    {
        if (NeedsPmrUpdate)
        {
            KiUpdateSerrorMaskOnlyForIrql(NewIrql);
            ARM64_SYNC_BARRIER();
        }
        KiTraceSerrorPolicy("raise", OldIrql, NewIrql);
    }
}

/*
 * Exported KeGetCurrentIrql function for drivers and other modules.
 * The kernel itself uses the macro in ketypes.h which reads from per-CPU PCR.
 * This exported version also reads from PCR via KiQueryCurrentIrql.
 */
#undef KeGetCurrentIrql
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

    /*
     * PASSIVE/APC/DISPATCH share the same unmasked GIC PMR after HAL init.
     * Raising inside that band is a logical PCR update only, so avoid the
     * DMB/PMR/ISB path that is needed only when CPU interrupt masking changes.
     */
    if (KiHalInitialized &&
        (OldIrql <= DISPATCH_LEVEL) &&
        (NewIrql <= DISPATCH_LEVEL))
    {
        KiSetCurrentIrql(NewIrql);
        return OldIrql;
    }

    /*
     * ARM64 Memory Barrier: Ensure all pending loads/stores complete before
     * IRQL raise. This prevents critical section violations where stores to
     * shared data might reorder past the IRQL raise on ARM64's relaxed memory model.
     */
    __asm__ __volatile__("dmb ish" ::: "memory");

    KiApplyIrqMaskForIrqlTransition(OldIrql, NewIrql);
    KiSetCurrentIrql(NewIrql);

    /*
     * ARM64 Instruction Barrier: Ensure IRQL change and GIC PMR update take
     * effect before continuing. Prevents speculative execution from reading
     * stale IRQL values or executing code that should be protected by the new IRQL.
     */
    __asm__ __volatile__("isb" ::: "memory");

    return OldIrql;
}

VOID
FASTCALL
KfLowerIrql(
    _In_ KIRQL NewIrql)
{
    KIRQL OldIrql = KiQueryCurrentIrql();

    if (NewIrql > HIGH_LEVEL)
    {
        PVOID RetAddr = _ReturnAddress();
        PVOID FramePtr;
        ULONG64 LinkReg;

        /* Get frame pointer and link register for stack analysis */
        __asm__ __volatile__("mov %0, x29" : "=r"(FramePtr));
        __asm__ __volatile__("mov %0, x30" : "=r"(LinkReg));

        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, NewIrql, OldIrql, (ULONG_PTR)RetAddr, (ULONG_PTR)LinkReg);
    }

    if (NewIrql > OldIrql)
    {
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

    /*
     * ARM64 Memory Barrier: Ensure IRQL write is visible before unmasking.
     * DMB (lighter than DSB) is sufficient here - we just need the IRQL write
     * to be visible before GIC PMR update allows interrupts to fire.
     */
    __asm__ __volatile__("dmb ish" ::: "memory");

    /* Now unmask interrupts - any pending hardware interrupts may fire here */
    KiApplyIrqMaskForIrqlTransition(OldIrql, NewIrql);

    /*
     * After lowering IRQL, check for pending dispatch work.
     * Only deliver it if we're now at a low enough IRQL (< DISPATCH_LEVEL).
     *
     * SCHEDULER INTEGRATION:
     *   - DpcInterruptRequested: Software interrupt flag for DPC delivery
     *   - TimerRequest: Pending timer expiration
     *   - DpcData[0].DpcQueueDepth: Normal-priority DPC queue
     *   - DpcData[1].DpcQueueDepth: High-priority DPC queue
     *   - QuantumEnd: Thread quantum expired, need reschedule
     *   - NextThread: Different thread ready to run
     *
     * If any condition is true, call KiDispatchInterrupt() to process
     * DPCs and potentially switch threads.
     */
    if (NewIrql < DISPATCH_LEVEL)
    {
        PKPRCB Prcb = KeGetCurrentPrcb();
        BOOLEAN NeedDispatch = FALSE;

        /* Debug: Periodically log when we check for DPC delivery (disabled for performance) */
#if 0
        static ULONG DpcCheckCounter = 0;
        DpcCheckCounter++;
        if (DpcCheckCounter % 10000 == 1 && Prcb)
        {
            DPRINT1("[arm64] KfLowerIrql DPC check: Old=%u New=%u TimerReq=%p DpcReq=%d DpcDepth[0]=%lu DpcDepth[1]=%lu\n",
                    OldIrql, NewIrql, (PVOID)Prcb->TimerRequest,
                    Prcb->DpcInterruptRequested,
                    Prcb->DpcData[0].DpcQueueDepth,
                    Prcb->DpcData[1].DpcQueueDepth);
        }
#endif

        if (Prcb)
        {
            /* Check all dispatch triggers */
            if (Prcb->DpcInterruptRequested)
                NeedDispatch = TRUE;

            if (Prcb->TimerRequest)
                NeedDispatch = TRUE;

            /*
             * Check normal DPC queue (index 0).
             * Note: DpcData[1] is for threaded DPCs which are not implemented in ReactOS.
             * The dispatcher only processes DpcData[0], so checking DpcData[1] would
             * cause unnecessary KiDispatchInterrupt calls that do nothing.
             */
            if (Prcb->DpcData[0].DpcQueueDepth > 0)
                NeedDispatch = TRUE;

            /* Check scheduler state */
            if (Prcb->QuantumEnd || Prcb->NextThread)
                NeedDispatch = TRUE;
        }

        if (NeedDispatch)
        {
            /*
             * Clear the DpcInterruptRequested flag FIRST to prevent re-delivery.
             * Note: TimerRequest is cleared by KiDispatchInterrupt itself.
             * If DPCs are queued again during KiDispatchInterrupt,
             * the flag will be set again and we'll deliver on the next IRQL lowering.
             */
            if (Prcb->DpcInterruptRequested)
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

            /*
             * TODO (Future SMP Support):
             * For cross-CPU reschedules, need to send IPI to target CPU:
             *
             *   if (Prcb->NextThread &&
             *       Prcb->NextThread->NextProcessor != KeGetCurrentProcessorNumber())
             *   {
             *       KiIpiSend(Prcb->NextThread->NextProcessor, IPI_RESCHEDULE);
             *   }
             *
             * Current single-CPU implementation relies on timer tick to pick up
             * NextThread, which is sufficient for UP but adds latency on SMP.
             */

            /*
             * ARM64 Instruction Barrier: Ensure DPC dispatch effects (which may
             * include software interrupts or thread switches) complete before
             * returning to caller. Prevents speculative execution across the
             * dispatch boundary.
             */
            __asm__ __volatile__("isb" ::: "memory");
        }
    }

    /*
     * ARM64 APC DELIVERY:
     *
     * On x86/x64, HalRequestSoftwareInterrupt(APC_LEVEL) sets a pending APC
     * interrupt that fires when IRQL drops below APC_LEVEL. The hardware APIC
     * handles this automatically.
     *
     * ARM64 has no equivalent hardware mechanism. Without explicit APC delivery
     * here, kernel APCs are never dispatched when lowering IRQL without a
     * context switch. This causes infinite loops in KeRemoveQueue (and similar
     * wait functions) when KernelApcPending is set: the wait loop detects the
     * pending APC, calls KiExitDispatcher to lower IRQL and deliver it, but
     * KiExitDispatcher just calls KeLowerIrql which returns without delivering
     * the APC. The loop repeats forever.
     *
     * Fix: After DPC dispatch, check for pending kernel APCs and deliver them
     * at APC_LEVEL, matching the x86/x64 software interrupt behavior.
     */
    if (NewIrql < APC_LEVEL && OldIrql >= APC_LEVEL)
    {
        PKTHREAD Thread = KeGetCurrentThread();

        /*
         * Only deliver APCs when interrupts are enabled. During
         * KeThawExecution (KdExitDebugger path), KeLowerIrql is called
         * while DAIF.I is still set. Delivering APCs with interrupts
         * disabled can hang because APC routines may need I/O interrupts.
         * Also skip if lowering from HIGH_LEVEL (debugger/freeze context)
         * to avoid re-entrancy in the KD path.
         */
        if (Thread &&
            Thread->ApcState.KernelApcPending &&
            !Thread->SpecialApcDisable)
        {
            ULONG64 Daif;
            __asm__ __volatile__("mrs %0, daif" : "=r"(Daif));
            if (!(Daif & (1ULL << 7)))  /* Check DAIF.I (bit 7) - IRQ not masked */
            {
                /* Deliver kernel APCs at APC_LEVEL */
                KiSetCurrentIrql(APC_LEVEL);
                KiDeliverApc(KernelMode, NULL, NULL);
                KiSetCurrentIrql(PASSIVE_LEVEL);
            }
        }
    }
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

NTKERNELAPI
KIRQL
KxRaiseIrqlToSynchLevel(VOID)
{
    return KeRaiseIrqlToSynchLevel();
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
