/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        drivers/network/ndis/ndis/60filter.c
 * PURPOSE:     NDIS 6 filter driver datapath chain walk.
 *
 *              Filters attach to an adapter via NdisFRegisterFilterDriver
 *              + AttachHandler. Once attached, every TX, send-complete,
 *              receive, and return-NBL operation must walk the per-adapter
 *              filter chain in order:
 *
 *                  TX:           protocol -> top filter -> ... -> bottom filter -> miniport
 *                  Send-Cmpl:    miniport -> bottom -> ... -> top -> protocol
 *                  RX:           miniport -> bottom -> ... -> top -> protocol
 *                  Return-NBL:   protocol -> top -> ... -> bottom -> miniport
 *
 *              The bridge models the chain as Adapter->NDIS6_ADAPTER_EXT->
 *              FilterModuleList. List head = topmost filter (closest to
 *              the protocol), list tail = bottommost (closest to the
 *              miniport). Each filter sees the bridge as "the layer above
 *              and below"; the bridge multiplexes all NdisF* helper calls
 *              into a chain walk.
 *
 *              The handle the bridge passes to the filter at AttachHandler
 *              time is (NDIS_HANDLE)PNDIS6_FILTER_MODULE. Filters round-
 *              trip that handle in every NdisF* helper call so we can
 *              find their position in the chain.
 *
 *              Created on the dev-nt6-1 branch as part of the NDIS 5↔6
 *              filter framework work (Phase 8 of the bridge plan).
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors
 */

#include "ndis6_internal.h"

/* MiniIndicateReceivePacket forward decl — same as 60thunk_rx.c. */
extern VOID NTAPI
MiniIndicateReceivePacket(
    IN NDIS_HANDLE   MiniportAdapterHandle,
    IN PPNDIS_PACKET PacketArray,
    IN UINT          NumberOfPackets);

VOID
NTAPI
NdisFPauseComplete(
    _In_ NDIS_HANDLE NdisFilterHandle)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;

    if (Module == NULL ||
        Module->Signature != NDIS6_FILTER_MODULE_SIGNATURE ||
        InterlockedCompareExchange(&Module->State, NDIS6_FILTER_STATE_PAUSING, NDIS6_FILTER_STATE_PAUSING) !=
            NDIS6_FILTER_STATE_PAUSING)
    {
        return;
    }

    Module->PauseStatus = NDIS_STATUS_SUCCESS;
    KeSetEvent(&Module->PauseEvent, IO_NO_INCREMENT, FALSE);
}

VOID
NTAPI
NdisFRestartComplete(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ NDIS_STATUS Status)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;

    if (Module == NULL ||
        Module->Signature != NDIS6_FILTER_MODULE_SIGNATURE ||
        InterlockedCompareExchange(&Module->State, NDIS6_FILTER_STATE_RESTARTING, NDIS6_FILTER_STATE_RESTARTING) !=
            NDIS6_FILTER_STATE_RESTARTING)
    {
        return;
    }

    Module->RestartStatus = Status;
    KeSetEvent(&Module->RestartEvent, IO_NO_INCREMENT, FALSE);
}

NDIS_STATUS
Ndis6PauseFilterModules(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    for (;;)
    {
        PNDIS6_FILTER_MODULE Module = NULL;
        PLIST_ENTRY Entry;
        KIRQL OldIrql;

        /* Adapter list head is the top of the filter stack. Pause proceeds
         * top-down toward the miniport. */
        KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
        for (Entry = Ext->FilterModuleList.Flink;
             Entry != &Ext->FilterModuleList;
             Entry = Entry->Flink)
        {
            PNDIS6_FILTER_MODULE Candidate =
                CONTAINING_RECORD(Entry, NDIS6_FILTER_MODULE, ListEntry);
            if (InterlockedCompareExchange(&Candidate->State, NDIS6_FILTER_STATE_PAUSING, NDIS6_FILTER_STATE_RUNNING) ==
                    NDIS6_FILTER_STATE_RUNNING &&
                Ndis6ReferenceFilterModule(Candidate))
            {
                Module = Candidate;
                break;
            }
        }
        KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);

        if (Module == NULL)
            return NDIS_STATUS_SUCCESS;

        {
            NDIS_FILTER_PAUSE_PARAMETERS Parameters;
            NDIS_STATUS Status;
            NTSTATUS WaitStatus;

            RtlZeroMemory(&Parameters, sizeof(Parameters));
            Parameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Parameters.Header.Revision = NDIS_FILTER_PAUSE_PARAMETERS_REVISION_1;
            Parameters.Header.Size = NDIS_SIZEOF_FILTER_PAUSE_PARAMETERS_REVISION_1;

            Module->PauseStatus = NDIS_STATUS_PENDING;
            KeClearEvent(&Module->PauseEvent);
            if (Module->DriverBlock->Characteristics.PauseHandler != NULL)
            {
                Status = Module->DriverBlock->Characteristics.PauseHandler(Module->FilterModuleContext, &Parameters);
            }
            else
            {
                Status = NDIS_STATUS_SUCCESS;
            }
            if (Status == NDIS_STATUS_PENDING)
            {
                WaitStatus = KeWaitForSingleObject(&Module->PauseEvent, Executive, KernelMode, FALSE, NULL);
                Status = NT_SUCCESS(WaitStatus)
                    ? Module->PauseStatus
                    : (NDIS_STATUS)WaitStatus;
            }

            InterlockedExchange(&Module->State, Status == NDIS_STATUS_SUCCESS ? NDIS6_FILTER_STATE_PAUSED : NDIS6_FILTER_STATE_RUNNING);
            Ndis6DereferenceFilterModule(Module);
            if (Status != NDIS_STATUS_SUCCESS)
                return Status;
        }
    }
}

NDIS_STATUS
Ndis6RestartFilterModules(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    for (;;)
    {
        PNDIS6_FILTER_MODULE Module = NULL;
        PLIST_ENTRY Entry;
        KIRQL OldIrql;

        /* Restart proceeds bottom-up from the miniport toward protocols. */
        KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
        for (Entry = Ext->FilterModuleList.Blink;
             Entry != &Ext->FilterModuleList;
             Entry = Entry->Blink)
        {
            PNDIS6_FILTER_MODULE Candidate =
                CONTAINING_RECORD(Entry, NDIS6_FILTER_MODULE, ListEntry);
            if (InterlockedCompareExchange(&Candidate->State, NDIS6_FILTER_STATE_RESTARTING, NDIS6_FILTER_STATE_PAUSED) ==
                    NDIS6_FILTER_STATE_PAUSED &&
                Ndis6ReferenceFilterModule(Candidate))
            {
                Module = Candidate;
                break;
            }
        }
        KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);

        if (Module == NULL)
            return NDIS_STATUS_SUCCESS;

        {
            NDIS_FILTER_RESTART_PARAMETERS Parameters;
            NDIS_STATUS Status;
            NTSTATUS WaitStatus;

            RtlZeroMemory(&Parameters, sizeof(Parameters));
            Parameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            Parameters.Header.Revision = NDIS_FILTER_RESTART_PARAMETERS_REVISION_1;
            Parameters.Header.Size = NDIS_SIZEOF_FILTER_RESTART_PARAMETERS_REVISION_1;
            if (Ext->GeneralAttrsValid)
            {
                Parameters.MiniportMediaType = Ext->GeneralAttrs.MediaType;
                Parameters.MiniportPhysicalMediaType =
                    Ext->GeneralAttrs.PhysicalMediumType;
            }
            Parameters.LowerIfIndex = Ext->IfIndex;
            Parameters.LowerIfNetLuid = Ext->NetLuid;

            Module->RestartStatus = NDIS_STATUS_PENDING;
            KeClearEvent(&Module->RestartEvent);
            if (Module->DriverBlock->Characteristics.RestartHandler != NULL)
            {
                Status = Module->DriverBlock->Characteristics.RestartHandler(Module->FilterModuleContext, &Parameters);
            }
            else
            {
                Status = NDIS_STATUS_SUCCESS;
            }
            if (Status == NDIS_STATUS_PENDING)
            {
                WaitStatus = KeWaitForSingleObject(&Module->RestartEvent, Executive, KernelMode, FALSE, NULL);
                Status = NT_SUCCESS(WaitStatus)
                    ? Module->RestartStatus
                    : (NDIS_STATUS)WaitStatus;
            }

            InterlockedExchange(&Module->State, Status == NDIS_STATUS_SUCCESS ? NDIS6_FILTER_STATE_RUNNING : NDIS6_FILTER_STATE_PAUSED);
            Ndis6DereferenceFilterModule(Module);
            if (Status != NDIS_STATUS_SUCCESS)
                return Status;
        }
    }
}

/* ============================================================================
 *  Chain helpers
 * ============================================================================ */

typedef enum _NDIS6_FILTER_OPERATION
{
    Ndis6FilterSend,
    Ndis6FilterSendComplete,
    Ndis6FilterCancelSend,
    Ndis6FilterReceive,
    Ndis6FilterReturn,
    Ndis6FilterOidRequest,
    Ndis6FilterOidRequestComplete,
    Ndis6FilterDirectOidRequest,
    Ndis6FilterDirectOidRequestComplete,
    Ndis6FilterStatus,
    Ndis6FilterDevicePnPEvent,
    Ndis6FilterNetPnPEvent
} NDIS6_FILTER_OPERATION;

static FILTER_SEND_NET_BUFFER_LISTS_HANDLER
Ndis6FilterGetSendHandler(
    _In_ PNDIS6_FILTER_MODULE Module)
{
    return Module->PartialCharacteristicsValid
        ? Module->PartialCharacteristics.SendNetBufferListsHandler
        : Module->DriverBlock->Characteristics.SendNetBufferListsHandler;
}

static FILTER_SEND_NET_BUFFER_LISTS_COMPLETE_HANDLER
Ndis6FilterGetSendCompleteHandler(
    _In_ PNDIS6_FILTER_MODULE Module)
{
    return Module->PartialCharacteristicsValid
        ? Module->PartialCharacteristics.SendNetBufferListsCompleteHandler
        : Module->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler;
}

static FILTER_CANCEL_SEND_HANDLER
Ndis6FilterGetCancelSendHandler(
    _In_ PNDIS6_FILTER_MODULE Module)
{
    return Module->PartialCharacteristicsValid
        ? Module->PartialCharacteristics.CancelSendNetBufferListsHandler
        : Module->DriverBlock->Characteristics.CancelSendNetBufferListsHandler;
}

static FILTER_RECEIVE_NET_BUFFER_LISTS_HANDLER
Ndis6FilterGetReceiveHandler(
    _In_ PNDIS6_FILTER_MODULE Module)
{
    return Module->PartialCharacteristicsValid
        ? Module->PartialCharacteristics.ReceiveNetBufferListsHandler
        : Module->DriverBlock->Characteristics.ReceiveNetBufferListsHandler;
}

static FILTER_RETURN_NET_BUFFER_LISTS_HANDLER
Ndis6FilterGetReturnHandler(
    _In_ PNDIS6_FILTER_MODULE Module)
{
    return Module->PartialCharacteristicsValid
        ? Module->PartialCharacteristics.ReturnNetBufferListsHandler
        : Module->DriverBlock->Characteristics.ReturnNetBufferListsHandler;
}

