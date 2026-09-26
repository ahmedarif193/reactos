/*
 * PROJECT:     ReactOS RTL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     RtlVirtualUnwind2 over the native RtlVirtualUnwind
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <rtl.h>

#define NDEBUG
#include <debug.h>

/*
 * Status-returning form of RtlVirtualUnwind, exported by Windows 11 ntdll
 * and ntoskrnl on every 64-bit architecture. Where RtlVirtualUnwind is the
 * native unwinder, forward to it. The native decoders do not report
 * machine frames, so MachineFrameUnwound is always FALSE here.
 */
NTSTATUS
NTAPI
RtlVirtualUnwind2(
    _In_ ULONG HandlerType,
    _In_ ULONG_PTR ImageBase,
    _In_ ULONG_PTR ControlPc,
    _In_opt_ PRUNTIME_FUNCTION FunctionEntry,
    _Inout_ PCONTEXT ContextRecord,
    _Out_opt_ PBOOLEAN MachineFrameUnwound,
    _Out_opt_ PVOID *HandlerData,
    _Out_opt_ PULONG_PTR EstablisherFrame,
    _Out_opt_ PKNONVOLATILE_CONTEXT_POINTERS ContextPointers,
    _Out_opt_ PULONG_PTR LowLimit,
    _Out_opt_ PULONG_PTR HighLimit,
    _Out_opt_ PEXCEPTION_ROUTINE *HandlerRoutine,
    _In_ ULONG UnwindFlags)
{
    PEXCEPTION_ROUTINE Handler;
    PVOID LocalHandlerData = NULL;
    ULONG64 LocalEstablisherFrame = 0;
    ULONG_PTR StackLow, StackHigh;

    if ((FunctionEntry == NULL) || (ContextRecord == NULL) || (UnwindFlags != 0))
        return STATUS_INVALID_PARAMETER;

    Handler = RtlVirtualUnwind(HandlerType,
                               ImageBase,
                               ControlPc,
                               FunctionEntry,
                               ContextRecord,
                               &LocalHandlerData,
                               &LocalEstablisherFrame,
                               ContextPointers);

    if (MachineFrameUnwound != NULL)
        *MachineFrameUnwound = FALSE;
    if (HandlerData != NULL)
        *HandlerData = LocalHandlerData;
    if (EstablisherFrame != NULL)
        *EstablisherFrame = (ULONG_PTR)LocalEstablisherFrame;
    if ((LowLimit != NULL) || (HighLimit != NULL))
    {
        RtlpGetStackLimits(&StackLow, &StackHigh);
        if (LowLimit != NULL)
            *LowLimit = StackLow;
        if (HighLimit != NULL)
            *HighLimit = StackHigh;
    }
    if (HandlerRoutine != NULL)
        *HandlerRoutine = Handler;

    return STATUS_SUCCESS;
}
