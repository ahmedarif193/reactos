/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC system service dispatch
 *
 * User stubs execute "li r0, Service; sc; blr": the first eight arguments
 * are in r3-r10, the rest in the caller's parameter area at 56(r1).
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* Keep in sync with KiPpcInvokeSystemService; reject, never truncate. */
#define KI_PPC_MAX_SERVICE_ARGUMENTS 32
#define KI_PPC_STACK_ARGUMENT_OFFSET 56
ULONG_PTR NTAPI KiPpcInvokeSystemService(PVOID Routine, PULONG_PTR Arguments);
NTSTATUS NTAPI PsConvertToGuiThread(VOID);
DECLSPEC_NORETURN VOID NTAPI KiPpcReturnToUser(_Inout_ PKTRAP_FRAME Frame);

static
ULONG_PTR
KiPpcUserDebugService(_Inout_ PKTRAP_FRAME Frame)
{
    BOOLEAN Handled;

    switch (Frame->Context.Gpr3)
    {
        case BREAKPOINT_PRINT:
            return (LONG_PTR)KdpPrint((ULONG)Frame->Context.Gpr6, (ULONG)Frame->Context.Gpr7, (PCHAR)Frame->Context.Gpr4, (USHORT)Frame->Context.Gpr5, UserMode, Frame, NULL, &Handled);
        case BREAKPOINT_PROMPT:
            return KdpPrompt((PCHAR)Frame->Context.Gpr4, (USHORT)Frame->Context.Gpr5, (PCHAR)Frame->Context.Gpr6, (USHORT)Frame->Context.Gpr7, UserMode, Frame, NULL);
        case BREAKPOINT_LOAD_SYMBOLS:
        case BREAKPOINT_UNLOAD_SYMBOLS:
        case BREAKPOINT_COMMAND_STRING:
            return STATUS_SUCCESS;
        default:
            return (LONG_PTR)STATUS_INVALID_PARAMETER;
    }
}

/* 32-bit NT marks a GUI thread by pointing its service table at the shadow
 * table (PsConvertToGuiThread). */
FORCEINLINE
BOOLEAN
KiPpcIsGuiThread(_In_ PKTHREAD Thread)
{
    return Thread->ServiceTable != (PVOID)KeServiceDescriptorTable;
}

static
DECLSPEC_NOINLINE
ULONG_PTR
KiPpcDispatchSystemService(_Inout_ PKTRAP_FRAME Frame)
{
    ULONG Service = Frame->Context.Gpr0;
    ULONG Table = (Service >> TABLE_OFFSET_BITS) & ((1 << TABLE_NUMBER_BITS) - 1);
    ULONG Index = Service & SERVICE_NUMBER_MASK;
    PKTHREAD Thread = KeGetCurrentThread();
    PKSERVICE_TABLE_DESCRIPTOR Descriptor;
    ULONG_PTR Arguments[KI_PPC_MAX_SERVICE_ARGUMENTS] = {0};
    PULONG Gpr = &Frame->Context.Gpr0;
    ULONG Count, Argument;
    NTSTATUS Status;

    if (Service == PPC_DEBUG_SERVICE_CALL)
        return KiPpcUserDebugService(Frame);

    if ((Table >= SSDT_MAX_ENTRIES) || (Table && !KiPpcIsGuiThread(Thread)))
        return (LONG_PTR)STATUS_INVALID_SYSTEM_SERVICE;
    Descriptor = &((PKSERVICE_TABLE_DESCRIPTOR)Thread->ServiceTable)[Table];
    if (!Descriptor->Base || !Descriptor->Number || (Index >= Descriptor->Limit))
        return (LONG_PTR)STATUS_INVALID_SYSTEM_SERVICE;
    Count = Descriptor->Number[Index];
    if ((Count % sizeof(ULONG_PTR)) || (Count / sizeof(ULONG_PTR) > RTL_NUMBER_OF(Arguments)) || (Descriptor->Base[Index] < (ULONG_PTR)MmSystemRangeStart))
        return (LONG_PTR)STATUS_INVALID_SYSTEM_SERVICE;
    Count /= sizeof(ULONG_PTR);

    for (Argument = 0; (Argument < Count) && (Argument < 8); ++Argument)
        Arguments[Argument] = Gpr[3 + Argument];
    if (Count > 8)
    {
        Status = KiPpcCopyFromUser(&Arguments[8], (PVOID)(Frame->Context.Gpr1 + KI_PPC_STACK_ARGUMENT_OFFSET), (Count - 8) * sizeof(ULONG_PTR));
        if (!NT_SUCCESS(Status))
            return (LONG_PTR)Status;
    }

    return KiPpcInvokeSystemService((PVOID)Descriptor->Base[Index], Arguments);
}

DECLSPEC_NORETURN
VOID
NTAPI
KiPpcSystemServiceDispatch(_Inout_ PKTRAP_FRAME Frame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG Service = Frame->Context.Gpr0;
    ULONG_PTR Result;
    NTSTATUS Status;

    ASSERT(KiUserTrap(Frame));
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    Thread->PreviousMode = UserMode;
    KeGetCurrentPrcb()->KeSystemCalls++;
    _enable();

    if (((Service >> TABLE_OFFSET_BITS) == WIN32K_SERVICE_INDEX) && !KiPpcIsGuiThread(Thread) && KeServiceDescriptorTableShadow[WIN32K_SERVICE_INDEX].Base && PspW32ProcessCallout && PspW32ThreadCallout)
    {
        Status = PsConvertToGuiThread();
        /* The conversion moves the kernel stack with this frame on it. */
        Frame = ((PKTRAP_FRAME)Thread->InitialStack) - 1;
        if (!NT_SUCCESS(Status) && (Status != STATUS_ALREADY_WIN32))
        {
            Result = (LONG_PTR)Status;
            goto Exit;
        }
    }
    if (((Service >> TABLE_OFFSET_BITS) == WIN32K_SERVICE_INDEX) && KiPpcIsGuiThread(Thread) && Thread->Teb && KeGdiFlushUserBatch)
    {
        ULONG GdiBatchCount = 0;

        Status = KiPpcCopyFromUser(&GdiBatchCount, (PUCHAR)Thread->Teb + FIELD_OFFSET(TEB, GdiBatchCount), sizeof(GdiBatchCount));
        if (NT_SUCCESS(Status) && GdiBatchCount)
            KeGdiFlushUserBatch();
    }
    Result = KiPpcDispatchSystemService(Frame);

Exit:
    /* The service frame sits right below the initial stack. */
    Frame = ((PKTRAP_FRAME)Thread->InitialStack) - 1;
    Frame->Context.Gpr3 = Result;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        KeBugCheckEx(IRQL_GT_ZERO_AT_SYSTEM_SERVICE, Service, KeGetCurrentIrql(), 0, 0);
    if ((Thread->ApcStateIndex != OriginalApcEnvironment) || Thread->CombinedApcDisable)
        KeBugCheckEx(APC_INDEX_MISMATCH, Service, Thread->ApcStateIndex, Thread->CombinedApcDisable, 0);
    KiPpcReturnToUser(Frame);
}
