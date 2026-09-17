/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V KD architecture support (ABI-127)
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* No MSRs, ports, buses or control space are reachable through KD yet. Do
 * not fabricate output data for any of them. */
#define RISCV_KD_UNAVAILABLE(Name, Parameters) \
    NTSTATUS NTAPI Name Parameters { return STATUS_NOT_SUPPORTED; }

RISCV_KD_UNAVAILABLE(KdpSysReadMsr, (ULONG Msr, PULONGLONG MsrValue))
RISCV_KD_UNAVAILABLE(KdpSysWriteMsr, (ULONG Msr, PULONGLONG MsrValue))
RISCV_KD_UNAVAILABLE(KdpSysReadControlSpace, (ULONG Processor, ULONG64 BaseAddress, PVOID Buffer, ULONG Length, PULONG ActualLength))
RISCV_KD_UNAVAILABLE(KdpSysWriteControlSpace, (ULONG Processor, ULONG64 BaseAddress, PVOID Buffer, ULONG Length, PULONG ActualLength))
RISCV_KD_UNAVAILABLE(KdpSysReadBusData, (BUS_DATA_TYPE BusDataType, ULONG BusNumber, ULONG SlotNumber, ULONG Offset, PVOID Buffer, ULONG Length, PULONG ActualLength))
RISCV_KD_UNAVAILABLE(KdpSysWriteBusData, (BUS_DATA_TYPE BusDataType, ULONG BusNumber, ULONG SlotNumber, ULONG Offset, PVOID Buffer, ULONG Length, PULONG ActualLength))
RISCV_KD_UNAVAILABLE(KdpSysReadIoSpace, (INTERFACE_TYPE InterfaceType, ULONG BusNumber, ULONG AddressSpace, ULONG64 IoAddress, PVOID DataValue, ULONG DataSize, PULONG ActualDataSize))
RISCV_KD_UNAVAILABLE(KdpSysWriteIoSpace, (INTERFACE_TYPE InterfaceType, ULONG BusNumber, ULONG AddressSpace, ULONG64 IoAddress, PVOID DataValue, ULONG DataSize, PULONG ActualDataSize))
RISCV_KD_UNAVAILABLE(KdpSysCheckLowMemory, (ULONG Flags))

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
    WaitStateChange->ControlReport.Pc = Context->Pc;
    WaitStateChange->ControlReport.Sp = Context->Sp;
    WaitStateChange->ControlReport.Ra = Context->Ra;
}

NTSTATUS
NTAPI
KdpAllowDisable(VOID)
{
    /* No hardware breakpoint state can be left armed on RISC-V yet. */
    return STATUS_SUCCESS;
}

KCONTINUE_STATUS
NTAPI
KxSwitchKdProcessor(
    _In_ ULONG ProcessorIndex)
{
    UNREFERENCED_PARAMETER(ProcessorIndex);
    return ContinueError;
}

/*
 * Interim debug-service path (ABI-127): RtlDebugPrint/DbgPrompt reach
 * KdpPrint/KdpPrompt through a direct call because the RISC-V trap path is
 * not resumable yet, so no ebreak-based debug-service ABI can be dispatched
 * through KdpTrap. The argument mapping matches the x86/amd64 DebugService
 * (Argument1 = string, Argument2 = length, Argument3/4 = component/level or
 * response buffer/length). Replace with a trap-based service once the
 * debug-service ebreak ABI exists.
 */
ULONG
NTAPI
DebugService(
    _In_ ULONG Service,
    _In_ PVOID Argument1,
    _In_ PVOID Argument2,
    _In_ PVOID Argument3,
    _In_ PVOID Argument4)
{
    BOOLEAN Handled = FALSE;

    switch (Service)
    {
        case BREAKPOINT_PRINT:
            return (ULONG)KdpPrint((ULONG)(ULONG_PTR)Argument3,
                                   (ULONG)(ULONG_PTR)Argument4,
                                   (PCHAR)Argument1,
                                   (USHORT)(ULONG_PTR)Argument2,
                                   KernelMode,
                                   NULL,
                                   NULL,
                                   &Handled);

        case BREAKPOINT_PROMPT:
            return (ULONG)KdpPrompt((PCHAR)Argument1,
                                    (USHORT)(ULONG_PTR)Argument2,
                                    (PCHAR)Argument3,
                                    (USHORT)(ULONG_PTR)Argument4,
                                    KernelMode,
                                    NULL,
                                    NULL);

        default:
            break;
    }

    return (ULONG)STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
DebugService2(
    _In_ PVOID Argument1,
    _In_ PVOID Argument2,
    _In_ ULONG Service)
{
    /* Symbol load/unload and command-string notifications have no consumer
     * until a resumable debug-service trap exists. */
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);
    UNREFERENCED_PARAMETER(Service);
}
