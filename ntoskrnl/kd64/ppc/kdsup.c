/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC kernel debugger support
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* No MSRs, ports, buses or control space are reachable through KD yet. Do
 * not fabricate output data for any of them. */
#define PPC_KD_UNAVAILABLE(Name, Parameters) \
    NTSTATUS NTAPI Name Parameters { return STATUS_NOT_SUPPORTED; }

PPC_KD_UNAVAILABLE(KdpSysReadMsr, (ULONG Msr, PULONGLONG MsrValue))
PPC_KD_UNAVAILABLE(KdpSysWriteMsr, (ULONG Msr, PULONGLONG MsrValue))
PPC_KD_UNAVAILABLE(KdpSysReadControlSpace, (ULONG Processor, ULONG64 BaseAddress, PVOID Buffer, ULONG Length, PULONG ActualLength))
PPC_KD_UNAVAILABLE(KdpSysWriteControlSpace, (ULONG Processor, ULONG64 BaseAddress, PVOID Buffer, ULONG Length, PULONG ActualLength))
PPC_KD_UNAVAILABLE(KdpSysReadBusData, (BUS_DATA_TYPE BusDataType, ULONG BusNumber, ULONG SlotNumber, ULONG Offset, PVOID Buffer, ULONG Length, PULONG ActualLength))
PPC_KD_UNAVAILABLE(KdpSysWriteBusData, (BUS_DATA_TYPE BusDataType, ULONG BusNumber, ULONG SlotNumber, ULONG Offset, PVOID Buffer, ULONG Length, PULONG ActualLength))
PPC_KD_UNAVAILABLE(KdpSysReadIoSpace, (INTERFACE_TYPE InterfaceType, ULONG BusNumber, ULONG AddressSpace, ULONG64 IoAddress, PVOID DataValue, ULONG DataSize, PULONG ActualDataSize))
PPC_KD_UNAVAILABLE(KdpSysWriteIoSpace, (INTERFACE_TYPE InterfaceType, ULONG BusNumber, ULONG AddressSpace, ULONG64 IoAddress, PVOID DataValue, ULONG DataSize, PULONG ActualDataSize))
PPC_KD_UNAVAILABLE(KdpSysCheckLowMemory, (ULONG Flags))

VOID
NTAPI
KdpGetStateChange(
    _Inout_ PDBGKD_MANIPULATE_STATE64 State,
    _Inout_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);

    /* Only process successful continue requests */
    if (NT_SUCCESS(State->u.Continue2.ContinueStatus))
    {
        /* Update current symbol window if debugger sent new values */
        if (State->u.Continue2.ControlSet.CurrentSymbolStart != 1)
        {
            KdpCurrentSymbolStart = (ULONG_PTR)State->u.Continue2.ControlSet.CurrentSymbolStart;
            KdpCurrentSymbolEnd = (ULONG_PTR)State->u.Continue2.ControlSet.CurrentSymbolEnd;
        }
    }
}

VOID
NTAPI
KdpSetContextState(
    _Inout_ PDBGKD_ANY_WAIT_STATE_CHANGE WaitStateChange,
    _Inout_ PCONTEXT Context)
{
    /* KdpSetCommonState already copied the instruction window and count.
     * Report the control registers that identify the stopped frame. */
    WaitStateChange->ControlReport.Iar = Context->Iar;
    WaitStateChange->ControlReport.Sp = Context->Gpr1;
    WaitStateChange->ControlReport.Lr = Context->Lr;
    WaitStateChange->ControlReport.Msr = Context->Msr;
}

NTSTATUS
NTAPI
KdpAllowDisable(VOID)
{
    /* No hardware breakpoint state can be left armed yet. */
    return STATUS_SUCCESS;
}
