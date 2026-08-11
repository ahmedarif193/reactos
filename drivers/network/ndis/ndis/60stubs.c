/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NDIS library
 * FILE:        ndis/60stubs.c
 * PURPOSE:     NDIS 6 filter, protocol, and miscellaneous entry points.
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
 * This file retains filter and protocol registration/lifecycle entry points,
 * plus miscellaneous NDIS 6 exports that do not yet have a dedicated source
 * file. The filter and native-protocol data paths are implemented here and in
 * 60filter.c and the 60thunk_*.c bridge files.
 *
 * Like every NDIS 6 file in this directory, this one is compiled with
 * NDIS620_MINIPORT and SKIP_PRECOMPILE_HEADERS ON because the PCH is
 * locked at NDIS 5.1.
 */

#include "ndis6_internal.h"

#ifndef EXPORT
#define EXPORT NTAPI
#endif

/* ------------------------------------------------------------------ */
/*  Filter driver registration and per-adapter module lifecycle.      */
/* ------------------------------------------------------------------ */

LIST_ENTRY  g_Ndis6FilterDriverList;
KSPIN_LOCK  g_Ndis6FilterDriverListLock;
static KMUTEX g_Ndis6FilterLifecycleMutex;
static volatile LONG g_Ndis6FilterDriverListState;

#define NDIS6_FILTER_DRIVER_TAG  'fDNn'  /* "nNDf" */
#define NDIS6_FILTER_NAME_TAG    'sFNn'
#define NDIS6_FILTER_MODULE_TAG  'mFNn'  /* "nNFm" */
#define NDIS6_FILTER_SNAPSHOT_TAG 'aFNn'

static VOID
Ndis6DetachFilterModule(
    _In_ PNDIS6_FILTER_MODULE Module);

static VOID
Ndis6AttachFilterDriverToAllAdapters(
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block);

static VOID
Ndis6FilterDriverListInit(VOID)
{
    if (InterlockedCompareExchange(&g_Ndis6FilterDriverListState, 1, 0) == 0)
    {
        InitializeListHead(&g_Ndis6FilterDriverList);
        KeInitializeSpinLock(&g_Ndis6FilterDriverListLock);
        KeInitializeMutex(&g_Ndis6FilterLifecycleMutex, 0);
        InterlockedExchange(&g_Ndis6FilterDriverListState, 2);
        return;
    }

    while (InterlockedCompareExchange(&g_Ndis6FilterDriverListState, 2, 2) != 2)
        YieldProcessor();
}

static BOOLEAN
Ndis6IsSupportedFilterMinorVersion(
    _In_ UCHAR MinorVersion)
{
    switch (MinorVersion)
    {
        case 0:
        case 1:
        case 20:
        case 30:
        case 40:
        case 50:
        case 51:
        case 60:
        case 70:
        case 80:
        case 81:
        case 82:
        case 83:
        case 84:
        case 85:
        case 86:
        case 87:
        case 88:
        case 89:
            return TRUE;

        default:
            return FALSE;
    }
}

static BOOLEAN
Ndis6ValidateFilterName(
    _In_ PNDIS_STRING Name)
{
    return !((Name->Length != 0 && Name->Buffer == NULL) ||
             Name->Length > Name->MaximumLength ||
             Name->Length > MAXUSHORT - sizeof(WCHAR) ||
             (Name->Length & (sizeof(WCHAR) - 1)) != 0);
}

static NDIS_STATUS
Ndis6ValidateFilterCharacteristics(
    _In_ PNDIS_FILTER_DRIVER_CHARACTERISTICS Characteristics,
    _Out_ PULONG CopySize)
{
    ULONG AllowedFlags;
    ULONG RequiredSize;
    UCHAR RequiredRevision;

    if (Characteristics->Header.Type !=
        NDIS_OBJECT_TYPE_FILTER_DRIVER_CHARACTERISTICS)
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    if (Characteristics->MajorNdisVersion != 6 ||
        !Ndis6IsSupportedFilterMinorVersion(Characteristics->MinorNdisVersion))
    {
        return NDIS_STATUS_BAD_VERSION;
    }

    RequiredRevision = Characteristics->MinorNdisVersion >= 80
        ? NDIS_FILTER_DRIVER_CHARACTERISTICS_REVISION_3
        : (Characteristics->MinorNdisVersion >= 1
            ? NDIS_FILTER_DRIVER_CHARACTERISTICS_REVISION_2
            : NDIS_FILTER_DRIVER_CHARACTERISTICS_REVISION_1);
    if (Characteristics->Header.Revision != RequiredRevision)
        return NDIS_STATUS_BAD_VERSION;

    switch (RequiredRevision)
    {
        case NDIS_FILTER_DRIVER_CHARACTERISTICS_REVISION_1:
            RequiredSize = NDIS_SIZEOF_FILTER_DRIVER_CHARACTERISTICS_REVISION_1;
            break;
        case NDIS_FILTER_DRIVER_CHARACTERISTICS_REVISION_2:
            RequiredSize = NDIS_SIZEOF_FILTER_DRIVER_CHARACTERISTICS_REVISION_2;
            break;
        case NDIS_FILTER_DRIVER_CHARACTERISTICS_REVISION_3:
            RequiredSize = NDIS_SIZEOF_FILTER_DRIVER_CHARACTERISTICS_REVISION_3;
            break;
        default:
            return NDIS_STATUS_BAD_VERSION;
    }

    if (Characteristics->Header.Size != RequiredSize ||
        RequiredSize > sizeof(*Characteristics) ||
        Characteristics->AttachHandler == NULL ||
        Characteristics->DetachHandler == NULL ||
        Characteristics->RestartHandler == NULL ||
        Characteristics->PauseHandler == NULL ||
        (Characteristics->OidRequestHandler != NULL &&
         Characteristics->OidRequestCompleteHandler == NULL) ||
        (RequiredRevision >= NDIS_FILTER_DRIVER_CHARACTERISTICS_REVISION_2 &&
         Characteristics->DirectOidRequestCompleteHandler != NULL &&
         Characteristics->DirectOidRequestHandler == NULL) ||
        ((Characteristics->ReceiveNetBufferListsHandler != NULL ||
          Characteristics->ReturnNetBufferListsHandler != NULL) &&
         Characteristics->StatusHandler == NULL) ||
        !Ndis6ValidateFilterName(&Characteristics->FriendlyName) ||
        !Ndis6ValidateFilterName(&Characteristics->UniqueName) ||
        !Ndis6ValidateFilterName(&Characteristics->ServiceName))
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    AllowedFlags = NDIS_FILTER_DRIVER_MANDATORY;
    if (Characteristics->MinorNdisVersion >= 50)
    {
        AllowedFlags |= NDIS_FILTER_DRIVER_SUPPORTS_CURRENT_MAC_ADDRESS_CHANGE |
                        NDIS_FILTER_DRIVER_SUPPORTS_L2_MTU_SIZE_CHANGE;
    }
    if (Characteristics->MinorNdisVersion >= 89)
        AllowedFlags |= NDIS_FILTER_DRIVER_UDP_RSC_NOT_SUPPORTED;
    if ((Characteristics->Flags & ~AllowedFlags) != 0)
        return NDIS_STATUS_BAD_CHARACTERISTICS;

    *CopySize = RequiredSize;
    return NDIS_STATUS_SUCCESS;
}

static NDIS_STATUS
Ndis6CopyFilterName(
    _In_ PNDIS_STRING Source,
    _Out_ PNDIS_STRING Destination,
    _Out_ PWCHAR *OwnedBuffer)
{
    ULONG Bytes = Source->Length;

    *OwnedBuffer = NULL;
    RtlZeroMemory(Destination, sizeof(*Destination));
    if (Bytes == 0)
        return NDIS_STATUS_SUCCESS;

    *OwnedBuffer = ExAllocatePoolWithTag(PagedPool, Bytes + sizeof(WCHAR), NDIS6_FILTER_NAME_TAG);
    if (*OwnedBuffer == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlCopyMemory(*OwnedBuffer, Source->Buffer, Bytes);
    (*OwnedBuffer)[Bytes / sizeof(WCHAR)] = UNICODE_NULL;
    Destination->Buffer = *OwnedBuffer;
    Destination->Length = (USHORT)Bytes;
    Destination->MaximumLength = (USHORT)(Bytes + sizeof(WCHAR));
    return NDIS_STATUS_SUCCESS;
}

static VOID
Ndis6FreeFilterDriverBlock(
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block)
{
    Block->Signature = 0;
    if (Block->FriendlyNameBuffer != NULL)
        ExFreePoolWithTag(Block->FriendlyNameBuffer, NDIS6_FILTER_NAME_TAG);
    if (Block->UniqueNameBuffer != NULL)
        ExFreePoolWithTag(Block->UniqueNameBuffer, NDIS6_FILTER_NAME_TAG);
    if (Block->ServiceNameBuffer != NULL)
        ExFreePoolWithTag(Block->ServiceNameBuffer, NDIS6_FILTER_NAME_TAG);
    ExFreePoolWithTag(Block, NDIS6_FILTER_DRIVER_TAG);
}

static BOOLEAN
Ndis6ReferenceFilterDriver(
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block)
{
    if (Block == NULL || Block->Signature != NDIS6_FILTER_DRIVER_SIGNATURE ||
        InterlockedCompareExchange(&Block->Closing, FALSE, FALSE) != FALSE)
        return FALSE;

    if (!ExAcquireRundownProtection(&Block->CallbackRundown))
        return FALSE;

    if (Block->Signature != NDIS6_FILTER_DRIVER_SIGNATURE ||
        InterlockedCompareExchange(&Block->Closing, FALSE, FALSE) != FALSE)
    {
        ExReleaseRundownProtection(&Block->CallbackRundown);
        return FALSE;
    }

    return TRUE;
}

static VOID
Ndis6DereferenceFilterDriver(
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block)
{
    ExReleaseRundownProtection(&Block->CallbackRundown);
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
    NDIS_STATUS Status;
    ULONG CopySize;
    KIRQL OldIrql;

    if (DriverObject == NULL || FilterDriverCharacteristics == NULL ||
        NdisFilterDriverHandle == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    *NdisFilterDriverHandle = NULL;

    Status = Ndis6ValidateFilterCharacteristics(FilterDriverCharacteristics, &CopySize);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Ndis6FilterDriverListInit();

    Block = (PNDIS6_FILTER_DRIVER_BLOCK)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_FILTER_DRIVER_BLOCK),
        NDIS6_FILTER_DRIVER_TAG);
    if (Block == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Block, sizeof(*Block));
    InitializeListHead(&Block->ListEntry);
    InitializeListHead(&Block->ModuleList);
    KeInitializeSpinLock(&Block->ModuleListLock);
    ExInitializeRundownProtection(&Block->CallbackRundown);
    Block->Signature           = NDIS6_FILTER_DRIVER_SIGNATURE;
    Block->DriverObject        = DriverObject;
    Block->FilterDriverContext = FilterDriverContext;
    RtlCopyMemory(&Block->Characteristics, FilterDriverCharacteristics, CopySize);

    Status = Ndis6CopyFilterName(&FilterDriverCharacteristics->FriendlyName, &Block->Characteristics.FriendlyName, &Block->FriendlyNameBuffer);
    if (Status == NDIS_STATUS_SUCCESS)
        Status = Ndis6CopyFilterName(&FilterDriverCharacteristics->UniqueName, &Block->Characteristics.UniqueName, &Block->UniqueNameBuffer);
    if (Status == NDIS_STATUS_SUCCESS)
        Status = Ndis6CopyFilterName(&FilterDriverCharacteristics->ServiceName, &Block->Characteristics.ServiceName, &Block->ServiceNameBuffer);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        Ndis6FreeFilterDriverBlock(Block);
        return Status;
    }

    if (Block->Characteristics.SetOptionsHandler != NULL)
    {
        Status = Block->Characteristics.SetOptionsHandler((NDIS_HANDLE)Block, FilterDriverContext);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            Ndis6FreeFilterDriverBlock(Block);
            return Status;
        }
    }

    (VOID)KeWaitForSingleObject(&g_Ndis6FilterLifecycleMutex, Executive, KernelMode, FALSE, NULL);
    KeAcquireSpinLock(&g_Ndis6FilterDriverListLock, &OldIrql);
    InsertTailList(&g_Ndis6FilterDriverList, &Block->ListEntry);
    KeReleaseSpinLock(&g_Ndis6FilterDriverListLock, OldIrql);
    KeReleaseMutex(&g_Ndis6FilterLifecycleMutex, FALSE);

    Ndis6AttachFilterDriverToAllAdapters(Block);
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

    if (Block == NULL || Block->Signature != NDIS6_FILTER_DRIVER_SIGNATURE ||
        InterlockedCompareExchange(&Block->Closing, TRUE, FALSE) != FALSE)
        return;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);

    (VOID)KeWaitForSingleObject(&g_Ndis6FilterLifecycleMutex, Executive, KernelMode, FALSE, NULL);

    KeAcquireSpinLock(&g_Ndis6FilterDriverListLock, &OldIrql);
    if (Block->ListEntry.Flink != NULL &&
        Block->ListEntry.Blink != NULL &&
        Block->ListEntry.Flink != &Block->ListEntry)
    {
        RemoveEntryList(&Block->ListEntry);
        InitializeListHead(&Block->ListEntry);
    }
    KeReleaseSpinLock(&g_Ndis6FilterDriverListLock, OldIrql);

    ExWaitForRundownProtectionRelease(&Block->CallbackRundown);

    /* Windows does not release a filter-driver registration while modules
     * still point into its characteristics table. Quiesce each affected
     * stack, detach the module, and only then release the driver block. */
    for (;;)
    {
        PNDIS6_FILTER_MODULE Module = NULL;
        PLOGICAL_ADAPTER Adapter;
        PNDIS6_ADAPTER_EXT Ext;
        BOOLEAN WasRunning = FALSE;
        NDIS_STATUS Status;

        KeAcquireSpinLock(&Block->ModuleListLock, &OldIrql);
        if (!IsListEmpty(&Block->ModuleList))
        {
            Module = CONTAINING_RECORD(Block->ModuleList.Flink, NDIS6_FILTER_MODULE, DriverLink);
        }
        KeReleaseSpinLock(&Block->ModuleListLock, OldIrql);

        if (Module == NULL)
            break;

        Adapter = Module->Adapter;
        Ext = Adapter != NULL ? NDIS6_EXT(Adapter) : NULL;
        if (Ext != NULL && Ext->Initialized &&
            InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) == 0 &&
            InterlockedCompareExchange((volatile LONG *)&Ext->PauseState, NDIS6_PAUSE_STATE_RUNNING, NDIS6_PAUSE_STATE_RUNNING) ==
                NDIS6_PAUSE_STATE_RUNNING)
        {
            WasRunning = TRUE;
            Status = Ndis6PauseDriverStack(Adapter);
        }
        else if (Adapter != NULL)
        {
            Status = Ndis6PauseFilterModules(Adapter);
        }
        else
        {
            Status = NDIS_STATUS_SUCCESS;
        }

        if (Status != NDIS_STATUS_SUCCESS)
        {
            DbgPrint("NDIS6: filter stack pause failed during deregistration 0x%08lx\n", (ULONG)Status);
        }

        Ndis6DetachFilterModule(Module);

        if (WasRunning && Ext != NULL && Ext->Initialized &&
            InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) == 0)
        {
            Status = Ndis6RestartDriverStack(Adapter);
            if (Status != NDIS_STATUS_SUCCESS)
            {
                DbgPrint("NDIS6: filter stack restart failed during deregistration 0x%08lx\n", (ULONG)Status);
            }
        }
    }

    KeReleaseMutex(&g_Ndis6FilterLifecycleMutex, FALSE);

    Ndis6FreeFilterDriverBlock(Block);
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
 * ============================================================================ */