static BOOLEAN
Ndis6FilterSupportsOperation(
    _In_ PNDIS6_FILTER_MODULE Module,
    _In_ NDIS6_FILTER_OPERATION Operation)
{
    PNDIS_FILTER_DRIVER_CHARACTERISTICS Characteristics;
    LONG State;

    if (Module == NULL || Module->DriverBlock == NULL)
        return FALSE;

    State = InterlockedCompareExchange(&Module->State, NDIS6_FILTER_STATE_PAUSED, NDIS6_FILTER_STATE_PAUSED);
    /* Pause gates new sends and receive indications. Completions, returns,
     * and OIDs must still cross a paused stack to retire existing ownership. */
    if ((Operation == Ndis6FilterSend || Operation == Ndis6FilterReceive) &&
        State != NDIS6_FILTER_STATE_RUNNING)
    {
        return FALSE;
    }

    Characteristics = &Module->DriverBlock->Characteristics;
    switch (Operation)
    {
        case Ndis6FilterSend:
            return Ndis6FilterGetSendHandler(Module) != NULL;
        case Ndis6FilterSendComplete:
            return Ndis6FilterGetSendCompleteHandler(Module) != NULL;
        case Ndis6FilterCancelSend:
            return Ndis6FilterGetCancelSendHandler(Module) != NULL;
        case Ndis6FilterReceive:
            return Ndis6FilterGetReceiveHandler(Module) != NULL;
        case Ndis6FilterReturn:
            return Ndis6FilterGetReturnHandler(Module) != NULL;
        case Ndis6FilterOidRequest:
            return Characteristics->OidRequestHandler != NULL;
        case Ndis6FilterOidRequestComplete:
            return Characteristics->OidRequestCompleteHandler != NULL;
        case Ndis6FilterDirectOidRequest:
            return Characteristics->DirectOidRequestHandler != NULL;
        case Ndis6FilterDirectOidRequestComplete:
            return Characteristics->DirectOidRequestCompleteHandler != NULL;
        case Ndis6FilterStatus:
            return Characteristics->StatusHandler != NULL;
        case Ndis6FilterDevicePnPEvent:
            return Characteristics->DevicePnPEventNotifyHandler != NULL;
        case Ndis6FilterNetPnPEvent:
            return Characteristics->NetPnPEventHandler != NULL;
        default:
            return FALSE;
    }
}

static PNDIS6_FILTER_MODULE
Ndis6FilterChainFindLocked(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _In_ PLIST_ENTRY FirstEntry,
    _In_ BOOLEAN WalkDown,
    _In_ NDIS6_FILTER_OPERATION Operation)
{
    PLIST_ENTRY Entry;

    for (Entry = FirstEntry;
         Entry != &Ext->FilterModuleList;
         Entry = WalkDown ? Entry->Flink : Entry->Blink)
    {
        PNDIS6_FILTER_MODULE Candidate =
            CONTAINING_RECORD(Entry, NDIS6_FILTER_MODULE, ListEntry);

        if (Ndis6FilterSupportsOperation(Candidate, Operation) &&
            Ndis6ReferenceFilterModule(Candidate))
        {
            return Candidate;
        }
    }

    return NULL;
}

/* The returned module owns one rundown reference. Detach marks the module
 * closing and drains that reference before unlinking it from this list. */
static PNDIS6_FILTER_MODULE
Ndis6FilterChainFirst(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _In_ BOOLEAN WalkDown,
    _In_ NDIS6_FILTER_OPERATION Operation)
{
    KIRQL OldIrql;
    PNDIS6_FILTER_MODULE Module;

    if (Ext == NULL)
        return NULL;

    KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
    Module = Ndis6FilterChainFindLocked(Ext, WalkDown ? Ext->FilterModuleList.Flink : Ext->FilterModuleList.Blink, WalkDown, Operation);
    KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);
    return Module;
}

static PNDIS6_FILTER_MODULE
Ndis6FilterChainAdjacent(
    _In_ PNDIS6_FILTER_MODULE Module,
    _In_ BOOLEAN WalkDown,
    _In_ NDIS6_FILTER_OPERATION Operation)
{
    PNDIS6_ADAPTER_EXT Ext;
    KIRQL OldIrql;
    PNDIS6_FILTER_MODULE Adjacent = NULL;

    if (Module == NULL || Module->Adapter == NULL)
        return NULL;

    Ext = NDIS6_EXT(Module->Adapter);
    if (Ext == NULL)
        return NULL;

    KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
    if (Module->ListEntry.Flink != &Module->ListEntry &&
        Module->ListEntry.Blink != &Module->ListEntry)
    {
        Adjacent = Ndis6FilterChainFindLocked(Ext, WalkDown ? Module->ListEntry.Flink : Module->ListEntry.Blink, WalkDown, Operation);
    }
    KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);
    return Adjacent;
}

/* ============================================================================
 *  Bridge dispatch entry points — called by the bridge's TX/RX glue
 *  (60thunk_tx.c, 60thunk_rx.c) to inject NBLs into the filter chain.
 *  If no filters are attached the bridge calls the terminal handler
 *  directly; the chain walk only happens when at least one filter is
 *  installed.
 * ============================================================================ */

