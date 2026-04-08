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
    Block->Characteristics     = *FilterDriverCharacteristics;

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
            (NDIS_HANDLE)Block,        /* BindContext */
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
            (NDIS_HANDLE)Snapshot[i],
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
    Block->Characteristics       = *ProtocolDriverCharacteristics;

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

    UNREFERENCED_PARAMETER(OpenParameters);

    if (Block == NULL || NdisBindingHandle == NULL || BindContext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    *NdisBindingHandle = NULL;

    /* The bridge passes (NDIS_HANDLE)NDIS6_PROTOCOL_DRIVER_BLOCK as the
     * BindContext to BindAdapterHandlerEx, but the protocol typically
     * gets the adapter pointer via NDIS_BIND_PARAMETERS.MiniportHandle
     * (which we set to (NDIS_HANDLE)Adapter). Native protocols are
     * expected to remember the adapter from BindParameters and we'd
     * need a richer protocol-API surface to pass it through OpenParameters
     * cleanly. For now we look up the adapter via BindContext: the
     * protocol must pass the adapter pointer it received from
     * NDIS_BIND_PARAMETERS.MiniportHandle as BindContext. (This matches
     * our Phase 7A bind walker which set BindContext = Block.) */
    Adapter = NULL;
    if (BindContext != (NDIS_HANDLE)Block)
    {
        /* The driver passed a different value as BindContext — interpret
         * it as the MiniportHandle (= adapter pointer) the bridge gave
         * via NDIS_BIND_PARAMETERS. */
        Adapter = (PLOGICAL_ADAPTER)BindContext;
    }
    if (Adapter == NULL || !Adapter->IsNdis6)
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

    Block = Binding->DriverBlock;

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
    UNREFERENCED_PARAMETER(NdisBindingHandle);
    UNREFERENCED_PARAMETER(NetBufferList);
    UNREFERENCED_PARAMETER(PortNumber);
    UNREFERENCED_PARAMETER(SendFlags);
}

VOID
EXPORT
NdisReturnNetBufferLists(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    UNREFERENCED_PARAMETER(NdisBindingHandle);
    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(ReturnFlags);
}

NDIS_STATUS
EXPORT
NdisOidRequest(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    /* Phase 7C: forward the NDIS 6 protocol's OID request to the bound
     * miniport's OidRequestHandler. NdisBindingHandle is what we handed
     * the protocol via NDIS_BIND_PARAMETERS.MiniportHandle, which is the
     * PLOGICAL_ADAPTER pointer. (We don't yet implement NdisOpenAdapterEx
     * with a separate per-binding context, so the binding handle doubles
     * as the adapter pointer.)
     *
     * We can't deliver async completion to the protocol here because the
     * fan-out path from NdisMOidRequestComplete to the protocol's
     * OidRequestCompleteHandler isn't wired yet — so PENDING returns
     * are demoted to NOT_SUPPORTED as a graceful degradation. */
    PLOGICAL_ADAPTER   Adapter = (PLOGICAL_ADAPTER)NdisBindingHandle;
    PNDIS6_ADAPTER_EXT Ext;
    NDIS_STATUS        Status;

    if (Adapter == NULL || OidRequest == NULL || !Adapter->IsNdis6)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.OidRequestHandler == NULL)
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    Status = Ext->DriverBlock->Characteristics.OidRequestHandler(
        Ext->MiniportAdapterContext, OidRequest);

    if (Status == NDIS_STATUS_PENDING)
    {
        /* No completion routing yet — demote to NOT_SUPPORTED so the
         * protocol gracefully backs off. Future phase: wire async
         * completion via Ndis6OidWaiter or per-binding contexts. */
        return NDIS_STATUS_NOT_SUPPORTED;
    }
    return Status;
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

/* EOF */