static BOOLEAN
Ndis6FilterModuleExists(
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block,
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    KeAcquireSpinLock(&Block->ModuleListLock, &OldIrql);
    for (Entry = Block->ModuleList.Flink;
         Entry != &Block->ModuleList;
         Entry = Entry->Flink)
    {
        PNDIS6_FILTER_MODULE Module =
            CONTAINING_RECORD(Entry, NDIS6_FILTER_MODULE, DriverLink);
        if (Module->Adapter == Adapter &&
            InterlockedCompareExchange(&Module->Closing, FALSE, FALSE) == FALSE)
        {
            Found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&Block->ModuleListLock, OldIrql);
    return Found;
}

static VOID
Ndis6BuildFilterAttachParameters(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block,
    _Out_ PNDIS_FILTER_ATTACH_PARAMETERS Params)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);

    RtlZeroMemory(Params, sizeof(*Params));
    Params->Header.Type = NDIS_OBJECT_TYPE_FILTER_ATTACH_PARAMETERS;
    if (Block->Characteristics.MinorNdisVersion >= 30)
    {
        Params->Header.Revision = NDIS_FILTER_ATTACH_PARAMETERS_REVISION_4;
        Params->Header.Size = NDIS_SIZEOF_FILTER_ATTACH_PARAMETERS_REVISION_4;
    }
    else if (Block->Characteristics.MinorNdisVersion >= 20)
    {
        Params->Header.Revision = NDIS_FILTER_ATTACH_PARAMETERS_REVISION_3;
        Params->Header.Size = NDIS_SIZEOF_FILTER_ATTACH_PARAMETERS_REVISION_3;
    }
    else if (Block->Characteristics.MinorNdisVersion >= 1)
    {
        Params->Header.Revision = NDIS_FILTER_ATTACH_PARAMETERS_REVISION_2;
        Params->Header.Size = NDIS_SIZEOF_FILTER_ATTACH_PARAMETERS_REVISION_2;
    }
    else
    {
        Params->Header.Revision = NDIS_FILTER_ATTACH_PARAMETERS_REVISION_1;
        Params->Header.Size = NDIS_SIZEOF_FILTER_ATTACH_PARAMETERS_REVISION_1;
    }

    Params->FilterModuleGuidName = &Block->Characteristics.UniqueName;
    Params->IfIndex = Ext->IfIndex;
    Params->NetLuid = Ext->NetLuid;
    Params->BaseMiniportIfIndex = Ext->IfIndex;
    Params->BaseMiniportInstanceName = &Adapter->NdisMiniportBlock.MiniportName;
    Params->BaseMiniportName = &Adapter->NdisMiniportBlock.MiniportName;
    Params->BaseMiniportNetLuid = Ext->NetLuid;
    Params->LowerIfIndex = Ext->IfIndex;
    Params->LowerIfNetLuid = Ext->NetLuid;
    Params->MiniportPhysicalDeviceObject = Ext->PhysicalDeviceObject;
    if (Ext->GeneralAttrsValid)
    {
        Params->MediaConnectState = Ext->GeneralAttrs.MediaConnectState;
        Params->MediaDuplexState =
            (NET_IF_MEDIA_DUPLEX_STATE)Ext->GeneralAttrs.MediaDuplexState;
        Params->XmitLinkSpeed = Ext->GeneralAttrs.XmitLinkSpeed;
        Params->RcvLinkSpeed = Ext->GeneralAttrs.RcvLinkSpeed;
        Params->MiniportMediaType = Ext->GeneralAttrs.MediaType;
        Params->MiniportPhysicalMediaType =
            Ext->GeneralAttrs.PhysicalMediumType;
        Params->DefaultOffloadConfiguration = Ext->OffloadDefaultPtr;
        Params->MacAddressLength = Ext->GeneralAttrs.MacAddressLength;
        if (Params->MacAddressLength <= sizeof(Params->CurrentMacAddress))
        {
            RtlCopyMemory(Params->CurrentMacAddress, Ext->GeneralAttrs.CurrentMacAddress, Params->MacAddressLength);
        }
        Params->BaseMiniportIfConnectorPresent =
            Ext->GeneralAttrs.IfConnectorPresent;
    }
}

/* g_Ndis6FilterLifecycleMutex serializes the driver's registration block
 * with deregistration while the callback builds and publishes this module. */
static NDIS_STATUS
Ndis6AttachFilterModuleLocked(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);
    NDIS_FILTER_ATTACH_PARAMETERS Params;
    PNDIS6_FILTER_MODULE Module;
    NDIS_STATUS Status;
    BOOLEAN AttachSucceeded = FALSE;
    KIRQL OldIrql;

    if (Ext == NULL || Block == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (Block->Characteristics.AttachHandler == NULL ||
        Ndis6FilterModuleExists(Block, Adapter))
    {
        return NDIS_STATUS_SUCCESS;
    }

    /* A closing block is being removed from the global list. Treat it as
     * absent instead of turning an unrelated mandatory attach into failure. */
    if (!Ndis6ReferenceFilterDriver(Block))
        return NDIS_STATUS_SUCCESS;

    Module = (PNDIS6_FILTER_MODULE)ExAllocatePoolWithTag(NonPagedPool, sizeof(NDIS6_FILTER_MODULE), NDIS6_FILTER_MODULE_TAG);
    if (Module == NULL)
    {
        Ndis6DereferenceFilterDriver(Block);
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(Module, sizeof(*Module));
    InitializeListHead(&Module->ListEntry);
    InitializeListHead(&Module->DriverLink);
    Module->DriverBlock = Block;
    Module->Adapter = Adapter;
    Module->Signature = NDIS6_FILTER_MODULE_SIGNATURE;
    Module->State = NDIS6_FILTER_STATE_PAUSED;
    ExInitializeRundownProtection(&Module->RundownRef);
    KeInitializeEvent(&Module->PauseEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Module->RestartEvent, NotificationEvent, FALSE);

    Ndis6BuildFilterAttachParameters(Adapter, Block, &Params);
    Status = Block->Characteristics.AttachHandler((NDIS_HANDLE)Module, Block->FilterDriverContext, &Params);

    if (Status == NDIS_STATUS_SUCCESS)
    {
        AttachSucceeded = TRUE;
        if (!Module->SetAttributesCalled)
        {
            Status = NDIS_STATUS_BAD_CHARACTERISTICS;
        }
        else if (Block->Characteristics.SetFilterModuleOptionsHandler != NULL)
        {
            Status = Block->Characteristics.SetFilterModuleOptionsHandler(Module->FilterModuleContext);
        }

        if (Status == NDIS_STATUS_SUCCESS &&
            InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) != 0)
        {
            Status = NDIS_STATUS_CLOSING;
        }

        if (Status == NDIS_STATUS_SUCCESS)
        {
            KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
            InsertTailList(&Ext->FilterModuleList, &Module->ListEntry);
            KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);

            KeAcquireSpinLock(&Block->ModuleListLock, &OldIrql);
            InsertTailList(&Block->ModuleList, &Module->DriverLink);
            KeReleaseSpinLock(&Block->ModuleListLock, OldIrql);
        }
    }

    if (Status != NDIS_STATUS_SUCCESS)
    {
        if (AttachSucceeded && Block->Characteristics.DetachHandler != NULL)
            Block->Characteristics.DetachHandler(Module->FilterModuleContext);
        Module->Signature = 0;
        ExFreePoolWithTag(Module, NDIS6_FILTER_MODULE_TAG);
    }

    Ndis6DereferenceFilterDriver(Block);
    return Status;
}

NDIS_STATUS
Ndis6AttachFiltersToAdapter(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    PLIST_ENTRY        Entry;
    NDIS_STATUS        FailureStatus = NDIS_STATUS_SUCCESS;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (InterlockedCompareExchange(&g_Ndis6FilterDriverListState, 2, 2) != 2)
        return NDIS_STATUS_SUCCESS;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    (VOID)KeWaitForSingleObject(&g_Ndis6FilterLifecycleMutex, Executive, KernelMode, FALSE, NULL);

    if (!Adapter->IsNdis6 || NDIS6_EXT(Adapter) != Ext ||
        InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) != 0)
    {
        KeReleaseMutex(&g_Ndis6FilterLifecycleMutex, FALSE);
        return NDIS_STATUS_CLOSING;
    }

    /* The lifecycle mutex pins every driver block while callbacks run, so
     * this walk has no fixed snapshot limit and cannot observe a freed block. */
    for (Entry = g_Ndis6FilterDriverList.Flink;
         Entry != &g_Ndis6FilterDriverList;
         Entry = Entry->Flink)
    {
        PNDIS6_FILTER_DRIVER_BLOCK Block =
            CONTAINING_RECORD(Entry, NDIS6_FILTER_DRIVER_BLOCK, ListEntry);
        NDIS_STATUS Status = Ndis6AttachFilterModuleLocked(Adapter, Block);

        if (Status != NDIS_STATUS_SUCCESS &&
            (Block->Characteristics.Flags & NDIS_FILTER_DRIVER_MANDATORY))
        {
            FailureStatus = Status;
            break;
        }
    }

    KeReleaseMutex(&g_Ndis6FilterLifecycleMutex, FALSE);

    if (FailureStatus != NDIS_STATUS_SUCCESS)
        Ndis6DetachFiltersFromAdapter(Adapter);
    return FailureStatus;
}

/* Registering a filter can modify stacks that are already running. Windows
 * pauses each such stack, inserts the new module in the Paused state, and
 * restarts bottom-up. Adapter rundown keeps the snapshot valid after the
 * global adapter spin lock is dropped for driver callbacks. */
