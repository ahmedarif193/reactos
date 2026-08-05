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
#include <devpkey.h>
#include <wdmguid.h>

DEFINE_GUID(GUID_REACTOS_PCI_ROOT_BUS_INTERFACE,
            0xd7b6f1ba, 0x9f5a, 0x4d9d, 0x9d, 0xfe, 0x5d, 0x4a, 0x17, 0xb8, 0xc5, 0xa1);

/* GUIDs not yet in the ReactOS SDK headers, or defined before initguid.h */
DEFINE_GUID(GUID_PCI_PME_INTERFACE,
            0xaac7e6ac, 0xbb0b, 0x11d2, 0xb4, 0x84, 0x00, 0xc0, 0x4f, 0x72, 0xde, 0x8b);
DEFINE_GUID(GUID_PCI_EXPRESS_LINK_QUIESCENT_INTERFACE,
            0x146cd41c, 0xdae3, 0x4437, 0x8a, 0xff, 0x2a, 0xf3, 0xf0, 0x38, 0x09, 0x9b);
DEFINE_GUID(GUID_PCI_EXPRESS_ROOT_PORT_INTERFACE,
            0x83a7734a, 0x84c7, 0x4161, 0x9a, 0x98, 0x60, 0x00, 0xed, 0x0c, 0x4a, 0x33);
DEFINE_GUID(GUID_PCI_VIRTUALIZATION_INTERFACE,
            0x64897b47, 0x3a4a, 0x4d75, 0xbc, 0x74, 0x89, 0xdd, 0x6c, 0x07, 0x82, 0x93);

#define NDEBUG
#include <debug.h>

#if 0
#define DBGPRINT(...) DbgPrint(__VA_ARGS__)
#else
#define DBGPRINT(...)
#endif

#if defined(_M_AMD64)
NTHALAPI
NTSTATUS
NTAPI
HalpGetInterruptTargetInformation(
    _Inout_ PHAL_INTERRUPT_TARGET_INFORMATION TargetInformation);

NTHALAPI
NTSTATUS
NTAPI
HalpGetMessageRoutingInfo(
    _Inout_ PHAL_MESSAGE_ROUTING_INFO RoutingInfo);
#endif

#if defined(_M_ARM64)
static
USHORT
PciPdoGetRequesterId(
    _In_ PPCI_DEVICE Device)
{
    return (USHORT)(((Device->BusNumber & 0xFF) << 8) |
                    ((Device->SlotNumber.u.bits.DeviceNumber & 0x1F) << 3) |
                    (Device->SlotNumber.u.bits.FunctionNumber & 0x07));
}

static
NTSTATUS
PciPdoGetArm64MsiMessage(
    _In_ PPCI_DEVICE Device,
    _In_ ULONG Vector,
    _In_ KAFFINITY Affinity,
    _Out_ PULONG AddressLow,
    _Out_ PULONG AddressHigh,
    _Out_ PUSHORT Data)
{
    USHORT RequesterId;

    RequesterId = PciPdoGetRequesterId(Device);
    if (!HalGetMsiMessageAddressEx(RequesterId,
                                   Vector,
                                   Affinity,
                                   AddressLow,
                                   AddressHigh,
                                   Data))
    {
        DPRINT1("PCI PDO: ARM64 MSI route failed for RID 0x%04x vector %lu affinity 0x%Ix\n",
                RequesterId,
                Vector,
                Affinity);
        return STATUS_NOT_SUPPORTED;
    }

    DPRINT1("PCI PDO: ARM64 MSI route RID 0x%04x vector %lu affinity 0x%Ix address 0x%08lx%08lx data 0x%04x\n",
            RequesterId,
            Vector,
            Affinity,
            *AddressHigh,
            *AddressLow,
            *Data);

    return STATUS_SUCCESS;
}
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
#define PCI_PROGIF_USB_XHCI    0x30

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
PciPdoShouldExposeInterruptResources(
    _In_ PPCI_COMMON_CONFIG PciConfig)
{
    if (PCI_CONFIGURATION_TYPE(PciConfig) != PCI_DEVICE_TYPE)
        return FALSE;

    return TRUE;
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

    /* Find Power Management capability */
    if (Device->PmCapability == 0)
    {
        if (PciPdoFindCapability(DeviceExtension, PCI_CAP_ID_PM, &Offset))
            Device->PmCapability = Offset;
    }

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

static
BOOLEAN
PciPdoShouldUseDefaultMessageInterrupts(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension)
{
    PPCI_COMMON_CONFIG PciConfig;

    if (!DeviceExtension || !DeviceExtension->PciDevice)
        return FALSE;

    PciConfig = &DeviceExtension->PciDevice->PciConfig;

    /*
     * xHCI, AHCI, and NVMe can be boot-critical before an INF policy is
     * installed, and these controller classes use message interrupts.
     */
    if ((PciConfig->BaseClass == PCI_CLASS_SERIAL_BUS_CTLR) &&
        (PciConfig->SubClass == PCI_SUBCLASS_SB_USB) &&
        (PciConfig->ProgIf == PCI_PROGIF_USB_XHCI))
    {
        return TRUE;
    }

    if (PciConfig->BaseClass != PCI_CLASS_MASS_STORAGE_CTLR)
        return FALSE;

    return (((PciConfig->SubClass == PCI_SUBCLASS_MSC_AHCI_CTLR) &&
             (PciConfig->ProgIf == 0x01)) ||
            ((PciConfig->SubClass == PCI_SUBCLASS_MSC_NVM_CTLR) &&
             (PciConfig->ProgIf == PCI_PROGRAMMING_INTERFACE_MSC_NVM_EXPRESS)));
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

/*
 * Message interrupt resource overlay for pre-Vista NTDDI builds.
 * The MessageInterrupt member of CM_PARTIAL_RESOURCE_DESCRIPTOR.u
 * is guarded by NTDDI_VERSION >= NTDDI_LONGHORN, but ReactOS builds
 * at WS03 level. The struct overlays Interrupt at the same offset.
 *
 * Raw layout: { USHORT Reserved, USHORT MessageCount, ULONG Vector, KAFFINITY Affinity }
 * Translated layout: identical to Interrupt { ULONG Level, ULONG Vector, KAFFINITY Affinity }
 */
typedef struct _PCI_MSG_INTERRUPT_RAW
{
    USHORT Reserved;
    USHORT MessageCount;
    ULONG Vector;
    KAFFINITY Affinity;
} PCI_MSG_INTERRUPT_RAW, *PPCI_MSG_INTERRUPT_RAW;

#define PCI_MSG_RAW(desc) \
    ((PPCI_MSG_INTERRUPT_RAW)&(desc)->u.Interrupt)

static
BOOLEAN
PciPdoApplyLegacyInterruptPolicyFromKey(
    _In_ HANDLE KeyHandle,
    _Inout_ PBOOLEAN AllowMsi,
    _Inout_ PBOOLEAN AllowMsix)
{
    UNICODE_STRING ValueName;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION ValueInfo = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    ULONG ResultLength;
    NTSTATUS Status;
    BOOLEAN Found = FALSE;

    if (!KeyHandle)
        return FALSE;

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
        Found = TRUE;
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
        Found = TRUE;
    }

    return Found;
}

static
BOOLEAN
PciPdoApplyStandardMessageInterruptPolicy(
    _In_ HANDLE DeviceKeyHandle,
    _Inout_ PBOOLEAN AllowMsi,
    _Inout_ PBOOLEAN AllowMsix,
    _Out_opt_ PULONG MessageNumberLimit)
{
    static const WCHAR SubKeyNameBuffer[] =
        L"Interrupt Management\\MessageSignaledInterruptProperties";
    UNICODE_STRING SubKeyName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE SubKeyHandle;
    UNICODE_STRING ValueName;
    UCHAR Buffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION ValueInfo = (PKEY_VALUE_PARTIAL_INFORMATION)Buffer;
    ULONG ResultLength;
    NTSTATUS Status;
    BOOLEAN Found = FALSE;

    if (!DeviceKeyHandle)
        return FALSE;

    RtlInitUnicodeString(&SubKeyName, SubKeyNameBuffer);
    InitializeObjectAttributes(&ObjectAttributes,
                               &SubKeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               DeviceKeyHandle,
                               NULL);
    Status = ZwOpenKey(&SubKeyHandle, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return FALSE;

    RtlInitUnicodeString(&ValueName, L"MSISupported");
    Status = ZwQueryValueKey(SubKeyHandle,
                             &ValueName,
                             KeyValuePartialInformation,
                             ValueInfo,
                             sizeof(Buffer),
                             &ResultLength);
    if (NT_SUCCESS(Status) &&
        ValueInfo->Type == REG_DWORD &&
        ValueInfo->DataLength == sizeof(ULONG))
    {
        BOOLEAN Enabled = (*(PULONG)ValueInfo->Data) != 0;
        *AllowMsi = Enabled;
        *AllowMsix = Enabled;
        Found = TRUE;
    }

    if (MessageNumberLimit)
    {
        RtlInitUnicodeString(&ValueName, L"MessageNumberLimit");
        Status = ZwQueryValueKey(SubKeyHandle,
                                 &ValueName,
                                 KeyValuePartialInformation,
                                 ValueInfo,
                                 sizeof(Buffer),
                                 &ResultLength);
        if (NT_SUCCESS(Status) &&
            ValueInfo->Type == REG_DWORD &&
            ValueInfo->DataLength == sizeof(ULONG))
        {
            *MessageNumberLimit = *(PULONG)ValueInfo->Data;
        }
    }

    ZwClose(SubKeyHandle);
    return Found;
}

static
NTSTATUS
PciPdoGetDeviceNode(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _Out_ PDEVICE_NODE *DeviceNode)
{
    PEXTENDED_DEVOBJ_EXTENSION DeviceObjectExtension;

    if (!DeviceNode)
        return STATUS_INVALID_PARAMETER;

    *DeviceNode = NULL;

    if (!DeviceExtension ||
        !DeviceExtension->PciDevice ||
        !DeviceExtension->PciDevice->Pdo ||
        !DeviceExtension->PciDevice->Pdo->DeviceObjectExtension)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DeviceObjectExtension =
        (PEXTENDED_DEVOBJ_EXTENSION)DeviceExtension->PciDevice->Pdo->DeviceObjectExtension;
    if (!DeviceObjectExtension->DeviceNode)
        return STATUS_INVALID_DEVICE_REQUEST;

    *DeviceNode = DeviceObjectExtension->DeviceNode;
    return STATUS_SUCCESS;
}

static
NTSTATUS
PciPdoOpenEnumInstanceKey(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PHANDLE KeyHandle)
{
    static const WCHAR RootKeyName[] =
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Enum\\";
    PDEVICE_NODE DeviceNode;
    UNICODE_STRING KeyName;
    ULONG KeyNameLength;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;

    Status = PciPdoGetDeviceNode(DeviceExtension, &DeviceNode);
    if (!NT_SUCCESS(Status))
        return Status;
    if (DeviceNode->InstancePath.Length == 0)
        return STATUS_INVALID_DEVICE_REQUEST;

    KeyNameLength = sizeof(RootKeyName) - sizeof(UNICODE_NULL) +
                    DeviceNode->InstancePath.Length +
                    sizeof(UNICODE_NULL);
    KeyName.Buffer = ExAllocatePoolWithTag(PagedPool, KeyNameLength, TAG_PCI);
    if (!KeyName.Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeyName.Length = 0;
    KeyName.MaximumLength = (USHORT)KeyNameLength;
    RtlAppendUnicodeToString(&KeyName, RootKeyName);
    Status = RtlAppendUnicodeStringToString(&KeyName, &DeviceNode->InstancePath);
    if (NT_SUCCESS(Status))
    {
        InitializeObjectAttributes(&ObjectAttributes,
                                   &KeyName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL,
                                   NULL);
        Status = ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);
    }

    ExFreePoolWithTag(KeyName.Buffer, TAG_PCI);
    return Status;
}

static
NTSTATUS
PciPdoOpenServiceKey(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PHANDLE KeyHandle)
{
    static const WCHAR ServicesRootKeyName[] =
        L"\\Registry\\Machine\\System\\CurrentControlSet\\Services\\";
    PDEVICE_NODE DeviceNode;
    UNICODE_STRING KeyName;
    ULONG KeyNameLength;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;

    *KeyHandle = NULL;

    Status = PciPdoGetDeviceNode(DeviceExtension, &DeviceNode);
    if (!NT_SUCCESS(Status))
        return Status;

    if (DeviceNode->ServiceName.Length == 0)
        return STATUS_INVALID_DEVICE_REQUEST;

    if (DeviceNode->ServiceName.Length >
        MAXUSHORT -
        (sizeof(ServicesRootKeyName) - sizeof(UNICODE_NULL)) -
        sizeof(UNICODE_NULL))
    {
        return STATUS_NAME_TOO_LONG;
    }

    KeyNameLength = sizeof(ServicesRootKeyName) - sizeof(UNICODE_NULL) +
                    DeviceNode->ServiceName.Length +
                    sizeof(UNICODE_NULL);

    KeyName.Buffer = ExAllocatePoolWithTag(PagedPool, KeyNameLength, TAG_PCI);
    if (!KeyName.Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    KeyName.Length = 0;
    KeyName.MaximumLength = (USHORT)KeyNameLength;
    Status = RtlAppendUnicodeToString(&KeyName, ServicesRootKeyName);
    if (NT_SUCCESS(Status))
        Status = RtlAppendUnicodeStringToString(&KeyName, &DeviceNode->ServiceName);
    if (NT_SUCCESS(Status))
    {
        InitializeObjectAttributes(&ObjectAttributes,
                                   &KeyName,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   NULL,
                                   NULL);
        Status = ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);
    }

    ExFreePoolWithTag(KeyName.Buffer, TAG_PCI);
    return Status;
}

static
NTSTATUS
PciPdoQueryStringProperty(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ DEVICE_REGISTRY_PROPERTY LegacyProperty,
    _Out_ PUNICODE_STRING Value)
{
    DEVPROPTYPE PropertyType = DEVPROP_TYPE_EMPTY;
    ULONG RequiredLength = 0;
    PWSTR Buffer = NULL;
    NTSTATUS Status;

    if (!Value)
        return STATUS_INVALID_PARAMETER;

    Value->Buffer = NULL;
    Value->Length = 0;
    Value->MaximumLength = 0;

    if (!DeviceExtension ||
        !DeviceExtension->PciDevice ||
        !DeviceExtension->PciDevice->Pdo)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = IoGetDevicePropertyData(DeviceExtension->PciDevice->Pdo,
                                     PropertyKey,
                                     0,
                                     0,
                                     0,
                                     NULL,
                                     &RequiredLength,
                                     &PropertyType);
    if (Status == STATUS_BUFFER_TOO_SMALL &&
        RequiredLength >= sizeof(WCHAR) &&
        PropertyType == DEVPROP_TYPE_STRING)
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, RequiredLength, TAG_PCI);
        if (!Buffer)
            return STATUS_INSUFFICIENT_RESOURCES;

        Status = IoGetDevicePropertyData(DeviceExtension->PciDevice->Pdo,
                                         PropertyKey,
                                         0,
                                         0,
                                         RequiredLength,
                                         Buffer,
                                         &RequiredLength,
                                         &PropertyType);
    }
    else
    {
        Status = IoGetDeviceProperty(DeviceExtension->PciDevice->Pdo,
                                     LegacyProperty,
                                     0,
                                     NULL,
                                     &RequiredLength);
        if (Status != STATUS_BUFFER_TOO_SMALL ||
            RequiredLength < sizeof(WCHAR))
        {
            return Status;
        }

        Buffer = ExAllocatePoolWithTag(PagedPool, RequiredLength, TAG_PCI);
        if (!Buffer)
            return STATUS_INSUFFICIENT_RESOURCES;

        Status = IoGetDeviceProperty(DeviceExtension->PciDevice->Pdo,
                                     LegacyProperty,
                                     RequiredLength,
                                     Buffer,
                                     &RequiredLength);
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Buffer, TAG_PCI);
        return Status;
    }

    if (RequiredLength < sizeof(WCHAR) || RequiredLength > MAXUSHORT)
    {
        ExFreePoolWithTag(Buffer, TAG_PCI);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    Value->Buffer = Buffer;
    Value->Length = (USHORT)(RequiredLength - sizeof(WCHAR));
    Value->MaximumLength = (USHORT)RequiredLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
PciPdoOpenDriverPropertyKey(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _In_ ACCESS_MASK DesiredAccess,
    _Out_ PHANDLE KeyHandle)
{
    UNICODE_STRING DriverKeyName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    NTSTATUS Status;

    Status = PciPdoQueryStringProperty(DeviceExtension,
                                       &DEVPKEY_Device_Driver,
                                       DevicePropertyDriverKeyName,
                                       &DriverKeyName);
    if (!NT_SUCCESS(Status))
        return Status;

    InitializeObjectAttributes(&ObjectAttributes,
                               &DriverKeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);
    ExFreePoolWithTag(DriverKeyName.Buffer, TAG_PCI);
    return Status;
}

static
VOID
PciPdoSetUint32DeviceProperty(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ ULONG Value)
{
    NTSTATUS Status;

    if (!DeviceExtension ||
        !DeviceExtension->PciDevice ||
        !DeviceExtension->PciDevice->Pdo)
    {
        return;
    }

    Status = IoSetDevicePropertyData(DeviceExtension->PciDevice->Pdo,
                                     PropertyKey,
                                     0,
                                     0,
                                     DEVPROP_TYPE_UINT32,
                                     sizeof(Value),
                                     &Value);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("PCI: IoSetDevicePropertyData(pid %lu) failed for %lu:%02lx:%02lx.%lu: 0x%08lx\n",
               PropertyKey->pid,
               PciPdoGetSegment(DeviceExtension),
               DeviceExtension->PciDevice->BusNumber,
               DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
               DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber,
               Status);
    }
}

