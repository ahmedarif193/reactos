/*
 * PROJECT:         ReactOS PCI bus driver
 * FILE:            pdo.c
 * PURPOSE:         Child device object dispatch routines
 * PROGRAMMERS:     Casper S. Hornstrup (chorns@users.sourceforge.net)
 * UPDATE HISTORY:
 *      10-09-2001  CSH  Created
 */

#include "pci.h"

#include <initguid.h>
#include <wdmguid.h>

DEFINE_GUID(GUID_REACTOS_PCI_ROOT_BUS_INTERFACE,
            0xd7b6f1ba, 0x9f5a, 0x4d9d, 0x9d, 0xfe, 0x5d, 0x4a, 0x17, 0xb8, 0xc5, 0xa1);

#define NDEBUG
#include <debug.h>

#if 0
#define DBGPRINT(...) DbgPrint(__VA_ARGS__)
#else
#define DBGPRINT(...)
#endif

static
BOOLEAN
PciPdoIsBusInRange(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension)
{
    USHORT Segment = 0;
    PFDO_DEVICE_EXTENSION FdoExtension;
    ULONG BusNumber;

    if (!DeviceExtension || !DeviceExtension->Fdo)
        return TRUE;

    FdoExtension = (PFDO_DEVICE_EXTENSION)DeviceExtension->Fdo->DeviceExtension;
    if (!FdoExtension)
        return TRUE;

    Segment = FdoExtension->BusSegment;
    BusNumber = DeviceExtension->PciDevice->BusNumber;
    if (FdoExtension->BusRangeStart <= FdoExtension->BusRangeEnd)
    {
        if (BusNumber < FdoExtension->BusRangeStart ||
            BusNumber > FdoExtension->BusRangeEnd)
        {
            DPRINT1("PCI: Skipping config access for seg %u bus %lu outside firmware range [%lu-%lu].\n",
                    Segment,
                    BusNumber,
                    FdoExtension->BusRangeStart,
                    FdoExtension->BusRangeEnd);
            return FALSE;
        }
    }

    return TRUE;
}

static
ULONG
PciPdoGetBusData(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    if (!PciPdoIsBusInRange(DeviceExtension))
        return 0;

    return HalGetBusData(PCIConfiguration,
                         DeviceExtension->PciDevice->BusNumber,
                         DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                         Buffer,
                         Length);
}

static
ULONG
PciPdoGetBusDataByOffset(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    if (!PciPdoIsBusInRange(DeviceExtension))
        return 0;

    return HalGetBusDataByOffset(PCIConfiguration,
                                 DeviceExtension->PciDevice->BusNumber,
                                 DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                 Buffer,
                                 Offset,
                                 Length);
}

static
ULONG
PciPdoSetBusDataByOffset(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    if (!PciPdoIsBusInRange(DeviceExtension))
        return 0;

    return HalSetBusDataByOffset(PCIConfiguration,
                                 DeviceExtension->PciDevice->BusNumber,
                                 DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                                 Buffer,
                                 Offset,
                                 Length);
}

#define PCI_CAP_PTR_FIRST      0x40
#define PCI_CAP_MAX_ITERATIONS 48

static
USHORT
PciPdoGetSegment(
    _In_opt_ PPDO_DEVICE_EXTENSION DeviceExtension)
{
    PFDO_DEVICE_EXTENSION FdoExtension;

    if (!DeviceExtension || !DeviceExtension->Fdo)
        return 0;

    FdoExtension = (PFDO_DEVICE_EXTENSION)DeviceExtension->Fdo->DeviceExtension;
    if (!FdoExtension)
        return 0;

    return FdoExtension->BusSegment;
}

static
BOOLEAN
PciPdoFindCapability(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR CapabilityId,
    _Out_opt_ PUCHAR CapabilityOffset)
{
    UCHAR HeaderType;
    UCHAR CapPointer;
    USHORT Status;
    ULONG CapFieldOffset;
    ULONG BytesRead;
    UCHAR Iter;

    if (!DeviceExtension || !DeviceExtension->PciDevice)
        return FALSE;

    BytesRead = PciPdoGetBusDataByOffset(DeviceExtension,
                                         &Status,
                                         FIELD_OFFSET(PCI_COMMON_CONFIG, Status),
                                         sizeof(Status));
    if (BytesRead != sizeof(Status) ||
        !(Status & PCI_STATUS_CAP_LIST))
    {
        return FALSE;
    }

    BytesRead = PciPdoGetBusDataByOffset(DeviceExtension,
                                         &HeaderType,
                                         FIELD_OFFSET(PCI_COMMON_CONFIG, HeaderType),
                                         sizeof(HeaderType));
    if (BytesRead != sizeof(HeaderType))
        return FALSE;

    HeaderType &= PCI_HEADER_TYPE_MASK;
    CapFieldOffset = (HeaderType == PCI_CARDBUS_BRIDGE_TYPE) ?
                     PCI_CB_CAPABILITY_LIST : PCI_CAPABILITY_LIST;

    BytesRead = PciPdoGetBusDataByOffset(DeviceExtension,
                                         &CapPointer,
                                         CapFieldOffset,
                                         sizeof(CapPointer));
    if (BytesRead != sizeof(CapPointer) || CapPointer < PCI_CAP_PTR_FIRST)
        return FALSE;

    for (Iter = 0; Iter < PCI_CAP_MAX_ITERATIONS; ++Iter)
    {
        UCHAR Header[2];

        BytesRead = PciPdoGetBusDataByOffset(DeviceExtension,
                                             Header,
                                             CapPointer,
                                             sizeof(Header));
        if (BytesRead != sizeof(Header))
            break;

        if (Header[0] == CapabilityId)
        {
            if (CapabilityOffset)
                *CapabilityOffset = CapPointer;
            return TRUE;
        }

        if (Header[1] < PCI_CAP_PTR_FIRST || Header[1] == CapPointer)
            break;

        CapPointer = Header[1];
    }

    return FALSE;
}

static
VOID
PciPdoCacheMsiInfo(
    _Inout_ PPDO_DEVICE_EXTENSION DeviceExtension)
{
    PPCI_DEVICE Device;
    UCHAR Offset;
    USHORT Control;
    ULONG TableInfo;
    ULONG BytesRead;

    Device = DeviceExtension ? DeviceExtension->PciDevice : NULL;
    if (!Device)
        return;

    if (Device->MsiCapability == 0)
    {
        if (PciPdoFindCapability(DeviceExtension, PCI_CAP_ID_MSI, &Offset))
            Device->MsiCapability = Offset;
    }

    if (Device->MsiCapability)
    {
        BytesRead = PciPdoGetBusDataByOffset(DeviceExtension,
                                             &Control,
                                             Device->MsiCapability + PCI_MSI_FLAGS,
                                             sizeof(Control));
        if (BytesRead == sizeof(Control))
        {
            Device->MsiControl = Control;
            Device->MsiMaxCount = (UCHAR)(1 << ((Control & PCI_MSI_FLAGS_QMASK) >> 1));
            if (Device->MsiMaxCount == 0)
                Device->MsiMaxCount = 1;
            if (Device->MsiMaxCount > 32)
                Device->MsiMaxCount = 32;
        }
    }

    if (Device->MsixCapability == 0)
    {
        if (PciPdoFindCapability(DeviceExtension, PCI_CAP_ID_MSIX, &Offset))
            Device->MsixCapability = Offset;
    }

    if (Device->MsixCapability)
    {
        BytesRead = PciPdoGetBusDataByOffset(DeviceExtension,
                                             &Control,
                                             Device->MsixCapability + PCI_MSIX_FLAGS,
                                             sizeof(Control));
        if (BytesRead == sizeof(Control))
        {
            Device->MsixControl = Control;
            Device->MsixTableSize = (USHORT)((Control & PCI_MSIX_FLAGS_TABLE_SIZE) + 1);
        }

        BytesRead = PciPdoGetBusDataByOffset(DeviceExtension,
                                             &TableInfo,
                                             Device->MsixCapability + PCI_MSIX_TABLE,
                                             sizeof(TableInfo));
        if (BytesRead == sizeof(TableInfo))
        {
            Device->MsixTableBir = (UCHAR)(TableInfo & PCI_MSIX_TABLE_BIR_MASK);
            Device->MsixTableOffset = TableInfo & PCI_MSIX_TABLE_OFFSET_MASK;
        }

        BytesRead = PciPdoGetBusDataByOffset(DeviceExtension,
                                             &TableInfo,
                                             Device->MsixCapability + PCI_MSIX_PBA,
                                             sizeof(TableInfo));
        if (BytesRead == sizeof(TableInfo))
        {
            Device->MsixPbaBir = (UCHAR)(TableInfo & PCI_MSIX_TABLE_BIR_MASK);
            Device->MsixPbaOffset = TableInfo & PCI_MSIX_TABLE_OFFSET_MASK;
        }
    }
}

#define PCI_ADDRESS_MEMORY_ADDRESS_MASK_64     0xfffffffffffffff0ull
#define PCI_ADDRESS_IO_ADDRESS_MASK_64         0xfffffffffffffffcull

typedef struct _PCI_MSIX_TABLE_ENTRY
{
    ULONG MessageAddressLow;
    ULONG MessageAddressHigh;
    ULONG MessageData;
    ULONG VectorControl;
} PCI_MSIX_TABLE_ENTRY, *PPCI_MSIX_TABLE_ENTRY;

typedef struct _PCI_MSIX_MESSAGE_INFO
{
    ULONG Vector;
    KAFFINITY Affinity;
} PCI_MSIX_MESSAGE_INFO, *PPCI_MSIX_MESSAGE_INFO;

static
VOID
PciPdoApplyInterruptPolicyFromKey(
    _In_ HANDLE KeyHandle,
    _Inout_ PBOOLEAN AllowMsi,
    _Inout_ PBOOLEAN AllowMsix)
{
    UNICODE_STRING ValueName;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION ValueInfo = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    ULONG ResultLength;
    NTSTATUS Status;

    if (!KeyHandle)
        return;

    RtlInitUnicodeString(&ValueName, L"AllowMSI");
    Status = ZwQueryValueKey(KeyHandle,
                             &ValueName,
                             KeyValuePartialInformation,
                             ValueInfo,
                             sizeof(Buffer),
                             &ResultLength);
    if (NT_SUCCESS(Status) &&
        ValueInfo->Type == REG_DWORD &&
        ValueInfo->DataLength == sizeof(ULONG))
    {
        *AllowMsi = (*(PULONG)ValueInfo->Data) != 0;
    }

    RtlInitUnicodeString(&ValueName, L"AllowMSIX");
    Status = ZwQueryValueKey(KeyHandle,
                             &ValueName,
                             KeyValuePartialInformation,
                             ValueInfo,
                             sizeof(Buffer),
                             &ResultLength);
    if (NT_SUCCESS(Status) &&
        ValueInfo->Type == REG_DWORD &&
        ValueInfo->DataLength == sizeof(ULONG))
    {
        *AllowMsix = (*(PULONG)ValueInfo->Data) != 0;
    }
}

static
VOID
PciPdoDetermineInterruptPolicy(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _Out_ PBOOLEAN AllowMsi,
    _Out_ PBOOLEAN AllowMsix)
{
    PFDO_DEVICE_EXTENSION FdoExtension = NULL;
    BOOLEAN UseMsi = PciMsiEnabledByPolicy;
    BOOLEAN UseMsix = PciMsixEnabledByPolicy;
    HANDLE KeyHandle;

    if (!DeviceExtension || !DeviceExtension->PciDevice)
    {
        *AllowMsi = UseMsi;
        *AllowMsix = UseMsix;
        return;
    }

    if (DeviceExtension->Fdo)
        FdoExtension = (PFDO_DEVICE_EXTENSION)DeviceExtension->Fdo->DeviceExtension;

    if (NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceExtension->PciDevice->Pdo,
                                           PLUGPLAY_REGKEY_DEVICE,
                                           KEY_READ,
                                           &KeyHandle)))
    {
        PciPdoApplyInterruptPolicyFromKey(KeyHandle, &UseMsi, &UseMsix);
        ZwClose(KeyHandle);
    }

    ULONG Length = 0;
    NTSTATUS Status = IoGetDeviceProperty(DeviceExtension->PciDevice->Pdo,
                                          DevicePropertyDriverKeyName,
                                          0,
                                          NULL,
                                          &Length);
    if (Status == STATUS_BUFFER_TOO_SMALL && Length)
    {
        PWCHAR Buffer = ExAllocatePoolWithTag(PagedPool, Length, TAG_PCI);
        if (Buffer)
        {
            Status = IoGetDeviceProperty(DeviceExtension->PciDevice->Pdo,
                                         DevicePropertyDriverKeyName,
                                         Length,
                                         Buffer,
                                         &Length);
            if (NT_SUCCESS(Status))
            {
                HANDLE ServiceHandle;
                UNICODE_STRING DriverKeyPath;
                OBJECT_ATTRIBUTES ObjectAttributes;

                DriverKeyPath.Buffer = Buffer;
                if (Length >= sizeof(WCHAR))
                {
                    SIZE_T PathChars = (Length / sizeof(WCHAR)) - 1;
                    DriverKeyPath.Length = (USHORT)(PathChars * sizeof(WCHAR));
                }
                else
                {
                    DriverKeyPath.Length = 0;
                }
                DriverKeyPath.MaximumLength = (USHORT)Length;

                InitializeObjectAttributes(&ObjectAttributes,
                                           &DriverKeyPath,
                                           OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                           NULL,
                                           NULL);
                if (NT_SUCCESS(ZwOpenKey(&ServiceHandle, KEY_READ, &ObjectAttributes)))
                {
                    PciPdoApplyInterruptPolicyFromKey(ServiceHandle, &UseMsi, &UseMsix);
                    ZwClose(ServiceHandle);
                }
            }
            ExFreePoolWithTag(Buffer, TAG_PCI);
        }
    }

    if (FdoExtension && !FdoExtension->MsiSupported)
    {
        if (!FdoExtension->MsiDiagLogged)
        {
            DPRINT1("PCI: Disabling MSI/MSI-X on seg %u bus %lu due to _OSC status 0x%lx grant 0x%lx\n",
                    FdoExtension->BusSegment,
                    FdoExtension->BusNumber,
                    FdoExtension->OscStatusFlags,
                    FdoExtension->OscControlGranted);
            if (FdoExtension->OscMasked && !FdoExtension->MsiMaskLogged)
            {
                DPRINT1("PCI: _OSC masked controls 0x%lx on seg %u bus %lu\n",
                        FdoExtension->OscMasked,
                        FdoExtension->BusSegment,
                        FdoExtension->BusNumber);
                FdoExtension->MsiMaskLogged = TRUE;
            }
            FdoExtension->MsiDiagLogged = TRUE;
        }
        UseMsi = FALSE;
        UseMsix = FALSE;
    }
    else if (FdoExtension && FdoExtension->OscMasked && !FdoExtension->MsiMaskLogged)
    {
        DPRINT1("PCI: _OSC masked controls 0x%lx on seg %u bus %lu (status 0x%lx grant 0x%lx) but MSI remains allowed\n",
                FdoExtension->OscMasked,
                FdoExtension->BusSegment,
                FdoExtension->BusNumber,
                FdoExtension->OscStatusFlags,
                FdoExtension->OscControlGranted);
        FdoExtension->MsiMaskLogged = TRUE;
    }

    *AllowMsi = UseMsi;
    *AllowMsix = UseMsix;
}

