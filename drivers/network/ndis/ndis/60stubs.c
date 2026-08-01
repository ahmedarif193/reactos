/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NDIS library
 * FILE:        ndis/60stubs.c
 * PURPOSE:     NDIS 6 entry points that are still stubs.
 * PROGRAMMERS: dev-nt6-1 branch (NT 5.2 -> NT 6.1 API upgrade)
 *
 * NOTE: This file used to host every NDIS 6 stub. Most of those have
 * been promoted to functional implementations and moved into purpose-
 * specific files:
 *
 *   60nbl.c     - NET_BUFFER / NET_BUFFER_LIST pool + alloc + free
 *   60io.c      - MMIO / IO port / DMA / shared memory / interrupts
 *   60driver.c  - NdisMRegisterMiniportDriver, AddDevice, attributes
 *   60adapter.c - LOGICAL_ADAPTER lifecycle, MiniportInitializeEx
 *
 * This file retains:
 *   - Filter driver registration stubs (no real filter chain yet)
 *   - Protocol driver registration stubs (no NDIS 6 protocols yet)
 *   - Datapath callbacks (NdisMSendNetBufferListsComplete, etc.) that
 *     still return STATUS_SUCCESS without doing thunk work — Phase 3/4
 *     of the dev-nt6-1 plan will move these to 60thunk.c
 *
 * Like every NDIS 6 file in this directory, this one is compiled with
 * NDIS620_MINIPORT and SKIP_PRECOMPILE_HEADERS ON because the PCH is
 * locked at NDIS 5.1.
 */

#include "ndis6_internal.h"

#ifndef EXPORT
#define EXPORT NTAPI
#endif

/* Bounded snapshot size used by the protocol-bind and filter-attach
 * fan-outs. Adapters with more than 16 protocol drivers / filter
 * drivers are unheard of in the consumer ReactOS use case; we silently
 * truncate. Each register/create cycle is independent, so a future
 * register call still gets the chance to bind. */
#define NDIS6_BIND_SNAPSHOT_MAX 16

/* ------------------------------------------------------------------ */
/*  Filter driver registration                                        */
/*                                                                    */
/*  Phase 6: real registration. The driver is added to                */
/*  g_Ndis6FilterDriverList and its characteristics are saved. The    */
/*  per-adapter filter chain walk on send/receive is not yet          */
/*  implemented; WFP and capture filters can register cleanly without */
/*  the bridge crashing, but their callbacks won't fire until a       */
/*  future phase adds the chain walk.                                 */
/* ------------------------------------------------------------------ */

LIST_ENTRY  g_Ndis6FilterDriverList;
KSPIN_LOCK  g_Ndis6FilterDriverListLock;
static BOOLEAN g_Ndis6FilterDriverListReady = FALSE;

#define NDIS6_FILTER_DRIVER_TAG  'fDNn'  /* "nNDf" */

static VOID
Ndis6FilterDriverListInit(VOID)
{
    if (!g_Ndis6FilterDriverListReady)
    {
        InitializeListHead(&g_Ndis6FilterDriverList);
        KeInitializeSpinLock(&g_Ndis6FilterDriverListLock);
        g_Ndis6FilterDriverListReady = TRUE;
    }
}

NDIS_STATUS
EXPORT
NdisFRegisterFilterDriver(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ NDIS_HANDLE FilterDriverContext,
    _In_ PNDIS_FILTER_DRIVER_CHARACTERISTICS FilterDriverCharacteristics,
    _Out_ PNDIS_HANDLE NdisFilterDriverHandle)
{
    PNDIS6_FILTER_DRIVER_BLOCK Block;
    KIRQL OldIrql;

    if (DriverObject == NULL || FilterDriverCharacteristics == NULL ||
        NdisFilterDriverHandle == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    if (FilterDriverCharacteristics->Header.Type !=
        NDIS_OBJECT_TYPE_FILTER_DRIVER_CHARACTERISTICS)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Ndis6FilterDriverListInit();

    Block = (PNDIS6_FILTER_DRIVER_BLOCK)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_FILTER_DRIVER_BLOCK),
        NDIS6_FILTER_DRIVER_TAG);
    if (Block == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Block, sizeof(*Block));
    Block->DriverObject        = DriverObject;
    Block->FilterDriverContext = FilterDriverContext;
    /* Copy only Header.Size bytes — a REV_1 (NDIS 6.0) filter's struct ends
     * at StatusHandler; the REV_2 direct-OID handlers stay NULL. */
    {
        USHORT CharSize = FilterDriverCharacteristics->Header.Size;
        if (CharSize == 0 || CharSize > sizeof(Block->Characteristics))
            CharSize = sizeof(Block->Characteristics);
        RtlCopyMemory(&Block->Characteristics, FilterDriverCharacteristics, CharSize);
    }

    KeAcquireSpinLock(&g_Ndis6FilterDriverListLock, &OldIrql);
    InsertTailList(&g_Ndis6FilterDriverList, &Block->ListEntry);
    KeReleaseSpinLock(&g_Ndis6FilterDriverListLock, OldIrql);

    *NdisFilterDriverHandle = (NDIS_HANDLE)Block;
    return NDIS_STATUS_SUCCESS;
}

VOID
EXPORT
NdisFDeregisterFilterDriver(
    _In_ NDIS_HANDLE NdisFilterDriverHandle)
{
    PNDIS6_FILTER_DRIVER_BLOCK Block = (PNDIS6_FILTER_DRIVER_BLOCK)NdisFilterDriverHandle;
    KIRQL OldIrql;

    if (Block == NULL)
        return;

    KeAcquireSpinLock(&g_Ndis6FilterDriverListLock, &OldIrql);
    if (Block->ListEntry.Flink != NULL && Block->ListEntry.Blink != NULL)
        RemoveEntryList(&Block->ListEntry);
    KeReleaseSpinLock(&g_Ndis6FilterDriverListLock, OldIrql);

    ExFreePoolWithTag(Block, NDIS6_FILTER_DRIVER_TAG);
}

/* ============================================================================
 *  Phase 7B filter attach/detach helpers
 *
 *  Ndis6AttachFiltersToAdapter — called from Ndis6CreateLogicalAdapter
 *  after MiniportInitializeEx populates GeneralAttrs. Walks the global
 *  filter driver list and calls each AttachHandler with a freshly built
 *  NDIS_FILTER_ATTACH_PARAMETERS, then stores the FilterModuleContext on
 *  the adapter's FilterModuleList.
 *
 *  Ndis6DetachFiltersFromAdapter — called from Ndis6DestroyLogicalAdapter
 *  on REMOVE. Walks the per-adapter filter module list and calls each
 *  filter's DetachHandler, then frees the modules.
 *
 *  The TX/RX datapath does not yet walk the filter chain — registration
 *  is functional, but filters won't see traffic until a future phase.
 * ============================================================================ */

#define NDIS6_FILTER_MODULE_TAG  'mFNn'  /* "nNFm" */

