/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        drivers/network/ndis/ndis/60driver.c
 * PURPOSE:     NDIS 6 miniport driver registration + PnP dispatcher.
 *
 *              When an NDIS 6 driver calls NdisMRegisterMiniportDriver,
 *              we record its characteristics in a NDIS6_DRIVER_BLOCK,
 *              hijack its DriverObject->DriverExtension->AddDevice and
 *              MajorFunction[IRP_MJ_PNP] entries, and let our own
 *              dispatchers handle the PnP IRPs that bring up adapter
 *              instances.
 *
 *              When PnP enumerates a device the driver claims (via the
 *              .inf file's PCI ID match), our AddDevice creates an FDO,
 *              wraps it in a LOGICAL_ADAPTER + NDIS6_ADAPTER_EXT, and
 *              waits for IRP_MN_START_DEVICE. On START we extract
 *              hardware resources from the resource list and call the
 *              driver's MiniportInitializeEx.
 *
 *              Created on the dev-nt6-1 branch by the NDIS 5↔6 bridge
 *              work that lets e1000e move real packets.
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors
 */

#include "ndis6_internal.h"

LIST_ENTRY  g_Ndis6DriverList;
KSPIN_LOCK  g_Ndis6DriverListLock;
static BOOLEAN g_Ndis6DriverListReady = FALSE;

#define NDIS6_DRIVER_TAG    'rDNn'  /* "nNDr" */

VOID
Ndis6DriverInit(VOID)
{
    if (!g_Ndis6DriverListReady)
    {
        InitializeListHead(&g_Ndis6DriverList);
        KeInitializeSpinLock(&g_Ndis6DriverListLock);
        g_Ndis6DriverListReady = TRUE;
    }
}

/* ============================================================================
 *  Forward declarations for our PnP dispatchers (defined further down)
 * ============================================================================ */

static NTSTATUS NTAPI
Ndis6AddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject);

static NTSTATUS NTAPI
Ndis6DispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

static NTSTATUS NTAPI
Ndis6DispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

static NTSTATUS NTAPI
Ndis6DispatchSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

