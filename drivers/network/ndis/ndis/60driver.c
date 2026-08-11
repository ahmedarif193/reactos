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
static volatile LONG g_Ndis6DriverListState;

#define NDIS6_DRIVER_TAG    'rDNn'  /* "nNDr" */

VOID
Ndis6DriverInit(VOID)
{
    if (InterlockedCompareExchange(&g_Ndis6DriverListState, 1, 0) == 0)
    {
        InitializeListHead(&g_Ndis6DriverList);
        KeInitializeSpinLock(&g_Ndis6DriverListLock);
        InterlockedExchange(&g_Ndis6DriverListState, 2);
        return;
    }

    while (InterlockedCompareExchange(&g_Ndis6DriverListState, 2, 2) != 2)
        YieldProcessor();
}

static BOOLEAN
Ndis6IsSupportedMinorVersion(
    _In_ UCHAR MinorVersion)
{
    switch (MinorVersion)
    {
        case 0:
        case 1:
#if NDIS_SUPPORT_NDIS620
        case 20:
#endif
#if NDIS_SUPPORT_NDIS630
        case 30:
#endif
#if NDIS_SUPPORT_NDIS640
        case 40:
#endif
#if NDIS_SUPPORT_NDIS650
        case 50:
#endif
#if NDIS_SUPPORT_NDIS651
        case 51:
#endif
#if NDIS_SUPPORT_NDIS660
        case 60:
#endif
#if NDIS_SUPPORT_NDIS670
        case 70:
#endif
#if NDIS_SUPPORT_NDIS680
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
#endif
            return TRUE;

        default:
            return FALSE;
    }
}

static NDIS_STATUS
Ndis6ValidateMiniportDriverCharacteristics(
    _In_ PNDIS_MINIPORT_DRIVER_CHARACTERISTICS Characteristics,
    _Out_ PULONG CopySize)
{
    ULONG RequiredSize;
    UCHAR RequiredRevision;

    if (Characteristics->Header.Type !=
        NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS)
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    switch (Characteristics->Header.Revision)
    {
        case NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1:
            RequiredSize = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
            break;

        case NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2:
            RequiredSize = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
            break;

#if NDIS_SUPPORT_NDIS680
        case NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_3:
            RequiredSize = NDIS_SIZEOF_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_3;
            break;
#endif

        default:
            return NDIS_STATUS_BAD_VERSION;
    }

    if (Characteristics->Header.Size < RequiredSize ||
        Characteristics->Header.Size > sizeof(*Characteristics))
    {
        return NDIS_STATUS_BAD_CHARACTERISTICS;
    }

    if (Characteristics->MajorNdisVersion != 6 ||
        !Ndis6IsSupportedMinorVersion(Characteristics->MinorNdisVersion))
    {
        return NDIS_STATUS_BAD_VERSION;
    }

#if NDIS_SUPPORT_NDIS680
    if (Characteristics->MinorNdisVersion >= 80)
        RequiredRevision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_3;
    else
#endif
    if (Characteristics->MinorNdisVersion >= 1)
        RequiredRevision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    else
        RequiredRevision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_1;
    if (Characteristics->Header.Revision != RequiredRevision)
        return NDIS_STATUS_BAD_CHARACTERISTICS;

    *CopySize = Characteristics->Header.Size;
    return NDIS_STATUS_SUCCESS;
}

/* Windows 11 zero-normalizes these descriptors into fixed NDIS-owned
 * storage before publishing them to the rest of the stack. */
#define NDIS6_OFFLOAD_MINIMUM_SIZE       0x70
#define NDIS6_OFFLOAD_STORAGE_SIZE       0xD8
#define NDIS6_TCP_OFFLOAD_MINIMUM_SIZE   0x14
#define NDIS6_TCP_OFFLOAD_STORAGE_SIZE   0x14