VOID
Ndis6AttachFiltersToAdapter(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    PLIST_ENTRY        entry;
    KIRQL              OldIrql;
    PNDIS6_FILTER_DRIVER_BLOCK Snapshot[NDIS6_BIND_SNAPSHOT_MAX];
    UINT               SnapCount = 0;
    UINT               i;

    if (!g_Ndis6FilterDriverListReady || Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    KeAcquireSpinLock(&g_Ndis6FilterDriverListLock, &OldIrql);
    for (entry = g_Ndis6FilterDriverList.Flink;
         entry != &g_Ndis6FilterDriverList && SnapCount < NDIS6_BIND_SNAPSHOT_MAX;
         entry = entry->Flink)
    {
        PNDIS6_FILTER_DRIVER_BLOCK Block =
            CONTAINING_RECORD(entry, NDIS6_FILTER_DRIVER_BLOCK, ListEntry);
        if (Block->Characteristics.AttachHandler != NULL)
            Snapshot[SnapCount++] = Block;
    }
    KeReleaseSpinLock(&g_Ndis6FilterDriverListLock, OldIrql);

    for (i = 0; i < SnapCount; i++)
    {
        NDIS_FILTER_ATTACH_PARAMETERS Params;
        PNDIS6_FILTER_MODULE          Module;
        NDIS_STATUS                   Status;

        Module = (PNDIS6_FILTER_MODULE)ExAllocatePoolWithTag(
            NonPagedPool, sizeof(NDIS6_FILTER_MODULE), NDIS6_FILTER_MODULE_TAG);
        if (Module == NULL)
            continue;

        RtlZeroMemory(Module, sizeof(*Module));
        Module->DriverBlock = Snapshot[i];
        Module->Adapter     = Adapter;

        RtlZeroMemory(&Params, sizeof(Params));
        Params.Header.Type     = NDIS_OBJECT_TYPE_FILTER_ATTACH_PARAMETERS;
        Params.Header.Revision = 1;
        Params.Header.Size     = sizeof(NDIS_FILTER_ATTACH_PARAMETERS);
        if (Ext->GeneralAttrsValid)
        {
            Params.MtuSize          = Ext->GeneralAttrs.MtuSize;
            Params.MiniportMediaType = Ext->GeneralAttrs.MediaType;
            Params.PhysicalMediumType = Ext->GeneralAttrs.PhysicalMediumType;
        }
        Params.BaseMiniportName = &Adapter->NdisMiniportBlock.MiniportName;

        /* The filter's AttachHandler stores its per-adapter context via
         * NdisFSetAttributes, but we don't yet implement that API.
         * Filters that absolutely require it will fail attach gracefully;
         * passive monitoring filters will be fine. */
        Status = Snapshot[i]->Characteristics.AttachHandler(
            (NDIS_HANDLE)Module,                /* NdisFilterHandle */
            Snapshot[i]->FilterDriverContext,
            &Params);

        if (Status == NDIS_STATUS_SUCCESS)
        {
            KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
            InsertTailList(&Ext->FilterModuleList, &Module->ListEntry);
            KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);
        }
        else
        {
            ExFreePoolWithTag(Module, NDIS6_FILTER_MODULE_TAG);
        }
    }
}

VOID
Ndis6DetachFiltersFromAdapter(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    LIST_ENTRY         LocalList;
    KIRQL              OldIrql;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    InitializeListHead(&LocalList);

    /* Move the filter modules off the adapter's list under the lock so
     * we can walk them and call DetachHandler outside the lock. */
    KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
    while (!IsListEmpty(&Ext->FilterModuleList))
    {
        PLIST_ENTRY entry = RemoveHeadList(&Ext->FilterModuleList);
        InsertTailList(&LocalList, entry);
    }
    KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);

    while (!IsListEmpty(&LocalList))
    {
        PLIST_ENTRY entry = RemoveHeadList(&LocalList);
        PNDIS6_FILTER_MODULE Module =
            CONTAINING_RECORD(entry, NDIS6_FILTER_MODULE, ListEntry);

        if (Module->DriverBlock != NULL &&
            Module->DriverBlock->Characteristics.DetachHandler != NULL)
        {
            Module->DriverBlock->Characteristics.DetachHandler(
                Module->FilterModuleContext);
        }
        ExFreePoolWithTag(Module, NDIS6_FILTER_MODULE_TAG);
    }
}

/* ------------------------------------------------------------------ */
/*  Protocol driver registration                                      */
/*                                                                    */
/*  Phase 6: real registration. The driver is added to                */
/*  g_Ndis6ProtocolDriverList. The per-adapter ProtocolBindAdapterEx  */
/*  fan-out on adapter create / driver register is left as a future   */
/*  exercise — current ReactOS has no NDIS 6 protocol drivers in tree */
/*  to bind. The native-NBL TX path (NdisSendNetBufferLists) is still */
/*  a no-op stub below.                                               */
/* ------------------------------------------------------------------ */

LIST_ENTRY  g_Ndis6ProtocolDriverList;
KSPIN_LOCK  g_Ndis6ProtocolDriverListLock;
static BOOLEAN g_Ndis6ProtocolDriverListReady = FALSE;

#define NDIS6_PROTOCOL_DRIVER_TAG  'pDNn'  /* "nNDp" */

static VOID
Ndis6ProtocolDriverListInit(VOID)
{
    if (!g_Ndis6ProtocolDriverListReady)
    {
        InitializeListHead(&g_Ndis6ProtocolDriverList);
        KeInitializeSpinLock(&g_Ndis6ProtocolDriverListLock);
        g_Ndis6ProtocolDriverListReady = TRUE;
    }
}

/* ============================================================================
 *  Build an NDIS_BIND_PARAMETERS from an adapter's cached general attrs.
 *  Used by the protocol bind fan-out below.
 * ============================================================================ */

static VOID
Ndis6BuildBindParameters(
    _In_  PLOGICAL_ADAPTER       Adapter,
    _Out_ PNDIS_BIND_PARAMETERS  Params)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);

    RtlZeroMemory(Params, sizeof(*Params));
    Params->Header.Type     = NDIS_OBJECT_TYPE_DEFAULT;
    Params->Header.Revision = 1;
    Params->Header.Size     = sizeof(NDIS_BIND_PARAMETERS);
    Params->AdapterName     = &Adapter->NdisMiniportBlock.MiniportName;
    Params->MiniportHandle  = (NDIS_HANDLE)Adapter;

    if (Ext != NULL)
    {
        Params->PhysicalDeviceObject = Ext->PhysicalDeviceObject;
        if (Ext->GeneralAttrsValid)
        {
            Params->MediaType            = Ext->GeneralAttrs.MediaType;
            Params->PhysicalMediumType   = Ext->GeneralAttrs.PhysicalMediumType;
            Params->MtuSize              = Ext->GeneralAttrs.MtuSize;
            Params->MaxXmitLinkSpeed     = (ULONG)Ext->GeneralAttrs.XmitLinkSpeed;
            Params->MaxRcvLinkSpeed      = (ULONG)Ext->GeneralAttrs.RcvLinkSpeed;
            Params->LookaheadSize        = Ext->GeneralAttrs.LookaheadSize;
            Params->MacOptions           = Ext->GeneralAttrs.MacOptions;
            Params->SupportedPacketFilters = Ext->GeneralAttrs.SupportedPacketFilters;
            Params->MaxMulticastListSize = Ext->GeneralAttrs.MaxMulticastListSize;
            Params->MacAddressLength     = Ext->GeneralAttrs.MacAddressLength;
            if (Ext->GeneralAttrs.MacAddressLength <= sizeof(Params->CurrentMacAddress))
            {
                RtlCopyMemory(Params->CurrentMacAddress,
                              Ext->GeneralAttrs.CurrentMacAddress,
                              Ext->GeneralAttrs.MacAddressLength);
            }
        }
    }
}

/* ============================================================================
 *  Ndis6BindProtocolToAllAdapters — when a protocol registers, walk the
 *  global LOGICAL_ADAPTER list and call its BindAdapterHandlerEx for every
 *  NDIS 6 adapter.
 * ============================================================================ */

