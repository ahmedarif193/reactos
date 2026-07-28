/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/kd64/arm64/kdarm64.c
 * PURPOSE:         KD support for ARM64
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define KD_100NS_PER_SECOND 10000000ULL

/* cntfrq_el0 is immutable after firmware init; read and sanitize it once */
static ULONGLONG KdpArm64CounterFrequency;

ULONGLONG
NTAPI
KdpQueryDebugTimestamp(VOID)
{
    ULONGLONG InterruptTime;
    ULONGLONG Frequency;
    ULONGLONG Counter;
    ULONGLONG Seconds;
    ULONGLONG Remainder;
    ULONGLONG CounterTime;

    InterruptTime = KeQueryInterruptTime();

    Frequency = KdpArm64CounterFrequency;
    if (Frequency == 0)
    {
        Frequency = KiArm64GetCounterFrequency();
        KdpArm64CounterFrequency = Frequency;
    }

    /*
     * ARM64 keeps GIC Group 1 delivery masked during early MM bring-up, so
     * SharedUserData->InterruptTime remains zero until the clock ISR can run.
     * The architectural counter is already live and gives useful monotonic
     * debug timestamps without enabling interrupts too early.
     */
    __asm__ __volatile__("mrs %0, cntpct_el0" : "=r"(Counter));
    Seconds = Counter / Frequency;
    Remainder = Counter % Frequency;
    CounterTime = (Seconds * KD_100NS_PER_SECOND) + ((Remainder * KD_100NS_PER_SECOND) / Frequency);

    return max(CounterTime, InterruptTime);
}

VOID
NTAPI
KdpGetStateChange(_Inout_ PDBGKD_MANIPULATE_STATE64 State,
                  _Inout_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);

    /* Only process successful continue requests */
    if (NT_SUCCESS(State->u.Continue2.ContinueStatus))
    {
        /* Update current symbol window if debugger sent new values */
        if (State->u.Continue2.ControlSet.CurrentSymbolStart != 1)
        {
            KdpCurrentSymbolStart = State->u.Continue2.ControlSet.CurrentSymbolStart;
            KdpCurrentSymbolEnd = State->u.Continue2.ControlSet.CurrentSymbolEnd;
        }
    }
}

VOID
NTAPI
KdpSetContextState(_Inout_ PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange,
                   _Inout_ PCONTEXT Context)
{
    PEXCEPTION_RECORD64 ExceptionRecord;

    /*
     * Report which debug event actually fired, so the debugger can tell the
     * slots apart. For an instruction breakpoint the program counter is the
     * matched address. A watchpoint's FAR_EL1 travels with its exception
     * record, avoiding shared state between processors.
     */
    WaitStateChange->ControlReport.Bvr = Context->Pc;
    WaitStateChange->ControlReport.Wvr = 0;

    if (WaitStateChange->NewState != DbgKdExceptionStateChange)
        return;

    ExceptionRecord = &WaitStateChange->u.Exception.ExceptionRecord;
    if (ExceptionRecord->ExceptionCode == STATUS_SINGLE_STEP &&
        ExceptionRecord->NumberParameters != 0)
    {
        WaitStateChange->ControlReport.Wvr =
            ExceptionRecord->ExceptionInformation[0];
    }
}