static VOID
Ndis6AttachFilterDriverToAllAdapters(
    _In_ PNDIS6_FILTER_DRIVER_BLOCK Block)
{
    extern LIST_ENTRY AdapterListHead;
    extern KSPIN_LOCK AdapterListLock;
    PLOGICAL_ADAPTER *Snapshot = NULL;
    SIZE_T SnapshotCapacity = 0;
    SIZE_T SnapCount = 0;
    SIZE_T Index;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    KeAcquireSpinLock(&AdapterListLock, &OldIrql);
    for (Entry = AdapterListHead.Flink;
         Entry != &AdapterListHead;
         Entry = Entry->Flink)
    {
        PLOGICAL_ADAPTER Adapter =
            CONTAINING_RECORD(Entry, LOGICAL_ADAPTER, ListEntry);
        if (Adapter->IsNdis6 && NDIS6_EXT(Adapter) != NULL)
            SnapshotCapacity++;
    }
    KeReleaseSpinLock(&AdapterListLock, OldIrql);

    if (SnapshotCapacity == 0)
        return;

    if (SnapshotCapacity > MAXULONG_PTR / sizeof(*Snapshot))
    {
        DbgPrint("NDIS6: adapter snapshot size overflow during filter registration\n");
        return;
    }

    Snapshot = ExAllocatePoolWithTag(NonPagedPool, SnapshotCapacity * sizeof(*Snapshot), NDIS6_FILTER_SNAPSHOT_TAG);
    if (Snapshot == NULL)
    {
        DbgPrint("NDIS6: unable to allocate adapter snapshot during filter registration\n");
        return;
    }

    KeAcquireSpinLock(&AdapterListLock, &OldIrql);
    for (Entry = AdapterListHead.Flink;
         Entry != &AdapterListHead && SnapCount < SnapshotCapacity;
         Entry = Entry->Flink)
    {
        PLOGICAL_ADAPTER Adapter =
            CONTAINING_RECORD(Entry, LOGICAL_ADAPTER, ListEntry);
        if (Adapter->IsNdis6 && NDIS6_EXT(Adapter) != NULL &&
            Ndis6ReferenceAdapterLifecycle(Adapter))
        {
            Snapshot[SnapCount++] = Adapter;
        }
    }
    KeReleaseSpinLock(&AdapterListLock, OldIrql);

    for (Index = 0; Index < SnapCount; Index++)
    {
        PLOGICAL_ADAPTER Adapter = Snapshot[Index];
        PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);
        NDIS_STATUS Status = NDIS_STATUS_CLOSING;
        NDIS_STATUS RestartStatus;
        BOOLEAN WasRunning = FALSE;

        (VOID)KeWaitForSingleObject(&g_Ndis6FilterLifecycleMutex, Executive, KernelMode, FALSE, NULL);

        if (Adapter->IsNdis6 && Ext != NULL && Ext->Initialized &&
            Block->Signature == NDIS6_FILTER_DRIVER_SIGNATURE &&
            InterlockedCompareExchange(&Block->Closing, 0, 0) == 0 &&
            InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) == 0)
        {
            (VOID)KeWaitForSingleObject(&Ext->StackTransitionMutex, Executive, KernelMode, FALSE, NULL);

            if (Adapter->IsNdis6 && NDIS6_EXT(Adapter) == Ext &&
                Ext->Initialized &&
                InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) == 0)
            {
                WasRunning = InterlockedCompareExchange((volatile LONG *)&Ext->PauseState, NDIS6_PAUSE_STATE_RUNNING, NDIS6_PAUSE_STATE_RUNNING) ==
                             NDIS6_PAUSE_STATE_RUNNING;

                if (WasRunning)
                    Status = Ndis6PauseDriverStackLocked(Adapter);
                else if (InterlockedCompareExchange((volatile LONG *)&Ext->PauseState, NDIS6_PAUSE_STATE_PAUSED, NDIS6_PAUSE_STATE_PAUSED) ==
                         NDIS6_PAUSE_STATE_PAUSED)
                    Status = NDIS_STATUS_SUCCESS;
                else
                    Status = NDIS_STATUS_INVALID_STATE;

                if (Status == NDIS_STATUS_SUCCESS &&
                    InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) == 0)
                {
                    Status = Ndis6AttachFilterModuleLocked(Adapter, Block);
                }

                if (WasRunning &&
                    InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) == 0)
                {
                    RestartStatus = Ndis6RestartDriverStackLocked(Adapter);
                    if (Status == NDIS_STATUS_SUCCESS)
                        Status = RestartStatus;
                }
            }

            KeReleaseMutex(&Ext->StackTransitionMutex, FALSE);
        }

        KeReleaseMutex(&g_Ndis6FilterLifecycleMutex, FALSE);

        if (Status != NDIS_STATUS_SUCCESS && Status != NDIS_STATUS_CLOSING)
        {
            DbgPrint("NDIS6: late filter attach failed 0x%08lx\n", (ULONG)Status);
        }

        Ndis6DereferenceAdapterLifecycle(Adapter);
    }

    ExFreePoolWithTag(Snapshot, NDIS6_FILTER_SNAPSHOT_TAG);
}

static VOID
Ndis6DetachFilterModule(
    _In_ PNDIS6_FILTER_MODULE Module)
{
    PNDIS6_FILTER_DRIVER_BLOCK Block;
    PLOGICAL_ADAPTER Adapter;
    PNDIS6_ADAPTER_EXT Ext;
    KIRQL OldIrql;

    if (Module == NULL || Module->Signature != NDIS6_FILTER_MODULE_SIGNATURE ||
        InterlockedCompareExchange(&Module->Closing, TRUE, FALSE) != FALSE)
    {
        return;
    }

    /* Mark closing before draining. Chain selection obtains rundown while
     * holding the adapter-list lock, so no new callback can enter after this
     * point. Keep the list links intact until existing callbacks have left. */
    ExWaitForRundownProtectionRelease(&Module->RundownRef);

    Adapter = Module->Adapter;
    Block = Module->DriverBlock;
    Ext = Adapter != NULL ? NDIS6_EXT(Adapter) : NULL;
    if (Ext != NULL)
    {
        KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
        if (Module->ListEntry.Flink != NULL &&
            Module->ListEntry.Blink != NULL &&
            Module->ListEntry.Flink != &Module->ListEntry)
        {
            RemoveEntryList(&Module->ListEntry);
            InitializeListHead(&Module->ListEntry);
        }
        KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);
    }

    if (Block != NULL)
    {
        KeAcquireSpinLock(&Block->ModuleListLock, &OldIrql);
        if (Module->DriverLink.Flink != NULL &&
            Module->DriverLink.Blink != NULL &&
            Module->DriverLink.Flink != &Module->DriverLink)
        {
            RemoveEntryList(&Module->DriverLink);
            InitializeListHead(&Module->DriverLink);
        }
        KeReleaseSpinLock(&Block->ModuleListLock, OldIrql);

        if (Block->Characteristics.DetachHandler != NULL)
            Block->Characteristics.DetachHandler(Module->FilterModuleContext);
    }

    Module->Adapter = NULL;
    Module->DriverBlock = NULL;
    Module->Signature = 0;
    ExFreePoolWithTag(Module, NDIS6_FILTER_MODULE_TAG);
}

VOID
Ndis6DetachFiltersFromAdapter(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    KIRQL              OldIrql;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (InterlockedCompareExchange(&g_Ndis6FilterDriverListState, 2, 2) != 2)
        return;

    (VOID)KeWaitForSingleObject(&g_Ndis6FilterLifecycleMutex, Executive, KernelMode, FALSE, NULL);
    (VOID)Ndis6PauseFilterModules(Adapter);

    for (;;)
    {
        PNDIS6_FILTER_MODULE Module = NULL;

        KeAcquireSpinLock(&Ext->FilterModuleListLock, &OldIrql);
        if (!IsListEmpty(&Ext->FilterModuleList))
        {
            Module = CONTAINING_RECORD(Ext->FilterModuleList.Flink, NDIS6_FILTER_MODULE, ListEntry);
        }
        KeReleaseSpinLock(&Ext->FilterModuleListLock, OldIrql);

        if (Module == NULL)
            break;
        Ndis6DetachFilterModule(Module);
    }

    KeReleaseMutex(&g_Ndis6FilterLifecycleMutex, FALSE);
}

/* ------------------------------------------------------------------ */
/*  Protocol driver registration and per-adapter binding lifecycle.   */
/* ------------------------------------------------------------------ */

LIST_ENTRY  g_Ndis6ProtocolDriverList;
KSPIN_LOCK  g_Ndis6ProtocolDriverListLock;
static KMUTEX g_Ndis6ProtocolLifecycleMutex;
static volatile LONG g_Ndis6ProtocolDriverListState;

#define NDIS6_PROTOCOL_DRIVER_TAG  'pDNn'  /* "nNDp" */
#define NDIS6_PROTOCOL_SNAPSHOT_TAG 'sPNn'

static VOID
Ndis6ProtocolDriverListInit(VOID)
{
    if (InterlockedCompareExchange(&g_Ndis6ProtocolDriverListState, 1, 0) == 0)
    {
        InitializeListHead(&g_Ndis6ProtocolDriverList);
        KeInitializeSpinLock(&g_Ndis6ProtocolDriverListLock);
        KeInitializeMutex(&g_Ndis6ProtocolLifecycleMutex, 0);
        InterlockedExchange(&g_Ndis6ProtocolDriverListState, 2);
        return;
    }

    while (InterlockedCompareExchange(&g_Ndis6ProtocolDriverListState, 2, 2) != 2)
        YieldProcessor();
}

static BOOLEAN
Ndis6ReferenceProtocolDriver(
    _In_ PNDIS6_PROTOCOL_DRIVER_BLOCK Block)
{
    if (Block == NULL || Block->Signature != NDIS6_PROTOCOL_DRIVER_SIGNATURE ||
        InterlockedCompareExchange(&Block->Closing, 0, 0) != 0)
        return FALSE;

    if (!ExAcquireRundownProtection(&Block->CallbackRundown))
        return FALSE;

    if (Block->Signature != NDIS6_PROTOCOL_DRIVER_SIGNATURE ||
        InterlockedCompareExchange(&Block->Closing, 0, 0) != 0)
    {
        ExReleaseRundownProtection(&Block->CallbackRundown);
        return FALSE;
    }

    return TRUE;
}

static VOID
Ndis6DereferenceProtocolDriver(
    _In_ PNDIS6_PROTOCOL_DRIVER_BLOCK Block)
{
    ExReleaseRundownProtection(&Block->CallbackRundown);
}

static BOOLEAN
Ndis6IsSupportedProtocolMinorVersion(
    _In_ UCHAR MinorVersion)
{
    switch (MinorVersion)
    {
        case 0:
        case 1:
        case 20:
        case 30:
        case 40:
        case 50:
        case 51:
        case 60:
        case 70:
        case 80:
        case 81:
        case 82:
        case 83:
        case 84:
        case 85:
        case 86:
        case 87:
        case 88:
        case 89:
            return TRUE;

        default:
            return FALSE;
    }
}

static NDIS_STATUS
Ndis6ValidateProtocolCharacteristics(
    _In_ PNDIS_PROTOCOL_DRIVER_CHARACTERISTICS Characteristics,
    _Out_ PULONG CopySize)
{
    ULONG AllowedFlags = 0;
    ULONG RequiredSize;
    UCHAR RequiredRevision;

    if (Characteristics->Header.Type !=
        NDIS_OBJECT_TYPE_PROTOCOL_DRIVER_CHARACTERISTICS)
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    if (Characteristics->MajorNdisVersion != 6 ||
        !Ndis6IsSupportedProtocolMinorVersion(Characteristics->MinorNdisVersion))
    {
        return NDIS_STATUS_BAD_VERSION;
    }

    RequiredRevision = (Characteristics->MinorNdisVersion >= 1)
        ? NDIS_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_2
        : NDIS_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_1;
    RequiredSize = RequiredRevision == NDIS_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_2
        ? NDIS_SIZEOF_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_2
        : NDIS_SIZEOF_PROTOCOL_DRIVER_CHARACTERISTICS_REVISION_1;
    if (Characteristics->Header.Revision != RequiredRevision)
        return NDIS_STATUS_BAD_VERSION;

    if (Characteristics->Header.Size != RequiredSize ||
        RequiredSize > sizeof(*Characteristics) ||
        Characteristics->BindAdapterHandlerEx == NULL ||
        Characteristics->UnbindAdapterHandlerEx == NULL)
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    if (Characteristics->MinorNdisVersion >= 50)
    {
        AllowedFlags |= NDIS_PROTOCOL_DRIVER_SUPPORTS_CURRENT_MAC_ADDRESS_CHANGE |
                        NDIS_PROTOCOL_DRIVER_SUPPORTS_L2_MTU_SIZE_CHANGE;
    }
    if (Characteristics->MinorNdisVersion >= 89)
        AllowedFlags |= NDIS_PROTOCOL_DRIVER_UDP_RSC_NOT_SUPPORTED;
    if ((Characteristics->Flags & ~AllowedFlags) != 0)
        return NDIS_STATUS_BAD_CHARACTERISTICS;

    if ((Characteristics->Name.Length != 0 && Characteristics->Name.Buffer == NULL) ||
        Characteristics->Name.Length > Characteristics->Name.MaximumLength ||
        Characteristics->Name.Length > MAXUSHORT - sizeof(WCHAR) ||
        (Characteristics->Name.Length & (sizeof(WCHAR) - 1)) != 0)
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    *CopySize = RequiredSize;
    return NDIS_STATUS_SUCCESS;
}