static VOID
Ndis6BindProtocolToAllAdapters(
    _In_ PNDIS6_PROTOCOL_DRIVER_BLOCK Block)
{
    extern LIST_ENTRY AdapterListHead;
    extern KSPIN_LOCK AdapterListLock;
    PLIST_ENTRY entry;
    KIRQL OldIrql;
    /* Snapshot adapter pointers under the lock; call BindAdapterHandlerEx
     * outside the lock to avoid holding it across protocol code. */
    PLOGICAL_ADAPTER Snapshot[NDIS6_BIND_SNAPSHOT_MAX];
    UINT SnapCount = 0;
    UINT i;

    if (Block->Characteristics.BindAdapterHandlerEx == NULL)
        return;

    KeAcquireSpinLock(&AdapterListLock, &OldIrql);
    for (entry = AdapterListHead.Flink;
         entry != &AdapterListHead && SnapCount < NDIS6_BIND_SNAPSHOT_MAX;
         entry = entry->Flink)
    {
        PLOGICAL_ADAPTER Adapter =
            CONTAINING_RECORD(entry, LOGICAL_ADAPTER, ListEntry);
        if (Adapter->IsNdis6)
            Snapshot[SnapCount++] = Adapter;
    }
    KeReleaseSpinLock(&AdapterListLock, OldIrql);

    for (i = 0; i < SnapCount; i++)
    {
        NDIS_BIND_PARAMETERS Params;
        Ndis6BuildBindParameters(Snapshot[i], &Params);
        Block->Characteristics.BindAdapterHandlerEx(
            Block->ProtocolDriverContext,
            (NDIS_HANDLE)Snapshot[i],  /* BindContext = adapter handle (see NdisOpenAdapterEx) */
            &Params);
    }
}

/* ============================================================================
 *  Ndis6BindAllProtocolsToAdapter — when a new adapter is created, walk
 *  the registered protocol list and call each one's BindAdapterHandlerEx.
 *  Called from Ndis6CreateLogicalAdapter (60adapter.c) at the end of adapter
 *  setup, after GeneralAttrs are populated.
 * ============================================================================ */

VOID
Ndis6BindAllProtocolsToAdapter(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PLIST_ENTRY entry;
    KIRQL OldIrql;
    PNDIS6_PROTOCOL_DRIVER_BLOCK Snapshot[NDIS6_BIND_SNAPSHOT_MAX];
    UINT SnapCount = 0;
    UINT i;
    NDIS_BIND_PARAMETERS Params;

    if (!g_Ndis6ProtocolDriverListReady || Adapter == NULL || !Adapter->IsNdis6)
        return;

    KeAcquireSpinLock(&g_Ndis6ProtocolDriverListLock, &OldIrql);
    for (entry = g_Ndis6ProtocolDriverList.Flink;
         entry != &g_Ndis6ProtocolDriverList && SnapCount < NDIS6_BIND_SNAPSHOT_MAX;
         entry = entry->Flink)
    {
        PNDIS6_PROTOCOL_DRIVER_BLOCK Block =
            CONTAINING_RECORD(entry, NDIS6_PROTOCOL_DRIVER_BLOCK, ListEntry);
        if (Block->Characteristics.BindAdapterHandlerEx != NULL)
            Snapshot[SnapCount++] = Block;
    }
    KeReleaseSpinLock(&g_Ndis6ProtocolDriverListLock, OldIrql);

    if (SnapCount == 0)
        return;

    Ndis6BuildBindParameters(Adapter, &Params);

    for (i = 0; i < SnapCount; i++)
    {
        Snapshot[i]->Characteristics.BindAdapterHandlerEx(
            Snapshot[i]->ProtocolDriverContext,
            (NDIS_HANDLE)Adapter,       /* BindContext = adapter handle (see NdisOpenAdapterEx) */
            &Params);
    }
}

NDIS_STATUS
EXPORT
NdisRegisterProtocolDriver(
    _In_ NDIS_HANDLE ProtocolDriverContext,
    _In_ PNDIS_PROTOCOL_DRIVER_CHARACTERISTICS ProtocolDriverCharacteristics,
    _Out_ PNDIS_HANDLE NdisProtocolHandle)
{
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block;
    KIRQL OldIrql;

    if (ProtocolDriverCharacteristics == NULL || NdisProtocolHandle == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (ProtocolDriverCharacteristics->Header.Type !=
        NDIS_OBJECT_TYPE_PROTOCOL_DRIVER_CHARACTERISTICS)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Ndis6ProtocolDriverListInit();

    Block = (PNDIS6_PROTOCOL_DRIVER_BLOCK)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_PROTOCOL_DRIVER_BLOCK),
        NDIS6_PROTOCOL_DRIVER_TAG);
    if (Block == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Block, sizeof(*Block));
    Block->ProtocolDriverContext = ProtocolDriverContext;
    /* Copy only Header.Size bytes — a REV_1 (NDIS 6.0) caller's struct ends
     * at SendNetBufferListsCompleteHandler; the REV_2 direct-OID completion
     * handler stays NULL from the zero-fill. */
    {
        USHORT CharSize = ProtocolDriverCharacteristics->Header.Size;
        if (CharSize == 0 || CharSize > sizeof(Block->Characteristics))
            CharSize = sizeof(Block->Characteristics);
        RtlCopyMemory(&Block->Characteristics, ProtocolDriverCharacteristics, CharSize);
    }

    KeAcquireSpinLock(&g_Ndis6ProtocolDriverListLock, &OldIrql);
    InsertTailList(&g_Ndis6ProtocolDriverList, &Block->ListEntry);
    KeReleaseSpinLock(&g_Ndis6ProtocolDriverListLock, OldIrql);

    *NdisProtocolHandle = (NDIS_HANDLE)Block;

    /* Phase 7A: bind this new protocol to every NDIS 6 adapter that
     * already exists. The protocol's BindAdapterHandlerEx is expected
     * to call NdisOpenAdapterEx synchronously to actually take the
     * binding — without that API the bind is informational only. */
    Ndis6BindProtocolToAllAdapters(Block);

    return NDIS_STATUS_SUCCESS;
}

VOID
EXPORT
NdisDeregisterProtocolDriver(
    _In_ NDIS_HANDLE NdisProtocolHandle)
{
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block = (PNDIS6_PROTOCOL_DRIVER_BLOCK)NdisProtocolHandle;
    KIRQL OldIrql;

    if (Block == NULL)
        return;

    KeAcquireSpinLock(&g_Ndis6ProtocolDriverListLock, &OldIrql);
    if (Block->ListEntry.Flink != NULL && Block->ListEntry.Blink != NULL)
        RemoveEntryList(&Block->ListEntry);
    KeReleaseSpinLock(&g_Ndis6ProtocolDriverListLock, OldIrql);

    ExFreePoolWithTag(Block, NDIS6_PROTOCOL_DRIVER_TAG);
}

/* ============================================================================
 *  NdisOpenAdapterEx / NdisCloseAdapterEx
 *
 *  Phase 9C: real per-binding context for native NDIS 6 protocols. The
 *  protocol calls NdisOpenAdapterEx from inside its BindAdapterHandlerEx
 *  callback to take a real binding. We allocate an NDIS6_PROTOCOL_BINDING
 *  with backptrs to the adapter and the protocol driver block, and hand
 *  it back as the binding handle. The protocol uses that handle for all
 *  subsequent operations on this binding (NdisOidRequest, etc.).
 *
 *  OpenParameters is an NDIS_OPEN_PARAMETERS struct containing the
 *  medium array, frame type array, and selected medium index pointer.
 *  We pick the first NdisMedium802_3 we find and report it back.
 *
 *  Synchronous open only — the protocol's OpenAdapterCompleteHandlerEx
 *  is invoked before this returns. PENDING completion would need a
 *  per-binding waiter we don't yet implement.
 * ============================================================================ */

#define NDIS6_PROTOCOL_BINDING_TAG  'bPNn'  /* "nNPb" */