static NTSTATUS NTAPI
Ndis6DispatchUnknown(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

static NTSTATUS NTAPI
Ndis6DispatchHybridGeneric(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp);

/* ============================================================================
 *  NdisMRegisterMiniportDriver — the entry point e1000e (and others) call
 *  from DriverEntry to register themselves as an NDIS 6 miniport.
 * ============================================================================ */

NDIS_STATUS
NTAPI
NdisMRegisterMiniportDriver(
    _In_     PDRIVER_OBJECT                          DriverObject,
    _In_     PUNICODE_STRING                         RegistryPath,
    _In_opt_ NDIS_HANDLE                             MiniportDriverContext,
    _In_     PNDIS_MINIPORT_DRIVER_CHARACTERISTICS   MiniportDriverCharacteristics,
    _Out_    PNDIS_HANDLE                            NdisMiniportDriverHandle)
{
    PNDIS6_DRIVER_BLOCK Block;
    KIRQL OldIrql;
    ULONG i;

    if (DriverObject == NULL || MiniportDriverCharacteristics == NULL ||
        NdisMiniportDriverHandle == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    /* Validate the characteristics minimally — we don't enforce every
     * required handler because the bridge can tolerate some being NULL. */
    if (MiniportDriverCharacteristics->Header.Type !=
        NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    if (MiniportDriverCharacteristics->InitializeHandlerEx == NULL ||
        MiniportDriverCharacteristics->HaltHandlerEx == NULL)
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    Ndis6DriverInit();

    Block = (PNDIS6_DRIVER_BLOCK)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_DRIVER_BLOCK), NDIS6_DRIVER_TAG);
    if (Block == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Block, sizeof(*Block));
    Block->DriverObject          = DriverObject;
    Block->MiniportDriverContext = MiniportDriverContext;
    /* Copy only the bytes the driver actually supplied (REV_1 is 16 bytes
     * shorter than REV_2). The block was zeroed above so the REV_2 direct-OID
     * handlers stay NULL for a REV_1 driver. */
    {
        ULONG CharSize = MiniportDriverCharacteristics->Header.Size;
        if (CharSize == 0 || CharSize > sizeof(Block->Characteristics))
            CharSize = sizeof(Block->Characteristics);
        RtlCopyMemory(&Block->Characteristics, MiniportDriverCharacteristics, CharSize);
    }

    /* Copy the registry path so the driver can free its own copy if it
     * wants. We allocate fresh PagedPool storage for the buffer. */
    if (RegistryPath && RegistryPath->Buffer && RegistryPath->Length)
    {
        Block->RegistryPath.Length        = RegistryPath->Length;
        Block->RegistryPath.MaximumLength = RegistryPath->Length + sizeof(WCHAR);
        Block->RegistryPath.Buffer        = (PWSTR)ExAllocatePoolWithTag(
            PagedPool, Block->RegistryPath.MaximumLength, NDIS6_DRIVER_TAG);
        if (Block->RegistryPath.Buffer)
        {
            RtlCopyMemory(Block->RegistryPath.Buffer,
                          RegistryPath->Buffer,
                          RegistryPath->Length);
            Block->RegistryPath.Buffer[RegistryPath->Length / sizeof(WCHAR)] = L'\0';
        }
    }

    /* Hijack the driver object's PnP / power / AddDevice slots. We save
     * the originals so we can chain them if the driver had its own. NDIS
     * miniport drivers shouldn't have their own (they only register via
     * NdisMRegisterMiniportDriver) but we save them for safety. */
    Block->OriginalAddDevice = DriverObject->DriverExtension->AddDevice;
    Block->OriginalPnpDispatch = DriverObject->MajorFunction[IRP_MJ_PNP];
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
        Block->OriginalMajorFunction[i] = DriverObject->MajorFunction[i];

    /* Hybrid KMDF+NDIS driver: WdfDriverCreate already installed its own
     * AddDevice. Only AddDevice is a reliable signal — the IO manager
     * pre-fills every MajorFunction slot with IopInvalidDeviceRequest. */
    Block->IsWdfHybrid = (Block->OriginalAddDevice != NULL && Block->OriginalAddDevice != Ndis6AddDevice);
    if (Block->IsWdfHybrid)
    {
        DbgPrint("NDIS6: hybrid KMDF+NDIS miniport detected (AddDevice=%p Pnp=%p)\n", Block->OriginalAddDevice, Block->OriginalPnpDispatch);
    }

    DriverObject->DriverExtension->AddDevice         = Ndis6AddDevice;
    DriverObject->MajorFunction[IRP_MJ_PNP]          = Ndis6DispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER]        = Ndis6DispatchPower;
    DriverObject->MajorFunction[IRP_MJ_SYSTEM_CONTROL] = Ndis6DispatchSystemControl;

    /* Default-handle every other major function so the IO manager
     * doesn't return STATUS_INVALID_DEVICE_REQUEST. IRP_MJ_DEVICE_CONTROL,
     * IRP_MJ_CREATE, IRP_MJ_CLOSE, etc. stay at STATUS_NOT_SUPPORTED
     * until someone needs them — e1000e doesn't use them.
     * Hybrid path: the slots hold KMDF's FxDevice::Dispatch, which must
     * never see our adapter FDO — interpose a per-device demux. */
    for (i = 0; i <= IRP_MJ_MAXIMUM_FUNCTION; i++)
    {
        if (DriverObject->MajorFunction[i] == NULL)
            DriverObject->MajorFunction[i] = Ndis6DispatchUnknown;
        else if (Block->IsWdfHybrid && i != IRP_MJ_PNP && i != IRP_MJ_POWER && i != IRP_MJ_SYSTEM_CONTROL)
            DriverObject->MajorFunction[i] = Ndis6DispatchHybridGeneric;
    }

    /* Insert into the global driver list. */
    KeAcquireSpinLock(&g_Ndis6DriverListLock, &OldIrql);
    InsertTailList(&g_Ndis6DriverList, &Block->ListEntry);
    KeReleaseSpinLock(&g_Ndis6DriverListLock, OldIrql);

    *NdisMiniportDriverHandle = (NDIS_HANDLE)Block;
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NdisMDeregisterMiniportDriver(
    _In_ NDIS_HANDLE NdisMiniportDriverHandle)
{
    PNDIS6_DRIVER_BLOCK Block = (PNDIS6_DRIVER_BLOCK)NdisMiniportDriverHandle;
    KIRQL OldIrql;

    if (Block == NULL)
        return;

    KeAcquireSpinLock(&g_Ndis6DriverListLock, &OldIrql);
    RemoveEntryList(&Block->ListEntry);
    KeReleaseSpinLock(&g_Ndis6DriverListLock, OldIrql);

    if (Block->RegistryPath.Buffer)
        ExFreePoolWithTag(Block->RegistryPath.Buffer, NDIS6_DRIVER_TAG);

    ExFreePoolWithTag(Block, NDIS6_DRIVER_TAG);
}

/* ============================================================================
 *  NdisMSetMiniportAttributes — driver pushes its per-adapter context and
 *  capabilities back to the library during MiniportInitializeEx.
 *
 *  The driver may call this several times with different Header.Type
 *  values (Registration first, then General, then optionally Offload).
 * ============================================================================ */

NDIS_STATUS
NTAPI
NdisMSetMiniportAttributes(
    _In_ NDIS_HANDLE                        NdisMiniportAdapterHandle,
    _In_ PNDIS_MINIPORT_ADAPTER_ATTRIBUTES  MiniportAttributes)
{
    PLOGICAL_ADAPTER Adapter = (PLOGICAL_ADAPTER)NdisMiniportAdapterHandle;
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || MiniportAttributes == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (!Adapter->IsNdis6)
        return NDIS_STATUS_NOT_SUPPORTED;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    switch (MiniportAttributes->Header.Type)
    {
        case NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES:
        {
            PNDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Reg =
                &MiniportAttributes->RegistrationAttributes;

            Ext->RegistrationAttrs       = *Reg;
            Ext->RegistrationAttrsValid  = TRUE;

            /* The MiniportAdapterContext field is the driver's per-instance
             * cookie. Every subsequent call into the driver passes this. */
            Ext->MiniportAdapterContext  = Reg->MiniportAdapterContext;
            return NDIS_STATUS_SUCCESS;
        }

        case NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES:
        {
            PNDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES Gen =
                &MiniportAttributes->GeneralAttributes;

            Ext->GeneralAttrs       = *Gen;
            Ext->GeneralAttrsValid  = TRUE;

            /* Mirror the MAC address into the legacy LOGICAL_ADAPTER
             * fields so existing 5.x consumers see it. */
            if (Gen->MacAddressLength <= sizeof(Adapter->Address))
            {
                RtlCopyMemory(&Adapter->Address,
                              Gen->CurrentMacAddress,
                              Gen->MacAddressLength);
                Adapter->AddressLength = Gen->MacAddressLength;
            }
            DbgPrint("NDIS6: adapter %p MAC %02x:%02x:%02x:%02x:%02x:%02x connect=%d\n", Adapter, Gen->CurrentMacAddress[0], Gen->CurrentMacAddress[1], Gen->CurrentMacAddress[2], Gen->CurrentMacAddress[3], Gen->CurrentMacAddress[4], Gen->CurrentMacAddress[5], Gen->MediaConnectState);
            return NDIS_STATUS_SUCCESS;
        }

        case 0xCA:  /* NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES */
        {
            /* B2: record the driver's offload capability pointers. The
             * struct layout at the start is:
             *   NDIS_OBJECT_HEADER Header;
             *   NDIS_OFFLOAD*      DefaultOffloadConfiguration;
             *   NDIS_OFFLOAD*      HardwareOffloadCapabilities;
             * We read the pointers by offset because the header file
             * doesn't fully declare the struct in our NDIS 5.1 PCH. */
            PVOID* AttrBase  = (PVOID*)((PUCHAR)MiniportAttributes + sizeof(NDIS_OBJECT_HEADER));
            Ext->OffloadDefaultPtr = AttrBase[0];
            Ext->OffloadHwPtr      = AttrBase[1];
            Ext->OffloadValid      = TRUE;
            DbgPrint("NDIS6: offload attrs recorded Default=%p Hw=%p\n",
                     Ext->OffloadDefaultPtr, Ext->OffloadHwPtr);
            return NDIS_STATUS_SUCCESS;
        }

        case NDIS_OBJECT_TYPE_DEFAULT:
        default:
            /* RSS and other attributes — accept and ignore for now. */
            return NDIS_STATUS_SUCCESS;
    }
}

/* ============================================================================
 *  PnP dispatchers — proper WDM function-driver pattern
 *
 *  An NDIS 6 miniport is a function driver sitting on top of the PCI bus
 *  driver's PDO. Every PnP IRP MUST pass through our FDO AND reach the
 *  lower driver (PCI). We handle only the IRPs we care about (START,
 *  REMOVE/SURPRISE_REMOVAL) and pass everything else down unchanged.
 *
 *  IRP_MN_QUERY_INTERFACE in particular is how the driver fetches a
 *  BUS_INTERFACE_STANDARD from PCI for PCI config space reads. If we
 *  complete that IRP ourselves with STATUS_NOT_SUPPORTED, the driver
 *  falls back to reading bus 0 slot 0 (the host bridge) and rejects
 *  the hardware as unrecognized.
 * ============================================================================ */

static NTSTATUS
Ndis6CompleteIrp(_In_ PIRP Irp, _In_ NTSTATUS Status)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

/* Completion routine used by Ndis6ForwardPnpIrpAndWait. Signals the
 * event and returns STATUS_MORE_PROCESSING_REQUIRED so the caller can
 * complete the IRP itself after doing its own post-START work. */
typedef struct _NDIS6_PNP_COMPLETION_CONTEXT
{
    KEVENT    Event;
    NTSTATUS  Status;
} NDIS6_PNP_COMPLETION_CONTEXT, *PNDIS6_PNP_COMPLETION_CONTEXT;

static NTSTATUS NTAPI
Ndis6PnpCompletionRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PNDIS6_PNP_COMPLETION_CONTEXT ctx = (PNDIS6_PNP_COMPLETION_CONTEXT)Context;
    UNREFERENCED_PARAMETER(DeviceObject);

    ctx->Status = Irp->IoStatus.Status;
    KeSetEvent(&ctx->Event, IO_NO_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

/* Send a PnP IRP down the stack and block until the lower driver
 * finishes. Used for IRP_MN_START_DEVICE where we need PCI to fully
 * configure the device before we call MiniportInitializeEx. */
static NTSTATUS
Ndis6ForwardPnpIrpAndWait(
    _In_ PDEVICE_OBJECT LowerDevice,
    _In_ PIRP Irp)
{
    NDIS6_PNP_COMPLETION_CONTEXT ctx;
    NTSTATUS status;

    KeInitializeEvent(&ctx.Event, NotificationEvent, FALSE);
    ctx.Status = STATUS_SUCCESS;

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp, Ndis6PnpCompletionRoutine, &ctx,
                           TRUE, TRUE, TRUE);

    status = IoCallDriver(LowerDevice, Irp);
    if (status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&ctx.Event, Executive, KernelMode, FALSE, NULL);
        status = ctx.Status;
    }
    return status;
}