static NDIS_STATUS
Ndis6DuplicateOffloadDescriptor(
    _In_opt_ PVOID Source,
    _In_ UCHAR ExpectedType,
    _In_ USHORT MinimumSize,
    _In_ USHORT StorageSize,
    _Outptr_result_bytebuffer_maybenull_(StorageSize) PVOID *Copy)
{
    PNDIS_OBJECT_HEADER Header;
    ULONG CopyLength;
    PVOID Buffer;

    *Copy = NULL;
    if (Source == NULL)
        return NDIS_STATUS_SUCCESS;

    Header = Source;
    if (Header->Type != ExpectedType ||
        Header->Revision < 1 ||
        Header->Size < MinimumSize)
    {
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                   StorageSize,
                                   NDIS6_ATTR_TAG);
    if (Buffer == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Buffer, StorageSize);
    CopyLength = Header->Size < StorageSize ? Header->Size : StorageSize;
    RtlCopyMemory(Buffer, Source, CopyLength);
    *Copy = Buffer;
    return NDIS_STATUS_SUCCESS;
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
Ndis6RegisterMiniportDriverInternal(
    _In_     PDRIVER_OBJECT                          DriverObject,
    _In_     PUNICODE_STRING                         RegistryPath,
    _In_opt_ NDIS_HANDLE                             MiniportDriverContext,
    _In_     PNDIS_MINIPORT_DRIVER_CHARACTERISTICS   MiniportDriverCharacteristics,
    _In_opt_ PNDIS6_WDF_CX_DRIVER                    WdfCxDriver,
    _Out_    PNDIS_HANDLE                            NdisMiniportDriverHandle)
{
    PNDIS6_DRIVER_BLOCK Block;
    NDIS_STATUS Status;
    KIRQL OldIrql;
    ULONG CharacteristicsSize;
    ULONG i;

    if (DriverObject == NULL || MiniportDriverCharacteristics == NULL ||
        NdisMiniportDriverHandle == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    *NdisMiniportDriverHandle = NULL;

    if (RegistryPath == NULL ||
        RegistryPath->Length > RegistryPath->MaximumLength ||
        (RegistryPath->Length & (sizeof(WCHAR) - 1)) != 0 ||
        RegistryPath->Length > MAXUSHORT - sizeof(WCHAR) ||
        (RegistryPath->Length != 0 && RegistryPath->Buffer == NULL) ||
        (!WdfCxDriver && DriverObject->DriverExtension == NULL))
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    Status = Ndis6ValidateMiniportDriverCharacteristics(MiniportDriverCharacteristics, &CharacteristicsSize);
    if (Status != NDIS_STATUS_SUCCESS)
        return Status;

    /* These two callbacks are required for every NDIS 6 miniport. Other
     * handlers remain optional or depend on the miniport type. */
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
    Block->Signature             = NDIS6_DRIVER_BLOCK_SIGNATURE;
    Block->DriverObject          = DriverObject;
    Block->MiniportDriverContext = MiniportDriverContext;
    Block->IsWdfManaged          = (WdfCxDriver != NULL);
    Block->WdfCxDriver           = WdfCxDriver;
    RtlCopyMemory(&Block->Characteristics, MiniportDriverCharacteristics, CharacteristicsSize);

    /* Copy the registry path so the driver can free its own copy if it
     * wants. We allocate fresh PagedPool storage for the buffer. */
    if (RegistryPath->Length != 0)
    {
        Block->RegistryPath.Length        = RegistryPath->Length;
        Block->RegistryPath.MaximumLength = RegistryPath->Length + sizeof(WCHAR);
        Block->RegistryPath.Buffer        = (PWSTR)ExAllocatePoolWithTag(
            PagedPool, Block->RegistryPath.MaximumLength, NDIS6_DRIVER_TAG);
        if (Block->RegistryPath.Buffer == NULL)
        {
            ExFreePoolWithTag(Block, NDIS6_DRIVER_TAG);
            return NDIS_STATUS_RESOURCES;
        }

        RtlCopyMemory(Block->RegistryPath.Buffer,
                      RegistryPath->Buffer,
                      RegistryPath->Length);
        Block->RegistryPath.Buffer[RegistryPath->Length / sizeof(WCHAR)] = L'\0';
    }

    /* Windows invokes MiniportSetOptions synchronously from registration,
     * with the newly allocated NDIS driver handle already usable. */
    if (Block->Characteristics.SetOptionsHandler != NULL)
    {
        *NdisMiniportDriverHandle = (NDIS_HANDLE)Block;
        Status = Block->Characteristics.SetOptionsHandler(
            (NDIS_HANDLE)Block,
            MiniportDriverContext);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            *NdisMiniportDriverHandle = NULL;
            if (Block->RegistryPath.Buffer != NULL)
                ExFreePoolWithTag(Block->RegistryPath.Buffer, NDIS6_DRIVER_TAG);
            ExFreePoolWithTag(Block, NDIS6_DRIVER_TAG);
            return Status;
        }
    }

    if (!Block->IsWdfManaged)
    {
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
    }

    /* Insert into the global driver list. */
    KeAcquireSpinLock(&g_Ndis6DriverListLock, &OldIrql);
    InsertTailList(&g_Ndis6DriverList, &Block->ListEntry);
    KeReleaseSpinLock(&g_Ndis6DriverListLock, OldIrql);

    if (WdfCxDriver)
        InterlockedIncrement(&WdfCxDriver->ClientCount);

    *NdisMiniportDriverHandle = (NDIS_HANDLE)Block;
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
NdisMRegisterMiniportDriver(
    _In_     PDRIVER_OBJECT                          DriverObject,
    _In_     PUNICODE_STRING                         RegistryPath,
    _In_opt_ NDIS_HANDLE                             MiniportDriverContext,
    _In_     PNDIS_MINIPORT_DRIVER_CHARACTERISTICS   MiniportDriverCharacteristics,
    _Out_    PNDIS_HANDLE                            NdisMiniportDriverHandle)
{
    return Ndis6RegisterMiniportDriverInternal(DriverObject,
                                               RegistryPath,
                                               MiniportDriverContext,
                                               MiniportDriverCharacteristics,
                                               NULL,
                                               NdisMiniportDriverHandle);
}

VOID
NTAPI
NdisMDeregisterMiniportDriver(
    _In_ NDIS_HANDLE NdisMiniportDriverHandle)
{
    PNDIS6_DRIVER_BLOCK Block = (PNDIS6_DRIVER_BLOCK)NdisMiniportDriverHandle;
    KIRQL OldIrql;

    if (Block == NULL || Block->Signature != NDIS6_DRIVER_BLOCK_SIGNATURE)
        return;

    KeAcquireSpinLock(&g_Ndis6DriverListLock, &OldIrql);
    RemoveEntryList(&Block->ListEntry);
    KeReleaseSpinLock(&g_Ndis6DriverListLock, OldIrql);

    if (Block->RegistryPath.Buffer)
        ExFreePoolWithTag(Block->RegistryPath.Buffer, NDIS6_DRIVER_TAG);

    if (Block->WdfCxDriver)
        InterlockedDecrement(&Block->WdfCxDriver->ClientCount);

    Block->Signature = 0;
    ExFreePoolWithTag(Block, NDIS6_DRIVER_TAG);
}

NDIS_STATUS
Ndis6CallMiniportAddDevice(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_DRIVER_BLOCK Block;
    NDIS_STATUS Status;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return NDIS_STATUS_INVALID_PARAMETER;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    Block = Ext->DriverBlock;
    if (!Block->PnpCharacteristicsValid ||
        Block->PnpCharacteristics.MiniportAddDeviceHandler == NULL)
    {
        return NDIS_STATUS_SUCCESS;
    }

    if (InterlockedCompareExchange(&Ext->MiniportAddDeviceState, NDIS6_ADD_DEVICE_CALLING, NDIS6_ADD_DEVICE_NOT_CALLED) !=
        NDIS6_ADD_DEVICE_NOT_CALLED)
    {
        return NDIS_STATUS_INVALID_STATE;
    }

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    Status = Block->PnpCharacteristics.MiniportAddDeviceHandler((NDIS_HANDLE)Adapter, Block->MiniportDriverContext);
    if (Status == NDIS_STATUS_SUCCESS)
    {
        InterlockedExchange(&Ext->MiniportAddDeviceState, NDIS6_ADD_DEVICE_SUCCEEDED);
    }
    else
    {
        Ext->MiniportAddDeviceContext = NULL;
        Ext->MiniportAddDeviceAttributesValid = FALSE;
        InterlockedExchange(&Ext->MiniportAddDeviceState, NDIS6_ADD_DEVICE_FAILED);
    }

    return Status;
}

VOID
Ndis6CallMiniportRemoveDevice(
    _In_ PLOGICAL_ADAPTER Adapter)
{
    PNDIS6_ADAPTER_EXT Ext;
    PNDIS6_DRIVER_BLOCK Block;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || Ext->DriverBlock == NULL)
        return;

    if (InterlockedCompareExchange(&Ext->MiniportAddDeviceState, NDIS6_ADD_DEVICE_REMOVING, NDIS6_ADD_DEVICE_SUCCEEDED) !=
        NDIS6_ADD_DEVICE_SUCCEEDED)
    {
        return;
    }

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    Block = Ext->DriverBlock;
    if (Block->PnpCharacteristicsValid &&
        Block->PnpCharacteristics.MiniportRemoveDeviceHandler != NULL)
    {
        Block->PnpCharacteristics.MiniportRemoveDeviceHandler(Ext->MiniportAddDeviceContext);
    }

    Ext->MiniportAddDeviceContext = NULL;
    Ext->MiniportAddDeviceAttributesValid = FALSE;
    InterlockedExchange(&Ext->MiniportAddDeviceState, NDIS6_ADD_DEVICE_REMOVED);
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
        case NDIS_OBJECT_TYPE_MINIPORT_ADD_DEVICE_REGISTRATION_ATTRIBUTES:
        {
            PNDIS_MINIPORT_ADD_DEVICE_REGISTRATION_ATTRIBUTES AddDevice =
                &MiniportAttributes->AddDeviceRegistrationAttributes;

            if (InterlockedCompareExchange(&Ext->MiniportAddDeviceState, NDIS6_ADD_DEVICE_CALLING, NDIS6_ADD_DEVICE_CALLING) !=
                    NDIS6_ADD_DEVICE_CALLING ||
                Ext->MiniportAddDeviceAttributesValid)
            {
                return NDIS_STATUS_INVALID_STATE;
            }

            if (AddDevice->Header.Revision !=
                    NDIS_MINIPORT_ADD_DEVICE_REGISTRATION_ATTRIBUTES_REVISION_1)
            {
                return NDIS_STATUS_BAD_VERSION;
            }

            if (AddDevice->Header.Size !=
                    NDIS_SIZEOF_MINIPORT_ADD_DEVICE_REGISTRATION_ATTRIBUTES_REVISION_1)
            {
                return NDIS_STATUS_INVALID_LENGTH;
            }

            if (AddDevice->Flags != 0)
                return NDIS_STATUS_INVALID_PARAMETER;

            Ext->MiniportAddDeviceContext =
                AddDevice->MiniportAddDeviceContext;
            Ext->MiniportAddDeviceAttributesValid = TRUE;
            return NDIS_STATUS_SUCCESS;
        }

        case NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES:
        {
            PNDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES Reg =
                &MiniportAttributes->RegistrationAttributes;
            ULONG RequiredSize;

            if (Ext->RegistrationAttrsValid || Ext->GeneralAttrsValid)
                return NDIS_STATUS_INVALID_STATE;

            switch (Reg->Header.Revision)
            {
                case NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1:
                    RequiredSize = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
                    break;

                case NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_2:
                    RequiredSize = NDIS_SIZEOF_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_2;
                    break;

                default:
                    return NDIS_STATUS_BAD_VERSION;
            }

            if (Reg->Header.Size < RequiredSize ||
                Reg->Header.Size > sizeof(*Reg))
            {
                return NDIS_STATUS_INVALID_LENGTH;
            }

            RtlZeroMemory(&Ext->RegistrationAttrs, sizeof(Ext->RegistrationAttrs));
            RtlCopyMemory(&Ext->RegistrationAttrs, Reg, Reg->Header.Size);
            Ext->RegistrationAttrsValid = TRUE;

            /* The MiniportAdapterContext field is the driver's per-instance
             * cookie. Every subsequent call into the driver passes this. */
            if (!Ext->IsWdfManaged)
                Ext->MiniportAdapterContext = Reg->MiniportAdapterContext;
            return NDIS_STATUS_SUCCESS;
        }

        case NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES:
        {
            PNDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES Gen =
                &MiniportAttributes->GeneralAttributes;
            NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES NewGen;
            PNDIS_OID NewOidList = NULL;
            PNDIS_OID OldOidList;
            ULONG RequiredSize;

            if (!Ext->RegistrationAttrsValid)
                return NDIS_STATUS_INVALID_STATE;

            switch (Gen->Header.Revision)
            {
                case NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1:
                    RequiredSize = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
                    break;

                case NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2:
                    RequiredSize = NDIS_SIZEOF_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_2;
                    break;

                default:
                    return NDIS_STATUS_BAD_VERSION;
            }

            if (Gen->Header.Size < RequiredSize ||
                Gen->Header.Size > sizeof(*Gen) ||
                Gen->MacAddressLength > sizeof(Gen->CurrentMacAddress) ||
                Gen->MacAddressLength > sizeof(Gen->PermanentMacAddress) ||
                Gen->MacAddressLength > sizeof(Adapter->Address) ||
                (Gen->SupportedOidListLength % sizeof(NDIS_OID)) != 0)
            {
                return NDIS_STATUS_INVALID_LENGTH;
            }

            if (Gen->SupportedOidListLength != 0)
            {
                if (Gen->SupportedOidList == NULL)
                    return NDIS_STATUS_INVALID_PARAMETER;

                NewOidList = ExAllocatePoolWithTag(
                    NonPagedPool,
                    Gen->SupportedOidListLength,
                    NDIS6_ATTR_TAG);
                if (NewOidList == NULL)
                    return NDIS_STATUS_RESOURCES;

                RtlCopyMemory(NewOidList,
                              Gen->SupportedOidList,
                              Gen->SupportedOidListLength);
            }

            RtlZeroMemory(&NewGen, sizeof(NewGen));
            RtlCopyMemory(&NewGen, Gen, Gen->Header.Size);
            NewGen.SupportedOidList = NewOidList;

            /* These are nested, caller-owned descriptors. They are not yet
             * consumed by the bridge, so do not retain transient pointers. */
            NewGen.PowerManagementCapabilities = NULL;
            NewGen.RecvScaleCapabilities = NULL;
            NewGen.PowerManagementCapabilitiesEx = NULL;

            OldOidList = Ext->GeneralAttrs.SupportedOidList;
            Ext->GeneralAttrs = NewGen;
            Ext->GeneralAttrsValid = TRUE;
            if (OldOidList != NULL)
                ExFreePoolWithTag(OldOidList, NDIS6_ATTR_TAG);

            /* Mirror the MAC address into the legacy LOGICAL_ADAPTER
             * fields so existing 5.x consumers see it. */
            if (Gen->MacAddressLength != 0)
            {
                RtlCopyMemory(&Adapter->Address,
                              Gen->CurrentMacAddress,
                              Gen->MacAddressLength);
                Adapter->AddressLength = Gen->MacAddressLength;
            }
            if (Gen->MacAddressLength >= 6)
            {
                DbgPrint("NDIS6: adapter %p MAC %02x:%02x:%02x:%02x:%02x:%02x connect=%d\n", Adapter, Gen->CurrentMacAddress[0], Gen->CurrentMacAddress[1], Gen->CurrentMacAddress[2], Gen->CurrentMacAddress[3], Gen->CurrentMacAddress[4], Gen->CurrentMacAddress[5], Gen->MediaConnectState);
            }
            return NDIS_STATUS_SUCCESS;
        }

        case NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES:
        {
            PNDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES Offload =
                &MiniportAttributes->OffloadAttributes;
            PVOID Default = NULL;
            PVOID Hardware = NULL;
            PVOID TcpDefault = NULL;
            PVOID TcpHardware = NULL;
            PVOID NewDefault = NULL;
            PVOID NewHardware = NULL;
            PVOID NewTcpDefault = NULL;
            PVOID NewTcpHardware = NULL;
            NDIS_STATUS Status;

            if (!Ext->GeneralAttrsValid)
                return NDIS_STATUS_INVALID_STATE;

            if (Offload->Header.Revision !=
                NDIS_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES_REVISION_1)
            {
                return NDIS_STATUS_BAD_VERSION;
            }

            /* Windows treats a short outer descriptor as an empty offload
             * declaration and does not read beyond Header.Size. */
            if (Offload->Header.Size >=
                NDIS_SIZEOF_MINIPORT_ADAPTER_OFFLOAD_ATTRIBUTES_REVISION_1)
            {
                Default = Offload->DefaultOffloadConfiguration;
                Hardware = Offload->HardwareOffloadCapabilities;
                TcpDefault = Offload->DefaultTcpConnectionOffloadConfiguration;
                TcpHardware = Offload->TcpConnectionOffloadHardwareCapabilities;
            }

            if ((Default == NULL) != (Hardware == NULL) ||
                (TcpDefault == NULL) != (TcpHardware == NULL))
            {
                return NDIS_STATUS_NOT_SUPPORTED;
            }

            Status = Ndis6DuplicateOffloadDescriptor(
                Default,
                NDIS_OBJECT_TYPE_OFFLOAD,
                NDIS6_OFFLOAD_MINIMUM_SIZE,
                NDIS6_OFFLOAD_STORAGE_SIZE,
                &NewDefault);
            if (Status != NDIS_STATUS_SUCCESS)
                goto OffloadFailure;

            Status = Ndis6DuplicateOffloadDescriptor(
                Hardware,
                NDIS_OBJECT_TYPE_OFFLOAD,
                NDIS6_OFFLOAD_MINIMUM_SIZE,
                NDIS6_OFFLOAD_STORAGE_SIZE,
                &NewHardware);
            if (Status != NDIS_STATUS_SUCCESS)
                goto OffloadFailure;

            Status = Ndis6DuplicateOffloadDescriptor(
                TcpDefault,
                NDIS_OBJECT_TYPE_DEFAULT,
                NDIS6_TCP_OFFLOAD_MINIMUM_SIZE,
                NDIS6_TCP_OFFLOAD_STORAGE_SIZE,
                &NewTcpDefault);
            if (Status != NDIS_STATUS_SUCCESS)
                goto OffloadFailure;

            Status = Ndis6DuplicateOffloadDescriptor(
                TcpHardware,
                NDIS_OBJECT_TYPE_DEFAULT,
                NDIS6_TCP_OFFLOAD_MINIMUM_SIZE,
                NDIS6_TCP_OFFLOAD_STORAGE_SIZE,
                &NewTcpHardware);
            if (Status != NDIS_STATUS_SUCCESS)
                goto OffloadFailure;

            if (Ext->OffloadDefaultPtr != NULL)
                ExFreePoolWithTag(Ext->OffloadDefaultPtr, NDIS6_ATTR_TAG);
            if (Ext->OffloadHwPtr != NULL)
                ExFreePoolWithTag(Ext->OffloadHwPtr, NDIS6_ATTR_TAG);
            if (Ext->TcpOffloadDefaultPtr != NULL)
                ExFreePoolWithTag(Ext->TcpOffloadDefaultPtr, NDIS6_ATTR_TAG);
            if (Ext->TcpOffloadHwPtr != NULL)
                ExFreePoolWithTag(Ext->TcpOffloadHwPtr, NDIS6_ATTR_TAG);

            Ext->OffloadDefaultPtr = NewDefault;
            Ext->OffloadHwPtr = NewHardware;
            Ext->TcpOffloadDefaultPtr = NewTcpDefault;
            Ext->TcpOffloadHwPtr = NewTcpHardware;
            Ext->OffloadValid = TRUE;
            DbgPrint("NDIS6: offload attributes copied Default=%p Hw=%p TcpDefault=%p TcpHw=%p\n", NewDefault, NewHardware, NewTcpDefault, NewTcpHardware);
            return NDIS_STATUS_SUCCESS;

OffloadFailure:
            if (NewDefault != NULL)
                ExFreePoolWithTag(NewDefault, NDIS6_ATTR_TAG);
            if (NewHardware != NULL)
                ExFreePoolWithTag(NewHardware, NDIS6_ATTR_TAG);
            if (NewTcpDefault != NULL)
                ExFreePoolWithTag(NewTcpDefault, NDIS6_ATTR_TAG);
            if (NewTcpHardware != NULL)
                ExFreePoolWithTag(NewTcpHardware, NDIS6_ATTR_TAG);
            return Status;
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

static NTSTATUS
Ndis6CompleteIrpPreserveInformation(
    _In_ PIRP Irp,
    _In_ NTSTATUS Status)
{
    Irp->IoStatus.Status = Status;
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

PNDIS6_DRIVER_BLOCK
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
PLOGICAL_ADAPTER
Ndis6FindAdapterByFdo(_In_ PDEVICE_OBJECT DeviceObject)
{
    PLIST_ENTRY entry;
    PLOGICAL_ADAPTER found = NULL;
    KIRQL       oldIrql;
    extern LIST_ENTRY AdapterListHead;
    extern KSPIN_LOCK AdapterListLock;

    KeAcquireSpinLock(&AdapterListLock, &oldIrql);
    for (entry = AdapterListHead.Flink; entry != &AdapterListHead; entry = entry->Flink)
    {
        PLOGICAL_ADAPTER adapter = CONTAINING_RECORD(entry, LOGICAL_ADAPTER, ListEntry);
        if (adapter->NdisMiniportBlock.DeviceObject == DeviceObject)
        {
            found = adapter;
            break;
        }
    }
    KeReleaseSpinLock(&AdapterListLock, oldIrql);
    return found;
}

PDEVICE_OBJECT
Ndis6GetIoWorkItemDeviceObject(
    _In_ NDIS_HANDLE NdisObjectHandle)
{
    PLIST_ENTRY Entry;
    PDEVICE_OBJECT DeviceObject = NULL;
    KIRQL OldIrql;
    extern PDEVICE_OBJECT Ndis6GetFilterOrControlIoWorkItemObject(NDIS_HANDLE);
    extern LIST_ENTRY AdapterListHead;
    extern KSPIN_LOCK AdapterListLock;

    if (NdisObjectHandle == NULL)
        return NULL;

    Ndis6DriverInit();
    KeAcquireSpinLock(&g_Ndis6DriverListLock, &OldIrql);
    for (Entry = g_Ndis6DriverList.Flink;
         Entry != &g_Ndis6DriverList;
         Entry = Entry->Flink)
    {
        PNDIS6_DRIVER_BLOCK Block =
            CONTAINING_RECORD(Entry, NDIS6_DRIVER_BLOCK, ListEntry);
        if ((NDIS_HANDLE)Block == NdisObjectHandle)
        {
            DeviceObject = (PDEVICE_OBJECT)Block->DriverObject;
            break;
        }
    }
    KeReleaseSpinLock(&g_Ndis6DriverListLock, OldIrql);

    if (DeviceObject != NULL)
        return DeviceObject;

    DeviceObject = Ndis6GetFilterOrControlIoWorkItemObject(NdisObjectHandle);
    if (DeviceObject != NULL)
        return DeviceObject;

    KeAcquireSpinLock(&AdapterListLock, &OldIrql);
    for (Entry = AdapterListHead.Flink;
         Entry != &AdapterListHead;
         Entry = Entry->Flink)
    {
        PLOGICAL_ADAPTER Adapter =
            CONTAINING_RECORD(Entry, LOGICAL_ADAPTER, ListEntry);
        if ((NDIS_HANDLE)Adapter == NdisObjectHandle)
        {
            DeviceObject = Adapter->NdisMiniportBlock.PhysicalDeviceObject;
            break;
        }
    }
    KeReleaseSpinLock(&AdapterListLock, OldIrql);
    return DeviceObject;
}

static BOOLEAN
Ndis6DeviceIsAdapterFdo(_In_ PDEVICE_OBJECT DeviceObject)
{
    return Ndis6FindAdapterByFdo(DeviceObject) != NULL;
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

    Adapter = Ndis6FindAdapterByFdo(DeviceObject);
    if (Adapter == NULL)
        return FALSE;

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

    NdisStatus = Ndis6CallMiniportAddDevice(Adapter);
    if (NdisStatus != NDIS_STATUS_SUCCESS)
    {
        Ndis6DestroyLogicalAdapter(Adapter);
        return (NTSTATUS)NdisStatus;
    }

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
            /* MiniportStartDevice removes any private resources that the
             * optional post-bus FilterResourceRequirements callback added.
             * Windows invokes it before the START reaches lower drivers. */
            if (Ext != NULL &&
                Ext->DriverBlock->PnpCharacteristicsValid &&
                Ext->DriverBlock->PnpCharacteristics.MiniportStartDeviceHandler != NULL)
            {
                ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
                NdisStatus = Ext->DriverBlock->PnpCharacteristics.MiniportStartDeviceHandler(Ext->MiniportAddDeviceContext, Irp);
                if (NdisStatus != NDIS_STATUS_SUCCESS)
                    return Ndis6CompleteIrp(Irp, (NTSTATUS)NdisStatus);
            }

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
                {
                    Ext->Initialized = TRUE;
                    InterlockedExchange(&Ext->ProtocolBindingsClosing, 0);
                }
                Status = STATUS_SUCCESS;

                /* Build the paused stack first: filter modules attach below
                 * protocol bindings. Then restart bottom-up and only expose a
                 * Running binding after the miniport restart succeeds. */
                {
                    extern VOID Ndis6BindAllProtocolsToAdapter(PLOGICAL_ADAPTER);
                    NdisStatus = Ndis6AttachFiltersToAdapter(Adapter);
                    if (NdisStatus == NDIS_STATUS_SUCCESS)
                        Ndis6BindAllProtocolsToAdapter(Adapter);
                }

                if (NdisStatus == NDIS_STATUS_SUCCESS)
                    NdisStatus = Ndis6RestartDriverStack(Adapter);
                if (NdisStatus == NDIS_STATUS_SUCCESS)
                {
                    Status = STATUS_SUCCESS;
                }
                else
                {
                    DbgPrint("NDIS6: initial stack activation failed 0x%08lx\n", (ULONG)NdisStatus);
                    Ndis6CallMiniportHaltEx(Adapter, NdisHaltDeviceInitializationFailed);
                    if (Ext)
                        Ext->Initialized = FALSE;
                    Status = STATUS_UNSUCCESSFUL;
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

        case IRP_MN_SURPRISE_REMOVAL:
        {
            /* Surprise removal tears down the running miniport but is not the
             * final PnP lifetime boundary. Keep the FDO and AddDevice context
             * until the matching IRP_MN_REMOVE_DEVICE arrives. */
            if (Ext != NULL)
            {
                Ext->SurpriseRemoved = TRUE;
                if (Ext->Initialized)
                {
                    Ndis6NotifyMiniportDevicePnPEvent(Ext, NdisDevicePnPEventSurpriseRemoved);
                    Ndis6CallMiniportHaltEx(Adapter, NdisHaltDeviceSurpriseRemoved);
                    Ext->Initialized = FALSE;
                }
            }

            return Ndis6PassThroughIrp(LowerDevice, Irp);
        }

        case IRP_MN_REMOVE_DEVICE:
        {
            if (Ext != NULL && Ext->Initialized)
            {
                Ndis6CallMiniportHaltEx(Adapter, Ext->SurpriseRemoved ? NdisHaltDeviceSurpriseRemoved : NdisHaltDeviceDisabled);
                Ext->Initialized = FALSE;
            }

            /* The AddDevice context outlives all halt/reinitialize cycles but
             * not the final remove. Call the optional release callback before
             * the lower stack and PDO begin their own final destruction. */
            Ndis6CallMiniportRemoveDevice(Adapter);
            Status = Ndis6PassThroughIrp(LowerDevice, Irp);
            Ndis6DestroyLogicalAdapter(Adapter);
            return Status;
        }

        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
        {
            ULONG_PTR LowerInformation;

            /* The bus stack owns the first pass. The miniport may then adjust
             * message-interrupt requirements while the adapter is halted. */
            Status = Ndis6ForwardPnpIrpAndWait(LowerDevice, Irp);
            if (!NT_SUCCESS(Status))
                return Ndis6CompleteIrpPreserveInformation(Irp, Status);

            LowerInformation = Irp->IoStatus.Information;
            if (Ext != NULL && !Ext->Initialized &&
                Ext->DriverBlock->PnpCharacteristicsValid &&
                Ext->DriverBlock->PnpCharacteristics.MiniportFilterResourceRequirementsHandler != NULL)
            {
                ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
                NdisStatus = Ext->DriverBlock->PnpCharacteristics.MiniportFilterResourceRequirementsHandler(Ext->MiniportAddDeviceContext, Irp);
                if (NdisStatus != NDIS_STATUS_SUCCESS)
                {
                    /* Windows falls back to the lower driver's requirements
                     * when the optional miniport filter declines the request. */
                    Irp->IoStatus.Information = LowerInformation;
                    DbgPrint("NDIS6: FilterResourceRequirements declined 0x%08lx\n", (ULONG)NdisStatus);
                }
            }

            return Ndis6CompleteIrpPreserveInformation(Irp, Status);
        }

        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
            /* These queries do not require a local state transition. */
            return Ndis6PassThroughIrp(LowerDevice, Irp);

        case IRP_MN_STOP_DEVICE:
            /* A resource rebalance restarts this FDO with new resources.
             * Release the current miniport instance so the following START
             * can initialize it again without retaining the old resources. */
            if (Ext && Ext->Initialized)
            {
                Ndis6CallMiniportHaltEx(Adapter, NdisHaltDeviceStopped);
                Ext->Initialized = FALSE;
            }
            return Ndis6PassThroughIrp(LowerDevice, Irp);

        default:
            /* Every other PnP IRP — IRP_MN_QUERY_INTERFACE (critical:
             * BUS_INTERFACE_STANDARD), IRP_MN_QUERY_CAPABILITIES,
             * IRP_MN_QUERY_PNP_DEVICE_STATE,
             * IRP_MN_QUERY_DEVICE_RELATIONS, IRP_MN_QUERY_ID,
             * IRP_MN_QUERY_BUS_INFORMATION, etc. — pass through to PCI. */
            return Ndis6PassThroughIrp(LowerDevice, Irp);
    }
}

/* Power transitions must invoke MiniportPause/MiniportRestart at
 * PASSIVE_LEVEL. DispatchPower and lower-driver completion routines can run
 * at DISPATCH_LEVEL, so defer those callbacks to a system worker and retain
 * ownership of the power IRP until the required transition is complete. */
typedef enum _NDIS6_POWER_WORK_OPERATION
{
    Ndis6PowerPauseAndForward,
    Ndis6PowerResumeAndComplete
} NDIS6_POWER_WORK_OPERATION;

static VOID
Ndis6ApplyMiniportPowerState(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ NDIS6_POWER_WORK_OPERATION Operation,
    _In_ DEVICE_POWER_STATE NewState)
{
    PNDIS6_ADAPTER_EXT Ext;
    NDIS_STATUS Status;
    BOOLEAN WasRunning;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || !Ext->Initialized)
        return;

    if (Operation == Ndis6PowerPauseAndForward &&
        Ext->DevicePowerState == PowerDeviceD0)
    {
        Status = Ndis6NotifyProtocolBindingsPower(Adapter, NewState);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            DbgPrint("NDIS6: protocol NetEventSetPower D%u failed 0x%08lx\n", (ULONG)NewState - (ULONG)PowerDeviceD0, (ULONG)Status);
        }

        WasRunning = (Ext->PauseState == NDIS6_PAUSE_STATE_RUNNING);
        Status = Ndis6PauseDriverStack(Adapter);
        if (WasRunning && Ext->PauseState == NDIS6_PAUSE_STATE_PAUSED)
            Ext->PowerPaused = TRUE;
        if (Status != NDIS_STATUS_SUCCESS)
            DbgPrint("NDIS6: device power-down stack pause failed 0x%08lx\n", (ULONG)Status);
    }

    Status = Ndis6SetMiniportDevicePowerState(Ext, NewState);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        DbgPrint("NDIS6: OID_PNP_SET_POWER D%u failed 0x%08lx\n", (ULONG)NewState - (ULONG)PowerDeviceD0, (ULONG)Status);
    }

    Ext->DevicePowerState = NewState;
    if (Ext->FunctionalDeviceObject != NULL)
    {
        POWER_STATE PowerState;
        PowerState.DeviceState = NewState;
        (VOID)PoSetPowerState(Ext->FunctionalDeviceObject, DevicePowerState, PowerState);
    }

    if (Operation == Ndis6PowerResumeAndComplete &&
        NewState == PowerDeviceD0 &&
        Ext->PowerPaused)
    {
        if (Status == NDIS_STATUS_SUCCESS)
        {
            Status = Ndis6RestartDriverStack(Adapter);
            if (Status == NDIS_STATUS_SUCCESS)
            {
                Status = Ndis6NotifyProtocolBindingsPower(Adapter, NewState);
                if (Status != NDIS_STATUS_SUCCESS)
                {
                    DbgPrint("NDIS6: protocol NetEventSetPower D0 failed 0x%08lx\n", (ULONG)Status);
                }
                Ext->PowerPaused = FALSE;
            }
            else
                DbgPrint("NDIS6: device power-up restart failed 0x%08lx\n", (ULONG)Status);
        }
    }
}

static BOOLEAN
Ndis6ReservePowerWork(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    PLOGICAL_ADAPTER Adapter;

    if (Ext == NULL ||
        (Adapter = Ext->Adapter) == NULL ||
        !Ndis6ReferenceAdapterLifecycle(Adapter))
    {
        return FALSE;
    }

    for (;;)
    {
        if (InterlockedCompareExchange(&Ext->PowerWorkBusy, 1, 0) == 0)
        {
            KeClearEvent(&Ext->PowerWorkIdleEvent);
            return TRUE;
        }

        if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        {
            Ndis6DereferenceAdapterLifecycle(Adapter);
            return FALSE;
        }

        (VOID)KeWaitForSingleObject(&Ext->PowerWorkIdleEvent, Executive, KernelMode, FALSE, NULL);
    }
}

static VOID
Ndis6ReleasePowerWork(
    _In_ PNDIS6_ADAPTER_EXT Ext)
{
    PLOGICAL_ADAPTER Adapter = Ext->Adapter;

    KeSetEvent(&Ext->PowerWorkIdleEvent, IO_NO_INCREMENT, FALSE);
    InterlockedExchange(&Ext->PowerWorkBusy, 0);
    Ndis6DereferenceAdapterLifecycle(Adapter);
}

static VOID
NTAPI
Ndis6PowerWorker(
    _In_ PVOID Parameter)
{
    PLOGICAL_ADAPTER Adapter = Parameter;
    PNDIS6_ADAPTER_EXT Ext;
    PDEVICE_OBJECT LowerDevice;
    PIRP Irp;
    NDIS6_POWER_WORK_OPERATION Operation;
    DEVICE_POWER_STATE NewState;

    if (Adapter == NULL || !Adapter->IsNdis6)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL)
        return;

    LowerDevice = Ext->PowerLowerDevice;
    Irp = Ext->PowerIrp;
    Operation = (NDIS6_POWER_WORK_OPERATION)Ext->PowerWorkOperation;
    NewState = Ext->PowerTargetState;

    Ndis6ApplyMiniportPowerState(Adapter, Operation, NewState);

    Ext->PowerIrp = NULL;
    Ext->PowerLowerDevice = NULL;

    if (Operation == Ndis6PowerPauseAndForward)
    {
        PoStartNextPowerIrp(Irp);
        IoSkipCurrentIrpStackLocation(Irp);
        (VOID)PoCallDriver(LowerDevice, Irp);
    }
    else
    {
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    Ndis6ReleasePowerWork(Ext);
}

static NTSTATUS NTAPI
Ndis6PowerCompletionRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP           Irp,
    _In_ PVOID          Context)
{
    PLOGICAL_ADAPTER Adapter = Context;
    PNDIS6_ADAPTER_EXT Ext;

    UNREFERENCED_PARAMETER(DeviceObject);

    Ext = (Adapter != NULL && Adapter->IsNdis6) ? NDIS6_EXT(Adapter) : NULL;
    if (Ext != NULL &&
        NT_SUCCESS(Irp->IoStatus.Status) &&
        InterlockedCompareExchange(&Ext->PowerWorkBusy, 1, 1) == 1)
    {
        Ext->PowerIrp = Irp;
        ExInitializeWorkItem(&Ext->PowerWorkItem, Ndis6PowerWorker, Adapter);
        ExQueueWorkItem(&Ext->PowerWorkItem, DelayedWorkQueue);

        if (Irp->PendingReturned)
            IoMarkIrpPending(Irp);
        return STATUS_MORE_PROCESSING_REQUIRED;
    }

    if (Ext != NULL)
        Ndis6ReleasePowerWork(Ext);

    if (Irp->PendingReturned)
        IoMarkIrpPending(Irp);

    return STATUS_CONTINUE_COMPLETION;
}

