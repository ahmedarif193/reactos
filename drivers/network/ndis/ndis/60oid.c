/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        drivers/network/ndis/ndis/60oid.c
 * PURPOSE:     Legacy OID dispatcher for NDIS 6 adapters.
 *
 *              When a legacy protocol (tcpip.sys, NDIS 5 ProRequest path)
 *              calls NdisRequest on an adapter that is actually an NDIS 6
 *              miniport (e1000e), the old MiniDoRequest() would dereference
 *              Adapter->NdisMiniportBlock.DriverHandle->MiniportCharacteristics
 *              and crash because DriverHandle is NULL on an NDIS 6 adapter.
 *
 *              This file services the common legacy OIDs (OID_GEN_*,
 *              OID_802_3_CURRENT_ADDRESS, etc.) from the cached
 *              NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES the driver pushed
 *              during NdisMSetMiniportAttributes. No round-trip to the
 *              NDIS 6 miniport's OidRequestHandler is needed for these
 *              informational OIDs — the values are fixed at init time.
 *
 *              Set-OIDs (OID_GEN_CURRENT_LOOKAHEAD, OID_GEN_CURRENT_PACKET_FILTER)
 *              are accepted silently for now; Phase 3 will forward them
 *              to the real OidRequestHandler so filter changes take effect
 *              on the hardware.
 *
 *              Created on the dev-nt6-1 branch by the NDIS 5↔6 bridge
 *              work that lets e1000e answer tcpip's bootstrap OIDs.
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors
 */

#include "ndis6_internal.h"

/* ETH_HEADER_SIZE — fixed Ethernet II header length. Used to derive
 * OID_GEN_MAXIMUM_TOTAL_SIZE from the MTU the driver reported. */
#define NDIS6_ETH_HEADER_SIZE   14

/* ============================================================================
 *  Ndis6OidForward — synchronous forwarder from a legacy NDIS_REQUEST to
 *  the NDIS 6 miniport's OidRequestHandler.
 *
 *  Builds an NDIS_OID_REQUEST from the legacy fields, registers a waiter on
 *  Ext->OidWaiters BEFORE calling the driver (avoid the sync-completion
 *  race), invokes Characteristics.OidRequestHandler, and waits on the
 *  waiter event if the driver returned PENDING. Copies the result back
 *  into the legacy NDIS_REQUEST.
 *
 *  Used for set-OIDs and for unknown query-OIDs that the cache doesn't
 *  serve. Common read-only query OIDs are still served from
 *  Ext->GeneralAttrs in the fast path above.
 * ============================================================================ */