static
ULONG
PciPdoSelectDestinationId(
    _In_ KAFFINITY Affinity)
{
    ULONG Index = 0;
    KAFFINITY Mask = Affinity;

    if (Mask == 0)
        Mask = KeQueryActiveProcessors();

    while ((Mask & 1) == 0)
    {
        Mask >>= 1;
        Index++;
    }

    return Index;
}

static
BOOLEAN
PciPdoGetMsixTableAddress(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _Out_ PPHYSICAL_ADDRESS TableAddress)
{
    PPCI_DEVICE Device;
    ULONGLONG BarValue;

    if (!DeviceExtension || !TableAddress)
        return FALSE;

    Device = DeviceExtension->PciDevice;
    if (!Device ||
        Device->MsixCapability == 0 ||
        Device->MsixTableBir >= PCI_TYPE0_ADDRESSES)
    {
        return FALSE;
    }

    BarValue = Device->PciConfig.u.type0.BaseAddresses[Device->MsixTableBir];
    if (BarValue == 0 || (BarValue & PCI_ADDRESS_IO_SPACE))
        return FALSE;

    if ((BarValue & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT)
    {
        ULONGLONG HighPart;

        HighPart = Device->PciConfig.u.type0.BaseAddresses[Device->MsixTableBir + 1];
        BarValue = (HighPart << 32) | (BarValue & PCI_ADDRESS_MEMORY_ADDRESS_MASK_64);
    }
    else
    {
        BarValue &= PCI_ADDRESS_MEMORY_ADDRESS_MASK_64;
    }

    TableAddress->QuadPart = BarValue + Device->MsixTableOffset;
    return TRUE;
}

static
NTSTATUS
PciPdoEnableMsi(
    _Inout_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR RawDescriptor,
    _In_opt_ PCM_PARTIAL_RESOURCE_DESCRIPTOR TranslatedDescriptor)
{
    PPCI_DEVICE Device;
    ULONG MessageAddressLow;
    ULONG MessageAddressHigh;
    USHORT MessageData;
    USHORT Control;
    BOOLEAN Is64Bit;
    ULONG MessageCount;
    ULONG DestId;
    KAFFINITY Affinity;
    ULONG Vector;

    Device = DeviceExtension->PciDevice;
    if (!Device || Device->MsiCapability == 0 || !RawDescriptor)
        return STATUS_NOT_SUPPORTED;

    MessageCount = 1;
    if (RawDescriptor->u.MessageInterrupt.Raw.MessageCount)
        MessageCount = RawDescriptor->u.MessageInterrupt.Raw.MessageCount;

    if (Device->MsiMaxCount != 0 && MessageCount > Device->MsiMaxCount)
        MessageCount = Device->MsiMaxCount;

    Affinity = TranslatedDescriptor ?
               TranslatedDescriptor->u.MessageInterrupt.Translated.Affinity :
               KeQueryActiveProcessors();
    Vector = TranslatedDescriptor ?
             TranslatedDescriptor->u.MessageInterrupt.Translated.Vector :
             RawDescriptor->u.MessageInterrupt.Raw.Vector;

    DestId = PciPdoSelectDestinationId(Affinity);

    MessageAddressLow = 0xFEE00000 | (DestId << 12);
    MessageAddressHigh = 0;
    MessageData = (USHORT)(Vector & 0xFF);

    Control = Device->MsiControl;
    Control &= ~(PCI_MSI_FLAGS_QSIZE | PCI_MSI_FLAGS_ENABLE);
    {
        UCHAR EnableCount = 0;
        while ((1U << EnableCount) < MessageCount && EnableCount < 5)
            ++EnableCount;
        Control |= (EnableCount << 4);
    }
    Control |= PCI_MSI_FLAGS_ENABLE;

    Is64Bit = (Device->MsiControl & PCI_MSI_FLAGS_64BIT) != 0;

    PciPdoSetBusDataByOffset(DeviceExtension,
                             &MessageAddressLow,
                             Device->MsiCapability + PCI_MSI_ADDRESS_LO,
                             sizeof(MessageAddressLow));
    if (Is64Bit)
    {
        PciPdoSetBusDataByOffset(DeviceExtension,
                                 &MessageAddressHigh,
                                 Device->MsiCapability + PCI_MSI_ADDRESS_HI,
                                 sizeof(MessageAddressHigh));
        PciPdoSetBusDataByOffset(DeviceExtension,
                                 &MessageData,
                                 Device->MsiCapability + PCI_MSI_DATA_64,
                                 sizeof(MessageData));
    }
    else
    {
        PciPdoSetBusDataByOffset(DeviceExtension,
                                 &MessageData,
                                 Device->MsiCapability + PCI_MSI_DATA_32,
                                 sizeof(MessageData));
    }
    PciPdoSetBusDataByOffset(DeviceExtension,
                             &Control,
                             Device->MsiCapability + PCI_MSI_FLAGS,
                             sizeof(Control));

    Device->MsiControl = Control;
    return STATUS_SUCCESS;
}

static
NTSTATUS
PciPdoEnableMsix(
    _Inout_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _In_reads_(MessageCount) PPCI_MSIX_MESSAGE_INFO Messages,
    _In_ ULONG MessageCount)
{
    PPCI_DEVICE Device;
    PHYSICAL_ADDRESS TableAddress;
    PVOID TableMapping = NULL;
    ULONG ProgramCount;
    ULONG i;
    USHORT Control;
    NTSTATUS Status;

    Device = DeviceExtension->PciDevice;
    if (!Device || Device->MsixCapability == 0 || !Messages || MessageCount == 0)
        return STATUS_INVALID_PARAMETER;

    if (!PciPdoGetMsixTableAddress(DeviceExtension, &TableAddress))
        return STATUS_INVALID_DEVICE_STATE;

    ProgramCount = MessageCount;
    if (Device->MsixTableSize != 0 && ProgramCount > Device->MsixTableSize)
        ProgramCount = Device->MsixTableSize;

    Control = Device->MsixControl | PCI_MSIX_FLAGS_MASKALL;
    PciPdoSetBusDataByOffset(DeviceExtension,
                             &Control,
                             Device->MsixCapability + PCI_MSIX_FLAGS,
                             sizeof(Control));

    TableMapping = MmMapIoSpace(TableAddress,
                                ProgramCount * sizeof(PCI_MSIX_TABLE_ENTRY),
                                MmNonCached);
    if (!TableMapping)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (i = 0; i < ProgramCount; ++i)
    {
        ULONG AddressLow;
        ULONG AddressHigh = 0;
        USHORT Data;
        KAFFINITY Affinity;
        ULONG Vector;
        ULONG DestId;
        PPCI_MSIX_TABLE_ENTRY Entry;

        Affinity = Messages[i].Affinity;
        if (Affinity == 0)
            Affinity = KeQueryActiveProcessors();

        Vector = Messages[i].Vector;
        DestId = PciPdoSelectDestinationId(Affinity);

        AddressLow = 0xFEE00000 | (DestId << 12);
        Data = (USHORT)(Vector & 0xFF);

        Entry = (PPCI_MSIX_TABLE_ENTRY)((PUCHAR)TableMapping + (i * sizeof(PCI_MSIX_TABLE_ENTRY)));
        Entry->MessageAddressLow = AddressLow;
        Entry->MessageAddressHigh = AddressHigh;
        Entry->MessageData = Data;
        Entry->VectorControl = 0;
    }

    Control |= PCI_MSIX_FLAGS_ENABLE;
    Control &= ~PCI_MSIX_FLAGS_MASKALL;
    PciPdoSetBusDataByOffset(DeviceExtension,
                             &Control,
                             Device->MsixCapability + PCI_MSIX_FLAGS,
                             sizeof(Control));
    Device->MsixControl = Control;
    Status = STATUS_SUCCESS;

    MmUnmapIoSpace(TableMapping, ProgramCount * sizeof(PCI_MSIX_TABLE_ENTRY));
    return Status;
}

/*** PRIVATE *****************************************************************/

static NTSTATUS
PdoQueryDeviceText(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    UNICODE_STRING String;
    NTSTATUS Status;

    DPRINT("Called\n");

    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    switch (IrpSp->Parameters.QueryDeviceText.DeviceTextType)
    {
        case DeviceTextDescription:
            Status = PciDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                               &DeviceExtension->DeviceDescription,
                                               &String);

            DPRINT("DeviceTextDescription\n");
            Irp->IoStatus.Information = (ULONG_PTR)String.Buffer;
            break;

        case DeviceTextLocationInformation:
            Status = PciDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                               &DeviceExtension->DeviceLocation,
                                               &String);

            DPRINT("DeviceTextLocationInformation\n");
            Irp->IoStatus.Information = (ULONG_PTR)String.Buffer;
            break;

        default:
            Irp->IoStatus.Information = 0;
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    return Status;
}


static NTSTATUS
PdoQueryId(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    UNICODE_STRING String;
    NTSTATUS Status;
    USHORT Segment = PciPdoGetSegment((PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension);

    DPRINT("Called (seg %u)\n", Segment);

    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

//    Irp->IoStatus.Information = 0;

    Status = STATUS_SUCCESS;

    RtlInitUnicodeString(&String, NULL);

    switch (IrpSp->Parameters.QueryId.IdType)
    {
        case BusQueryDeviceID:
            Status = PciDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                               &DeviceExtension->DeviceID,
                                               &String);

            DPRINT("DeviceID: %S\n", String.Buffer);

            Irp->IoStatus.Information = (ULONG_PTR)String.Buffer;
            break;

        case BusQueryHardwareIDs:
            Status = PciDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                               &DeviceExtension->HardwareIDs,
                                               &String);

            Irp->IoStatus.Information = (ULONG_PTR)String.Buffer;
            break;

        case BusQueryCompatibleIDs:
            Status = PciDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                               &DeviceExtension->CompatibleIDs,
                                               &String);

            Irp->IoStatus.Information = (ULONG_PTR)String.Buffer;
            break;

        case BusQueryInstanceID:
            Status = PciDuplicateUnicodeString(RTL_DUPLICATE_UNICODE_STRING_NULL_TERMINATE,
                                               &DeviceExtension->InstanceID,
                                               &String);

            DPRINT("InstanceID: %S\n", String.Buffer);

            Irp->IoStatus.Information = (ULONG_PTR)String.Buffer;
            break;

        case BusQueryDeviceSerialNumber:
        default:
            Status = STATUS_NOT_IMPLEMENTED;
    }

    return Status;
}


