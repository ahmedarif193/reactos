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
    PKPRCB Prcb;

    /* Report selected ARM64 debug state via control report */
    Prcb = KeGetCurrentPrcb();
    if (Prcb != NULL)
    {
        /* Expose first breakpoint/watchpoint registers for visibility */
        WaitStateChange->ControlReport.Bvr =
            Prcb->ProcessorState.SpecialRegisters.KernelBvr[0];
        WaitStateChange->ControlReport.Wvr =
            Prcb->ProcessorState.SpecialRegisters.KernelWvr[0];
    }

    UNREFERENCED_PARAMETER(Context);
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

NTSTATUS
NTAPI
KdpSysReadControlSpace(_In_ ULONG Processor,
                       _In_ ULONG64 BaseAddress,
                       _Out_writes_bytes_(Length) PVOID Buffer,
                       _In_ ULONG Length,
                       _Out_ PULONG ActualLength)
{
    PVOID ControlStart;
    PKPRCB Prcb;
    PKIPCR Pcr;

    if (Processor >= KeNumberProcessors)
    {
        if (ActualLength) *ActualLength = 0;
        return STATUS_INVALID_PARAMETER;
    }

    Prcb = KiProcessorBlock[Processor];
    Pcr = CONTAINING_RECORD(Prcb, KIPCR, Prcb);

    /* TODO: are those correct and should they be called AMD64_xxx? */
    switch (BaseAddress)
    {
        case AMD64_DEBUG_CONTROL_SPACE_KPCR:
            ControlStart = &Pcr;
            *ActualLength = sizeof(PVOID);
            break;

        case AMD64_DEBUG_CONTROL_SPACE_KPRCB:
            ControlStart = &Prcb;
            *ActualLength = sizeof(PVOID);
            break;

        case AMD64_DEBUG_CONTROL_SPACE_KSPECIAL:
            ControlStart = &Prcb->ProcessorState.SpecialRegisters;
            *ActualLength = sizeof(KSPECIAL_REGISTERS);
            break;

        case AMD64_DEBUG_CONTROL_SPACE_KTHREAD:
            ControlStart = &Prcb->CurrentThread;
            *ActualLength = sizeof(PVOID);
            break;

        default:
            *ActualLength = 0;
            return STATUS_UNSUCCESSFUL;
    }

    RtlCopyMemory(Buffer, ControlStart, min(Length, *ActualLength));
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KdpSysWriteControlSpace(_In_ ULONG Processor,
                        _In_ ULONG64 BaseAddress,
                        _In_reads_bytes_(Length) PVOID Buffer,
                        _In_ ULONG Length,
                        _Out_ PULONG ActualLength)
{
    PVOID ControlStart;
    PKPRCB Prcb;

    if (Processor >= KeNumberProcessors)
    {
        if (ActualLength) *ActualLength = 0;
        return STATUS_INVALID_PARAMETER;
    }

    Prcb = KiProcessorBlock[Processor];

    switch (BaseAddress)
    {
        case AMD64_DEBUG_CONTROL_SPACE_KSPECIAL:
            ControlStart = &Prcb->ProcessorState.SpecialRegisters;
            *ActualLength = sizeof(KSPECIAL_REGISTERS);
            break;

        default:
            *ActualLength = 0;
            return STATUS_UNSUCCESSFUL;
    }

    RtlCopyMemory(ControlStart, Buffer, min(Length, *ActualLength));
    return STATUS_SUCCESS;
}

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
