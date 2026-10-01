/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC processor coordination (uniprocessor),
 *              floating-point state and processor identification
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

VOID
FASTCALL
KiIpiSend(KAFFINITY Targets, ULONG Request)
{
    /* Only the boot processor runs. */
    UNREFERENCED_PARAMETER(Targets);
    UNREFERENCED_PARAMETER(Request);
}

BOOLEAN
NTAPI
KiIpiServiceRoutine(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPRCB Prcb = KeGetCurrentPrcb();

    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    InterlockedBitTestAndReset(&Prcb->RequestSummary, IPI_FREEZE);
    if (InterlockedBitTestAndReset(&Prcb->RequestSummary, IPI_APC))
        HalRequestSoftwareInterrupt(APC_LEVEL);
    if (InterlockedBitTestAndReset(&Prcb->RequestSummary, IPI_DPC))
    {
        Prcb->DpcInterruptRequested = TRUE;
        HalRequestSoftwareInterrupt(DISPATCH_LEVEL);
    }
    return TRUE;
}

VOID
NTAPI
KiIpiProcessRequests(VOID)
{
    KiIpiServiceRoutine(NULL, NULL);
}

ULONG_PTR
NTAPI
KeIpiGenericCall(PKIPI_BROADCAST_WORKER Function, ULONG_PTR Argument)
{
    KIRQL OldIrql;
    ULONG_PTR Result;

    ASSERT(Function != NULL);
    OldIrql = KfRaiseIrql(IPI_LEVEL);
    Result = Function(Argument);
    KfLowerIrql(OldIrql);
    return Result;
}

CODE_SEG("INIT")
VOID
NTAPI
KeStartAllProcessors(VOID)
{
    /* Secondary processors are not started. */
}

BOOLEAN
KiProcessorFreezeHandler(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    return FALSE;
}

VOID
NTAPI
KiFreezeIfRequested(VOID)
{
}

VOID
NTAPI
KxFreezeExecution(VOID)
{
    KeGetCurrentPrcb()->IpiFrozen = IPI_FROZEN_STATE_OWNER | IPI_FROZEN_FLAG_ACTIVE;
}

VOID
NTAPI
KxThawExecution(VOID)
{
    KeGetCurrentPrcb()->IpiFrozen = IPI_FROZEN_STATE_RUNNING;
}

KCONTINUE_STATUS
NTAPI
KxSwitchKdProcessor(_In_ ULONG ProcessorIndex)
{
    UNREFERENCED_PARAMETER(ProcessorIndex);
    return ContinueSuccess;
}

/* Every thread owns the FP registers in its trap and switch frames, so a
 * kernel user only has to preserve the volatile registers it touches. */
NTSTATUS
NTAPI
KeSaveFloatingPointState(_Out_ PKFLOATING_SAVE FloatSave)
{
    ULONGLONG Fpscr;

    __asm__ __volatile__("mffs 0\n\tstfd 0, 0(%0)" :: "r"(&Fpscr) : "fr0", "memory");
    FloatSave->Fpscr = *(double *)&Fpscr;
    FloatSave->Irql = KeGetCurrentIrql();
    FloatSave->Owner = KeGetCurrentThread();
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KeRestoreFloatingPointState(_In_ PKFLOATING_SAVE FloatSave)
{
    __asm__ __volatile__("lfd 0, 0(%0)\n\tmtfsf 0xFF, 0" :: "r"(&FloatSave->Fpscr) : "fr0", "memory");
    return STATUS_SUCCESS;
}

VOID
NTAPI
KiPpcIdentifyProcessor(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    extern ULONG KiPpcDcacheLineSize, KiPpcIcacheLineSize;
    PPPC_LOADER_BLOCK PpcBlock = &LoaderBlock->u.PowerPC;
    ULONG Pvr;

    __asm__ __volatile__("mfpvr %0" : "=r"(Pvr));
    KeProcessorArchitecture = PROCESSOR_ARCHITECTURE_PPC;
    KeProcessorLevel = (USHORT)(Pvr >> 16);
    KeProcessorRevision = (USHORT)Pvr;
    KeFeatureBits = 0;
    if (PpcBlock->DcacheLineSize)
        KiPpcDcacheLineSize = PpcBlock->DcacheLineSize;
    if (PpcBlock->IcacheLineSize)
        KiPpcIcacheLineSize = PpcBlock->IcacheLineSize;
    KeGetCurrentPrcb()->MHz = PpcBlock->ProcessorFrequency / 1000000;
}