/* ============================================================================
 *  Build an NDIS_BIND_PARAMETERS from an adapter's cached general attrs.
 *  Used by the protocol bind fan-out below.
 * ============================================================================ */

static VOID
Ndis6BuildBindParameters(
    _In_  PLOGICAL_ADAPTER       Adapter,
    _In_  PNDIS6_PROTOCOL_DRIVER_BLOCK Block,
    _Out_ PNDIS_BIND_PARAMETERS  Params)
{
    PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Adapter);

    RtlZeroMemory(Params, sizeof(*Params));
    Params->Header.Type = NDIS_OBJECT_TYPE_BIND_PARAMETERS;
    if (Block->Characteristics.MinorNdisVersion >= 30)
    {
        Params->Header.Revision = NDIS_BIND_PARAMETERS_REVISION_4;
        Params->Header.Size = NDIS_SIZEOF_BIND_PARAMETERS_REVISION_4;
    }
    else if (Block->Characteristics.MinorNdisVersion >= 20)
    {
        Params->Header.Revision = NDIS_BIND_PARAMETERS_REVISION_3;
        Params->Header.Size = NDIS_SIZEOF_BIND_PARAMETERS_REVISION_3;
    }
    else if (Block->Characteristics.MinorNdisVersion >= 1)
    {
        Params->Header.Revision = NDIS_BIND_PARAMETERS_REVISION_2;
        Params->Header.Size = NDIS_SIZEOF_BIND_PARAMETERS_REVISION_2;
    }
    else
    {
        Params->Header.Revision = NDIS_BIND_PARAMETERS_REVISION_1;
        Params->Header.Size = NDIS_SIZEOF_BIND_PARAMETERS_REVISION_1;
    }
    Params->AdapterName     = &Adapter->NdisMiniportBlock.MiniportName;
    Params->BoundAdapterName = &Adapter->NdisMiniportBlock.MiniportName;

    if (Ext != NULL)
    {
        Params->PhysicalDeviceObject = Ext->PhysicalDeviceObject;
        Params->BoundIfNetluid = Ext->NetLuid;
        Params->BoundIfIndex = Ext->IfIndex;
        Params->LowestIfNetluid = Ext->NetLuid;
        Params->LowestIfIndex = Ext->IfIndex;
        if (Ext->GeneralAttrsValid)
        {
            Params->MediaType            = Ext->GeneralAttrs.MediaType;
            Params->PhysicalMediumType   = Ext->GeneralAttrs.PhysicalMediumType;
            Params->MtuSize              = Ext->GeneralAttrs.MtuSize;
            Params->MaxXmitLinkSpeed     = Ext->GeneralAttrs.MaxXmitLinkSpeed;
            Params->XmitLinkSpeed        = Ext->GeneralAttrs.XmitLinkSpeed;
            Params->MaxRcvLinkSpeed      = Ext->GeneralAttrs.MaxRcvLinkSpeed;
            Params->RcvLinkSpeed         = Ext->GeneralAttrs.RcvLinkSpeed;
            Params->MediaConnectState    = Ext->GeneralAttrs.MediaConnectState;
            Params->MediaDuplexState     = Ext->GeneralAttrs.MediaDuplexState;
            Params->LookaheadSize        = Ext->GeneralAttrs.LookaheadSize;
            Params->PowerManagementCapabilities =
                Ext->GeneralAttrs.PowerManagementCapabilities;
            Params->MacOptions           = Ext->GeneralAttrs.MacOptions;
            Params->SupportedPacketFilters = Ext->GeneralAttrs.SupportedPacketFilters;
            Params->MaxMulticastListSize = Ext->GeneralAttrs.MaxMulticastListSize;
            Params->MacAddressLength     = Ext->GeneralAttrs.MacAddressLength;
            if (Ext->GeneralAttrs.MacAddressLength <= sizeof(Params->CurrentMacAddress))
            {
                RtlCopyMemory(Params->CurrentMacAddress, Ext->GeneralAttrs.CurrentMacAddress, Ext->GeneralAttrs.MacAddressLength);
            }
            Params->RcvScaleCapabilities =
                (PNDIS_RECEIVE_SCALE_CAPABILITIES)Ext->GeneralAttrs.RecvScaleCapabilities;
            Params->AccessType = Ext->GeneralAttrs.AccessType;
            Params->DirectionType = Ext->GeneralAttrs.DirectionType;
            Params->ConnectionType = Ext->GeneralAttrs.ConnectionType;
            Params->IfType = Ext->GeneralAttrs.IfType;
            Params->IfConnectorPresent = Ext->GeneralAttrs.IfConnectorPresent;
            Params->DataBackFillSize = Ext->GeneralAttrs.DataBackFillSize;
            Params->ContextBackFillSize = Ext->GeneralAttrs.ContextBackFillSize;
            Params->DefaultOffloadConfiguration = Ext->OffloadDefaultPtr;
            Params->PowerManagementCapabilitiesEx =
                Ext->GeneralAttrs.PowerManagementCapabilitiesEx;
        }
    }
}

typedef struct _NDIS6_BIND_OPERATION
{
    ULONG Signature;
    PLOGICAL_ADAPTER Adapter;
    KEVENT CompleteEvent;
    volatile LONG Completed;
    NDIS_STATUS CompletionStatus;
} NDIS6_BIND_OPERATION, *PNDIS6_BIND_OPERATION;

#define NDIS6_BIND_OPERATION_SIGNATURE 'bBNn'

VOID
EXPORT
NdisCompleteBindAdapterEx(
    _In_ NDIS_HANDLE BindAdapterContext,
    _In_ NDIS_STATUS Status)
{
    PNDIS6_BIND_OPERATION Operation = BindAdapterContext;

    if (Operation == NULL ||
        Operation->Signature != NDIS6_BIND_OPERATION_SIGNATURE)
    {
        return;
    }

    /* Claim completion before publishing the status. A second completion is
     * a protocol-driver contract violation and must not overwrite the first. */
    if (InterlockedCompareExchange(&Operation->Completed, -1, 0) == 0)
    {
        Operation->CompletionStatus = Status;
        InterlockedExchange(&Operation->Completed, 1);
        KeSetEvent(&Operation->CompleteEvent, IO_NO_INCREMENT, FALSE);
    }
}

static NDIS_STATUS
Ndis6InvokeProtocolBind(
    _In_ PNDIS6_PROTOCOL_DRIVER_BLOCK Block,
    _In_ PLOGICAL_ADAPTER Adapter)
{
    NDIS6_BIND_OPERATION Operation;
    NDIS_BIND_PARAMETERS Params;
    NDIS_STATUS Status;
    NTSTATUS WaitStatus;

    if (Block == NULL || Adapter == NULL ||
        Block->Characteristics.BindAdapterHandlerEx == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&Operation, sizeof(Operation));
    Operation.Signature = NDIS6_BIND_OPERATION_SIGNATURE;
    Operation.Adapter = Adapter;
    Operation.CompletionStatus = NDIS_STATUS_PENDING;
    KeInitializeEvent(&Operation.CompleteEvent, NotificationEvent, FALSE);

    Ndis6BuildBindParameters(Adapter, Block, &Params);
    Status = Block->Characteristics.BindAdapterHandlerEx(Block->ProtocolDriverContext, (NDIS_HANDLE)&Operation, &Params);

    if (Status == NDIS_STATUS_PENDING)
    {
        WaitStatus = KeWaitForSingleObject(&Operation.CompleteEvent, Executive, KernelMode, FALSE, NULL);
        Status = NT_SUCCESS(WaitStatus)
            ? Operation.CompletionStatus
            : (NDIS_STATUS)WaitStatus;
    }

    Operation.Signature = 0;
    return Status;
}

/* ============================================================================
 *  Ndis6BindProtocolToAllAdaptersLocked — when a protocol registers, walk the
 *  global LOGICAL_ADAPTER list and call its BindAdapterHandlerEx for every
 *  NDIS 6 adapter.
 * ============================================================================ */

static VOID
Ndis6BindProtocolToAllAdaptersLocked(
    _In_ PNDIS6_PROTOCOL_DRIVER_BLOCK Block)
{
    extern LIST_ENTRY AdapterListHead;
    extern KSPIN_LOCK AdapterListLock;
    PLIST_ENTRY entry;
    KIRQL OldIrql;
    PLOGICAL_ADAPTER *Snapshot = NULL;
    SIZE_T SnapshotCapacity = 0;
    SIZE_T SnapCount = 0;
    SIZE_T i;

    if (Block->Characteristics.BindAdapterHandlerEx == NULL)
        return;

    if (!Ndis6ReferenceProtocolDriver(Block))
        return;

    /* Count every NDIS 6 adapter in one list generation. Adapter teardown
     * waits on the protocol lifecycle mutex before it can free the object,
     * and adapters inserted after this count bind the protocol from their own
     * start path. */
    KeAcquireSpinLock(&AdapterListLock, &OldIrql);
    for (entry = AdapterListHead.Flink;
         entry != &AdapterListHead;
         entry = entry->Flink)
    {
        PLOGICAL_ADAPTER Adapter =
            CONTAINING_RECORD(entry, LOGICAL_ADAPTER, ListEntry);
        if (Adapter->IsNdis6 && NDIS6_EXT(Adapter) != NULL)
            SnapshotCapacity++;
    }
    KeReleaseSpinLock(&AdapterListLock, OldIrql);

    if (SnapshotCapacity == 0)
        goto Exit;

    if (SnapshotCapacity > MAXULONG_PTR / sizeof(*Snapshot))
    {
        DbgPrint("NDIS6: adapter snapshot size overflow during protocol bind\n");
        goto Exit;
    }

    Snapshot = ExAllocatePoolWithTag(NonPagedPool, SnapshotCapacity * sizeof(*Snapshot), NDIS6_PROTOCOL_SNAPSHOT_TAG);
    if (Snapshot == NULL)
    {
        DbgPrint("NDIS6: unable to allocate adapter snapshot during protocol bind\n");
        goto Exit;
    }

    KeAcquireSpinLock(&AdapterListLock, &OldIrql);
    for (entry = AdapterListHead.Flink;
         entry != &AdapterListHead && SnapCount < SnapshotCapacity;
         entry = entry->Flink)
    {
        PLOGICAL_ADAPTER Adapter =
            CONTAINING_RECORD(entry, LOGICAL_ADAPTER, ListEntry);
        if (Adapter->IsNdis6 && NDIS6_EXT(Adapter) != NULL &&
            Ndis6ReferenceAdapterLifecycle(Adapter))
        {
            Snapshot[SnapCount++] = Adapter;
        }
    }
    KeReleaseSpinLock(&AdapterListLock, OldIrql);

    for (i = 0; i < SnapCount; i++)
    {
        PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Snapshot[i]);
        if (Snapshot[i]->IsNdis6 && Ext != NULL &&
            InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) == 0)
        {
            (VOID)Ndis6InvokeProtocolBind(Block, Snapshot[i]);
            if (InterlockedCompareExchange((volatile LONG *)&Ext->PauseState, NDIS6_PAUSE_STATE_RUNNING, NDIS6_PAUSE_STATE_RUNNING) ==
                NDIS6_PAUSE_STATE_RUNNING)
            {
                Ndis6RestartProtocolBindings(Snapshot[i]);
            }
        }
        Ndis6DereferenceAdapterLifecycle(Snapshot[i]);
    }

    ExFreePoolWithTag(Snapshot, NDIS6_PROTOCOL_SNAPSHOT_TAG);