static NDIS_STATUS
Ndis6OidForward(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _In_ PNDIS_REQUEST      Request)
{
    NDIS_OID_REQUEST  OidReq;
    NDIS6_OID_WAITER  Waiter;
    NDIS_STATUS       Status;
    KIRQL             OldIrql;

    if (Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.OidRequestHandler == NULL)
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(&OidReq, sizeof(OidReq));
    OidReq.Header.Type     = NDIS_OBJECT_TYPE_OID_REQUEST;
    OidReq.Header.Revision = NDIS_OID_REQUEST_REVISION_1;
    OidReq.Header.Size     = sizeof(NDIS_OID_REQUEST);
    OidReq.RequestType     = Request->RequestType;
    OidReq.PortNumber      = 0;
    OidReq.Timeout         = NDIS_OID_REQUEST_TIMEOUT_INFINITE;

    if (Request->RequestType == NdisRequestQueryInformation)
    {
        OidReq.DATA.QUERY_INFORMATION.Oid =
            Request->DATA.QUERY_INFORMATION.Oid;
        OidReq.DATA.QUERY_INFORMATION.InformationBuffer =
            Request->DATA.QUERY_INFORMATION.InformationBuffer;
        OidReq.DATA.QUERY_INFORMATION.InformationBufferLength =
            Request->DATA.QUERY_INFORMATION.InformationBufferLength;
    }
    else if (Request->RequestType == NdisRequestSetInformation)
    {
        OidReq.DATA.SET_INFORMATION.Oid =
            Request->DATA.SET_INFORMATION.Oid;
        OidReq.DATA.SET_INFORMATION.InformationBuffer =
            Request->DATA.SET_INFORMATION.InformationBuffer;
        OidReq.DATA.SET_INFORMATION.InformationBufferLength =
            Request->DATA.SET_INFORMATION.InformationBufferLength;
    }
    else
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    /* Set up the waiter and link it BEFORE calling the driver. The driver
     * may complete synchronously inside the call to OidRequestHandler;
     * NdisMOidRequestComplete will look up the waiter via OidReq.RequestId
     * and signal the event. If we set the request id and waited AFTER the
     * driver call, a sync completion would race the wait setup. */
    KeInitializeEvent(&Waiter.Event, NotificationEvent, FALSE);
    Waiter.OidRequest       = &OidReq;
    Waiter.CompletionStatus = NDIS_STATUS_PENDING;

    OidReq.RequestId = (PVOID)&Waiter;

    KeAcquireSpinLock(&Ext->OidWaiterLock, &OldIrql);
    InsertTailList(&Ext->OidWaiters, &Waiter.ListEntry);
    KeReleaseSpinLock(&Ext->OidWaiterLock, OldIrql);

    /* Phase 9D: route through the filter chain so any installed filters
     * see (and possibly modify/intercept) the OID before the miniport.
     * Ndis6FilterDispatchOidRequest short-circuits to the miniport when
     * no filters are attached. */
    Status = Ndis6FilterDispatchOidRequest(Ext->Adapter, &OidReq);

    if (Status == NDIS_STATUS_PENDING)
    {
        KeWaitForSingleObject(&Waiter.Event, Executive,
                              KernelMode, FALSE, NULL);
        Status = Waiter.CompletionStatus;
    }
    else
    {
        /* Synchronous completion — pull the waiter off the list ourselves
         * (NdisMOidRequestComplete may or may not have done so). */
        KeAcquireSpinLock(&Ext->OidWaiterLock, &OldIrql);
        if (Waiter.ListEntry.Flink != NULL && Waiter.ListEntry.Blink != NULL)
        {
            RemoveEntryList(&Waiter.ListEntry);
            Waiter.ListEntry.Flink = NULL;
            Waiter.ListEntry.Blink = NULL;
        }
        KeReleaseSpinLock(&Ext->OidWaiterLock, OldIrql);
    }

    /* Copy the OID-request result back to the legacy NDIS_REQUEST. */
    if (Request->RequestType == NdisRequestQueryInformation)
    {
        Request->DATA.QUERY_INFORMATION.BytesWritten =
            OidReq.DATA.QUERY_INFORMATION.BytesWritten;
        Request->DATA.QUERY_INFORMATION.BytesNeeded =
            OidReq.DATA.QUERY_INFORMATION.BytesNeeded;
    }
    else
    {
        Request->DATA.SET_INFORMATION.BytesRead =
            OidReq.DATA.SET_INFORMATION.BytesRead;
        Request->DATA.SET_INFORMATION.BytesNeeded =
            OidReq.DATA.SET_INFORMATION.BytesNeeded;
    }

    return Status;
}

/* ============================================================================
 *  NdisMOidRequestComplete — driver-side OID-completion callback.
 *
 *  The miniport calls this from its OidRequestHandler (or from a worker
 *  thread / DPC) to signal that an asynchronously-completing OID is done.
 *  We pop the matching NDIS6_OID_WAITER from Ext->OidWaiters via the
 *  RequestId field (which Ndis6OidForward stashed), record the status, and
 *  signal the event so the synchronous waiter wakes up.
 *
 *  Replaces the no-op stub that lived in 60stubs.c.
 * ============================================================================ */

/* ============================================================================
 *  Phase 4 helpers — exported by name for the legacy library to call from
 *  miniport.c. The legacy file is compiled with NDIS51 PCH and can't see
 *  PNDIS6_ADAPTER_EXT, so it forward-declares these prototypes and calls
 *  them via extern.
 * ============================================================================ */