NDIS_STATUS
NTAPI
NdisOpenAdapterEx(
    _In_  NDIS_HANDLE  NdisProtocolHandle,
    _In_  NDIS_HANDLE  ProtocolBindingContext,
    _In_  PVOID        OpenParameters,
    _In_  NDIS_HANDLE  BindContext,
    _Out_ PNDIS_HANDLE NdisBindingHandle)
{
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block = (PNDIS6_PROTOCOL_DRIVER_BLOCK)NdisProtocolHandle;
    PNDIS6_PROTOCOL_BINDING      Binding;
    PLOGICAL_ADAPTER             Adapter;
    PNDIS6_ADAPTER_EXT           Ext;
    PNDIS_OPEN_PARAMETERS        OpenParams = (PNDIS_OPEN_PARAMETERS)OpenParameters;

    if (Block == NULL || NdisBindingHandle == NULL || BindContext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    *NdisBindingHandle = NULL;

    /* BindContext is the adapter handle: the bind walkers pass the
     * PLOGICAL_ADAPTER (= NDIS_BIND_PARAMETERS.MiniportHandle) as BindContext
     * and the protocol threads it through unchanged. Reject the protocol
     * handle (Block) itself - that can never be a valid adapter. */
    Adapter = NULL;
    if (BindContext != (NDIS_HANDLE)Block)
        Adapter = (PLOGICAL_ADAPTER)BindContext;
    if (Adapter == NULL || !Adapter->IsNdis6)
        return NDIS_STATUS_FAILURE;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return NDIS_STATUS_FAILURE;

    Binding = (PNDIS6_PROTOCOL_BINDING)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_PROTOCOL_BINDING),
        NDIS6_PROTOCOL_BINDING_TAG);
    if (Binding == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Binding, sizeof(*Binding));
    Binding->DriverBlock            = Block;
    Binding->Adapter                = Adapter;
    Binding->ProtocolBindingContext = ProtocolBindingContext;
    /* D4: initialize per-binding pending-OID list. */
    InitializeListHead(&Binding->PendingOidRequests);
    KeInitializeSpinLock(&Binding->PendingOidRequestsLock);
    ExInitializeRundownProtection(&Binding->RundownRef);
    Binding->Closing = FALSE;

    /* D6: parse NDIS_OPEN_PARAMETERS and pick a medium. Walk the
     * MediumArray looking for NdisMedium802_3 (the only medium the
     * bridge currently bridges NDIS 5↔6). Report the matching index
     * via SelectedMediumIndex so the protocol knows which medium
     * won. If nothing matches, fail the open with NDIS_STATUS_UNSUPPORTED_MEDIA. */
    if (OpenParams != NULL)
    {
        NDIS_MEDIUM  AdapterMedium = NdisMedium802_3;
        UINT         i;
        BOOLEAN      Matched = FALSE;
        if (Ext != NULL && Ext->GeneralAttrsValid)
            AdapterMedium = Ext->GeneralAttrs.MediaType;

        if (OpenParams->MediumArray != NULL && OpenParams->MediumArraySize > 0)
        {
            for (i = 0; i < OpenParams->MediumArraySize; i++)
            {
                if (OpenParams->MediumArray[i] == AdapterMedium)
                {
                    if (OpenParams->SelectedMediumIndex != NULL)
                        *OpenParams->SelectedMediumIndex = i;
                    Matched = TRUE;
                    break;
                }
            }
            if (!Matched)
            {
                ExFreePoolWithTag(Binding, NDIS6_PROTOCOL_BINDING_TAG);
                return NDIS_STATUS_UNSUPPORTED_MEDIA;
            }
        }
        /* FrameTypeArray: ignored — we deliver raw Ethernet II frames
         * to the protocol and let it decode. */
    }

    /* Link this binding into the adapter's native-protocol list. The current
     * receive bridge cannot safely fan one mutable NBL out to several native
     * protocols, so reject a second native open instead of allowing two
     * independent returns to reach the miniport. Must happen before
     * OpenAdapterCompleteHandlerEx, from which the protocol may start sending. */
    {
        KIRQL OldIrql;

        KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &OldIrql);
        if (!IsListEmpty(&Ext->ProtocolBindingList))
        {
            KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);
            ExFreePoolWithTag(Binding, NDIS6_PROTOCOL_BINDING_TAG);
            return NDIS_STATUS_OPEN_FAILED;
        }
        InsertTailList(&Ext->ProtocolBindingList, &Binding->AdapterLink);
        KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);
    }

    *NdisBindingHandle = (NDIS_HANDLE)Binding;

    /* Synchronous open complete — call the protocol's
     * OpenAdapterCompleteHandlerEx if it has one, then return SUCCESS. */
    if (Block->Characteristics.OpenAdapterCompleteHandlerEx != NULL)
    {
        Block->Characteristics.OpenAdapterCompleteHandlerEx(
            ProtocolBindingContext, NDIS_STATUS_SUCCESS);
    }

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NdisCloseAdapterEx(
    _In_ NDIS_HANDLE NdisBindingHandle)
{
    PNDIS6_PROTOCOL_BINDING Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block;

    if (Binding == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    Block = Binding->DriverBlock;
    InterlockedExchange(&Binding->Closing, TRUE);

    /* Unlink from the adapter's native-protocol list before freeing the
     * binding, so the datapath never observes a dangling AdapterLink. */
    if (Binding->Adapter != NULL)
    {
        PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Binding->Adapter);
        if (Ext != NULL)
        {
            KIRQL OldIrql;
            KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &OldIrql);
            if (Binding->AdapterLink.Flink != NULL &&
                Binding->AdapterLink.Blink != NULL)
            {
                RemoveEntryList(&Binding->AdapterLink);
                Binding->AdapterLink.Flink = NULL;
                Binding->AdapterLink.Blink = NULL;
            }
            KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);
        }
    }

    ExWaitForRundownProtectionRelease(&Binding->RundownRef);

    /* Synchronous close complete. */
    if (Block != NULL &&
        Block->Characteristics.CloseAdapterCompleteHandlerEx != NULL)
    {
        Block->Characteristics.CloseAdapterCompleteHandlerEx(
            Binding->ProtocolBindingContext);
    }

    ExFreePoolWithTag(Binding, NDIS6_PROTOCOL_BINDING_TAG);
    return NDIS_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  Protocol-side datapath stubs                                      */
/*  (Native NDIS 6 protocols only — currently nothing in the tree)    */
/* ------------------------------------------------------------------ */