Exit:
    Ndis6DereferenceProtocolDriver(Block);
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
    PNDIS6_PROTOCOL_DRIVER_BLOCK *Snapshot = NULL;
    SIZE_T SnapshotCapacity = 0;
    SIZE_T SnapCount = 0;
    SIZE_T i;

    if (InterlockedCompareExchange(&g_Ndis6ProtocolDriverListState, 2, 2) != 2 ||
        Adapter == NULL || !Adapter->IsNdis6)
        return;

    (VOID)KeWaitForSingleObject(&g_Ndis6ProtocolLifecycleMutex, Executive, KernelMode, FALSE, NULL);

    if (!Adapter->IsNdis6 || NDIS6_EXT(Adapter) == NULL ||
        InterlockedCompareExchange(&NDIS6_EXT(Adapter)->ProtocolBindingsClosing, 0, 0) != 0)
    {
        KeReleaseMutex(&g_Ndis6ProtocolLifecycleMutex, FALSE);
        return;
    }

    KeAcquireSpinLock(&g_Ndis6ProtocolDriverListLock, &OldIrql);
    for (entry = g_Ndis6ProtocolDriverList.Flink;
         entry != &g_Ndis6ProtocolDriverList;
         entry = entry->Flink)
    {
        SnapshotCapacity++;
    }
    KeReleaseSpinLock(&g_Ndis6ProtocolDriverListLock, OldIrql);

    if (SnapshotCapacity == 0)
        goto Exit;

    if (SnapshotCapacity > MAXULONG_PTR / sizeof(*Snapshot))
    {
        DbgPrint("NDIS6: protocol snapshot size overflow during adapter bind\n");
        goto Exit;
    }

    Snapshot = ExAllocatePoolWithTag(NonPagedPool, SnapshotCapacity * sizeof(*Snapshot), NDIS6_PROTOCOL_SNAPSHOT_TAG);
    if (Snapshot == NULL)
    {
        DbgPrint("NDIS6: unable to allocate protocol snapshot during adapter bind\n");
        goto Exit;
    }

    KeAcquireSpinLock(&g_Ndis6ProtocolDriverListLock, &OldIrql);
    for (entry = g_Ndis6ProtocolDriverList.Flink;
         entry != &g_Ndis6ProtocolDriverList && SnapCount < SnapshotCapacity;
         entry = entry->Flink)
    {
        PNDIS6_PROTOCOL_DRIVER_BLOCK Block =
            CONTAINING_RECORD(entry, NDIS6_PROTOCOL_DRIVER_BLOCK, ListEntry);
        if (Block->Characteristics.BindAdapterHandlerEx != NULL &&
            Ndis6ReferenceProtocolDriver(Block))
        {
            Snapshot[SnapCount] = Block;
        }
        else
        {
            Snapshot[SnapCount] = NULL;
        }
        SnapCount++;
    }
    KeReleaseSpinLock(&g_Ndis6ProtocolDriverListLock, OldIrql);

    for (i = 0; i < SnapCount; i++)
    {
        if (Snapshot[i] == NULL)
            continue;
        (VOID)Ndis6InvokeProtocolBind(Snapshot[i], Adapter);
        Ndis6DereferenceProtocolDriver(Snapshot[i]);
    }

    ExFreePoolWithTag(Snapshot, NDIS6_PROTOCOL_SNAPSHOT_TAG);

Exit:
    KeReleaseMutex(&g_Ndis6ProtocolLifecycleMutex, FALSE);
}

NDIS_STATUS
EXPORT
NdisRegisterProtocolDriver(
    _In_ NDIS_HANDLE ProtocolDriverContext,
    _In_ PNDIS_PROTOCOL_DRIVER_CHARACTERISTICS ProtocolDriverCharacteristics,
    _Out_ PNDIS_HANDLE NdisProtocolHandle)
{
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block;
    NDIS_STATUS Status;
    ULONG CopySize;
    ULONG NameBytes;
    KIRQL OldIrql;

    if (ProtocolDriverCharacteristics == NULL || NdisProtocolHandle == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    *NdisProtocolHandle = NULL;

    Status = Ndis6ValidateProtocolCharacteristics(ProtocolDriverCharacteristics, &CopySize);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    Ndis6ProtocolDriverListInit();

    Block = (PNDIS6_PROTOCOL_DRIVER_BLOCK)ExAllocatePoolWithTag(NonPagedPool, sizeof(NDIS6_PROTOCOL_DRIVER_BLOCK), NDIS6_PROTOCOL_DRIVER_TAG);
    if (Block == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Block, sizeof(*Block));
    Block->Signature = NDIS6_PROTOCOL_DRIVER_SIGNATURE;
    InitializeListHead(&Block->BindingList);
    KeInitializeSpinLock(&Block->BindingListLock);
    ExInitializeRundownProtection(&Block->CallbackRundown);
    Block->ProtocolDriverContext = ProtocolDriverContext;
    RtlCopyMemory(&Block->Characteristics, ProtocolDriverCharacteristics, CopySize);

    NameBytes = ProtocolDriverCharacteristics->Name.Length;
    if (NameBytes != 0)
    {
        Block->NameBuffer = ExAllocatePoolWithTag(PagedPool, NameBytes + sizeof(WCHAR), NDIS6_PROTOCOL_DRIVER_TAG);
        if (Block->NameBuffer == NULL)
        {
            ExFreePoolWithTag(Block, NDIS6_PROTOCOL_DRIVER_TAG);
            return NDIS_STATUS_RESOURCES;
        }

        RtlCopyMemory(Block->NameBuffer, ProtocolDriverCharacteristics->Name.Buffer, NameBytes);
        Block->NameBuffer[NameBytes / sizeof(WCHAR)] = UNICODE_NULL;
        Block->Characteristics.Name.Buffer = Block->NameBuffer;
        Block->Characteristics.Name.Length = (USHORT)NameBytes;
        Block->Characteristics.Name.MaximumLength =
            (USHORT)(NameBytes + sizeof(WCHAR));
    }

    if (Block->Characteristics.SetOptionsHandler != NULL)
    {
        Status = Block->Characteristics.SetOptionsHandler((NDIS_HANDLE)Block, ProtocolDriverContext);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            Block->Signature = 0;
            if (Block->NameBuffer != NULL)
                ExFreePoolWithTag(Block->NameBuffer, NDIS6_PROTOCOL_DRIVER_TAG);
            ExFreePoolWithTag(Block, NDIS6_PROTOCOL_DRIVER_TAG);
            return Status;
        }
    }

    (VOID)KeWaitForSingleObject(&g_Ndis6ProtocolLifecycleMutex, Executive, KernelMode, FALSE, NULL);
    KeAcquireSpinLock(&g_Ndis6ProtocolDriverListLock, &OldIrql);
    InsertTailList(&g_Ndis6ProtocolDriverList, &Block->ListEntry);
    KeReleaseSpinLock(&g_Ndis6ProtocolDriverListLock, OldIrql);

    *NdisProtocolHandle = (NDIS_HANDLE)Block;

    /* Publish and perform the initial adapter walk as one lifecycle operation.
     * Otherwise adapter startup can observe the newly published block and bind
     * it before this walk, causing the same protocol to bind twice. */
    Ndis6BindProtocolToAllAdaptersLocked(Block);
    KeReleaseMutex(&g_Ndis6ProtocolLifecycleMutex, FALSE);

    return NDIS_STATUS_SUCCESS;
}

typedef struct _NDIS6_UNBIND_OPERATION
{
    ULONG Signature;
    KEVENT CompleteEvent;
    volatile LONG Completed;
} NDIS6_UNBIND_OPERATION, *PNDIS6_UNBIND_OPERATION;

#define NDIS6_UNBIND_OPERATION_SIGNATURE 'bUNn'

VOID
EXPORT
NdisCompleteUnbindAdapterEx(
    _In_ NDIS_HANDLE UnbindContext)
{
    PNDIS6_UNBIND_OPERATION Operation = UnbindContext;

    if (Operation == NULL ||
        Operation->Signature != NDIS6_UNBIND_OPERATION_SIGNATURE)
    {
        return;
    }

    if (InterlockedCompareExchange(&Operation->Completed, 1, 0) == 0)
        KeSetEvent(&Operation->CompleteEvent, IO_NO_INCREMENT, FALSE);
}

typedef struct _NDIS6_NET_PNP_OPERATION
{
    ULONG Signature;
    PNDIS6_PROTOCOL_BINDING Binding;
    NET_PNP_EVENT_NOTIFICATION Notification;
    KEVENT CompleteEvent;
    volatile LONG Completed;
    NDIS_STATUS CompletionStatus;
} NDIS6_NET_PNP_OPERATION, *PNDIS6_NET_PNP_OPERATION;

#define NDIS6_NET_PNP_OPERATION_SIGNATURE 'pPNn'

VOID
EXPORT
NdisCompleteNetPnPEvent(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification,
    _In_ NDIS_STATUS Status)
{
    PNDIS6_NET_PNP_OPERATION Operation;

    if (NdisBindingHandle == NULL || NetPnPEventNotification == NULL)
        return;

    Operation = CONTAINING_RECORD(NetPnPEventNotification, NDIS6_NET_PNP_OPERATION, Notification);
    if (Operation->Signature != NDIS6_NET_PNP_OPERATION_SIGNATURE ||
        Operation->Binding != (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle)
    {
        return;
    }

    if (InterlockedCompareExchange(&Operation->Completed, -1, 0) == 0)
    {
        Operation->CompletionStatus = Status;
        InterlockedExchange(&Operation->Completed, 1);
        KeSetEvent(&Operation->CompleteEvent, IO_NO_INCREMENT, FALSE);
    }
}

static NDIS_STATUS
Ndis6CallProtocolNetPnPEvent(
    _In_ PNDIS6_PROTOCOL_BINDING Binding,
    _In_ PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification)
{
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block = Binding->DriverBlock;
    NDIS6_NET_PNP_OPERATION Operation;
    NDIS_STATUS Status;
    NTSTATUS WaitStatus;

    if (Block == NULL || Block->Characteristics.NetPnPEventHandler == NULL)
        return NDIS_STATUS_SUCCESS;

    RtlZeroMemory(&Operation, sizeof(Operation));
    Operation.Signature = NDIS6_NET_PNP_OPERATION_SIGNATURE;
    Operation.Binding = Binding;
    Operation.CompletionStatus = NDIS_STATUS_PENDING;
    KeInitializeEvent(&Operation.CompleteEvent, NotificationEvent, FALSE);

    Operation.Notification.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    if (Block->Characteristics.MinorNdisVersion >= 50)
    {
        Operation.Notification.Header.Revision =
            NET_PNP_EVENT_NOTIFICATION_REVISION_2;
        Operation.Notification.Header.Size =
            NDIS_SIZEOF_NET_PNP_EVENT_NOTIFICATION_REVISION_2;
    }
    else
    {
        Operation.Notification.Header.Revision =
            NET_PNP_EVENT_NOTIFICATION_REVISION_1;
        Operation.Notification.Header.Size =
            NDIS_SIZEOF_NET_PNP_EVENT_NOTIFICATION_REVISION_1;
    }
    Operation.Notification.PortNumber = NetPnPEventNotification->PortNumber;
    Operation.Notification.NetPnPEvent = NetPnPEventNotification->NetPnPEvent;
    Operation.Notification.Flags = NetPnPEventNotification->Flags;
    if (Operation.Notification.Header.Revision >= NET_PNP_EVENT_NOTIFICATION_REVISION_2 &&
        NetPnPEventNotification->Header.Revision >= NET_PNP_EVENT_NOTIFICATION_REVISION_2 &&
        NetPnPEventNotification->Header.Size >= NDIS_SIZEOF_NET_PNP_EVENT_NOTIFICATION_REVISION_2)
    {
        Operation.Notification.SwitchId = NetPnPEventNotification->SwitchId;
        Operation.Notification.VPortId = NetPnPEventNotification->VPortId;
    }

    Status = Block->Characteristics.NetPnPEventHandler(Binding->ProtocolBindingContext, &Operation.Notification);
    if (Status == NDIS_STATUS_PENDING)
    {
        WaitStatus = KeWaitForSingleObject(&Operation.CompleteEvent, Executive, KernelMode, FALSE, NULL);
        Status = NT_SUCCESS(WaitStatus)
            ? Operation.CompletionStatus
            : (NDIS_STATUS)WaitStatus;
    }

    Operation.Signature = 0;
    return Status;
}

static NDIS_STATUS
Ndis6CallProtocolNetPnPEventCode(
    _In_ PNDIS6_PROTOCOL_BINDING Binding,
    _In_ NET_PNP_EVENT_CODE EventCode,
    _In_opt_ PVOID EventBuffer,
    _In_ ULONG EventBufferLength)
{
    NET_PNP_EVENT_NOTIFICATION Notification;

    RtlZeroMemory(&Notification, sizeof(Notification));
    Notification.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Notification.Header.Revision = NET_PNP_EVENT_NOTIFICATION_REVISION_2;
    Notification.Header.Size =
        NDIS_SIZEOF_NET_PNP_EVENT_NOTIFICATION_REVISION_2;
    Notification.NetPnPEvent.NetEvent = EventCode;
    Notification.NetPnPEvent.Buffer = EventBuffer;
    Notification.NetPnPEvent.BufferLength = EventBufferLength;
    return Ndis6CallProtocolNetPnPEvent(Binding, &Notification);
}

static NDIS_STATUS
Ndis6SnapshotProtocolBindings(
    _In_ PNDIS6_ADAPTER_EXT Ext,
    _Outptr_result_buffer_(*SnapshotCount)
        PNDIS6_PROTOCOL_BINDING **SnapshotOut,
    _Out_ PSIZE_T SnapshotCount)
{
    PNDIS6_PROTOCOL_BINDING *Snapshot;
    SIZE_T Capacity = 0;
    SIZE_T Count = 0;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    *SnapshotOut = NULL;
    *SnapshotCount = 0;

    KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &OldIrql);
    for (Entry = Ext->ProtocolBindingList.Flink;
         Entry != &Ext->ProtocolBindingList;
         Entry = Entry->Flink)
    {
        Capacity++;
    }
    KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);

    if (Capacity == 0)
        return NDIS_STATUS_SUCCESS;

    if (Capacity > MAXULONG_PTR / sizeof(*Snapshot))
        return NDIS_STATUS_RESOURCES;

    Snapshot = ExAllocatePoolWithTag(NonPagedPool, Capacity * sizeof(*Snapshot), NDIS6_PROTOCOL_SNAPSHOT_TAG);
    if (Snapshot == NULL)
        return NDIS_STATUS_RESOURCES;

    KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &OldIrql);
    for (Entry = Ext->ProtocolBindingList.Flink;
         Entry != &Ext->ProtocolBindingList && Count < Capacity;
         Entry = Entry->Flink)
    {
        PNDIS6_PROTOCOL_BINDING Binding =
            CONTAINING_RECORD(Entry, NDIS6_PROTOCOL_BINDING, AdapterLink);

        if (Ndis6ReferenceProtocolBinding(Binding))
            Snapshot[Count++] = Binding;
    }
    KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);

    if (Count == 0)
    {
        ExFreePoolWithTag(Snapshot, NDIS6_PROTOCOL_SNAPSHOT_TAG);
        return NDIS_STATUS_SUCCESS;
    }

    *SnapshotOut = Snapshot;
    *SnapshotCount = Count;
    return NDIS_STATUS_SUCCESS;
}