/* D3: call the miniport's DevicePnPEventNotifyHandler for a given
 * event. The handler was installed via the driver's characteristics
 * struct; most drivers tolerate NULL data. */
VOID
Ndis6NotifyMiniportDevicePnPEvent(
    _In_ PNDIS6_ADAPTER_EXT     Ext,
    _In_ NDIS_DEVICE_PNP_EVENT  Event)
{
    NET_DEVICE_PNP_EVENT NetEvent;

    if (Ext == NULL || !Ext->Initialized || Ext->Adapter == NULL)
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

    Ndis6FilterDispatchDevicePnPEvent(Ext->Adapter, &NetEvent);
}

VOID
Ndis6FilterTerminalDevicePnPEvent(
    _In_ PLOGICAL_ADAPTER Adapter,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PNDIS6_ADAPTER_EXT Ext;

    if (Adapter == NULL || NetDevicePnPEvent == NULL)
        return;

    Ext = NDIS6_EXT(Adapter);
    if (Ext == NULL || !Ext->Initialized || Ext->DriverBlock == NULL ||
        Ext->DriverBlock->Characteristics.DevicePnPEventNotifyHandler == NULL ||
        Ext->MiniportAdapterContext == NULL)
    {
        return;
    }

    Ext->DriverBlock->Characteristics.DevicePnPEventNotifyHandler(Ext->MiniportAdapterContext, NetDevicePnPEvent);
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

    /* NDIS power sequencing has two separate contracts: Pause/Restart gates
     * network traffic, while OID_PNP_SET_POWER makes the miniport save or
     * restore device state. For a numerically lower-power D-state, perform
     * both before the bus loses power. For a higher-power state, let the bus
     * restore power first and perform the miniport transition in completion.
     * The embedded adapter work item keeps this correct even under pool
     * pressure and guarantees every miniport callback runs at PASSIVE_LEVEL. */
    if (Ext != NULL &&
        Stack->MajorFunction == IRP_MJ_POWER &&
        Stack->MinorFunction == IRP_MN_SET_POWER &&
        Stack->Parameters.Power.Type == DevicePowerState)
    {
        DEVICE_POWER_STATE NewState = Stack->Parameters.Power.State.DeviceState;

        if (Ext->Initialized &&
            NewState >= PowerDeviceD0 &&
            NewState <= PowerDeviceD3 &&
            NewState > Ext->DevicePowerState)
        {
            if (KeGetCurrentIrql() != PASSIVE_LEVEL)
            {
                if (Ndis6ReservePowerWork(Ext))
                {
                    Ext->PowerWorkOperation = Ndis6PowerPauseAndForward;
                    Ext->PowerTargetState = NewState;
                    Ext->PowerLowerDevice = LowerDevice;
                    Ext->PowerIrp = Irp;
                    ExInitializeWorkItem(&Ext->PowerWorkItem, Ndis6PowerWorker, Adapter);
                    IoMarkIrpPending(Irp);
                    ExQueueWorkItem(&Ext->PowerWorkItem, DelayedWorkQueue);
                    return STATUS_PENDING;
                }

                DbgPrint("NDIS6: serialized power work unexpectedly busy at IRQL %lu\n", (ULONG)KeGetCurrentIrql());
            }
            else
            {
                Ndis6ApplyMiniportPowerState(Adapter, Ndis6PowerPauseAndForward, NewState);
            }
        }
        else if (Ext->Initialized &&
                 NewState >= PowerDeviceD0 &&
                 NewState <= PowerDeviceD3 &&
                 NewState < Ext->DevicePowerState)
        {
            if (Ndis6ReservePowerWork(Ext))
            {
                Ext->PowerWorkOperation = Ndis6PowerResumeAndComplete;
                Ext->PowerTargetState = NewState;
                Ext->PowerLowerDevice = LowerDevice;
                Ext->PowerIrp = NULL;
                IoCopyCurrentIrpStackLocationToNext(Irp);
                IoSetCompletionRoutine(Irp, Ndis6PowerCompletionRoutine, Adapter, TRUE, TRUE, TRUE);
                PoStartNextPowerIrp(Irp);
                return PoCallDriver(LowerDevice, Irp);
            }

            DbgPrint("NDIS6: serialized power-up work unexpectedly busy at IRQL %lu\n", (ULONG)KeGetCurrentIrql());
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