VOID
EXPORT
NdisSendNetBufferLists(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PNDIS6_PROTOCOL_BINDING Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;
    PLOGICAL_ADAPTER        Adapter;
    PNDIS6_ADAPTER_EXT      Ext;
    PNET_BUFFER_LIST        CurrentNbl;
    PNET_BUFFER_LIST        NextNbl;

    UNREFERENCED_PARAMETER(PortNumber);
    UNREFERENCED_PARAMETER(SendFlags);

    if (Binding == NULL || NetBufferList == NULL)
        return;

    if (!Ndis6ReferenceProtocolBinding(Binding))
    {
        /* The binding is closing (or already ran down); without a reference
         * it may be freed at any moment, so the completion handler cannot be
         * called. The NBLs are caller-owned — flag them and bail. A protocol
         * racing sends against its own NdisCloseAdapterEx violates the NDIS
         * contract and forfeits the completion callback. */
        for (CurrentNbl = NetBufferList; CurrentNbl != NULL;
             CurrentNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl))
        {
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_CLOSING;
        }
        return;
    }

    Adapter = Binding->Adapter;
    Ext = (Adapter != NULL && Adapter->IsNdis6) ? NDIS6_EXT(Adapter) : NULL;
    if (Ext == NULL || Ext->DriverBlock == NULL || Ext->DriverBlock->Characteristics.SendNetBufferListsHandler == NULL)
    {
        /* No usable adapter underneath, but the binding reference is held, so
         * the chain can be handed back through the completion handler. */
        for (CurrentNbl = NetBufferList; CurrentNbl != NULL;
             CurrentNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl))
        {
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_FAILURE;
        }
        if (Binding->DriverBlock != NULL &&
            Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler != NULL)
        {
            Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(
                Binding->ProtocolBindingContext, NetBufferList, 0);
        }
        Ndis6DereferenceProtocolBinding(Binding);
        return;
    }

    /* SourceHandle = Binding marks a native send so completion routes to the
     * protocol's SendNetBufferListsCompleteHandler, not the legacy bridge
     * path. NdisReserved[1] carries the binding reference ownership marker
     * until the completion path releases it. */
    for (CurrentNbl = NetBufferList; CurrentNbl != NULL; CurrentNbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);
        NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = NULL;

        if (!Ndis6ReferenceProtocolBinding(Binding))
        {
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_CLOSING;
            if (Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler != NULL)
                Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(Binding->ProtocolBindingContext, CurrentNbl, 0);
            continue;
        }

        if (!Ndis6ReferenceNativeTransmit(Ext))
        {
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_PAUSED;
            if (Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler != NULL)
                Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(Binding->ProtocolBindingContext, CurrentNbl, 0);
            Ndis6DereferenceProtocolBinding(Binding);
            continue;
        }

        CurrentNbl->SourceHandle      = (NDIS_HANDLE)Binding;
        CurrentNbl->NdisReserved[1]   = Binding;

        Ndis6FilterDispatchSend(Adapter, CurrentNbl);
    }

    Ndis6DereferenceProtocolBinding(Binding);
}

VOID
EXPORT
NdisReturnNetBufferLists(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PNDIS6_PROTOCOL_BINDING Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;
    PNET_BUFFER_LIST        CurrentNbl;
    PNET_BUFFER_LIST        NextNbl;

    if (Binding == NULL || NetBufferLists == NULL)
        return;

    /* Hand the receive NBLs back to the miniport's ReturnNetBufferListsHandler
     * via the filter chain. Each NBL carries the native receive context that
     * keeps this binding alive until the protocol releases its final NBL. */
    for (CurrentNbl = NetBufferLists; CurrentNbl != NULL; CurrentNbl = NextNbl)
    {
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);
        NET_BUFFER_LIST_NEXT_NBL(CurrentNbl) = NULL;

        Ndis6RxReturnNativeNetBufferList(Binding, CurrentNbl, ReturnFlags);
    }
}

#define NDIS6_PROTOCOL_PENDING_OID_TAG  'dOPn'

NDIS_STATUS
EXPORT
NdisOidRequest(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    /* D4: Native NDIS 6 protocol OID request with proper async
     * completion routing. NdisBindingHandle here is a
     * PNDIS6_PROTOCOL_BINDING (from NdisOpenAdapterEx) OR a raw
     * PLOGICAL_ADAPTER for legacy bindings. We disambiguate by
     * checking whether IsNdis6 is set on the object at offset 0. */
    PNDIS6_PROTOCOL_BINDING    Binding = NULL;
    PLOGICAL_ADAPTER           Adapter = NULL;
    PNDIS6_ADAPTER_EXT         Ext;
    PNDIS6_PROTOCOL_PENDING_OID Pending = NULL;
    NDIS_STATUS                Status;
    KIRQL                      OldIrql;

    if (NdisBindingHandle == NULL || OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    /* Try to interpret the handle as a protocol binding first. Binding's
     * Adapter field should point to a valid LOGICAL_ADAPTER with IsNdis6. */
    Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;
    if (Binding->Adapter != NULL && Binding->Adapter->IsNdis6 &&
        Binding->DriverBlock != NULL)
    {
        Adapter = Binding->Adapter;
    }
    else
    {
        /* Fall back: handle is a raw adapter pointer (Phase 1 path). */
        Binding = NULL;
        Adapter = (PLOGICAL_ADAPTER)NdisBindingHandle;
        if (!Adapter->IsNdis6)
            return NDIS_STATUS_INVALID_PARAMETER;
    }

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.OidRequestHandler == NULL)
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    /* For a real protocol binding we can wire async completion. Allocate
     * a pending context, save the protocol's RequestId, replace it with
     * a pointer to the context so NdisMOidRequestComplete can find it. */
    if (Binding != NULL &&
        Binding->DriverBlock->Characteristics.OidRequestCompleteHandler != NULL)
    {
        Pending = (PNDIS6_PROTOCOL_PENDING_OID)ExAllocatePoolWithTag(
            NonPagedPool, sizeof(NDIS6_PROTOCOL_PENDING_OID),
            NDIS6_PROTOCOL_PENDING_OID_TAG);
        if (Pending == NULL)
            return NDIS_STATUS_RESOURCES;

        Pending->Binding           = Binding;
        Pending->OriginalRequestId = OidRequest->RequestId;
        Pending->OidRequest        = OidRequest;

        KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &OldIrql);
        InsertTailList(&Binding->PendingOidRequests, &Pending->ListEntry);
        KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);

        /* Swap in our context pointer as the RequestId. The driver
         * doesn't care what's there; it just echoes it back via
         * NdisMOidRequestComplete. */
        OidRequest->RequestId = Pending;
    }

    Status = Ext->DriverBlock->Characteristics.OidRequestHandler(
        Ext->MiniportAdapterContext, OidRequest);

    if (Status == NDIS_STATUS_PENDING)
    {
        /* Async completion will come via NdisMOidRequestComplete →
         * the per-binding pending list → protocol's completion handler. */
        return NDIS_STATUS_PENDING;
    }

    /* Synchronous completion — restore the original RequestId and
     * pop the pending entry. */
    if (Pending != NULL)
    {
        OidRequest->RequestId = Pending->OriginalRequestId;
        KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &OldIrql);
        RemoveEntryList(&Pending->ListEntry);
        KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);
        ExFreePoolWithTag(Pending, NDIS6_PROTOCOL_PENDING_OID_TAG);
    }
    return Status;
}

/* ============================================================================
 *  NDIS 6.1 direct OID request path — unserialized OIDs the miniport
 *  declared via the REV_2 characteristics' DirectOidRequestHandler.
 *  Mirrors NdisOidRequest, but dispatches to the direct handler and
 *  completes through NdisMDirectOidRequestComplete → the protocol's
 *  DirectOidRequestCompleteHandler.
 * ============================================================================ */