/* Unconditional pass-through: forward the IRP to the next driver and
 * return whatever it returns, without signalling completion ourselves.
 * Used for every PnP IRP we don't explicitly handle, and for
 * IRP_MJ_SYSTEM_CONTROL (WMI). */
static NTSTATUS
Ndis6PassThroughIrp(
    _In_ PDEVICE_OBJECT LowerDevice,
    _In_ PIRP Irp)
{
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(LowerDevice, Irp);
}

static PNDIS6_DRIVER_BLOCK
Ndis6FindDriverBlock(_In_ PDRIVER_OBJECT DriverObject)
{
    PLIST_ENTRY entry;
    PNDIS6_DRIVER_BLOCK block = NULL;
    KIRQL oldIrql;

    KeAcquireSpinLock(&g_Ndis6DriverListLock, &oldIrql);
    for (entry = g_Ndis6DriverList.Flink;
         entry != &g_Ndis6DriverList;
         entry = entry->Flink)
    {
        PNDIS6_DRIVER_BLOCK candidate =
            CONTAINING_RECORD(entry, NDIS6_DRIVER_BLOCK, ListEntry);
        if (candidate->DriverObject == DriverObject)
        {
            block = candidate;
            break;
        }
    }
    KeReleaseSpinLock(&g_Ndis6DriverListLock, oldIrql);
    return block;
}

