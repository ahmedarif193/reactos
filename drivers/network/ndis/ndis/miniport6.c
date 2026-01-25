/*
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 * PROJECT:     ReactOS NDIS library
 * FILE:        ndis/miniport6.c
 * PURPOSE:     NDIS 6.x Miniport Driver Registration APIs
 * PROGRAMMERS: ReactOS Development Team
 * NOTES:       This file implements the NDIS 6.x miniport driver registration
 *              functions. Currently provides stub implementations that can be
 *              extended to provide full NDIS 6.x support.
 */

#include "ndissys.h"

#if NDIS_SUPPORT_NDIS6

/*
 * Internal structure to track NDIS 6.x miniport driver registrations
 */
typedef struct _NDIS6_MINIPORT_DRIVER_BLOCK {
    LIST_ENTRY ListEntry;
    PDRIVER_OBJECT DriverObject;
    UNICODE_STRING RegistryPath;
    NDIS_HANDLE MiniportDriverContext;
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics;
    ULONG Flags;
    /* Reference count for this driver block */
    LONG RefCount;
} NDIS6_MINIPORT_DRIVER_BLOCK, *PNDIS6_MINIPORT_DRIVER_BLOCK;

/* Forward declarations for NDIS 6.x PnP handlers */
static NTSTATUS NTAPI Ndis6AddDevice(PDRIVER_OBJECT DriverObject, PDEVICE_OBJECT PhysicalDeviceObject);
static NTSTATUS NTAPI Ndis6GenericIrpHandler(PDEVICE_OBJECT DeviceObject, PIRP Irp);
static NTSTATUS NTAPI Ndis6StartDeviceComplete(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context);

/* Forward declaration for protocol announcement */
static VOID Ndis6iAnnounceAdapterToProtocols(PDEVICE_OBJECT DeviceObject, PNDIS6_MINIPORT_DRIVER_BLOCK DriverBlock);

/* Forward declarations for NDIS 6.x state machine helpers */
NDIS_STATUS Ndis6iInitiateAdapterPause(_In_ NDIS_HANDLE MiniportAdapterHandle, _In_ ULONG PauseReason);
NDIS_STATUS Ndis6iInitiateAdapterRestart(_In_ NDIS_HANDLE MiniportAdapterHandle);
NDIS_STATUS Ndis6iWaitForPause(_In_ NDIS_HANDLE MiniportAdapterHandle, _In_opt_ PLARGE_INTEGER Timeout);
NDIS_STATUS Ndis6iWaitForRestart(_In_ NDIS_HANDLE MiniportAdapterHandle, _In_opt_ PLARGE_INTEGER Timeout);
NDIS_STATUS Ndis6iSetAdapterHalting(_In_ NDIS_HANDLE MiniportAdapterHandle);
VOID Ndis6iSetAdapterHalted(_In_ NDIS_HANDLE MiniportAdapterHandle);
NDIS_MINIPORT_ADAPTER_STATE Ndis6iGetAdapterState(_In_ NDIS_HANDLE MiniportAdapterHandle);

/* Unique ID for driver object extension */
#define NDIS6_DRIVER_EXTENSION_ID ((PVOID)'ND6I')

/* Define missing NDIS 6.x constants */
#ifndef NDIS_OBJECT_TYPE_MINIPORT_INIT_PARAMETERS
#define NDIS_OBJECT_TYPE_MINIPORT_INIT_PARAMETERS 0x81
#endif
#ifndef NDIS_MINIPORT_INIT_PARAMETERS_REVISION_1
#define NDIS_MINIPORT_INIT_PARAMETERS_REVISION_1 1
#endif

/* Global list of registered NDIS 6.x miniport drivers */
static LIST_ENTRY Ndis6MiniportDriverList;
static KSPIN_LOCK Ndis6MiniportDriverListLock;
static BOOLEAN Ndis6MiniportInitialized = FALSE;

/*
 * Send an IRP down synchronously and return completion status.
 */
static NTSTATUS
Ndis6iSendIrpSynchronously(
    _In_ PDEVICE_OBJECT NextDeviceObject,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, Ndis6StartDeviceComplete, &Event, TRUE, TRUE, TRUE);

    Status = IoCallDriver(NextDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }

    return Status;
}

/*
 * Halt the miniport (NDIS 6.x) if possible.
 */
static VOID
Ndis6iHaltAdapter(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PNDIS6_MINIPORT_DRIVER_BLOCK DriverBlock;
    PVOID MiniportAdapterContext;

    if (DeviceObject == NULL || DeviceObject->DeviceExtension == NULL)
    {
        return;
    }

    DriverBlock = (PNDIS6_MINIPORT_DRIVER_BLOCK)((PVOID*)DeviceObject->DeviceExtension)[0];
    MiniportAdapterContext = ((PVOID*)DeviceObject->DeviceExtension)[2];

    if (DriverBlock != NULL && DriverBlock->Characteristics.HaltHandlerEx != NULL)
    {
        MINIPORT_HALT HaltHandler = (MINIPORT_HALT)DriverBlock->Characteristics.HaltHandlerEx;

        if (HaltHandler == NULL)
        {
            return;
        }

        Ndis6iSetAdapterHalting(DeviceObject);
        HaltHandler(MiniportAdapterContext, HaltAction);
        Ndis6iSetAdapterHalted(DeviceObject);
    }
}

/*
 * Cleanup adapter resources after stop/remove.
 */
static VOID
Ndis6iCleanupAdapterResources(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN DeleteDeviceObject)
{
    PLOGICAL_ADAPTER Adapter;
    PDEVICE_OBJECT NextDeviceObject;
    PCM_RESOURCE_LIST ResourcesCopy;

    if (DeviceObject == NULL || DeviceObject->DeviceExtension == NULL)
    {
        return;
    }

    Adapter = (PLOGICAL_ADAPTER)((PVOID*)DeviceObject->DeviceExtension)[6];
    if (Adapter != NULL)
    {
        ExInterlockedRemoveEntryList(&Adapter->ListEntry, &AdapterListLock);

        if (Adapter->NdisMiniportBlock.MiniportName.Buffer != NULL)
        {
            ExFreePoolWithTag(Adapter->NdisMiniportBlock.MiniportName.Buffer, NDIS_TAG);
            Adapter->NdisMiniportBlock.MiniportName.Buffer = NULL;
        }

        ExFreePoolWithTag(Adapter, NDIS_TAG);
        ((PVOID*)DeviceObject->DeviceExtension)[6] = NULL;
    }

    ResourcesCopy = (PCM_RESOURCE_LIST)((PVOID*)DeviceObject->DeviceExtension)[5];
    if (ResourcesCopy != NULL)
    {
        ExFreePoolWithTag(ResourcesCopy, NDIS_TAG);
        ((PVOID*)DeviceObject->DeviceExtension)[5] = NULL;
    }

    if (DeleteDeviceObject)
    {
        NextDeviceObject = (PDEVICE_OBJECT)((PVOID*)DeviceObject->DeviceExtension)[3];
        if (NextDeviceObject != NULL)
        {
            IoDetachDevice(NextDeviceObject);
        }

        IoDeleteDevice(DeviceObject);
    }
}

static BOOLEAN
Ndis6iIsBindingInAdapterList(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PADAPTER_BINDING Binding)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    if (Adapter == NULL || Binding == NULL)
    {
        return FALSE;
    }

    KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
    for (Entry = Adapter->ProtocolListHead.Flink;
         Entry != &Adapter->ProtocolListHead;
         Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, ADAPTER_BINDING, AdapterListEntry) == Binding)
        {
            KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);
            return TRUE;
        }
    }
    KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);
    return FALSE;
}

static VOID
Ndis6iUnbindProtocols(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    PLOGICAL_ADAPTER Adapter;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    ULONG Count = 0;
    ULONG Index = 0;
    PADAPTER_BINDING *Bindings = NULL;

    if (DeviceObject == NULL || DeviceObject->DeviceExtension == NULL)
    {
        return;
    }

    Adapter = (PLOGICAL_ADAPTER)((PVOID*)DeviceObject->DeviceExtension)[6];
    if (Adapter == NULL)
    {
        return;
    }

    KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
    for (Entry = Adapter->ProtocolListHead.Flink;
         Entry != &Adapter->ProtocolListHead;
         Entry = Entry->Flink)
    {
        Count++;
    }
    KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);

    if (Count == 0)
    {
        return;
    }

    Bindings = ExAllocatePoolWithTag(NonPagedPool, sizeof(PADAPTER_BINDING) * Count, NDIS_TAG);
    if (Bindings == NULL)
    {
        DPRINT1("NDIS6: Failed to allocate binding array for unbind\n");
        return;
    }

    KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
    for (Entry = Adapter->ProtocolListHead.Flink;
         Entry != &Adapter->ProtocolListHead && Index < Count;
         Entry = Entry->Flink)
    {
        Bindings[Index++] = CONTAINING_RECORD(Entry, ADAPTER_BINDING, AdapterListEntry);
    }
    KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);

    for (Index = 0; Index < Count; Index++)
    {
        PADAPTER_BINDING Binding = Bindings[Index];
        PPROTOCOL_BINDING Protocol;
        NDIS_STATUS UnbindStatus = NDIS_STATUS_SUCCESS;

        if (Binding == NULL)
        {
            continue;
        }

        Protocol = Binding->ProtocolBinding;
        if (Protocol != NULL && Protocol->Chars.UnbindAdapterHandler != NULL)
        {
            Protocol->Chars.UnbindAdapterHandler(&UnbindStatus,
                                                 Binding->NdisOpenBlock.ProtocolBindingContext,
                                                 Binding);
        }
        else
        {
            UnbindStatus = NDIS_STATUS_SUCCESS;
        }

        if (UnbindStatus != NDIS_STATUS_PENDING)
        {
            if (Ndis6iIsBindingInAdapterList(Adapter, Binding))
            {
                NDIS_STATUS CloseStatus;
                NdisCloseAdapter(&CloseStatus, Binding);
            }
        }
    }

    /* Best-effort wait for unbind completion */
    {
        ULONG Retries = 20; /* ~2s total */
        LARGE_INTEGER Interval;
        Interval.QuadPart = -1000000LL; /* 100ms */

        while (Retries--)
        {
            BOOLEAN Empty;
            KIRQL WaitIrql;

            KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &WaitIrql);
            Empty = (Adapter->ProtocolListHead.Flink == &Adapter->ProtocolListHead);
            KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, WaitIrql);

            if (Empty)
            {
                break;
            }
            KeDelayExecutionThread(KernelMode, FALSE, &Interval);
        }
    }

    ExFreePoolWithTag(Bindings, NDIS_TAG);
}