NTSTATUS
NTAPI
KdpSysReadMsr(_In_ ULONG Msr,
              _Out_ PULONGLONG MsrValue)
{
    /* ARM64 has no x86-style MSRs; return a clear failure */
    UNREFERENCED_PARAMETER(Msr);
    if (MsrValue) *MsrValue = 0;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
KdpSysWriteMsr(_In_ ULONG Msr,
               _In_ PULONGLONG MsrValue)
{
    UNREFERENCED_PARAMETER(Msr);
    UNREFERENCED_PARAMETER(MsrValue);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
KdpSysReadBusData(_In_ BUS_DATA_TYPE BusDataType,
                  _In_ ULONG BusNumber,
                  _In_ ULONG SlotNumber,
                  _In_ ULONG Offset,
                  _Out_writes_bytes_(Length) PVOID Buffer,
                  _In_ ULONG Length,
                  _Out_ PULONG ActualLength)
{
    UNREFERENCED_PARAMETER(BusDataType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    if (ActualLength) *ActualLength = 0;
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
KdpSysWriteBusData(_In_ BUS_DATA_TYPE BusDataType,
                   _In_ ULONG BusNumber,
                   _In_ ULONG SlotNumber,
                   _In_ ULONG Offset,
                   _In_reads_bytes_(Length) PVOID Buffer,
                   _In_ ULONG Length,
                   _Out_ PULONG ActualLength)
{
    UNREFERENCED_PARAMETER(BusDataType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(SlotNumber);
    UNREFERENCED_PARAMETER(Offset);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
    if (ActualLength) *ActualLength = 0;
    return STATUS_NOT_IMPLEMENTED;
}

/* KdpSysReadControlSpace/KdpSysWriteControlSpace live in kd64/kdcontrol.c,
 * shared with AMD64 (same control-space layout and constants). */

NTSTATUS
NTAPI
KdpSysReadIoSpace(_In_ INTERFACE_TYPE InterfaceType,
                  _In_ ULONG BusNumber,
                  _In_ ULONG AddressSpace,
                  _In_ ULONG64 IoAddress,
                  _Out_writes_bytes_(DataSize) PVOID DataValue,
                  _In_ ULONG DataSize,
                  _Out_ PULONG ActualDataSize)
{
    UNREFERENCED_PARAMETER(InterfaceType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(AddressSpace);
    UNREFERENCED_PARAMETER(IoAddress);
    UNREFERENCED_PARAMETER(DataValue);
    if (ActualDataSize) *ActualDataSize = 0;
    UNREFERENCED_PARAMETER(DataSize);
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS
NTAPI
KdpSysWriteIoSpace(_In_ INTERFACE_TYPE InterfaceType,
                   _In_ ULONG BusNumber,
                   _In_ ULONG AddressSpace,
                   _In_ ULONG64 IoAddress,
                   _In_reads_bytes_(DataSize) PVOID DataValue,
                   _In_ ULONG DataSize,
                   _Out_ PULONG ActualDataSize)
{
    UNREFERENCED_PARAMETER(InterfaceType);
    UNREFERENCED_PARAMETER(BusNumber);
    UNREFERENCED_PARAMETER(AddressSpace);
    UNREFERENCED_PARAMETER(IoAddress);
    UNREFERENCED_PARAMETER(DataValue);
    if (ActualDataSize) *ActualDataSize = 0;
    UNREFERENCED_PARAMETER(DataSize);
    return STATUS_INVALID_PARAMETER;
}

NTSTATUS
NTAPI
KdpSysCheckLowMemory(_In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Flags);
    return STATUS_NOT_IMPLEMENTED;
}

NTSTATUS
NTAPI
KdpAllowDisable(VOID)
{
    ULONG i, j;

    /* Disallow disabling KD if any HW break/watchpoints are enabled */
    for (i = 0; i < KeNumberProcessors; i++)
    {
        PKSPECIAL_REGISTERS Sr = &KiProcessorBlock[i]->ProcessorState.SpecialRegisters;
        for (j = 0; j < RTL_NUMBER_OF(Sr->KernelBcr); j++)
        {
            if (Sr->KernelBcr[j] & 0x1) return STATUS_ACCESS_DENIED; /* E bit */
        }
        for (j = 0; j < RTL_NUMBER_OF(Sr->KernelWcr); j++)
        {
            if (Sr->KernelWcr[j] & 0x1) return STATUS_ACCESS_DENIED; /* E bit */
        }
    }

    return STATUS_SUCCESS;
}