VOID
Ndis6FilterDispatchSend(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PNDIS6_ADAPTER_EXT   Ext;
    PNDIS6_FILTER_MODULE Top;

    if (Adapter == NULL || NetBufferList == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    Top = Ndis6FilterChainFirst(Ext, TRUE, Ndis6FilterSend);
    if (Top == NULL)
    {
        Ndis6FilterTerminalSend(Adapter, NetBufferList, PortNumber, SendFlags);
        return;
    }

    Ndis6FilterGetSendHandler(Top)(Top->FilterModuleContext, NetBufferList, PortNumber, SendFlags);
    Ndis6DereferenceFilterModule(Top);
}

VOID
Ndis6FilterDispatchSendComplete(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ ULONG            SendCompleteFlags)
{
    PNDIS6_ADAPTER_EXT   Ext;
    PNDIS6_FILTER_MODULE Bottom;

    if (Adapter == NULL || NetBufferList == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    Bottom = Ndis6FilterChainFirst(Ext, FALSE, Ndis6FilterSendComplete);
    if (Bottom == NULL)
    {
        Ndis6FilterTerminalSendComplete(Adapter, NetBufferList, SendCompleteFlags);
        return;
    }

    Ndis6FilterGetSendCompleteHandler(Bottom)(Bottom->FilterModuleContext, NetBufferList, SendCompleteFlags);
    Ndis6DereferenceFilterModule(Bottom);
}

VOID
Ndis6FilterDispatchReceive(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNET_BUFFER_LIST  NetBufferLists,
    _In_ NDIS_PORT_NUMBER  PortNumber,
    _In_ ULONG             NumberOfNetBufferLists,
    _In_ ULONG             ReceiveFlags)
{
    PNDIS6_ADAPTER_EXT   Ext;
    PNDIS6_FILTER_MODULE Bottom;

    if (Adapter == NULL || NetBufferLists == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    Bottom = Ndis6FilterChainFirst(Ext, FALSE, Ndis6FilterReceive);
    if (Bottom == NULL)
    {
        Ndis6FilterTerminalReceive(Adapter, NetBufferLists, PortNumber, NumberOfNetBufferLists, ReceiveFlags);
        return;
    }

    Ndis6FilterGetReceiveHandler(Bottom)(Bottom->FilterModuleContext, NetBufferLists, PortNumber, NumberOfNetBufferLists, ReceiveFlags);
    Ndis6DereferenceFilterModule(Bottom);
}

VOID
Ndis6FilterDispatchReturn(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ ULONG             ReturnFlags)
{
    PNDIS6_ADAPTER_EXT   Ext;
    PNDIS6_FILTER_MODULE Top;

    if (Adapter == NULL || NetBufferList == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    Top = Ndis6FilterChainFirst(Ext, TRUE, Ndis6FilterReturn);
    if (Top == NULL)
    {
        Ndis6FilterTerminalReturn(Adapter, NetBufferList, ReturnFlags);
        return;
    }

    Ndis6FilterGetReturnHandler(Top)(Top->FilterModuleContext, NetBufferList, ReturnFlags);
    Ndis6DereferenceFilterModule(Top);
}

VOID
Ndis6FilterDispatchCancelSend(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PVOID CancelId)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_FILTER_MODULE Top;

    if (Adapter == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    Top = Ndis6FilterChainFirst(Ext, TRUE, Ndis6FilterCancelSend);
    if (Top == NULL)
    {
        Ndis6FilterTerminalCancelSend(Adapter, CancelId);
        return;
    }

    Ndis6FilterGetCancelSendHandler(Top)(Top->FilterModuleContext, CancelId);
    Ndis6DereferenceFilterModule(Top);
}

VOID
Ndis6FilterDispatchStatus(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS_STATUS_INDICATION StatusIndication)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_FILTER_MODULE Bottom;

    if (Adapter == NULL || StatusIndication == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    Bottom = Ndis6FilterChainFirst(Ext, FALSE, Ndis6FilterStatus);
    if (Bottom == NULL)
    {
        Ndis6FilterTerminalStatus(Adapter, StatusIndication);
        return;
    }

    Bottom->DriverBlock->Characteristics.StatusHandler(Bottom->FilterModuleContext, StatusIndication);
    Ndis6DereferenceFilterModule(Bottom);
}

VOID
Ndis6FilterDispatchDevicePnPEvent(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_FILTER_MODULE Top;

    if (Adapter == NULL || NetDevicePnPEvent == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    Top = Ndis6FilterChainFirst(Ext, TRUE, Ndis6FilterDevicePnPEvent);
    if (Top == NULL)
    {
        Ndis6FilterTerminalDevicePnPEvent(Adapter, NetDevicePnPEvent);
        return;
    }

    Top->DriverBlock->Characteristics.DevicePnPEventNotifyHandler(Top->FilterModuleContext, NetDevicePnPEvent);
    Ndis6DereferenceFilterModule(Top);
}

NDIS_STATUS
Ndis6FilterDispatchNetPnPEvent(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_FILTER_MODULE Bottom;
    NDIS_STATUS Status;

    if (Adapter == NULL || NetPnPEventNotification == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    Bottom = Ndis6FilterChainFirst(Ext, FALSE, Ndis6FilterNetPnPEvent);
    if (Bottom == NULL)
    {
        return Ndis6FilterTerminalNetPnPEvent(Adapter, NetPnPEventNotification);
    }

    Status = Bottom->DriverBlock->Characteristics.NetPnPEventHandler(Bottom->FilterModuleContext, NetPnPEventNotification);
    Ndis6DereferenceFilterModule(Bottom);
    return Status;
}

/* ============================================================================
 *  NdisF* helper API — what filters call to push NBLs through the chain
 *
 *  In each helper, NdisFilterHandle is the (NDIS_HANDLE)Module pointer the
 *  bridge gave the filter at AttachHandler time. The filter rounds-trips
 *  it back so we can locate the filter's position in the chain and
 *  forward to the next adjacent filter (or to the terminal handler).
 * ============================================================================ */

VOID
NTAPI
NdisFSendNetBufferLists(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ NDIS_PORT_NUMBER  PortNumber,
    _In_ ULONG             SendFlags)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_MODULE Next;
    PLOGICAL_ADAPTER Adapter;
    LONG State;

    if (NetBufferList == NULL || !Ndis6ReferenceFilterModule(Module))
        return;

    Adapter = Module->Adapter;
    State = InterlockedCompareExchange(&Module->State, 0, 0);
    if (Adapter == NULL ||
        (State != NDIS6_FILTER_STATE_RUNNING &&
         State != NDIS6_FILTER_STATE_PAUSING))
    {
        Ndis6DereferenceFilterModule(Module);
        return;
    }

    Next = Ndis6FilterChainAdjacent(Module, TRUE, Ndis6FilterSend);
    if (Next != NULL)
    {
        Ndis6FilterGetSendHandler(Next)(Next->FilterModuleContext, NetBufferList, PortNumber, SendFlags);
        Ndis6DereferenceFilterModule(Next);
    }
    else
    {
        Ndis6FilterTerminalSend(Adapter, NetBufferList, PortNumber, SendFlags);
    }

    Ndis6DereferenceFilterModule(Module);
}

VOID
NTAPI
NdisFSendNetBufferListsComplete(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ ULONG             SendCompleteFlags)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_MODULE Prev;
    PLOGICAL_ADAPTER Adapter;

    if (NetBufferList == NULL || !Ndis6ReferenceFilterModule(Module))
        return;

    Adapter = Module->Adapter;
    if (Adapter == NULL)
    {
        Ndis6DereferenceFilterModule(Module);
        return;
    }

    Prev = Ndis6FilterChainAdjacent(Module, FALSE, Ndis6FilterSendComplete);
    if (Prev != NULL)
    {
        Ndis6FilterGetSendCompleteHandler(Prev)(Prev->FilterModuleContext, NetBufferList, SendCompleteFlags);
        Ndis6DereferenceFilterModule(Prev);
    }
    else
    {
        Ndis6FilterTerminalSendComplete(Adapter, NetBufferList, SendCompleteFlags);
    }

    Ndis6DereferenceFilterModule(Module);
}

VOID
NTAPI
NdisFIndicateReceiveNetBufferLists(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ NDIS_PORT_NUMBER  PortNumber,
    _In_ ULONG             NumberOfNetBufferLists,
    _In_ ULONG             ReceiveFlags)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_MODULE Prev;
    PLOGICAL_ADAPTER Adapter;

    if (NetBufferList == NULL || !Ndis6ReferenceFilterModule(Module))
        return;

    Adapter = Module->Adapter;
    if (Adapter == NULL ||
        InterlockedCompareExchange(&Module->State, 0, 0) !=
            NDIS6_FILTER_STATE_RUNNING)
    {
        Ndis6DereferenceFilterModule(Module);
        return;
    }

    Prev = Ndis6FilterChainAdjacent(Module, FALSE, Ndis6FilterReceive);
    if (Prev != NULL)
    {
        Ndis6FilterGetReceiveHandler(Prev)(Prev->FilterModuleContext, NetBufferList, PortNumber, NumberOfNetBufferLists, ReceiveFlags);
        Ndis6DereferenceFilterModule(Prev);
    }
    else
    {
        Ndis6FilterTerminalReceive(Adapter, NetBufferList, PortNumber, NumberOfNetBufferLists, ReceiveFlags);
    }

    Ndis6DereferenceFilterModule(Module);
}

VOID
NTAPI
NdisFReturnNetBufferLists(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNET_BUFFER_LIST  NetBufferList,
    _In_ ULONG             ReturnFlags)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_MODULE Next;
    PLOGICAL_ADAPTER Adapter;

    if (NetBufferList == NULL || !Ndis6ReferenceFilterModule(Module))
        return;

    Adapter = Module->Adapter;
    if (Adapter == NULL)
    {
        Ndis6DereferenceFilterModule(Module);
        return;
    }

    Next = Ndis6FilterChainAdjacent(Module, TRUE, Ndis6FilterReturn);
    if (Next != NULL)
    {
        Ndis6FilterGetReturnHandler(Next)(Next->FilterModuleContext, NetBufferList, ReturnFlags);
        Ndis6DereferenceFilterModule(Next);
    }
    else
    {
        Ndis6FilterTerminalReturn(Adapter, NetBufferList, ReturnFlags);
    }

    Ndis6DereferenceFilterModule(Module);
}

VOID
NTAPI
NdisFCancelSendNetBufferLists(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ PVOID CancelId)
{
    PNDIS6_FILTER_MODULE Module = NdisFilterHandle;
    PNDIS6_FILTER_MODULE Next;
    PLOGICAL_ADAPTER Adapter;

    if (!Ndis6ReferenceFilterModule(Module))
        return;

    Adapter = Module->Adapter;
    if (Adapter == NULL)
    {
        Ndis6DereferenceFilterModule(Module);
        return;
    }

    Next = Ndis6FilterChainAdjacent(Module, TRUE, Ndis6FilterCancelSend);
    if (Next != NULL)
    {
        Ndis6FilterGetCancelSendHandler(Next)(Next->FilterModuleContext, CancelId);
        Ndis6DereferenceFilterModule(Next);
    }
    else
    {
        Ndis6FilterTerminalCancelSend(Adapter, CancelId);
    }

    Ndis6DereferenceFilterModule(Module);
}

VOID
NTAPI
NdisFIndicateStatus(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ PNDIS_STATUS_INDICATION StatusIndication)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_MODULE Previous;
    PLOGICAL_ADAPTER Adapter;

    if (StatusIndication == NULL || !Ndis6ReferenceFilterModule(Module))
        return;

    Adapter = Module->Adapter;
    if (Adapter == NULL)
    {
        Ndis6DereferenceFilterModule(Module);
        return;
    }

    Previous = Ndis6FilterChainAdjacent(Module, FALSE, Ndis6FilterStatus);
    if (Previous != NULL)
    {
        Previous->DriverBlock->Characteristics.StatusHandler(Previous->FilterModuleContext, StatusIndication);
        Ndis6DereferenceFilterModule(Previous);
    }
    else
    {
        Ndis6FilterTerminalStatus(Adapter, StatusIndication);
    }

    Ndis6DereferenceFilterModule(Module);
}

VOID
NTAPI
NdisFDevicePnPEventNotify(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_MODULE Next;
    PLOGICAL_ADAPTER Adapter;

    if (NetDevicePnPEvent == NULL || !Ndis6ReferenceFilterModule(Module))
        return;

    Adapter = Module->Adapter;
    if (Adapter == NULL)
    {
        Ndis6DereferenceFilterModule(Module);
        return;
    }

    Next = Ndis6FilterChainAdjacent(Module, TRUE, Ndis6FilterDevicePnPEvent);
    if (Next != NULL)
    {
        Next->DriverBlock->Characteristics.DevicePnPEventNotifyHandler(Next->FilterModuleContext, NetDevicePnPEvent);
        Ndis6DereferenceFilterModule(Next);
    }
    else
    {
        Ndis6FilterTerminalDevicePnPEvent(Adapter, NetDevicePnPEvent);
    }

    Ndis6DereferenceFilterModule(Module);
}

NDIS_STATUS
NTAPI
NdisFNetPnPEvent(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_MODULE Previous;
    PLOGICAL_ADAPTER Adapter;
    NDIS_STATUS Status;

    if (NetPnPEventNotification == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;
    if (!Ndis6ReferenceFilterModule(Module))
        return NDIS_STATUS_CLOSING;

    Adapter = Module->Adapter;
    if (Adapter == NULL)
    {
        Ndis6DereferenceFilterModule(Module);
        return NDIS_STATUS_CLOSING;
    }

    Previous = Ndis6FilterChainAdjacent(Module, FALSE, Ndis6FilterNetPnPEvent);
    if (Previous != NULL)
    {
        Status = Previous->DriverBlock->Characteristics.NetPnPEventHandler(Previous->FilterModuleContext, NetPnPEventNotification);
        Ndis6DereferenceFilterModule(Previous);
    }
    else
    {
        Status = Ndis6FilterTerminalNetPnPEvent(Adapter, NetPnPEventNotification);
    }

    Ndis6DereferenceFilterModule(Module);
    return Status;
}

NDIS_STATUS
NTAPI
NdisFSetAttributes(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ NDIS_HANDLE       FilterModuleContext,
    _In_ PNDIS_FILTER_ATTRIBUTES FilterAttributes)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;

    if (Module == NULL || FilterModuleContext == NULL || FilterAttributes == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (Module->Signature != NDIS6_FILTER_MODULE_SIGNATURE ||
        InterlockedCompareExchange(&Module->Closing, FALSE, FALSE) != FALSE)
    {
        return NDIS_STATUS_CLOSING;
    }

    if (Module->SetAttributesCalled ||
        FilterAttributes->Header.Type != NDIS_OBJECT_TYPE_FILTER_ATTRIBUTES ||
        FilterAttributes->Header.Revision != NDIS_FILTER_ATTRIBUTES_REVISION_1 ||
        FilterAttributes->Header.Size != NDIS_SIZEOF_FILTER_ATTRIBUTES_REVISION_1 ||
        FilterAttributes->Flags != 0)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Module->FilterModuleContext = FilterModuleContext;
    Module->SetAttributesCalled = TRUE;
    Module->Flags = FilterAttributes->Flags;
    Module->AttributesValid = TRUE;
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NdisSetOptionalHandlers(
    _In_ NDIS_HANDLE NdisHandle,
    _In_ PNDIS_DRIVER_OPTIONAL_HANDLERS OptionalHandlers)
{
    PNDIS6_DRIVER_BLOCK MiniportDriver = (PNDIS6_DRIVER_BLOCK)NdisHandle;
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisHandle;
    PNDIS_FILTER_PARTIAL_CHARACTERISTICS Partial;

    if (NdisHandle == NULL || OptionalHandlers == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (MiniportDriver->Signature == NDIS6_DRIVER_BLOCK_SIGNATURE)
    {
        switch (OptionalHandlers->Header.Type)
        {
            case NDIS_OBJECT_TYPE_MINIPORT_PNP_CHARACTERISTICS:
            {
                PNDIS_MINIPORT_PNP_CHARACTERISTICS Pnp =
                    (PNDIS_MINIPORT_PNP_CHARACTERISTICS)OptionalHandlers;

                if (Pnp->Header.Revision !=
                        NDIS_MINIPORT_PNP_CHARACTERISTICS_REVISION_1)
                {
                    return NDIS_STATUS_BAD_VERSION;
                }

                if (Pnp->Header.Size !=
                        NDIS_SIZEOF_MINIPORT_PNP_CHARACTERISTICS_REVISION_1)
                {
                    return NDIS_STATUS_INVALID_LENGTH;
                }

                if (Pnp->Flags != 0)
                    return NDIS_STATUS_INVALID_PARAMETER;

                RtlZeroMemory(&MiniportDriver->PnpCharacteristics, sizeof(MiniportDriver->PnpCharacteristics));
                RtlCopyMemory(&MiniportDriver->PnpCharacteristics, Pnp, Pnp->Header.Size);
                MiniportDriver->PnpCharacteristicsValid = TRUE;
                return NDIS_STATUS_SUCCESS;
            }

#if NDIS_SUPPORT_NDIS630
            case NDIS_OBJECT_TYPE_MINIPORT_SS_CHARACTERISTICS:
            {
                PNDIS_MINIPORT_SS_CHARACTERISTICS SelectiveSuspend =
                    (PNDIS_MINIPORT_SS_CHARACTERISTICS)OptionalHandlers;

                if (MiniportDriver->Characteristics.MinorNdisVersion < 30)
                    return NDIS_STATUS_NOT_SUPPORTED;

#if NDIS_SUPPORT_NDIS680
                /* Windows does not allow one miniport to expose selective
                 * suspend and the synchronous-OID interface together. */
                if (MiniportDriver->Characteristics.SynchronousOidRequestHandler != NULL)
                    return NDIS_STATUS_NOT_SUPPORTED;
#endif

                if (SelectiveSuspend->Header.Revision !=
                        NDIS_MINIPORT_SS_CHARACTERISTICS_REVISION_1)
                {
                    return NDIS_STATUS_BAD_VERSION;
                }

                if (SelectiveSuspend->Header.Size !=
                        NDIS_SIZEOF_MINIPORT_SS_CHARACTERISTICS_REVISION_1)
                {
                    return NDIS_STATUS_INVALID_LENGTH;
                }

                if (SelectiveSuspend->Flags != 0 ||
                    SelectiveSuspend->IdleNotificationHandler == NULL ||
                    SelectiveSuspend->CancelIdleNotificationHandler == NULL)
                {
                    return NDIS_STATUS_INVALID_PARAMETER;
                }

                RtlZeroMemory(&MiniportDriver->SelectiveSuspendCharacteristics, sizeof(MiniportDriver->SelectiveSuspendCharacteristics));
                RtlCopyMemory(&MiniportDriver->SelectiveSuspendCharacteristics, SelectiveSuspend, SelectiveSuspend->Header.Size);
                MiniportDriver->SelectiveSuspendCharacteristicsValid = TRUE;
                return NDIS_STATUS_SUCCESS;
            }
#endif

            default:
                return NDIS_STATUS_NOT_SUPPORTED;
        }
    }

    /* Filter-driver SetOptions has no optional filter-driver services. The
     * partial-characteristics object is valid only for a filter-module handle
     * from FilterSetModuleOptions. */
    if (Module->Signature != NDIS6_FILTER_MODULE_SIGNATURE)
        return NDIS_STATUS_NOT_SUPPORTED;

    if (!Ndis6ReferenceFilterModule(Module))
        return NDIS_STATUS_CLOSING;

    if (InterlockedCompareExchange(&Module->State, 0, 0) !=
            NDIS6_FILTER_STATE_PAUSED ||
        OptionalHandlers->Header.Type !=
            NDIS_OBJECT_TYPE_FILTER_PARTIAL_CHARACTERISTICS ||
        OptionalHandlers->Header.Revision !=
            NDIS_FILTER_PARTIAL_CHARACTERISTICS_REVISION_1 ||
        OptionalHandlers->Header.Size !=
            NDIS_SIZEOF_FILTER_PARTIAL_CHARACTERISTICS_REVISION_1)
    {
        Ndis6DereferenceFilterModule(Module);
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Partial = (PNDIS_FILTER_PARTIAL_CHARACTERISTICS)OptionalHandlers;
    if (Partial->Flags != 0 ||
        ((Partial->ReceiveNetBufferListsHandler != NULL ||
          Partial->ReturnNetBufferListsHandler != NULL) &&
         Module->DriverBlock->Characteristics.StatusHandler == NULL))
    {
        Ndis6DereferenceFilterModule(Module);
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    RtlCopyMemory(&Module->PartialCharacteristics, Partial, sizeof(Module->PartialCharacteristics));
    Module->PartialCharacteristicsValid = TRUE;
    Ndis6DereferenceFilterModule(Module);
    return NDIS_STATUS_SUCCESS;
}

#if NDIS_SUPPORT_NDIS680

typedef struct _NDIS6_FILTER_SYNC_OID_FRAME
{
    PNDIS6_FILTER_MODULE Module;
    PVOID CallContext;
} NDIS6_FILTER_SYNC_OID_FRAME, *PNDIS6_FILTER_SYNC_OID_FRAME;

#define NDIS6_FILTER_SYNC_OID_TAG 'sONn'

static NDIS_STATUS
Ndis6FilterSnapshotSynchronousOidModules(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_opt_ PNDIS6_FILTER_MODULE StartAfter,
    _Outptr_result_buffer_(*FrameCount) PNDIS6_FILTER_SYNC_OID_FRAME *FramesOut,
    _Out_ PSIZE_T FrameCount)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);
    PNDIS6_FILTER_SYNC_OID_FRAME Frames;
    PLIST_ENTRY Entry;
    SIZE_T Capacity = 0;
    SIZE_T Count = 0;
    SIZE_T ActualCount;
    KIRQL OldIrql;

    *FramesOut = NULL;
    *FrameCount = 0;
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

Retry:
    Capacity = 0;
    Count = 0;
    KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
    if (StartAfter != NULL &&
        (StartAfter->Adapter != Adapter ||
         StartAfter->ListEntry.Flink == NULL ||
         StartAfter->ListEntry.Blink == NULL ||
         StartAfter->ListEntry.Flink == &StartAfter->ListEntry))
    {
        KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);
        return NDIS_STATUS_CLOSING;
    }

    Entry = StartAfter != NULL
        ? StartAfter->ListEntry.Flink
        : Ext->FilterModuleList.Flink;
    for (; Entry != &Ext->FilterModuleList; Entry = Entry->Flink)
        Capacity++;
    KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);

    if (Capacity == 0)
        return NDIS_STATUS_SUCCESS;
    if (Capacity > MAXULONG_PTR / sizeof(*Frames))
        return NDIS_STATUS_RESOURCES;

    Frames = ExAllocatePoolWithTag(NonPagedPool, Capacity * sizeof(*Frames), NDIS6_FILTER_SYNC_OID_TAG);
    if (Frames == NULL)
        return NDIS_STATUS_RESOURCES;
    RtlZeroMemory(Frames, Capacity * sizeof(*Frames));

    KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
    if (StartAfter != NULL &&
        (StartAfter->Adapter != Adapter ||
         StartAfter->ListEntry.Flink == NULL ||
         StartAfter->ListEntry.Blink == NULL ||
         StartAfter->ListEntry.Flink == &StartAfter->ListEntry))
    {
        KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);
        ExFreePoolWithTag(Frames, NDIS6_FILTER_SYNC_OID_TAG);
        return NDIS_STATUS_CLOSING;
    }

    Entry = StartAfter != NULL
        ? StartAfter->ListEntry.Flink
        : Ext->FilterModuleList.Flink;
    ActualCount = 0;
    for (; Entry != &Ext->FilterModuleList; Entry = Entry->Flink)
        ActualCount++;
    if (ActualCount > Capacity)
    {
        KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);
        ExFreePoolWithTag(Frames, NDIS6_FILTER_SYNC_OID_TAG);
        goto Retry;
    }

    Entry = StartAfter != NULL
        ? StartAfter->ListEntry.Flink
        : Ext->FilterModuleList.Flink;
    for (; Entry != &Ext->FilterModuleList && Count < Capacity;
         Entry = Entry->Flink)
    {
        PNDIS6_FILTER_MODULE Module =
            CONTAINING_RECORD(Entry, NDIS6_FILTER_MODULE, ListEntry);

        if (Ndis6ReferenceFilterModule(Module))
            Frames[Count++].Module = Module;
    }
    KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);

    if (Count == 0)
    {
        ExFreePoolWithTag(Frames, NDIS6_FILTER_SYNC_OID_TAG);
        return NDIS_STATUS_SUCCESS;
    }

    *FramesOut = Frames;
    *FrameCount = Count;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Ndis6FilterIssueSynchronousOidRequest(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_opt_ PNDIS6_FILTER_MODULE StartAfter,
    _Inout_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_FILTER_SYNC_OID_FRAME Frames = NULL;
    PNDIS6_ADAPTER_EXT Ext;
    SIZE_T FrameCount = 0;
    SIZE_T PassedCount = 0;
    SIZE_T Index;
    NDIS_STATUS Status;

    if (Adapter == NULL || !Adapter->IsNdis6 || OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL)
        return NDIS_STATUS_CLOSING;

    Status = Ndis6FilterSnapshotSynchronousOidModules(Adapter, StartAfter, &Frames, &FrameCount);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    for (Index = 0; Index < FrameCount; Index++)
    {
        FILTER_SYNCHRONOUS_OID_REQUEST_HANDLER Handler =
            Frames[Index].Module->DriverBlock->Characteristics.SynchronousOidRequestHandler;

        Status = Handler != NULL
            ? Handler(Frames[Index].Module->FilterModuleContext, OidRequest, &Frames[Index].CallContext)
            : NDIS_STATUS_SUCCESS;

        if (Status == NDIS_STATUS_ALREADY_COMPLETE)
        {
            Status = NDIS_STATUS_SUCCESS;
            break;
        }
        if (Status == NDIS_STATUS_PENDING)
        {
            DbgPrint("NDIS6: filter illegally pended a synchronous OID\n");
            Status = NDIS_STATUS_INVALID_STATE;
        }
        if (Status != NDIS_STATUS_SUCCESS)
            break;

        /* Completion is guaranteed exactly for filters whose request stage
         * succeeded. A completion-only filter participates with NULL context. */
        PassedCount++;
    }

    if (PassedCount == FrameCount)
    {
        MINIPORT_SYNCHRONOUS_OID_REQUEST_HANDLER Handler =
            Ext->DriverBlock->Characteristics.SynchronousOidRequestHandler;

        Status = Handler != NULL
            ? Handler(Ext->MiniportAdapterContext, OidRequest)
            : NDIS_STATUS_NOT_SUPPORTED;
        if (Status == NDIS_STATUS_PENDING ||
            Status == NDIS_STATUS_REQUEST_ABORTED ||
            Status == NDIS_STATUS_ALREADY_COMPLETE)
        {
            DbgPrint("NDIS6: miniport returned an illegal synchronous OID status 0x%08lx\n", (ULONG)Status);
            Status = NDIS_STATUS_INVALID_STATE;
        }
    }

    while (PassedCount != 0)
    {
        FILTER_SYNCHRONOUS_OID_REQUEST_COMPLETE_HANDLER CompleteHandler;

        PassedCount--;
        CompleteHandler = Frames[PassedCount].Module->DriverBlock->Characteristics.SynchronousOidRequestCompleteHandler;
        if (CompleteHandler != NULL)
        {
            CompleteHandler(Frames[PassedCount].Module->FilterModuleContext, OidRequest, &Status, Frames[PassedCount].CallContext);
            if (Status == NDIS_STATUS_PENDING ||
                Status == NDIS_STATUS_ALREADY_COMPLETE)
            {
                DbgPrint("NDIS6: filter produced an illegal synchronous OID completion status\n");
                Status = NDIS_STATUS_INVALID_STATE;
            }
        }
    }

    for (Index = 0; Index < FrameCount; Index++)
        Ndis6DereferenceFilterModule(Frames[Index].Module);
    if (Frames != NULL)
        ExFreePoolWithTag(Frames, NDIS6_FILTER_SYNC_OID_TAG);
    return Status;
}

