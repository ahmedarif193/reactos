/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Interrupt request level management for ARM64
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

BOOLEAN KiHalInitialized = FALSE;

/* Capture the macro implementation before defining the exported function. */
FORCEINLINE
KIRQL
KiQueryCurrentIrql(VOID)
{
    return KeGetCurrentIrql();
}

#undef KeLowerIrql
#undef KeRaiseIrql
#undef KeGetCurrentIrql

VOID
NTAPI
KiRestoreTrapFrameIrql(
    _In_ KIRQL Irql)
{
    /* Called from the KiTrapReturn assembly tail. */
    KiSetIrqlAndPriorityMask(Irql);
}

FORCEINLINE
ULONG
KiQueryCurrentProcessorNumber(VOID)
{
    ULONG Number;

    if (KeGetPcr() == NULL)
    {
        return 0;
    }

    __asm__ __volatile__("ldr %w0, [x18, #" ARM64_KPCR_STRINGIFY(ARM64_KPCR_PRCB_NUMBER) "]"
                         : "=r"(Number) :: "memory");
    return Number;
}

ULONG
NTAPI
KeGetCurrentProcessorNumber(VOID)
{
    return KiQueryCurrentProcessorNumber();
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
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, NewIrql, OldIrql, 0, 0);
    }

    if (NewIrql <= OldIrql)
    {
        return OldIrql;
    }

    /*
     * Lazy IRQL: raising deliberately leaves the GIC priority mask alone.
     * The HAL defers any interrupt that arrives below the new level and
     * reconciles it on the way back down, from HalSetGicPriorityMask().
     */
    KiSetCurrentIrql(NewIrql);

    return OldIrql;
}

/* Synchronous APC delivery here requires IRQ delivery to be available. */
static
BOOLEAN
KiShouldDeliverApcOnLower(
    _In_ KIRQL NewIrql)
{
    ULONG64 Daif;
    PKTHREAD Thread;

    if (NewIrql >= APC_LEVEL)
    {
        return FALSE;
    }

    __asm__ __volatile__("mrs %0, daif" : "=r"(Daif));
    if (Daif & ARM64_PSTATE_IRQ_MASK)
    {
        return FALSE;
    }

    Thread = KeGetCurrentThread();
    return ((Thread != NULL) &&
            KiIsKernelApcDeliverable(Thread, NewIrql));
}

VOID
FASTCALL
KfLowerIrql(
    _In_ KIRQL NewIrql)
{
    KIRQL OldIrql = KiQueryCurrentIrql();

    if (NewIrql > OldIrql)
    {
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, NewIrql, OldIrql, (ULONG_PTR)_ReturnAddress(), 0);
    }

    if (NewIrql == OldIrql)
    {
        return;
    }

    KiSetIrqlAndPriorityMask(NewIrql);

    if (KiShouldDeliverApcOnLower(NewIrql))
    {
        KiSetIrqlAndPriorityMask(APC_LEVEL);
        KiDeliverApc(KernelMode, NULL, NULL);
        KiSetIrqlAndPriorityMask(NewIrql);
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