static VOID
Ndis6ReleaseProtocolBindingSnapshot(
    _In_reads_opt_(SnapshotCount) PNDIS6_PROTOCOL_BINDING *Snapshot,
    _In_ SIZE_T SnapshotCount)
{
    SIZE_T Index;

    for (Index = 0; Index < SnapshotCount; Index++)
        Ndis6DereferenceProtocolBinding(Snapshot[Index]);

    if (Snapshot != NULL)
        ExFreePoolWithTag(Snapshot, NDIS6_PROTOCOL_SNAPSHOT_TAG);
}

NDIS_STATUS
Ndis6FilterTerminalNetPnPEvent(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_PNP_EVENT_NOTIFICATION NetPnPEventNotification)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_PROTOCOL_BINDING *Snapshot;
    SIZE_T SnapshotCount;
    SIZE_T Index;
    NDIS_STATUS OverallStatus = NDIS_STATUS_SUCCESS;
    NDIS_STATUS Status;

    if (Adapter == NULL || !Adapter->IsNdis6 ||
        NetPnPEventNotification == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (InterlockedCompareExchange(&g_Ndis6ProtocolDriverListState, 2, 2) != 2)
        return NDIS_STATUS_SUCCESS;

    Status = Ndis6SnapshotProtocolBindings(Ext, &Snapshot, &SnapshotCount);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    /* Protocol callbacks may pend and may re-enter unrelated NDIS lifecycle
     * paths. Keep only per-binding rundown references while calling them;
     * global registration serialization must never cover driver callbacks. */
    for (Index = 0; Index < SnapshotCount; Index++)
    {
        Status = Ndis6CallProtocolNetPnPEvent(Snapshot[Index], NetPnPEventNotification);
        if (Status != NDIS_STATUS_SUCCESS)
            OverallStatus = Status;
    }

    Ndis6ReleaseProtocolBindingSnapshot(Snapshot, SnapshotCount);
    return OverallStatus;
}

static NDIS_STATUS
Ndis6NotifyProtocolBindingsEvent(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ NET_PNP_EVENT_CODE EventCode,
    _In_opt_ PVOID EventBuffer,
    _In_ ULONG EventBufferLength)
{
    NET_PNP_EVENT_NOTIFICATION Notification;

    RtlZeroMemory(&Notification, sizeof(Notification));
    Notification.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Notification.Header.Revision = NET_PNP_EVENT_NOTIFICATION_REVISION_2;
    Notification.Header.Size =
        NDIS_SIZEOF_NET_PNP_EVENT_NOTIFICATION_REVISION_2;
    Notification.NetPnPEvent.NetEvent = EventCode;
    Notification.NetPnPEvent.Buffer = EventBuffer;
    Notification.NetPnPEvent.BufferLength = EventBufferLength;
    return Ndis6FilterDispatchNetPnPEvent(Adapter, &Notification);
}

NDIS_STATUS
Ndis6NotifyProtocolBindingsPower(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ DEVICE_POWER_STATE DevicePowerState)
{
    NDIS_DEVICE_POWER_STATE NdisPowerState;

    if (DevicePowerState < PowerDeviceD0 ||
        DevicePowerState > PowerDeviceD3)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    NdisPowerState = (NDIS_DEVICE_POWER_STATE)DevicePowerState;
    return Ndis6NotifyProtocolBindingsEvent(Adapter, NetEventSetPower, &NdisPowerState, sizeof(NdisPowerState));
}

static NDIS_STATUS
Ndis6PauseProtocolBinding(
    _In_ PNDIS6_PROTOCOL_BINDING Binding)
{
    NDIS_PROTOCOL_PAUSE_PARAMETERS PauseParameters;
    NDIS_STATUS Status;
    LONG State;

    State = InterlockedCompareExchange(&Binding->State, NDIS6_PROTOCOL_STATE_PAUSING, NDIS6_PROTOCOL_STATE_RUNNING);
    if (State == NDIS6_PROTOCOL_STATE_PAUSED)
        return NDIS_STATUS_SUCCESS;
    if (State != NDIS6_PROTOCOL_STATE_RUNNING)
        return NDIS_STATUS_INVALID_STATE;

    RtlZeroMemory(&PauseParameters, sizeof(PauseParameters));
    PauseParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    PauseParameters.Header.Revision =
        NDIS_PROTOCOL_PAUSE_PARAMETERS_REVISION_1;
    PauseParameters.Header.Size =
        NDIS_SIZEOF_PROTOCOL_PAUSE_PARAMETERS_REVISION_1;

    Status = Ndis6CallProtocolNetPnPEventCode(Binding, NetEventPause, &PauseParameters, sizeof(PauseParameters));
    InterlockedExchange(&Binding->State, Status == NDIS_STATUS_SUCCESS ? NDIS6_PROTOCOL_STATE_PAUSED : NDIS6_PROTOCOL_STATE_RUNNING);
    return Status;
}

NDIS_STATUS
Ndis6PauseProtocolBindings(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_PROTOCOL_BINDING *Snapshot;
    SIZE_T SnapshotCount;
    SIZE_T Index;
    NDIS_STATUS OverallStatus = NDIS_STATUS_SUCCESS;
    NDIS_STATUS Status;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (InterlockedCompareExchange(&g_Ndis6ProtocolDriverListState, 2, 2) != 2)
        return NDIS_STATUS_SUCCESS;

    Status = Ndis6SnapshotProtocolBindings(Ext, &Snapshot, &SnapshotCount);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    for (Index = 0; Index < SnapshotCount; Index++)
    {
        Status = Ndis6PauseProtocolBinding(Snapshot[Index]);
        if (Status != NDIS_STATUS_SUCCESS &&
            Status != NDIS_STATUS_INVALID_STATE)
        {
            OverallStatus = Status;
        }
    }

    Ndis6ReleaseProtocolBindingSnapshot(Snapshot, SnapshotCount);
    return OverallStatus;
}

VOID
Ndis6RestartProtocolBindings(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_PROTOCOL_BINDING *Snapshot;
    SIZE_T SnapshotCount;
    SIZE_T Index;
    NDIS_STATUS SnapshotStatus;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    if (InterlockedCompareExchange(&g_Ndis6ProtocolDriverListState, 2, 2) != 2)
        return;

    SnapshotStatus = Ndis6SnapshotProtocolBindings(Ext, &Snapshot, &SnapshotCount);
    if (SnapshotStatus != NDIS_STATUS_SUCCESS)
    {
        DbgPrint("NDIS6: unable to snapshot protocol bindings for restart\n");
        return;
    }

    for (Index = 0; Index < SnapshotCount; Index++)
    {
        PNDIS6_PROTOCOL_BINDING Binding = Snapshot[Index];

        if (InterlockedCompareExchange(&Binding->State, NDIS6_PROTOCOL_STATE_RESTARTING, NDIS6_PROTOCOL_STATE_PAUSED) ==
            NDIS6_PROTOCOL_STATE_PAUSED)
        {
            NDIS_PROTOCOL_RESTART_PARAMETERS RestartParameters;
            NDIS_STATUS Status;

            RtlZeroMemory(&RestartParameters, sizeof(RestartParameters));
            RestartParameters.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
            RestartParameters.Header.Revision =
                NDIS_PROTOCOL_RESTART_PARAMETERS_REVISION_1;
            RestartParameters.Header.Size =
                NDIS_SIZEOF_PROTOCOL_RESTART_PARAMETERS_REVISION_1;

            Status = Ndis6CallProtocolNetPnPEventCode(Binding, NetEventRestart, &RestartParameters, sizeof(RestartParameters));
            InterlockedExchange(&Binding->State, Status == NDIS_STATUS_SUCCESS ? NDIS6_PROTOCOL_STATE_RUNNING : NDIS6_PROTOCOL_STATE_PAUSED);
        }
    }

    Ndis6ReleaseProtocolBindingSnapshot(Snapshot, SnapshotCount);
}

static BOOLEAN
Ndis6ProtocolBindingStillOpen(
    _In_ PNDIS6_PROTOCOL_DRIVER_BLOCK Block,
    _In_ PNDIS6_PROTOCOL_BINDING Binding)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    KeAcquireSpinLock(&Block->BindingListLock, &OldIrql);
    for (Entry = Block->BindingList.Flink;
         Entry != &Block->BindingList;
         Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, NDIS6_PROTOCOL_BINDING, ListEntry) == Binding)
        {
            Found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&Block->BindingListLock, OldIrql);
    return Found;
}

static VOID
Ndis6UnbindProtocolBinding(
    _In_ PNDIS6_PROTOCOL_BINDING Binding)
{
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block;
    PROTOCOL_UNBIND_ADAPTER_EX_HANDLER UnbindHandler;
    NDIS_HANDLE ProtocolBindingContext;
    NDIS6_UNBIND_OPERATION Operation;
    NDIS_STATUS Status;

    Block = Binding->DriverBlock;
    if (Block == NULL)
        return;

    UnbindHandler = Block->Characteristics.UnbindAdapterHandlerEx;
    ProtocolBindingContext = Binding->ProtocolBindingContext;

    if (Ndis6ReferenceProtocolBinding(Binding))
    {
        (VOID)Ndis6PauseProtocolBinding(Binding);
        Ndis6DereferenceProtocolBinding(Binding);
    }

    RtlZeroMemory(&Operation, sizeof(Operation));
    Operation.Signature = NDIS6_UNBIND_OPERATION_SIGNATURE;
    KeInitializeEvent(&Operation.CompleteEvent, NotificationEvent, FALSE);

    Status = UnbindHandler((NDIS_HANDLE)&Operation, ProtocolBindingContext);
    if (Status == NDIS_STATUS_PENDING)
    {
        (VOID)KeWaitForSingleObject(&Operation.CompleteEvent, Executive, KernelMode, FALSE, NULL);
    }
    else if (Status != NDIS_STATUS_SUCCESS)
    {
        DbgPrint("NDIS6: ProtocolUnbindAdapterEx returned invalid status 0x%08lx\n", (ULONG)Status);
    }

    Operation.Signature = 0;

    /* ProtocolUnbindAdapterEx is required to close the binding and cannot
     * fail. Keep teardown memory-safe even when a defective protocol omits
     * NdisCloseAdapterEx. */
    if (Ndis6ProtocolBindingStillOpen(Block, Binding))
    {
        DbgPrint("NDIS6: protocol returned from unbind with an open binding; closing it\n");
        (VOID)NdisCloseAdapterEx((NDIS_HANDLE)Binding);
    }
}