NDIS_STATUS
Ndis6FilterDispatchSynchronousOidRequest(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    return Ndis6FilterIssueSynchronousOidRequest(Adapter, NULL, OidRequest);
}

NDIS_STATUS
NTAPI
NdisFSynchronousOidRequest(
    _In_ NDIS_HANDLE NdisFilterModuleHandle,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_FILTER_MODULE Module = NdisFilterModuleHandle;
    NDIS_STATUS Status;

    if (OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;
    if (!Ndis6ReferenceFilterModule(Module))
        return NDIS_STATUS_CLOSING;

    Status = Ndis6FilterIssueSynchronousOidRequest(Module->Adapter, Module, OidRequest);
    Ndis6DereferenceFilterModule(Module);
    return Status;
}

#endif /* NDIS_SUPPORT_NDIS680 */

static VOID
NTAPI
Ndis6FilterRestartWorker(
    _In_ PVOID Context)
{
    PNDIS6_FILTER_MODULE Module = Context;
    PLOGICAL_ADAPTER Adapter = Module->Adapter;
    PNDIS6_ADAPTER_EXT Ext = Adapter != NULL ? NDIS6_EXT(Adapter) : NULL;
    NDIS_STATUS Status = NDIS_STATUS_CLOSING;
    NDIS_STATUS RestartStatus;

    if (Ext != NULL)
    {
        (VOID)KeWaitForSingleObject(&Ext->StackTransitionMutex, Executive, KernelMode, FALSE, NULL);

        if (Module->Signature == NDIS6_FILTER_MODULE_SIGNATURE &&
            InterlockedCompareExchange(&Module->Closing, FALSE, FALSE) == FALSE &&
            InterlockedCompareExchange(&Module->State, 0, 0) ==
                NDIS6_FILTER_STATE_RUNNING)
        {
            Status = Ndis6PauseDriverStackLocked(Adapter);
            if (Status == NDIS_STATUS_SUCCESS &&
                Module->DriverBlock->Characteristics.SetFilterModuleOptionsHandler != NULL)
            {
                Status = Module->DriverBlock->Characteristics.SetFilterModuleOptionsHandler(Module->FilterModuleContext);
            }

            /* Restore the stack even if an optional-handler update failed; a
             * failed run-time update must not strand a working adapter paused. */
            RestartStatus = Ndis6RestartDriverStackLocked(Adapter);
            if (Status == NDIS_STATUS_SUCCESS)
                Status = RestartStatus;
        }

        KeReleaseMutex(&Ext->StackTransitionMutex, FALSE);
    }

    if (Status != NDIS_STATUS_SUCCESS && Status != NDIS_STATUS_CLOSING)
    {
        DbgPrint("NDIS6: filter-requested restart failed 0x%08lx\n", (ULONG)Status);
    }

    InterlockedExchange(&Module->RestartWorkQueued, 0);
    Ndis6DereferenceFilterModule(Module);
}

NDIS_STATUS
NTAPI
NdisFRestartFilter(
    _In_ NDIS_HANDLE NdisFilterHandle)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;

    if (!Ndis6ReferenceFilterModule(Module))
        return NDIS_STATUS_CLOSING;

    if (InterlockedCompareExchange(&Module->State, 0, 0) !=
        NDIS6_FILTER_STATE_RUNNING)
    {
        Ndis6DereferenceFilterModule(Module);
        return NDIS_STATUS_PAUSED;
    }

    if (InterlockedCompareExchange(&Module->RestartWorkQueued, 1, 0) != 0)
    {
        Ndis6DereferenceFilterModule(Module);
        return NDIS_STATUS_SUCCESS;
    }

    ExInitializeWorkItem(&Module->RestartWorkItem, Ndis6FilterRestartWorker, Module);
    ExQueueWorkItem(&Module->RestartWorkItem, DelayedWorkQueue);
    /* The rundown reference acquired above is transferred to the worker. */
    return NDIS_STATUS_SUCCESS;
}