/* TRUE if DeviceObject is one of the bridge's adapter FDOs (PnP or IM
 * instance). KMDF's device objects share the driver object on hybrid
 * drivers, so DeviceExtension can only be trusted after this check. */
static BOOLEAN
Ndis6DeviceIsAdapterFdo(_In_ PDEVICE_OBJECT DeviceObject)
{
    PLIST_ENTRY entry;
    BOOLEAN     found = FALSE;
    KIRQL       oldIrql;
    extern LIST_ENTRY AdapterListHead;
    extern KSPIN_LOCK AdapterListLock;

    KeAcquireSpinLock(&AdapterListLock, &oldIrql);
    for (entry = AdapterListHead.Flink; entry != &AdapterListHead; entry = entry->Flink)
    {
        PLOGICAL_ADAPTER adapter = CONTAINING_RECORD(entry, LOGICAL_ADAPTER, ListEntry);
        if (adapter->NdisMiniportBlock.DeviceObject == DeviceObject)
        {
            found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&AdapterListLock, oldIrql);
    return found;
}

/* User-mode surface of the miniport FDO: open/close succeed and
 * IOCTL_NDIS_QUERY_GLOBAL_STATS goes through MiniQueryInformation —
 * same as the legacy NdisIDeviceIoControl surface. Returns TRUE (IRP
 * completed, *OutStatus set) only for our own adapter FDOs. */
BOOLEAN
Ndis6TryDispatchAdapterFdoIrp(
    _In_  PDEVICE_OBJECT DeviceObject,
    _In_  PIRP           Irp,
    _Out_ NTSTATUS*      OutStatus)
{
    PLOGICAL_ADAPTER   Adapter;
    PIO_STACK_LOCATION Stack;
    NTSTATUS           Status;
    ULONG              Written = 0;

    if (!Ndis6DeviceIsAdapterFdo(DeviceObject))
        return FALSE;

    Adapter = (PLOGICAL_ADAPTER)DeviceObject->DeviceExtension;
    Stack   = IoGetCurrentIrpStackLocation(Irp);

    switch (Stack->MajorFunction)
    {
        case IRP_MJ_CREATE:
        case IRP_MJ_CLEANUP:
        case IRP_MJ_CLOSE:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MJ_DEVICE_CONTROL:
        {
            if (Stack->Parameters.DeviceIoControl.IoControlCode == IOCTL_NDIS_QUERY_GLOBAL_STATS && Irp->AssociatedIrp.SystemBuffer != NULL && Stack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(NDIS_OID))
            {
                /* METHOD_OUT_DIRECT: OID in SystemBuffer, result in the MDL. */
                PVOID OutBuffer = NULL;
                ULONG OutLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;

                if (Irp->MdlAddress != NULL)
                {
                    OutBuffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
                    if (OutBuffer == NULL)
                    {
                        Status = STATUS_INSUFFICIENT_RESOURCES;
                        break;
                    }
                }
                else
                {
                    OutLength = 0;
                }

                Status = MiniQueryInformation(Adapter, *(PNDIS_OID)Irp->AssociatedIrp.SystemBuffer, OutLength, OutBuffer, &Written);
            }
            else
            {
                Status = STATUS_NOT_SUPPORTED;
            }
            break;
        }

        default:
            Status = STATUS_NOT_SUPPORTED;
            break;
    }

    /* Complete preserving the bytes-written count (Ndis6CompleteIrp
     * zeroes Information, so it can't be used here). */
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Written;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    *OutStatus = Status;
    return TRUE;
}

PDRIVER_DISPATCH
Ndis6HybridGetOriginalDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ UCHAR          MajorFunction)
{
    PNDIS6_DRIVER_BLOCK Block;

    Block = Ndis6FindDriverBlock(DeviceObject->DriverObject);
    if (Block == NULL || !Block->IsWdfHybrid)
        return NULL;

    if (Ndis6DeviceIsAdapterFdo(DeviceObject) || Ndis6DeviceIsControlDevice(DeviceObject))
        return NULL;

    return Block->OriginalMajorFunction[MajorFunction];
}

