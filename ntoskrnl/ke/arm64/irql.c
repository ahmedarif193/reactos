/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Interrupt request level management for ARM64
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern KIRQL KeArm64CurrentIrql;

BOOLEAN KiHalInitialized = FALSE;

#undef KeLowerIrql
#undef KeRaiseIrql
#undef KeGetCurrentIrql

FORCEINLINE
KIRQL
KiQueryCurrentIrql(VOID)
{
    ULONG_PTR Pcr = (ULONG_PTR)KeGetPcr();
    ULONG Irql;

    if (Pcr != 0)
    {
        __asm__ __volatile__("ldrb %w0, [x18, #" ARM64_KPCR_STRINGIFY(ARM64_KPCR_CURRENT_IRQL) "]"
                             : "=r"(Irql) :: "memory");
        return (KIRQL)Irql;
    }

    return KeArm64CurrentIrql;
}

VOID
KiSetCurrentIrql(
    _In_ KIRQL Irql)
{
    ULONG_PTR Pcr = (ULONG_PTR)KeGetPcr();
    ULONG Value = Irql;

    if (Pcr != 0)
    {
        __asm__ __volatile__("strb %w0, [x18, #" ARM64_KPCR_STRINGIFY(ARM64_KPCR_CURRENT_IRQL) "]"
                             :: "r"(Value) : "memory");
        return;
    }

    KeArm64CurrentIrql = Irql;
}

VOID
NTAPI
KiRestoreTrapFrameIrql(
    _In_ KIRQL Irql)
{
    KiSetCurrentIrql(Irql);

    if (KiHalInitialized)
    {
        HalSetGicPriorityMask(Irql);
    }
}

ULONG
NTAPI
KeGetCurrentProcessorNumber(VOID)
{
    ULONG_PTR Pcr = (ULONG_PTR)KeGetPcr();
    ULONG Number;

    if (Pcr != 0)
    {
        __asm__ __volatile__("ldr %w0, [x18, #" ARM64_KPCR_STRINGIFY(ARM64_KPCR_PRCB_NUMBER) "]"
                             : "=r"(Number) :: "memory");
        return Number;
    }

    return 0;
}

ULONG
NTAPI
KeGetCurrentProcessorNumberEx(
    _Out_opt_ PPROCESSOR_NUMBER ProcNumber)
{
    ULONG_PTR Pcr = (ULONG_PTR)KeGetPcr();
    ULONG Processor;

    if (Pcr != 0)
    {
        __asm__ __volatile__("ldr %w0, [x18, #" ARM64_KPCR_STRINGIFY(ARM64_KPCR_PRCB_NUMBER) "]"
                             : "=r"(Processor) :: "memory");
    }
    else
    {
        Processor = 0;
    }

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

    if (NewIrql > HIGH_LEVEL)
    {
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, NewIrql, OldIrql, 0, 0);
    }

    if (NewIrql <= OldIrql)
    {
        return OldIrql;
    }

    KiSetCurrentIrql(NewIrql);

    return OldIrql;
}

VOID
FASTCALL
KfLowerIrql(
    _In_ KIRQL NewIrql)
{
    KIRQL OldIrql = KiQueryCurrentIrql();

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

    KiSetCurrentIrql(NewIrql);

    if (KiHalInitialized)
    {
        HalSetGicPriorityMask(NewIrql);
    }

    if ((OldIrql >= APC_LEVEL) && (NewIrql < APC_LEVEL))
    {
        ULONG64 Daif;
        PKTHREAD Thread;

        __asm__ __volatile__("mrs %0, daif" : "=r"(Daif));
        if (!(Daif & 0x80))
        {
            Thread = KeGetCurrentThread();
            if ((Thread != NULL) &&
                (Thread->ApcState.KernelApcPending) &&
                !(Thread->SpecialApcDisable))
            {
                KiSetCurrentIrql(APC_LEVEL);
                if (KiHalInitialized)
                    HalSetGicPriorityMask(APC_LEVEL);

                KiDeliverApc(KernelMode, NULL, NULL);

                KiSetCurrentIrql(NewIrql);
                if (KiHalInitialized)
                    HalSetGicPriorityMask(NewIrql);
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