/* ============================================================================
 *  Filter OID ownership
 *
 *  A pending OID must complete through the same filter modules that saw the
 *  request on the way down. Each frame holds a module rundown reference, so
 *  filter detach cannot free the module while an asynchronous OID still owns
 *  it. The context pointer and marker live in the first two NDIS-owned request
 *  slots; filter clones share the same context with a separate reference.
 * ============================================================================ */

typedef struct _NDIS6_FILTER_OID_FRAME
{
    struct _NDIS6_FILTER_OID_FRAME *Previous;
    PNDIS6_FILTER_MODULE Module;
    PVOID RequestId;
    volatile LONG References;
    BOOLEAN CancelIndicated;
    BOOLEAN CancelForwarded;
} NDIS6_FILTER_OID_FRAME, *PNDIS6_FILTER_OID_FRAME;

typedef struct _NDIS6_FILTER_OID_CONTEXT
{
    ULONG Signature;
    LIST_ENTRY ListEntry;
    KSPIN_LOCK Lock;
    volatile LONG References;
    PLOGICAL_ADAPTER Adapter;
    PNDIS_OID_REQUEST OriginalRequest;
    NDIS_HANDLE OriginHandle;
    PVOID OriginRequestId;
    PNDIS6_FILTER_OID_FRAME Top;
    PVOID ActiveRequestId;
    volatile LONG Listed;
    BOOLEAN DirectRequest;
    BOOLEAN FilterOriginated;
    BOOLEAN AtMiniport;
    BOOLEAN OriginCancelIssued;
    BOOLEAN MiniportCancelIssued;
    BOOLEAN Completing;
} NDIS6_FILTER_OID_CONTEXT, *PNDIS6_FILTER_OID_CONTEXT;

#define NDIS6_FILTER_OID_CONTEXT_SIGNATURE 'oFNn'
#define NDIS6_FILTER_OID_CONTEXT_TAG       'cONn'
#define NDIS6_FILTER_OID_FRAME_TAG         'fONn'

static const UCHAR Ndis6FilterOidContextMarker;

static VOID
Ndis6FilterOidReadReserved(
    _In_ PNDIS_OID_REQUEST OidRequest,
    _Out_ PVOID *Slot0,
    _Out_ PVOID *Slot1)
{
    RtlCopyMemory(Slot0, &OidRequest->NdisReserved[0], sizeof(*Slot0));
    RtlCopyMemory(Slot1, &OidRequest->NdisReserved[sizeof(PVOID)], sizeof(*Slot1));
}

static VOID
Ndis6FilterOidWriteReserved(
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_opt_ PVOID Slot0,
    _In_opt_ PVOID Slot1)
{
    RtlCopyMemory(&OidRequest->NdisReserved[0], &Slot0, sizeof(Slot0));
    RtlCopyMemory(&OidRequest->NdisReserved[sizeof(PVOID)], &Slot1, sizeof(Slot1));
}

static PNDIS6_FILTER_OID_CONTEXT
Ndis6FilterOidGetContext(
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PVOID Slot0;
    PVOID Slot1;

    Ndis6FilterOidReadReserved(OidRequest, &Slot0, &Slot1);
    if (Slot1 != (PVOID)&Ndis6FilterOidContextMarker || Slot0 == NULL)
        return NULL;

    return (PNDIS6_FILTER_OID_CONTEXT)Slot0;
}

static VOID
Ndis6FilterOidReferenceContext(
    _In_ PNDIS6_FILTER_OID_CONTEXT Context)
{
    InterlockedIncrement(&Context->References);
}

static VOID
Ndis6FilterOidDereferenceContext(
    _In_ PNDIS6_FILTER_OID_CONTEXT Context)
{
    if (InterlockedDecrement(&Context->References) == 0)
    {
        Context->Signature = 0;
        ExFreePoolWithTag(Context, NDIS6_FILTER_OID_CONTEXT_TAG);
    }
}

static VOID
Ndis6FilterOidReferenceFrame(
    _In_ PNDIS6_FILTER_OID_FRAME Frame)
{
    InterlockedIncrement(&Frame->References);
}

static VOID
Ndis6FilterOidDereferenceFrame(
    _In_ PNDIS6_FILTER_OID_FRAME Frame)
{
    if (InterlockedDecrement(&Frame->References) == 0)
    {
        Ndis6DereferenceFilterModule(Frame->Module);
        ExFreePoolWithTag(Frame, NDIS6_FILTER_OID_FRAME_TAG);
    }
}

static NDIS_STATUS
Ndis6FilterOidPushFrame(
    _In_ PNDIS6_FILTER_OID_CONTEXT Context,
    _In_ PNDIS6_FILTER_MODULE Module,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _Out_opt_ PNDIS6_FILTER_OID_FRAME *FrameOut)
{
    PNDIS6_FILTER_OID_FRAME Frame;
    KIRQL OldIrql;

    if (!Ndis6ReferenceFilterModule(Module))
        return NDIS_STATUS_CLOSING;

    Frame = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Frame), NDIS6_FILTER_OID_FRAME_TAG);
    if (Frame == NULL)
    {
        Ndis6DereferenceFilterModule(Module);
        return NDIS_STATUS_RESOURCES;
    }

    Frame->Module = Module;
    Frame->RequestId = OidRequest->RequestId;
    Frame->References = 1;
    Frame->CancelIndicated = FALSE;
    Frame->CancelForwarded = FALSE;

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    Frame->Previous = Context->Top;
    Context->Top = Frame;
    KeReleaseSpinLock(&Context->Lock, OldIrql);
    if (FrameOut != NULL)
        *FrameOut = Frame;
    return NDIS_STATUS_SUCCESS;
}

static PNDIS6_FILTER_OID_FRAME
Ndis6FilterOidReferenceTopFrame(
    _In_ PNDIS6_FILTER_OID_CONTEXT Context)
{
    PNDIS6_FILTER_OID_FRAME Frame;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    Frame = Context->Top;
    if (Frame != NULL)
        Ndis6FilterOidReferenceFrame(Frame);
    KeReleaseSpinLock(&Context->Lock, OldIrql);
    return Frame;
}

static VOID
Ndis6FilterOidPopFrame(
    _In_ PNDIS6_FILTER_OID_CONTEXT Context,
    _In_ PNDIS6_FILTER_OID_FRAME Frame)
{
    KIRQL OldIrql;
    BOOLEAN Removed = FALSE;

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    if (Context->Top == Frame)
    {
        Context->Top = Frame->Previous;
        Frame->Previous = NULL;
        Removed = TRUE;
    }
    KeReleaseSpinLock(&Context->Lock, OldIrql);

    if (Removed)
        Ndis6FilterOidDereferenceFrame(Frame);
}

static VOID
Ndis6FilterOidReleaseFrames(
    _In_ PNDIS6_FILTER_OID_CONTEXT Context)
{
    for (;;)
    {
        PNDIS6_FILTER_OID_FRAME Frame =
            Ndis6FilterOidReferenceTopFrame(Context);

        if (Frame == NULL)
            break;
        Ndis6FilterOidPopFrame(Context, Frame);
        Ndis6FilterOidDereferenceFrame(Frame);
    }
}

static PNDIS6_FILTER_OID_CONTEXT
Ndis6FilterOidCreateContext(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ BOOLEAN DirectRequest,
    _In_ BOOLEAN FilterOriginated)
{
    PNDIS6_FILTER_OID_CONTEXT Context;
    PNDIS6_PROTOCOL_PENDING_OID Pending;
    PNDIS6_ADAPTER_EXT Ext;
    KIRQL OldIrql;

    Ext = Adapter != NULL ? NDIS6_EXT(Adapter) : NULL;
    if (Ext == NULL)
        return NULL;

    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Context), NDIS6_FILTER_OID_CONTEXT_TAG);
    if (Context == NULL)
        return NULL;

    RtlZeroMemory(Context, sizeof(*Context));
    Context->Signature = NDIS6_FILTER_OID_CONTEXT_SIGNATURE;
    InitializeListHead(&Context->ListEntry);
    KeInitializeSpinLock(&Context->Lock);
    /* One reference belongs to the request and one pins this dispatch call. */
    Context->References = 2;
    Context->Adapter = Adapter;
    Context->OriginalRequest = OidRequest;
    Pending = Ndis6GetPendingOidContext(OidRequest);
    if (Pending != NULL &&
        Pending->Signature == NDIS6_PROTOCOL_PENDING_OID_SIGNATURE)
    {
        Context->OriginHandle = Pending->Binding;
    }
    Context->OriginRequestId = OidRequest->RequestId;
    Context->ActiveRequestId = OidRequest->RequestId;
    Context->Listed = TRUE;
    Context->DirectRequest = DirectRequest;
    Context->FilterOriginated = FilterOriginated;
    Ndis6FilterOidWriteReserved(OidRequest, Context, (PVOID)&Ndis6FilterOidContextMarker);

    KeAcquireSpinLock(&Ext->FilterOidContextListLock, &OldIrql);
    InsertTailList(&Ext->FilterOidContextList, &Context->ListEntry);
    KeReleaseSpinLock(&Ext->FilterOidContextListLock, OldIrql);
    return Context;
}

static VOID
Ndis6FilterOidSetActiveRequest(
    _In_ PNDIS6_FILTER_OID_CONTEXT Context,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ BOOLEAN AtMiniport)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    Context->ActiveRequestId = OidRequest->RequestId;
    Context->AtMiniport = AtMiniport;
    KeReleaseSpinLock(&Context->Lock, OldIrql);
}

static BOOLEAN
Ndis6FilterOidBeginCompletion(
    _In_ PNDIS6_FILTER_OID_CONTEXT Context,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    KIRQL OldIrql;
    BOOLEAN FirstCompletion;

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    FirstCompletion = !Context->Completing;
    Context->Completing = TRUE;
    Context->AtMiniport = FALSE;
    Context->ActiveRequestId = OidRequest->RequestId;
    KeReleaseSpinLock(&Context->Lock, OldIrql);
    return FirstCompletion;
}