static NTSTATUS
PdoQueryBusInformation(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    PPNP_BUS_INFORMATION BusInformation;
    USHORT Segment;

    UNREFERENCED_PARAMETER(IrpSp);
    Segment = PciPdoGetSegment((PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension);
    DPRINT("Called (seg %u)\n", Segment);

    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    BusInformation = ExAllocatePoolWithTag(PagedPool, sizeof(PNP_BUS_INFORMATION), TAG_PCI);
    Irp->IoStatus.Information = (ULONG_PTR)BusInformation;
    if (BusInformation != NULL)
    {
        BusInformation->BusTypeGuid = GUID_BUS_TYPE_PCI;
        BusInformation->LegacyBusType = PCIBus;
        BusInformation->BusNumber = DeviceExtension->PciDevice->BusNumber;

        return STATUS_SUCCESS;
    }

    return STATUS_INSUFFICIENT_RESOURCES;
}


static NTSTATUS
PdoQueryCapabilities(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    PDEVICE_CAPABILITIES DeviceCapabilities;
    ULONG DeviceNumber, FunctionNumber;
    USHORT Segment;

    UNREFERENCED_PARAMETER(Irp);
    Segment = PciPdoGetSegment((PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension);
    DPRINT("Called (seg %u)\n", Segment);

    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    DeviceCapabilities = IrpSp->Parameters.DeviceCapabilities.Capabilities;

    if (DeviceCapabilities->Version != 1)
        return STATUS_UNSUCCESSFUL;

    DeviceNumber = DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber;
    FunctionNumber = DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber;

    DeviceCapabilities->UniqueID = FALSE;
    DeviceCapabilities->Address = ((DeviceNumber << 16) & 0xFFFF0000) + (FunctionNumber & 0xFFFF);
    DeviceCapabilities->UINumber = MAXULONG; /* FIXME */

    return STATUS_SUCCESS;
}

static BOOLEAN
PdoReadPciBar(PPDO_DEVICE_EXTENSION DeviceExtension,
              ULONG Offset,
              PULONG OriginalValue,
              PULONG NewValue)
{
    ULONG Size;
    ULONG AllOnes;
    USHORT Segment = PciPdoGetSegment(DeviceExtension);

    /* Read the original value */
    Size = PciPdoGetBusDataByOffset(DeviceExtension,
                                    OriginalValue,
                                    Offset,
                                    sizeof(ULONG));
    if (Size != sizeof(ULONG))
    {
        DPRINT1("Wrong size %lu (seg %u)\n", Size, Segment);
        return FALSE;
    }

    /* Write all ones to determine which bits are held to zero */
    AllOnes = MAXULONG;
    Size = PciPdoSetBusDataByOffset(DeviceExtension,
                                    &AllOnes,
                                    Offset,
                                    sizeof(ULONG));
    if (Size != sizeof(ULONG))
    {
        DPRINT1("Wrong size %lu (seg %u)\n", Size, Segment);
        return FALSE;
    }

    /* Get the range length */
    Size = PciPdoGetBusDataByOffset(DeviceExtension,
                                    NewValue,
                                    Offset,
                                    sizeof(ULONG));
    if (Size != sizeof(ULONG))
    {
        DPRINT1("Wrong size %lu (seg %u)\n", Size, Segment);
        return FALSE;
    }

    /* Restore original value */
    Size = PciPdoSetBusDataByOffset(DeviceExtension,
                                    OriginalValue,
                                    Offset,
                                    sizeof(ULONG));
    if (Size != sizeof(ULONG))
    {
        DPRINT1("Wrong size %lu (seg %u)\n", Size, Segment);
        return FALSE;
    }

    return TRUE;
}

static BOOLEAN
PdoGetRangeLength(PPDO_DEVICE_EXTENSION DeviceExtension,
                  UCHAR Bar,
                  PULONGLONG Base,
                  PULONGLONG Length,
                  PULONG Flags,
                  PUCHAR NextBar,
                  PULONGLONG MaximumAddress)
{
    union {
        struct {
            ULONG Bar0;
            ULONG Bar1;
        } Bars;
        ULONGLONG Bar;
    } OriginalValue;
    union {
        struct {
            ULONG Bar0;
            ULONG Bar1;
        } Bars;
        ULONGLONG Bar;
    } NewValue;
    ULONG Offset;
    ULONGLONG Size;

    /* Compute the offset of this BAR in PCI config space */
    Offset = 0x10 + Bar * 4;

    /* Assume this is a 32-bit BAR until we find wrong */
    *NextBar = Bar + 1;

    /* Initialize BAR values to zero */
    OriginalValue.Bar = 0ULL;
    NewValue.Bar = 0ULL;

    /* Read the first BAR */
    if (!PdoReadPciBar(DeviceExtension, Offset,
                       &OriginalValue.Bars.Bar0,
                       &NewValue.Bars.Bar0))
    {
        return FALSE;
    }

    /* Check if this is a memory BAR */
    if (!(OriginalValue.Bars.Bar0 & PCI_ADDRESS_IO_SPACE))
    {
        /* Write the maximum address if the caller asked for it */
        if (MaximumAddress != NULL)
        {
            if ((OriginalValue.Bars.Bar0 & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_32BIT)
            {
                *MaximumAddress = 0x00000000FFFFFFFFULL;
            }
            else if ((OriginalValue.Bars.Bar0 & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_20BIT)
            {
                *MaximumAddress = 0x00000000000FFFFFULL;
            }
            else if ((OriginalValue.Bars.Bar0 & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT)
            {
                *MaximumAddress = 0xFFFFFFFFFFFFFFFFULL;
            }
        }

        /* Check if this is a 64-bit BAR */
        if ((OriginalValue.Bars.Bar0 & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT)
        {
            /* We've now consumed the next BAR too */
            *NextBar = Bar + 2;

            /* Read the next BAR */
            if (!PdoReadPciBar(DeviceExtension, Offset + 4,
                               &OriginalValue.Bars.Bar1,
                               &NewValue.Bars.Bar1))
            {
                return FALSE;
            }
        }
    }
    else
    {
        /* Write the maximum I/O port address */
        if (MaximumAddress != NULL)
        {
            *MaximumAddress = 0x00000000FFFFFFFFULL;
        }
    }

    if (NewValue.Bar == 0)
    {
        DPRINT("Unused address register\n");
        *Base = 0;
        *Length = 0;
        *Flags = 0;
        return TRUE;
    }

    *Base = ((OriginalValue.Bar & PCI_ADDRESS_IO_SPACE)
             ? (OriginalValue.Bar & PCI_ADDRESS_IO_ADDRESS_MASK_64)
             : (OriginalValue.Bar & PCI_ADDRESS_MEMORY_ADDRESS_MASK_64));

    Size = (NewValue.Bar & PCI_ADDRESS_IO_SPACE)
           ? (NewValue.Bar & PCI_ADDRESS_IO_ADDRESS_MASK_64)
           : (NewValue.Bar & PCI_ADDRESS_MEMORY_ADDRESS_MASK_64);
    *Length = Size & ~(Size - 1);

    *Flags = (NewValue.Bar & PCI_ADDRESS_IO_SPACE)
             ? (NewValue.Bar & ~PCI_ADDRESS_IO_ADDRESS_MASK_64)
             : (NewValue.Bar & ~PCI_ADDRESS_MEMORY_ADDRESS_MASK_64);

    return TRUE;
}


static NTSTATUS
PdoQueryResourceRequirements(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    PCI_COMMON_CONFIG PciConfig;
    PIO_RESOURCE_REQUIREMENTS_LIST ResourceList;
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Size;
    ULONG ListSize;
    UCHAR Bar;
    ULONGLONG Base;
    ULONGLONG Length;
    ULONG Flags;
    ULONGLONG MaximumAddress;
    BOOLEAN HasMsi;
    BOOLEAN HasMsix;
    BOOLEAN AllowMsi;
    BOOLEAN AllowMsix;
    UCHAR InterruptPin;
    ULONG MsixMessageCount;
    UCHAR MsiMessageCount;
    ULONG BaseDescriptorCount;
    IO_RESOURCE_DESCRIPTOR BaseDescriptors[32];
    ULONG RequirementsBusNumber;
    BOOLEAN MsixOption;
    BOOLEAN MsiOption;
    BOOLEAN LegacyOption;
    ULONG OptionCount;
    SIZE_T AllocationSize;
    PUCHAR ListPtr;
    ULONG OptionIndex;
    ULONG CurrentCount;
    typedef enum _PCI_INTERRUPT_REQUIREMENT {
        PciRequirementNone = 0,
        PciRequirementLegacy,
        PciRequirementMsi,
        PciRequirementMsix,
    } PCI_INTERRUPT_REQUIREMENT;
    PCI_INTERRUPT_REQUIREMENT Options[3];
    USHORT Segment;
    PFDO_DEVICE_EXTENSION FdoExtension;

    UNREFERENCED_PARAMETER(IrpSp);
    Segment = PciPdoGetSegment((PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension);
    DPRINT("PdoQueryResourceRequirements() called (seg %u)\n", Segment);

    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    FdoExtension = DeviceExtension && DeviceExtension->Fdo ? (PFDO_DEVICE_EXTENSION)DeviceExtension->Fdo->DeviceExtension : NULL;

    /* Get PCI configuration space */
    Size = PciPdoGetBusData(DeviceExtension,
                            &PciConfig,
                            PCI_COMMON_HDR_LENGTH);
    DPRINT("Size %lu (seg %u)\n", Size, Segment);
    if (Size < PCI_COMMON_HDR_LENGTH)
    {
        Irp->IoStatus.Information = 0;
        return STATUS_UNSUCCESSFUL;
    }

    DPRINT("Command register (seg %u): 0x%04hx\n", Segment, PciConfig.Command);
    HasMsi = FALSE;
    HasMsix = FALSE;
    AllowMsi = FALSE;
    AllowMsix = FALSE;

    PciPdoDetermineInterruptPolicy(DeviceExtension, &AllowMsi, &AllowMsix);
    InterruptPin = 0;
    MsixMessageCount = 0;
    MsiMessageCount = 0;

    PciPdoCacheMsiInfo(DeviceExtension);
    HasMsi = (DeviceExtension->PciDevice->MsiCapability != 0);
    HasMsix = (DeviceExtension->PciDevice->MsixCapability != 0);
    InterruptPin = PciConfig.u.type0.InterruptPin;
    if (HasMsix)
    {
        MsixMessageCount = DeviceExtension->PciDevice->MsixTableSize;
        if (MsixMessageCount == 0)
            MsixMessageCount = 1;
    }
    if (HasMsi)
    {
        MsiMessageCount = DeviceExtension->PciDevice->MsiMaxCount;
        if (MsiMessageCount == 0)
            MsiMessageCount = 1;
    }

    RequirementsBusNumber = DeviceExtension->PciDevice->BusNumber;
    BaseDescriptorCount = 0;
    RtlZeroMemory(BaseDescriptors, sizeof(BaseDescriptors));
    Descriptor = BaseDescriptors;

    /* Build non-interrupt resource descriptors once */
    if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_DEVICE_TYPE)
    {
        for (Bar = 0; Bar < PCI_TYPE0_ADDRESSES;)
        {
            if (!PdoGetRangeLength(DeviceExtension,
                                   Bar,
                                   &Base,
                                   &Length,
                                   &Flags,
                                   &Bar,
                                   &MaximumAddress))
                break;

            if (Length == 0)
            {
                DPRINT("Unused address register (seg %u)\n", Segment);
                continue;
            }

            Descriptor->Option = IO_RESOURCE_PREFERRED;
            if (Flags & PCI_ADDRESS_IO_SPACE)
            {
                Descriptor->Type = CmResourceTypePort;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_PORT_IO |
                                    CM_RESOURCE_PORT_16_BIT_DECODE |
                                    CM_RESOURCE_PORT_POSITIVE_DECODE;

                Descriptor->u.Port.Length = Length;
                Descriptor->u.Port.Alignment = 1;
                Descriptor->u.Port.MinimumAddress.QuadPart = Base;
                Descriptor->u.Port.MaximumAddress.QuadPart = Base + Length - 1;
            }
            else
            {
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                    ((Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0);

                Descriptor->u.Memory.Length = Length;
                Descriptor->u.Memory.Alignment = 1;
                Descriptor->u.Memory.MinimumAddress.QuadPart = Base;
                Descriptor->u.Memory.MaximumAddress.QuadPart = Base + Length - 1;
            }
            Descriptor++;

            Descriptor->Option = IO_RESOURCE_ALTERNATIVE;
            if (Flags & PCI_ADDRESS_IO_SPACE)
            {
                Descriptor->Type = CmResourceTypePort;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_PORT_IO |
                                    CM_RESOURCE_PORT_16_BIT_DECODE |
                                    CM_RESOURCE_PORT_POSITIVE_DECODE;

                Descriptor->u.Port.Length = Length;
                Descriptor->u.Port.Alignment = Length;
                Descriptor->u.Port.MinimumAddress.QuadPart = 0;
                Descriptor->u.Port.MaximumAddress.QuadPart = MaximumAddress;
            }
            else
            {
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                    ((Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0);

                Descriptor->u.Memory.Length = Length;
                Descriptor->u.Memory.Alignment = Length;
                Descriptor->u.Memory.MinimumAddress.QuadPart = 0;
                Descriptor->u.Memory.MaximumAddress.QuadPart = MaximumAddress;
            }
            Descriptor++;
        }

        /* FIXME: Check ROM address */
    }
    else if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_BRIDGE_TYPE)
    {
        for (Bar = 0; Bar < PCI_TYPE1_ADDRESSES;)
        {
            if (!PdoGetRangeLength(DeviceExtension,
                                   Bar,
                                   &Base,
                                   &Length,
                                   &Flags,
                                   &Bar,
                                   &MaximumAddress))
                break;

            if (Length == 0)
            {
                DPRINT("Unused address register (seg %u)\n", Segment);
                continue;
            }

            Descriptor->Option = IO_RESOURCE_PREFERRED;
            if (Flags & PCI_ADDRESS_IO_SPACE)
            {
                Descriptor->Type = CmResourceTypePort;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_PORT_IO |
                                    CM_RESOURCE_PORT_16_BIT_DECODE |
                                    CM_RESOURCE_PORT_POSITIVE_DECODE;

                Descriptor->u.Port.Length = Length;
                Descriptor->u.Port.Alignment = 1;
                Descriptor->u.Port.MinimumAddress.QuadPart = Base;
                Descriptor->u.Port.MaximumAddress.QuadPart = Base + Length - 1;
            }
            else
            {
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                    ((Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0);

                Descriptor->u.Memory.Length = Length;
                Descriptor->u.Memory.Alignment = 1;
                Descriptor->u.Memory.MinimumAddress.QuadPart = Base;
                Descriptor->u.Memory.MaximumAddress.QuadPart = Base + Length - 1;
            }
            Descriptor++;

            Descriptor->Option = IO_RESOURCE_ALTERNATIVE;
            if (Flags & PCI_ADDRESS_IO_SPACE)
            {
                Descriptor->Type = CmResourceTypePort;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_PORT_IO |
                                    CM_RESOURCE_PORT_16_BIT_DECODE |
                                    CM_RESOURCE_PORT_POSITIVE_DECODE;

                Descriptor->u.Port.Length = Length;
                Descriptor->u.Port.Alignment = Length;
                Descriptor->u.Port.MinimumAddress.QuadPart = 0;
                Descriptor->u.Port.MaximumAddress.QuadPart = MaximumAddress;
            }
            else
            {
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                    ((Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0);

                Descriptor->u.Memory.Length = Length;
                Descriptor->u.Memory.Alignment = Length;
                Descriptor->u.Memory.MinimumAddress.QuadPart = 0;
                Descriptor->u.Memory.MaximumAddress.QuadPart = MaximumAddress;
            }
            Descriptor++;
        }

        if (DeviceExtension->PciDevice->PciConfig.BaseClass == PCI_CLASS_BRIDGE_DEV)
        {
            Descriptor->Option = 0;
            Descriptor->Type = CmResourceTypeBusNumber;
            Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;

            RequirementsBusNumber =
            Descriptor->u.BusNumber.MinBusNumber =
            Descriptor->u.BusNumber.MaxBusNumber = DeviceExtension->PciDevice->PciConfig.u.type1.SecondaryBus;
            Descriptor->u.BusNumber.Length = 1;
            Descriptor->u.BusNumber.Reserved = 0;
            Descriptor++;
        }
    }
    else if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_CARDBUS_BRIDGE_TYPE)
    {
        /* FIXME: Count Cardbus bridge resources */
    }
    else
    {
        DPRINT1("Unsupported header type %d (seg %u)\n",
                PCI_CONFIGURATION_TYPE(&PciConfig),
                Segment);
    }

    BaseDescriptorCount = (ULONG)(Descriptor - BaseDescriptors);
    MsixOption = (HasMsix && AllowMsix);
    MsiOption = (HasMsi && AllowMsi);
    LegacyOption = (InterruptPin != 0);
    if (FdoExtension && FdoExtension->OscMasked)
    {
        ULONG Masked = FdoExtension->OscMasked;
        if (!AllowMsi || (Masked & HAL_ACPI_OSC_SUPPORT_MSI))
            MsiOption = FALSE;
        /* If firmware masked MSI but not MSI-X, allow MSI-X; no explicit MSI-X bit defined, so honor AllowMsix */
        if (!AllowMsix)
            MsixOption = FALSE;
    }

    if ((BaseDescriptorCount == 0) && !MsixOption && !MsiOption && !LegacyOption)
    {
        Irp->IoStatus.Information = 0;
        return STATUS_SUCCESS;
    }

    OptionCount = 0;
    if (MsixOption)
        Options[OptionCount++] = PciRequirementMsix;
    if (MsiOption)
        Options[OptionCount++] = PciRequirementMsi;
    if (LegacyOption)
        Options[OptionCount++] = PciRequirementLegacy;
    if (OptionCount == 0)
        Options[OptionCount++] = PciRequirementNone;

    AllocationSize = FIELD_OFFSET(IO_RESOURCE_REQUIREMENTS_LIST, List[0]);
    for (OptionIndex = 0; OptionIndex < OptionCount; OptionIndex++)
    {
        CurrentCount = BaseDescriptorCount +
                       ((Options[OptionIndex] == PciRequirementNone) ? 0 : 1);
        AllocationSize += FIELD_OFFSET(IO_RESOURCE_LIST, Descriptors) +
                          CurrentCount * sizeof(IO_RESOURCE_DESCRIPTOR);
    }

    ListSize = (ULONG)AllocationSize;

    DPRINT("ListSize %lu (0x%lx) (seg %u)\n", ListSize, ListSize, Segment);

    /* Allocate the resource requirements list */
    ResourceList = ExAllocatePoolWithTag(PagedPool,
                                         ListSize,
                                         TAG_PCI);
    if (ResourceList == NULL)
    {
        Irp->IoStatus.Information = 0;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(ResourceList, ListSize);
    ResourceList->ListSize = ListSize;
    ResourceList->InterfaceType = PCIBus;
    ResourceList->BusNumber = RequirementsBusNumber;
    ResourceList->SlotNumber = DeviceExtension->PciDevice->SlotNumber.u.AsULONG;
    ResourceList->AlternativeLists = OptionCount;
    ListPtr = (PUCHAR)&ResourceList->List[0];

    for (OptionIndex = 0; OptionIndex < OptionCount; OptionIndex++)
    {
        PIO_RESOURCE_LIST IoList = (PIO_RESOURCE_LIST)ListPtr;
        PIO_RESOURCE_DESCRIPTOR Dest;
        BOOLEAN IncludeInterrupt = (Options[OptionIndex] != PciRequirementNone);

        CurrentCount = BaseDescriptorCount + (IncludeInterrupt ? 1 : 0);
        IoList->Version = 1;
        IoList->Revision = 1;
        IoList->Count = CurrentCount;

        Dest = &IoList->Descriptors[0];
        if (BaseDescriptorCount)
        {
            RtlCopyMemory(Dest,
                          BaseDescriptors,
                          BaseDescriptorCount * sizeof(IO_RESOURCE_DESCRIPTOR));
            Dest += BaseDescriptorCount;
        }

        if (IncludeInterrupt)
        {
            Dest->Option = 0;
            Dest->Type = CmResourceTypeInterrupt;

            if (Options[OptionIndex] == PciRequirementLegacy)
            {
                Dest->ShareDisposition = CmResourceShareShared;
                Dest->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
                Dest->u.Interrupt.MinimumVector = 0;
                Dest->u.Interrupt.MaximumVector = 0xFF;
            }
            else
            {
                Dest->ShareDisposition = CmResourceShareDeviceExclusive;
                Dest->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE |
                              CM_RESOURCE_INTERRUPT_MESSAGE;
                Dest->u.Interrupt.MinimumVector = 1;
                Dest->u.Interrupt.MaximumVector =
                    (Options[OptionIndex] == PciRequirementMsix) ?
                        MsixMessageCount : MsiMessageCount;
            }

            Dest->u.Interrupt.AffinityPolicy = IrqPolicyMachineDefault;
            Dest->u.Interrupt.PriorityPolicy = IrqPriorityUndefined;
            Dest->u.Interrupt.TargetedProcessors = 0;
        }

        ListPtr += FIELD_OFFSET(IO_RESOURCE_LIST, Descriptors) +
                   CurrentCount * sizeof(IO_RESOURCE_DESCRIPTOR);
    }

    Irp->IoStatus.Information = (ULONG_PTR)ResourceList;

    return STATUS_SUCCESS;
}


static NTSTATUS
PdoQueryResources(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    PCI_COMMON_CONFIG PciConfig;
    PCM_RESOURCE_LIST ResourceList;
    PCM_PARTIAL_RESOURCE_LIST PartialList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG Size;
    ULONG ResCount = 0;
    ULONG ListSize;
    UCHAR Bar;
    ULONGLONG Base;
    ULONGLONG Length;
    ULONG Flags;
    USHORT Segment;

    Segment = PciPdoGetSegment((PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension);
    DPRINT("PdoQueryResources() called (seg %u)\n", Segment);

    UNREFERENCED_PARAMETER(IrpSp);
    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    /* Get PCI configuration space */
    Size = PciPdoGetBusData(DeviceExtension,
                             &PciConfig,
                             PCI_COMMON_HDR_LENGTH);
    DPRINT("Size %lu (seg %u)\n", Size, Segment);
    if (Size < PCI_COMMON_HDR_LENGTH)
    {
        Irp->IoStatus.Information = 0;
        return STATUS_UNSUCCESSFUL;
    }

    DPRINT("Command register (seg %u): 0x%04hx\n", Segment, PciConfig.Command);

    /* Count required resource descriptors */
    ResCount = 0;
    if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_DEVICE_TYPE)
    {
        for (Bar = 0; Bar < PCI_TYPE0_ADDRESSES;)
        {
            if (!PdoGetRangeLength(DeviceExtension,
                                   Bar,
                                   &Base,
                                   &Length,
                                   &Flags,
                                   &Bar,
                                   NULL))
                break;

            if (Length)
                ResCount++;
        }

        if ((PciConfig.u.type0.InterruptPin != 0) &&
            (PciConfig.u.type0.InterruptLine != 0) &&
            (PciConfig.u.type0.InterruptLine != 0xFF))
            ResCount++;
    }
    else if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_BRIDGE_TYPE)
    {
        for (Bar = 0; Bar < PCI_TYPE1_ADDRESSES;)
        {
            if (!PdoGetRangeLength(DeviceExtension,
                                   Bar,
                                   &Base,
                                   &Length,
                                   &Flags,
                                   &Bar,
                                   NULL))
                break;

            if (Length != 0)
                ResCount++;
        }

        if (DeviceExtension->PciDevice->PciConfig.BaseClass == PCI_CLASS_BRIDGE_DEV)
            ResCount++;
    }
    else if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_CARDBUS_BRIDGE_TYPE)
    {
        /* FIXME: Count Cardbus bridge resources */
    }
    else
    {
        DPRINT1("Unsupported header type %d (seg %u)\n",
                PCI_CONFIGURATION_TYPE(&PciConfig),
                Segment);
    }

    if (ResCount == 0)
    {
        Irp->IoStatus.Information = 0;
        return STATUS_SUCCESS;
    }

    /* Calculate the resource list size */
    ListSize = FIELD_OFFSET(CM_RESOURCE_LIST, List[0].PartialResourceList.PartialDescriptors) +
               ResCount * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);

    /* Allocate the resource list */
    ResourceList = ExAllocatePoolWithTag(PagedPool,
                                         ListSize,
                                         TAG_PCI);
    if (ResourceList == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(ResourceList, ListSize);
    ResourceList->Count = 1;
    ResourceList->List[0].InterfaceType = PCIBus;
    ResourceList->List[0].BusNumber = DeviceExtension->PciDevice->BusNumber;

    PartialList = &ResourceList->List[0].PartialResourceList;
    PartialList->Version = 1;
    PartialList->Revision = 1;
    PartialList->Count = ResCount;

    Descriptor = &PartialList->PartialDescriptors[0];
    if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_DEVICE_TYPE)
    {
        for (Bar = 0; Bar < PCI_TYPE0_ADDRESSES;)
        {
            if (!PdoGetRangeLength(DeviceExtension,
                                   Bar,
                                   &Base,
                                   &Length,
                                   &Flags,
                                   &Bar,
                                   NULL))
                break;

            if (Length == 0)
            {
                DPRINT("Unused address register (seg %u)\n", Segment);
                continue;
            }

            if (Flags & PCI_ADDRESS_IO_SPACE)
            {
                Descriptor->Type = CmResourceTypePort;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_PORT_IO |
                                    CM_RESOURCE_PORT_16_BIT_DECODE |
                                    CM_RESOURCE_PORT_POSITIVE_DECODE;
                Descriptor->u.Port.Start.QuadPart = (ULONGLONG)Base;
                Descriptor->u.Port.Length = Length;

                /* Enable IO space access */
                DeviceExtension->PciDevice->EnableIoSpace = TRUE;
            }
            else
            {
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                    (Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0;
                Descriptor->u.Memory.Start.QuadPart = (ULONGLONG)Base;
                Descriptor->u.Memory.Length = Length;

                /* Enable memory space access */
                DeviceExtension->PciDevice->EnableMemorySpace = TRUE;
            }

            Descriptor++;
        }

        /* Add interrupt resource */
        if ((PciConfig.u.type0.InterruptPin != 0) &&
            (PciConfig.u.type0.InterruptLine != 0) &&
            (PciConfig.u.type0.InterruptLine != 0xFF))
        {
            Descriptor->Type = CmResourceTypeInterrupt;
            Descriptor->ShareDisposition = CmResourceShareShared;
            Descriptor->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
            Descriptor->u.Interrupt.Level = PciConfig.u.type0.InterruptLine;
            Descriptor->u.Interrupt.Vector = PciConfig.u.type0.InterruptLine;
            Descriptor->u.Interrupt.Affinity = 0xFFFFFFFF;
        }

        /* Allow bus master mode */
       DeviceExtension->PciDevice->EnableBusMaster = TRUE;
    }
    else if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_BRIDGE_TYPE)
    {
        for (Bar = 0; Bar < PCI_TYPE1_ADDRESSES;)
        {
            if (!PdoGetRangeLength(DeviceExtension,
                                   Bar,
                                   &Base,
                                   &Length,
                                   &Flags,
                                   &Bar,
                                   NULL))
                break;

            if (Length == 0)
            {
                DPRINT("Unused address register\n");
                continue;
            }

            if (Flags & PCI_ADDRESS_IO_SPACE)
            {
                Descriptor->Type = CmResourceTypePort;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_PORT_IO |
                                    CM_RESOURCE_PORT_16_BIT_DECODE |
                                    CM_RESOURCE_PORT_POSITIVE_DECODE;
                Descriptor->u.Port.Start.QuadPart = (ULONGLONG)Base;
                Descriptor->u.Port.Length = Length;

                /* Enable IO space access */
                DeviceExtension->PciDevice->EnableIoSpace = TRUE;
            }
            else
            {
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                    (Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0;
                Descriptor->u.Memory.Start.QuadPart = (ULONGLONG)Base;
                Descriptor->u.Memory.Length = Length;

                /* Enable memory space access */
                DeviceExtension->PciDevice->EnableMemorySpace = TRUE;
            }

            Descriptor++;
        }

        if (DeviceExtension->PciDevice->PciConfig.BaseClass == PCI_CLASS_BRIDGE_DEV)
        {
            Descriptor->Type = CmResourceTypeBusNumber;
            Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;

            ResourceList->List[0].BusNumber =
            Descriptor->u.BusNumber.Start = DeviceExtension->PciDevice->PciConfig.u.type1.SecondaryBus;
            Descriptor->u.BusNumber.Length = 1;
            Descriptor->u.BusNumber.Reserved = 0;
        }
    }
    else if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_CARDBUS_BRIDGE_TYPE)
    {
        /* FIXME: Add Cardbus bridge resources */
    }

    Irp->IoStatus.Information = (ULONG_PTR)ResourceList;

    return STATUS_SUCCESS;
}


static VOID NTAPI
InterfaceReference(
    IN PVOID Context)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;

    DPRINT("InterfaceReference(%p)\n", Context);

    DeviceExtension = (PPDO_DEVICE_EXTENSION)((PDEVICE_OBJECT)Context)->DeviceExtension;
    InterlockedIncrement(&DeviceExtension->References);
}


static VOID NTAPI
InterfaceDereference(
    IN PVOID Context)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;

    DPRINT("InterfaceDereference(%p)\n", Context);

    DeviceExtension = (PPDO_DEVICE_EXTENSION)((PDEVICE_OBJECT)Context)->DeviceExtension;
    InterlockedDecrement(&DeviceExtension->References);
}

static TRANSLATE_BUS_ADDRESS InterfaceBusTranslateBusAddress;

static
BOOLEAN
NTAPI
InterfaceBusTranslateBusAddress(
    IN PVOID Context,
    IN PHYSICAL_ADDRESS BusAddress,
    IN ULONG Length,
    IN OUT PULONG AddressSpace,
    OUT PPHYSICAL_ADDRESS TranslatedAddress)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;

    DPRINT("InterfaceBusTranslateBusAddress(%p %p 0x%lx %p %p)\n",
           Context, BusAddress, Length, AddressSpace, TranslatedAddress);

    DeviceExtension = (PPDO_DEVICE_EXTENSION)((PDEVICE_OBJECT)Context)->DeviceExtension;

    return HalTranslateBusAddress(PCIBus,
                                  DeviceExtension->PciDevice->BusNumber,
                                  BusAddress,
                                  AddressSpace,
                                  TranslatedAddress);
}

static GET_DMA_ADAPTER InterfaceBusGetDmaAdapter;

static
PDMA_ADAPTER
NTAPI
InterfaceBusGetDmaAdapter(
    IN PVOID Context,
    IN PDEVICE_DESCRIPTION DeviceDescription,
    OUT PULONG NumberOfMapRegisters)
{
    DPRINT("InterfaceBusGetDmaAdapter(%p %p %p)\n",
           Context, DeviceDescription, NumberOfMapRegisters);
    return (PDMA_ADAPTER)HalGetAdapter(DeviceDescription, NumberOfMapRegisters);
}

static GET_SET_DEVICE_DATA InterfaceBusSetBusData;

static
ULONG
NTAPI
InterfaceBusSetBusData(
    IN PVOID Context,
    IN ULONG DataType,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    ULONG Size;

    DPRINT("InterfaceBusSetBusData(%p 0x%lx %p 0x%lx 0x%lx)\n",
           Context, DataType, Buffer, Offset, Length);

    if (DataType != PCI_WHICHSPACE_CONFIG)
    {
        DPRINT("Unknown DataType %lu\n", DataType);
        return 0;
    }

    DeviceExtension = (PPDO_DEVICE_EXTENSION)((PDEVICE_OBJECT)Context)->DeviceExtension;

    /* Get PCI configuration space */
    Size = PciPdoSetBusDataByOffset(DeviceExtension,
                                    Buffer,
                                    Offset,
                                    Length);
    return Size;
}

static
BOOLEAN
PciLookupCriticalDatabaseServiceForId(
    _In_ PCWSTR Id,
    _Outptr_result_maybenull_ PWSTR *ServiceNameOut)
{
    static const WCHAR CriticalDbPrefix[] =
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Control\\CriticalDeviceDatabase\\";

    UNICODE_STRING keyName;
    UNICODE_STRING valueName;
    OBJECT_ATTRIBUTES objectAttributes;
    PKEY_VALUE_PARTIAL_INFORMATION valueInfo = NULL;
    PWSTR keyBuffer = NULL;
    HANDLE keyHandle = NULL;
    PWSTR serviceBuffer = NULL;
    SIZE_T prefixChars = RTL_NUMBER_OF(CriticalDbPrefix) - 1;
    SIZE_T idChars;
    SIZE_T totalChars;
    SIZE_T i;
    ULONG valueLength = 0;
    NTSTATUS Status;

    if (!Id || !ServiceNameOut)
        return FALSE;

    idChars = wcslen(Id);
    if (idChars == 0)
        return FALSE;

    totalChars = prefixChars + idChars;
    keyBuffer = ExAllocatePoolWithTag(PagedPool,
                                      (totalChars + 1) * sizeof(WCHAR),
                                      'prCP');
    if (!keyBuffer)
        return FALSE;

    RtlCopyMemory(keyBuffer,
                  CriticalDbPrefix,
                  prefixChars * sizeof(WCHAR));

    for (i = 0; i < idChars; ++i)
    {
        WCHAR ch = Id[i];
        if (ch == L'\\')
            ch = L'#';
        else
            ch = RtlUpcaseUnicodeChar(ch);

        keyBuffer[prefixChars + i] = ch;
    }

    keyBuffer[totalChars] = UNICODE_NULL;
    RtlInitUnicodeString(&keyName, keyBuffer);

    InitializeObjectAttributes(&objectAttributes,
                               &keyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&keyHandle, KEY_READ, &objectAttributes);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    RtlInitUnicodeString(&valueName, L"Service");
    Status = ZwQueryValueKey(keyHandle,
                             &valueName,
                             KeyValuePartialInformation,
                             NULL,
                             0,
                             &valueLength);
    if (Status != STATUS_BUFFER_TOO_SMALL ||
        valueLength < sizeof(KEY_VALUE_PARTIAL_INFORMATION))
        goto Cleanup;

    valueInfo = ExAllocatePoolWithTag(PagedPool, valueLength, 'prCP');
    if (!valueInfo)
        goto Cleanup;

    Status = ZwQueryValueKey(keyHandle,
                             &valueName,
                             KeyValuePartialInformation,
                             valueInfo,
                             valueLength,
                             &valueLength);
    if (!NT_SUCCESS(Status) ||
        valueInfo->Type != REG_SZ ||
        valueInfo->DataLength < sizeof(WCHAR))
        goto Cleanup;

    serviceBuffer = ExAllocatePoolWithTag(PagedPool, valueInfo->DataLength, 'prCP');
    if (!serviceBuffer)
        goto Cleanup;

    RtlCopyMemory(serviceBuffer,
                  valueInfo->Data,
                  valueInfo->DataLength);

    serviceBuffer[(valueInfo->DataLength / sizeof(WCHAR)) - 1] = UNICODE_NULL;
    *ServiceNameOut = serviceBuffer;

    ExFreePool(valueInfo);
    ZwClose(keyHandle);
    ExFreePool(keyBuffer);
    return TRUE;

Cleanup:
    if (serviceBuffer)
        ExFreePool(serviceBuffer);
    if (valueInfo)
        ExFreePool(valueInfo);
    if (keyHandle)
        ZwClose(keyHandle);
    if (keyBuffer)
        ExFreePool(keyBuffer);

    return FALSE;
}

static
BOOLEAN
PciTryLookupServiceInIdList(
    _In_opt_ PWSTR IdList,
    _Outptr_result_maybenull_ PWSTR *ServiceNameOut)
{
    if (!IdList)
        return FALSE;

    PWSTR current;

    for (current = IdList; *current; current += wcslen(current) + 1)
    {
        if (PciLookupCriticalDatabaseServiceForId(current, ServiceNameOut))
            return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
PciFindCriticalDeviceService(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Outptr_result_maybenull_ PWSTR *ServiceNameOut)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    PWSTR buffer = NULL;
    ULONG length = 0;
    BOOLEAN found = FALSE;
    NTSTATUS Status;

    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    if (!DeviceExtension || !ServiceNameOut)
        return FALSE;

    *ServiceNameOut = NULL;

    Status = IoGetDeviceProperty(DeviceObject,
                                 DevicePropertyHardwareID,
                                 0,
                                 NULL,
                                 &length);
    if (Status == STATUS_BUFFER_TOO_SMALL && length >= sizeof(WCHAR))
    {
        buffer = ExAllocatePoolWithTag(PagedPool, length, 'prCP');
        if (buffer)
        {
            Status = IoGetDeviceProperty(DeviceObject,
                                         DevicePropertyHardwareID,
                                         length,
                                         buffer,
                                         &length);
            if (NT_SUCCESS(Status))
                found = PciTryLookupServiceInIdList(buffer, ServiceNameOut);

            ExFreePool(buffer);
            buffer = NULL;
        }
    }

    if (!found)
    {
        length = 0;
        Status = IoGetDeviceProperty(DeviceObject,
                                     DevicePropertyCompatibleIDs,
                                     0,
                                     NULL,
                                     &length);
        if (Status == STATUS_BUFFER_TOO_SMALL && length >= sizeof(WCHAR))
        {
            buffer = ExAllocatePoolWithTag(PagedPool, length, 'prCP');
            if (buffer)
            {
                Status = IoGetDeviceProperty(DeviceObject,
                                             DevicePropertyCompatibleIDs,
                                             length,
                                             buffer,
                                             &length);
                if (NT_SUCCESS(Status))
                    found = PciTryLookupServiceInIdList(buffer, ServiceNameOut);

                ExFreePool(buffer);
                buffer = NULL;
            }
        }
    }

    if (!found)
    {
        WCHAR classKey[20];
        UCHAR baseClass = DeviceExtension->PciDevice->PciConfig.BaseClass;
        UCHAR subClass = DeviceExtension->PciDevice->PciConfig.SubClass;
        UCHAR progIf = DeviceExtension->PciDevice->PciConfig.ProgIf;

        _snwprintf(classKey,
                   RTL_NUMBER_OF(classKey),
                   L"PCI#CC_%02X%02X",
                   baseClass,
                   subClass);
        classKey[RTL_NUMBER_OF(classKey) - 1] = UNICODE_NULL;
        found = PciLookupCriticalDatabaseServiceForId(classKey, ServiceNameOut);

        if (!found && progIf != 0)
        {
            _snwprintf(classKey,
                       RTL_NUMBER_OF(classKey),
                       L"PCI#CC_%02X%02X%02X",
                       baseClass,
                       subClass,
                       progIf);
            classKey[RTL_NUMBER_OF(classKey) - 1] = UNICODE_NULL;
            found = PciLookupCriticalDatabaseServiceForId(classKey, ServiceNameOut);
        }
    }

    return found;
}

static VOID
PciPublishLegacyScsiportConfig(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PCM_RESOURCE_LIST ResourceListTranslated)
{
    static const WCHAR ServicePrefix[] =
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";

    PPDO_DEVICE_EXTENSION DeviceExtension;
    UNICODE_STRING driverKeyName, serviceValueName, keyName;
    UNICODE_STRING prefixUs, serviceUs, parametersSuffix, basePathUs;
    OBJECT_ATTRIBUTES objectAttributes;
    HANDLE driverKey = NULL, parametersKey = NULL;
    HANDLE deviceKey = NULL, pdoDeviceKey = NULL;
    KEY_VALUE_PARTIAL_INFORMATION *serviceInfo = NULL;
    KEY_VALUE_PARTIAL_INFORMATION *busInfo = NULL;
    PWCHAR serviceName = NULL, basePath = NULL;
    PCM_FULL_RESOURCE_DESCRIPTOR descriptorCopy = NULL;
    PCM_FULL_RESOURCE_DESCRIPTOR fullDescriptor;
    ULONG neededLength = 0, descriptorSize;
    ULONG deviceIndex;
    ULONG disposition = 0;
    ULONG busNumber;
    NTSTATUS Status;
    BOOLEAN freeServiceName = FALSE;

    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    if (!DeviceExtension || !DeviceExtension->PciDevice)
        return;

    if (DeviceExtension->PciDevice->PciConfig.BaseClass != PCI_CLASS_MASS_STORAGE_CTLR)
        return;

    if (!ResourceListTranslated || ResourceListTranslated->Count == 0)
        return;

    RtlZeroMemory(&driverKeyName, sizeof(driverKeyName));

    if (!serviceName)
    {
        if (PciFindCriticalDeviceService(DeviceObject, &serviceName))
        {
            freeServiceName = TRUE;
        }
    }

    Status = IoOpenDeviceRegistryKey(DeviceObject,
                                     PLUGPLAY_REGKEY_DRIVER,
                                     KEY_READ,
                                     &driverKey);
    if (NT_SUCCESS(Status))
    {
        RtlInitUnicodeString(&serviceValueName, L"Service");
        Status = ZwQueryValueKey(driverKey,
                                 &serviceValueName,
                                 KeyValuePartialInformation,
                                 NULL,
                                 0,
                                 &neededLength);
        if (Status == STATUS_BUFFER_TOO_SMALL &&
            neededLength >= sizeof(KEY_VALUE_PARTIAL_INFORMATION))
        {
            serviceInfo = ExAllocatePoolWithTag(PagedPool, neededLength, 'prCP');
            if (serviceInfo)
            {
                Status = ZwQueryValueKey(driverKey,
                                         &serviceValueName,
                                         KeyValuePartialInformation,
                                         serviceInfo,
                                         neededLength,
                                         &neededLength);
                if (NT_SUCCESS(Status) &&
                    serviceInfo->Type == REG_SZ &&
                    serviceInfo->DataLength >= sizeof(WCHAR))
                {
                    serviceName = ExAllocatePoolWithTag(PagedPool,
                                                        serviceInfo->DataLength,
                                                        'prCP');
                    if (serviceName)
                    {
                        RtlCopyMemory(serviceName,
                                      serviceInfo->Data,
                                      serviceInfo->DataLength);
                        freeServiceName = TRUE;
                        if (serviceInfo->DataLength >= sizeof(WCHAR))
                            serviceName[(serviceInfo->DataLength / sizeof(WCHAR)) - 1] = UNICODE_NULL;
                    }
                }

                ExFreePool(serviceInfo);
                serviceInfo = NULL;
            }
        }

        ZwClose(driverKey);
        driverKey = NULL;
    }

    if (!serviceName)
    {
        Status = IoOpenDeviceRegistryKey(DeviceObject,
                                         PLUGPLAY_REGKEY_DEVICE,
                                         KEY_READ,
                                         &pdoDeviceKey);
        if (NT_SUCCESS(Status))
        {
            RtlInitUnicodeString(&serviceValueName, L"Service");
            Status = ZwQueryValueKey(pdoDeviceKey,
                                     &serviceValueName,
                                     KeyValuePartialInformation,
                                     NULL,
                                     0,
                                     &neededLength);
            if (Status == STATUS_BUFFER_TOO_SMALL &&
                neededLength >= sizeof(KEY_VALUE_PARTIAL_INFORMATION))
            {
                serviceInfo = ExAllocatePoolWithTag(PagedPool, neededLength, 'prCP');
                if (serviceInfo)
                {
                    Status = ZwQueryValueKey(pdoDeviceKey,
                                             &serviceValueName,
                                             KeyValuePartialInformation,
                                             serviceInfo,
                                             neededLength,
                                             &neededLength);
                    if (NT_SUCCESS(Status) &&
                        serviceInfo->Type == REG_SZ &&
                        serviceInfo->DataLength >= sizeof(WCHAR))
                    {
                        serviceName = ExAllocatePoolWithTag(PagedPool,
                                                            serviceInfo->DataLength,
                                                            'prCP');
                        if (serviceName)
                        {
                            RtlCopyMemory(serviceName,
                                          serviceInfo->Data,
                                          serviceInfo->DataLength);
                            freeServiceName = TRUE;
                            if (serviceInfo->DataLength >= sizeof(WCHAR))
                                serviceName[(serviceInfo->DataLength / sizeof(WCHAR)) - 1] = UNICODE_NULL;
                        }
                    }

                    ExFreePool(serviceInfo);
                    serviceInfo = NULL;
                }
            }

            ZwClose(pdoDeviceKey);
            pdoDeviceKey = NULL;
        }

        if (!serviceName)
        {
            Status = IoGetDeviceProperty(DeviceObject,
                                         DevicePropertyDriverKeyName,
                                         0,
                                         NULL,
                                         &neededLength);
            if (Status != STATUS_BUFFER_TOO_SMALL || neededLength < sizeof(WCHAR))
                return;

            driverKeyName.Buffer = ExAllocatePoolWithTag(PagedPool, neededLength, 'prCP');
            if (!driverKeyName.Buffer)
                return;

            Status = IoGetDeviceProperty(DeviceObject,
                                         DevicePropertyDriverKeyName,
                                         neededLength,
                                         driverKeyName.Buffer,
                                         &neededLength);
            if (!NT_SUCCESS(Status))
                goto Cleanup;

            driverKeyName.Length = (USHORT)(neededLength - sizeof(WCHAR));
            driverKeyName.MaximumLength = (USHORT)neededLength;

            InitializeObjectAttributes(&objectAttributes,
                                       &driverKeyName,
                                       OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                       NULL,
                                       NULL);
            Status = ZwOpenKey(&driverKey, KEY_READ, &objectAttributes);
            if (!NT_SUCCESS(Status))
                goto Cleanup;

            RtlInitUnicodeString(&serviceValueName, L"Service");
            Status = ZwQueryValueKey(driverKey,
                                     &serviceValueName,
                                     KeyValuePartialInformation,
                                     NULL,
                                     0,
                                     &neededLength);
            if (Status != STATUS_BUFFER_TOO_SMALL || neededLength < sizeof(KEY_VALUE_PARTIAL_INFORMATION))
                goto Cleanup;

            serviceInfo = ExAllocatePoolWithTag(PagedPool, neededLength, 'prCP');
            if (!serviceInfo)
                goto Cleanup;

            Status = ZwQueryValueKey(driverKey,
                                     &serviceValueName,
                                     KeyValuePartialInformation,
                                     serviceInfo,
                                     neededLength,
                                     &neededLength);
            if (!NT_SUCCESS(Status) || serviceInfo->Type != REG_SZ || serviceInfo->DataLength < sizeof(WCHAR))
                goto Cleanup;

            serviceName = ExAllocatePoolWithTag(PagedPool, serviceInfo->DataLength, 'prCP');
            if (!serviceName)
                goto Cleanup;

            RtlCopyMemory(serviceName, serviceInfo->Data, serviceInfo->DataLength);
            freeServiceName = TRUE;
        }
    }

    prefixUs.Buffer = (PWSTR)ServicePrefix;
    prefixUs.Length = prefixUs.MaximumLength =
        (USHORT)((RTL_NUMBER_OF(ServicePrefix) - 1) * sizeof(WCHAR));

    RtlInitUnicodeString(&serviceUs, serviceName);
    RtlInitUnicodeString(&parametersSuffix, L"\\Parameters");

    basePathUs.MaximumLength = prefixUs.Length + serviceUs.Length +
        parametersSuffix.Length + sizeof(WCHAR);
    basePathUs.Buffer = ExAllocatePoolWithTag(PagedPool, basePathUs.MaximumLength, 'prCP');
    if (!basePathUs.Buffer)
        goto Cleanup;
    RtlZeroMemory(basePathUs.Buffer, basePathUs.MaximumLength);
    basePathUs.Length = 0;
    basePath = basePathUs.Buffer;

    RtlCopyUnicodeString(&basePathUs, &prefixUs);
    RtlAppendUnicodeStringToString(&basePathUs, &serviceUs);
    RtlAppendUnicodeStringToString(&basePathUs, &parametersSuffix);
    basePathUs.Buffer[basePathUs.Length / sizeof(WCHAR)] = UNICODE_NULL;
    DPRINT1("PCI: Parameters base path %wZ\n", &basePathUs);

    fullDescriptor = &ResourceListTranslated->List[0];
    descriptorSize = (ULONG)(FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR,
                                          PartialResourceList.PartialDescriptors) +
                              fullDescriptor->PartialResourceList.Count *
                              sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR));

    descriptorCopy = ExAllocatePoolWithTag(PagedPool, descriptorSize, 'prCP');
    if (!descriptorCopy)
        goto Cleanup;
    RtlCopyMemory(descriptorCopy, fullDescriptor, descriptorSize);

    busNumber = fullDescriptor->BusNumber;
    if (busNumber == (ULONG)-1)
        busNumber = DeviceExtension->PciDevice->BusNumber;

    DPRINT1("PCI: Publishing legacy config for service %wZ (bus %lu, resources %lu)\n",
            &serviceUs,
            busNumber,
            fullDescriptor->PartialResourceList.Count);

    RtlInitUnicodeString(&keyName, basePathUs.Buffer);
    InitializeObjectAttributes(&objectAttributes,
                               &keyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwCreateKey(&parametersKey,
                         KEY_ALL_ACCESS,
                         &objectAttributes,
                         0,
                         NULL,
                         REG_OPTION_NON_VOLATILE,
                         NULL);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PCI: ZwCreateKey(%wZ) for Parameters failed (%lx)\n", &keyName, Status);
        goto Cleanup;
    }

    for (deviceIndex = 0; deviceIndex < 256; ++deviceIndex)
    {
        WCHAR deviceNameBuffer[32];

        _snwprintf(deviceNameBuffer,
                   RTL_NUMBER_OF(deviceNameBuffer),
                   L"Device%lu",
                   deviceIndex);
        deviceNameBuffer[RTL_NUMBER_OF(deviceNameBuffer) - 1] = UNICODE_NULL;
        RtlInitUnicodeString(&keyName, deviceNameBuffer);

        InitializeObjectAttributes(&objectAttributes,
                                   &keyName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   parametersKey,
                                   NULL);

        Status = ZwCreateKey(&deviceKey,
                             KEY_ALL_ACCESS,
                             &objectAttributes,
                             0,
                             NULL,
                             REG_OPTION_NON_VOLATILE,
                             &disposition);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("PCI: ZwCreateKey(Device%lu) failed (%lx)\n", deviceIndex, Status);
            goto Cleanup;
        }

        if (disposition == REG_OPENED_EXISTING_KEY)
        {
            UNICODE_STRING busValueName;
            ULONG tempLength = 0;

            RtlInitUnicodeString(&busValueName, L"BusNumber");
            Status = ZwQueryValueKey(deviceKey,
                                     &busValueName,
                                     KeyValuePartialInformation,
                                     NULL,
                                     0,
                                     &tempLength);
            if (Status == STATUS_BUFFER_TOO_SMALL &&
                tempLength >= sizeof(KEY_VALUE_PARTIAL_INFORMATION))
            {
                busInfo = ExAllocatePoolWithTag(PagedPool, tempLength, 'prCP');
                if (!busInfo)
                {
                    ZwClose(deviceKey);
                    deviceKey = NULL;
                    goto Cleanup;
                }

                Status = ZwQueryValueKey(deviceKey,
                                         &busValueName,
                                         KeyValuePartialInformation,
                                         busInfo,
                                         tempLength,
                                         &tempLength);
                if (NT_SUCCESS(Status) &&
                    busInfo->Type == REG_DWORD &&
                    busInfo->DataLength == sizeof(ULONG) &&
                    *(ULONG*)busInfo->Data == busNumber)
                {
                    ExFreePool(busInfo);
                    busInfo = NULL;
                    DPRINT1("PCI: Reusing Device%lu for service %wZ (bus %lu)\n",
                            deviceIndex, &serviceUs, busNumber);
                    break;
                }

                ExFreePool(busInfo);
                busInfo = NULL;
            }

            ZwClose(deviceKey);
            deviceKey = NULL;
            continue;
        }

        DPRINT1("PCI: Created Device%lu for service %wZ\n", deviceIndex, &serviceUs);

        break;
    }

    if (!deviceKey)
        goto Cleanup;

    RtlInitUnicodeString(&keyName, L"ResourceList");
    Status = ZwSetValueKey(deviceKey,
                           &keyName,
                           0,
                           REG_FULL_RESOURCE_DESCRIPTOR,
                           descriptorCopy,
                           descriptorSize);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("PCI: ZwSetValueKey(ResourceList) failed (%lx)\n", Status);
        goto Cleanup;
    }

    RtlInitUnicodeString(&keyName, L"Configuration Data");
    Status = ZwSetValueKey(deviceKey,
                           &keyName,
                           0,
                           REG_FULL_RESOURCE_DESCRIPTOR,
                           descriptorCopy,
                           descriptorSize);
    if (!NT_SUCCESS(Status))
        DPRINT1("PCI: ZwSetValueKey(Configuration Data) failed (%lx) for Device%lu\n", Status, deviceIndex);

    RtlInitUnicodeString(&keyName, L"BusNumber");
    Status = ZwSetValueKey(deviceKey,
                           &keyName,
                           0,
                           REG_DWORD,
                           &busNumber,
                           sizeof(ULONG));
    if (!NT_SUCCESS(Status))
        DPRINT1("PCI: ZwSetValueKey(BusNumber) failed (%lx) for Device%lu\n", Status, deviceIndex);

Cleanup:
    if (deviceKey)
        ZwClose(deviceKey);
    if (pdoDeviceKey)
        ZwClose(pdoDeviceKey);
    if (parametersKey)
        ZwClose(parametersKey);
    if (driverKey)
        ZwClose(driverKey);
    if (descriptorCopy)
        ExFreePool(descriptorCopy);
    if (serviceName && freeServiceName)
        ExFreePool(serviceName);
    if (basePath)
        ExFreePool(basePath);
    if (serviceInfo)
        ExFreePool(serviceInfo);
    if (busInfo)
        ExFreePool(busInfo);
    if (driverKeyName.Buffer)
        ExFreePool(driverKeyName.Buffer);
}

