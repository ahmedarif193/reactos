/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V thread context get/set APC
 */

#include <ntoskrnl.h>

VOID
NTAPI
PspGetOrSetContextKernelRoutine(
    _In_ PKAPC Apc,
    _Inout_ PKNORMAL_ROUTINE *NormalRoutine,
    _Inout_ PVOID *NormalContext,
    _Inout_ PVOID *SystemArgument1,
    _Inout_ PVOID *SystemArgument2)
{
    PGET_SET_CTX_CONTEXT Request = CONTAINING_RECORD(Apc, GET_SET_CTX_CONTEXT, Apc);
    PKTHREAD Thread = Apc->SystemArgument2;
    PKTRAP_FRAME Frame = Thread->TrapFrame;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
    ASSERT(Thread == KeGetCurrentThread());

    /* Nested kernel traps link back to the complete interrupted user bank. */
    if (Request->Mode == UserMode)
    {
        while (Frame && !KiUserTrap(Frame))
            Frame = Frame->PreviousTrapFrame;
    }
    if (!Frame && Thread->Teb)
        Frame = (PKTRAP_FRAME)Thread->InitialStack - 1;

    if (!Frame)
    {
        Request->Status = STATUS_INVALID_DEVICE_STATE;
        KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
        return;
    }

    /* Accept only this architecture's context layout. */
    if (!(Request->Context.ContextFlags & CONTEXT_RISCV64) ||
        (Request->Context.ContextFlags & ~CONTEXT_ALL))
    {
        Request->Status = STATUS_INVALID_PARAMETER;
        KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
        return;
    }

    if (Apc->SystemArgument1)
        KeContextToTrapFrame(&Request->Context, NULL, Frame,
                            Request->Context.ContextFlags, Request->Mode);
    else
        KeTrapFrameToContext(Frame, NULL, &Request->Context);

    KeSetEvent(&Request->Event, IO_NO_INCREMENT, FALSE);
}