static VOID
Ndis6FilterOidDetachContext(
    _In_ PNDIS6_FILTER_OID_CONTEXT Context,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_ADAPTER_EXT Ext;
    KIRQL OldIrql;
    BOOLEAN Removed = FALSE;

    Ext = Context->Adapter != NULL ? NDIS6_EXT(Context->Adapter) : NULL;
    if (Ext != NULL)
    {
        KeAcquireSpinLock(&Ext->FilterOidContextListLock, &OldIrql);
        if (InterlockedCompareExchange(&Context->Listed, FALSE, TRUE) == TRUE)
        {
            RemoveEntryList(&Context->ListEntry);
            InitializeListHead(&Context->ListEntry);
            Removed = TRUE;
        }
        KeReleaseSpinLock(&Ext->FilterOidContextListLock, OldIrql);
    }

    if (!Removed)
        return;

    if (Ndis6FilterOidGetContext(OidRequest) == Context)
        Ndis6FilterOidWriteReserved(OidRequest, NULL, NULL);
    if (Context->OriginalRequest != OidRequest &&
        Ndis6FilterOidGetContext(Context->OriginalRequest) == Context)
    {
        Ndis6FilterOidWriteReserved(Context->OriginalRequest, NULL, NULL);
    }
    Ndis6FilterOidDereferenceContext(Context);
}

typedef struct _NDIS6_CLONED_OID_REQUEST
{
    ULONG Signature;
    ULONG PoolTag;
    NDIS_HANDLE SourceHandle;
    NDIS_OID_REQUEST Request;
} NDIS6_CLONED_OID_REQUEST, *PNDIS6_CLONED_OID_REQUEST;

#define NDIS6_CLONED_OID_REQUEST_SIGNATURE 'lONn'

static BOOLEAN
Ndis6FilterOidReferenceSourceHandle(
    _In_ NDIS_HANDLE SourceHandle,
    _Out_ PBOOLEAN IsFilterModule)
{
    PNDIS6_FILTER_MODULE Module = SourceHandle;
    PNDIS6_PROTOCOL_BINDING Binding = SourceHandle;

    *IsFilterModule = FALSE;
    if (Module != NULL &&
        Module->Signature == NDIS6_FILTER_MODULE_SIGNATURE &&
        Ndis6ReferenceFilterModule(Module))
    {
        *IsFilterModule = TRUE;
        return TRUE;
    }

    return Ndis6ReferenceProtocolBinding(Binding);
}

static VOID
Ndis6FilterOidDereferenceSourceHandle(
    _In_ NDIS_HANDLE SourceHandle,
    _In_ BOOLEAN IsFilterModule)
{
    if (IsFilterModule)
        Ndis6DereferenceFilterModule((PNDIS6_FILTER_MODULE)SourceHandle);
    else
        Ndis6DereferenceProtocolBinding((PNDIS6_PROTOCOL_BINDING)SourceHandle);
}

NDIS_STATUS
NTAPI
NdisAllocateCloneOidRequest(
    _In_ NDIS_HANDLE SourceHandle,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ UINT PoolTag,
    _Out_ PNDIS_OID_REQUEST *ClonedOidRequest)
{
    PNDIS6_CLONED_OID_REQUEST Clone;
    PNDIS6_FILTER_OID_CONTEXT Context;
    BOOLEAN IsFilterModule;
    ULONG CopySize;

    if (ClonedOidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;
    *ClonedOidRequest = NULL;

    if (OidRequest == NULL ||
        OidRequest->Header.Type != NDIS_OBJECT_TYPE_OID_REQUEST)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    if (OidRequest->Header.Revision == NDIS_OID_REQUEST_REVISION_1 &&
        OidRequest->Header.Size == NDIS_SIZEOF_OID_REQUEST_REVISION_1)
    {
        CopySize = NDIS_SIZEOF_OID_REQUEST_REVISION_1;
    }
#if NDIS_SUPPORT_NDIS650
    else if (OidRequest->Header.Revision == NDIS_OID_REQUEST_REVISION_2 &&
             OidRequest->Header.Size == NDIS_SIZEOF_OID_REQUEST_REVISION_2)
    {
        CopySize = NDIS_SIZEOF_OID_REQUEST_REVISION_2;
    }
#endif
    else
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    if (!Ndis6FilterOidReferenceSourceHandle(SourceHandle, &IsFilterModule))
        return NDIS_STATUS_INVALID_PARAMETER;

    Clone = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Clone), PoolTag);
    if (Clone == NULL)
    {
        Ndis6FilterOidDereferenceSourceHandle(SourceHandle, IsFilterModule);
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(Clone, sizeof(*Clone));
    Clone->Signature = NDIS6_CLONED_OID_REQUEST_SIGNATURE;
    Clone->PoolTag = PoolTag;
    Clone->SourceHandle = SourceHandle;
    RtlCopyMemory(&Clone->Request, OidRequest, CopySize);

    Context = Ndis6FilterOidGetContext(OidRequest);
    if (Context != NULL &&
        Context->Signature == NDIS6_FILTER_OID_CONTEXT_SIGNATURE)
    {
        Ndis6FilterOidReferenceContext(Context);
        Ndis6FilterOidWriteReserved(&Clone->Request, Context, (PVOID)&Ndis6FilterOidContextMarker);
    }

    *ClonedOidRequest = &Clone->Request;
    Ndis6FilterOidDereferenceSourceHandle(SourceHandle, IsFilterModule);
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NdisFreeCloneOidRequest(
    _In_ NDIS_HANDLE SourceHandle,
    _In_ PNDIS_OID_REQUEST Request)
{
    PNDIS6_CLONED_OID_REQUEST Clone;
    PNDIS6_FILTER_OID_CONTEXT Context;

    if (Request == NULL)
        return;

    Clone = CONTAINING_RECORD(Request, NDIS6_CLONED_OID_REQUEST, Request);
    if (Clone->Signature != NDIS6_CLONED_OID_REQUEST_SIGNATURE ||
        Clone->SourceHandle != SourceHandle)
    {
        return;
    }

    Context = Ndis6FilterOidGetContext(Request);
    if (Context != NULL &&
        Context->Signature == NDIS6_FILTER_OID_CONTEXT_SIGNATURE)
    {
        Ndis6FilterOidWriteReserved(Request, NULL, NULL);
        Ndis6FilterOidDereferenceContext(Context);
    }

    Clone->Signature = 0;
    ExFreePoolWithTag(Clone, Clone->PoolTag);
}

static VOID
Ndis6FilterContinueOidCompletion(
    _In_ PNDIS6_FILTER_OID_CONTEXT Context,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS Status)
{
    PLOGICAL_ADAPTER Adapter = Context->Adapter;

    for (;;)
    {
        PNDIS6_FILTER_OID_FRAME Frame =
            Ndis6FilterOidReferenceTopFrame(Context);
        PNDIS6_FILTER_MODULE Module;
        FILTER_OID_REQUEST_COMPLETE_HANDLER OidHandler;
        FILTER_DIRECT_OID_REQUEST_COMPLETE_HANDLER DirectHandler;

        if (Frame == NULL)
            break;

        Module = Frame->Module;

        OidHandler = Context->DirectRequest
            ? NULL
            : Module->DriverBlock->Characteristics.OidRequestCompleteHandler;
        DirectHandler = Context->DirectRequest
            ? Module->DriverBlock->Characteristics.DirectOidRequestCompleteHandler
            : NULL;

        if (OidHandler != NULL || DirectHandler != NULL)
        {
            BOOLEAN IsOriginCompletion =
                Context->FilterOriginated && Frame->Previous == NULL;

            /* An OID originated by this filter terminates at the source
             * filter's completion callback. It must not call NdisF*Complete
             * again merely to release NDIS's traversal bookkeeping. */
            if (IsOriginCompletion)
            {
                Ndis6FilterOidPopFrame(Context, Frame);
                Ndis6FilterOidDetachContext(Context, OidRequest);
            }

            if (Context->DirectRequest)
            {
                DirectHandler(Module->FilterModuleContext, OidRequest, Status);
            }
            else
            {
                OidHandler(Module->FilterModuleContext, OidRequest, Status);
            }
            Ndis6FilterOidDereferenceFrame(Frame);
            return;
        }

        Ndis6FilterOidPopFrame(Context, Frame);
        Ndis6FilterOidDereferenceFrame(Frame);
    }

    Ndis6FilterOidDetachContext(Context, OidRequest);
    if (!Context->FilterOriginated)
    {
        if (Context->DirectRequest)
            Ndis6CompleteDirectOidRequestToOrigin(Adapter, OidRequest, Status);
        else
            Ndis6CompleteOidRequestToOrigin(Adapter, OidRequest, Status);
    }
}

/* ============================================================================
 *  Filter OID forward
 *
 *  Ndis6FilterDispatchOidRequest — entry point from 60oid.c when an OID
 *  request needs to walk the filter chain on the way to the miniport.
 *  Calls the topmost filter's OidRequestHandler; the filter does its
 *  inspect/modify work and calls NdisFOidRequest to push the OID down
 *  to the next filter (or to the miniport at the bottom).
 *
 *  Per-request context and frame records preserve the exact downward path so
 *  synchronous and pending requests complete through the same modules in the
 *  reverse direction.
 * ============================================================================ */

NDIS_STATUS
Ndis6FilterDispatchOidRequest(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_ADAPTER_EXT   Ext;
    PNDIS6_FILTER_MODULE Top;
    PNDIS6_FILTER_OID_CONTEXT Context;
    NDIS_STATUS Status;

    if (Adapter == NULL || OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    Top = Ndis6FilterChainFirst(Ext, TRUE, Ndis6FilterOidRequest);
    Context = Ndis6FilterOidCreateContext(Adapter, OidRequest, FALSE, FALSE);
    if (Context == NULL)
    {
        if (Top != NULL)
            Ndis6DereferenceFilterModule(Top);
        return NDIS_STATUS_RESOURCES;
    }

    if (Top != NULL)
    {
        Status = Ndis6FilterOidPushFrame(Context, Top, OidRequest, NULL);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            Ndis6FilterOidDetachContext(Context, OidRequest);
            Ndis6FilterOidDereferenceContext(Context);
            Ndis6DereferenceFilterModule(Top);
            return Status;
        }

        Status = Top->DriverBlock->Characteristics.OidRequestHandler(Top->FilterModuleContext, OidRequest);
        Ndis6DereferenceFilterModule(Top);
    }
    else
    {
        Ndis6FilterOidSetActiveRequest(Context, OidRequest, TRUE);
        Status = Ndis6FilterTerminalOidRequest(Adapter, OidRequest);
    }

    if (Status != NDIS_STATUS_PENDING &&
        Ndis6FilterOidGetContext(OidRequest) == Context)
    {
        Ndis6FilterOidReleaseFrames(Context);
        Ndis6FilterOidDetachContext(Context, OidRequest);
    }

    /* Release the call-stack pin. A pending request retains the request-owned
     * reference until its completion reaches the original caller. */
    Ndis6FilterOidDereferenceContext(Context);
    return Status;
}

NDIS_STATUS
Ndis6FilterTerminalOidRequest(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.OidRequestHandler == NULL)
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    return Ext->DriverBlock->Characteristics.OidRequestHandler(Ext->MiniportAdapterContext, OidRequest);
}

BOOLEAN
Ndis6FilterCompleteOidFromMiniport(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS Status)
{
    PNDIS6_FILTER_OID_CONTEXT Context;

    if (Adapter == NULL || OidRequest == NULL)
        return FALSE;

    Context = Ndis6FilterOidGetContext(OidRequest);
    if (Context == NULL ||
        Context->Signature != NDIS6_FILTER_OID_CONTEXT_SIGNATURE ||
        Context->Adapter != Adapter || Context->DirectRequest)
    {
        return FALSE;
    }

    Ndis6FilterOidReferenceContext(Context);
    if (!Ndis6FilterOidBeginCompletion(Context, OidRequest))
    {
        Ndis6FilterOidDereferenceContext(Context);
        return TRUE;
    }
    Ndis6FilterContinueOidCompletion(Context, OidRequest, Status);
    Ndis6FilterOidDereferenceContext(Context);
    return TRUE;
}

NDIS_STATUS
Ndis6FilterTerminalDirectOidRequest(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.DirectOidRequestHandler == NULL)
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    return Ext->DriverBlock->Characteristics.DirectOidRequestHandler(Ext->MiniportAdapterContext, OidRequest);
}