static GET_SET_DEVICE_DATA InterfaceBusGetBusData;

static
ULONG
NTAPI
InterfaceBusGetBusData(
    IN PVOID Context,
    IN ULONG DataType,
    IN PVOID Buffer,
    IN ULONG Offset,
    IN ULONG Length)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    ULONG Size;

    DPRINT("InterfaceBusGetBusData(%p 0x%lx %p 0x%lx 0x%lx) called\n",
           Context, DataType, Buffer, Offset, Length);

    if (DataType != PCI_WHICHSPACE_CONFIG)
    {
        DPRINT("Unknown DataType %lu\n", DataType);
        return 0;
    }

    DeviceExtension = (PPDO_DEVICE_EXTENSION)((PDEVICE_OBJECT)Context)->DeviceExtension;

    /* Get PCI configuration space */
    Size = PciPdoGetBusDataByOffset(DeviceExtension,
                                    Buffer,
                                    Offset,
                                    Length);
    return Size;
}


static BOOLEAN NTAPI
InterfacePciDevicePresent(
    IN USHORT VendorID,
    IN USHORT DeviceID,
    IN UCHAR RevisionID,
    IN USHORT SubVendorID,
    IN USHORT SubSystemID,
    IN ULONG Flags)
{
    PFDO_DEVICE_EXTENSION FdoDeviceExtension;
    PPCI_DEVICE PciDevice;
    PLIST_ENTRY CurrentBus, CurrentEntry;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    KeAcquireSpinLock(&DriverExtension->BusListLock, &OldIrql);
    CurrentBus = DriverExtension->BusListHead.Flink;
    while (!Found && CurrentBus != &DriverExtension->BusListHead)
    {
        FdoDeviceExtension = CONTAINING_RECORD(CurrentBus, FDO_DEVICE_EXTENSION, ListEntry);

        KeAcquireSpinLockAtDpcLevel(&FdoDeviceExtension->DeviceListLock);
        CurrentEntry = FdoDeviceExtension->DeviceListHead.Flink;
        while (!Found && CurrentEntry != &FdoDeviceExtension->DeviceListHead)
        {
            PciDevice = CONTAINING_RECORD(CurrentEntry, PCI_DEVICE, ListEntry);
            if (PciDevice->PciConfig.VendorID == VendorID &&
                PciDevice->PciConfig.DeviceID == DeviceID)
            {
                if (!(Flags & PCI_USE_SUBSYSTEM_IDS) ||
                    (PciDevice->PciConfig.u.type0.SubVendorID == SubVendorID &&
                     PciDevice->PciConfig.u.type0.SubSystemID == SubSystemID))
                {
                    if (!(Flags & PCI_USE_REVISION) ||
                        PciDevice->PciConfig.RevisionID == RevisionID)
                    {
                        DPRINT("Found the PCI device\n");
                        Found = TRUE;
                    }
                }
            }

            CurrentEntry = CurrentEntry->Flink;
        }

        KeReleaseSpinLockFromDpcLevel(&FdoDeviceExtension->DeviceListLock);
        CurrentBus = CurrentBus->Flink;
    }
    KeReleaseSpinLock(&DriverExtension->BusListLock, OldIrql);

    return Found;
}


