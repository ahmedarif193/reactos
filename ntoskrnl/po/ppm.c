/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:            ntoskrnl/po/ppm.c
 * PURPOSE:         Processor power management helpers
 * COPYRIGHT:       2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <internal/ke.h>
#include <internal/po.h>
#include <debug.h>

#define PO_IDLE_HANDLER_TAG 'dIoP'

#ifndef NonPagedPoolNx
#define NonPagedPoolNx NonPagedPool
#endif

static KSPIN_LOCK PopIdleHandlerLock;
POP_IDLE_RUNDOWN_ENTRY PopIdleHandlerRundown[MAXIMUM_PROCESSORS];

VOID
PopInitIdleHandlerSupport(VOID)
{
    KeInitializeSpinLock(&PopIdleHandlerLock);
    for (ULONG Index = 0; Index < MAXIMUM_PROCESSORS; ++Index)
    {
        ExInitializeRundownProtection(&PopIdleHandlerRundown[Index].Ref);
    }
}

static
BOOLEAN
PopValidateIdleRequest(
    _In_ ULONG ProcessorNumber,
    _In_reads_(HandlerCount) PPO_PROCESSOR_IDLE_HANDLER IdleHandlers,
    _In_ ULONG HandlerCount)
{
    if (!IdleHandlers || HandlerCount == 0 || HandlerCount > MAX_IDLE_HANDLERS)
        return FALSE;

    if (ProcessorNumber >= (ULONG)KeNumberProcessors)
        return FALSE;

    for (ULONG Index = 0; Index < HandlerCount; ++Index)
    {
        if (!IdleHandlers[Index].Info.Handler)
            return FALSE;
    }

    return TRUE;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
NTSTATUS
NTAPI
PoRegisterProcessorIdleHandler(
    _In_ ULONG ProcessorNumber,
    _In_reads_(HandlerCount) PPO_PROCESSOR_IDLE_HANDLER IdleHandlers,
    _In_ ULONG HandlerCount)
{
    PKPRCB Prcb;
    PPOP_IDLE_HANDLER_ENTRY Entries = NULL;
    KIRQL OldIrql;

    if (!PopValidateIdleRequest(ProcessorNumber, IdleHandlers, HandlerCount))
        return STATUS_INVALID_PARAMETER;

    Prcb = KiProcessorBlock[ProcessorNumber];
    if (!Prcb)
        return STATUS_INVALID_PARAMETER;

    Entries = ExAllocatePoolWithTag(NonPagedPoolNx,
                                    HandlerCount * sizeof(POP_IDLE_HANDLER_ENTRY),
                                    PO_IDLE_HANDLER_TAG);
    if (!Entries)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (ULONG Index = 0; Index < HandlerCount; ++Index)
    {
        Entries[Index].Info = IdleHandlers[Index].Info;
        Entries[Index].Context = IdleHandlers[Index].Context;
    }

    KeAcquireSpinLock(&PopIdleHandlerLock, &OldIrql);
    if (Prcb->PowerState.IdleHandlers != NULL)
    {
        KeReleaseSpinLock(&PopIdleHandlerLock, OldIrql);
        ExFreePoolWithTag(Entries, PO_IDLE_HANDLER_TAG);
        return STATUS_DEVICE_BUSY;
    }

    ExfReInitializeRundownProtection(&PopIdleHandlerRundown[ProcessorNumber].Ref);
    Prcb->PowerState.IdleHandlers = Entries;
    Prcb->PowerState.IdleHandlersCount = HandlerCount;
    KeReleaseSpinLock(&PopIdleHandlerLock, OldIrql);

    return STATUS_SUCCESS;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
VOID
NTAPI
PoUnregisterProcessorIdleHandler(
    _In_ ULONG ProcessorNumber)
{
    PKPRCB Prcb;
    PPOP_IDLE_HANDLER_ENTRY Entries = NULL;
    KIRQL OldIrql;

    if (ProcessorNumber >= (ULONG)KeNumberProcessors)
        return;

    Prcb = KiProcessorBlock[ProcessorNumber];
    if (!Prcb)
        return;

    KeAcquireSpinLock(&PopIdleHandlerLock, &OldIrql);
    Entries = (PPOP_IDLE_HANDLER_ENTRY)Prcb->PowerState.IdleHandlers;
    Prcb->PowerState.IdleHandlers = NULL;
    Prcb->PowerState.IdleHandlersCount = 0;
    KeReleaseSpinLock(&PopIdleHandlerLock, OldIrql);

    if (Entries)
    {
        ExRundownCompleted(&PopIdleHandlerRundown[ProcessorNumber].Ref);
        ExWaitForRundownProtectionRelease(&PopIdleHandlerRundown[ProcessorNumber].Ref);
        ExFreePoolWithTag(Entries, PO_IDLE_HANDLER_TAG);
    }
}
