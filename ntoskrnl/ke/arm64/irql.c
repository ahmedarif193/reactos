/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Interrupt request level management for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern KIRQL KeArm64CurrentIrql;

BOOLEAN KiHalInitialized = FALSE;

#define ARM64_DAIF_IRQ_MASK 0x80ULL

#undef KeLowerIrql
#undef KeRaiseIrql
#undef KeGetCurrentIrql

FORCEINLINE
KIRQL
KiQueryCurrentIrql(VOID)
{
    KIRQL Irql;
    ULONG64 Daif;

    __asm__ __volatile__("mrs %0, daif\n\tmsr daifset, #3" : "=r"(Daif) :: "memory");
    {
        PKIPCR Pcr = KeGetPcr();
        Irql = (Pcr != NULL) ? Pcr->CurrentIrql : KeArm64CurrentIrql;
    }
    __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");

    return Irql;
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

    KeArm64CurrentIrql = Irql;
}

static
VOID
KiRestoreInterruptFlag(
    _In_ ULONG64 Daif)
{
    if (!(Daif & ARM64_DAIF_IRQ_MASK))
    {
        __asm__ __volatile__("msr daifclr, #2" ::: "memory");
    }
}

static
VOID
KiSetCurrentIrqlAndGicMask(
    _In_ KIRQL Irql)
{
    ULONG64 Daif;

    __asm__ __volatile__("mrs %0, daif" : "=r"(Daif));
    __asm__ __volatile__("msr daifset, #2" ::: "memory");

    KiSetCurrentIrql(Irql);

    if (KiHalInitialized)
    {
        HalSetGicPriorityMask(Irql);
    }

    KiRestoreInterruptFlag(Daif);
}

VOID
NTAPI
KiRestoreTrapFrameIrql(
    _In_ KIRQL Irql)
{
    KiSetCurrentIrqlAndGicMask(Irql);
}

ULONG
NTAPI
KeGetCurrentProcessorNumber(VOID)
{
    ULONG Number;
    ULONG64 Daif;

    /*
     * ARM64 SMP: KeGetCurrentPrcb() derives the PRCB pointer from
     * mrs tpidr_el1; the dependent Prcb->Number load is not atomic vs a
     * reschedule-SGI migration below DISPATCH_LEVEL. Callers that index a
     * per-CPU array with the result (e.g. the GIC PMR shadow in
     * HalpArm64SetPmrExact) would otherwise touch the wrong CPU's slot.
     */
    __asm__ __volatile__("mrs %0, daif\n\tmsr daifset, #3" : "=r"(Daif) :: "memory");
    {
        PKPRCB Prcb = KeGetCurrentPrcb();
        Number = Prcb ? Prcb->Number : 0;
    }
    __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");

    return Number;
}

ULONG
NTAPI
KeGetCurrentProcessorNumberEx(
    _Out_opt_ PPROCESSOR_NUMBER ProcNumber)
{
    ULONG Processor;
    ULONG64 Daif;

    __asm__ __volatile__("mrs %0, daif\n\tmsr daifset, #3" : "=r"(Daif) :: "memory");
    {
        PKPRCB Prcb = KeGetCurrentPrcb();
        Processor = Prcb ? Prcb->Number : 0;
    }
    __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");

    if (ProcNumber)
    {
        ProcNumber->Group = 0;
        ProcNumber->Number = (UCHAR)Processor;
        ProcNumber->Reserved = 0;
    }

    return Processor;
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
    KIRQL OldIrql;
    ULONG64 Daif;

    /*
     * ARM64 SMP: CurrentIrql lives in per-CPU PCR memory, reached via
     * mrs tpidr_el1 + a dependent load/store. The read of OldIrql and the
     * write of NewIrql (plus the GIC PMR update, which itself derives the
     * CPU number from tpidr_el1) must form a single migration-atomic
     * read-modify-write. The DISPATCH reschedule SGI is deliverable below
     * DISPATCH_LEVEL, so without masking IRQ/FIQ across the whole sequence a
     * thread can migrate between the read and the write, reading the old
     * CPU's IRQL and committing to a different CPU's PCR/PMR shadow. amd64
     * gets this for free because cr8 is a single per-CPU register that moves
     * with the thread; on ARM64 we must mask. Mask FIRST, before reading.
     */
    __asm__ __volatile__("mrs %0, daif\n\tmsr daifset, #3" : "=r"(Daif) :: "memory");

    OldIrql = KiQueryCurrentIrql();

    if (NewIrql > HIGH_LEVEL)
    {
        __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, NewIrql, OldIrql, 0, 0);
    }

    if (NewIrql <= OldIrql)
    {
        __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");
        return OldIrql;
    }

    KiSetCurrentIrql(NewIrql);

    if (KiHalInitialized)
    {
        HalRaiseGicPriorityMask(NewIrql);
    }

    __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");

    return OldIrql;
}

VOID
FASTCALL
KfLowerIrql(
    _In_ KIRQL NewIrql)
{
    KIRQL OldIrql;
    ULONG64 Daif;
    BOOLEAN DeliverApc = FALSE;

    /*
     * ARM64 SMP: as in KfRaiseIrql, the OldIrql read and the CurrentIrql/PMR
     * write must be one migration-atomic read-modify-write. Mask IRQ/FIQ
     * before reading so the thread cannot migrate between reading the old
     * CPU's IRQL and committing to a (possibly different) CPU's PCR. The
     * only place the mask is lifted is around KiDeliverApc, which must run
     * with interrupts enabled at APC_LEVEL after the IRQL has already been
     * coherently set on this CPU.
     */
    __asm__ __volatile__("mrs %0, daif\n\tmsr daifset, #3" : "=r"(Daif) :: "memory");

    OldIrql = KiQueryCurrentIrql();

    if (NewIrql > OldIrql)
    {
        __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL,
                     NewIrql,
                     OldIrql,
                     (ULONG_PTR)_ReturnAddress(),
                     0);
    }

    if (NewIrql == OldIrql)
    {
        __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");
        return;
    }

    /*
     * Decide on APC delivery using the IRQ-enable state the caller had on
     * entry (the saved Daif), not the masked state we are in now.
     */
    if ((OldIrql >= APC_LEVEL) && (NewIrql < APC_LEVEL) &&
        !(Daif & ARM64_DAIF_IRQ_MASK))
    {
        PKTHREAD Thread = KeGetCurrentThread();
        if ((Thread != NULL) &&
            (Thread->ApcState.KernelApcPending) &&
            !(Thread->SpecialApcDisable))
        {
            DeliverApc = TRUE;
        }
    }

    if (DeliverApc)
    {
        if (OldIrql != APC_LEVEL)
        {
            KiSetCurrentIrql(APC_LEVEL);
            if (KiHalInitialized)
            {
                HalSetGicPriorityMask(APC_LEVEL);
            }
        }

        /* APCs run at APC_LEVEL with interrupts enabled; lift the mask */
        __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");

        KiDeliverApc(KernelMode, NULL, NULL);

        /* Re-mask for the final IRQL commit */
        __asm__ __volatile__("mrs %0, daif\n\tmsr daifset, #3" : "=r"(Daif) :: "memory");
    }

    KiSetCurrentIrql(NewIrql);
    if (KiHalInitialized)
    {
        HalSetGicPriorityMask(NewIrql);
    }

    __asm__ __volatile__("msr daif, %0" :: "r"(Daif) : "memory");
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
