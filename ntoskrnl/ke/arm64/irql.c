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
        PKPRCB RaisePrcb = KeGetCurrentPrcb();
        ULONG64 RaiseDaif;
        /* Capture the pre-raise DAIF so the matching lower restores it instead of blindly unmasking */
        __asm__ __volatile__("mrs %0, daif" : "=r"(RaiseDaif));
        if (RaisePrcb) RaisePrcb->HighLevelSavedDaif = RaiseDaif;
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
            /* Restore the DAIF captured by the matching raise (keeps a caller's _disable intact); SError stays masked */
            __asm__ __volatile__("msr daif, %0" :: "r"(Prcb->HighLevelSavedDaif | 0x100ULL) : "memory");
            __asm__ __volatile__("isb" ::: "memory");
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
     * That only applies to DAIF.I state owned by IRQL masking itself: the
     * HIGH_LEVEL drop path above clears it after programming the GIC PMR.
     * Normal non-HIGH_LEVEL transitions must not undo a caller's explicit
     * _disable(); those callers reopen the interrupt window themselves.
     */
    NeedsPmrUpdate = KiIrqlTransitionNeedsGicPmrUpdate(OldIrql, NewIrql);
    if (NeedsPmrUpdate)
    {
        HalSetGicPriorityMask(NewIrql);
        ARM64_SYNC_BARRIER();
    }

    /*
     * Enforce SError policy on transitions that actually touch the GIC PMR.
     * DAIF.I is left unchanged here; non-HIGH_LEVEL IRQL is represented by PMR.
     */
    if (NewIrql < OldIrql)
    {
        if (NeedsPmrUpdate)
        {
            KiUpdateSerrorMaskOnlyForIrql(NewIrql);
            ARM64_SYNC_BARRIER();
        }
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

    {
        ULONG64 Daif;
        __asm__ __volatile__("mrs %0, daif" : "=r"(Daif));
        if (NewIrql < HIGH_LEVEL)
            __asm__ __volatile__("msr daifset, #0x2\n\tisb" ::: "memory");
        KiApplyIrqMaskForIrqlTransition(OldIrql, NewIrql);
        KiSetCurrentIrql(NewIrql);
        if ((NewIrql < HIGH_LEVEL) && !(Daif & 0x80))
            __asm__ __volatile__("msr daifclr, #0x2" ::: "memory");
    }

    /*
     * ARM64 Instruction Barrier: Ensure IRQL change and GIC PMR update take
     * effect before continuing. Prevents speculative execution from reading
     * stale IRQL values or executing code that should be protected by the new IRQL.
     */
    __asm__ __volatile__("isb" ::: "memory");

    return OldIrql;
}

VOID
KiArm64ProcessPendingSoftwareInterrupts(
    _In_ KIRQL OldIrql,
    _In_ KIRQL NewIrql)
{
    /* Synchronous DPC/APC software-interrupt emulation; callers guarantee DAIF.I is clear and CurrentIrql == NewIrql */
    if (NewIrql < DISPATCH_LEVEL)
    {
        PKPRCB Prcb = KeGetCurrentPrcb();
        BOOLEAN NeedDispatch = FALSE;

        if (Prcb)
        {
            if (Prcb->DpcInterruptRequested) NeedDispatch = TRUE;
            if (Prcb->TimerRequest) NeedDispatch = TRUE;
            /* DpcData[1] is for threaded DPCs, which ReactOS never dispatches */
            if (Prcb->DpcData[0].DpcQueueDepth > 0) NeedDispatch = TRUE;
            if (Prcb->QuantumEnd || Prcb->NextThread) NeedDispatch = TRUE;
        }

        if (NeedDispatch)
        {
            /* Clear the request first; TimerRequest is consumed by KiDispatchInterrupt, which raises to DISPATCH_LEVEL internally */
            if (Prcb->DpcInterruptRequested) Prcb->DpcInterruptRequested = FALSE;
            KiDispatchInterrupt();
            __asm__ __volatile__("isb" ::: "memory");
        }
    }

    /* Kernel APC delivery when dropping below APC_LEVEL, matching the x86/x64 software interrupt */
    if (NewIrql < APC_LEVEL && OldIrql >= APC_LEVEL)
    {
        PKTHREAD Thread = KeGetCurrentThread();

        static volatile LONG ApcTraceBusy = 0;
        if (Thread && Thread->ApcState.KernelApcPending && InterlockedCompareExchange((PLONG)&ApcTraceBusy, 1, 0) == 0)
        {
            DbgPrint("APCX old=%u new=%u sad=%d kad=%d thr=%p\n", OldIrql, NewIrql, Thread->SpecialApcDisable, Thread->KernelApcDisable, Thread);
            InterlockedExchange((PLONG)&ApcTraceBusy, 0);
        }
        if (Thread && Thread->ApcState.KernelApcPending && !Thread->SpecialApcDisable)
        {
            KiSetCurrentIrql(APC_LEVEL);
            KiDeliverApc(KernelMode, NULL, NULL);
            KiSetCurrentIrql(NewIrql);
        }
    }
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

    /* Publish the new IRQL before the hardware mask opens so an immediate interrupt sees a consistent level */
    KiSetCurrentIrql(NewIrql);

    __asm__ __volatile__("dmb ish" ::: "memory");

    KiApplyIrqMaskForIrqlTransition(OldIrql, NewIrql);

    {
        ULONG64 Daif;
        PKTHREAD GateThread = KeGetCurrentThread();
        static volatile LONG SkipTraceBusy = 0;
        __asm__ __volatile__("mrs %0, daif" : "=r"(Daif));
        if ((Daif & 0x80) && GateThread && GateThread->ApcState.KernelApcPending && NewIrql < APC_LEVEL && OldIrql >= APC_LEVEL && InterlockedCompareExchange((PLONG)&SkipTraceBusy, 1, 0) == 0)
        {
            DbgPrint("APCSKIP old=%u new=%u thr=%p\n", OldIrql, NewIrql, GateThread);
            InterlockedExchange((PLONG)&SkipTraceBusy, 0);
        }
        /* With DAIF.I still masked (explicit _disable or KD freeze/thaw), leave work pended; the next interrupt tail or unmasked lowering drains it */
        if (!(Daif & 0x80)) KiArm64ProcessPendingSoftwareInterrupts(OldIrql, NewIrql);
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