/*
 * InitializeNdis6MiniportSupport
 * Internal function to initialize NDIS 6.x miniport support structures
 */
static VOID
InitializeNdis6MiniportSupport(VOID)
{
    if (!Ndis6MiniportInitialized)
    {
        InitializeListHead(&Ndis6MiniportDriverList);
        KeInitializeSpinLock(&Ndis6MiniportDriverListLock);
        Ndis6MiniportInitialized = TRUE;
    }
}

/*
 * ValidateMiniportDriverCharacteristics
 * Validates the NDIS_MINIPORT_DRIVER_CHARACTERISTICS structure
 */
static NDIS_STATUS
ValidateMiniportDriverCharacteristics(
    _In_ PNDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics)
{
    /* Validate header */
    if (Characteristics->Header.Type != NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS)
    {
        DPRINT1("Invalid object type: 0x%x\n",
            Characteristics->Header.Type);
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    /* Validate NDIS version */
    if (Characteristics->MajorNdisVersion < 6)
    {
        DPRINT1("Invalid NDIS version: %d.%d\n",
            Characteristics->MajorNdisVersion,
            Characteristics->MinorNdisVersion);
        return NDIS_STATUS_BAD_VERSION;
    }

    /* Validate required handlers for NDIS 6.x */
    if (Characteristics->InitializeHandlerEx == NULL)
    {
        DPRINT1("InitializeHandlerEx is required\n");
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    if (Characteristics->HaltHandlerEx == NULL)
    {
        DPRINT1("HaltHandlerEx is required\n");
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    if (Characteristics->PauseHandler == NULL)
    {
        DPRINT1("PauseHandler is required\n");
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    if (Characteristics->RestartHandler == NULL)
    {
        DPRINT1("RestartHandler is required\n");
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    if (Characteristics->OidRequestHandler == NULL)
    {
        DPRINT1("OidRequestHandler is required\n");
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    if (Characteristics->SendNetBufferListsHandler == NULL)
    {
        DPRINT1("SendNetBufferListsHandler is required\n");
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    if (Characteristics->ReturnNetBufferListsHandler == NULL)
    {
        DPRINT1("ReturnNetBufferListsHandler is required\n");
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    return NDIS_STATUS_SUCCESS;
}

/*
 * @implemented
 */
NDIS_STATUS
EXPORT
NdisRegisterMiniportDriver(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_opt_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_DRIVER_CHARACTERISTICS MiniportDriverCharacteristics,
    _Out_ PNDIS_HANDLE NdisMiniportDriverHandle)
{
    PNDIS6_MINIPORT_DRIVER_BLOCK DriverBlock;
    PNDIS6_MINIPORT_DRIVER_BLOCK *DriverBlockPtr;
    NDIS_STATUS Status;
    NTSTATUS NtStatus;
    KIRQL OldIrql;
    PWCHAR RegistryPathBuffer;
    ULONG RegistryPathLength;
    ULONG i;

    DPRINT("NdisRegisterMiniportDriver called\n");
    DPRINT1("NdisRegisterMiniportDriver - DriverObject=%p\n", DriverObject);

    /* Initialize NDIS 6.x support if needed */
    InitializeNdis6MiniportSupport();

    /* Validate parameters */
    if (DriverObject == NULL ||
        RegistryPath == NULL ||
        MiniportDriverCharacteristics == NULL ||
        NdisMiniportDriverHandle == NULL)
    {
        DPRINT1("Invalid parameter\n");
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    *NdisMiniportDriverHandle = NULL;

    /* Validate characteristics */
    Status = ValidateMiniportDriverCharacteristics(MiniportDriverCharacteristics);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DPRINT1("Invalid characteristics: 0x%x\n", Status);
        return Status;
    }

    /* Allocate driver block */
    DriverBlock = ExAllocatePoolWithTag(NonPagedPool,
                                        sizeof(NDIS6_MINIPORT_DRIVER_BLOCK),
                                        NDIS_TAG);
    if (DriverBlock == NULL)
    {
        DPRINT1("Failed to allocate driver block\n");
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(DriverBlock, sizeof(NDIS6_MINIPORT_DRIVER_BLOCK));

    /* Copy registry path */
    RegistryPathLength = RegistryPath->Length + sizeof(WCHAR);
    RegistryPathBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                               RegistryPathLength,
                                               NDIS_TAG);
    if (RegistryPathBuffer == NULL)
    {
        ExFreePoolWithTag(DriverBlock, NDIS_TAG);
        DPRINT1("Failed to allocate registry path\n");
        return NDIS_STATUS_RESOURCES;
    }

    RtlCopyMemory(RegistryPathBuffer, RegistryPath->Buffer, RegistryPath->Length);
    RegistryPathBuffer[RegistryPath->Length / sizeof(WCHAR)] = UNICODE_NULL;

    DriverBlock->RegistryPath.Buffer = RegistryPathBuffer;
    DriverBlock->RegistryPath.Length = RegistryPath->Length;
    DriverBlock->RegistryPath.MaximumLength = (USHORT)RegistryPathLength;

    /* Initialize driver block */
    DriverBlock->DriverObject = DriverObject;
    DriverBlock->MiniportDriverContext = MiniportDriverContext;
    DriverBlock->RefCount = 1;

    /* Copy characteristics */
    RtlCopyMemory(&DriverBlock->Characteristics,
                  MiniportDriverCharacteristics,
                  sizeof(NDIS_MINIPORT_DRIVER_CHARACTERISTICS));

    /* Set unload handler if provided */
    if (MiniportDriverCharacteristics->UnloadHandler != NULL)
    {
        /* Store the driver's unload handler - we'll call it from our unload routine */
        DriverBlock->Flags |= 0x1; /* Flag indicating custom unload handler */
    }

    /* Allocate driver object extension to store our driver block pointer */
    NtStatus = IoAllocateDriverObjectExtension(DriverObject,
                                               NDIS6_DRIVER_EXTENSION_ID,
                                               sizeof(PNDIS6_MINIPORT_DRIVER_BLOCK),
                                               (PVOID*)&DriverBlockPtr);
    if (!NT_SUCCESS(NtStatus))
    {
        DPRINT1("Failed to allocate driver object extension: 0x%x\n", NtStatus);
        ExFreePoolWithTag(DriverBlock->RegistryPath.Buffer, NDIS_TAG);
        ExFreePoolWithTag(DriverBlock, NDIS_TAG);
        return NDIS_STATUS_FAILURE;
    }

    *DriverBlockPtr = DriverBlock;

    /* Add to global list */
    KeAcquireSpinLock(&Ndis6MiniportDriverListLock, &OldIrql);
    InsertTailList(&Ndis6MiniportDriverList, &DriverBlock->ListEntry);
    KeReleaseSpinLock(&Ndis6MiniportDriverListLock, OldIrql);

    /* Hook the driver object's MajorFunction array */
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
    {
        DriverObject->MajorFunction[i] = Ndis6GenericIrpHandler;
    }

    /* Hook the AddDevice routine - this is critical for PnP to work */
    DriverObject->DriverExtension->AddDevice = Ndis6AddDevice;

    DPRINT1("Hooked AddDevice=%p, MajorFunction[0]=%p\n",
             DriverObject->DriverExtension->AddDevice,
             DriverObject->MajorFunction[0]);

    /* Call SetOptionsHandler if provided */
    if (MiniportDriverCharacteristics->SetOptionsHandler != NULL)
    {
        Status = MiniportDriverCharacteristics->SetOptionsHandler(
            (NDIS_HANDLE)DriverBlock,
            MiniportDriverContext);

        if (Status != NDIS_STATUS_SUCCESS)
        {
            DPRINT1("SetOptionsHandler failed: 0x%x\n", Status);
            /* Remove from list on failure */
            KeAcquireSpinLock(&Ndis6MiniportDriverListLock, &OldIrql);
            RemoveEntryList(&DriverBlock->ListEntry);
            KeReleaseSpinLock(&Ndis6MiniportDriverListLock, OldIrql);

            ExFreePoolWithTag(DriverBlock->RegistryPath.Buffer, NDIS_TAG);
            ExFreePoolWithTag(DriverBlock, NDIS_TAG);
            return Status;
        }
    }

    *NdisMiniportDriverHandle = (NDIS_HANDLE)DriverBlock;

    DPRINT1("NdisRegisterMiniportDriver completed successfully, handle=%p\n", DriverBlock);

    return NDIS_STATUS_SUCCESS;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisDeregisterMiniportDriver(
    _In_ NDIS_HANDLE NdisMiniportDriverHandle)
{
    PNDIS6_MINIPORT_DRIVER_BLOCK DriverBlock;
    KIRQL OldIrql;

    DPRINT("NdisDeregisterMiniportDriver called\n");

    if (NdisMiniportDriverHandle == NULL)
    {
        DPRINT1("Invalid handle\n");
        return;
    }

    DriverBlock = (PNDIS6_MINIPORT_DRIVER_BLOCK)NdisMiniportDriverHandle;

    /* Decrement reference count */
    if (InterlockedDecrement(&DriverBlock->RefCount) > 0)
    {
        /* Still in use, don't free yet */
        DPRINT1("Driver block still in use, RefCount=%ld\n",
            DriverBlock->RefCount);
        return;
    }

    /* Remove from global list */
    KeAcquireSpinLock(&Ndis6MiniportDriverListLock, &OldIrql);
    RemoveEntryList(&DriverBlock->ListEntry);
    KeReleaseSpinLock(&Ndis6MiniportDriverListLock, OldIrql);

    /* Call unload handler if registered */
    if ((DriverBlock->Flags & 0x1) &&
        DriverBlock->Characteristics.UnloadHandler != NULL)
    {
        DriverBlock->Characteristics.UnloadHandler(DriverBlock->DriverObject);
    }

    /* Free resources */
    if (DriverBlock->RegistryPath.Buffer != NULL)
    {
        ExFreePoolWithTag(DriverBlock->RegistryPath.Buffer, NDIS_TAG);
    }

    ExFreePoolWithTag(DriverBlock, NDIS_TAG);

    DPRINT1("NdisDeregisterMiniportDriver completed\n");
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMIndicateReceiveNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG NumberOfNetBufferLists,
    _In_ ULONG ReceiveFlags)
{
    PDEVICE_OBJECT DeviceObject = (PDEVICE_OBJECT)MiniportAdapterHandle;
    PLOGICAL_ADAPTER Adapter;
    PLIST_ENTRY CurrentEntry;
    PADAPTER_BINDING AdapterBinding;
    PNET_BUFFER_LIST CurrentNbl;
    PNET_BUFFER CurrentNb;
    KIRQL OldIrql;
    BOOLEAN AtDispatchLevel;
    BOOLEAN ResourcesFlag;
    PMDL FirstMdl;
    PUCHAR PacketData;
    ULONG PacketDataLength;
    ULONG TotalPacketLength;
    ULONG HeaderSize;
    PUCHAR LookaheadBuffer;
    ULONG LookaheadSize;
    ULONG NblCount;

    /* Initialize OldIrql to avoid compiler warning - it's only used when !AtDispatchLevel */
    OldIrql = PASSIVE_LEVEL;

    DPRINT("NdisMIndicateReceiveNetBufferLists - Handle=%p, NBLs=%p, Count=%lu\n",
           MiniportAdapterHandle, NetBufferLists, NumberOfNetBufferLists);

    /* Validate parameters */
    if (MiniportAdapterHandle == NULL || NetBufferLists == NULL || NumberOfNetBufferLists == 0)
    {
        DPRINT1("NdisMIndicateReceiveNetBufferLists - invalid parameters\n");
        return;
    }

    /* Get the LOGICAL_ADAPTER from device extension */
    if (DeviceObject->DeviceExtension == NULL)
    {
        DPRINT1("NdisMIndicateReceiveNetBufferLists - no device extension\n");
        return;
    }

    Adapter = (PLOGICAL_ADAPTER)((PVOID*)DeviceObject->DeviceExtension)[6];
    if (Adapter == NULL)
    {
        DPRINT1("NdisMIndicateReceiveNetBufferLists - no LOGICAL_ADAPTER\n");
        return;
    }

    /* Check if NDIS_RECEIVE_FLAGS_RESOURCES is set - miniport retains ownership */
    ResourcesFlag = (ReceiveFlags & NDIS_RECEIVE_FLAGS_RESOURCES) != 0;

    /* Check if we're at DISPATCH_LEVEL */
    AtDispatchLevel = (ReceiveFlags & NDIS_RECEIVE_FLAGS_DISPATCH_LEVEL) != 0;

    /* Acquire adapter lock */
    if (!AtDispatchLevel)
    {
        KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
    }
    else
    {
        KeAcquireSpinLockAtDpcLevel(&Adapter->NdisMiniportBlock.Lock);
    }

    /* Set the source handle on each NBL for the return path */
    NblCount = 0;
    for (CurrentNbl = NetBufferLists; CurrentNbl != NULL; CurrentNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl))
    {
        /* Store the adapter as source handle so we know where to return it */
        CurrentNbl->SourceHandle = (NDIS_HANDLE)Adapter;
        NblCount++;
    }

    /* Check if any protocols are bound */
    if (Adapter->ProtocolListHead.Flink == &Adapter->ProtocolListHead)
    {
        DPRINT1("No protocols bound to adapter\n");
        goto done;
    }

    /*
     * For each NBL, extract the packet data and indicate to each bound protocol.
     * We use the legacy ReceiveHandler which takes header + lookahead buffers.
     * This is simpler than ReceivePacketHandler which requires NDIS_PACKET allocation.
     */
    for (CurrentNbl = NetBufferLists; CurrentNbl != NULL; CurrentNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl))
    {
        CurrentNb = NET_BUFFER_LIST_FIRST_NB(CurrentNbl);
        if (CurrentNb == NULL)
        {
            DPRINT1("NBL %p has no NET_BUFFER\n", CurrentNbl);
            continue;
        }

        /* Get the MDL chain from the NET_BUFFER */
        FirstMdl = NET_BUFFER_CURRENT_MDL(CurrentNb);
        if (FirstMdl == NULL)
        {
            DPRINT1("NB %p has no MDL\n", CurrentNb);
            continue;
        }

        /* Get virtual address of packet data */
        PacketData = MmGetSystemAddressForMdlSafe(FirstMdl, NormalPagePriority);
        if (PacketData == NULL)
        {
            DPRINT1("Cannot map MDL %p\n", FirstMdl);
            continue;
        }

        /* Adjust for CurrentMdlOffset in the NET_BUFFER */
        PacketData += NET_BUFFER_CURRENT_MDL_OFFSET(CurrentNb);

        /* Get packet length */
        PacketDataLength = MmGetMdlByteCount(FirstMdl) - NET_BUFFER_CURRENT_MDL_OFFSET(CurrentNb);
        TotalPacketLength = (ULONG)NET_BUFFER_DATA_LENGTH(CurrentNb);

        /* Use adapter's medium header size (typically 14 for Ethernet) */
        HeaderSize = Adapter->MediumHeaderSize;
        if (HeaderSize == 0)
        {
            HeaderSize = 14;  /* Default Ethernet header size */
        }

        if (PacketDataLength < HeaderSize)
        {
            DPRINT("Packet too small: %lu < %lu\n", PacketDataLength, HeaderSize);
            continue;
        }

        /* Print packet header for debugging - only in debug builds */
#ifdef DBG
        if (PacketDataLength >= 14)
        {
            DPRINT("Packet %lu bytes: %02X:%02X:%02X:%02X:%02X:%02X <- %02X:%02X:%02X:%02X:%02X:%02X (type=%02X%02X)\n",
                     TotalPacketLength,
                     PacketData[0], PacketData[1], PacketData[2], PacketData[3], PacketData[4], PacketData[5],
                     PacketData[6], PacketData[7], PacketData[8], PacketData[9], PacketData[10], PacketData[11],
                     PacketData[12], PacketData[13]);
        }
#endif

        /* Calculate lookahead - the data after the header */
        LookaheadBuffer = PacketData + HeaderSize;
        LookaheadSize = TotalPacketLength - HeaderSize;

        /* Limit lookahead to what's available in the first MDL */
        if (LookaheadSize > PacketDataLength - HeaderSize)
        {
            LookaheadSize = PacketDataLength - HeaderSize;
        }

        DPRINT("Header=%p (%u bytes), Lookahead=%p (%u bytes), Total=%u\n",
                 PacketData, HeaderSize, LookaheadBuffer, LookaheadSize, TotalPacketLength);

        /* Indicate to each bound protocol */
        CurrentEntry = Adapter->ProtocolListHead.Flink;
        while (CurrentEntry != &Adapter->ProtocolListHead)
        {
            AdapterBinding = CONTAINING_RECORD(CurrentEntry, ADAPTER_BINDING, AdapterListEntry);

            if (AdapterBinding->ProtocolBinding != NULL &&
                AdapterBinding->ProtocolBinding->Chars.ReceiveHandler != NULL)
            {
                DPRINT("Indicating to protocol '%wZ'\n",
                       &AdapterBinding->ProtocolBinding->Chars.Name);

                /*
                 * Call the legacy ReceiveHandler.
                 * Parameters:
                 *   - ProtocolBindingContext: context for this binding
                 *   - MacReceiveContext: used by NdisTransferData (we pass the NBL)
                 *   - HeaderBuffer: pointer to the MAC header
                 *   - HeaderBufferSize: size of the header (14 for Ethernet)
                 *   - LookaheadBuffer: pointer to data after header
                 *   - LookaheadBufferSize: how much data is available
                 *   - PacketSize: total size of data after header
                 *
                 * Note: We must release the lock before calling the handler to prevent
                 * deadlock if the protocol calls back into NDIS.
                 */
                if (!AtDispatchLevel)
                {
                    KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);
                }
                else
                {
                    KeReleaseSpinLockFromDpcLevel(&Adapter->NdisMiniportBlock.Lock);
                }

                {
                    NDIS_STATUS RcvStatus;

                    RcvStatus = (*AdapterBinding->ProtocolBinding->Chars.ReceiveHandler)(
                        AdapterBinding->NdisOpenBlock.ProtocolBindingContext,
                        (NDIS_HANDLE)CurrentNbl,  /* MacReceiveContext */
                        PacketData,               /* HeaderBuffer */
                        HeaderSize,               /* HeaderBufferSize */
                        LookaheadBuffer,          /* LookaheadBuffer */
                        LookaheadSize,            /* LookaheadBufferSize */
                        TotalPacketLength - HeaderSize);  /* PacketSize */

                    DPRINT("ReceiveHandler returned Status=0x%x\n", RcvStatus);
                }

                /* Re-acquire the lock */
                if (!AtDispatchLevel)
                {
                    KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
                }
                else
                {
                    KeAcquireSpinLockAtDpcLevel(&Adapter->NdisMiniportBlock.Lock);
                }
            }

            CurrentEntry = CurrentEntry->Flink;
        }
    }

done:
    /* Release adapter lock */
    if (!AtDispatchLevel)
    {
        KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);
    }
    else
    {
        KeReleaseSpinLockFromDpcLevel(&Adapter->NdisMiniportBlock.Lock);
    }

    /*
     * If NDIS_RECEIVE_FLAGS_RESOURCES is set, the miniport retains ownership
     * of the NBLs and they should not be returned via NdisReturnNetBufferLists.
     * The miniport will reuse them immediately after this call returns.
     */
    if (ResourcesFlag)
    {
        DPRINT1("Resources flag set - miniport retains NBL ownership\n");
    }
    else
    {
        /*
         * For non-resources mode, we need to return the NBLs to the miniport.
         * In the original NDIS design, protocols would hold onto packets and return
         * them later. Since we're indicating synchronously with the legacy
         * ReceiveHandler (which copies data), we can return them immediately.
         *
         * Call the miniport's ReturnNetBufferListsHandler.
         */
        PNDIS6_MINIPORT_DRIVER_BLOCK DriverBlock;
        PVOID MiniportAdapterContext;
        ULONG ReturnFlags;

        DriverBlock = (PNDIS6_MINIPORT_DRIVER_BLOCK)((PVOID*)DeviceObject->DeviceExtension)[0];
        MiniportAdapterContext = ((PVOID*)DeviceObject->DeviceExtension)[2];
        ReturnFlags = AtDispatchLevel ? NDIS_RETURN_FLAGS_DISPATCH_LEVEL : 0;

        if (DriverBlock != NULL && DriverBlock->Characteristics.ReturnNetBufferListsHandler != NULL)
        {
            DPRINT("Returning %lu NBLs to miniport\n", NblCount);

            DriverBlock->Characteristics.ReturnNetBufferListsHandler(
                MiniportAdapterContext,
                NetBufferLists,
                ReturnFlags);

            DPRINT("ReturnNetBufferListsHandler returned\n");
        }
        else
        {
            DPRINT1("No ReturnNetBufferListsHandler, NBLs not returned\n");
        }
    }

    DPRINT("NdisMIndicateReceiveNetBufferLists completed\n");
}

/*
 * Context for tracking NDIS 5.x to 6.x send conversion
 * This must match the structure in protocol.c
 */
typedef struct _NDIS6_SEND_CONTEXT_MINIPORT6 {
    PNDIS_PACKET OriginalPacket;
    PADAPTER_BINDING AdapterBinding;
    PLOGICAL_ADAPTER Adapter;
} NDIS6_SEND_CONTEXT_MINIPORT6, *PNDIS6_SEND_CONTEXT_MINIPORT6;

/*
 * @implemented
 */
VOID
EXPORT
NdisMSendNetBufferListsComplete(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG SendCompleteFlags)
{
    PDEVICE_OBJECT DeviceObject = (PDEVICE_OBJECT)MiniportAdapterHandle;
    PLOGICAL_ADAPTER Adapter;
    PNET_BUFFER_LIST CurrentNbl;
    PNET_BUFFER_LIST NextNbl;
    PADAPTER_BINDING AdapterBinding;
    KIRQL OldIrql;
    BOOLEAN AtDispatchLevel;
    ULONG CompletedCount = 0;
    PNDIS6_SEND_CONTEXT_MINIPORT6 SendContext;
    PNDIS_PACKET OriginalPacket;
    NDIS_STATUS NdisStatus;

    DPRINT("NdisMSendNetBufferListsComplete - Handle=%p, NBLs=%p, Flags=0x%x\n",
           MiniportAdapterHandle, NetBufferLists, SendCompleteFlags);

    /* Validate parameters */
    if (MiniportAdapterHandle == NULL || NetBufferLists == NULL)
    {
        DPRINT1("NdisMSendNetBufferListsComplete - invalid parameters\n");
        return;
    }

    /* Get the LOGICAL_ADAPTER from device extension */
    if (DeviceObject->DeviceExtension == NULL)
    {
        DPRINT1("NdisMSendNetBufferListsComplete - no device extension\n");
        return;
    }

    Adapter = (PLOGICAL_ADAPTER)((PVOID*)DeviceObject->DeviceExtension)[6];
    if (Adapter == NULL)
    {
        DPRINT1("NdisMSendNetBufferListsComplete - no LOGICAL_ADAPTER\n");
        return;
    }

    /* Check if we're at DISPATCH_LEVEL */
    AtDispatchLevel = (SendCompleteFlags & NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL) != 0;

    /* If not already at DISPATCH_LEVEL, raise to it for consistency */
    if (!AtDispatchLevel)
    {
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    }

    /*
     * Walk the NET_BUFFER_LIST chain and complete each one back to its source.
     *
     * For sends converted from NDIS_PACKET by ProSendPacketToNdis6Miniport,
     * we stored the context in ProtocolReserved[0].
     */
    for (CurrentNbl = NetBufferLists; CurrentNbl != NULL; CurrentNbl = NextNbl)
    {
        /* Get next before we potentially free this one */
        NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);

        /* Clear the next pointer to avoid confusion */
        CurrentNbl->Next = NULL;

        CompletedCount++;

        /* Convert NBL status to NDIS status */
        NdisStatus = CurrentNbl->Status;

        DPRINT("Completing NBL %p, Status=0x%x, SourceHandle=%p\n",
               CurrentNbl, NdisStatus, CurrentNbl->SourceHandle);

        /*
         * Check if this NBL has a send context from NDIS 5.x packet conversion.
         * The context is stored in ProtocolReserved[0].
         */
        SendContext = *(PNDIS6_SEND_CONTEXT_MINIPORT6*)&CurrentNbl->ProtocolReserved[0];

        if (SendContext != NULL && SendContext->OriginalPacket != NULL)
        {
            /* This is a converted NDIS 5.x packet - complete it */
            OriginalPacket = SendContext->OriginalPacket;
            AdapterBinding = SendContext->AdapterBinding;

            DPRINT("Completing converted packet %p, AdapterBinding=%p\n",
                     OriginalPacket, AdapterBinding);

            if (AdapterBinding != NULL &&
                AdapterBinding->ProtocolBinding != NULL &&
                AdapterBinding->ProtocolBinding->Chars.SendCompleteHandler != NULL)
            {
                DPRINT("Calling SendCompleteHandler for protocol '%wZ'\n",
                       &AdapterBinding->ProtocolBinding->Chars.Name);

                /* Call the protocol's SendCompleteHandler */
                (*AdapterBinding->ProtocolBinding->Chars.SendCompleteHandler)(
                    AdapterBinding->NdisOpenBlock.ProtocolBindingContext,
                    OriginalPacket,
                    NdisStatus);

                DPRINT("SendCompleteHandler returned\n");
            }
            else
            {
                DPRINT("No SendCompleteHandler for packet %p\n", OriginalPacket);
            }

            /*
             * Free the NBL structure.
             * Note: We allocated NBL + NB + SendContext as one block in protocol.c.
             * The MdlChain (NDIS_BUFFER) belongs to the original packet and is NOT freed here.
             */
            ExFreePoolWithTag(CurrentNbl, 'lbNS');  /* SNBL - Send NBL, must match protocol.c */

            DPRINT("Freed NBL %p\n", CurrentNbl);
        }
        else
        {
            /* This is a native NDIS 6.x NBL - try SourceHandle */
            AdapterBinding = (PADAPTER_BINDING)CurrentNbl->SourceHandle;

            if (AdapterBinding != NULL &&
                AdapterBinding->ProtocolBinding != NULL &&
                AdapterBinding->ProtocolBinding->Chars.SendCompleteHandler != NULL)
            {
                DPRINT1("NBL %p has no context but has valid binding, cannot complete\n",
                         CurrentNbl);
            }
            else
            {
                DPRINT1("NBL %p has no valid context or binding for completion\n",
                         CurrentNbl);
            }
        }
    }

    /* Lower IRQL if we raised it */
    if (!AtDispatchLevel)
    {
        KeLowerIrql(OldIrql);
    }

    DPRINT("NdisMSendNetBufferListsComplete completed %lu NBLs\n", CompletedCount);
}

/*
 * @implemented
 */
VOID
EXPORT
NdisReturnNetBufferLists(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
/*
 * FUNCTION: Returns NET_BUFFER_LISTs to the miniport after protocol processing
 *
 * This function is called by protocols to return NBLs that were indicated
 * via NdisMIndicateReceiveNetBufferLists. The NBLs are returned to the
 * miniport's ReturnNetBufferListsHandler.
 *
 * Parameters:
 *   NdisBindingHandle - Handle to the protocol's binding (ADAPTER_BINDING)
 *   NetBufferLists - Chain of NET_BUFFER_LISTs to return
 *   ReturnFlags - NDIS_RETURN_FLAGS_DISPATCH_LEVEL if at DISPATCH_LEVEL
 */
{
    PLOGICAL_ADAPTER Adapter;
    PNDIS6_MINIPORT_DRIVER_BLOCK DriverBlock;
    PDEVICE_OBJECT DeviceObject;
    PNET_BUFFER_LIST CurrentNbl;
    PVOID MiniportAdapterContext;
    KIRQL OldIrql;
    BOOLEAN AtDispatchLevel;
    ULONG ReturnedCount = 0;

    DPRINT1("NdisReturnNetBufferLists - Handle=%p, NBLs=%p, Flags=0x%x\n",
             NdisBindingHandle, NetBufferLists, ReturnFlags);

    /* Validate parameters */
    if (NdisBindingHandle == NULL || NetBufferLists == NULL)
    {
        DPRINT1("NdisReturnNetBufferLists - invalid parameters\n");
        return;
    }

    /* Check if we're at DISPATCH_LEVEL */
    AtDispatchLevel = (ReturnFlags & NDIS_RETURN_FLAGS_DISPATCH_LEVEL) != 0;

    /*
     * The binding handle could be:
     * 1. An ADAPTER_BINDING (from NdisOpenAdapterEx)
     * 2. A filter binding (for filter drivers)
     *
     * For now, we treat it as the handle to find the adapter.
     * The SourceHandle in the NBL tells us which adapter to return to.
     */

    /* Walk the NBL chain and return each to its source adapter */
    for (CurrentNbl = NetBufferLists; CurrentNbl != NULL; )
    {
        PNET_BUFFER_LIST NextNbl = NET_BUFFER_LIST_NEXT_NBL(CurrentNbl);

        /* The SourceHandle was set by NdisMIndicateReceiveNetBufferLists */
        Adapter = (PLOGICAL_ADAPTER)CurrentNbl->SourceHandle;

        if (Adapter == NULL)
        {
            DPRINT1("NBL %p has no SourceHandle, cannot return\n", CurrentNbl);
            CurrentNbl = NextNbl;
            continue;
        }

        /* Get the device object and driver block */
        DeviceObject = Adapter->NdisMiniportBlock.DeviceObject;
        if (DeviceObject == NULL || DeviceObject->DeviceExtension == NULL)
        {
            DPRINT1("Cannot find device for adapter %p\n", Adapter);
            CurrentNbl = NextNbl;
            continue;
        }

        DriverBlock = (PNDIS6_MINIPORT_DRIVER_BLOCK)((PVOID*)DeviceObject->DeviceExtension)[0];
        MiniportAdapterContext = ((PVOID*)DeviceObject->DeviceExtension)[2];

        if (DriverBlock == NULL ||
            DriverBlock->Characteristics.ReturnNetBufferListsHandler == NULL)
        {
            DPRINT1("No ReturnNetBufferListsHandler for adapter %p\n", Adapter);
            CurrentNbl = NextNbl;
            continue;
        }

        /* Clear the Next pointer since we're returning one at a time */
        CurrentNbl->Next = NULL;

        /* Call the miniport's ReturnNetBufferListsHandler */
        if (!AtDispatchLevel)
        {
            KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
        }

        DPRINT1("Calling ReturnNetBufferListsHandler for NBL %p\n", CurrentNbl);

        DriverBlock->Characteristics.ReturnNetBufferListsHandler(
            MiniportAdapterContext,
            CurrentNbl,
            ReturnFlags);

        if (!AtDispatchLevel)
        {
            KeLowerIrql(OldIrql);
        }

        ReturnedCount++;
        CurrentNbl = NextNbl;
    }

    DPRINT1("NdisReturnNetBufferLists returned %lu NBLs\n", ReturnedCount);
}

/*
 * Ndis6AddDevice - AddDevice handler for NDIS 6.x miniport drivers
 *
 * This is called by PnP when a new device is found that matches our driver.
 * We create the device object and set up the adapter context.
 */
static NTSTATUS
NTAPI
Ndis6AddDevice(
    PDRIVER_OBJECT DriverObject,
    PDEVICE_OBJECT PhysicalDeviceObject)
{
    PNDIS6_MINIPORT_DRIVER_BLOCK *DriverBlockPtr;
    PNDIS6_MINIPORT_DRIVER_BLOCK DriverBlock;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT NextDeviceObject;
    NTSTATUS Status;
    static ULONG AdapterIndex = 0;
    WCHAR DeviceNameBuffer[64];
    UNICODE_STRING DeviceName;

    DPRINT("AddDevice called - DriverObject=%p, PDO=%p\n",
             DriverObject, PhysicalDeviceObject);

    /* Get our driver block from the driver object extension */
    DriverBlockPtr = IoGetDriverObjectExtension(DriverObject, NDIS6_DRIVER_EXTENSION_ID);
    if (DriverBlockPtr == NULL)
    {
        DPRINT1("AddDevice - Can't get driver object extension\n");
        return STATUS_UNSUCCESSFUL;
    }
    DriverBlock = *DriverBlockPtr;

    /* Create device name using simple format */
    {
        ULONG Index = AdapterIndex++;
        WCHAR *p = DeviceNameBuffer;
        WCHAR Hex[] = L"0123456789ABCDEF";
        INT i;

        /* Copy "\\Device\\NDIS6_" prefix */
        RtlCopyMemory(p, L"\\Device\\NDIS6_", 14 * sizeof(WCHAR));
        p += 14;

        /* Add 8-digit hex number */
        for (i = 7; i >= 0; i--)
        {
            p[i] = Hex[Index & 0xF];
            Index >>= 4;
        }
        p[8] = L'\0';
    }
    RtlInitUnicodeString(&DeviceName, DeviceNameBuffer);

    DPRINT1("Creating device %wZ\n", &DeviceName);

    /* Create the device object - use a simple device extension for now */
    Status = IoCreateDevice(DriverObject,
                            sizeof(PVOID) * 8,  /* Simple extension to store adapter info */
                            &DeviceName,
                            FILE_DEVICE_PHYSICAL_NETCARD,
                            0,
                            FALSE,
                            &DeviceObject);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("IoCreateDevice failed: 0x%x\n", Status);
        return Status;
    }

    /* Store the driver block in the device extension */
    ((PVOID*)DeviceObject->DeviceExtension)[0] = DriverBlock;
    ((PVOID*)DeviceObject->DeviceExtension)[1] = PhysicalDeviceObject;
    ((PVOID*)DeviceObject->DeviceExtension)[2] = NULL;  /* Adapter context - set during init */

    /* Attach to the device stack */
    NextDeviceObject = IoAttachDeviceToDeviceStack(DeviceObject, PhysicalDeviceObject);
    if (NextDeviceObject == NULL)
    {
        DPRINT1("IoAttachDeviceToDeviceStack failed\n");
        IoDeleteDevice(DeviceObject);
        return STATUS_UNSUCCESSFUL;
    }

    ((PVOID*)DeviceObject->DeviceExtension)[3] = NextDeviceObject;

    /* Clear the initializing flag */
    DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    DeviceObject->Flags |= DO_BUFFERED_IO;

    DPRINT1("AddDevice complete - DeviceObject=%p, NextDevice=%p\n",
             DeviceObject, NextDeviceObject);

    return STATUS_SUCCESS;
}

/*
 * Ndis6HandleStartDevice - Handle IRP_MN_START_DEVICE
 *
 * This is where we actually initialize the miniport by calling InitializeHandlerEx.
 */
/*
 * Completion routine for START_DEVICE IRP
 */
static NTSTATUS
NTAPI
Ndis6StartDeviceComplete(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp,
    PVOID Context)
{
    PKEVENT Event = (PKEVENT)Context;
    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    if (Event)
    {
        KeSetEvent(Event, IO_NO_INCREMENT, FALSE);
    }

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS
Ndis6HandleStartDevice(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PNDIS6_MINIPORT_DRIVER_BLOCK DriverBlock;
    PDEVICE_OBJECT NextDeviceObject;
    PIO_STACK_LOCATION IrpSp;
    NDIS_STATUS NdisStatus;
    NDIS_MINIPORT_INIT_PARAMETERS InitParams;
    MINIPORT_INITIALIZE InitHandler;
    PCM_RESOURCE_LIST AllocatedResources;
    PCM_RESOURCE_LIST AllocatedResourcesTranslated;
    KEVENT Event;
    NTSTATUS Status;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    DriverBlock = ((PVOID*)DeviceObject->DeviceExtension)[0];
    NextDeviceObject = ((PVOID*)DeviceObject->DeviceExtension)[3];

    DPRINT1("START_DEVICE - calling InitializeHandlerEx\n");

    /* Save the resources BEFORE passing the IRP down */
    AllocatedResources = IrpSp->Parameters.StartDevice.AllocatedResources;
    AllocatedResourcesTranslated = IrpSp->Parameters.StartDevice.AllocatedResourcesTranslated;

    DPRINT1("Resources=%p, ResourcesTranslated=%p\n",
             AllocatedResources, AllocatedResourcesTranslated);

    if (AllocatedResourcesTranslated)
    {
        DPRINT1("ResourcesTranslated Count=%u\n", AllocatedResourcesTranslated->Count);
        if (AllocatedResourcesTranslated->Count > 0)
        {
            ULONG i;
            PCM_PARTIAL_RESOURCE_LIST PartialList = &AllocatedResourcesTranslated->List[0].PartialResourceList;
            DPRINT1("PartialResourceList Count=%u\n", PartialList->Count);
            for (i = 0; i < PartialList->Count && i < 10; i++)
            {
                PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc = &PartialList->PartialDescriptors[i];
                DPRINT1("  Resource[%u]: Type=%u\n", i, Desc->Type);
            }
        }
    }

    /* First, pass the IRP down to the lower driver and wait for completion */
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, Ndis6StartDeviceComplete, &Event, TRUE, TRUE, TRUE);

    Status = IoCallDriver(NextDeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("Lower driver START_DEVICE failed: 0x%x\n", Status);
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    /* Now call the miniport's initialize handler */
    RtlZeroMemory(&InitParams, sizeof(InitParams));
    InitParams.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INIT_PARAMETERS;
    InitParams.Header.Revision = NDIS_MINIPORT_INIT_PARAMETERS_REVISION_1;
    InitParams.Header.Size = sizeof(NDIS_MINIPORT_INIT_PARAMETERS);

    /* Set the allocated resources */
    InitParams.AllocatedResources = NULL;
    if (AllocatedResourcesTranslated && AllocatedResourcesTranslated->Count > 0)
    {
        /* Point to the partial resource list as NDIS_RESOURCE_LIST */
        InitParams.AllocatedResources = (PNDIS_RESOURCE_LIST)&AllocatedResourcesTranslated->List[0].PartialResourceList;
    }

    DPRINT1("Calling InitializeHandlerEx, Resources=%p\n", InitParams.AllocatedResources);

    /*
     * Store the allocated resources in the device extension for later use by
     * NdisMRegisterInterruptEx. We need to make a copy because the IRP resources
     * may be freed after we return.
     *
     * Device extension layout for NDIS 6.x:
     *   [0] = DriverBlock pointer
     *   [1] = PhysicalDeviceObject (PDO)
     *   [2] = Adapter context
     *   [3] = NextDeviceObject
     *   [4] = DMA adapter (set by NdisMRegisterScatterGatherDma)
     *   [5] = AllocatedResourcesTranslated (copy)
     */
    if (AllocatedResourcesTranslated && AllocatedResourcesTranslated->Count > 0)
    {
        /* Calculate size of the resource list */
        ULONG ResourceListSize = sizeof(CM_RESOURCE_LIST) +
            (AllocatedResourcesTranslated->List[0].PartialResourceList.Count - 1) *
            sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);

        PCM_RESOURCE_LIST ResourcesCopy = ExAllocatePoolWithTag(NonPagedPool,
                                                                 ResourceListSize,
                                                                 NDIS_TAG);
        if (ResourcesCopy != NULL)
        {
            RtlCopyMemory(ResourcesCopy, AllocatedResourcesTranslated, ResourceListSize);
            ((PVOID*)DeviceObject->DeviceExtension)[5] = ResourcesCopy;
            DPRINT1("Stored resources copy at DeviceExtension[5]: %p\n", ResourcesCopy);
        }
    }

    /* Cast and call the initialize handler */
    InitHandler = (MINIPORT_INITIALIZE)DriverBlock->Characteristics.InitializeHandlerEx;
    if (InitHandler != NULL)
    {
        NdisStatus = InitHandler(
                        DeviceObject,  /* Use DeviceObject as NdisMiniportHandle for now */
                        DriverBlock->MiniportDriverContext,
                        &InitParams);

        DPRINT1("InitializeHandlerEx returned 0x%x\n", NdisStatus);

        if (NdisStatus != NDIS_STATUS_SUCCESS)
        {
            Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_UNSUCCESSFUL;
        }

        /* Transition adapter to Running state */
        Ndis6iSetAdapterRunning(DeviceObject);
        DPRINT1("Adapter transitioned to Running state\n");

        /* Announce adapter to protocols */
        Ndis6iAnnounceAdapterToProtocols(DeviceObject, DriverBlock);
    }

    /* Complete the IRP */
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

/*
 * Ndis6GenericIrpHandler - Generic IRP handler for NDIS 6.x devices
 */
static NTSTATUS
NTAPI
Ndis6GenericIrpHandler(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PDEVICE_OBJECT NextDeviceObject;
    NTSTATUS Status;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    /* Get next device in stack */
    if (DeviceObject->DeviceExtension)
    {
        NextDeviceObject = ((PVOID*)DeviceObject->DeviceExtension)[3];
    }
    else
    {
        NextDeviceObject = NULL;
    }

    DPRINT1("IRP MajorFunction=0x%x MinorFunction=0x%x\n",
             IrpSp->MajorFunction, IrpSp->MinorFunction);

    switch (IrpSp->MajorFunction)
    {
        case IRP_MJ_PNP:
            switch (IrpSp->MinorFunction)
            {
                case IRP_MN_START_DEVICE:
                    DPRINT1("NDIS6: IRP_MN_START_DEVICE\n");
                    return Ndis6HandleStartDevice(DeviceObject, Irp);

                case IRP_MN_QUERY_STOP_DEVICE:
                case IRP_MN_QUERY_REMOVE_DEVICE:
                {
                    NDIS_STATUS PauseStatus;
                    LARGE_INTEGER Timeout;
                    NDIS_MINIPORT_ADAPTER_STATE State;

                    DPRINT1("NDIS6: IRP_MN_QUERY_STOP/REMOVE\n");

                    State = Ndis6iGetAdapterState(DeviceObject);
                    if (State == NdisMiniportAdapterStatePaused)
                    {
                        PauseStatus = NDIS_STATUS_SUCCESS;
                    }
                    else
                    {
                        PauseStatus = Ndis6iInitiateAdapterPause(DeviceObject, 0);
                    }

                    if (PauseStatus == NDIS_STATUS_PENDING)
                    {
                        Timeout.QuadPart = -300000000LL; /* 30 seconds */
                        PauseStatus = Ndis6iWaitForPause(DeviceObject, &Timeout);
                    }

                    if (PauseStatus != NDIS_STATUS_SUCCESS)
                    {
                        DPRINT1("NDIS6: Pause failed for QUERY_STOP/REMOVE: 0x%x\n", PauseStatus);
                        Irp->IoStatus.Status = STATUS_DEVICE_BUSY;
                        IoCompleteRequest(Irp, IO_NO_INCREMENT);
                        return STATUS_DEVICE_BUSY;
                    }

                    if (NextDeviceObject != NULL)
                    {
                        IoSkipCurrentIrpStackLocation(Irp);
                        return IoCallDriver(NextDeviceObject, Irp);
                    }

                    Irp->IoStatus.Status = STATUS_SUCCESS;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return STATUS_SUCCESS;
                }

                case IRP_MN_CANCEL_STOP_DEVICE:
                case IRP_MN_CANCEL_REMOVE_DEVICE:
                {
                    NDIS_STATUS RestartStatus;
                    LARGE_INTEGER Timeout;
                    NDIS_MINIPORT_ADAPTER_STATE State;

                    DPRINT1("NDIS6: IRP_MN_CANCEL_STOP/REMOVE\n");

                    State = Ndis6iGetAdapterState(DeviceObject);
                    if (State == NdisMiniportAdapterStateRunning)
                    {
                        RestartStatus = NDIS_STATUS_SUCCESS;
                    }
                    else
                    {
                        RestartStatus = Ndis6iInitiateAdapterRestart(DeviceObject);
                    }

                    if (RestartStatus == NDIS_STATUS_PENDING)
                    {
                        Timeout.QuadPart = -300000000LL; /* 30 seconds */
                        RestartStatus = Ndis6iWaitForRestart(DeviceObject, &Timeout);
                    }

                    if (RestartStatus != NDIS_STATUS_SUCCESS)
                    {
                        DPRINT1("NDIS6: Restart failed for CANCEL_STOP/REMOVE: 0x%x\n", RestartStatus);
                        Irp->IoStatus.Status = STATUS_UNSUCCESSFUL;
                        IoCompleteRequest(Irp, IO_NO_INCREMENT);
                        return STATUS_UNSUCCESSFUL;
                    }

                    if (NextDeviceObject != NULL)
                    {
                        IoSkipCurrentIrpStackLocation(Irp);
                        return IoCallDriver(NextDeviceObject, Irp);
                    }

                    Irp->IoStatus.Status = STATUS_SUCCESS;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return STATUS_SUCCESS;
                }

                case IRP_MN_STOP_DEVICE:
                {
                    NTSTATUS StopStatus = STATUS_SUCCESS;
                    NDIS_STATUS PauseStatus;
                    LARGE_INTEGER Timeout;
                    NDIS_MINIPORT_ADAPTER_STATE State;

                    DPRINT1("NDIS6: IRP_MN_STOP_DEVICE\n");

                    State = Ndis6iGetAdapterState(DeviceObject);
                    if (State == NdisMiniportAdapterStatePaused)
                    {
                        PauseStatus = NDIS_STATUS_SUCCESS;
                    }
                    else
                    {
                        PauseStatus = Ndis6iInitiateAdapterPause(DeviceObject, 0);
                    }

                    if (PauseStatus == NDIS_STATUS_PENDING)
                    {
                        Timeout.QuadPart = -300000000LL; /* 30 seconds */
                        PauseStatus = Ndis6iWaitForPause(DeviceObject, &Timeout);
                    }

                    if (NextDeviceObject != NULL)
                    {
                        StopStatus = Ndis6iSendIrpSynchronously(NextDeviceObject, Irp);
                    }

                    Ndis6iCancelOidRequestsForMiniport(DeviceObject);
                    Ndis6iUnbindProtocols(DeviceObject);
                    Ndis6iHaltAdapter(DeviceObject, NdisHaltDeviceStopped);
                    Ndis6iCleanupAdapterResources(DeviceObject, FALSE);

                    Irp->IoStatus.Status = StopStatus;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return StopStatus;
                }

                case IRP_MN_SURPRISE_REMOVAL:
                    DPRINT1("NDIS6: IRP_MN_SURPRISE_REMOVAL\n");
                    Ndis6iCancelOidRequestsForMiniport(DeviceObject);
                    Ndis6iUnbindProtocols(DeviceObject);
                    Ndis6iHaltAdapter(DeviceObject, NdisHaltDeviceSurpriseRemoved);
                    Ndis6iCleanupAdapterResources(DeviceObject, FALSE);
                    break;

                case IRP_MN_REMOVE_DEVICE:
                {
                    NTSTATUS RemoveStatus = STATUS_SUCCESS;
                    NDIS_STATUS PauseStatus;
                    LARGE_INTEGER Timeout;
                    NDIS_MINIPORT_ADAPTER_STATE State;

                    DPRINT1("NDIS6: IRP_MN_REMOVE_DEVICE\n");

                    if (NextDeviceObject != NULL)
                    {
                        RemoveStatus = Ndis6iSendIrpSynchronously(NextDeviceObject, Irp);
                    }

                    State = Ndis6iGetAdapterState(DeviceObject);
                    if (State != NdisMiniportAdapterStatePaused &&
                        State != NdisMiniportAdapterStateHalted)
                    {
                        PauseStatus = Ndis6iInitiateAdapterPause(DeviceObject, 0);
                        if (PauseStatus == NDIS_STATUS_PENDING)
                        {
                            Timeout.QuadPart = -300000000LL; /* 30 seconds */
                            PauseStatus = Ndis6iWaitForPause(DeviceObject, &Timeout);
                        }
                    }

                    Ndis6iCancelOidRequestsForMiniport(DeviceObject);
                    Ndis6iUnbindProtocols(DeviceObject);
                    Ndis6iHaltAdapter(DeviceObject, NdisHaltDeviceInstanceDeInitialized);
                    Ndis6iCleanupAdapterResources(DeviceObject, TRUE);

                    Irp->IoStatus.Status = RemoveStatus;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return RemoveStatus;
                }

                default:
                    DPRINT1("NDIS6: PnP minor function 0x%x\n", IrpSp->MinorFunction);
                    break;
            }
            break;

        case IRP_MJ_POWER:
            DPRINT1("NDIS6: IRP_MJ_POWER\n");
            break;
    }

    /* Pass the IRP down to the next driver */
    if (NextDeviceObject != NULL)
    {
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(NextDeviceObject, Irp);
    }

    /* No next device, complete the IRP */
    Status = Irp->IoStatus.Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

/*
 * Ndis6iGetNetCfgInstanceId - Read NetCfgInstanceId from registry
 *
 * Opens the device's software registry key and reads the NetCfgInstanceId value
 * which is set by the network class installer (netcfgx.dll).
 *
 * Returns STATUS_SUCCESS and fills AdapterNameBuffer with "\\Device\\{GUID}"
 * or returns error status on failure.
 */
static NTSTATUS
Ndis6iGetNetCfgInstanceId(
    PDEVICE_OBJECT PhysicalDeviceObject,
    PWCHAR AdapterNameBuffer,
    ULONG BufferLength)
{
    HANDLE KeyHandle = NULL;
    NTSTATUS Status;
    UNICODE_STRING ValueName;
    PKEY_VALUE_PARTIAL_INFORMATION ValueInfo;
    ULONG ValueLength;
    UCHAR ValueBuffer[256];
    PWCHAR GuidString;
    ULONG GuidLength;

    /* Open the device's software registry key (DIREG_DRV) */
    Status = IoOpenDeviceRegistryKey(PhysicalDeviceObject,
                                     PLUGPLAY_REGKEY_DRIVER,
                                     KEY_READ,
                                     &KeyHandle);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NDIS6: IoOpenDeviceRegistryKey failed: 0x%x\n", Status);
        return Status;
    }

    /* Query NetCfgInstanceId value */
    RtlInitUnicodeString(&ValueName, L"NetCfgInstanceId");
    ValueInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ValueBuffer;
    ValueLength = sizeof(ValueBuffer);

    Status = ZwQueryValueKey(KeyHandle,
                             &ValueName,
                             KeyValuePartialInformation,
                             ValueInfo,
                             ValueLength,
                             &ValueLength);

    ZwClose(KeyHandle);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("NDIS6: ZwQueryValueKey(NetCfgInstanceId) failed: 0x%x\n", Status);
        return Status;
    }

    if (ValueInfo->Type != REG_SZ)
    {
        DPRINT1("NDIS6: NetCfgInstanceId is not REG_SZ (type=%lu)\n", ValueInfo->Type);
        return STATUS_INVALID_PARAMETER;
    }

    /* The value is the GUID in {XXXXXXXX-XXXX-XXXX-XXXX-XXXXXXXXXXXX} format */
    GuidString = (PWCHAR)ValueInfo->Data;
    GuidLength = ValueInfo->DataLength / sizeof(WCHAR);

    DPRINT("NDIS6: NetCfgInstanceId = %S\n", GuidString);

    /* Build device name: \\Device\\{GUID} */
    if (BufferLength < (8 + GuidLength) * sizeof(WCHAR))
    {
        DPRINT1("NDIS6: Buffer too small for adapter name\n");
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlCopyMemory(AdapterNameBuffer, L"\\Device\\", 8 * sizeof(WCHAR));
    RtlCopyMemory(AdapterNameBuffer + 8, GuidString, GuidLength * sizeof(WCHAR));
    /* Ensure null termination */
    AdapterNameBuffer[8 + GuidLength - 1] = L'\0';

    DPRINT1("NDIS6: Adapter device name = %S\n", AdapterNameBuffer);

    return STATUS_SUCCESS;
}

/*
 * Ndis6iAnnounceAdapterToProtocols - Announce new adapter to protocol drivers
 *
 * This function creates a LOGICAL_ADAPTER structure for the NDIS 6.x adapter
 * and adds it to the global AdapterListHead so protocols can bind to it.
 */
static VOID
Ndis6iAnnounceAdapterToProtocols(
    PDEVICE_OBJECT DeviceObject,
    PNDIS6_MINIPORT_DRIVER_BLOCK DriverBlock)
{
    PLOGICAL_ADAPTER Adapter;
    PVOID AdapterContext;
    PDEVICE_OBJECT PhysicalDeviceObject;
    static ULONG Ndis6AdapterIndex = 0;
    WCHAR AdapterNameBuffer[128];
    ULONG Index;
    INT i;
    WCHAR Hex[] = L"0123456789ABCDEF";
    WCHAR *p;
    PLIST_ENTRY CurrentEntry;
    PPROTOCOL_BINDING Protocol;
    NDIS_STATUS BindStatus;
    NTSTATUS Status;
    BOOLEAN GotGuidName = FALSE;

    UNREFERENCED_PARAMETER(GotGuidName);

    DPRINT1("NDIS6: Announcing adapter to protocols - DeviceObject=%p\n", DeviceObject);

    /* Get adapter context from device extension */
    AdapterContext = ((PVOID*)DeviceObject->DeviceExtension)[2];
    PhysicalDeviceObject = ((PVOID*)DeviceObject->DeviceExtension)[1];

    DPRINT1("NDIS6: Adapter context = %p\n", AdapterContext);

    /* Try to get the NetCfgInstanceId (GUID) from registry */
    Status = Ndis6iGetNetCfgInstanceId(PhysicalDeviceObject, AdapterNameBuffer, sizeof(AdapterNameBuffer));
    if (NT_SUCCESS(Status))
    {
        GotGuidName = TRUE;
        DPRINT1("NDIS6: Using GUID-based adapter name from registry\n");
    }
    else
    {
        /* Fall back to generating a name */
        DPRINT1("NDIS6: Failed to get NetCfgInstanceId (0x%x), generating name\n", Status);
        Index = InterlockedIncrement((LONG*)&Ndis6AdapterIndex) - 1;
        p = AdapterNameBuffer;
        RtlCopyMemory(p, L"\\Device\\NDIS6_", 14 * sizeof(WCHAR));
        p += 14;
        for (i = 7; i >= 0; i--)
        {
            p[i] = Hex[Index & 0xF];
            Index >>= 4;
        }
        p[8] = L'\0';
    }

    /* Allocate a LOGICAL_ADAPTER structure for this NDIS 6.x adapter */
    Adapter = ExAllocatePoolWithTag(NonPagedPool, sizeof(LOGICAL_ADAPTER), NDIS_TAG);
    if (Adapter == NULL)
    {
        DPRINT1("NDIS6: Failed to allocate LOGICAL_ADAPTER\n");
        return;
    }

    RtlZeroMemory(Adapter, sizeof(LOGICAL_ADAPTER));

    /* Allocate and copy the adapter name */
    Adapter->NdisMiniportBlock.MiniportName.Length = (USHORT)(wcslen(AdapterNameBuffer) * sizeof(WCHAR));
    Adapter->NdisMiniportBlock.MiniportName.MaximumLength = Adapter->NdisMiniportBlock.MiniportName.Length + sizeof(WCHAR);
    Adapter->NdisMiniportBlock.MiniportName.Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                                                            Adapter->NdisMiniportBlock.MiniportName.MaximumLength,
                                                                            NDIS_TAG);
    if (Adapter->NdisMiniportBlock.MiniportName.Buffer != NULL)
    {
        RtlCopyMemory(Adapter->NdisMiniportBlock.MiniportName.Buffer, AdapterNameBuffer,
                      Adapter->NdisMiniportBlock.MiniportName.MaximumLength);
    }

    DPRINT1("NDIS6: Created adapter %wZ\n", &Adapter->NdisMiniportBlock.MiniportName);

    /* Initialize adapter fields */
    Adapter->NdisMiniportBlock.MediaType = NdisMedium802_3;  /* Ethernet */
    Adapter->NdisMiniportBlock.PhysicalMediumType = NdisPhysicalMediumUnspecified;
    Adapter->NdisMiniportBlock.MiniportAdapterContext = AdapterContext;
    Adapter->NdisMiniportBlock.PhysicalDeviceObject = PhysicalDeviceObject;
    Adapter->NdisMiniportBlock.DeviceObject = DeviceObject;
    Adapter->MediumHeaderSize = 14;  /* Ethernet header size */
    Adapter->AddressLength = 6;      /* MAC address length */

    /* Initialize the protocol list for this adapter */
    InitializeListHead(&Adapter->ProtocolListHead);
    KeInitializeSpinLock(&Adapter->NdisMiniportBlock.Lock);

    /* Store reference to the NDIS 6.x device object in the miniport block */
    /* Use DriverHandle to store our driver block for later reference */
    Adapter->NdisMiniportBlock.DriverHandle = (PNDIS_M_DRIVER_BLOCK)DriverBlock;

    /* Store the adapter pointer in device extension for later use */
    ((PVOID*)DeviceObject->DeviceExtension)[6] = Adapter;

    /* Add adapter to the global adapter list */
    DPRINT1("NDIS6: Adding adapter to AdapterListHead\n");
    ExInterlockedInsertTailList(&AdapterListHead, &Adapter->ListEntry, &AdapterListLock);

    /*
     * Notify all registered protocols about the new adapter
     * by calling their BindAdapterHandler
     */
    DPRINT1("NDIS6: Refreshing protocol bindings\n");

    CurrentEntry = ProtocolListHead.Flink;
    while (CurrentEntry != &ProtocolListHead)
    {
        Protocol = CONTAINING_RECORD(CurrentEntry, PROTOCOL_BINDING, ListEntry);

        DPRINT1("NDIS6: Protocol=%p, CurrentEntry=%p\n", Protocol, CurrentEntry);
        DPRINT1("NDIS6: PROTOCOL_BINDING size=%u, Chars offset=%u\n",
                 (ULONG)sizeof(PROTOCOL_BINDING),
                 (ULONG)FIELD_OFFSET(PROTOCOL_BINDING, Chars));
        DPRINT1("NDIS6: NDIS_PROTOCOL_CHARACTERISTICS size=%u\n",
                 (ULONG)sizeof(NDIS_PROTOCOL_CHARACTERISTICS));
        DPRINT1("NDIS6: BindAdapterHandler offset in Chars=%u\n",
                 (ULONG)FIELD_OFFSET(NDIS_PROTOCOL_CHARACTERISTICS, BindAdapterHandler));
        DPRINT1("NDIS6: MajorNdisVersion=%u, MinorNdisVersion=%u\n",
                 Protocol->Chars.MajorNdisVersion, Protocol->Chars.MinorNdisVersion);
        DPRINT1("NDIS6: BindAdapterHandler=%p\n", Protocol->Chars.BindAdapterHandler);
        DPRINT1("NDIS6: Checking protocol '%wZ' for binding\n", &Protocol->Chars.Name);

        if (Protocol->Chars.BindAdapterHandler != NULL)
        {
            UNICODE_STRING RegistryPath;
            WCHAR RegistryPathBuffer[256];
            ULONG_PTR HandlerAddr = (ULONG_PTR)Protocol->Chars.BindAdapterHandler;

            /* Validate that the handler looks like a valid kernel address */
#ifdef _M_AMD64
            if (HandlerAddr < 0xFFFF800000000000ULL)
#else
            if (HandlerAddr < 0x80000000UL)
#endif
            {
                DPRINT1("NDIS6: ERROR - Invalid BindAdapterHandler %p (not a kernel address!)\n",
                         Protocol->Chars.BindAdapterHandler);
                DPRINT1("NDIS6: Protocol->Chars raw bytes at BindAdapterHandler offset:\n");
                {
                    PUCHAR RawBytes = (PUCHAR)&Protocol->Chars.BindAdapterHandler;
                    DPRINT1("NDIS6:   %02x %02x %02x %02x %02x %02x %02x %02x\n",
                             RawBytes[0], RawBytes[1], RawBytes[2], RawBytes[3],
                             RawBytes[4], RawBytes[5], RawBytes[6], RawBytes[7]);
                }
                CurrentEntry = CurrentEntry->Flink;
                continue;
            }

            /* Build a registry path for protocol parameters */
            /* Format: \Registry\Machine\System\CurrentControlSet\Services\<DeviceName>\Parameters\<Protocol> */
            RtlZeroMemory(RegistryPathBuffer, sizeof(RegistryPathBuffer));
            wcscpy(RegistryPathBuffer, L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\");
            wcscat(RegistryPathBuffer, AdapterNameBuffer + 8);  /* Skip \\Device\\ */
            wcscat(RegistryPathBuffer, L"\\Parameters\\");
            /* Append protocol name safely */
            if (Protocol->Chars.Name.Length < 100 * sizeof(WCHAR))
            {
                wcsncat(RegistryPathBuffer, Protocol->Chars.Name.Buffer,
                        Protocol->Chars.Name.Length / sizeof(WCHAR));
            }
            RtlInitUnicodeString(&RegistryPath, RegistryPathBuffer);

            DPRINT1("NDIS6: Calling BindAdapterHandler for '%wZ' with device '%wZ'\n",
                     &Protocol->Chars.Name, &Adapter->NdisMiniportBlock.MiniportName);

            /* Call the protocol's BindAdapterHandler */
            Protocol->Chars.BindAdapterHandler(
                &BindStatus,
                NULL,  /* BindContext */
                &Adapter->NdisMiniportBlock.MiniportName,
                &RegistryPath,
                0);    /* SystemSpecific1 */

            DPRINT1("NDIS6: BindAdapterHandler returned 0x%x\n", BindStatus);
        }
        else
        {
            DPRINT1("NDIS6: Protocol has no BindAdapterHandler\n");
        }

        CurrentEntry = CurrentEntry->Flink;
    }

    DPRINT1("NDIS6: Adapter announcement complete\n");
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMIndicateStatusEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ PNDIS_STATUS_INDICATION StatusIndication)
{
    PDEVICE_OBJECT DeviceObject = (PDEVICE_OBJECT)MiniportAdapterHandle;
    PLOGICAL_ADAPTER Adapter;
    PLIST_ENTRY CurrentEntry;
    PADAPTER_BINDING AdapterBinding;
    KIRQL OldIrql;
    NDIS_STATUS LegacyStatus;
    PVOID LegacyStatusBuffer;
    UINT LegacyStatusBufferSize;

    DPRINT1("NDIS6: NdisMIndicateStatusEx - Handle=%p, StatusCode=0x%x\n",
             MiniportAdapterHandle, StatusIndication->StatusCode);

    /* Validate parameters */
    if (MiniportAdapterHandle == NULL || StatusIndication == NULL)
    {
        DPRINT1("NDIS6: NdisMIndicateStatusEx - invalid parameters\n");
        return;
    }

    /* Get the LOGICAL_ADAPTER from device extension */
    if (DeviceObject->DeviceExtension == NULL)
    {
        DPRINT1("NDIS6: NdisMIndicateStatusEx - no device extension\n");
        return;
    }

    Adapter = (PLOGICAL_ADAPTER)((PVOID*)DeviceObject->DeviceExtension)[6];
    if (Adapter == NULL)
    {
        /*
         * This can happen during initialization when the miniport calls
         * NdisMIndicateStatusEx before we've created the LOGICAL_ADAPTER.
         * This is expected - silently return since there are no protocols
         * bound yet anyway.
         */
        DPRINT("NDIS6: NdisMIndicateStatusEx - no LOGICAL_ADAPTER yet (during init), ignoring status 0x%x\n",
                 StatusIndication->StatusCode);
        return;
    }

    /* Log the status indication for debugging */
    switch (StatusIndication->StatusCode)
    {
        case NDIS_STATUS_LINK_STATE:
            if (StatusIndication->StatusBufferSize >= sizeof(NDIS_LINK_STATE))
            {
                PNDIS_LINK_STATE LinkState = (PNDIS_LINK_STATE)StatusIndication->StatusBuffer;
                DPRINT1("NDIS6: Link state change - MediaConnectState=%d, XmitSpeed=%llu, RcvSpeed=%llu\n",
                         LinkState->MediaConnectState,
                         (unsigned long long)LinkState->XmitLinkSpeed,
                         (unsigned long long)LinkState->RcvLinkSpeed);

                /*
                 * Convert NDIS 6.x link state to legacy status for NDIS 5.x protocols
                 */
                if (LinkState->MediaConnectState == MediaConnectStateConnected)
                {
                    LegacyStatus = NDIS_STATUS_MEDIA_CONNECT;
                }
                else if (LinkState->MediaConnectState == MediaConnectStateDisconnected)
                {
                    LegacyStatus = NDIS_STATUS_MEDIA_DISCONNECT;
                }
                else
                {
                    LegacyStatus = StatusIndication->StatusCode;
                }
            }
            else
            {
                LegacyStatus = StatusIndication->StatusCode;
            }
            break;

        case NDIS_STATUS_MEDIA_CONNECT:
            DPRINT1("NDIS6: Media connected\n");
            LegacyStatus = NDIS_STATUS_MEDIA_CONNECT;
            break;

        case NDIS_STATUS_MEDIA_DISCONNECT:
            DPRINT1("NDIS6: Media disconnected\n");
            LegacyStatus = NDIS_STATUS_MEDIA_DISCONNECT;
            break;

        case NDIS_STATUS_OPER_STATUS:
            DPRINT1("NDIS6: Operational status change\n");
            LegacyStatus = StatusIndication->StatusCode;
            break;

        default:
            DPRINT1("NDIS6: Status indication 0x%x (size=%lu)\n",
                     StatusIndication->StatusCode,
                     StatusIndication->StatusBufferSize);
            LegacyStatus = StatusIndication->StatusCode;
            break;
    }

    /* Prepare legacy status buffer */
    LegacyStatusBuffer = StatusIndication->StatusBuffer;
    LegacyStatusBufferSize = StatusIndication->StatusBufferSize;

    /* Acquire adapter lock */
    KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);

    /* Iterate through bound protocols and call their StatusHandler */
    CurrentEntry = Adapter->ProtocolListHead.Flink;

    if (CurrentEntry == &Adapter->ProtocolListHead)
    {
        DPRINT1("NDIS6: No protocols bound to adapter for status indication\n");
    }

    while (CurrentEntry != &Adapter->ProtocolListHead)
    {
        AdapterBinding = CONTAINING_RECORD(CurrentEntry, ADAPTER_BINDING, AdapterListEntry);

        if (AdapterBinding->ProtocolBinding != NULL)
        {
            DPRINT1("NDIS6: Indicating status 0x%x to protocol '%wZ'\n",
                     LegacyStatus, &AdapterBinding->ProtocolBinding->Chars.Name);

            /*
             * Call the legacy StatusHandler for NDIS 5.x protocols.
             * The StatusHandler expects:
             * - ProtocolBindingContext
             * - GeneralStatus (NDIS_STATUS)
             * - StatusBuffer
             * - StatusBufferSize
             */
            if (AdapterBinding->ProtocolBinding->Chars.StatusHandler != NULL)
            {
                DPRINT1("NDIS6: Calling protocol StatusHandler\n");

                /* Release lock before calling protocol handler */
                KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);

                (*AdapterBinding->ProtocolBinding->Chars.StatusHandler)(
                    AdapterBinding->NdisOpenBlock.ProtocolBindingContext,
                    LegacyStatus,
                    LegacyStatusBuffer,
                    LegacyStatusBufferSize);

                /* Reacquire lock */
                KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);

                /*
                 * Also call StatusCompleteHandler if present.
                 * This is required for proper NDIS 5.x protocol semantics.
                 */
                if (AdapterBinding->ProtocolBinding->Chars.StatusCompleteHandler != NULL)
                {
                    KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);

                    (*AdapterBinding->ProtocolBinding->Chars.StatusCompleteHandler)(
                        AdapterBinding->NdisOpenBlock.ProtocolBindingContext);

                    KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
                }
            }
            else
            {
                DPRINT1("NDIS6: Protocol has no StatusHandler\n");
            }
        }

        CurrentEntry = CurrentEntry->Flink;
    }

    /* Release adapter lock */
    KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);

    DPRINT1("NDIS6: NdisMIndicateStatusEx completed\n");
}

#endif /* NDIS_SUPPORT_NDIS6 */

/* EOF */