NDIS_STATUS
Ndis6FilterDispatchDirectOidRequest(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_FILTER_MODULE Top;
    PNDIS6_FILTER_OID_CONTEXT Context;
    NDIS_STATUS Status;

    if (Adapter == NULL || OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    Top = Ndis6FilterChainFirst(Ext, TRUE, Ndis6FilterDirectOidRequest);
    Context = Ndis6FilterOidCreateContext(Adapter, OidRequest, TRUE, FALSE);
    if (Context == NULL)
    {
        if (Top != NULL)
            Ndis6DereferenceFilterModule(Top);
        return NDIS_STATUS_RESOURCES;
    }

    if (Top != NULL)
    {
        Status = Ndis6FilterOidPushFrame(Context, Top, OidRequest, NULL);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            Ndis6FilterOidDetachContext(Context, OidRequest);
            Ndis6FilterOidDereferenceContext(Context);
            Ndis6DereferenceFilterModule(Top);
            return Status;
        }

        Status = Top->DriverBlock->Characteristics.DirectOidRequestHandler(Top->FilterModuleContext, OidRequest);
        Ndis6DereferenceFilterModule(Top);
    }
    else
    {
        Ndis6FilterOidSetActiveRequest(Context, OidRequest, TRUE);
        Status = Ndis6FilterTerminalDirectOidRequest(Adapter, OidRequest);
    }

    if (Status != NDIS_STATUS_PENDING &&
        Ndis6FilterOidGetContext(OidRequest) == Context)
    {
        Ndis6FilterOidReleaseFrames(Context);
        Ndis6FilterOidDetachContext(Context, OidRequest);
    }

    Ndis6FilterOidDereferenceContext(Context);
    return Status;
}

BOOLEAN
Ndis6FilterCompleteDirectOidFromMiniport(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS Status)
{
    PNDIS6_FILTER_OID_CONTEXT Context;

    if (Adapter == NULL || OidRequest == NULL)
        return FALSE;

    Context = Ndis6FilterOidGetContext(OidRequest);
    if (Context == NULL ||
        Context->Signature != NDIS6_FILTER_OID_CONTEXT_SIGNATURE ||
        Context->Adapter != Adapter || !Context->DirectRequest)
    {
        return FALSE;
    }

    Ndis6FilterOidReferenceContext(Context);
    if (!Ndis6FilterOidBeginCompletion(Context, OidRequest))
    {
        Ndis6FilterOidDereferenceContext(Context);
        return TRUE;
    }
    Ndis6FilterContinueOidCompletion(Context, OidRequest, Status);
    Ndis6FilterOidDereferenceContext(Context);
    return TRUE;
}

NDIS_STATUS
NTAPI
NdisFOidRequest(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_MODULE Next;
    PNDIS6_FILTER_OID_CONTEXT Context;
    PNDIS6_FILTER_OID_FRAME CurrentFrame;
    PNDIS6_FILTER_OID_FRAME NextFrame = NULL;
    PLOGICAL_ADAPTER Adapter;
    NDIS_STATUS Status;
    BOOLEAN Originated = FALSE;

    if (OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (!Ndis6ReferenceFilterModule(Module))
        return NDIS_STATUS_CLOSING;

    Adapter = Module->Adapter;
    if (Adapter == NULL)
    {
        Ndis6DereferenceFilterModule(Module);
        return NDIS_STATUS_CLOSING;
    }

    Context = Ndis6FilterOidGetContext(OidRequest);
    if (Context == NULL)
    {
        Context = Ndis6FilterOidCreateContext(Adapter, OidRequest, FALSE, TRUE);
        if (Context == NULL)
        {
            Ndis6DereferenceFilterModule(Module);
            return NDIS_STATUS_RESOURCES;
        }

        Status = Ndis6FilterOidPushFrame(Context, Module, OidRequest, NULL);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            Ndis6FilterOidDetachContext(Context, OidRequest);
            Ndis6FilterOidDereferenceContext(Context);
            Ndis6DereferenceFilterModule(Module);
            return Status;
        }
        Originated = TRUE;
    }
    else if (Context->Signature != NDIS6_FILTER_OID_CONTEXT_SIGNATURE ||
             Context->Adapter != Adapter || Context->DirectRequest)
    {
        Ndis6DereferenceFilterModule(Module);
        return NDIS_STATUS_INVALID_STATE;
    }
    else
    {
        CurrentFrame = Ndis6FilterOidReferenceTopFrame(Context);
        if (CurrentFrame == NULL || CurrentFrame->Module != Module)
        {
            if (CurrentFrame != NULL)
                Ndis6FilterOidDereferenceFrame(CurrentFrame);
            Ndis6DereferenceFilterModule(Module);
            return NDIS_STATUS_INVALID_STATE;
        }
        Ndis6FilterOidDereferenceFrame(CurrentFrame);
        Ndis6FilterOidReferenceContext(Context);
    }

    Ndis6FilterOidSetActiveRequest(Context, OidRequest, FALSE);

    Next = Ndis6FilterChainAdjacent(Module, TRUE, Ndis6FilterOidRequest);
    if (Next != NULL)
    {
        Status = Ndis6FilterOidPushFrame(Context, Next, OidRequest, &NextFrame);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            Ndis6DereferenceFilterModule(Next);
            if (Originated)
            {
                Ndis6FilterOidReleaseFrames(Context);
                Ndis6FilterOidDetachContext(Context, OidRequest);
            }
            Ndis6FilterOidDereferenceContext(Context);
            Ndis6DereferenceFilterModule(Module);
            return Status;
        }

        Status = Next->DriverBlock->Characteristics.OidRequestHandler(Next->FilterModuleContext, OidRequest);
        Ndis6DereferenceFilterModule(Next);

        if (Status != NDIS_STATUS_PENDING &&
            Ndis6FilterOidGetContext(OidRequest) == Context)
        {
            Ndis6FilterOidPopFrame(Context, NextFrame);
        }
    }
    else
    {
        Ndis6FilterOidSetActiveRequest(Context, OidRequest, TRUE);
        Status = Ndis6FilterTerminalOidRequest(Adapter, OidRequest);
    }

    if (Originated && Status != NDIS_STATUS_PENDING &&
        Ndis6FilterOidGetContext(OidRequest) == Context)
    {
        Ndis6FilterOidReleaseFrames(Context);
        Ndis6FilterOidDetachContext(Context, OidRequest);
    }

    Ndis6FilterOidDereferenceContext(Context);
    Ndis6DereferenceFilterModule(Module);
    return Status;
}

VOID
NTAPI
NdisFOidRequestComplete(
    _In_ NDIS_HANDLE       NdisFilterHandle,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS       Status)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_OID_CONTEXT Context;
    PNDIS6_FILTER_OID_FRAME Frame;

    if (OidRequest == NULL)
        return;

    Context = Ndis6FilterOidGetContext(OidRequest);
    if (Context == NULL ||
        Context->Signature != NDIS6_FILTER_OID_CONTEXT_SIGNATURE ||
        Context->DirectRequest)
    {
        return;
    }

    Ndis6FilterOidReferenceContext(Context);
    Frame = Ndis6FilterOidReferenceTopFrame(Context);
    if (Frame == NULL || Frame->Module != Module)
    {
        if (Frame != NULL)
            Ndis6FilterOidDereferenceFrame(Frame);
        Ndis6FilterOidDereferenceContext(Context);
        return;
    }
    (VOID)Ndis6FilterOidBeginCompletion(Context, OidRequest);
    Ndis6FilterOidPopFrame(Context, Frame);
    Ndis6FilterContinueOidCompletion(Context, OidRequest, Status);
    Ndis6FilterOidDereferenceFrame(Frame);
    Ndis6FilterOidDereferenceContext(Context);
}

NDIS_STATUS
NTAPI
NdisFDirectOidRequest(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_MODULE Next;
    PNDIS6_FILTER_OID_CONTEXT Context;
    PNDIS6_FILTER_OID_FRAME CurrentFrame;
    PNDIS6_FILTER_OID_FRAME NextFrame = NULL;
    PLOGICAL_ADAPTER Adapter;
    NDIS_STATUS Status;
    BOOLEAN Originated = FALSE;

    if (OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (!Ndis6ReferenceFilterModule(Module))
        return NDIS_STATUS_CLOSING;

    Adapter = Module->Adapter;
    if (Adapter == NULL)
    {
        Ndis6DereferenceFilterModule(Module);
        return NDIS_STATUS_CLOSING;
    }

    Context = Ndis6FilterOidGetContext(OidRequest);
    if (Context == NULL)
    {
        Context = Ndis6FilterOidCreateContext(Adapter, OidRequest, TRUE, TRUE);
        if (Context == NULL)
        {
            Ndis6DereferenceFilterModule(Module);
            return NDIS_STATUS_RESOURCES;
        }

        Status = Ndis6FilterOidPushFrame(Context, Module, OidRequest, NULL);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            Ndis6FilterOidDetachContext(Context, OidRequest);
            Ndis6FilterOidDereferenceContext(Context);
            Ndis6DereferenceFilterModule(Module);
            return Status;
        }
        Originated = TRUE;
    }
    else if (Context->Signature != NDIS6_FILTER_OID_CONTEXT_SIGNATURE ||
             Context->Adapter != Adapter || !Context->DirectRequest)
    {
        Ndis6DereferenceFilterModule(Module);
        return NDIS_STATUS_INVALID_STATE;
    }
    else
    {
        CurrentFrame = Ndis6FilterOidReferenceTopFrame(Context);
        if (CurrentFrame == NULL || CurrentFrame->Module != Module)
        {
            if (CurrentFrame != NULL)
                Ndis6FilterOidDereferenceFrame(CurrentFrame);
            Ndis6DereferenceFilterModule(Module);
            return NDIS_STATUS_INVALID_STATE;
        }
        Ndis6FilterOidDereferenceFrame(CurrentFrame);
        Ndis6FilterOidReferenceContext(Context);
    }

    Ndis6FilterOidSetActiveRequest(Context, OidRequest, FALSE);

    Next = Ndis6FilterChainAdjacent(Module, TRUE, Ndis6FilterDirectOidRequest);
    if (Next != NULL)
    {
        Status = Ndis6FilterOidPushFrame(Context, Next, OidRequest, &NextFrame);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            Ndis6DereferenceFilterModule(Next);
            if (Originated)
            {
                Ndis6FilterOidReleaseFrames(Context);
                Ndis6FilterOidDetachContext(Context, OidRequest);
            }
            Ndis6FilterOidDereferenceContext(Context);
            Ndis6DereferenceFilterModule(Module);
            return Status;
        }

        Status = Next->DriverBlock->Characteristics.DirectOidRequestHandler(Next->FilterModuleContext, OidRequest);
        Ndis6DereferenceFilterModule(Next);

        if (Status != NDIS_STATUS_PENDING &&
            Ndis6FilterOidGetContext(OidRequest) == Context)
        {
            Ndis6FilterOidPopFrame(Context, NextFrame);
        }
    }
    else
    {
        Ndis6FilterOidSetActiveRequest(Context, OidRequest, TRUE);
        Status = Ndis6FilterTerminalDirectOidRequest(Adapter, OidRequest);
    }

    if (Originated && Status != NDIS_STATUS_PENDING &&
        Ndis6FilterOidGetContext(OidRequest) == Context)
    {
        Ndis6FilterOidReleaseFrames(Context);
        Ndis6FilterOidDetachContext(Context, OidRequest);
    }

    Ndis6FilterOidDereferenceContext(Context);
    Ndis6DereferenceFilterModule(Module);
    return Status;
}

