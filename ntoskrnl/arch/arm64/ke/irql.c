/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Interrupt request level management for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

extern PKIPCR KeArm64CurrentPcr;
extern KIRQL KeArm64CurrentIrql;
VOID KiArm64BootStageLog(_In_z_ PCSTR Stage);

#undef KeLowerIrql
#undef KeRaiseIrql
#undef KeGetCurrentIrql

#define ARM64_MASK_IRQ()   __asm__ __volatile__("msr daifset, #0x2" ::: "memory")
#define ARM64_UNMASK_IRQ() __asm__ __volatile__("msr daifclr, #0x2" ::: "memory")
#define ARM64_MASK_ALL()   __asm__ __volatile__("msr daifset, #0xf" ::: "memory")
#define ARM64_UNMASK_ALL() __asm__ __volatile__("msr daifclr, #0xf" ::: "memory")
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

FORCEINLINE
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

FORCEINLINE
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
            ARM64_UNMASK_ALL();
            ARM64_SYNC_BARRIER();
            if (NewIrql >= DISPATCH_LEVEL)
            {
                ARM64_MASK_IRQ();
                ARM64_SYNC_BARRIER();
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

    if (NT_SUCCESS(RtlStringCbPrintfA(Buf,
                                      sizeof(Buf),
                                      "[arm64] KfLowerIrql: entry OldIrql=%lu NewIrql=%lu",
                                      (ULONG)OldIrql,
                                      (ULONG)NewIrql)))
    {
        KiArm64BootStageLog(Buf);
    }

    if (NewIrql > OldIrql)
    {
        if (NT_SUCCESS(RtlStringCbPrintfA(Buf,
                                          sizeof(Buf),
                                          "[arm64] KfLowerIrql: BUGCHECK OldIrql=%lu NewIrql=%lu",
                                          (ULONG)OldIrql,
                                          (ULONG)NewIrql)))
        {
            KiArm64BootStageLog(Buf);
        }

        DPRINT1("KfLowerIrql: raising IRQL via lower request (%u -> %u)\n", OldIrql, NewIrql);
        KeBugCheckEx(IRQL_NOT_GREATER_OR_EQUAL, NewIrql, OldIrql, 0, 0);
    }

    KiApplyIrqMaskForIrqlTransition(OldIrql, NewIrql);
    KiSetCurrentIrql(NewIrql);

    if (NT_SUCCESS(RtlStringCbPrintfA(Buf,
                                      sizeof(Buf),
                                      "[arm64] KfLowerIrql: exit OldIrql=%lu NewIrql=%lu",
                                      (ULONG)OldIrql,
                                      (ULONG)NewIrql)))
    {
        KiArm64BootStageLog(Buf);
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