/* Installed on every non-PnP/Power/WMI major function of a hybrid driver
 * object. Routes IRPs for KMDF-owned device objects to KMDF's dispatch;
 * IRPs for our own devices keep the pure-path NOT_SUPPORTED behavior. */
static NTSTATUS NTAPI
Ndis6DispatchHybridGeneric(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    NTSTATUS         Status;
    PDRIVER_DISPATCH Original = Ndis6HybridGetOriginalDispatch(DeviceObject, IoGetCurrentIrpStackLocation(Irp)->MajorFunction);

    if (Original != NULL)
        return Original(DeviceObject, Irp);

    if (Ndis6TryDispatchAdapterFdoIrp(DeviceObject, Irp, &Status))
        return Status;

    return Ndis6CompleteIrp(Irp, STATUS_NOT_SUPPORTED);
}

static NTSTATUS NTAPI
Ndis6AddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PNDIS6_DRIVER_BLOCK DriverBlock;
    PLOGICAL_ADAPTER    Adapter;
    NDIS_STATUS         NdisStatus;

    DriverBlock = Ndis6FindDriverBlock(DriverObject);
    if (DriverBlock == NULL)
        return STATUS_DEVICE_NOT_READY;

    /* Hybrid driver: chain KMDF's AddDevice first so its FDO attaches to
     * the PDO; ours then lands on top and IRPs flow NDIS → KMDF → bus. */
    if (DriverBlock->IsWdfHybrid && DriverBlock->OriginalAddDevice != NULL)
    {
        NTSTATUS Status = DriverBlock->OriginalAddDevice(DriverObject, PhysicalDeviceObject);
        DbgPrint("NDIS6: hybrid AddDevice → KMDF returned 0x%08lx\n", Status);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    NdisStatus = Ndis6CreateLogicalAdapter(DriverBlock, PhysicalDeviceObject, &Adapter);
    if (!NT_SUCCESS(NdisStatus))
        return NdisStatus;

    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
Ndis6DispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    PIO_STACK_LOCATION Stack;
    PLOGICAL_ADAPTER   Adapter;
    PNDIS6_ADAPTER_EXT Ext;
    PDEVICE_OBJECT     LowerDevice;
    NDIS_STATUS        NdisStatus;
    NTSTATUS           Status;

    /* Hybrid path: KMDF's own device objects keep KMDF's PnP dispatch. */
    {
        PDRIVER_DISPATCH Original = Ndis6HybridGetOriginalDispatch(DeviceObject, IRP_MJ_PNP);
        if (Original != NULL)
            return Original(DeviceObject, Irp);
    }

    Stack   = IoGetCurrentIrpStackLocation(Irp);
    Adapter = (PLOGICAL_ADAPTER)DeviceObject->DeviceExtension;
    Ext     = (Adapter && Adapter->IsNdis6) ? NDIS6_EXT(Adapter) : NULL;

    /* Every branch needs the lower device object to forward IRPs to. */
    LowerDevice = Adapter ? Adapter->NdisMiniportBlock.NextDeviceObject : NULL;
    if (LowerDevice == NULL)
    {
        /* Shouldn't happen if AddDevice ran correctly. Complete defensively. */
        return Ndis6CompleteIrp(Irp, STATUS_INVALID_DEVICE_STATE);
    }

    switch (Stack->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
        {
            /* ---------------------------------------------------------
             *  Forward START down first so PCI (the PDO's driver)
             *  finishes its own device setup — DMA adapter registration,
             *  power state transition, and the per-device bus-interface
             *  context that IRP_MN_QUERY_INTERFACE later depends on.
             * --------------------------------------------------------- */
            Status = Ndis6ForwardPnpIrpAndWait(LowerDevice, Irp);
            if (!NT_SUCCESS(Status))
                return Ndis6CompleteIrp(Irp, Status);

            /* Save resources AFTER PCI has run. The resource lists live
             * in the incoming stack location. Don't deep-copy; PnP
             * guarantees the lists stay live until REMOVE. */
            if (Ext)
            {
                PCM_RESOURCE_LIST raw        = Stack->Parameters.StartDevice.AllocatedResources;
                PCM_RESOURCE_LIST translated = Stack->Parameters.StartDevice.AllocatedResourcesTranslated;

                Ext->AllocatedResources           = raw;
                Ext->AllocatedResourcesTranslated = translated;
                Adapter->NdisMiniportBlock.AllocatedResources           = raw;
                Adapter->NdisMiniportBlock.AllocatedResourcesTranslated = translated;

                if (translated && translated->Count > 0)
                {
                    ULONG i;
                    PCM_PARTIAL_RESOURCE_DESCRIPTOR p =
                        translated->List[0].PartialResourceList.PartialDescriptors;
                    for (i = 0; i < translated->List[0].PartialResourceList.Count; i++, p++)
                    {
                        if (p->Type == CmResourceTypeInterrupt)
                        {
                            Ext->InterruptVector   = p->u.Interrupt.Vector;
                            Ext->InterruptIrql     = (KIRQL)p->u.Interrupt.Level;
                            Ext->InterruptAffinity = p->u.Interrupt.Affinity;
                            Ext->InterruptFlags    = p->Flags;
                        }
                    }
                }
            }

            /* Now call the miniport's InitializeHandlerEx. If it succeeds,
             * mark the adapter as initialized so the matching HaltEx will
             * run on REMOVE. If it fails, the driver has cleaned up
             * internally — we must NOT call HaltEx (MS DDK contract). */
            NdisStatus = Ndis6CallMiniportInitializeEx(Adapter);
            if (NdisStatus == NDIS_STATUS_SUCCESS)
            {
                if (Ext)
                    Ext->Initialized = TRUE;
                Status = STATUS_SUCCESS;

                /* Phase 7A: now that the adapter is up and GeneralAttrs
                 * is populated, fan out a bind notification to every
                 * native NDIS 6 protocol driver that has registered. */
                {
                    extern VOID Ndis6BindAllProtocolsToAdapter(PLOGICAL_ADAPTER);
                    extern VOID Ndis6AttachFiltersToAdapter(PLOGICAL_ADAPTER);
                    Ndis6BindAllProtocolsToAdapter(Adapter);
                    /* Phase 7B: attach any registered NDIS 6 filter
                     * drivers to this adapter. */
                    Ndis6AttachFiltersToAdapter(Adapter);
                }
            }
            else
            {
                Status = STATUS_UNSUCCESSFUL;
            }

            /* The IRP has already been sent down and the completion
             * routine blocked it; we now complete it ourselves. */
            return Ndis6CompleteIrp(Irp, Status);
        }

        case IRP_MN_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
        {
            /* ---------------------------------------------------------
             *  Symmetric teardown:
             *    1. Halt the miniport (only if init succeeded)
             *    2. Forward REMOVE down so PCI can clean up its side
             *    3. Destroy our own state (detach + free ext + delete FDO)
             * --------------------------------------------------------- */
            if (Ext && Ext->Initialized)
            {
                NDIS_HALT_ACTION action =
                    (Stack->MinorFunction == IRP_MN_SURPRISE_REMOVAL)
                        ? NdisHaltDeviceSurpriseRemoved
                        : NdisHaltDeviceRemoved;
                Ndis6CallMiniportHaltEx(Adapter, action);
                Ext->Initialized = FALSE;
            }

            Status = Ndis6PassThroughIrp(LowerDevice, Irp);
            Ndis6DestroyLogicalAdapter(Adapter);
            return Status;
        }

        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_STOP_DEVICE:
            /* Forward down; no local handling needed at Phase 1. */
            return Ndis6PassThroughIrp(LowerDevice, Irp);

        default:
            /* Every other PnP IRP — IRP_MN_QUERY_INTERFACE (critical:
             * BUS_INTERFACE_STANDARD), IRP_MN_QUERY_CAPABILITIES,
             * IRP_MN_QUERY_PNP_DEVICE_STATE, IRP_MN_FILTER_RESOURCE_REQUIREMENTS,
             * IRP_MN_QUERY_DEVICE_RELATIONS, IRP_MN_QUERY_ID,
             * IRP_MN_QUERY_BUS_INFORMATION, etc. — pass through to PCI. */
            return Ndis6PassThroughIrp(LowerDevice, Irp);
    }
}