VOID
Ndis6UnbindAllProtocolsFromAdapter(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    InterlockedExchange(&Ext->ProtocolBindingsClosing, 1);

    if (InterlockedCompareExchange(&g_Ndis6ProtocolDriverListState, 2, 2) != 2)
        return;

    (VOID)KeWaitForSingleObject(&g_Ndis6ProtocolLifecycleMutex, Executive, KernelMode, FALSE, NULL);
    for (;;)
    {
        PNDIS6_PROTOCOL_BINDING Binding;
        KIRQL OldIrql;

        KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &OldIrql);
        if (IsListEmpty(&Ext->ProtocolBindingList))
        {
            KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);
            break;
        }

        Binding = CONTAINING_RECORD(Ext->ProtocolBindingList.Flink, NDIS6_PROTOCOL_BINDING, AdapterLink);
        KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);
        Ndis6UnbindProtocolBinding(Binding);
    }
    KeReleaseMutex(&g_Ndis6ProtocolLifecycleMutex, FALSE);
}

VOID
EXPORT
NdisDeregisterProtocolDriver(
    _In_ NDIS_HANDLE NdisProtocolHandle)
{
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block = (PNDIS6_PROTOCOL_DRIVER_BLOCK)NdisProtocolHandle;
    KIRQL OldIrql;

    if (Block == NULL || Block->Signature != NDIS6_PROTOCOL_DRIVER_SIGNATURE ||
        InterlockedCompareExchange(&Block->Closing, 1, 0) != 0)
    {
        return;
    }

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    (VOID)KeWaitForSingleObject(&g_Ndis6ProtocolLifecycleMutex, Executive, KernelMode, FALSE, NULL);

    KeAcquireSpinLock(&g_Ndis6ProtocolDriverListLock, &OldIrql);
    if (Block->ListEntry.Flink != NULL && Block->ListEntry.Blink != NULL)
    {
        RemoveEntryList(&Block->ListEntry);
        Block->ListEntry.Flink = NULL;
        Block->ListEntry.Blink = NULL;
    }
    KeReleaseSpinLock(&g_Ndis6ProtocolDriverListLock, OldIrql);

    ExWaitForRundownProtectionRelease(&Block->CallbackRundown);

    for (;;)
    {
        PNDIS6_PROTOCOL_BINDING Binding;

        KeAcquireSpinLock(&Block->BindingListLock, &OldIrql);
        if (IsListEmpty(&Block->BindingList))
        {
            KeReleaseSpinLock(&Block->BindingListLock, OldIrql);
            break;
        }
        Binding = CONTAINING_RECORD(Block->BindingList.Flink, NDIS6_PROTOCOL_BINDING, ListEntry);
        KeReleaseSpinLock(&Block->BindingListLock, OldIrql);
        Ndis6UnbindProtocolBinding(Binding);
    }

    KeReleaseMutex(&g_Ndis6ProtocolLifecycleMutex, FALSE);

    Block->Signature = 0;
    if (Block->NameBuffer != NULL)
        ExFreePoolWithTag(Block->NameBuffer, NDIS6_PROTOCOL_DRIVER_TAG);
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
 *  We pick the first entry matching the miniport's registered medium.
 *
 *  Synchronous open only. PENDING completion would need a per-binding
 *  operation context that this bridge does not yet require.
 * ============================================================================ */

#define NDIS6_PROTOCOL_BINDING_TAG  'bPNn'  /* "nNPb" */

NDIS_STATUS
NTAPI
NdisOpenAdapterEx(
    _In_  NDIS_HANDLE  NdisProtocolHandle,
    _In_  NDIS_HANDLE  ProtocolBindingContext,
    _In_  PNDIS_OPEN_PARAMETERS OpenParameters,
    _In_  NDIS_HANDLE  BindContext,
    _Out_ PNDIS_HANDLE NdisBindingHandle)
{
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block = (PNDIS6_PROTOCOL_DRIVER_BLOCK)NdisProtocolHandle;
    PNDIS6_PROTOCOL_BINDING      Binding;
    PNDIS6_BIND_OPERATION        BindOperation;
    PLOGICAL_ADAPTER             Adapter;
    PNDIS6_ADAPTER_EXT           Ext;
    PNDIS_OPEN_PARAMETERS        OpenParams = OpenParameters;

    if (Block == NULL || NdisBindingHandle == NULL || BindContext == NULL ||
        OpenParams == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    *NdisBindingHandle = NULL;

    if (!Ndis6ReferenceProtocolDriver(Block))
        return NDIS_STATUS_CLOSING;

    if (OpenParams->Header.Type != NDIS_OBJECT_TYPE_OPEN_PARAMETERS ||
        OpenParams->Header.Revision != NDIS_OPEN_PARAMETERS_REVISION_1 ||
        OpenParams->Header.Size < NDIS_SIZEOF_OPEN_PARAMETERS_REVISION_1 ||
        OpenParams->Header.Size > sizeof(*OpenParams) ||
        OpenParams->MediumArray == NULL ||
        OpenParams->MediumArraySize == 0 ||
        OpenParams->SelectedMediumIndex == NULL)
    {
        Ndis6DereferenceProtocolDriver(Block);
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    /* BindContext identifies one NDIS-owned bind operation. It is deliberately
     * not an adapter handle: protocols may retain it only until they complete
     * a pending bind through NdisCompleteBindAdapterEx. */
    BindOperation = (PNDIS6_BIND_OPERATION)BindContext;
    Adapter = (BindOperation->Signature == NDIS6_BIND_OPERATION_SIGNATURE)
        ? BindOperation->Adapter
        : NULL;
    if (Adapter == NULL || !Adapter->IsNdis6)
    {
        Ndis6DereferenceProtocolDriver(Block);
        return NDIS_STATUS_FAILURE;
    }

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
    {
        Ndis6DereferenceProtocolDriver(Block);
        return NDIS_STATUS_FAILURE;
    }

    Binding = (PNDIS6_PROTOCOL_BINDING)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_PROTOCOL_BINDING),
        NDIS6_PROTOCOL_BINDING_TAG);
    if (Binding == NULL)
    {
        Ndis6DereferenceProtocolDriver(Block);
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(Binding, sizeof(*Binding));
    Binding->Signature              = NDIS6_PROTOCOL_BINDING_SIGNATURE;
    Binding->DriverBlock            = Block;
    Binding->Adapter                = Adapter;
    Binding->ProtocolBindingContext = ProtocolBindingContext;
    /* D4: initialize per-binding pending-OID list. */
    InitializeListHead(&Binding->PendingOidRequests);
    KeInitializeSpinLock(&Binding->PendingOidRequestsLock);
    ExInitializeRundownProtection(&Binding->RundownRef);
    Binding->Closing = FALSE;
    Binding->State = NDIS6_PROTOCOL_STATE_PAUSED;

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
                Ndis6DereferenceProtocolDriver(Block);
                return NDIS_STATUS_UNSUPPORTED_MEDIA;
            }
        }
        /* FrameTypeArray: ignored — we deliver raw Ethernet II frames
         * to the protocol and let it decode. */
    }

    /* Link this binding into the adapter and protocol ownership lists before
     * returning the handle. Receive indications use one clone per binding, so
     * multiple native protocols can bind to the same adapter independently. */
    {
        KIRQL OldIrql;

        KIRQL DriverIrql;

        KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &OldIrql);
        KeAcquireSpinLock(&Block->BindingListLock, &DriverIrql);
        if (InterlockedCompareExchange(&Block->Closing, 0, 0) != 0 ||
            InterlockedCompareExchange(&Ext->ProtocolBindingsClosing, 0, 0) != 0 ||
            !Adapter->IsNdis6 || NDIS6_EXT(Adapter) != Ext)
        {
            KeReleaseSpinLock(&Block->BindingListLock, DriverIrql);
            KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);
            ExFreePoolWithTag(Binding, NDIS6_PROTOCOL_BINDING_TAG);
            Ndis6DereferenceProtocolDriver(Block);
            return NDIS_STATUS_CLOSING;
        }
        InsertTailList(&Ext->ProtocolBindingList, &Binding->AdapterLink);
        InsertTailList(&Block->BindingList, &Binding->ListEntry);
        KeReleaseSpinLock(&Block->BindingListLock, DriverIrql);
        KeReleaseSpinLock(&Ext->ProtocolBindingListLock, OldIrql);
    }

    *NdisBindingHandle = (NDIS_HANDLE)Binding;

    /* The completion callback is reserved for an asynchronous open. This
     * implementation completes synchronously and therefore only returns the
     * status and binding handle. */
    Ndis6DereferenceProtocolDriver(Block);

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NdisCloseAdapterEx(
    _In_ NDIS_HANDLE NdisBindingHandle)
{
    PNDIS6_PROTOCOL_BINDING Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;
    PNDIS6_PROTOCOL_DRIVER_BLOCK Block;

    if (Binding == NULL ||
        Binding->Signature != NDIS6_PROTOCOL_BINDING_SIGNATURE)
        return NDIS_STATUS_INVALID_PARAMETER;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    Block = Binding->DriverBlock;
    if (InterlockedCompareExchange(&Binding->Closing, TRUE, FALSE) != FALSE)
        return NDIS_STATUS_CLOSING;

    InterlockedExchange(&Binding->State, NDIS6_PROTOCOL_STATE_PAUSED);

    /* Unlink from both ownership lists before beginning rundown. Adapter and
     * driver list locks are always acquired in this order. */
    if (Binding->Adapter != NULL)
    {
        PNDIS6_ADAPTER_EXT Ext = NDIS6_EXT(Binding->Adapter);
        if (Ext != NULL)
        {
            KIRQL AdapterIrql;
            KIRQL DriverIrql;

            KeAcquireSpinLock(&Ext->ProtocolBindingListLock, &AdapterIrql);
            if (Block != NULL)
                KeAcquireSpinLock(&Block->BindingListLock, &DriverIrql);
            if (Binding->AdapterLink.Flink != NULL &&
                Binding->AdapterLink.Blink != NULL)
            {
                RemoveEntryList(&Binding->AdapterLink);
                Binding->AdapterLink.Flink = NULL;
                Binding->AdapterLink.Blink = NULL;
            }
            if (Block != NULL &&
                Binding->ListEntry.Flink != NULL &&
                Binding->ListEntry.Blink != NULL)
            {
                RemoveEntryList(&Binding->ListEntry);
                Binding->ListEntry.Flink = NULL;
                Binding->ListEntry.Blink = NULL;
            }
            if (Block != NULL)
                KeReleaseSpinLock(&Block->BindingListLock, DriverIrql);
            KeReleaseSpinLock(&Ext->ProtocolBindingListLock, AdapterIrql);
        }
    }
    if (Block != NULL &&
        Binding->ListEntry.Flink != NULL &&
        Binding->ListEntry.Blink != NULL)
    {
        KIRQL DriverIrql;
        KeAcquireSpinLock(&Block->BindingListLock, &DriverIrql);
        if (Binding->ListEntry.Flink != NULL &&
            Binding->ListEntry.Blink != NULL)
        {
            RemoveEntryList(&Binding->ListEntry);
            Binding->ListEntry.Flink = NULL;
            Binding->ListEntry.Blink = NULL;
        }
        KeReleaseSpinLock(&Block->BindingListLock, DriverIrql);
    }

    ExWaitForRundownProtectionRelease(&Binding->RundownRef);

    if (!IsListEmpty(&Binding->PendingOidRequests))
    {
        DbgPrint("NDIS6: protocol closed a binding with pending OID requests\n");
    }

    /* The close completed synchronously; ProtocolCloseAdapterCompleteEx is
     * called only when NdisCloseAdapterEx returned NDIS_STATUS_PENDING. */
    Binding->Signature = 0;
    ExFreePoolWithTag(Binding, NDIS6_PROTOCOL_BINDING_TAG);
    return NDIS_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  Native NDIS 6 protocol datapath                                   */
/* ------------------------------------------------------------------ */

static ULONG
Ndis6SendFlagsToCompletionFlags(
    _In_ ULONG SendFlags)
{
    ULONG SendCompleteFlags = 0;

    if (SendFlags & NDIS_SEND_FLAGS_DISPATCH_LEVEL)
        SendCompleteFlags |= NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL;
    if (SendFlags & NDIS_SEND_FLAGS_SINGLE_QUEUE)
        SendCompleteFlags |= NDIS_SEND_COMPLETE_FLAGS_SINGLE_QUEUE;
    if (SendFlags & NDIS_SEND_FLAGS_SWITCH_SINGLE_SOURCE)
        SendCompleteFlags |= NDIS_SEND_COMPLETE_FLAGS_SWITCH_SINGLE_SOURCE;
    return SendCompleteFlags;
}

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
    ULONG                   SendCompleteFlags;

    if (Binding == NULL || NetBufferList == NULL)
        return;

    SendCompleteFlags = Ndis6SendFlagsToCompletionFlags(SendFlags);

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

    if (InterlockedCompareExchange(&Binding->State, NDIS6_PROTOCOL_STATE_RUNNING, NDIS6_PROTOCOL_STATE_RUNNING) !=
        NDIS6_PROTOCOL_STATE_RUNNING)
    {
        for (CurrentNbl = NetBufferList; CurrentNbl != NULL;
             CurrentNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl))
        {
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_PAUSED;
        }
        if (Binding->DriverBlock != NULL &&
            Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler != NULL)
        {
            Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(Binding->ProtocolBindingContext, NetBufferList, SendCompleteFlags);
        }
        Ndis6DereferenceProtocolBinding(Binding);
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
            Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(Binding->ProtocolBindingContext, NetBufferList, SendCompleteFlags);
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
                Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(Binding->ProtocolBindingContext, CurrentNbl, SendCompleteFlags);
            continue;
        }

        if (!Ndis6ReferenceNativeTransmit(Ext))
        {
            NET_BUFFER_LIST_STATUS(CurrentNbl) = NDIS_STATUS_PAUSED;
            if (Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler != NULL)
                Binding->DriverBlock->Characteristics.SendNetBufferListsCompleteHandler(Binding->ProtocolBindingContext, CurrentNbl, SendCompleteFlags);
            Ndis6DereferenceProtocolBinding(Binding);
            continue;
        }

        CurrentNbl->SourceHandle      = (NDIS_HANDLE)Binding;
        CurrentNbl->NdisReserved[1]   = Binding;

        Ndis6FilterDispatchSend(Adapter, CurrentNbl, PortNumber, SendFlags);
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

