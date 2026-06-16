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
    PKIPCR Pcr = KeGetPcr();
    if (Pcr != NULL)
    {
        return Pcr->CurrentIrql;
    }

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
    ULONG64 Daif;

    if (NewIrql > HIGH_LEVEL)
    {
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, NewIrql, OldIrql, 0, 0);
    }

    if (NewIrql <= OldIrql)
    {
        return OldIrql;
    }

    __asm__ __volatile__("mrs %0, daif" : "=r"(Daif));
    __asm__ __volatile__("msr daifset, #2" ::: "memory");

    KiSetCurrentIrql(NewIrql);

    if (KiHalInitialized)
    {
        HalRaiseGicPriorityMask(NewIrql);
    }

    KiRestoreInterruptFlag(Daif);

    return OldIrql;
}

VOID
FASTCALL
KfLowerIrql(
    _In_ KIRQL NewIrql)
{
    KIRQL OldIrql = KiQueryCurrentIrql();
    BOOLEAN DeliverApc = FALSE;

    if (NewIrql > OldIrql)
    {
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL,
                     NewIrql,
                     OldIrql,
                     (ULONG_PTR)_ReturnAddress(),
                     0);
    }

    if (NewIrql == OldIrql)
    {
        return;
    }

    if ((OldIrql >= APC_LEVEL) && (NewIrql < APC_LEVEL))
    {
        ULONG64 Daif;
        PKTHREAD Thread;

        __asm__ __volatile__("mrs %0, daif" : "=r"(Daif));
        if (!(Daif & ARM64_DAIF_IRQ_MASK))
        {
            Thread = KeGetCurrentThread();
            if ((Thread != NULL) &&
                (Thread->ApcState.KernelApcPending) &&
                !(Thread->SpecialApcDisable))
            {
                DeliverApc = TRUE;
            }
        }
    }

    if (DeliverApc)
    {
        if (OldIrql != APC_LEVEL)
        {
            KiSetCurrentIrqlAndGicMask(APC_LEVEL);
        }

        KiDeliverApc(KernelMode, NULL, NULL);
    }

    KiSetCurrentIrqlAndGicMask(NewIrql);
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