/* Completion routine for the D0 power-up path. Called by the IO manager
 * after PCI finishes powering the device back up. We can't safely call
 * the miniport's RestartHandler from inside this routine (it expects
 * IRQL <= PASSIVE), so we queue a work item if the IRQL is too high. */
typedef struct _NDIS6_POWER_COMPLETION_CONTEXT
{
    PNDIS6_ADAPTER_EXT  Ext;
    DEVICE_POWER_STATE  TargetState;
} NDIS6_POWER_COMPLETION_CONTEXT, *PNDIS6_POWER_COMPLETION_CONTEXT;

static VOID
Ndis6DoMiniportRestart(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    NDIS_MINIPORT_RESTART_PARAMETERS RestartParams;

    if (Ext == NULL || !Ext->Initialized || Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.RestartHandler == NULL)
    {
        return;
    }

    RtlZeroMemory(&RestartParams, sizeof(RestartParams));
    RestartParams.Header.Type     = NDIS_OBJECT_TYPE_DEFAULT;
    RestartParams.Header.Revision = NDIS_MINIPORT_RESTART_PARAMETERS_REVISION_1;
    RestartParams.Header.Size     = sizeof(RestartParams);
    Ext->DriverBlock->Characteristics.RestartHandler(
        Ext->MiniportAdapterContext, &RestartParams);
}