NDIS_STATUS
EXPORT
NdisDirectOidRequest(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_PROTOCOL_BINDING     Binding = NULL;
    PLOGICAL_ADAPTER            Adapter = NULL;
    PNDIS6_ADAPTER_EXT          Ext;
    PNDIS6_PROTOCOL_PENDING_OID Pending = NULL;
    NDIS_STATUS                 Status;
    KIRQL                       OldIrql;

    if (NdisBindingHandle == NULL || OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;
    if (Binding->Adapter != NULL && Binding->Adapter->IsNdis6 && Binding->DriverBlock != NULL)
    {
        Adapter = Binding->Adapter;
    }
    else
    {
        Binding = NULL;
        Adapter = (PLOGICAL_ADAPTER)NdisBindingHandle;
        if (!Adapter->IsNdis6)
            return NDIS_STATUS_INVALID_PARAMETER;
    }

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL || Ext->DriverBlock->Characteristics.DirectOidRequestHandler == NULL)
        return NDIS_STATUS_NOT_SUPPORTED;

    if (Binding != NULL && Binding->DriverBlock->Characteristics.DirectOidRequestCompleteHandler != NULL)
    {
        Pending = (PNDIS6_PROTOCOL_PENDING_OID)ExAllocatePoolWithTag(NonPagedPool, sizeof(NDIS6_PROTOCOL_PENDING_OID), NDIS6_PROTOCOL_PENDING_OID_TAG);
        if (Pending == NULL)
            return NDIS_STATUS_RESOURCES;

        Pending->Binding           = Binding;
        Pending->OriginalRequestId = OidRequest->RequestId;
        Pending->OidRequest        = OidRequest;

        KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &OldIrql);
        InsertTailList(&Binding->PendingOidRequests, &Pending->ListEntry);
        KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);

        OidRequest->RequestId = Pending;
    }

    Status = Ext->DriverBlock->Characteristics.DirectOidRequestHandler(Ext->MiniportAdapterContext, OidRequest);

    if (Status == NDIS_STATUS_PENDING)
        return NDIS_STATUS_PENDING;

    if (Pending != NULL)
    {
        OidRequest->RequestId = Pending->OriginalRequestId;
        KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &OldIrql);
        RemoveEntryList(&Pending->ListEntry);
        KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);
        ExFreePoolWithTag(Pending, NDIS6_PROTOCOL_PENDING_OID_TAG);
    }
    return Status;
}

VOID
EXPORT
NdisCancelDirectOidRequest(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PVOID RequestId)
{
    PNDIS6_PROTOCOL_BINDING Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;
    PLOGICAL_ADAPTER        Adapter;
    PNDIS6_ADAPTER_EXT      Ext;

    if (Binding == NULL)
        return;

    if (Binding->Adapter != NULL && Binding->Adapter->IsNdis6 && Binding->DriverBlock != NULL)
    {
        Adapter = Binding->Adapter;
    }
    else
    {
        Adapter = (PLOGICAL_ADAPTER)NdisBindingHandle;
        if (!Adapter->IsNdis6)
            return;
    }

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL || Ext->DriverBlock->Characteristics.CancelDirectOidRequestHandler == NULL)
        return;

    Ext->DriverBlock->Characteristics.CancelDirectOidRequestHandler(Ext->MiniportAdapterContext, RequestId);
}

VOID
EXPORT
NdisMDirectOidRequestComplete(
    _In_ NDIS_HANDLE       NdisMiniportHandle,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS       Status)
{
    PLOGICAL_ADAPTER            Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;
    PNDIS6_PROTOCOL_PENDING_OID Pending;
    PNDIS6_PROTOCOL_BINDING     Binding;
    KIRQL                       OldIrql;

    if (Adapter == NULL || !Adapter->IsNdis6 || OidRequest == NULL)
        return;

    /* Direct OIDs are only issued through NdisDirectOidRequest, which
     * stashes the pending context in RequestId when the protocol has a
     * DirectOidRequestCompleteHandler; otherwise nothing pends and this
     * call must not happen. */
    Pending = (PNDIS6_PROTOCOL_PENDING_OID)OidRequest->RequestId;
    if (Pending == NULL)
        return;

    Binding = Pending->Binding;
    if (Binding == NULL || Binding->DriverBlock == NULL)
        return;

    KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &OldIrql);
    RemoveEntryList(&Pending->ListEntry);
    KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);

    OidRequest->RequestId = Pending->OriginalRequestId;
    if (Binding->DriverBlock->Characteristics.DirectOidRequestCompleteHandler != NULL)
        Binding->DriverBlock->Characteristics.DirectOidRequestCompleteHandler(Binding->ProtocolBindingContext, OidRequest, Status);
    ExFreePoolWithTag(Pending, NDIS6_PROTOCOL_PENDING_OID_TAG);
}

/* ------------------------------------------------------------------ */
/*  Miniport-side datapath callbacks                                   */
/*  These are called BY the NDIS 6 driver and should be picked up by  */
/*  the bridge thunks. Phase 3/4 will move them to 60thunk.c with     */
/*  real NDIS 5↔6 packet translation. Until then they swallow the    */
/*  call so the driver doesn't deadlock waiting for completion.       */
/* ------------------------------------------------------------------ */

/* NdisMSendNetBufferListsComplete moved to 60thunk_tx.c (real impl). */

/* NdisMIndicateReceiveNetBufferLists moved to 60thunk_rx.c (real impl). */
/* NdisMIndicateStatusEx moved to 60thunk_rx.c (real impl). */

/* NdisMOidRequestComplete moved to 60oid.c (real impl). */

/* ============================================================================
 *  E1: NdisRegisterDeviceEx / NdisDeregisterDeviceEx
 *
 *  Creates a separate control device object (DO) for miniports that expose
 *  an IOCTL interface unrelated to the adapter's datapath. Used by WFP
 *  callout drivers, WWAN control channels, and filter drivers that need
 *  their own user-mode interface.
 *
 *  The caller provides NDIS_DEVICE_OBJECT_ATTRIBUTES with device name,
 *  symbolic link name, and a PDRIVER_DISPATCH array for the major
 *  functions (Create/Close/Cleanup/DeviceControl). We call IoCreateDevice,
 *  install the dispatch routines on the driver object, and optionally
 *  create a symbolic link.
 * ============================================================================ */

typedef struct _NDIS6_CONTROL_DEVICE
{
    LIST_ENTRY          ListEntry;
    PDEVICE_OBJECT      DeviceObject;
    UNICODE_STRING      SymbolicName;
    BOOLEAN             SymbolicLinkCreated;
    /* The control driver's own major-function handlers. IRPs are demuxed by
     * target device (Ndis6ControlDemuxDispatch) so one driver's handlers never
     * run for another device sharing the driver object (e.g. the miniport FDO,
     * or another driver's control device). */
    PDRIVER_DISPATCH    MajorFunctions[IRP_MJ_MAXIMUM_FUNCTION + 1];
} NDIS6_CONTROL_DEVICE, *PNDIS6_CONTROL_DEVICE;

/* PNP/POWER/SYSTEM_CONTROL on a miniport driver object belong to NDIS, never to
 * a control driver's handlers. */
#define NDIS6_MJ_IS_NDIS_OWNED(mj) \
    ((mj) == IRP_MJ_PNP || (mj) == IRP_MJ_POWER || (mj) == IRP_MJ_SYSTEM_CONTROL)

/* All live control devices, so NdisGetDeviceReservedExtension can tell a
 * NdisRegisterDeviceEx device object apart from a miniport FDO. */
static LIST_ENTRY g_Ndis6CtlDevList = { &g_Ndis6CtlDevList, &g_Ndis6CtlDevList };
static KSPIN_LOCK g_Ndis6CtlDevLock;

PDEVICE_OBJECT
Ndis6GetFilterOrControlIoWorkItemObject(
    _In_ NDIS_HANDLE NdisObjectHandle)
{
    PDEVICE_OBJECT Object = NULL;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    Ndis6FilterDriverListInit();
    KeAcquireSpinLock(&g_Ndis6FilterDriverListLock, &OldIrql);
    for (Entry = g_Ndis6FilterDriverList.Flink;
         Entry != &g_Ndis6FilterDriverList;
         Entry = Entry->Flink)
    {
        PNDIS6_FILTER_DRIVER_BLOCK Block =
            CONTAINING_RECORD(Entry, NDIS6_FILTER_DRIVER_BLOCK, ListEntry);

        if ((NDIS_HANDLE)Block == NdisObjectHandle)
        {
            Object = (PDEVICE_OBJECT)Block->DriverObject;
            break;
        }
    }
    KeReleaseSpinLock(&g_Ndis6FilterDriverListLock, OldIrql);

    if (Object != NULL)
        return Object;

    KeAcquireSpinLock(&g_Ndis6CtlDevLock, &OldIrql);
    for (Entry = g_Ndis6CtlDevList.Flink;
         Entry != &g_Ndis6CtlDevList;
         Entry = Entry->Flink)
    {
        PNDIS6_CONTROL_DEVICE CtlDev =
            CONTAINING_RECORD(Entry, NDIS6_CONTROL_DEVICE, ListEntry);

        if ((NDIS_HANDLE)CtlDev == NdisObjectHandle)
        {
            Object = CtlDev->DeviceObject;
            break;
        }
    }
    KeReleaseSpinLock(&g_Ndis6CtlDevLock, OldIrql);
    return Object;
}

