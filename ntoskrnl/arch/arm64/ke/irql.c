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
#define NDEBUG
#include <debug.h>

extern PKIPCR KeArm64CurrentPcr;
extern KIRQL KeArm64CurrentIrql;

#undef KeLowerIrql
#undef KeRaiseIrql
#undef KeGetCurrentIrql

#define ARM64_MASK_IRQ()   __asm__ __volatile__("msr daifset, #0x2" ::: "memory")
#define ARM64_UNMASK_IRQ() __asm__ __volatile__("msr daifclr, #0x2" ::: "memory")
#define ARM64_MASK_ALL()   __asm__ __volatile__("msr daifset, #0xf" ::: "memory")
/*
 * Don't unmask SError (bit 2 in immediate = A) during IRQL transitions.
 * SErrors from UEFI/FreeLdr can be stale and we want them to stay pended.
 * Unmask only D, I, F (bits 3, 1, 0 = 0xB).
 */
#define ARM64_UNMASK_ALL() __asm__ __volatile__("msr daifclr, #0xb" ::: "memory")
#define ARM64_SYNC_BARRIER()                                                     \
    do                                                                           \
    {                                                                            \
        __asm__ __volatile__("dsb sy" ::: "memory");                            \
        __asm__ __volatile__("isb" ::: "memory");                               \
    } while (0)

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
 * Re-entrancy guard for HIGH_LEVEL->lower IRQL transitions.
 *
 * Problem: When lowering from HIGH_LEVEL, we unmask interrupts. If any code
 * called during the transition (e.g., DPC dispatch, debug prints) raises to
 * HIGH_LEVEL and then lowers again, we would re-enter the unmask path,
 * causing an interrupt storm as pending interrupts fire repeatedly.
 *
 * Solution: Use a per-CPU flag (in PRCB) to detect re-entrancy. If we're already
 * in the process of unmasking from HIGH_LEVEL, skip the unmask on nested
 * transitions. The outer transition will complete the unmask.
 *
 * CRITICAL SMP FIX: The flag is now stored in the PRCB (Per-Processor Control Block)
 * instead of a global variable. This ensures each CPU has its own independent flag,
 * preventing false re-entrancy detection when multiple CPUs lower IRQL simultaneously.
 */

VOID
KiApplyIrqMaskForIrqlTransition(
    _In_ KIRQL OldIrql,
    _In_ KIRQL NewIrql)
{
    if (NewIrql > OldIrql)
    {
        if ((OldIrql < HIGH_LEVEL) && (NewIrql >= HIGH_LEVEL))
        {
            ARM64_MASK_ALL();
            ARM64_SYNC_BARRIER();
        }
        else if ((OldIrql < DISPATCH_LEVEL) && (NewIrql >= DISPATCH_LEVEL))
        {
            ARM64_MASK_IRQ();
            ARM64_SYNC_BARRIER();
        }
    }
    else if (NewIrql < OldIrql)
    {
        if ((OldIrql >= HIGH_LEVEL) && (NewIrql < HIGH_LEVEL))
        {
            PKPRCB Prcb = KeGetCurrentPrcb();

            /*
             * Check for re-entrancy using the per-CPU flag in PRCB.
             * If we're already in a HIGH_LEVEL->lower transition on THIS CPU,
             * skip the unmask. The outer transition will handle it.
             *
             * This fixes the SMP bug where a global flag would cause false
             * re-entrancy detection when multiple CPUs lower IRQL concurrently.
             */
            if (Prcb && InterlockedCompareExchange(&Prcb->InHighLevelTransition, 1, 0) != 0)
            {
                /* Re-entrancy detected on this CPU - skip unmask, just mask if needed */
                if (NewIrql >= DISPATCH_LEVEL)
                {
                    ARM64_MASK_IRQ();
                    ARM64_SYNC_BARRIER();
                }
                return;
            }

            /* Not re-entrant - perform the full unmask sequence */
            ARM64_UNMASK_ALL();
            ARM64_SYNC_BARRIER();

            if (NewIrql >= DISPATCH_LEVEL)
            {
                ARM64_MASK_IRQ();
                ARM64_SYNC_BARRIER();
            }

            /* Clear per-CPU re-entrancy flag */
            if (Prcb)
            {
                InterlockedExchange(&Prcb->InHighLevelTransition, 0);
            }
        }
        else if ((OldIrql >= DISPATCH_LEVEL) && (NewIrql < DISPATCH_LEVEL))
        {
            ARM64_UNMASK_IRQ();
            ARM64_SYNC_BARRIER();
        }
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

    /*
     * ARM64 FIX: Validate NewIrql before any comparison.
     *
     * Valid IRQL values are 0-15 (PASSIVE_LEVEL to HIGH_LEVEL).
     * Any value > HIGH_LEVEL is garbage, likely from:
     * 1. Stack corruption
     * 2. Register clobbering during interrupt handling
     * 3. Use-after-free of a structure containing saved IRQL
     *
     * The value 0xC0 (192) is particularly suspicious as it matches the ARM64
     * DAIF register value (D+A bits set), suggesting a calling convention
     * or register usage issue.
     *
     * When garbage IRQL is detected, clamp to OldIrql (no-op) to prevent
     * further damage, but log extensively for debugging.
     */
    if (NewIrql > HIGH_LEVEL)
    {
        static volatile LONG GarbageIrqlWarnBudget = 8;
        PVOID RetAddr = _ReturnAddress();
        PVOID FramePtr;
        ULONG64 LinkReg;

        /* Get frame pointer and link register for stack analysis */
        __asm__ __volatile__("mov %0, x29" : "=r"(FramePtr));
        __asm__ __volatile__("mov %0, x30" : "=r"(LinkReg));

        if (InterlockedDecrement(&GarbageIrqlWarnBudget) >= 0)
        {
            DPRINT1("[arm64] KfLowerIrql: GARBAGE NewIrql=%lu (0x%02X) > HIGH_LEVEL! OldIrql=%lu\n",
                    (ULONG)NewIrql, (ULONG)NewIrql, (ULONG)OldIrql);
            DPRINT1("[arm64] KfLowerIrql: Caller=%p FP=%p LR=%p\n",
                    RetAddr, FramePtr, (PVOID)LinkReg);
            DPRINT1("[arm64] KfLowerIrql: Thread=%p CurrentIrql(PCR)=%lu\n",
                    KeGetCurrentThread(),
                    (ULONG)KiQueryCurrentIrql());

            /*
             * Additional validation: Check if return address looks valid.
             * Valid kernel addresses on ARM64 are in the FFFF8000... range.
             * If the return address looks corrupted, bugcheck now to capture
             * better state rather than returning to garbage code.
             */
            if ((ULONG_PTR)RetAddr < 0xFFFF000000000000ULL)
            {
                DPRINT1("[arm64] KfLowerIrql: FATAL - Caller=%p is NOT a valid kernel address!\n",
                        RetAddr);
                KeBugCheckEx(KERNEL_STACK_INPAGE_ERROR,
                             (ULONG_PTR)NewIrql,
                             (ULONG_PTR)RetAddr,
                             (ULONG_PTR)LinkReg,
                             0xA64BAD1);
            }
        }
        /*
         * Clamp to OldIrql effectively making this a no-op.
         * The caller has garbage IRQL saved, but we maintain current state.
         * This prevents cascading failures while still allowing debugging.
         */
        NewIrql = OldIrql;
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