VOID
NTAPI
NdisFDirectOidRequestComplete(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS Status)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_OID_CONTEXT Context;
    PNDIS6_FILTER_OID_FRAME Frame;

    if (OidRequest == NULL)
        return;

    Context = Ndis6FilterOidGetContext(OidRequest);
    if (Context == NULL ||
        Context->Signature != NDIS6_FILTER_OID_CONTEXT_SIGNATURE ||
        !Context->DirectRequest)
    {
        return;
    }

    Ndis6FilterOidReferenceContext(Context);
    Frame = Ndis6FilterOidReferenceTopFrame(Context);
    if (Frame == NULL || Frame->Module != Module)
    {
        if (Frame != NULL)
            Ndis6FilterOidDereferenceFrame(Frame);
        Ndis6FilterOidDereferenceContext(Context);
        return;
    }
    (VOID)Ndis6FilterOidBeginCompletion(Context, OidRequest);
    Ndis6FilterOidPopFrame(Context, Frame);
    Ndis6FilterContinueOidCompletion(Context, OidRequest, Status);
    Ndis6FilterOidDereferenceFrame(Frame);
    Ndis6FilterOidDereferenceContext(Context);
}

static PNDIS6_FILTER_OID_FRAME
Ndis6FilterOidFindCancelTargetLocked(
    _In_ PNDIS6_FILTER_OID_CONTEXT Context,
    _In_opt_ PNDIS6_FILTER_MODULE SourceModule)
{
    PNDIS6_FILTER_OID_FRAME Frame;
    PNDIS6_FILTER_OID_FRAME SourceFrame = NULL;

    if (SourceModule == NULL)
    {
        Frame = Context->Top;
        if (Frame == NULL)
            return NULL;
        while (Frame->Previous != NULL)
            Frame = Frame->Previous;
        return Frame;
    }

    for (Frame = Context->Top;
         Frame != NULL;
         Frame = Frame->Previous)
    {
        if (Frame->Module == SourceModule)
        {
            SourceFrame = Frame;
            break;
        }
    }
    if (SourceFrame == NULL)
        return NULL;

    for (Frame = Context->Top;
         Frame != NULL;
         Frame = Frame->Previous)
    {
        if (Frame->Previous == SourceFrame)
            return Frame;
    }
    return NULL;
}

static PNDIS6_FILTER_OID_FRAME
Ndis6FilterOidFindChildFrameLocked(
    _In_ PNDIS6_FILTER_OID_CONTEXT Context,
    _In_ PNDIS6_FILTER_OID_FRAME Parent)
{
    PNDIS6_FILTER_OID_FRAME Frame;

    for (Frame = Context->Top;
         Frame != NULL;
         Frame = Frame->Previous)
    {
        if (Frame->Previous == Parent)
            return Frame;
    }
    return NULL;
}

static VOID
Ndis6FilterOidCancelContext(
    _In_ PNDIS6_FILTER_OID_CONTEXT Context,
    _In_opt_ PNDIS6_FILTER_MODULE SourceModule)
{
    PNDIS6_FILTER_OID_FRAME Frame;
    PNDIS6_FILTER_MODULE Module = NULL;
    PNDIS6_ADAPTER_EXT Ext;
    BOOLEAN AtMiniport;
    BOOLEAN DirectRequest;
    PVOID RequestId = NULL;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Context->Lock, &OldIrql);
    if (InterlockedCompareExchange(&Context->Listed, TRUE, TRUE) != TRUE ||
        Context->Completing)
    {
        KeReleaseSpinLock(&Context->Lock, OldIrql);
        return;
    }

    Frame = Ndis6FilterOidFindCancelTargetLocked(Context, SourceModule);
    while (Frame != NULL)
    {
        PNDIS_FILTER_DRIVER_CHARACTERISTICS Characteristics =
            &Frame->Module->DriverBlock->Characteristics;
        BOOLEAN HasHandler = Context->DirectRequest
            ? Characteristics->CancelDirectOidRequestHandler != NULL
            : Characteristics->CancelOidRequestHandler != NULL;

        if (HasHandler)
        {
            if (Frame->CancelIndicated)
            {
                KeReleaseSpinLock(&Context->Lock, OldIrql);
                return;
            }
            Frame->CancelIndicated = TRUE;
            Ndis6FilterOidReferenceFrame(Frame);
            Module = Frame->Module;
            RequestId = Frame->RequestId;
            break;
        }
        Frame = Ndis6FilterOidFindChildFrameLocked(Context, Frame);
    }
    AtMiniport = Context->AtMiniport;
    DirectRequest = Context->DirectRequest;
    if (Module == NULL && AtMiniport)
    {
        if (Context->MiniportCancelIssued)
        {
            KeReleaseSpinLock(&Context->Lock, OldIrql);
            return;
        }
        Context->MiniportCancelIssued = TRUE;
        RequestId = Context->ActiveRequestId;
    }
    KeReleaseSpinLock(&Context->Lock, OldIrql);

    if (Module != NULL)
    {
        if (DirectRequest)
        {
            Module->DriverBlock->Characteristics.CancelDirectOidRequestHandler(Module->FilterModuleContext, RequestId);
        }
        else
        {
            Module->DriverBlock->Characteristics.CancelOidRequestHandler(Module->FilterModuleContext, RequestId);
        }
        Ndis6FilterOidDereferenceFrame(Frame);
        return;
    }

    if (!AtMiniport || Context->Adapter == NULL)
        return;

    Ext = NDIS6_EXT(Context->Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL)
        return;

    if (DirectRequest)
    {
        if (Ext->DriverBlock->Characteristics.CancelDirectOidRequestHandler != NULL)
        {
            Ext->DriverBlock->Characteristics.CancelDirectOidRequestHandler(Ext->MiniportAdapterContext, RequestId);
        }
    }
    else if (Ext->DriverBlock->Characteristics.CancelOidRequestHandler != NULL)
    {
        Ext->DriverBlock->Characteristics.CancelOidRequestHandler(Ext->MiniportAdapterContext, RequestId);
    }
}

static PNDIS6_FILTER_OID_CONTEXT
Ndis6FilterOidFindProtocolContextForCancel(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ NDIS_HANDLE OriginHandle,
    _In_ PVOID RequestId,
    _In_ BOOLEAN DirectRequest)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_FILTER_OID_CONTEXT Context = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    if (Adapter == NULL || OriginHandle == NULL)
        return NULL;
    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return NULL;

    KeAcquireSpinLock(&Ext->FilterOidContextListLock, &OldIrql);
    for (Entry = Ext->FilterOidContextList.Flink;
         Entry != &Ext->FilterOidContextList;
         Entry = Entry->Flink)
    {
        PNDIS6_FILTER_OID_CONTEXT Candidate =
            CONTAINING_RECORD(Entry, NDIS6_FILTER_OID_CONTEXT, ListEntry);
        KIRQL ContextIrql;

        KeAcquireSpinLock(&Candidate->Lock, &ContextIrql);
        if (InterlockedCompareExchange(&Candidate->Listed, TRUE, TRUE) == TRUE &&
            !Candidate->Completing &&
            Candidate->DirectRequest == DirectRequest &&
            Candidate->OriginHandle == OriginHandle &&
            Candidate->OriginRequestId == RequestId &&
            !Candidate->OriginCancelIssued)
        {
            Candidate->OriginCancelIssued = TRUE;
            Ndis6FilterOidReferenceContext(Candidate);
            Context = Candidate;
        }
        KeReleaseSpinLock(&Candidate->Lock, ContextIrql);
        if (Context != NULL)
            break;
    }
    KeReleaseSpinLock(&Ext->FilterOidContextListLock, OldIrql);
    return Context;
}

VOID
Ndis6FilterCancelOidRequestFromProtocol(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ NDIS_HANDLE OriginHandle,
    _In_ PVOID RequestId,
    _In_ BOOLEAN DirectRequest)
{
    PNDIS6_FILTER_OID_CONTEXT Context;

    for (;;)
    {
        Context = Ndis6FilterOidFindProtocolContextForCancel(Adapter, OriginHandle, RequestId, DirectRequest);
        if (Context == NULL)
            break;

        Ndis6FilterOidCancelContext(Context, NULL);
        Ndis6FilterOidDereferenceContext(Context);
    }
}

static BOOLEAN
Ndis6FilterOidFrameForwardedRequestMatchesLocked(
    _In_ PNDIS6_FILTER_OID_CONTEXT Context,
    _In_ PNDIS6_FILTER_OID_FRAME SourceFrame,
    _In_ PVOID RequestId)
{
    PNDIS6_FILTER_OID_FRAME TargetFrame;

    TargetFrame = Ndis6FilterOidFindChildFrameLocked(Context, SourceFrame);
    if (TargetFrame != NULL)
        return TargetFrame->RequestId == RequestId;

    return Context->AtMiniport && Context->ActiveRequestId == RequestId;
}

static PNDIS6_FILTER_OID_CONTEXT
Ndis6FilterOidFindContextForCancel(
    _In_ PNDIS6_FILTER_MODULE Module,
    _In_ PVOID RequestId,
    _In_ BOOLEAN DirectRequest)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_FILTER_OID_CONTEXT Context = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    if (Module == NULL || Module->Adapter == NULL)
        return NULL;

    Ext = NDIS6_EXT(Module->Adapter);
    if (Ext == NULL)
        return NULL;

    KeAcquireSpinLock(&Ext->FilterOidContextListLock, &OldIrql);
    for (Entry = Ext->FilterOidContextList.Flink;
         Entry != &Ext->FilterOidContextList;
         Entry = Entry->Flink)
    {
        PNDIS6_FILTER_OID_CONTEXT Candidate =
            CONTAINING_RECORD(Entry, NDIS6_FILTER_OID_CONTEXT, ListEntry);
        KIRQL ContextIrql;
        PNDIS6_FILTER_OID_FRAME Frame;

        KeAcquireSpinLock(&Candidate->Lock, &ContextIrql);
        if (InterlockedCompareExchange(&Candidate->Listed, TRUE, TRUE) == TRUE &&
            !Candidate->Completing &&
            Candidate->DirectRequest == DirectRequest)
        {
            for (Frame = Candidate->Top;
                 Frame != NULL;
                 Frame = Frame->Previous)
            {
                if (Frame->Module == Module &&
                    !Frame->CancelForwarded &&
                    Ndis6FilterOidFrameForwardedRequestMatchesLocked(Candidate, Frame, RequestId))
                {
                    Frame->CancelForwarded = TRUE;
                    Ndis6FilterOidReferenceContext(Candidate);
                    Context = Candidate;
                    break;
                }
            }
        }
        KeReleaseSpinLock(&Candidate->Lock, ContextIrql);
        if (Context != NULL)
            break;
    }
    KeReleaseSpinLock(&Ext->FilterOidContextListLock, OldIrql);
    return Context;
}

VOID
NTAPI
NdisFCancelOidRequest(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ PVOID RequestId)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_OID_CONTEXT Context;

    if (!Ndis6ReferenceFilterModule(Module))
        return;

    for (;;)
    {
        Context = Ndis6FilterOidFindContextForCancel(Module, RequestId, FALSE);
        if (Context == NULL)
            break;

        Ndis6FilterOidCancelContext(Context, Module);
        Ndis6FilterOidDereferenceContext(Context);
    }
    Ndis6DereferenceFilterModule(Module);
}

VOID
NTAPI
NdisFCancelDirectOidRequest(
    _In_ NDIS_HANDLE NdisFilterHandle,
    _In_ PVOID RequestId)
{
    PNDIS6_FILTER_MODULE Module = (PNDIS6_FILTER_MODULE)NdisFilterHandle;
    PNDIS6_FILTER_OID_CONTEXT Context;

    if (!Ndis6ReferenceFilterModule(Module))
        return;

    for (;;)
    {
        Context = Ndis6FilterOidFindContextForCancel(Module, RequestId, TRUE);
        if (Context == NULL)
            break;

        Ndis6FilterOidCancelContext(Context, Module);
        Ndis6FilterOidDereferenceContext(Context);
    }
    Ndis6DereferenceFilterModule(Module);
}

/* EOF */