NDIS_STATUS
Ndis6CallResetHandlerEx(
    _In_ PLOGICAL_ADAPTER Adapter,
    _Out_ BOOLEAN* AddressingReset)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (AddressingReset != NULL)
        *AddressingReset = FALSE;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return NDIS_STATUS_NOT_SUPPORTED;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.ResetHandlerEx == NULL)
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    return Ext->DriverBlock->Characteristics.ResetHandlerEx(
        Ext->MiniportAdapterContext, AddressingReset);
}

BOOLEAN
Ndis6CallCheckForHangHandlerEx(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return FALSE;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.CheckForHangHandlerEx == NULL)
    {
        return FALSE;
    }

    return Ext->DriverBlock->Characteristics.CheckForHangHandlerEx(
        Ext->MiniportAdapterContext);
}

VOID
NTAPI
NdisMOidRequestComplete(
    _In_ NDIS_HANDLE       NdisMiniportHandle,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS       Status)
{
    PLOGICAL_ADAPTER    Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;
    PNDIS6_ADAPTER_EXT  Ext;
    PVOID               RequestId;
    PNDIS6_OID_WAITER   Waiter;
    KIRQL               OldIrql;
    PLIST_ENTRY         entry;
    BOOLEAN             IsProtocolPending;

    if (Adapter == NULL || !Adapter->IsNdis6 || OidRequest == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    RequestId = OidRequest->RequestId;
    if (RequestId == NULL)
        return;

    /* Two possible RequestId types:
     *   1. PNDIS6_OID_WAITER — a legacy-NDIS-5 forwarded request. Signal
     *      the event and let Ndis6OidForward wake up.
     *   2. PNDIS6_PROTOCOL_PENDING_OID — a native NDIS 6 protocol async
     *      request. Restore the original RequestId, pop the entry, and
     *      call the protocol's OidRequestCompleteHandler.
     *
     * We disambiguate by walking the legacy waiter list first (cheap,
     * one spinlock) — if found, treat as case 1. Otherwise case 2. */

    IsProtocolPending = TRUE;
    KeAcquireSpinLock(&Ext->OidWaiterLock, &OldIrql);
    for (entry = Ext->OidWaiters.Flink;
         entry != &Ext->OidWaiters;
         entry = entry->Flink)
    {
        Waiter = CONTAINING_RECORD(entry, NDIS6_OID_WAITER, ListEntry);
        if ((PVOID)Waiter == RequestId)
        {
            IsProtocolPending = FALSE;
            RemoveEntryList(&Waiter->ListEntry);
            Waiter->ListEntry.Flink = NULL;
            Waiter->ListEntry.Blink = NULL;
            break;
        }
    }
    KeReleaseSpinLock(&Ext->OidWaiterLock, OldIrql);

    if (!IsProtocolPending)
    {
        Waiter->CompletionStatus = Status;
        KeSetEvent(&Waiter->Event, IO_NO_INCREMENT, FALSE);
        return;
    }

    /* D4: native protocol async completion. */
    {
        PNDIS6_PROTOCOL_PENDING_OID Pending = (PNDIS6_PROTOCOL_PENDING_OID)RequestId;
        PNDIS6_PROTOCOL_BINDING     Binding;
        KIRQL                       BIrql;

        if (Pending == NULL)
            return;
        Binding = Pending->Binding;
        if (Binding == NULL || Binding->DriverBlock == NULL)
            return;

        /* Remove the pending entry from the binding's list. */
        KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &BIrql);
        RemoveEntryList(&Pending->ListEntry);
        KeReleaseSpinLock(&Binding->PendingOidRequestsLock, BIrql);

        /* Restore the protocol's original RequestId and call its
         * completion handler. */
        OidRequest->RequestId = Pending->OriginalRequestId;
        if (Binding->DriverBlock->Characteristics.OidRequestCompleteHandler != NULL)
        {
            Binding->DriverBlock->Characteristics.OidRequestCompleteHandler(
                Binding->ProtocolBindingContext, OidRequest, Status);
        }
        ExFreePoolWithTag(Pending, 'dOPn');
    }
}