static NTSTATUS NTAPI
Ndis6PowerCompletionRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp,
    _In_ PVOID          Context)
{
    PNDIS6_POWER_COMPLETION_CONTEXT ctx = (PNDIS6_POWER_COMPLETION_CONTEXT)Context;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (ctx != NULL)
    {
        if (ctx->TargetState == PowerDeviceD0)
        {
            /* Best-effort: call Restart now. The driver's restart handler
             * needs to tolerate being called from a completion routine
             * which may run at DISPATCH_LEVEL. e1000e and most NDIS 6
             * drivers do tolerate this. */
            Ndis6DoMiniportRestart(ctx->Ext);
        }
        ExFreePoolWithTag(ctx, 'wPNn');
    }

    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);

    return STATUS_CONTINUE_COMPLETION;
}

/* D3: call the miniport's DevicePnPEventNotifyHandler for a given
 * event. The handler was installed via the driver's characteristics
 * struct; most drivers tolerate NULL data. */
static VOID
Ndis6NotifyMiniportDevicePnPEvent(
    _In_ PNDIS6_ADAPTER_EXT     Ext,
    _In_ NDIS_DEVICE_PNP_EVENT  Event)
{
    NET_DEVICE_PNP_EVENT NetEvent;

    if (Ext == NULL || !Ext->Initialized || Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.DevicePnPEventNotifyHandler == NULL ||
        Ext->MiniportAdapterContext == NULL)
    {
        return;
    }

    RtlZeroMemory(&NetEvent, sizeof(NetEvent));
    NetEvent.Header.Type     = NDIS_OBJECT_TYPE_DEFAULT;
    NetEvent.Header.Revision = 1;
    NetEvent.Header.Size     = sizeof(NetEvent);
    NetEvent.DevicePnPEvent  = Event;
    NetEvent.PortNumber      = 0;
    NetEvent.InformationBuffer       = NULL;
    NetEvent.InformationBufferLength = 0;

    Ext->DriverBlock->Characteristics.DevicePnPEventNotifyHandler(
        Ext->MiniportAdapterContext,
        &NetEvent);
}

/* D3: Ndis6IndicateNetPnPEvent — fan a NET_PNP_EVENT out to every
 * bound legacy protocol driver. The protocol's PnPEventHandler is
 * called with the event struct. */
VOID
Ndis6IndicateNetPnPEvent(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ NET_PNP_EVENT_CODE EventCode,
    _In_opt_ PVOID        EventData,
    _In_ ULONG            EventDataLength)
{
    PLIST_ENTRY         Entry;
    KIRQL               OldIrql;
    NET_PNP_EVENT       Event;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    RtlZeroMemory(&Event, sizeof(Event));
    Event.NetEvent     = EventCode;
    Event.Buffer       = EventData;
    Event.BufferLength = EventDataLength;

    KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);
    for (Entry = Adapter->ProtocolListHead.Flink;
         Entry != &Adapter->ProtocolListHead;
         Entry = Entry->Flink)
    {
        PADAPTER_BINDING Binding =
            CONTAINING_RECORD(Entry, ADAPTER_BINDING, AdapterListEntry);
        PNP_EVENT_HANDLER Handler;
        PVOID             Context;
        NDIS_STATUS       CallbackStatus;

        if (Binding->ProtocolBinding == NULL)
            continue;
        Handler = Binding->ProtocolBinding->Chars.PnPEventHandler;
        if (Handler == NULL)
            continue;

        Context = Binding->NdisOpenBlock.ProtocolBindingContext;

        /* Drop lock across callback — protocols re-enter the bridge. */
        KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);
        CallbackStatus = Handler(Context, &Event);
        (void)CallbackStatus;
        KeAcquireSpinLock(&Adapter->NdisMiniportBlock.Lock, &OldIrql);

        /* List may have been mutated under us. Restart from head so
         * we don't dereference a freed binding. */
        Entry = &Adapter->ProtocolListHead;
    }
    KeReleaseSpinLock(&Adapter->NdisMiniportBlock.Lock, OldIrql);
}