static
VOID
PciPdoPublishDeviceProperties(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension)
{
    ULONG DeviceNumber, FunctionNumber, Address;

    if (!DeviceExtension || !DeviceExtension->PciDevice)
        return;

    DeviceNumber = DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber;
    FunctionNumber = DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber;
    Address = ((DeviceNumber << 16) & 0xFFFF0000) | (FunctionNumber & 0xFFFF);

    PciPdoSetUint32DeviceProperty(DeviceExtension,
                                  &DEVPKEY_Device_BusNumber,
                                  DeviceExtension->PciDevice->BusNumber);
    PciPdoSetUint32DeviceProperty(DeviceExtension,
                                  &DEVPKEY_Device_Address,
                                  Address);
}

static
ULONG
PciPdoNormalizeMsiMessageCount(
    _In_ ULONG MessageCount)
{
    if (MessageCount >= 16)
        return 16;
    if (MessageCount >= 8)
        return 8;
    if (MessageCount >= 4)
        return 4;
    if (MessageCount >= 2)
        return 2;

    return 1;
}

static
BOOLEAN
PciPdoResourceListHasMessageInterrupt(
    _In_opt_ PCM_RESOURCE_LIST ResourceList)
{
    ULONG i, ii;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDesc;

    if (!ResourceList)
        return FALSE;

    FullDesc = &ResourceList->List[0];
    for (i = 0; i < ResourceList->Count; i++, FullDesc = CmiGetNextResourceDescriptor(FullDesc))
    {
        PCM_PARTIAL_RESOURCE_LIST Partial = &FullDesc->PartialResourceList;

        for (ii = 0; ii < Partial->Count; ii++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc = &Partial->PartialDescriptors[ii];

            if (Desc->Type == CmResourceTypeInterrupt &&
                (Desc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static
BOOLEAN
PciPdoResourceListHasLegacyInterrupt(
    _In_opt_ PCM_RESOURCE_LIST ResourceList)
{
    ULONG i, ii;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDesc;

    if (!ResourceList)
        return FALSE;

    FullDesc = &ResourceList->List[0];
    for (i = 0; i < ResourceList->Count; i++, FullDesc = CmiGetNextResourceDescriptor(FullDesc))
    {
        PCM_PARTIAL_RESOURCE_LIST Partial = &FullDesc->PartialResourceList;

        for (ii = 0; ii < Partial->Count; ii++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc = &Partial->PartialDescriptors[ii];

            if (Desc->Type == CmResourceTypeInterrupt &&
                !(Desc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
            {
                return TRUE;
            }
        }
    }

    return FALSE;
}

static
BOOLEAN
PciPdoRequirementsListHasMessageInterrupt(
    _In_opt_ PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList)
{
    ULONG i, ii;
    PIO_RESOURCE_LIST ResourceList;

    if (!RequirementsList || RequirementsList->AlternativeLists == 0)
        return FALSE;

    ResourceList = &RequirementsList->List[0];
    for (i = 0; i < RequirementsList->AlternativeLists; i++)
    {
        for (ii = 0; ii < ResourceList->Count; ii++)
        {
            PIO_RESOURCE_DESCRIPTOR Desc = &ResourceList->Descriptors[ii];

            if (Desc->Type == CmResourceTypeInterrupt &&
                (Desc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
            {
                return TRUE;
            }
        }

        ResourceList = (PIO_RESOURCE_LIST)(ResourceList->Descriptors + ResourceList->Count);
    }

    return FALSE;
}

static
VOID
PciPdoDetermineInterruptPolicy(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _Out_ PBOOLEAN AllowMsi,
    _Out_ PBOOLEAN AllowMsix,
    _Out_opt_ PULONG MessageNumberLimit)
{
    PFDO_DEVICE_EXTENSION FdoExtension = NULL;
    BOOLEAN UseMsi = FALSE;
    BOOLEAN UseMsix = FALSE;
    ULONG MessageLimit = 0;
    HANDLE KeyHandle;
    BOOLEAN PolicyFound = FALSE;

    if (!DeviceExtension || !DeviceExtension->PciDevice)
    {
        *AllowMsi = FALSE;
        *AllowMsix = FALSE;
        if (MessageNumberLimit)
            *MessageNumberLimit = 0;
        return;
    }

    if (DeviceExtension->Fdo)
        FdoExtension = (PFDO_DEVICE_EXTENSION)DeviceExtension->Fdo->DeviceExtension;

    if (NT_SUCCESS(PciPdoOpenEnumInstanceKey(DeviceExtension,
                                             KEY_READ,
                                             &KeyHandle)))
    {
        /* Windows enables MSI through the device hardware key under
         * Interrupt Management\MessageSignaledInterruptProperties,
         * typically populated by a DDInstall.HW section on the PDO's
         * enum instance key. Keep the legacy AllowMSI/AllowMSIX
         * values as explicit overrides. */
        PolicyFound |= PciPdoApplyStandardMessageInterruptPolicy(KeyHandle, &UseMsi, &UseMsix, &MessageLimit);
        PolicyFound |= PciPdoApplyLegacyInterruptPolicyFromKey(KeyHandle, &UseMsi, &UseMsix);
        ZwClose(KeyHandle);
    }

    if (NT_SUCCESS(PciPdoOpenDriverPropertyKey(DeviceExtension,
                                               KEY_READ,
                                               &KeyHandle)))
    {
        /* ReactOS network and audio INFs commonly place hardware policy
         * under HKR in the main DDInstall section. Query the same class
         * driver key through the Win7 property path first, then keep the
         * direct registry-key fallback below for older callers. */
        PolicyFound |= PciPdoApplyStandardMessageInterruptPolicy(KeyHandle, &UseMsi, &UseMsix, &MessageLimit);
        PolicyFound |= PciPdoApplyLegacyInterruptPolicyFromKey(KeyHandle, &UseMsi, &UseMsix);
        ZwClose(KeyHandle);
    }
    else if (NT_SUCCESS(IoOpenDeviceRegistryKey(DeviceExtension->PciDevice->Pdo,
                                                PLUGPLAY_REGKEY_DRIVER,
                                                KEY_READ,
                                                &KeyHandle)))
    {
        PolicyFound |= PciPdoApplyStandardMessageInterruptPolicy(KeyHandle, &UseMsi, &UseMsix, &MessageLimit);
        PolicyFound |= PciPdoApplyLegacyInterruptPolicyFromKey(KeyHandle, &UseMsi, &UseMsix);
        ZwClose(KeyHandle);
    }

    if (!PolicyFound &&
        NT_SUCCESS(PciPdoOpenServiceKey(DeviceExtension,
                                        KEY_READ,
                                        &KeyHandle)))
    {
        /*
         * Boot-critical devices can bind from the critical-device database
         * before their INF hardware section is installed on the enum key.
         * Allow boot hives to carry the same MSI policy on the service key.
         */
        PolicyFound |= PciPdoApplyStandardMessageInterruptPolicy(KeyHandle, &UseMsi, &UseMsix, &MessageLimit);
        PolicyFound |= PciPdoApplyLegacyInterruptPolicyFromKey(KeyHandle, &UseMsi, &UseMsix);
        ZwClose(KeyHandle);
    }

    if (!PolicyFound)
    {
        PciPdoCacheMsiInfo(DeviceExtension);
        if (PciPdoShouldUseDefaultMessageInterrupts(DeviceExtension))
        {
            if (DeviceExtension->PciDevice->MsixCapability)
            {
                UseMsix = TRUE;
                UseMsi = FALSE;
                MessageLimit = 1;
            }
            else if (DeviceExtension->PciDevice->MsiCapability)
            {
                UseMsi = TRUE;
                UseMsix = FALSE;
                MessageLimit = 1;
            }
        }
    }

    if (!PciMsiEnabledByPolicy)
        UseMsi = FALSE;
    if (!PciMsixEnabledByPolicy)
        UseMsix = FALSE;

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
    if (MessageNumberLimit)
        *MessageNumberLimit = MessageLimit;
}

#if !defined(_M_AMD64) && !defined(_M_ARM64) && (NTDDI_VERSION < NTDDI_WIN7)
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
#endif

static
BOOLEAN
PciPdoNeedsMessageInterruptRequirementsRefresh(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension)
{
    BOOLEAN AllowMsi;
    BOOLEAN AllowMsix;
    BOOLEAN SupportsMsi;
    BOOLEAN SupportsMsix;
    BOOLEAN HasMessageResource;
    BOOLEAN HasLegacyResource;
    PDEVICE_NODE DeviceNode;
    PCM_RESOURCE_LIST EffectiveResources;
    NTSTATUS Status;

    if (!DeviceExtension || !DeviceExtension->PciDevice)
        return FALSE;

    if (!PciPdoShouldExposeInterruptResources(&DeviceExtension->PciDevice->PciConfig))
        return FALSE;

    PciPdoDetermineInterruptPolicy(DeviceExtension, &AllowMsi, &AllowMsix, NULL);
    if (!AllowMsi && !AllowMsix)
        return FALSE;

    PciPdoCacheMsiInfo(DeviceExtension);
    SupportsMsi = AllowMsi && (DeviceExtension->PciDevice->MsiCapability != 0);
    SupportsMsix = AllowMsix && (DeviceExtension->PciDevice->MsixCapability != 0);
    if (!SupportsMsi && !SupportsMsix)
        return FALSE;

    Status = PciPdoGetDeviceNode(DeviceExtension, &DeviceNode);
    if (!NT_SUCCESS(Status))
        return FALSE;

    EffectiveResources = DeviceNode->ResourceList ?
                         DeviceNode->ResourceList :
                         DeviceNode->BootResources;
    HasMessageResource = PciPdoResourceListHasMessageInterrupt(EffectiveResources);
    HasLegacyResource = PciPdoResourceListHasLegacyInterrupt(EffectiveResources);

    /* DDInstall.HW policy arrives after the first requirements query in
     * this tree. If the device is still running on legacy INTx and the
     * cached requirements list never advertised message interrupts, ask
     * PnP for a fresh requirements pass instead of rewriting the assigned
     * descriptor pair inside START_DEVICE. For first-start devices there is
     * no live ResourceList yet; fall back to BootResources so MSI-only
     * stacks like xHCI can refresh before their first START_DEVICE fails. */
    if (HasMessageResource)
        return FALSE;
    if (PciPdoRequirementsListHasMessageInterrupt(DeviceNode->ResourceRequirements))
        return FALSE;

    if (EffectiveResources != NULL)
        return HasLegacyResource;

    return TRUE;
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

    /*
     * Read the base address out of config space *now*, not out of
     * Device->PciConfig.
     *
     * That structure is the config as it was at enumeration.  The PnP manager
     * assigns BARs afterwards, and on a machine where it moves one, the cached
     * value is the address the device used to live at.  Programming the MSI-X
     * table through it writes the message address and data into whatever
     * occupies that physical range now, and returns success: the device's own
     * table is left masked, so it never raises an interrupt and nothing reports
     * an error.  usbport carries its own MSI-X programming precisely because of
     * this, computing the table address from the resource it was actually
     * given.
     */
    {
        ULONG BarOffset =
            FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.BaseAddresses[Device->MsixTableBir]);
        ULONG LowPart = 0;

        if (PciReadDeviceConfig(DeviceExtension->PciDevice,
                                &LowPart,
                                BarOffset,
                                sizeof(LowPart)) != sizeof(LowPart))
        {
            return FALSE;
        }
        BarValue = LowPart;
    }
    if (BarValue == 0 || (BarValue & PCI_ADDRESS_IO_SPACE))
        return FALSE;

    if ((BarValue & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_64BIT)
    {
        ULONG HighOffset =
            FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.BaseAddresses[Device->MsixTableBir + 1]);
        ULONG HighPart = 0;

        if (Device->MsixTableBir + 1 >= PCI_TYPE0_ADDRESSES)
            return FALSE;
        if (PciReadDeviceConfig(DeviceExtension->PciDevice,
                                &HighPart,
                                HighOffset,
                                sizeof(HighPart)) != sizeof(HighPart))
        {
            return FALSE;
        }
        BarValue = ((ULONGLONG)HighPart << 32) | (BarValue & PCI_ADDRESS_MEMORY_ADDRESS_MASK_64);
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
    KAFFINITY Affinity;
    ULONG Vector;

    Device = DeviceExtension->PciDevice;
    if (!Device || Device->MsiCapability == 0 || !RawDescriptor)
        return STATUS_NOT_SUPPORTED;

    MessageCount = 1;
    if (PCI_MSG_RAW(RawDescriptor)->MessageCount)
        MessageCount = PCI_MSG_RAW(RawDescriptor)->MessageCount;

    if (Device->MsiMaxCount != 0 && MessageCount > Device->MsiMaxCount)
        MessageCount = Device->MsiMaxCount;

    Affinity = TranslatedDescriptor ?
               TranslatedDescriptor->u.Interrupt.Affinity :
               KeQueryActiveProcessors();
    Vector = TranslatedDescriptor ?
             TranslatedDescriptor->u.Interrupt.Vector :
             PCI_MSG_RAW(RawDescriptor)->Vector;

#if defined(_M_AMD64)
    {
        HAL_MESSAGE_ROUTING_INFO RoutingInfo;
        HAL_INTERRUPT_TARGET_INFORMATION TargetInfo;
        NTSTATUS Status;

        RtlZeroMemory(&TargetInfo, sizeof(TargetInfo));
        TargetInfo.Version = HAL_INTERRUPT_TARGET_INFORMATION_VERSION;
        TargetInfo.TargetProcessors = Affinity;
        Status = HalpGetInterruptTargetInformation(&TargetInfo);
        if (NT_SUCCESS(Status) && TargetInfo.TargetProcessors != 0)
            Affinity = TargetInfo.TargetProcessors;

        RtlZeroMemory(&RoutingInfo, sizeof(RoutingInfo));
        RoutingInfo.Version = HAL_MESSAGE_ROUTING_INFO_VERSION;
        RoutingInfo.TargetProcessors = Affinity;
        RoutingInfo.Vector = Vector;
        RoutingInfo.Irql = TranslatedDescriptor ?
                           (KIRQL)TranslatedDescriptor->u.Interrupt.Level :
                           0;
        RoutingInfo.MessageCount = MessageCount;
        Status = HalpGetMessageRoutingInfo(&RoutingInfo);
        if (!NT_SUCCESS(Status))
            return Status;

        MessageAddressLow = RoutingInfo.MessageAddress.LowPart;
        MessageAddressHigh = RoutingInfo.MessageAddress.HighPart;
        MessageData = RoutingInfo.MessageData;
    }
#elif defined(_M_ARM64)
    {
        NTSTATUS Status;

        Status = PciPdoGetArm64MsiMessage(Device,
                                          Vector,
                                          Affinity,
                                          &MessageAddressLow,
                                          &MessageAddressHigh,
                                          &MessageData);
        if (!NT_SUCCESS(Status))
            return Status;
    }
#elif (NTDDI_VERSION >= NTDDI_WIN7)
    {
        HAL_MESSAGE_ROUTING_INFO RoutingInfo;
        HAL_INTERRUPT_TARGET_INFORMATION TargetInfo;
        NTSTATUS Status;

        RtlZeroMemory(&TargetInfo, sizeof(TargetInfo));
        TargetInfo.Version = HAL_INTERRUPT_TARGET_INFORMATION_VERSION;
        TargetInfo.TargetProcessors = Affinity;
        Status = HalGetInterruptTargetInformation(&TargetInfo);
        if (NT_SUCCESS(Status) && TargetInfo.TargetProcessors != 0)
            Affinity = TargetInfo.TargetProcessors;

        RtlZeroMemory(&RoutingInfo, sizeof(RoutingInfo));
        RoutingInfo.Version = HAL_MESSAGE_ROUTING_INFO_VERSION;
        RoutingInfo.TargetProcessors = Affinity;
        RoutingInfo.Vector = Vector;
        RoutingInfo.Irql = TranslatedDescriptor ?
                           (KIRQL)TranslatedDescriptor->u.Interrupt.Level :
                           0;
        RoutingInfo.MessageCount = MessageCount;
        Status = HalGetMessageRoutingInfo(&RoutingInfo);
        if (!NT_SUCCESS(Status))
            return Status;

        MessageAddressLow = RoutingInfo.MessageAddress.LowPart;
        MessageAddressHigh = RoutingInfo.MessageAddress.HighPart;
        MessageData = RoutingInfo.MessageData;
    }
#else
    MessageAddressLow = 0xFEE00000 | (PciPdoSelectDestinationId(Affinity) << 12);
    MessageAddressHigh = 0;
    MessageData = (USHORT)(Vector & 0xFF);
#endif

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
        KAFFINITY MsgAffinity;
        ULONG MsgVector;
#if defined(_M_AMD64) || (!defined(_M_ARM64) && (NTDDI_VERSION >= NTDDI_WIN7))
        HAL_MESSAGE_ROUTING_INFO RoutingInfo;
        HAL_INTERRUPT_TARGET_INFORMATION TargetInfo;
#endif
        PPCI_MSIX_TABLE_ENTRY Entry;

        MsgAffinity = Messages[i].Affinity;
        if (MsgAffinity == 0)
            MsgAffinity = KeQueryActiveProcessors();

        MsgVector = Messages[i].Vector;
#if defined(_M_AMD64)
        RtlZeroMemory(&TargetInfo, sizeof(TargetInfo));
        TargetInfo.Version = HAL_INTERRUPT_TARGET_INFORMATION_VERSION;
        TargetInfo.TargetProcessors = MsgAffinity;
        Status = HalpGetInterruptTargetInformation(&TargetInfo);
        if (NT_SUCCESS(Status) && TargetInfo.TargetProcessors != 0)
            MsgAffinity = TargetInfo.TargetProcessors;

        RtlZeroMemory(&RoutingInfo, sizeof(RoutingInfo));
        RoutingInfo.Version = HAL_MESSAGE_ROUTING_INFO_VERSION;
        RoutingInfo.TargetProcessors = MsgAffinity;
        RoutingInfo.Vector = MsgVector;
        RoutingInfo.MessageCount = 1;
        Status = HalpGetMessageRoutingInfo(&RoutingInfo);
        if (!NT_SUCCESS(Status))
        {
            MmUnmapIoSpace(TableMapping, ProgramCount * sizeof(PCI_MSIX_TABLE_ENTRY));
            return Status;
        }

        AddressLow = RoutingInfo.MessageAddress.LowPart;
        AddressHigh = RoutingInfo.MessageAddress.HighPart;
        Data = RoutingInfo.MessageData;
#elif defined(_M_ARM64)
        Status = PciPdoGetArm64MsiMessage(Device,
                                          MsgVector,
                                          MsgAffinity,
                                          &AddressLow,
                                          &AddressHigh,
                                          &Data);
        if (!NT_SUCCESS(Status))
        {
            MmUnmapIoSpace(TableMapping, ProgramCount * sizeof(PCI_MSIX_TABLE_ENTRY));
            return Status;
        }
#elif (NTDDI_VERSION >= NTDDI_WIN7)
        RtlZeroMemory(&TargetInfo, sizeof(TargetInfo));
        TargetInfo.Version = HAL_INTERRUPT_TARGET_INFORMATION_VERSION;
        TargetInfo.TargetProcessors = MsgAffinity;
        Status = HalGetInterruptTargetInformation(&TargetInfo);
        if (NT_SUCCESS(Status) && TargetInfo.TargetProcessors != 0)
            MsgAffinity = TargetInfo.TargetProcessors;

        RtlZeroMemory(&RoutingInfo, sizeof(RoutingInfo));
        RoutingInfo.Version = HAL_MESSAGE_ROUTING_INFO_VERSION;
        RoutingInfo.TargetProcessors = MsgAffinity;
        RoutingInfo.Vector = MsgVector;
        RoutingInfo.MessageCount = 1;
        Status = HalGetMessageRoutingInfo(&RoutingInfo);
        if (!NT_SUCCESS(Status))
        {
            MmUnmapIoSpace(TableMapping, ProgramCount * sizeof(PCI_MSIX_TABLE_ENTRY));
            return Status;
        }

        AddressLow = RoutingInfo.MessageAddress.LowPart;
        AddressHigh = RoutingInfo.MessageAddress.HighPart;
        Data = RoutingInfo.MessageData;
#else
        AddressLow = 0xFEE00000 | (PciPdoSelectDestinationId(MsgAffinity) << 12);
        Data = (USHORT)(MsgVector & 0xFF);
#endif

        Entry = (PPCI_MSIX_TABLE_ENTRY)((PUCHAR)TableMapping + (i * sizeof(PCI_MSIX_TABLE_ENTRY)));
        WRITE_REGISTER_ULONG(&Entry->MessageAddressLow, AddressLow);
        WRITE_REGISTER_ULONG(&Entry->MessageAddressHigh, AddressHigh);
        WRITE_REGISTER_ULONG(&Entry->MessageData, Data);
        WRITE_REGISTER_ULONG(&Entry->VectorControl, 0);
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

    UNREFERENCED_PARAMETER(IrpSp);
    DPRINT("Called\n");

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
    PPCI_DEVICE Device;
    DEVICE_POWER_STATE DeviceWake;
    USHORT PmCapabilities;
    ULONG DeviceNumber, FunctionNumber, Index;
    ULONG BytesRead;

    UNREFERENCED_PARAMETER(Irp);
    DPRINT("Called\n");

    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    DeviceCapabilities = IrpSp->Parameters.DeviceCapabilities.Capabilities;

    if (DeviceCapabilities == NULL ||
        DeviceCapabilities->Size < sizeof(DEVICE_CAPABILITIES))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DeviceCapabilities->Version != 1)
        return STATUS_REVISION_MISMATCH;

    Device = DeviceExtension->PciDevice;
    if (Device == NULL)
        return STATUS_NO_SUCH_DEVICE;

    DeviceNumber = Device->SlotNumber.u.bits.DeviceNumber;
    FunctionNumber = Device->SlotNumber.u.bits.FunctionNumber;

    DeviceCapabilities->UniqueID = FALSE;
    DeviceCapabilities->Address = ((DeviceNumber << 16) & 0xFFFF0000) + (FunctionNumber & 0xFFFF);
    DeviceCapabilities->UINumber = MAXULONG; /* FIXME */

    /*
     * A PCI PDO always maps S0 to D0. Keep the sleep-state mapping
     * conservative until the root bridge supplies platform-specific data.
     */
    DeviceCapabilities->DeviceState[PowerSystemUnspecified] = PowerDeviceUnspecified;
    DeviceCapabilities->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    for (Index = PowerSystemSleeping1; Index < PowerSystemMaximum; Index++)
        DeviceCapabilities->DeviceState[Index] = PowerDeviceD3;

    DeviceCapabilities->DeviceD1 = FALSE;
    DeviceCapabilities->DeviceD2 = FALSE;
    DeviceCapabilities->WakeFromD0 = FALSE;
    DeviceCapabilities->WakeFromD1 = FALSE;
    DeviceCapabilities->WakeFromD2 = FALSE;
    DeviceCapabilities->WakeFromD3 = FALSE;
    DeviceCapabilities->SystemWake = PowerSystemUnspecified;
    DeviceCapabilities->DeviceWake = PowerDeviceUnspecified;
    DeviceCapabilities->D1Latency = 0;
    DeviceCapabilities->D2Latency = 0;
    DeviceCapabilities->D3Latency = 0;

    PciPdoCacheMsiInfo(DeviceExtension);
    if (Device->PmCapability == 0)
        return STATUS_SUCCESS;

    BytesRead = PciPdoGetBusDataByOffset(DeviceExtension,
                                         &PmCapabilities,
                                         Device->PmCapability + PCI_PM_PMC,
                                         sizeof(PmCapabilities));
    if (BytesRead != sizeof(PmCapabilities))
        return STATUS_SUCCESS;

    DeviceCapabilities->DeviceD1 = !!(PmCapabilities & PCI_PM_CAP_D1);
    DeviceCapabilities->DeviceD2 = !!(PmCapabilities & PCI_PM_CAP_D2);
    DeviceCapabilities->WakeFromD0 = !!(PmCapabilities & PCI_PM_CAP_PME_D0);
    DeviceCapabilities->WakeFromD1 =
        DeviceCapabilities->DeviceD1 && !!(PmCapabilities & PCI_PM_CAP_PME_D1);
    DeviceCapabilities->WakeFromD2 =
        DeviceCapabilities->DeviceD2 && !!(PmCapabilities & PCI_PM_CAP_PME_D2);
    DeviceCapabilities->WakeFromD3 = !!(PmCapabilities & PCI_PM_CAP_PME_D3);

    DeviceWake = PowerDeviceUnspecified;
    if (DeviceCapabilities->WakeFromD0)
        DeviceWake = PowerDeviceD0;
    if (DeviceCapabilities->WakeFromD1)
        DeviceWake = PowerDeviceD1;
    if (DeviceCapabilities->WakeFromD2)
        DeviceWake = PowerDeviceD2;
    if (DeviceCapabilities->WakeFromD3)
        DeviceWake = PowerDeviceD3;

    DeviceCapabilities->DeviceWake = DeviceWake;
    if (DeviceWake != PowerDeviceUnspecified)
        DeviceCapabilities->SystemWake = PowerSystemWorking;

    /* PCI Bus Power Management Interface Specification maximum latencies. */
    DeviceCapabilities->D1Latency = 0;
    DeviceCapabilities->D2Latency = 2;
    DeviceCapabilities->D3Latency = 100;

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


static UCHAR
PciPdoRoutedInterruptLine(
    _In_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _In_ UCHAR InterruptPin,
    _In_ UCHAR InterruptLine)
{
#if defined(_M_IX86) || defined(_M_AMD64)
    ULONG Gsi = 0;

    if (InterruptPin != 0 &&
        HalQueryPciRoutedInterrupt(PciPdoGetSegment(DeviceExtension),
                                   DeviceExtension->PciDevice->BusNumber,
                                   DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
                                   DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber,
                                   InterruptPin,
                                   &Gsi) &&
        Gsi != 0 &&
        Gsi < 0xFF)
    {
        if ((UCHAR)Gsi != InterruptLine)
        {
            DPRINT1("PCI PDO: _PRT routes %02x:%02x.%u INT%c# to GSI %lu (line was %u)\n",
                    (UCHAR)DeviceExtension->PciDevice->BusNumber,
                    DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
                    DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber,
                    'A' + InterruptPin - 1,
                    Gsi,
                    InterruptLine);
        }

        return (UCHAR)Gsi;
    }
#endif

    return InterruptLine;
}


#define PCI_MAX_BASE_RESOURCE_DESCRIPTORS 64

static
VOID
PciPdoInitializeBarRequirement(
    _Out_ PIO_RESOURCE_DESCRIPTOR Descriptor,
    _In_ UCHAR Option,
    _In_ ULONGLONG Minimum,
    _In_ ULONGLONG Maximum,
    _In_ ULONG Length,
    _In_ ULONG Flags)
{
    RtlZeroMemory(Descriptor, sizeof(*Descriptor));
    Descriptor->Option = Option;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;

    if (Flags & PCI_ADDRESS_IO_SPACE)
    {
        Descriptor->Type = CmResourceTypePort;
        Descriptor->Flags = CM_RESOURCE_PORT_IO |
                            CM_RESOURCE_PORT_16_BIT_DECODE |
                            CM_RESOURCE_PORT_POSITIVE_DECODE |
                            CM_RESOURCE_PORT_BAR;
        Descriptor->u.Port.Length = Length;
        Descriptor->u.Port.Alignment = (Option == IO_RESOURCE_PREFERRED) ? 1 : Length;
        Descriptor->u.Port.MinimumAddress.QuadPart = Minimum;
        Descriptor->u.Port.MaximumAddress.QuadPart = Maximum;
    }
    else
    {
        Descriptor->Type = CmResourceTypeMemory;
        Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                            CM_RESOURCE_MEMORY_BAR |
            ((Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0);
        Descriptor->u.Memory.Length = Length;
        Descriptor->u.Memory.Alignment = (Option == IO_RESOURCE_PREFERRED) ? 1 : Length;
        Descriptor->u.Memory.MinimumAddress.QuadPart = Minimum;
        Descriptor->u.Memory.MaximumAddress.QuadPart = Maximum;
    }
}

static
PIO_RESOURCE_DESCRIPTOR
PciPdoAppendBarRequirements(
    _In_opt_ PFDO_DEVICE_EXTENSION FdoExtension,
    _Inout_ PIO_RESOURCE_DESCRIPTOR Descriptor,
    _In_ PIO_RESOURCE_DESCRIPTOR DescriptorEnd,
    _In_ ULONGLONG Base,
    _In_ ULONGLONG Length,
    _In_ ULONG Flags,
    _In_ ULONGLONG MaximumAddress)
{
    PPCI_ADDRESS_WINDOW Windows;
    ULONG WindowCount;
    ULONG i;
    BOOLEAN AddedAlternative = FALSE;
    BOOLEAN HasPreferred = (Base != 0);
    BOOLEAN IsLegacyMemoryBar = !(Flags & PCI_ADDRESS_IO_SPACE) && ((Flags & PCI_ADDRESS_MEMORY_TYPE_MASK) == PCI_TYPE_20BIT);

    if (Length == 0 || Length > MAXULONG)
        return Descriptor;

    if (HasPreferred && Descriptor < DescriptorEnd)
    {
        PciPdoInitializeBarRequirement(Descriptor++, IO_RESOURCE_PREFERRED, Base, Base + Length - 1, (ULONG)Length, Flags);
    }

    if (FdoExtension)
    {
        if (Flags & PCI_ADDRESS_IO_SPACE)
        {
            Windows = FdoExtension->IoWindows;
            WindowCount = FdoExtension->IoWindowCount;
        }
        else
        {
            Windows = FdoExtension->MemoryWindows;
            WindowCount = FdoExtension->MemoryWindowCount;
        }
    }
    else
    {
        Windows = NULL;
        WindowCount = 0;
    }

    for (i = 0; i < WindowCount && Descriptor < DescriptorEnd; i++)
    {
        ULONGLONG Minimum = Windows[i].Start;
        ULONGLONG Maximum = Windows[i].End;
        ULONGLONG AlignmentMask = Length - 1;

        if (!(Flags & PCI_ADDRESS_IO_SPACE) && Windows[i].Prefetchable && !(Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE))
            continue;

        /* The root bridge also publishes the VGA and option-ROM apertures
         * below 1 MiB.  Only a 20-bit memory BAR may be placed there; an
         * ordinary 32/64-bit BAR must use a real PCI memory aperture. */
        if (!(Flags & PCI_ADDRESS_IO_SPACE) && !IsLegacyMemoryBar && Minimum < 0x100000)
            Minimum = 0x100000;

        if (Maximum > MaximumAddress)
            Maximum = MaximumAddress;

        if (Minimum > MAXULONGLONG - AlignmentMask)
            continue;

        Minimum = (Minimum + AlignmentMask) & ~AlignmentMask;
        if (Minimum == 0)
            Minimum = Length;

        if (Minimum > Maximum || AlignmentMask > Maximum - Minimum)
            continue;

        PciPdoInitializeBarRequirement(Descriptor++, (!HasPreferred && !AddedAlternative) ? 0 : IO_RESOURCE_ALTERNATIVE, Minimum, Maximum, (ULONG)Length, Flags);
        AddedAlternative = TRUE;
    }

    /* Legacy firmware and non-ACPI buses may not publish parent apertures. */
    if (WindowCount == 0 && Descriptor < DescriptorEnd)
    {
        ULONGLONG Minimum = Length;

        if (!(Flags & PCI_ADDRESS_IO_SPACE) && !IsLegacyMemoryBar && Minimum < 0x100000)
            Minimum = 0x100000;
        PciPdoInitializeBarRequirement(Descriptor++, HasPreferred ? IO_RESOURCE_ALTERNATIVE : 0, Minimum, MaximumAddress, (ULONG)Length, Flags);
    }

    return Descriptor;
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
    ULONG MessageNumberLimit;
    ULONG BaseDescriptorCount;
    IO_RESOURCE_DESCRIPTOR BaseDescriptors[PCI_MAX_BASE_RESOURCE_DESCRIPTORS];
    ULONG RequirementsBusNumber;
    BOOLEAN MsixOption;
    BOOLEAN MsiOption;
    BOOLEAN LegacyOption;
    BOOLEAN InterruptResourcesAllowed;
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
    InterruptResourcesAllowed = PciPdoShouldExposeInterruptResources(&PciConfig);
    MessageNumberLimit = 0;

    PciPdoDetermineInterruptPolicy(DeviceExtension,
                                   &AllowMsi,
                                   &AllowMsix,
                                   &MessageNumberLimit);
    InterruptPin = 0;
    MsixMessageCount = 0;
    MsiMessageCount = 0;

    if (InterruptResourcesAllowed)
        InterruptPin = PciConfig.u.type0.InterruptPin;

    if (InterruptResourcesAllowed && (AllowMsi || AllowMsix))
    {
        PciPdoCacheMsiInfo(DeviceExtension);
        HasMsi = (DeviceExtension->PciDevice->MsiCapability != 0);
        HasMsix = (DeviceExtension->PciDevice->MsixCapability != 0);
        if (HasMsix)
        {
            MsixMessageCount = DeviceExtension->PciDevice->MsixTableSize;
            if (MsixMessageCount == 0)
                MsixMessageCount = 1;
            if (MessageNumberLimit != 0 && MsixMessageCount > MessageNumberLimit)
                MsixMessageCount = MessageNumberLimit;
        }
        if (HasMsi)
        {
            MsiMessageCount = DeviceExtension->PciDevice->MsiMaxCount;
            if (MsiMessageCount == 0)
                MsiMessageCount = 1;
            if (MessageNumberLimit != 0 && MsiMessageCount > MessageNumberLimit)
                MsiMessageCount = (UCHAR)MessageNumberLimit;
            MsiMessageCount = (UCHAR)PciPdoNormalizeMsiMessageCount(MsiMessageCount);
        }
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

            Descriptor = PciPdoAppendBarRequirements(FdoExtension, Descriptor, BaseDescriptors + RTL_NUMBER_OF(BaseDescriptors), Base, Length, Flags, MaximumAddress);
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

            Descriptor = PciPdoAppendBarRequirements(FdoExtension, Descriptor, BaseDescriptors + RTL_NUMBER_OF(BaseDescriptors), Base, Length, Flags, MaximumAddress);
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

        /* Bridge I/O window requirement */
        {
            UCHAR IoBase = PciConfig.u.type1.IOBase;
            UCHAR IoLimit = PciConfig.u.type1.IOLimit;
            ULONG IoBaseFull, IoLimitFull;
            ULONGLONG IoMax;

            IoBaseFull = ((ULONG)IoBase << 8) & 0xF000;
            IoLimitFull = ((ULONG)IoLimit << 8) | 0x0FFF;
            IoMax = 0xFFFF;
            if ((IoBase & PCI_IO_RANGE_TYPE_MASK) == PCI_IO_RANGE_TYPE_32)
            {
                IoBaseFull |= ((ULONG)PciConfig.u.type1.IOBaseUpper16 << 16);
                IoLimitFull |= ((ULONG)PciConfig.u.type1.IOLimitUpper16 << 16);
                IoMax = 0xFFFFFFFF;
            }
            if (IoBaseFull <= IoLimitFull)
            {
                ULONG IoLength = IoLimitFull - IoBaseFull + 1;

                /* Preferred: current assignment */
                Descriptor->Option = IO_RESOURCE_PREFERRED;
                Descriptor->Type = CmResourceTypePort;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_PORT_IO |
                                    CM_RESOURCE_PORT_POSITIVE_DECODE |
                                    CM_RESOURCE_PORT_WINDOW_DECODE;
                Descriptor->u.Port.Length = IoLength;
                Descriptor->u.Port.Alignment = 1;
                Descriptor->u.Port.MinimumAddress.QuadPart = IoBaseFull;
                Descriptor->u.Port.MaximumAddress.QuadPart = IoBaseFull + IoLength - 1;
                Descriptor++;

                /* Alternative: any valid range */
                Descriptor->Option = IO_RESOURCE_ALTERNATIVE;
                Descriptor->Type = CmResourceTypePort;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_PORT_IO |
                                    CM_RESOURCE_PORT_POSITIVE_DECODE |
                                    CM_RESOURCE_PORT_WINDOW_DECODE;
                Descriptor->u.Port.Length = IoLength;
                Descriptor->u.Port.Alignment = 0x1000;
                Descriptor->u.Port.MinimumAddress.QuadPart = 0x1000;
                Descriptor->u.Port.MaximumAddress.QuadPart = IoMax;
                Descriptor++;
            }
        }

        /* Bridge memory window requirement */
        {
            USHORT MemBase = PciConfig.u.type1.MemoryBase;
            USHORT MemLimit = PciConfig.u.type1.MemoryLimit;
            ULONG MemBaseFull, MemLimitFull;

            MemBaseFull = ((ULONG)MemBase << 16) & 0xFFF00000;
            MemLimitFull = ((ULONG)MemLimit << 16) | 0x000FFFFF;
            if (MemBaseFull <= MemLimitFull)
            {
                ULONG MemLength = MemLimitFull - MemBaseFull + 1;

                /* Preferred: current assignment */
                Descriptor->Option = IO_RESOURCE_PREFERRED;
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                                    CM_RESOURCE_MEMORY_WINDOW_DECODE;
                Descriptor->u.Memory.Length = MemLength;
                Descriptor->u.Memory.Alignment = 1;
                Descriptor->u.Memory.MinimumAddress.QuadPart = MemBaseFull;
                Descriptor->u.Memory.MaximumAddress.QuadPart = MemBaseFull + MemLength - 1;
                Descriptor++;

                /* Alternative: any valid range */
                Descriptor->Option = IO_RESOURCE_ALTERNATIVE;
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                                    CM_RESOURCE_MEMORY_WINDOW_DECODE;
                Descriptor->u.Memory.Length = MemLength;
                Descriptor->u.Memory.Alignment = 0x100000;
                Descriptor->u.Memory.MinimumAddress.QuadPart = 0x100000;
                Descriptor->u.Memory.MaximumAddress.QuadPart = 0xFFFFFFFF;
                Descriptor++;
            }
        }

        /* Bridge prefetchable memory window requirement */
        {
            USHORT PrefBase = PciConfig.u.type1.PrefetchBase;
            USHORT PrefLimit = PciConfig.u.type1.PrefetchLimit;
            ULONGLONG PrefBaseFull, PrefLimitFull;
            ULONGLONG PrefMax;

            PrefBaseFull = ((ULONGLONG)PrefBase << 16) & 0xFFF00000ULL;
            PrefLimitFull = ((ULONGLONG)PrefLimit << 16) | 0x000FFFFFULL;
            PrefMax = 0xFFFFFFFFULL;
            if ((PrefBase & PCI_PREF_RANGE_TYPE_MASK) == PCI_PREF_RANGE_TYPE_64)
            {
                PrefBaseFull |= ((ULONGLONG)PciConfig.u.type1.PrefetchBaseUpper32 << 32);
                PrefLimitFull |= ((ULONGLONG)PciConfig.u.type1.PrefetchLimitUpper32 << 32);
                PrefMax = 0xFFFFFFFFFFFFFFFFULL;
            }
            if (PrefBaseFull <= PrefLimitFull)
            {
                ULONGLONG PrefLength = PrefLimitFull - PrefBaseFull + 1;

                /* Preferred: current assignment */
                Descriptor->Option = IO_RESOURCE_PREFERRED;
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                                    CM_RESOURCE_MEMORY_PREFETCHABLE |
                                    CM_RESOURCE_MEMORY_WINDOW_DECODE;
                Descriptor->u.Memory.Length = (ULONG)PrefLength;
                Descriptor->u.Memory.Alignment = 1;
                Descriptor->u.Memory.MinimumAddress.QuadPart = PrefBaseFull;
                Descriptor->u.Memory.MaximumAddress.QuadPart = PrefBaseFull + PrefLength - 1;
                Descriptor++;

                /* Alternative: any valid range */
                Descriptor->Option = IO_RESOURCE_ALTERNATIVE;
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                                    CM_RESOURCE_MEMORY_PREFETCHABLE |
                                    CM_RESOURCE_MEMORY_WINDOW_DECODE;
                Descriptor->u.Memory.Length = (ULONG)PrefLength;
                Descriptor->u.Memory.Alignment = 0x100000;
                Descriptor->u.Memory.MinimumAddress.QuadPart = 0x100000;
                Descriptor->u.Memory.MaximumAddress.QuadPart = PrefMax;
                Descriptor++;
            }
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
    LegacyOption = (InterruptResourcesAllowed && InterruptPin != 0);
    if (FdoExtension && FdoExtension->OscMasked)
    {
        ULONG Masked = FdoExtension->OscMasked;
        if (!AllowMsi || (Masked & HAL_ACPI_OSC_SUPPORT_MSI))
            MsiOption = FALSE;
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
                Dest->Flags = CM_RESOURCE_INTERRUPT_LATCHED |
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
    UCHAR ExposableRoutedLine = 0;
    BOOLEAN ExposeInterrupt = FALSE;

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

    DPRINT("Command register: 0x%04hx\n", PciConfig.Command);

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

            if (Length && Base != 0)
                ResCount++;
        }

        if (PciPdoShouldExposeInterruptResources(&PciConfig) &&
            PciConfig.u.type0.InterruptPin != 0)
        {
            ExposableRoutedLine = PciPdoRoutedInterruptLine(DeviceExtension,
                                                            PciConfig.u.type0.InterruptPin,
                                                            PciConfig.u.type0.InterruptLine);
            ExposeInterrupt = ExposableRoutedLine != 0 &&
                              ExposableRoutedLine != 0xFF;
        }

        if (ExposeInterrupt)
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

            if (Length != 0 && Base != 0)
                ResCount++;
        }

        if (DeviceExtension->PciDevice->PciConfig.BaseClass == PCI_CLASS_BRIDGE_DEV)
            ResCount++;

        /* Count bridge window resources */
        {
            UCHAR IoBase = PciConfig.u.type1.IOBase;
            UCHAR IoLimit = PciConfig.u.type1.IOLimit;
            ULONG IoBaseFull, IoLimitFull;

            IoBaseFull = ((ULONG)IoBase << 8) & 0xF000;
            IoLimitFull = ((ULONG)IoLimit << 8) | 0x0FFF;
            if ((IoBase & PCI_IO_RANGE_TYPE_MASK) == PCI_IO_RANGE_TYPE_32)
            {
                IoBaseFull |= ((ULONG)PciConfig.u.type1.IOBaseUpper16 << 16);
                IoLimitFull |= ((ULONG)PciConfig.u.type1.IOLimitUpper16 << 16);
            }
            if (IoBaseFull <= IoLimitFull)
                ResCount++;
        }
        {
            USHORT MemBase = PciConfig.u.type1.MemoryBase;
            USHORT MemLimit = PciConfig.u.type1.MemoryLimit;
            ULONG MemBaseFull, MemLimitFull;

            MemBaseFull = ((ULONG)MemBase << 16) & 0xFFF00000;
            MemLimitFull = ((ULONG)MemLimit << 16) | 0x000FFFFF;
            if (MemBaseFull <= MemLimitFull)
                ResCount++;
        }
        {
            USHORT PrefBase = PciConfig.u.type1.PrefetchBase;
            USHORT PrefLimit = PciConfig.u.type1.PrefetchLimit;
            ULONGLONG PrefBaseFull, PrefLimitFull;

            PrefBaseFull = ((ULONGLONG)PrefBase << 16) & 0xFFF00000ULL;
            PrefLimitFull = ((ULONGLONG)PrefLimit << 16) | 0x000FFFFFULL;
            if ((PrefBase & PCI_PREF_RANGE_TYPE_MASK) == PCI_PREF_RANGE_TYPE_64)
            {
                PrefBaseFull |= ((ULONGLONG)PciConfig.u.type1.PrefetchBaseUpper32 << 32);
                PrefLimitFull |= ((ULONGLONG)PciConfig.u.type1.PrefetchLimitUpper32 << 32);
            }
            if (PrefBaseFull <= PrefLimitFull)
                ResCount++;
        }
    }
    else if (PCI_CONFIGURATION_TYPE(&PciConfig) == PCI_CARDBUS_BRIDGE_TYPE)
    {
        /* FIXME: Count Cardbus bridge resources */
    }
    else
    {
        DPRINT1("Unsupported header type %d\n", PCI_CONFIGURATION_TYPE(&PciConfig));
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

            if (Length == 0 || Base == 0)
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
                    ((Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0);
                Descriptor->u.Memory.Start.QuadPart = (ULONGLONG)Base;
                Descriptor->u.Memory.Length = Length;

                /* Enable memory space access */
                DeviceExtension->PciDevice->EnableMemorySpace = TRUE;
            }

            Descriptor++;
        }

        /* Add interrupt resource */
        if (ExposeInterrupt)
        {
            Descriptor->Type = CmResourceTypeInterrupt;
            Descriptor->ShareDisposition = CmResourceShareShared;
            Descriptor->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
            Descriptor->u.Interrupt.Level = ExposableRoutedLine;
            Descriptor->u.Interrupt.Vector = ExposableRoutedLine;
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

            if (Length == 0 || Base == 0)
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
                    ((Flags & PCI_ADDRESS_MEMORY_PREFETCHABLE) ? CM_RESOURCE_MEMORY_PREFETCHABLE : 0);
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
            Descriptor++;
        }

        /* Report bridge I/O window */
        {
            UCHAR IoBase = PciConfig.u.type1.IOBase;
            UCHAR IoLimit = PciConfig.u.type1.IOLimit;
            ULONG IoBaseFull, IoLimitFull;

            IoBaseFull = ((ULONG)IoBase << 8) & 0xF000;
            IoLimitFull = ((ULONG)IoLimit << 8) | 0x0FFF;
            if ((IoBase & PCI_IO_RANGE_TYPE_MASK) == PCI_IO_RANGE_TYPE_32)
            {
                IoBaseFull |= ((ULONG)PciConfig.u.type1.IOBaseUpper16 << 16);
                IoLimitFull |= ((ULONG)PciConfig.u.type1.IOLimitUpper16 << 16);
            }
            if (IoBaseFull <= IoLimitFull)
            {
                Descriptor->Type = CmResourceTypePort;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_PORT_IO |
                                    CM_RESOURCE_PORT_POSITIVE_DECODE |
                                    CM_RESOURCE_PORT_WINDOW_DECODE;
                Descriptor->u.Port.Start.QuadPart = IoBaseFull;
                Descriptor->u.Port.Length = IoLimitFull - IoBaseFull + 1;
                Descriptor++;
            }
        }

        /* Report bridge memory window */
        {
            USHORT MemBase = PciConfig.u.type1.MemoryBase;
            USHORT MemLimit = PciConfig.u.type1.MemoryLimit;
            ULONG MemBaseFull, MemLimitFull;

            MemBaseFull = ((ULONG)MemBase << 16) & 0xFFF00000;
            MemLimitFull = ((ULONG)MemLimit << 16) | 0x000FFFFF;
            if (MemBaseFull <= MemLimitFull)
            {
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                                    CM_RESOURCE_MEMORY_WINDOW_DECODE;
                Descriptor->u.Memory.Start.QuadPart = MemBaseFull;
                Descriptor->u.Memory.Length = MemLimitFull - MemBaseFull + 1;
                Descriptor++;
            }
        }

        /* Report bridge prefetchable memory window */
        {
            USHORT PrefBase = PciConfig.u.type1.PrefetchBase;
            USHORT PrefLimit = PciConfig.u.type1.PrefetchLimit;
            ULONGLONG PrefBaseFull, PrefLimitFull;

            PrefBaseFull = ((ULONGLONG)PrefBase << 16) & 0xFFF00000ULL;
            PrefLimitFull = ((ULONGLONG)PrefLimit << 16) | 0x000FFFFFULL;
            if ((PrefBase & PCI_PREF_RANGE_TYPE_MASK) == PCI_PREF_RANGE_TYPE_64)
            {
                PrefBaseFull |= ((ULONGLONG)PciConfig.u.type1.PrefetchBaseUpper32 << 32);
                PrefLimitFull |= ((ULONGLONG)PciConfig.u.type1.PrefetchLimitUpper32 << 32);
            }
            if (PrefBaseFull <= PrefLimitFull)
            {
                Descriptor->Type = CmResourceTypeMemory;
                Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
                Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE |
                                    CM_RESOURCE_MEMORY_PREFETCHABLE |
                                    CM_RESOURCE_MEMORY_WINDOW_DECODE;
                Descriptor->u.Memory.Start.QuadPart = PrefBaseFull;
                Descriptor->u.Memory.Length = (ULONG)(PrefLimitFull - PrefBaseFull + 1);
                Descriptor++;
            }
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
    else if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                              &GUID_AGP_TARGET_BUS_INTERFACE_STANDARD, sizeof(GUID)) == sizeof(GUID))
    {
        DPRINT("GUID_AGP_TARGET_BUS_INTERFACE_STANDARD requested\n");
        Status = STATUS_NOT_SUPPORTED;
    }
    else if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                              &GUID_PCI_PME_INTERFACE, sizeof(GUID)) == sizeof(GUID))
    {
        DPRINT("GUID_PCI_PME_INTERFACE requested\n");
        Status = STATUS_NOT_SUPPORTED;
    }
    else if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                              &GUID_PCI_EXPRESS_LINK_QUIESCENT_INTERFACE, sizeof(GUID)) == sizeof(GUID))
    {
        DPRINT("GUID_PCI_EXPRESS_LINK_QUIESCENT_INTERFACE requested\n");
        Status = STATUS_NOT_SUPPORTED;
    }
    else if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                              &GUID_PCI_EXPRESS_ROOT_PORT_INTERFACE, sizeof(GUID)) == sizeof(GUID))
    {
        DPRINT("GUID_PCI_EXPRESS_ROOT_PORT_INTERFACE requested\n");
        Status = STATUS_NOT_SUPPORTED;
    }
    else if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                              &GUID_PCI_VIRTUALIZATION_INTERFACE, sizeof(GUID)) == sizeof(GUID))
    {
        DPRINT("GUID_PCI_VIRTUALIZATION_INTERFACE requested\n");
        Status = STATUS_NOT_SUPPORTED;
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

static
NTSTATUS
PciPdoProgramType0Bars(
    _Inout_ PPDO_DEVICE_EXTENSION DeviceExtension,
    _In_ PCM_RESOURCE_LIST ResourceList)
{
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Assigned[PCI_TYPE0_ADDRESSES];
    BOOLEAN Used[PCI_TYPE0_ADDRESSES];
    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
    PCI_COMMON_CONFIG *PciConfig = &DeviceExtension->PciDevice->PciConfig;
    ULONG OriginalBars[PCI_TYPE0_ADDRESSES];
    ULONG AssignedCount = 0;
    ULONG FullIndex;
    UCHAR Bar;
    USHORT OriginalCommand;
    USHORT DisabledCommand;
    NTSTATUS Status = STATUS_SUCCESS;

    if (PCI_CONFIGURATION_TYPE(PciConfig) != PCI_DEVICE_TYPE)
        return STATUS_SUCCESS;

    RtlZeroMemory(Assigned, sizeof(Assigned));
    RtlZeroMemory(Used, sizeof(Used));
    RtlCopyMemory(OriginalBars, PciConfig->u.type0.BaseAddresses, sizeof(OriginalBars));
    FullDescriptor = &ResourceList->List[0];
    for (FullIndex = 0; FullIndex < ResourceList->Count; FullIndex++)
    {
        PCM_PARTIAL_RESOURCE_LIST PartialList = &FullDescriptor->PartialResourceList;
        ULONG PartialIndex;

        for (PartialIndex = 0; PartialIndex < PartialList->Count; PartialIndex++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor = &PartialList->PartialDescriptors[PartialIndex];

            if (AssignedCount >= RTL_NUMBER_OF(Assigned))
                break;
            if (PartialDescriptor->Type == CmResourceTypeMemory &&
                !(PartialDescriptor->Flags & CM_RESOURCE_MEMORY_WINDOW_DECODE))
            {
                Assigned[AssignedCount++] = PartialDescriptor;
            }
            else if (PartialDescriptor->Type == CmResourceTypePort &&
                     !(PartialDescriptor->Flags & CM_RESOURCE_PORT_WINDOW_DECODE))
            {
                Assigned[AssignedCount++] = PartialDescriptor;
            }
        }

        FullDescriptor = CmiGetNextResourceDescriptor(FullDescriptor);
    }

    OriginalCommand = PciConfig->Command;
    DisabledCommand = OriginalCommand & ~(PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_IO_SPACE);
    if (DisabledCommand != OriginalCommand &&
        PciPdoSetBusDataByOffset(DeviceExtension, &DisabledCommand, FIELD_OFFSET(PCI_COMMON_CONFIG, Command), sizeof(DisabledCommand)) != sizeof(DisabledCommand))
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    for (Bar = 0; Bar < PCI_TYPE0_ADDRESSES;)
    {
        UCHAR CurrentBar = Bar;
        UCHAR NextBar;
        ULONGLONG Base;
        ULONGLONG Length;
        ULONGLONG MaximumAddress;
        ULONGLONG AssignedBase = 0;
        ULONGLONG AddressMask;
        ULONGLONG BarValue;
        ULONG Flags;
        ULONG ResourceIndex;
        ULONG LowPart;
        ULONG HighPart = 0;
        ULONG ReadLow = 0;
        ULONG ReadHigh = 0;
        BOOLEAN Found = FALSE;
        BOOLEAN IsIo;

        if (!PdoGetRangeLength(DeviceExtension, CurrentBar, &Base, &Length, &Flags, &NextBar, &MaximumAddress))
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            break;
        }
        Bar = NextBar;
        if (Length == 0)
            continue;

        IsIo = !!(Flags & PCI_ADDRESS_IO_SPACE);
        for (ResourceIndex = 0; ResourceIndex < AssignedCount; ResourceIndex++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Resource = Assigned[ResourceIndex];

            if (Used[ResourceIndex])
                continue;
            if ((IsIo && Resource->Type != CmResourceTypePort) ||
                (!IsIo && Resource->Type != CmResourceTypeMemory))
            {
                continue;
            }
            if (Resource->u.Memory.Length != Length)
                continue;

            AssignedBase = Resource->u.Memory.Start.QuadPart;
            Used[ResourceIndex] = TRUE;
            Found = TRUE;
            break;
        }

        if (!Found)
        {
            if (Base == 0)
            {
                DPRINT1("PCI PDO: no assigned resource for BAR%u on %02x:%02x.%u\n",
                        CurrentBar,
                        (UCHAR)DeviceExtension->PciDevice->BusNumber,
                        DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
                        DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber);
                Status = STATUS_CONFLICTING_ADDRESSES;
                break;
            }
            continue;
        }

        if (AssignedBase == 0 || AssignedBase > MaximumAddress ||
            Length - 1 > MaximumAddress - AssignedBase ||
            (AssignedBase & (Length - 1)) != 0)
        {
            DPRINT1("PCI PDO: invalid BAR%u assignment 0x%I64x len 0x%I64x max 0x%I64x\n",
                    CurrentBar, AssignedBase, Length, MaximumAddress);
            Status = STATUS_CONFLICTING_ADDRESSES;
            break;
        }

        AddressMask = IsIo ? PCI_ADDRESS_IO_ADDRESS_MASK_64 : PCI_ADDRESS_MEMORY_ADDRESS_MASK_64;
        BarValue = (AssignedBase & AddressMask) | (Flags & ~AddressMask);
        LowPart = (ULONG)BarValue;
        HighPart = (ULONG)(BarValue >> 32);

        if (PciPdoSetBusDataByOffset(DeviceExtension, &LowPart, 0x10 + CurrentBar * sizeof(ULONG), sizeof(LowPart)) != sizeof(LowPart))
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            break;
        }
        if (NextBar == CurrentBar + 2 &&
            PciPdoSetBusDataByOffset(DeviceExtension, &HighPart, 0x10 + (CurrentBar + 1) * sizeof(ULONG), sizeof(HighPart)) != sizeof(HighPart))
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            break;
        }

        if (PciPdoGetBusDataByOffset(DeviceExtension, &ReadLow, 0x10 + CurrentBar * sizeof(ULONG), sizeof(ReadLow)) != sizeof(ReadLow) ||
            (NextBar == CurrentBar + 2 &&
             PciPdoGetBusDataByOffset(DeviceExtension, &ReadHigh, 0x10 + (CurrentBar + 1) * sizeof(ULONG), sizeof(ReadHigh)) != sizeof(ReadHigh)))
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            break;
        }

        BarValue = ((ULONGLONG)ReadHigh << 32) | ReadLow;
        if ((BarValue & AddressMask) != AssignedBase)
        {
            DPRINT1("PCI PDO: BAR%u readback mismatch assigned 0x%I64x read 0x%I64x\n",
                    CurrentBar, AssignedBase, BarValue & AddressMask);
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
            break;
        }

        PciConfig->u.type0.BaseAddresses[CurrentBar] = ReadLow;
        if (NextBar == CurrentBar + 2)
            PciConfig->u.type0.BaseAddresses[CurrentBar + 1] = ReadHigh;

        DPRINT1("PCI PDO: assigned BAR%u=0x%I64x len=0x%I64x for %02x:%02x.%u\n",
                CurrentBar,
                AssignedBase,
                Length,
                (UCHAR)DeviceExtension->PciDevice->BusNumber,
                DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
                DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber);
    }

    if (!NT_SUCCESS(Status))
    {
        ULONG RestoreBar;

        for (RestoreBar = 0; RestoreBar < PCI_TYPE0_ADDRESSES; RestoreBar++)
        {
            if (PciPdoSetBusDataByOffset(DeviceExtension, &OriginalBars[RestoreBar], 0x10 + RestoreBar * sizeof(ULONG), sizeof(ULONG)) != sizeof(ULONG))
                DPRINT1("PCI PDO: failed to restore BAR%lu after start failure\n", RestoreBar);
        }
        RtlCopyMemory(PciConfig->u.type0.BaseAddresses, OriginalBars, sizeof(OriginalBars));
    }

    if (DisabledCommand != OriginalCommand)
    {
        if (PciPdoSetBusDataByOffset(DeviceExtension, &OriginalCommand, FIELD_OFFSET(PCI_COMMON_CONFIG, Command), sizeof(OriginalCommand)) != sizeof(OriginalCommand) &&
            NT_SUCCESS(Status))
        {
            Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        }
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
        return STATUS_SUCCESS;

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
    if (PciGetPowerLevel(DeviceExtension->PciDevice) != PowerDeviceD0)
    {
        DEVICE_POWER_STATE PreviousPowerState = PciGetPowerLevel(DeviceExtension->PciDevice);
        NTSTATUS AcpiPowerStatus;

        DPRINT1("PCI PDO: waking %u:%02x:%02x.%u from D%u before BAR programming\n", Segment, (UCHAR)DeviceExtension->PciDevice->BusNumber, DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber, DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber, PreviousPowerState - PowerDeviceD0);
        AcpiPowerStatus = PciAcpiSetPower(Segment, DeviceExtension->PciDevice->BusNumber, DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber, DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber, 0);
        if (!NT_SUCCESS(AcpiPowerStatus) && AcpiPowerStatus != STATUS_NOT_FOUND && AcpiPowerStatus != STATUS_NOT_SUPPORTED)
            DPRINT1("PCI PDO: ACPI D0 transition failed for %u:%02x:%02x.%u (0x%08lx)\n", Segment, (UCHAR)DeviceExtension->PciDevice->BusNumber, DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber, DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber, AcpiPowerStatus);
        PciSetPowerLevel(DeviceExtension->PciDevice, PowerDeviceD0);
        DeviceExtension->Common.DevicePowerState = PowerDeviceD0;
        PciPdoGetBusData(DeviceExtension, &DeviceExtension->PciDevice->PciConfig, PCI_COMMON_HDR_LENGTH);
    }
    Status = PciPdoProgramType0Bars(DeviceExtension, RawResList);
    if (!NT_SUCCESS(Status))
        return Status;

    PciPdoCacheMsiInfo(DeviceExtension);
    PciPdoPublishDeviceProperties(DeviceExtension);
    if (DeviceExtension->PciDevice->MsixCapability)
    {
        MsixMessageLimit = DeviceExtension->PciDevice->MsixTableSize;
        if (MsixMessageLimit == 0)
            MsixMessageLimit = 1;
    }

    PciPdoDetermineInterruptPolicy(DeviceExtension,
                                   &MsiAllowed,
                                   &MsixAllowed,
                                   NULL);

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
                                TranslatedPartialDesc->u.Interrupt.Vector :
                                PCI_MSG_RAW(RawPartialDesc)->Vector;
                            MsixMessages[MsixMessageCount].Affinity =
                                TranslatedPartialDesc ?
                                TranslatedPartialDesc->u.Interrupt.Affinity :
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

    DPRINT1("PCI PDO: MSI/X state UsingMsix=%d MsixMessageCount=%lu MsixMessages=%p UsingMsi=%d\n",
            UsingMsix, MsixMessageCount, MsixMessages, UsingMsi);

    if (UsingMsix && MsixMessageCount > 0 && MsixMessages)
    {
        DPRINT1("PCI PDO: calling PciPdoEnableMsix MessageCount=%lu Vector[0]=%u Affinity[0]=0x%Ix\n",
                MsixMessageCount, MsixMessages[0].Vector, MsixMessages[0].Affinity);
        MsixStatus = PciPdoEnableMsix(DeviceExtension,
                                      MsixMessages,
                                      MsixMessageCount);
        DPRINT1("PCI PDO: PciPdoEnableMsix returned 0x%08lx\n", MsixStatus);
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

    return Status;
}

static NTSTATUS
PdoReadConfig(
    IN PDEVICE_OBJECT DeviceObject,
    IN PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
{
    ULONG Size;

    DPRINT("PdoReadConfig() called\n");

    Size = InterfaceBusGetBusData(DeviceObject,
                                  IrpSp->Parameters.ReadWriteConfig.WhichSpace,
                                  IrpSp->Parameters.ReadWriteConfig.Buffer,
                                  IrpSp->Parameters.ReadWriteConfig.Offset,
                                  IrpSp->Parameters.ReadWriteConfig.Length);

    if (Size != IrpSp->Parameters.ReadWriteConfig.Length)
    {
        DPRINT1("Size %lu  Length %lu\n", Size, IrpSp->Parameters.ReadWriteConfig.Length);
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

    DPRINT1("PdoWriteConfig() called\n");

    /* Get PCI configuration space */
    Size = InterfaceBusSetBusData(DeviceObject,
                                  IrpSp->Parameters.ReadWriteConfig.WhichSpace,
                                  IrpSp->Parameters.ReadWriteConfig.Buffer,
                                  IrpSp->Parameters.ReadWriteConfig.Offset,
                                  IrpSp->Parameters.ReadWriteConfig.Length);

    if (Size != IrpSp->Parameters.ReadWriteConfig.Length)
    {
        DPRINT1("Size %lu  Length %lu\n", Size, IrpSp->Parameters.ReadWriteConfig.Length);
        Irp->IoStatus.Information = 0;
        return STATUS_UNSUCCESSFUL;
    }

    Irp->IoStatus.Information = Size;

    return STATUS_SUCCESS;
}

static
VOID
PciPdoDisableDecodes(_Inout_ PPDO_DEVICE_EXTENSION DeviceExtension)
{
    USHORT Command;
    USHORT DisabledCommand;

    if (PciPdoGetBusDataByOffset(DeviceExtension, &Command, FIELD_OFFSET(PCI_COMMON_CONFIG, Command), sizeof(Command)) != sizeof(Command))
    {
        DPRINT1("PCI PDO: failed to read command register while disabling %u:%02x:%02x.%u\n", PciPdoGetSegment(DeviceExtension), (UCHAR)DeviceExtension->PciDevice->BusNumber, DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber, DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber);
        return;
    }

    DisabledCommand = Command & ~(PCI_ENABLE_IO_SPACE | PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER);
    if (DisabledCommand != Command && PciPdoSetBusDataByOffset(DeviceExtension, &DisabledCommand, FIELD_OFFSET(PCI_COMMON_CONFIG, Command), sizeof(DisabledCommand)) != sizeof(DisabledCommand))
    {
        DPRINT1("PCI PDO: failed to disable command decoding for %u:%02x:%02x.%u\n", PciPdoGetSegment(DeviceExtension), (UCHAR)DeviceExtension->PciDevice->BusNumber, DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber, DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber);
        return;
    }

    DeviceExtension->PciDevice->PciConfig.Command = DisabledCommand;
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
    PPDO_DEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;

    DPRINT("Called\n");

    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;

    Status = Irp->IoStatus.Status;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    Status = IoAcquireRemoveLock(&DeviceExtension->RemoveLock, Irp);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
            DPRINT("IRP_MN_DEVICE_USAGE_NOTIFICATION received\n");
            Status = STATUS_SUCCESS;
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
            DPRINT("IRP_MN_QUERY_DEVICE_TEXT received\n");
            Status = PdoQueryDeviceText(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_ID:
            DPRINT("IRP_MN_QUERY_ID received\n");
            Status = PdoQueryId(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
        {
            DPRINT("IRP_MN_QUERY_PNP_DEVICE_STATE received\n");
            Irp->IoStatus.Information = 0;
            if (DeviceExtension->PciDevice->IsDebuggingDevice)
            {
                Irp->IoStatus.Information |= PNP_DEVICE_NOT_DISABLEABLE;
            }
            if (PciPdoNeedsMessageInterruptRequirementsRefresh(DeviceExtension))
            {
                DPRINT1("PCI PDO: requesting resource requirements refresh for %u:%02x:%02x.%u\n",
                        PciPdoGetSegment(DeviceExtension),
                        (UCHAR)DeviceExtension->PciDevice->BusNumber,
                        DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
                        DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber);
                Irp->IoStatus.Information |= PNP_DEVICE_RESOURCE_REQUIREMENTS_CHANGED;
            }
            Status = STATUS_SUCCESS;
            break;
        }

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            DPRINT("IRP_MN_QUERY_RESOURCE_REQUIREMENTS received\n");
            Status = PdoQueryResourceRequirements(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_RESOURCES:
            DPRINT("IRP_MN_QUERY_RESOURCES received\n");
            Status = PdoQueryResources(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_SET_LOCK:
            DPRINT("IRP_MN_SET_LOCK received\n");
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_START_DEVICE:
            Status = PdoStartDevice(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_STOP_DEVICE:
        case IRP_MN_SURPRISE_REMOVAL:
            PciPdoDisableDecodes(DeviceExtension);
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            PciPdoDisableDecodes(DeviceExtension);
            IoReleaseRemoveLockAndWait(&DeviceExtension->RemoveLock, Irp);
            Status = STATUS_SUCCESS;
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

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

    IoReleaseRemoveLock(&DeviceExtension->RemoveLock, Irp);

    if (Status != STATUS_PENDING)
    {
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
    }

    DPRINT("Leaving. Status 0x%X\n", Status);

    return Status;
}

/**
 * @brief Set the PCI PM capability D-state for a device.
 *
 * Programs the PowerState field (bits 1:0) of the PCI Power Management
 * Capability Status/Control Register (PMCSR) to transition the device
 * to the requested D-state.
 */
VOID
PciSetPowerLevel(
    _In_ PPCI_DEVICE Device,
    _In_ DEVICE_POWER_STATE DeviceState)
{
    USHORT Pmcsr;
    UCHAR DStateBits;

    if (Device->PmCapability == 0)
    {
        DPRINT("PciSetPowerLevel: No PM capability, skipping\n");
        return;
    }

    /* Read current PMCSR (2 bytes at PM capability offset + 4) */
    PciReadDeviceConfig(Device, &Pmcsr, Device->PmCapability + 4, sizeof(USHORT));

    /* Map DEVICE_POWER_STATE to hardware D-state bits */
    switch (DeviceState)
    {
        case PowerDeviceD0: DStateBits = 0; break;
        case PowerDeviceD1: DStateBits = 1; break;
        case PowerDeviceD2: DStateBits = 2; break;
        case PowerDeviceD3: DStateBits = 3; break;
        default:
            DPRINT("PciSetPowerLevel: Invalid D-state %d\n", DeviceState);
            return;
    }

    /* Clear D-state bits (1:0) and set new value */
    Pmcsr = (Pmcsr & ~(USHORT)0x3) | DStateBits;

    PciWriteDeviceConfig(Device, &Pmcsr, Device->PmCapability + 4, sizeof(USHORT));

    DPRINT("PciSetPowerLevel: Bus %lu Dev %lu Func %lu -> D%d (PMCSR=0x%04x)\n",
           Device->BusNumber,
           Device->SlotNumber.u.bits.DeviceNumber,
           Device->SlotNumber.u.bits.FunctionNumber,
           DStateBits, Pmcsr);

    /* D3->D0 recovery delay per PCI PM spec (10ms minimum) */
    if (DeviceState == PowerDeviceD0)
    {
        LARGE_INTEGER Delay;
        Delay.QuadPart = -10 * 1000 * 10; /* 10ms in 100ns units, negative = relative */
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    }
}

/**
 * @brief Read the current D-state from the PCI PM capability.
 */
DEVICE_POWER_STATE
PciGetPowerLevel(
    _In_ PPCI_DEVICE Device)
{
    USHORT Pmcsr;
    UCHAR DStateBits;

    if (Device->PmCapability == 0)
        return PowerDeviceD0;

    PciReadDeviceConfig(Device, &Pmcsr, Device->PmCapability + 4, sizeof(USHORT));

    DStateBits = (UCHAR)(Pmcsr & 0x3);

    switch (DStateBits)
    {
        case 0: return PowerDeviceD0;
        case 1: return PowerDeviceD1;
        case 2: return PowerDeviceD2;
        case 3: return PowerDeviceD3;
        default: return PowerDeviceD0;
    }
}

/**
 * @brief Save PCI config space header into the cached PciConfig structure.
 *
 * Reads the full PCI common header from hardware into Device->PciConfig
 * so it can be restored after a D-state transition back to D0.
 */
VOID
PciSaveDeviceConfig(
    _In_ PPCI_DEVICE Device)
{
    PciReadDeviceConfig(Device,
                        &Device->PciConfig,
                        0,
                        PCI_COMMON_HDR_LENGTH);

    DPRINT("PciSaveDeviceConfig: Bus %lu Dev %lu Func %lu saved "
           "BARs[0]=0x%08lx Command=0x%04x IntLine=%u CacheLine=%u Latency=%u\n",
           Device->BusNumber,
           Device->SlotNumber.u.bits.DeviceNumber,
           Device->SlotNumber.u.bits.FunctionNumber,
           Device->PciConfig.u.type0.BaseAddresses[0],
           Device->PciConfig.Command,
           Device->PciConfig.u.type0.InterruptLine,
           Device->PciConfig.CacheLineSize,
           Device->PciConfig.LatencyTimer);
}

/**
 * @brief Restore PCI config space from cached values after D0 transition.
 *
 * Writes back the saved BARs, command register, interrupt line, and
 * cache line size + latency timer from the cached PciConfig structure.
 */
VOID
PciRestoreDeviceConfig(
    _In_ PPCI_DEVICE Device)
{
    /* Restore BARs */
    PciWriteDeviceConfig(Device,
                         &Device->PciConfig.u.type0.BaseAddresses[0],
                         FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.BaseAddresses[0]),
                         sizeof(Device->PciConfig.u.type0.BaseAddresses));

    /* Restore command register */
    PciWriteDeviceConfig(Device,
                         &Device->PciConfig.Command,
                         FIELD_OFFSET(PCI_COMMON_CONFIG, Command),
                         sizeof(USHORT));

    /* Restore interrupt line */
    PciWriteDeviceConfig(Device,
                         &Device->PciConfig.u.type0.InterruptLine,
                         FIELD_OFFSET(PCI_COMMON_CONFIG, u.type0.InterruptLine),
                         sizeof(UCHAR));

    /* Restore cache line size and latency timer (2 contiguous bytes) */
    PciWriteDeviceConfig(Device,
                         &Device->PciConfig.CacheLineSize,
                         FIELD_OFFSET(PCI_COMMON_CONFIG, CacheLineSize),
                         2 * sizeof(UCHAR));

    DPRINT("PciRestoreDeviceConfig: Bus %lu Dev %lu Func %lu restored "
           "BARs[0]=0x%08lx Command=0x%04x IntLine=%u CacheLine=%u Latency=%u\n",
           Device->BusNumber,
           Device->SlotNumber.u.bits.DeviceNumber,
           Device->SlotNumber.u.bits.FunctionNumber,
           Device->PciConfig.u.type0.BaseAddresses[0],
           Device->PciConfig.Command,
           Device->PciConfig.u.type0.InterruptLine,
           Device->PciConfig.CacheLineSize,
           Device->PciConfig.LatencyTimer);
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
    PPDO_DEVICE_EXTENSION DeviceExtension;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("Called\n");

    DeviceExtension = (PPDO_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_SET_POWER:
            if (IrpSp->Parameters.Power.Type == DevicePowerState)
            {
                DEVICE_POWER_STATE NewState = IrpSp->Parameters.Power.State.DeviceState;
                DEVICE_POWER_STATE OldState = DeviceExtension->Common.DevicePowerState;

                DPRINT("PDO IRP_MN_SET_POWER: D%d -> D%d\n",
                       OldState - PowerDeviceD0, NewState - PowerDeviceD0);

                if (NewState == OldState)
                {
                    /* No transition needed */
                    Status = STATUS_SUCCESS;
                }
                else if (NewState == PowerDeviceD0)
                {
                    /* Transitioning to D0: program PM cap then restore config */
                    if (DeviceExtension->PciDevice)
                    {
                        PPCI_DEVICE Device = DeviceExtension->PciDevice;

                        PciSetPowerLevel(Device, PowerDeviceD0);
                        PciRestoreDeviceConfig(Device);

                        /* Enable I/O and memory decoding, using IPI synchronization
                         * for debug devices to prevent races with KD on other CPUs */
                        if (Device->IsDebuggingDevice)
                        {
                            KdDisableDebugger();
                            PciExecuteIpiConfig(PciDecodeEnable,
                                                Device,
                                                (PVOID)(ULONG_PTR)TRUE);
                            KdEnableDebugger();
                        }
                        else
                        {
                            PciDecodeEnable(Device, (PVOID)(ULONG_PTR)TRUE);
                        }
                    }
                    Status = STATUS_SUCCESS;
                }
                else
                {
                    /* Transitioning to Dx (D1/D2/D3): save config then set PM level */
                    if (DeviceExtension->PciDevice)
                    {
                        PciSaveDeviceConfig(DeviceExtension->PciDevice);
                        PciSetPowerLevel(DeviceExtension->PciDevice, NewState);
                    }
                    Status = STATUS_SUCCESS;
                }

                /* Update power state */
                DeviceExtension->Common.DevicePowerState = NewState;
                PoSetPowerState(DeviceObject, DevicePowerState, IrpSp->Parameters.Power.State);
            }
            else if (IrpSp->Parameters.Power.Type == SystemPowerState)
            {
                DPRINT("PDO IRP_MN_SET_POWER: System state S%d\n",
                       IrpSp->Parameters.Power.State.SystemState - PowerSystemWorking);
                Status = STATUS_SUCCESS;
            }
            break;

        case IRP_MN_QUERY_POWER:
            /* PDO always succeeds power queries */
            DPRINT("PDO IRP_MN_QUERY_POWER\n");
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_WAIT_WAKE:
        {
            KIRQL OldIrql;

            DPRINT("PDO IRP_MN_WAIT_WAKE\n");

            /* Check if the device supports PME via PM capability */
            if (!DeviceExtension->PciDevice ||
                DeviceExtension->PciDevice->PmCapability == 0)
            {
                Status = STATUS_NOT_SUPPORTED;
                break;
            }

            KeAcquireSpinLock(&DeviceExtension->WaitWakeSpinLock, &OldIrql);

            if (DeviceExtension->WaitWakeState != 0)
            {
                /* Another Wait/Wake is already in progress */
                KeReleaseSpinLock(&DeviceExtension->WaitWakeSpinLock, OldIrql);
                Status = STATUS_DEVICE_BUSY;
                break;
            }

            /* Arm the device for wake */
            DeviceExtension->WaitWakeState = 2; /* armed */
            DeviceExtension->WaitWakeIrp = Irp;
            IoMarkIrpPending(Irp);

            KeReleaseSpinLock(&DeviceExtension->WaitWakeSpinLock, OldIrql);

            /* Increment parent FDO wait/wake child count */
            if (DeviceExtension->Fdo)
            {
                PFDO_DEVICE_EXTENSION FdoExt =
                    (PFDO_DEVICE_EXTENSION)DeviceExtension->Fdo->DeviceExtension;
                InterlockedIncrement(&FdoExt->WaitWakeChildCount);
            }

            DPRINT("PDO: Wait/Wake armed for Bus %lu Dev %lu Func %lu\n",
                   DeviceExtension->PciDevice->BusNumber,
                   DeviceExtension->PciDevice->SlotNumber.u.bits.DeviceNumber,
                   DeviceExtension->PciDevice->SlotNumber.u.bits.FunctionNumber);

            return STATUS_PENDING;
        }

        case IRP_MN_POWER_SEQUENCE:
            DPRINT("PDO IRP_MN_POWER_SEQUENCE\n");
            Status = STATUS_NOT_SUPPORTED;
            break;

        default:
            DPRINT("PDO Unknown power IRP minor 0x%x\n", IrpSp->MinorFunction);
            Status = STATUS_NOT_SUPPORTED;
            break;
    }

    /* PDO is the bottom of the stack - complete the IRP */
    Irp->IoStatus.Status = Status;
    PoStartNextPowerIrp(Irp);
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    DPRINT("Leaving. Status 0x%X\n", Status);
    return Status;
}

/* EOF */