static BOOLEAN
CheckPciDevice(
    IN PPCI_COMMON_CONFIG PciConfig,
    IN PPCI_DEVICE_PRESENCE_PARAMETERS Parameters)
{
    if ((Parameters->Flags & PCI_USE_VENDEV_IDS) &&
        (PciConfig->VendorID != Parameters->VendorID ||
         PciConfig->DeviceID != Parameters->DeviceID))
    {
        return FALSE;
    }

    if ((Parameters->Flags & PCI_USE_CLASS_SUBCLASS) &&
        (PciConfig->BaseClass != Parameters->BaseClass ||
         PciConfig->SubClass != Parameters->SubClass))
    {
        return FALSE;
    }

    if ((Parameters->Flags & PCI_USE_PROGIF) &&
         PciConfig->ProgIf != Parameters->ProgIf)
    {
        return FALSE;
    }

    if ((Parameters->Flags & PCI_USE_SUBSYSTEM_IDS) &&
        (PciConfig->u.type0.SubVendorID != Parameters->SubVendorID ||
         PciConfig->u.type0.SubSystemID != Parameters->SubSystemID))
    {
        return FALSE;
    }

    if ((Parameters->Flags & PCI_USE_REVISION) &&
        PciConfig->RevisionID != Parameters->RevisionID)
    {
        return FALSE;
    }

    return TRUE;
}