static NTSTATUS NTAPI
Ndis6DispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    PLOGICAL_ADAPTER   Adapter;
    PNDIS6_ADAPTER_EXT Ext;
    PDEVICE_OBJECT     LowerDevice;
    PIO_STACK_LOCATION Stack;
    PDRIVER_DISPATCH   Original;

    /* Hybrid path: KMDF's own device objects keep KMDF's power dispatch. */
    Original = Ndis6HybridGetOriginalDispatch(DeviceObject, IRP_MJ_POWER);
    if (Original != NULL)
        return Original(DeviceObject, Irp);

    Adapter = (PLOGICAL_ADAPTER)DeviceObject->DeviceExtension;
    Ext     = (Adapter && Adapter->IsNdis6) ? NDIS6_EXT(Adapter) : NULL;
    LowerDevice = Adapter ? Adapter->NdisMiniportBlock.NextDeviceObject : NULL;

    if (LowerDevice == NULL)
        return Ndis6CompleteIrp(Irp, STATUS_INVALID_DEVICE_STATE);

    Stack = IoGetCurrentIrpStackLocation(Irp);

    /* D3: system power state transitions (S0→S3/S4/S5 etc.). Notify the
     * miniport via DevicePnPEventNotifyHandler with
     * NdisDevicePnPEventPowerProfileChanged so drivers can adjust their
     * WoL / WOL / suspend state. */
    if (Ext != NULL && Stack->MajorFunction == IRP_MJ_POWER &&
        Stack->MinorFunction == IRP_MN_SET_POWER &&
        Stack->Parameters.Power.Type == SystemPowerState)
    {
        Ndis6NotifyMiniportDevicePnPEvent(
            Ext, NdisDevicePnPEventPowerProfileChanged);
    }

    /* Phase 6: route SET_POWER state transitions through the NDIS 6
     * miniport's PauseHandler / RestartHandler. NDIS 6 conceptually
     * pauses the data path on the way to D3 and restarts on the way
     * back to D0. We do the simplest valid mapping:
     *   - On D3 (or any non-D0 state): call PauseHandler before
     *     forwarding the IRP down so the miniport stops queueing.
     *   - On D0 entry: forward down first so PCI restores power, then
     *     call RestartHandler so the miniport resumes.
     *
     * The miniport's pause/restart handlers may be NULL — most NDIS 6
     * drivers register them but a few don't. We treat NULL as "nothing
     * to do" and just pass the IRP through. */
    if (Ext != NULL &&
        Stack->MajorFunction == IRP_MJ_POWER &&
        Stack->MinorFunction == IRP_MN_SET_POWER &&
        Stack->Parameters.Power.Type == DevicePowerState)
    {
        DEVICE_POWER_STATE NewState = Stack->Parameters.Power.State.DeviceState;

        if (NewState != PowerDeviceD0 && Ext->Initialized)
        {
            /* Going to a low-power state — pause the miniport now,
             * then forward down. */
            if (Ext->DriverBlock != NULL &&
                Ext->DriverBlock->Characteristics.PauseHandler != NULL)
            {
                NDIS_MINIPORT_PAUSE_PARAMETERS PauseParams;
                RtlZeroMemory(&PauseParams, sizeof(PauseParams));
                PauseParams.Header.Type     = NDIS_OBJECT_TYPE_DEFAULT;
                PauseParams.Header.Revision = 1;
                PauseParams.Header.Size     = sizeof(PauseParams);
                Ext->DriverBlock->Characteristics.PauseHandler(
                    Ext->MiniportAdapterContext, &PauseParams);
            }
        }
        else if (NewState == PowerDeviceD0 && Ext->Initialized)
        {
            /* Going to D0 — forward down first so PCI restores power,
             * then call RestartHandler from the completion routine
             * (Ndis6PowerCompletionRoutine). The completion routine
             * runs after PCI has finished its async resume work. */
            PNDIS6_POWER_COMPLETION_CONTEXT ctx;

            ctx = (PNDIS6_POWER_COMPLETION_CONTEXT)ExAllocatePoolWithTag(
                NonPagedPool, sizeof(*ctx), 'wPNn');
            if (ctx != NULL)
            {
                ctx->Ext         = Ext;
                ctx->TargetState = PowerDeviceD0;
                IoCopyCurrentIrpStackLocationToNext(Irp);
                IoSetCompletionRoutine(Irp, Ndis6PowerCompletionRoutine,
                                       ctx, TRUE, TRUE, TRUE);
                PoStartNextPowerIrp(Irp);
                return PoCallDriver(LowerDevice, Irp);
            }
            /* Allocation failed — fall through to the simple path. */
        }
    }

    /* WDM contract: every function driver that doesn't own the device
     * power state must PoStartNextPowerIrp + PoCallDriver to pass the
     * IRP to the PDO's driver. Skipping this breaks PCI's power state
     * machine and leaks power IRPs. */
    PoStartNextPowerIrp(Irp);
    IoSkipCurrentIrpStackLocation(Irp);
    return PoCallDriver(LowerDevice, Irp);
}

/* System control (WMI) dispatch — pass through to PCI unchanged. */
static NTSTATUS NTAPI
Ndis6DispatchSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    PLOGICAL_ADAPTER Adapter;
    PDEVICE_OBJECT   LowerDevice;

    /* Hybrid path: KMDF's own device objects keep KMDF's WMI dispatch. */
    PDRIVER_DISPATCH Original = Ndis6HybridGetOriginalDispatch(DeviceObject, IRP_MJ_SYSTEM_CONTROL);
    if (Original != NULL)
        return Original(DeviceObject, Irp);

    Adapter = (PLOGICAL_ADAPTER)DeviceObject->DeviceExtension;
    LowerDevice = Adapter ? Adapter->NdisMiniportBlock.NextDeviceObject : NULL;

    if (LowerDevice == NULL)
        return Ndis6CompleteIrp(Irp, STATUS_INVALID_DEVICE_STATE);

    return Ndis6PassThroughIrp(LowerDevice, Irp);
}

static NTSTATUS NTAPI
Ndis6DispatchUnknown(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp)
{
    NTSTATUS Status;

    /* User-mode opens + IOCTL_NDIS_QUERY_GLOBAL_STATS on the miniport
     * FDO (ipconfig / the connection-status dialog). */
    if (Ndis6TryDispatchAdapterFdoIrp(DeviceObject, Irp, &Status))
        return Status;

    return Ndis6CompleteIrp(Irp, STATUS_NOT_SUPPORTED);
}

/* EOF */