/* TRUE if DeviceObject is a control device created by NdisRegisterDeviceEx.
 * The hybrid KMDF demux (60driver.c) uses this so a control device with no
 * handler for a major function is never misrouted into KMDF's dispatch. */
BOOLEAN
Ndis6DeviceIsControlDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PLIST_ENTRY Entry;
    BOOLEAN     Found = FALSE;
    KIRQL       OldIrql;

    KeAcquireSpinLock(&g_Ndis6CtlDevLock, &OldIrql);
    for (Entry = g_Ndis6CtlDevList.Flink; Entry != &g_Ndis6CtlDevList; Entry = Entry->Flink)
    {
        PNDIS6_CONTROL_DEVICE CtlDev = CONTAINING_RECORD(Entry, NDIS6_CONTROL_DEVICE, ListEntry);
        if (CtlDev->DeviceObject == DeviceObject)
        {
            Found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&g_Ndis6CtlDevLock, OldIrql);
    return Found;
}

/*
 * Ndis6ControlDemuxDispatch
 *
 * Single IRP entry point installed on a miniport driver object for the major
 * functions a control driver registered. It routes each IRP to the control
 * device that owns it, so a control driver's handler never runs for the
 * miniport FDO or for a different driver's control device that happens to share
 * the driver object. Unknown targets (miniport FDO opens) complete benignly.
 */
static NTSTATUS
NTAPI
Ndis6ControlDemuxDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION    IoStack = IoGetCurrentIrpStackLocation(Irp);
    PNDIS6_CONTROL_DEVICE Match = NULL;
    PLIST_ENTRY           Entry;
    KIRQL                 OldIrql;
    NTSTATUS              Status;

    KeAcquireSpinLock(&g_Ndis6CtlDevLock, &OldIrql);
    for (Entry = g_Ndis6CtlDevList.Flink;
         Entry != &g_Ndis6CtlDevList;
         Entry = Entry->Flink)
    {
        PNDIS6_CONTROL_DEVICE CtlDev =
            CONTAINING_RECORD(Entry, NDIS6_CONTROL_DEVICE, ListEntry);
        if (CtlDev->DeviceObject == DeviceObject)
        {
            Match = CtlDev;
            break;
        }
    }
    KeReleaseSpinLock(&g_Ndis6CtlDevLock, OldIrql);

    if (Match != NULL && Match->MajorFunctions[IoStack->MajorFunction] != NULL)
        return Match->MajorFunctions[IoStack->MajorFunction](DeviceObject, Irp);

    /* Hybrid KMDF+NDIS driver: NdisRegisterDeviceEx overrode dispatch slots
     * KMDF had installed, so IRPs for KMDF's own device objects land here.
     * Hand them back to the dispatch KMDF originally registered. */
    if (Match == NULL)
    {
        PDRIVER_DISPATCH Original = Ndis6HybridGetOriginalDispatch(DeviceObject, IoStack->MajorFunction);
        if (Original != NULL)
            return Original(DeviceObject, Irp);

        /* Miniport FDO: user-mode opens + the global-stats OID IOCTL. */
        if (Ndis6TryDispatchAdapterFdoIrp(DeviceObject, Irp, &Status))
            return Status;
    }

    /* Not a control device (e.g. a user-mode open of the miniport FDO): a bare
     * open/close succeeds, everything else is unsupported. */
    Status = (IoStack->MajorFunction == IRP_MJ_CREATE ||
              IoStack->MajorFunction == IRP_MJ_CLOSE ||
              IoStack->MajorFunction == IRP_MJ_CLEANUP)
                 ? STATUS_SUCCESS : STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NDIS_STATUS