static BOOLEAN NTAPI
InterfacePciDevicePresentEx(
    IN PVOID Context,
    IN PPCI_DEVICE_PRESENCE_PARAMETERS Parameters)
{
    PPDO_DEVICE_EXTENSION DeviceExtension;
    PFDO_DEVICE_EXTENSION MyFdoDeviceExtension;
    PFDO_DEVICE_EXTENSION FdoDeviceExtension;
    PPCI_DEVICE PciDevice;
    PLIST_ENTRY CurrentBus, CurrentEntry;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    DPRINT("InterfacePciDevicePresentEx(%p %p) called\n",
           Context, Parameters);

    if (!Parameters || Parameters->Size != sizeof(PCI_DEVICE_PRESENCE_PARAMETERS))
        return FALSE;

    DeviceExtension = (PPDO_DEVICE_EXTENSION)((PDEVICE_OBJECT)Context)->DeviceExtension;
    MyFdoDeviceExtension = (PFDO_DEVICE_EXTENSION)DeviceExtension->Fdo->DeviceExtension;

    if (Parameters->Flags & PCI_USE_LOCAL_DEVICE)
    {
        return CheckPciDevice(&DeviceExtension->PciDevice->PciConfig, Parameters);
    }

    KeAcquireSpinLock(&DriverExtension->BusListLock, &OldIrql);
    CurrentBus = DriverExtension->BusListHead.Flink;
    while (!Found && CurrentBus != &DriverExtension->BusListHead)
    {
        FdoDeviceExtension = CONTAINING_RECORD(CurrentBus, FDO_DEVICE_EXTENSION, ListEntry);
        if (!(Parameters->Flags & PCI_USE_LOCAL_BUS) || FdoDeviceExtension == MyFdoDeviceExtension)
        {
            KeAcquireSpinLockAtDpcLevel(&FdoDeviceExtension->DeviceListLock);
            CurrentEntry = FdoDeviceExtension->DeviceListHead.Flink;
            while (!Found && CurrentEntry != &FdoDeviceExtension->DeviceListHead)
            {
                PciDevice = CONTAINING_RECORD(CurrentEntry, PCI_DEVICE, ListEntry);

                if (CheckPciDevice(&PciDevice->PciConfig, Parameters))
                {
                    DPRINT("Found the PCI device\n");
                    Found = TRUE;
                }

                CurrentEntry = CurrentEntry->Flink;
            }

            KeReleaseSpinLockFromDpcLevel(&FdoDeviceExtension->DeviceListLock);
        }
        CurrentBus = CurrentBus->Flink;
    }
    KeReleaseSpinLock(&DriverExtension->BusListLock, OldIrql);

    return Found;
}