/* ============================================================================
 *  Ndis6LegacyDoRequest — services NDIS 5 NDIS_REQUEST for NDIS 6 adapters
 *
 *  Called from the legacy MiniDoRequest in miniport.c via an IsNdis6 check.
 *  Returns an NDIS_STATUS just like MiniDoRequest would.
 * ============================================================================ */

NDIS_STATUS
Ndis6LegacyDoRequest(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS_REQUEST    Request)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES gen;
    NDIS_OID Oid;
    PVOID Buffer;
    UINT BufferLen;

    if (Adapter == NULL || Request == NULL)
        return NDIS_STATUS_INVALID_DATA;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_DATA;

    gen = &Ext->GeneralAttrs;

    switch (Request->RequestType)
    {
    /* ------------------------------------------------------------ */
    case NdisRequestQueryInformation:
    {
        Oid       = Request->DATA.QUERY_INFORMATION.Oid;
        Buffer    = Request->DATA.QUERY_INFORMATION.InformationBuffer;
        BufferLen = Request->DATA.QUERY_INFORMATION.InformationBufferLength;

        switch (Oid)
        {
        case 0x0001021E:    /* OID_GEN_RECEIVE_SCALE_CAPABILITIES */
        {
            /* E5: advertise single-queue RSS capabilities. Drivers that
             * support RSS will call back into NdisMSetMiniportAttributes
             * with an NDIS_OBJECT_TYPE_DEFAULT attrs of type
             * NDIS_RECEIVE_SCALE_CAPABILITIES. For query, we return
             * a single-queue capability so the caller knows RSS is
             * recognised without actually doing multi-queue. */
            UCHAR* rss = (UCHAR*)Buffer;
            if (BufferLen < 32)
            {
                Request->DATA.QUERY_INFORMATION.BytesNeeded = 32;
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            RtlZeroMemory(rss, 32);
            *(PUSHORT)(rss + 0)  = NDIS_OBJECT_TYPE_DEFAULT;
            *(PUCHAR) (rss + 2)  = 1;      /* Revision */
            *(PUSHORT)(rss + 4)  = 32;     /* Size */
            /* CapabilitiesFlags=0 NumberOfInterruptMessages=0
             * NumberOfReceiveQueues=1 NumberOfIndirectionTableEntries=0 */
            *(PULONG)(rss + 16) = 1;       /* NumberOfReceiveQueues */
            Request->DATA.QUERY_INFORMATION.BytesWritten = 32;
            return NDIS_STATUS_SUCCESS;
        }

        case OID_GEN_MAXIMUM_SEND_PACKETS:
            if (BufferLen < sizeof(ULONG))
            {
                Request->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            /* B1: batch up to 64 sends per miniport call. The bridge's
             * TX thunk handles chained NBLs and the driver's
             * SendNetBufferListsHandler sees the whole batch in one
             * invocation — critical for gigabit line-rate sends. */
            *(PULONG)Buffer = 64;
            Request->DATA.QUERY_INFORMATION.BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_HARDWARE_STATUS:
            if (BufferLen < sizeof(ULONG))
            {
                Request->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            *(PULONG)Buffer = Ext->Initialized ? NdisHardwareStatusReady
                                               : NdisHardwareStatusNotReady;
            Request->DATA.QUERY_INFORMATION.BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_MEDIA_CONNECT_STATUS:
            if (BufferLen < sizeof(ULONG))
            {
                Request->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            /* NDIS 6 uses NDIS_MEDIA_CONNECT_STATE enum, legacy uses
             * NDIS_MEDIA_STATE (Connected=0, Disconnected=1). Cast via
             * the simpler "connected if non-Unknown and not Disconnected"
             * rule. */
            if (Ext->GeneralAttrsValid &&
                gen->MediaConnectState == MediaConnectStateConnected)
            {
                *(PULONG)Buffer = NdisMediaStateConnected;
            }
            else
            {
                *(PULONG)Buffer = NdisMediaStateDisconnected;
            }
            Request->DATA.QUERY_INFORMATION.BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_LINK_SPEED:
            if (BufferLen < sizeof(ULONG))
            {
                Request->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            /* NDIS 5 OID_GEN_LINK_SPEED is in 100bps units; NDIS 6
             * GeneralAttrs.RcvLinkSpeed / XmitLinkSpeed are in raw bps.
             * e1000e reports 1e9 so we return 1e7 (100 Mbps units of
             * 100 bits = 1 Gbps). */
            if (Ext->GeneralAttrsValid && gen->RcvLinkSpeed)
            {
                *(PULONG)Buffer = (ULONG)(gen->RcvLinkSpeed / 100ULL);
            }
            else
            {
                *(PULONG)Buffer = 10000000; /* 1 Gbps default */
            }
            Request->DATA.QUERY_INFORMATION.BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_MAXIMUM_FRAME_SIZE:
            if (BufferLen < sizeof(ULONG))
            {
                Request->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            if (Ext->GeneralAttrsValid && gen->MtuSize)
            {
                *(PULONG)Buffer = gen->MtuSize;
            }
            else
            {
                *(PULONG)Buffer = 1500;
            }
            Request->DATA.QUERY_INFORMATION.BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_MAXIMUM_TOTAL_SIZE:
            if (BufferLen < sizeof(ULONG))
            {
                Request->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            /* MTU + Ethernet header (14) — legacy consumers add their
             * own VLAN / LLC padding on top. */
            if (Ext->GeneralAttrsValid && gen->MtuSize)
            {
                *(PULONG)Buffer = gen->MtuSize + NDIS6_ETH_HEADER_SIZE;
            }
            else
            {
                *(PULONG)Buffer = 1500 + NDIS6_ETH_HEADER_SIZE;
            }
            Request->DATA.QUERY_INFORMATION.BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_CURRENT_LOOKAHEAD:
            if (BufferLen < sizeof(ULONG))
            {
                Request->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            if (Ext->GeneralAttrsValid && gen->LookaheadSize)
            {
                *(PULONG)Buffer = gen->LookaheadSize;
            }
            else
            {
                *(PULONG)Buffer = 128;
            }
            Request->DATA.QUERY_INFORMATION.BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_MAC_OPTIONS:
            if (BufferLen < sizeof(ULONG))
            {
                Request->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(ULONG);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            *(PULONG)Buffer = Ext->GeneralAttrsValid
                ? gen->MacOptions
                : (NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                   NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                   NDIS_MAC_OPTION_NO_LOOPBACK);
            Request->DATA.QUERY_INFORMATION.BytesWritten = sizeof(ULONG);
            return NDIS_STATUS_SUCCESS;

        case OID_GEN_MEDIA_IN_USE:
        case OID_GEN_MEDIA_SUPPORTED:
            if (BufferLen < sizeof(NDIS_MEDIUM))
            {
                Request->DATA.QUERY_INFORMATION.BytesNeeded = sizeof(NDIS_MEDIUM);
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            *(NDIS_MEDIUM*)Buffer = NdisMedium802_3;
            Request->DATA.QUERY_INFORMATION.BytesWritten = sizeof(NDIS_MEDIUM);
            return NDIS_STATUS_SUCCESS;

        case OID_802_3_CURRENT_ADDRESS:
        case OID_802_3_PERMANENT_ADDRESS:
            if (BufferLen < 6)
            {
                Request->DATA.QUERY_INFORMATION.BytesNeeded = 6;
                return NDIS_STATUS_BUFFER_TOO_SHORT;
            }
            if (Ext->GeneralAttrsValid && gen->MacAddressLength >= 6)
            {
                const PUCHAR src = (Oid == OID_802_3_PERMANENT_ADDRESS)
                    ? gen->PermanentMacAddress
                    : gen->CurrentMacAddress;
                RtlCopyMemory(Buffer, src, 6);
            }
            else if (Adapter->AddressLength >= 6)
            {
                RtlCopyMemory(Buffer, &Adapter->Address, 6);
            }
            else
            {
                /* Fallback: zeros. tcpip will refuse to bind but we
                 * won't crash. */
                RtlZeroMemory(Buffer, 6);
            }
            Request->DATA.QUERY_INFORMATION.BytesWritten = 6;
            return NDIS_STATUS_SUCCESS;

        default:
            /* Unknown query — forward to the driver's OidRequestHandler
             * via the Phase 3 thunk. The driver may serve it directly
             * (e.g. statistics OIDs that live in hardware counters) or
             * return NDIS_STATUS_NOT_SUPPORTED. */
            return Ndis6OidForward(Ext, Request);
        }
    }

    /* ------------------------------------------------------------ */
    case NdisRequestSetInformation:
    {
        /* Forward all set-OIDs to the real OidRequestHandler. The driver
         * needs to actually program the hardware (e.g. update RAR/MTA for
         * OID_GEN_CURRENT_PACKET_FILTER). */
        UNREFERENCED_PARAMETER(Oid);
        UNREFERENCED_PARAMETER(Buffer);
        UNREFERENCED_PARAMETER(BufferLen);
        return Ndis6OidForward(Ext, Request);
    }

    /* ------------------------------------------------------------ */
    default:
        return NDIS_STATUS_NOT_SUPPORTED;
    }
}

/* ============================================================================
 *  A5: NdisMCancelOidRequest / NdisCancelOidRequest
 *
 *  Walk Ext->OidWaiters and signal the matching waiter with
 *  NDIS_STATUS_REQUEST_ABORTED. Also forward to the driver's
 *  CancelOidRequestHandler if it has one so the driver can drop
 *  any in-progress async OID work.
 * ============================================================================ */

VOID
NTAPI
NdisMCancelOidRequest(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ PVOID       RequestId)
{
    PLOGICAL_ADAPTER    Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;
    PNDIS6_ADAPTER_EXT  Ext;
    PLIST_ENTRY         entry;
    KIRQL               OldIrql;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    /* Forward to the driver first so it knows to stop any hardware
     * OID processing tied to this RequestId. */
    if (Ext->DriverBlock != NULL &&
        Ext->DriverBlock->Characteristics.CancelOidRequestHandler != NULL &&
        Ext->MiniportAdapterContext != NULL)
    {
        Ext->DriverBlock->Characteristics.CancelOidRequestHandler(
            Ext->MiniportAdapterContext, RequestId);
    }

    /* Walk the waiter list and signal any entry whose OID request
     * matches the cancel ID. The legacy-request forwarder stashes its
     * own waiter pointer as RequestId (not a caller id), but RequestId
     * in an OID_REQUEST is caller-opaque, so we scan for pointer match. */
    KeAcquireSpinLock(&Ext->OidWaiterLock, &OldIrql);
    for (entry = Ext->OidWaiters.Flink;
         entry != &Ext->OidWaiters;
         entry = entry->Flink)
    {
        PNDIS6_OID_WAITER w = CONTAINING_RECORD(entry, NDIS6_OID_WAITER, ListEntry);
        if (w->OidRequest != NULL && w->OidRequest->RequestId == RequestId)
        {
            w->CompletionStatus = NDIS_STATUS_REQUEST_ABORTED;
            KeSetEvent(&w->Event, IO_NO_INCREMENT, FALSE);
        }
    }
    KeReleaseSpinLock(&Ext->OidWaiterLock, OldIrql);
}

VOID
NTAPI
NdisCancelOidRequest(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PVOID       RequestId)
{
    /* Native NDIS 6 protocol path. The binding handle doubles as the
     * adapter pointer in our scheme; forward to the miniport path. */
    NdisMCancelOidRequest(NdisBindingHandle, RequestId);
}

/* EOF */
