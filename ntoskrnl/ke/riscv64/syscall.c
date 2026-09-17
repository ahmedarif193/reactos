/*
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* ReactOS-private U-mode ecall convention: t0 service, a0-a7 arguments. */
#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* Keep in sync with the assembly invocation frame; reject, never truncate. */
#define KI_RISCV_MAX_SERVICE_ARGUMENTS 32
C_ASSERT(KI_RISCV_MAX_SERVICE_ARGUMENTS * sizeof(ULONG_PTR) == 256);
ULONG_PTR NTAPI KiRiscvInvokeSystemService(PVOID Routine, PULONG_PTR Arguments);
NTSTATUS NTAPI PsConvertToGuiThread(VOID);

static
ULONG_PTR
KiRiscvUserDebugService(_Inout_ PKTRAP_FRAME Frame)
{
    CHAR Buffer[512];
    USHORT Length = min((USHORT)Frame->Context.A2, sizeof(Buffer));
    BOOLEAN Handled;
    NTSTATUS Status;

    /* The common user KD capture still depends on SEH. Capture here before
     * taking KD locks, then pass only a bounded kernel buffer to its worker. */
    if (Frame->Context.A0 != BREAKPOINT_PRINT)
        return (LONG_PTR)STATUS_NOT_IMPLEMENTED;
    if (NtQueryDebugFilterState((ULONG)Frame->Context.A3, (ULONG)Frame->Context.A4) == (NTSTATUS)FALSE)
        return STATUS_SUCCESS;
    Status = KiRiscvCopyFromUser(Buffer, (PVOID)Frame->Context.A1, Length);
    if (!NT_SUCCESS(Status)) return (LONG_PTR)Status;
    return (LONG_PTR)KdpPrint((ULONG)Frame->Context.A3, (ULONG)Frame->Context.A4, Buffer, Length, KernelMode, Frame, NULL, &Handled);
}

static
DECLSPEC_NOINLINE
ULONG_PTR
KiRiscvDispatchSystemService(_Inout_ PKTRAP_FRAME Frame)
{
    ULONG64 Service = Frame->Context.T0;
    ULONG64 Table = Service >> TABLE_OFFSET_BITS;
    ULONG Index = Service & SERVICE_NUMBER_MASK;
    PKTHREAD Thread = KeGetCurrentThread();
    PKSERVICE_TABLE_DESCRIPTOR Descriptor;
    ULONG_PTR Arguments[KI_RISCV_MAX_SERVICE_ARGUMENTS] = {0};
    ULONG Count, Argument;
    NTSTATUS Status;

    if (Service == RISCV_DEBUG_SERVICE_CALL)
        return KiRiscvUserDebugService(Frame);

    /* The outer entry completes GUI conversion before argument capture. */
    if ((Table >= SSDT_MAX_ENTRIES) || (Table && !Thread->GuiThread))
        return (LONG_PTR)STATUS_INVALID_SYSTEM_SERVICE;
    Descriptor = &(Thread->GuiThread ? KeServiceDescriptorTableShadow : KeServiceDescriptorTable)[Table];
    if (!Descriptor->Base || !Descriptor->Number || (Index >= Descriptor->Limit))
        return (LONG_PTR)STATUS_INVALID_SYSTEM_SERVICE;
    Count = Descriptor->Number[Index];
    if ((Count % sizeof(ULONG_PTR)) || (Count / sizeof(ULONG_PTR) > RTL_NUMBER_OF(Arguments)) ||
        (Descriptor->Base[Index] < (ULONG_PTR)MmSystemRangeStart))
        return (LONG_PTR)STATUS_INVALID_SYSTEM_SERVICE;
    Count /= sizeof(ULONG_PTR);

    for (Argument = 0; (Argument < Count) && (Argument < 8); ++Argument)
        Arguments[Argument] = Frame->Context.X[10 + Argument];
    if (Count > 8)
    {
        if (Frame->Context.Sp & 15) return (LONG_PTR)STATUS_DATATYPE_MISALIGNMENT;
        Status = KiRiscvCopyFromUser(&Arguments[8], (PVOID)Frame->Context.Sp, (Count - 8) * sizeof(ULONG_PTR));
        if (!NT_SUCCESS(Status)) return (LONG_PTR)Status;
    }

    return KiRiscvInvokeSystemService((PVOID)Descriptor->Base[Index], Arguments);
}

DECLSPEC_NORETURN
VOID
NTAPI
KiRiscvSystemService(_Inout_ PKTRAP_FRAME Frame)
{
    PKTHREAD Thread = KeGetCurrentThread();
    ULONG_PTR Result;
    NTSTATUS Status;

    ASSERT(KiUserTrap(Frame));
    ASSERT(Frame->Scause == 8);
    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    Thread->PreviousMode = UserMode;
    KeGetCurrentPrcb()->KeSystemCalls++;
    /* ECALL is always four bytes, unlike a compressed breakpoint. */
    Frame->Context.Pc += 4;
    _enable();
    if (((Frame->Context.T0 >> TABLE_OFFSET_BITS) == WIN32K_SERVICE_INDEX) &&
        !Thread->GuiThread && KeServiceDescriptorTableShadow[WIN32K_SERVICE_INDEX].Base &&
        PspW32ProcessCallout && PspW32ThreadCallout)
    {
        Status = PsConvertToGuiThread();
        /* Even a failed callout may have moved and freed the old stack. */
        Frame = Thread->TrapFrame;
        if (!NT_SUCCESS(Status) && (Status != STATUS_ALREADY_WIN32))
        {
            Result = (LONG_PTR)Status;
            goto Exit;
        }
    }
    /* GDI32 queues drawing commands in the TEB. The win32k call is the
     * synchronization point: without this flush, NtGdiFlush returns while
     * the batch remains full and later commands overwrite Win32ClientInfo. */
    if (((Frame->Context.T0 >> TABLE_OFFSET_BITS) == WIN32K_SERVICE_INDEX) &&
        Thread->GuiThread && Thread->Teb && KeGdiFlushUserBatch)
    {
        ULONG GdiBatchCount = 0;
        Status = KiRiscvCopyFromUser(&GdiBatchCount,
                                     (PUCHAR)Thread->Teb + FIELD_OFFSET(TEB, GdiBatchCount),
                                     sizeof(GdiBatchCount));
        if (NT_SUCCESS(Status) && GdiBatchCount)
            KeGdiFlushUserBatch();
    }
    Result = KiRiscvDispatchSystemService(Frame);

Exit:
    Frame = Thread->TrapFrame;
    Frame->Context.A0 = Result;

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        KeBugCheckEx(IRQL_GT_ZERO_AT_SYSTEM_SERVICE, Frame->Context.T0, KeGetCurrentIrql(), 0, 0);
    if ((Thread->ApcStateIndex != OriginalApcEnvironment) || Thread->CombinedApcDisable)
        KeBugCheckEx(APC_INDEX_MISMATCH, Frame->Context.T0, Thread->ApcStateIndex, Thread->CombinedApcDisable, 0);
    KiRiscvReturnToUser(Frame);
}