static NTSTATUS
PdoQueryInterface(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(Irp);

    if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                         &GUID_BUS_INTERFACE_STANDARD, sizeof(GUID)) == sizeof(GUID))
    {
        /* BUS_INTERFACE_STANDARD */
        if (IrpSp->Parameters.QueryInterface.Version < 1)
            Status = STATUS_NOT_SUPPORTED;
        else if (IrpSp->Parameters.QueryInterface.Size < sizeof(BUS_INTERFACE_STANDARD))
            Status = STATUS_BUFFER_TOO_SMALL;
        else
        {
            PBUS_INTERFACE_STANDARD BusInterface;
            BusInterface = (PBUS_INTERFACE_STANDARD)IrpSp->Parameters.QueryInterface.Interface;
            BusInterface->Size = sizeof(BUS_INTERFACE_STANDARD);
            BusInterface->Version = 1;
            BusInterface->TranslateBusAddress = InterfaceBusTranslateBusAddress;
            BusInterface->GetDmaAdapter = InterfaceBusGetDmaAdapter;
            BusInterface->SetBusData = InterfaceBusSetBusData;
            BusInterface->GetBusData = InterfaceBusGetBusData;
            Status = STATUS_SUCCESS;
        }
    }
    else if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                              &GUID_REACTOS_PCI_ROOT_BUS_INTERFACE, sizeof(GUID)) == sizeof(GUID))
    {
        if (IrpSp->Parameters.QueryInterface.Version < 1)
            Status = STATUS_NOT_SUPPORTED;
        else if (IrpSp->Parameters.QueryInterface.Size < sizeof(PCI_ROOT_BUS_INTERFACE))
            Status = STATUS_BUFFER_TOO_SMALL;
        else
        {
            PPDO_DEVICE_EXTENSION PdoExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
            PFDO_DEVICE_EXTENSION FdoExtension = (PFDO_DEVICE_EXTENSION)PdoExtension->Fdo->DeviceExtension;
            PPCI_ROOT_BUS_INTERFACE RootInterface = (PPCI_ROOT_BUS_INTERFACE)IrpSp->Parameters.QueryInterface.Interface;

            RootInterface->Interface.Size = sizeof(PCI_ROOT_BUS_INTERFACE);
            RootInterface->Interface.Version = 1;
            RootInterface->Interface.Context = DeviceObject;
            RootInterface->Interface.InterfaceReference = InterfaceReference;
            RootInterface->Interface.InterfaceDereference = InterfaceDereference;
            RootInterface->MinBus = FdoExtension->BusRangeStart;
            RootInterface->MaxBus = FdoExtension->BusRangeEnd;
            Status = STATUS_SUCCESS;
        }
    }
    else if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                              &GUID_PCI_DEVICE_PRESENT_INTERFACE, sizeof(GUID)) == sizeof(GUID))
    {
        /* PCI_DEVICE_PRESENT_INTERFACE */
        if (IrpSp->Parameters.QueryInterface.Version < 1)
            Status = STATUS_NOT_SUPPORTED;
        else if (IrpSp->Parameters.QueryInterface.Size < sizeof(PCI_DEVICE_PRESENT_INTERFACE))
            Status = STATUS_BUFFER_TOO_SMALL;
        else
        {
            PPCI_DEVICE_PRESENT_INTERFACE PciDevicePresentInterface;
            PciDevicePresentInterface = (PPCI_DEVICE_PRESENT_INTERFACE)IrpSp->Parameters.QueryInterface.Interface;
            PciDevicePresentInterface->Size = sizeof(PCI_DEVICE_PRESENT_INTERFACE);
            PciDevicePresentInterface->Version = 1;
            PciDevicePresentInterface->IsDevicePresent = InterfacePciDevicePresent;
            PciDevicePresentInterface->IsDevicePresentEx = InterfacePciDevicePresentEx;
            Status = STATUS_SUCCESS;
        }
    }
    else
    {
        /* Not a supported interface */
        return STATUS_NOT_SUPPORTED;
    }

    if (NT_SUCCESS(Status))
    {
        /* Add a reference for the returned interface */
        PINTERFACE Interface;
        Interface = (PINTERFACE)IrpSp->Parameters.QueryInterface.Interface;
        Interface->Context = DeviceObject;
        Interface->InterfaceReference = InterfaceReference;
        Interface->InterfaceDereference = InterfaceDereference;
        Interface->InterfaceReference(Interface->Context);
    }

    return Status;
}

