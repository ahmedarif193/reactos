/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/ipi.c
 * PURPOSE:         Inter-processor interrupt stubs for ARM64
 */

#include <ntoskrnl.h>

VOID
NTAPI
KiIpiGenericCallTarget(
    _In_ PKIPI_CONTEXT PacketContext,
    _In_ PVOID BroadcastFunction,
    _In_ PVOID Argument,
    _In_ PVOID Count)
{
    UNREFERENCED_PARAMETER(PacketContext);
    UNREFERENCED_PARAMETER(Count);

    if (BroadcastFunction != NULL)
    {
        ((PKIPI_BROADCAST_WORKER)BroadcastFunction)((ULONG_PTR)Argument);
    }
}

VOID
FASTCALL
KiIpiSend(
    _In_ KAFFINITY TargetSet,
    _In_ ULONG IpiRequest)
{
    UNREFERENCED_PARAMETER(TargetSet);
    UNREFERENCED_PARAMETER(IpiRequest);
}

VOID
NTAPI
KiIpiSendPacket(
    _In_ KAFFINITY TargetSet,
    _In_ PKIPI_WORKER WorkerFunction,
    _In_ PKIPI_BROADCAST_WORKER BroadcastFunction,
    _In_ ULONG_PTR Context,
    _Inout_ PULONG Count)
{
    UNREFERENCED_PARAMETER(TargetSet);
    UNREFERENCED_PARAMETER(WorkerFunction);
    UNREFERENCED_PARAMETER(BroadcastFunction);
    UNREFERENCED_PARAMETER(Context);

    if (Count != NULL)
    {
        *Count = 1;
    }
}

VOID
FASTCALL
KiIpiSignalPacketDone(
    _In_ PKIPI_CONTEXT PacketContext)
{
    UNREFERENCED_PARAMETER(PacketContext);
}

VOID
FASTCALL
KiIpiSignalPacketDoneAndStall(
    _In_ PKIPI_CONTEXT PacketContext,
    _Inout_ volatile PULONG ReverseStall)
{
    UNREFERENCED_PARAMETER(PacketContext);

    if (ReverseStall != NULL)
    {
        *ReverseStall = 0;
    }
}

BOOLEAN
NTAPI
KiIpiServiceRoutine(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PKEXCEPTION_FRAME ExceptionFrame)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    return TRUE;
}

ULONG_PTR
NTAPI
KeIpiGenericCall(
    _In_ PKIPI_BROADCAST_WORKER Function,
    _In_ ULONG_PTR Argument)
{
    if (Function != NULL)
    {
        return Function(Argument);
    }

    return 0;
}