NTAPI
NdisRegisterDeviceEx(
    _In_  NDIS_HANDLE   NdisHandle,
    _In_  PNDIS_DEVICE_OBJECT_ATTRIBUTES DeviceObjectAttributes,
    _Out_ PDEVICE_OBJECT* pDeviceObject,
    _Out_ PNDIS_HANDLE  NdisDeviceHandle)
{
    PNDIS_DEVICE_OBJECT_ATTRIBUTES Attrs = DeviceObjectAttributes;
    PNDIS6_CONTROL_DEVICE   CtlDev;
    PDRIVER_OBJECT          DriverObject = NULL;
    PDEVICE_OBJECT          Device       = NULL;
    NTSTATUS                Status;
    ULONG                   DeviceExtensionSize;
    ULONG                   i;
    KIRQL                   OldIrql;

    if (pDeviceObject != NULL)
        *pDeviceObject = NULL;
    if (NdisDeviceHandle != NULL)
        *NdisDeviceHandle = NULL;

    if (Attrs == NULL || pDeviceObject == NULL || NdisDeviceHandle == NULL ||
        Attrs->DeviceName == NULL || Attrs->MajorFunctions == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    if (Attrs->ExtensionSize > MAXULONG - sizeof(*CtlDev))
        return NDIS_STATUS_INVALID_PARAMETER;
    DeviceExtensionSize = Attrs->ExtensionSize + sizeof(*CtlDev);

    if (Attrs->SymbolicName != NULL &&
        (Attrs->SymbolicName->Length > Attrs->SymbolicName->MaximumLength ||
         (Attrs->SymbolicName->Length & (sizeof(WCHAR) - 1)) != 0 ||
         Attrs->SymbolicName->Length > MAXUSHORT - sizeof(WCHAR) ||
         (Attrs->SymbolicName->Length != 0 &&
          Attrs->SymbolicName->Buffer == NULL)))
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    /* Resolve the caller's own driver object. For a miniport, NdisHandle is the
     * NDIS6_DRIVER_BLOCK returned by NdisMRegisterMiniportDriver; validate it
     * against the registered-driver list and use ITS driver object. Using the
     * wrong driver object cross-installs a control driver's handlers over an
     * unrelated device (e.g. another driver's \Device\Nwifi), which then faults
     * inside that driver's handler. */
    KeAcquireSpinLock(&g_Ndis6DriverListLock, &OldIrql);
    for (PLIST_ENTRY Entry = g_Ndis6DriverList.Flink;
         Entry != &g_Ndis6DriverList;
         Entry = Entry->Flink)
    {
        PNDIS6_DRIVER_BLOCK Block =
            CONTAINING_RECORD(Entry, NDIS6_DRIVER_BLOCK, ListEntry);
        if ((NDIS_HANDLE)Block == NdisHandle)
        {
            DriverObject = Block->DriverObject;
            break;
        }
    }
    KeReleaseSpinLock(&g_Ndis6DriverListLock, OldIrql);

    if (DriverObject == NULL)
    {
        Ndis6FilterDriverListInit();
        KeAcquireSpinLock(&g_Ndis6FilterDriverListLock, &OldIrql);
        for (PLIST_ENTRY Entry = g_Ndis6FilterDriverList.Flink;
             Entry != &g_Ndis6FilterDriverList;
             Entry = Entry->Flink)
        {
            PNDIS6_FILTER_DRIVER_BLOCK Block =
                CONTAINING_RECORD(Entry, NDIS6_FILTER_DRIVER_BLOCK, ListEntry);
            if ((NDIS_HANDLE)Block == NdisHandle)
            {
                DriverObject = Block->DriverObject;
                break;
            }
        }
        KeReleaseSpinLock(&g_Ndis6FilterDriverListLock, OldIrql);
    }

    if (DriverObject == NULL)
        return NDIS_STATUS_NOT_SUPPORTED;

    /* Keep the NDIS control block in the device extension so its lifetime is
     * protected by the I/O manager while an IRP dispatch is still active. */
    Status = IoCreateDevice(DriverObject,
                            DeviceExtensionSize,
                            Attrs->DeviceName,
                            FILE_DEVICE_NETWORK,
                            0,
                            FALSE,
                            &Device);
    if (!NT_SUCCESS(Status))
        return (NDIS_STATUS)Status;

    CtlDev = (PNDIS6_CONTROL_DEVICE)Device->DeviceExtension;
    RtlZeroMemory(CtlDev, sizeof(*CtlDev));
    CtlDev->DeviceObject = Device;

    if (Attrs->SymbolicName != NULL)
    {
        Status = RtlDuplicateUnicodeString(
            RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
            Attrs->SymbolicName,
            &CtlDev->SymbolicName);
        if (!NT_SUCCESS(Status))
        {
            IoDeleteDevice(Device);
            return (NDIS_STATUS)Status;
        }
    }

    /* Keep the caller's handlers on the control device, and route the driver
     * object's matching slots through the per-device demux (never the raw
     * handler, so the miniport FDO and other devices are not affected). NDIS
     * keeps PNP/POWER/SYSTEM_CONTROL. */
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
    {
        if (Attrs->MajorFunctions[i] != NULL && !NDIS6_MJ_IS_NDIS_OWNED(i))
        {
            CtlDev->MajorFunctions[i]      = Attrs->MajorFunctions[i];
            DriverObject->MajorFunction[i] = Ndis6ControlDemuxDispatch;
        }
    }

    /* Symbolic link so user-mode can CreateFile on it. */
    if (Attrs->SymbolicName != NULL)
    {
        Status = IoCreateSymbolicLink(&CtlDev->SymbolicName,
                                      Attrs->DeviceName);
        if (NT_SUCCESS(Status))
        {
            CtlDev->SymbolicLinkCreated  = TRUE;
        }
        else
        {
            /* Symbolic link failure is non-fatal. */
            RtlFreeUnicodeString(&CtlDev->SymbolicName);
        }
    }

    Device->Flags &= ~DO_DEVICE_INITIALIZING;

    ExInterlockedInsertTailList(&g_Ndis6CtlDevList, &CtlDev->ListEntry,
                                &g_Ndis6CtlDevLock);

    *pDeviceObject    = Device;
    *NdisDeviceHandle = (NDIS_HANDLE)CtlDev;
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NdisDeregisterDeviceEx(
    _In_ NDIS_HANDLE NdisDeviceHandle)
{
    PNDIS6_CONTROL_DEVICE CtlDev = (PNDIS6_CONTROL_DEVICE)NdisDeviceHandle;
    KIRQL OldIrql;

    if (CtlDev == NULL)
        return;

    KeAcquireSpinLock(&g_Ndis6CtlDevLock, &OldIrql);
    RemoveEntryList(&CtlDev->ListEntry);
    KeReleaseSpinLock(&g_Ndis6CtlDevLock, OldIrql);

    if (CtlDev->SymbolicLinkCreated)
        IoDeleteSymbolicLink(&CtlDev->SymbolicName);
    RtlFreeUnicodeString(&CtlDev->SymbolicName);
    IoDeleteDevice(CtlDev->DeviceObject);
}

/*
 * NdisGetDeviceReservedExtension
 *
 * For a device created by NdisRegisterDeviceEx the NDIS control block leads
 * the device extension and the caller's reserved area follows it. For a
 * miniport FDO the reserved area is the WdfReserved scratch space in
 * LOGICAL_ADAPTER, where NDIS-WDF miniports keep their framework context.
 */
PVOID
NTAPI
NdisGetDeviceReservedExtension(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PLOGICAL_ADAPTER Adapter;
    PLIST_ENTRY Entry;
    PNDIS6_CONTROL_DEVICE CtlDev = NULL;
    KIRQL OldIrql;
    BOOLEAN IsCtlDev = FALSE;

    if (DeviceObject == NULL)
        return NULL;

    KeAcquireSpinLock(&g_Ndis6CtlDevLock, &OldIrql);
    for (Entry = g_Ndis6CtlDevList.Flink;
         Entry != &g_Ndis6CtlDevList;
         Entry = Entry->Flink)
    {
        CtlDev = CONTAINING_RECORD(Entry,
                                   NDIS6_CONTROL_DEVICE,
                                   ListEntry);
        if (CtlDev->DeviceObject == DeviceObject)
        {
            IsCtlDev = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&g_Ndis6CtlDevLock, OldIrql);

    if (IsCtlDev)
        return (PVOID)(CtlDev + 1);

    /* WDF-managed NetAdapterCx FDOs do not store the logical adapter in
     * DeviceExtension, so resolve both native and WDF-owned FDOs through
     * the registered adapter list. */
    Adapter = Ndis6FindAdapterByFdo(DeviceObject);
    if (Adapter != NULL)
        return &Adapter->WdfReserved[0];

    return NULL;
}

/*
 * NdisOpenConfigurationEx
 *
 * NDIS 6 replacement for NdisOpenConfiguration. ConfigObject->NdisHandle is
 * the adapter handle from MiniportInitializeEx; the returned handle feeds the
 * existing NdisReadConfiguration/NdisCloseConfiguration implementation, so it
 * must be a MINIPORT_CONFIGURATION_CONTEXT holding the adapter's driver key.
 */
NDIS_STATUS
NTAPI
NdisOpenConfigurationEx(
    _In_  PVOID         ConfigObject,
    _Out_ PNDIS_HANDLE  ConfigurationHandle)
{
    PNDIS_CONFIGURATION_OBJECT Obj = (PNDIS_CONFIGURATION_OBJECT)ConfigObject;
    PMINIPORT_CONFIGURATION_CONTEXT Ctx;
    PLOGICAL_ADAPTER Adapter;
    HANDLE KeyHandle;
    NTSTATUS Status;

    if (Obj == NULL || ConfigurationHandle == NULL || Obj->NdisHandle == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    *ConfigurationHandle = NULL;

    Adapter = GET_LOGICAL_ADAPTER(Obj->NdisHandle);
    if (!Adapter->IsNdis6 ||
        Adapter->NdisMiniportBlock.DeviceObject == NULL ||
        Adapter->NdisMiniportBlock.PhysicalDeviceObject == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Status = IoOpenDeviceRegistryKey(
        Adapter->NdisMiniportBlock.PhysicalDeviceObject,
        PLUGPLAY_REGKEY_DRIVER,
        KEY_ALL_ACCESS,
        &KeyHandle);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("NDIS6: failed to open adapter driver key (0x%08X)\n", Status);
        return NDIS_STATUS_FAILURE;
    }

    Ctx = ExAllocatePool(NonPagedPool, sizeof(MINIPORT_CONFIGURATION_CONTEXT));
    if (Ctx == NULL)
    {
        ZwClose(KeyHandle);
        return NDIS_STATUS_RESOURCES;
    }

    KeInitializeSpinLock(&Ctx->ResourceLock);
    InitializeListHead(&Ctx->ResourceListHead);
    Ctx->Handle = KeyHandle;

    *ConfigurationHandle = (NDIS_HANDLE)Ctx;
    return NDIS_STATUS_SUCCESS;
}

/* EOF */