static NTSTATUS
PdoStartDevice(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PCM_RESOURCE_LIST RawResList = IrpSp->Parameters.StartDevice.AllocatedResources;
    PCM_FULL_RESOURCE_DESCRIPTOR RawFullDesc;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR RawPartialDesc;
    PCM_RESOURCE_LIST TranslatedResList;
    PCM_FULL_RESOURCE_DESCRIPTOR TranslatedFullDesc;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR TranslatedPartialDesc;
    PPCI_MSIX_MESSAGE_INFO MsixMessages = NULL;
    ULONG MsixMessageCount = 0;
    ULONG MsixMessageLimit = 0;
    BOOLEAN UsingMsix = FALSE;
    BOOLEAN UsingMsi = FALSE;
    BOOLEAN DisableIntx = FALSE;
    BOOLEAN HadMessageResource = FALSE;
    BOOLEAN MsixAllowed = FALSE;
    BOOLEAN MsiAllowed = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;
    NTSTATUS MsixStatus = STATUS_SUCCESS;
    NTSTATUS MsiStatus = STATUS_SUCCESS;
    ULONG i, ii;
    PPDO_DEVICE_EXTENSION DeviceExtension = DeviceObject->DeviceExtension;
    PFDO_DEVICE_EXTENSION FdoExtension = DeviceExtension->Fdo ? (PFDO_DEVICE_EXTENSION)DeviceExtension->Fdo->DeviceExtension : NULL;
    USHORT Segment = FdoExtension ? FdoExtension->BusSegment : 0;
    UCHAR Irq;
    USHORT Command;
    BOOLEAN HasMemResource = FALSE;
    BOOLEAN HasIoResource = FALSE;

    UNREFERENCED_PARAMETER(Irp);

    if (!RawResList)
    {
        PciPublishLegacyScsiportConfig(DeviceObject,
            IrpSp->Parameters.StartDevice.AllocatedResourcesTranslated);
        return STATUS_SUCCESS;
    }

    /* TODO: Assign the other resources we get to the card */

    TranslatedResList = IrpSp->Parameters.StartDevice.AllocatedResourcesTranslated;
    TranslatedFullDesc = TranslatedResList ? &TranslatedResList->List[0] : NULL;

    PciPdoGetBusData(DeviceExtension,
                     &DeviceExtension->PciDevice->PciConfig,
                     PCI_COMMON_HDR_LENGTH);
    if (DeviceExtension->PciDevice->PciConfig.VendorID == PCI_INVALID_VENDORID ||
        DeviceExtension->PciDevice->PciConfig.VendorID == 0)
    {
        DPRINT1("PCI PDO: Invalid VID on first read for %u:%02x:%02x.%u; retrying config read\n",
                Segment,
                (UCHAR)DeviceExtension->PciDevice->BusNumber,
                DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
                DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber);
        PciPdoGetBusData(DeviceExtension,
                         &DeviceExtension->PciDevice->PciConfig,
                         PCI_COMMON_HDR_LENGTH);
        if (DeviceExtension->PciDevice->PciConfig.VendorID == PCI_INVALID_VENDORID ||
            DeviceExtension->PciDevice->PciConfig.VendorID == 0)
        {
            DPRINT1("PCI PDO: Config read still invalid for %u:%02x:%02x.%u; failing START_DEVICE\n",
                    Segment,
                    (UCHAR)DeviceExtension->PciDevice->BusNumber,
                    DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
                    DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber);
            return STATUS_UNSUCCESSFUL;
        }
    }
    PciPdoCacheMsiInfo(DeviceExtension);
    if (DeviceExtension->PciDevice->MsixCapability)
    {
        MsixMessageLimit = DeviceExtension->PciDevice->MsixTableSize;
        if (MsixMessageLimit == 0)
            MsixMessageLimit = 1;
    }

    PciPdoDetermineInterruptPolicy(DeviceExtension, &MsiAllowed, &MsixAllowed);

    RawFullDesc = &RawResList->List[0];
    for (i = 0; i < RawResList->Count; i++, RawFullDesc = CmiGetNextResourceDescriptor(RawFullDesc))
    {
        PCM_FULL_RESOURCE_DESCRIPTOR CurrentTranslated = TranslatedFullDesc;
        if (TranslatedFullDesc)
            TranslatedFullDesc = CmiGetNextResourceDescriptor(TranslatedFullDesc);

        for (ii = 0; ii < RawFullDesc->PartialResourceList.Count; ii++)
        {
            /* Partial resource descriptors can be of variable size (CmResourceTypeDeviceSpecific),
               but only one is allowed and it must be the last one in the list! */
            RawPartialDesc = &RawFullDesc->PartialResourceList.PartialDescriptors[ii];
            TranslatedPartialDesc = NULL;
            if (CurrentTranslated &&
                ii < CurrentTranslated->PartialResourceList.Count)
            {
                TranslatedPartialDesc = &CurrentTranslated->PartialResourceList.PartialDescriptors[ii];
            }

            if (RawPartialDesc->Type == CmResourceTypeInterrupt)
            {
                UCHAR LegacyLine;

                if (RawPartialDesc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
                {
                    if (!(MsixAllowed || MsiAllowed))
                        continue;

                    HadMessageResource = TRUE;

                    if (MsixAllowed && DeviceExtension->PciDevice->MsixCapability)
                    {
                        if (!MsixMessages && MsixMessageLimit != 0)
                        {
                            MsixMessages = ExAllocatePoolWithTag(NonPagedPool,
                                                                 MsixMessageLimit * sizeof(PCI_MSIX_MESSAGE_INFO),
                                                                 TAG_PCI);
                            if (MsixMessages)
                                RtlZeroMemory(MsixMessages, MsixMessageLimit * sizeof(PCI_MSIX_MESSAGE_INFO));
                            else
                                DPRINT1("PCI PDO: Failed to allocate MSI-X message table for %u:%02x:%02x.%u\n",
                                        Segment,
                                        (UCHAR)DeviceExtension->PciDevice->BusNumber,
                                        DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
                                        DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber);
                        }

                        if (MsixMessages &&
                            MsixMessageCount < MsixMessageLimit)
                        {
                            MsixMessages[MsixMessageCount].Vector =
                                TranslatedPartialDesc ?
                                TranslatedPartialDesc->u.MessageInterrupt.Translated.Vector :
                                RawPartialDesc->u.MessageInterrupt.Raw.Vector;
                            MsixMessages[MsixMessageCount].Affinity =
                                TranslatedPartialDesc ?
                                TranslatedPartialDesc->u.MessageInterrupt.Translated.Affinity :
                                0;
                            MsixMessageCount++;
                            UsingMsix = TRUE;
                        }
                    }
                    else if (MsiAllowed &&
                             !UsingMsi &&
                             DeviceExtension->PciDevice->MsiCapability)
                    {
                        MsiStatus = PciPdoEnableMsi(DeviceExtension,
                                                    RawPartialDesc,
                                                    TranslatedPartialDesc);
                        if (NT_SUCCESS(MsiStatus))
                        {
                            UsingMsi = TRUE;
                            DisableIntx = TRUE;
                        }
                        else
                        {
                            DPRINT1("PCI PDO: MSI enable failed for %u:%02x:%02x.%u (status 0x%08lx)\n",
                                    Segment,
                                    (UCHAR)DeviceExtension->PciDevice->BusNumber,
                                    DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
                                    DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber,
                                    MsiStatus);
                        }
                    }

                    continue;
                }

                if (UsingMsix || UsingMsi)
                    continue;

                if (RawPartialDesc->u.Interrupt.Level <= 0xFF)
                {
                    LegacyLine = (UCHAR)RawPartialDesc->u.Interrupt.Level;
                }
                else
                {
                    LegacyLine = (UCHAR)RawPartialDesc->u.Interrupt.Vector;
                    DPRINT1("PCI PDO: GSI %lu exceeds legacy range for %u:%02x:%02x.%u; using system vector %u for config write.\n",
                            RawPartialDesc->u.Interrupt.Level,
                            Segment,
                            (UCHAR)DeviceExtension->PciDevice->BusNumber,
                            DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
                            DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber,
                            RawPartialDesc->u.Interrupt.Vector);
                }

                DPRINT("Assigning PCI_INTERRUPT_LINE %u (system vector %u) to PCI device 0x%x on seg %u bus 0x%x\n",
                       LegacyLine,
                       RawPartialDesc->u.Interrupt.Vector,
                       DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
                       Segment,
                       DeviceExtension->PciDevice->BusNumber);

                Irq = LegacyLine;
                PciPdoSetBusDataByOffset(DeviceExtension,
                                          &Irq,
                                          0x3c /* PCI_INTERRUPT_LINE */,
                                          sizeof(UCHAR));
            }
            else if (RawPartialDesc->Type == CmResourceTypeMemory)
            {
                HasMemResource = TRUE;
            }
            else if (RawPartialDesc->Type == CmResourceTypePort)
            {
                HasIoResource = TRUE;
            }
        }
    }

    if (UsingMsix && MsixMessageCount > 0 && MsixMessages)
    {
        MsixStatus = PciPdoEnableMsix(DeviceExtension,
                                      MsixMessages,
                                      MsixMessageCount);
        if (NT_SUCCESS(MsixStatus))
        {
            DisableIntx = TRUE;
        }
        else
        {
        DPRINT1("PCI PDO: MSI-X enable failed for %u:%02x:%02x.%u (status 0x%08lx)\n",
                Segment,
                (UCHAR)DeviceExtension->PciDevice->BusNumber,
                DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
                DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber,
                MsixStatus);
        UsingMsix = FALSE;
    }
    }

    if (MsixMessages)
        ExFreePoolWithTag(MsixMessages, TAG_PCI);

    if (HadMessageResource && !(UsingMsix || UsingMsi))
    {
        DPRINT1("PCI PDO: Device %u:%02x:%02x.%u provided message interrupts but is running in legacy mode.\n",
                Segment,
                (UCHAR)DeviceExtension->PciDevice->BusNumber,
                DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
                DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber);
    }

    Command = 0;

    DBGPRINT("pci!PdoStartDevice: Enabling command flags for PCI device 0x%x on bus 0x%x: ",
            DeviceExtension->PciDevice->SlotNumber.u.AsULONG,
            DeviceExtension->PciDevice->BusNumber);
    if (DeviceExtension->PciDevice->EnableBusMaster ||
        (DeviceExtension->PciDevice->PciConfig.Command & PCI_ENABLE_BUS_MASTER))
    {
        Command |= PCI_ENABLE_BUS_MASTER;
        DBGPRINT("[Bus master] ");
    }

    if (HasMemResource ||
        DeviceExtension->PciDevice->EnableMemorySpace ||
        (DeviceExtension->PciDevice->PciConfig.Command & PCI_ENABLE_MEMORY_SPACE))
    {
        Command |= PCI_ENABLE_MEMORY_SPACE;
        DBGPRINT("[Memory space enable] ");
    }

    if (HasIoResource ||
        DeviceExtension->PciDevice->EnableIoSpace ||
        (DeviceExtension->PciDevice->PciConfig.Command & PCI_ENABLE_IO_SPACE))
    {
        Command |= PCI_ENABLE_IO_SPACE;
        DBGPRINT("[I/O space enable] ");
    }

    if (DisableIntx)
    {
        Command |= PCI_COMMAND_INTX_DISABLE;
        DBGPRINT("[INTx disable] ");
    }

    if (Command != 0)
    {
        DBGPRINT("\n");

        /* Force-enable bus master and MEM/IO as requested by policy and existing state */
        Command |= DeviceExtension->PciDevice->PciConfig.Command;

        PciPdoSetBusDataByOffset(DeviceExtension,
                                  &Command,
                                  FIELD_OFFSET(PCI_COMMON_CONFIG, Command),
                                  sizeof(USHORT));
    }
    else
    {
        DBGPRINT("None\n");
    }

    PciPublishLegacyScsiportConfig(DeviceObject,
        IrpSp->Parameters.StartDevice.AllocatedResourcesTranslated);

    return Status;
}

static NTSTATUS
PdoReadConfig(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    ULONG Size;
    USHORT Segment = PciPdoGetSegment((PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension);

    DPRINT("PdoReadConfig() called (seg %u)\n", Segment);

    Size = InterfaceBusGetBusData(DeviceObject,
                                  IrpSp->Parameters.ReadWriteConfig.WhichSpace,
                                  IrpSp->Parameters.ReadWriteConfig.Buffer,
                                  IrpSp->Parameters.ReadWriteConfig.Offset,
                                  IrpSp->Parameters.ReadWriteConfig.Length);

    if (Size != IrpSp->Parameters.ReadWriteConfig.Length)
    {
        DPRINT1("Size %lu  Length %lu (seg %u)\n",
                Size,
                IrpSp->Parameters.ReadWriteConfig.Length,
                Segment);
        Irp->IoStatus.Information = 0;
        return STATUS_UNSUCCESSFUL;
    }

    Irp->IoStatus.Information = Size;

    return STATUS_SUCCESS;
}


static NTSTATUS
PdoWriteConfig(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    ULONG Size;
    USHORT Segment = PciPdoGetSegment((PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension);

    DPRINT1("PdoWriteConfig() called (seg %u)\n", Segment);

    /* Get PCI configuration space */
    Size = InterfaceBusSetBusData(DeviceObject,
                                  IrpSp->Parameters.ReadWriteConfig.WhichSpace,
                                  IrpSp->Parameters.ReadWriteConfig.Buffer,
                                  IrpSp->Parameters.ReadWriteConfig.Offset,
                                  IrpSp->Parameters.ReadWriteConfig.Length);

    if (Size != IrpSp->Parameters.ReadWriteConfig.Length)
    {
        DPRINT1("Size %lu  Length %lu (seg %u)\n",
                Size,
                IrpSp->Parameters.ReadWriteConfig.Length,
                Segment);
        Irp->IoStatus.Information = 0;
        return STATUS_UNSUCCESSFUL;
    }

    Irp->IoStatus.Information = Size;

    return STATUS_SUCCESS;
}

static NTSTATUS
PdoQueryDeviceRelations(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    PDEVICE_RELATIONS DeviceRelations;

    /* We only support TargetDeviceRelation for child PDOs */
    if (IrpSp->Parameters.QueryDeviceRelations.Type != TargetDeviceRelation)
        return Irp->IoStatus.Status;

    /* We can do this because we only return 1 PDO for TargetDeviceRelation */
    DeviceRelations = ExAllocatePoolWithTag(PagedPool, sizeof(*DeviceRelations), TAG_PCI);
    if (!DeviceRelations)
        return STATUS_INSUFFICIENT_RESOURCES;

    DeviceRelations->Count = 1;
    DeviceRelations->Objects[0] = DeviceObject;

    /* The PnP manager will remove this when it is done with the PDO */
    ObReferenceObject(DeviceObject);

    Irp->IoStatus.Information = (ULONG_PTR)DeviceRelations;

    return STATUS_SUCCESS;
}


/*** PUBLIC ******************************************************************/

NTSTATUS
PdoPnpControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
/*
 * FUNCTION: Handle Plug and Play IRPs for the child device
 * ARGUMENTS:
 *     DeviceObject = Pointer to physical device object of the child device
 *     Irp          = Pointer to IRP that should be handled
 * RETURNS:
 *     Status
 */
{
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;
    USHORT Segment = PciPdoGetSegment((PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension);

    DPRINT("Called (seg %u)\n", Segment);

    Status = Irp->IoStatus.Status;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
            DPRINT("Unimplemented IRP_MN_DEVICE_USAGE_NOTIFICATION received\n");
            break;

        case IRP_MN_EJECT:
            DPRINT("Unimplemented IRP_MN_EJECT received\n");
            break;

        case IRP_MN_QUERY_BUS_INFORMATION:
            Status = PdoQueryBusInformation(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_CAPABILITIES:
            Status = PdoQueryCapabilities(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            Status = PdoQueryDeviceRelations(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            DPRINT("IRP_MN_QUERY_DEVICE_TEXT received (seg %u)\n", Segment);
            Status = PdoQueryDeviceText(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_ID:
            DPRINT("IRP_MN_QUERY_ID received (seg %u)\n", Segment);
            Status = PdoQueryId(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            DPRINT("Unimplemented IRP_MN_QUERY_ID received\n");
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            DPRINT("IRP_MN_QUERY_RESOURCE_REQUIREMENTS received (seg %u)\n", Segment);
            Status = PdoQueryResourceRequirements(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_RESOURCES:
            DPRINT("IRP_MN_QUERY_RESOURCES received (seg %u)\n", Segment);
            Status = PdoQueryResources(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_SET_LOCK:
            DPRINT("Unimplemented IRP_MN_SET_LOCK received\n");
            break;

        case IRP_MN_START_DEVICE:
            Status = PdoStartDevice(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
        case IRP_MN_REMOVE_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_INTERFACE:
            DPRINT("IRP_MN_QUERY_INTERFACE received\n");
            Status = PdoQueryInterface(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_READ_CONFIG:
            DPRINT("IRP_MN_READ_CONFIG received\n");
            Status = PdoReadConfig(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_WRITE_CONFIG:
            DPRINT("IRP_MN_WRITE_CONFIG received\n");
            Status = PdoWriteConfig(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            DPRINT("IRP_MN_FILTER_RESOURCE_REQUIREMENTS received\n");
            /* Nothing to do */
            Irp->IoStatus.Status = Status;
            break;

        default:
            DPRINT1("Unknown IOCTL 0x%lx\n", IrpSp->MinorFunction);
            break;
    }

    if (Status != STATUS_PENDING)
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    DPRINT("Leaving. Status 0x%X\n", Status);

    return Status;
}

NTSTATUS
PdoPowerControl(
    PDEVICE_OBJECT DeviceObject,
    PIRP Irp)
/*
 * FUNCTION: Handle power management IRPs for the child device
 * ARGUMENTS:
 *     DeviceObject = Pointer to physical device object of the child device
 *     Irp          = Pointer to IRP that should be handled
 * RETURNS:
 *     Status
 */
{
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status = Irp->IoStatus.Status;

    DPRINT("Called\n");

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_QUERY_POWER:
        case IRP_MN_SET_POWER:
            Status = STATUS_SUCCESS;
            break;
    }

    PoStartNextPowerIrp(Irp);
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    DPRINT("Leaving. Status 0x%X\n", Status);

    return Status;
}

/* EOF */