NDIS_STATUS
EXPORT
NdisOidRequest(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_PROTOCOL_BINDING    Binding;
    PLOGICAL_ADAPTER           Adapter;
    PNDIS6_ADAPTER_EXT         Ext;
    PNDIS6_PROTOCOL_PENDING_OID Pending = NULL;
    NDIS_STATUS                Status;
    KIRQL                      OldIrql;

    if (NdisBindingHandle == NULL || OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;
    if (!Ndis6ReferenceProtocolBinding(Binding))
        return NDIS_STATUS_CLOSING;

    Adapter = Binding->Adapter;
    if (Adapter == NULL || !Adapter->IsNdis6 || Binding->DriverBlock == NULL)
    {
        Ndis6DereferenceProtocolBinding(Binding);
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL)
    {
        Ndis6DereferenceProtocolBinding(Binding);
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    /* For a real protocol binding we can wire async completion. Allocate
     * a pending context and keep the protocol's RequestId unchanged. */
    if (Binding != NULL)
    {
        Pending = (PNDIS6_PROTOCOL_PENDING_OID)ExAllocatePoolWithTag(
            NonPagedPool, sizeof(NDIS6_PROTOCOL_PENDING_OID),
            NDIS6_PROTOCOL_PENDING_OID_TAG);
        if (Pending == NULL)
        {
            Ndis6DereferenceProtocolBinding(Binding);
            return NDIS_STATUS_RESOURCES;
        }

        RtlZeroMemory(Pending, sizeof(*Pending));
        Pending->Signature         = NDIS6_PROTOCOL_PENDING_OID_SIGNATURE;
        Pending->Binding           = Binding;
        Pending->References        = 2; /* call stack + pending-list ownership */
        Pending->Listed            = TRUE;
        Pending->DirectRequest     = FALSE;

        KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &OldIrql);
        InsertTailList(&Binding->PendingOidRequests, &Pending->ListEntry);
        KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);

        /* NdisReserved belongs to NDIS; RequestId remains caller-opaque all
         * the way through filters and the miniport. */
        Ndis6SetPendingOidContext(OidRequest, Pending);
    }

    Status = Ndis6FilterDispatchOidRequest(Adapter, OidRequest);

    if (Status == NDIS_STATUS_PENDING)
    {
        /* Async completion will come via NdisMOidRequestComplete →
         * the per-binding pending list → protocol's completion handler. */
        Ndis6DereferencePendingOid(Pending);
        return NDIS_STATUS_PENDING;
    }

    /* Synchronous completion — clear NDIS bookkeeping and pop the entry. */
    if (Pending != NULL)
    {
        BOOLEAN Removed = FALSE;

        Ndis6ClearPendingOidContext(OidRequest);
        KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &OldIrql);
        if (Pending->Listed)
        {
            RemoveEntryList(&Pending->ListEntry);
            Pending->Listed = FALSE;
            Removed = TRUE;
        }
        KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);
        if (Removed)
            Ndis6DereferencePendingOid(Pending);
        Ndis6DereferencePendingOid(Pending);
    }
    return Status;
}

#if NDIS_SUPPORT_NDIS680
NDIS_STATUS
EXPORT
NdisSynchronousOidRequest(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    PNDIS6_PROTOCOL_BINDING Binding = NdisBindingHandle;
    PLOGICAL_ADAPTER Adapter;
    NDIS_STATUS Status;

    if (OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;
    if (!Ndis6ReferenceProtocolBinding(Binding))
        return NDIS_STATUS_CLOSING;

    Adapter = Binding->Adapter;
    if (Adapter == NULL || !Adapter->IsNdis6 || NDIS6_EXT(Adapter) == NULL)
    {
        Ndis6DereferenceProtocolBinding(Binding);
        return NDIS_STATUS_CLOSING;
    }

    Status = Ndis6FilterDispatchSynchronousOidRequest(Adapter, OidRequest);
    Ndis6DereferenceProtocolBinding(Binding);
    return Status;
}
#endif

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
    PNDIS6_PROTOCOL_BINDING     Binding;
    PLOGICAL_ADAPTER            Adapter;
    PNDIS6_ADAPTER_EXT          Ext;
    PNDIS6_PROTOCOL_PENDING_OID Pending = NULL;
    NDIS_STATUS                 Status;
    KIRQL                       OldIrql;

    if (NdisBindingHandle == NULL || OidRequest == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Binding = (PNDIS6_PROTOCOL_BINDING)NdisBindingHandle;
    if (!Ndis6ReferenceProtocolBinding(Binding))
        return NDIS_STATUS_CLOSING;

    Adapter = Binding->Adapter;
    if (Adapter == NULL || !Adapter->IsNdis6 || Binding->DriverBlock == NULL)
    {
        Ndis6DereferenceProtocolBinding(Binding);
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL)
    {
        Ndis6DereferenceProtocolBinding(Binding);
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    if (Binding != NULL)
    {
        Pending = (PNDIS6_PROTOCOL_PENDING_OID)ExAllocatePoolWithTag(NonPagedPool, sizeof(NDIS6_PROTOCOL_PENDING_OID), NDIS6_PROTOCOL_PENDING_OID_TAG);
        if (Pending == NULL)
        {
            Ndis6DereferenceProtocolBinding(Binding);
            return NDIS_STATUS_RESOURCES;
        }

        RtlZeroMemory(Pending, sizeof(*Pending));
        Pending->Signature         = NDIS6_PROTOCOL_PENDING_OID_SIGNATURE;
        Pending->Binding           = Binding;
        Pending->References        = 2;
        Pending->Listed            = TRUE;
        Pending->DirectRequest     = TRUE;

        KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &OldIrql);
        InsertTailList(&Binding->PendingOidRequests, &Pending->ListEntry);
        KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);

        Ndis6SetPendingOidContext(OidRequest, Pending);
    }

    Status = Ndis6FilterDispatchDirectOidRequest(Adapter, OidRequest);

    if (Status == NDIS_STATUS_PENDING)
    {
        Ndis6DereferencePendingOid(Pending);
        return NDIS_STATUS_PENDING;
    }

    if (Pending != NULL)
    {
        BOOLEAN Removed = FALSE;

        Ndis6ClearPendingOidContext(OidRequest);
        KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &OldIrql);
        if (Pending->Listed)
        {
            RemoveEntryList(&Pending->ListEntry);
            Pending->Listed = FALSE;
            Removed = TRUE;
        }
        KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);
        if (Removed)
            Ndis6DereferencePendingOid(Pending);
        Ndis6DereferencePendingOid(Pending);
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

    if (Binding == NULL || !Ndis6ReferenceProtocolBinding(Binding))
        return;

    if (Binding->Adapter != NULL)
    {
        Ndis6FilterCancelOidRequestFromProtocol(Binding->Adapter, Binding, RequestId, TRUE);
    }

    Ndis6DereferenceProtocolBinding(Binding);
}

VOID
Ndis6CompleteDirectOidRequestToOrigin(
    _In_ PLOGICAL_ADAPTER  Adapter,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS       Status)
{
    PNDIS6_PROTOCOL_PENDING_OID Pending;
    PNDIS6_PROTOCOL_BINDING     Binding;
    KIRQL                       OldIrql;

    if (Adapter == NULL || !Adapter->IsNdis6 || OidRequest == NULL)
        return;

    /* Direct OIDs issued by protocols carry their binding bookkeeping in
     * NdisReserved. RequestId is never repurposed by NDIS. */
    Pending = Ndis6GetPendingOidContext(OidRequest);
    if (Pending == NULL ||
        Pending->Signature != NDIS6_PROTOCOL_PENDING_OID_SIGNATURE)
        return;
    if (!Pending->DirectRequest)
    {
        DbgPrint("NDIS6: regular OID completed through NdisMDirectOidRequestComplete\n");
        return;
    }

    Binding = Pending->Binding;
    if (Binding == NULL || Binding->DriverBlock == NULL)
        return;

    KeAcquireSpinLock(&Binding->PendingOidRequestsLock, &OldIrql);
    if (!Pending->Listed)
    {
        KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);
        return;
    }
    RemoveEntryList(&Pending->ListEntry);
    Pending->Listed = FALSE;
    KeReleaseSpinLock(&Binding->PendingOidRequestsLock, OldIrql);

    Ndis6ClearPendingOidContext(OidRequest);
    if (Binding->DriverBlock->Characteristics.DirectOidRequestCompleteHandler != NULL)
        Binding->DriverBlock->Characteristics.DirectOidRequestCompleteHandler(Binding->ProtocolBindingContext, OidRequest, Status);
    Ndis6DereferencePendingOid(Pending);
}

VOID
EXPORT
NdisMDirectOidRequestComplete(
    _In_ NDIS_HANDLE       NdisMiniportHandle,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS       Status)
{
    PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)NdisMiniportHandle;

    if (Adapter == NULL || !Adapter->IsNdis6 || OidRequest == NULL)
        return;

    if (Ndis6FilterCompleteDirectOidFromMiniport(Adapter, OidRequest, Status))
    {
        return;
    }

    Ndis6CompleteDirectOidRequestToOrigin(Adapter, OidRequest, Status);
}

/* ------------------------------------------------------------------ */
/*  Miniport-side datapath callbacks                                  */
/*  Implemented by the NDIS 5-to-6 bridge thunks in 60thunk_*.c.      */
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
#define NDIS6_CONTROL_NAME_TAG 'mSyN'

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
    USHORT                  SymbolicMaximumLength;

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
        SymbolicMaximumLength = Attrs->SymbolicName->Length + sizeof(WCHAR);
        CtlDev->SymbolicName.Buffer = ExAllocatePoolWithTag(NonPagedPool, SymbolicMaximumLength, NDIS6_CONTROL_NAME_TAG);
        if (CtlDev->SymbolicName.Buffer == NULL)
        {
            IoDeleteDevice(Device);
            return NDIS_STATUS_RESOURCES;
        }

        CtlDev->SymbolicName.Length = Attrs->SymbolicName->Length;
        CtlDev->SymbolicName.MaximumLength = SymbolicMaximumLength;
        if (Attrs->SymbolicName->Length != 0)
            RtlCopyMemory(CtlDev->SymbolicName.Buffer, Attrs->SymbolicName->Buffer, Attrs->SymbolicName->Length);
        CtlDev->SymbolicName.Buffer[Attrs->SymbolicName->Length / sizeof(WCHAR)] = UNICODE_NULL;
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
            ExFreePoolWithTag(CtlDev->SymbolicName.Buffer, NDIS6_CONTROL_NAME_TAG);
            RtlZeroMemory(&CtlDev->SymbolicName, sizeof(CtlDev->SymbolicName));
        }
    }

    ExInterlockedInsertTailList(&g_Ndis6CtlDevList, &CtlDev->ListEntry,
                                &g_Ndis6CtlDevLock);
    Device->Flags &= ~DO_DEVICE_INITIALIZING;

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
    if (CtlDev->SymbolicName.Buffer != NULL)
        ExFreePoolWithTag(CtlDev->SymbolicName.Buffer, NDIS6_CONTROL_NAME_TAG);
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
