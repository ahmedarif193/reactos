/*
 * PROJECT:     ReactOS Kernel - Vista+ APIs
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Io functions of Vista+
 * COPYRIGHT:   2016 Pierre Schweitzer (pierre@reactos.org)
 *              2020 Victor Perevertkin (victor.perevertkin@reactos.org)
 */

#include <ntdef.h>
#include <ntifs.h>
#include <ntstrsafe.h>
#include <initguid.h>
#include <devpkey.h>

typedef struct _EX_WORKITEM_CONTEXT
{
    PIO_WORKITEM WorkItem;
    PIO_WORKITEM_ROUTINE_EX WorkItemRoutineEx;
    PVOID Context;
} EX_WORKITEM_CONTEXT, *PEX_WORKITEM_CONTEXT;

#define TAG_IOWI 'IWOI'
#define TAG_IOVP 'PVOI'

typedef struct _IOP_DEVPROP_LEGACY_MAP
{
    const DEVPROPKEY *PropertyKey;
    DEVICE_REGISTRY_PROPERTY DeviceProperty;
    DEVPROPTYPE Type;
    BOOLEAN ConvertGuidString;
} IOP_DEVPROP_LEGACY_MAP, *PIOP_DEVPROP_LEGACY_MAP;

static
BOOLEAN
NTAPI
IopIsEqualDevPropKey(
    _In_ CONST DEVPROPKEY *Key1,
    _In_ CONST DEVPROPKEY *Key2)
{
    return Key1 != NULL &&
           Key2 != NULL &&
           Key1->pid == Key2->pid &&
           RtlCompareMemory(&Key1->fmtid, &Key2->fmtid, sizeof(GUID)) == sizeof(GUID);
}

static
NTSTATUS
NTAPI
IopOpenRelativeKey(
    _In_ HANDLE ParentKey,
    _In_ PCWSTR Name,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ BOOLEAN Create,
    _Out_ PHANDLE KeyHandle)
{
    UNICODE_STRING KeyName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG Disposition;

    RtlInitUnicodeString(&KeyName, Name);
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               ParentKey,
                               NULL);

    if (Create)
    {
        return ZwCreateKey(KeyHandle,
                           DesiredAccess,
                           &ObjectAttributes,
                           0,
                           NULL,
                           REG_OPTION_NON_VOLATILE,
                           &Disposition);
    }

    return ZwOpenKey(KeyHandle, DesiredAccess, &ObjectAttributes);
}

static
NTSTATUS
NTAPI
IopOpenPropertyDataKeyFromParent(
    _In_ HANDLE ParentKey,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ LCID Lcid,
    _In_ BOOLEAN Create,
    _Out_ PHANDLE PropertyDataKey)
{
    HANDLE PropertiesKey = NULL;
    HANDLE GuidKey = NULL;
    UNICODE_STRING GuidString;
    WCHAR PropertyLeaf[32];
    NTSTATUS Status;

    *PropertyDataKey = NULL;

    Status = IopOpenRelativeKey(ParentKey,
                                L"Properties",
                                Create ? (KEY_READ | KEY_WRITE) : KEY_READ,
                                Create,
                                &PropertiesKey);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = RtlStringFromGUID(&PropertyKey->fmtid, &GuidString);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = IopOpenRelativeKey(PropertiesKey,
                                GuidString.Buffer,
                                Create ? (KEY_READ | KEY_WRITE) : KEY_READ,
                                Create,
                                &GuidKey);
    RtlFreeUnicodeString(&GuidString);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = RtlStringCchPrintfW(PropertyLeaf,
                                 RTL_NUMBER_OF(PropertyLeaf),
                                 L"%08lx.%08lx",
                                 PropertyKey->pid,
                                 Lcid);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = IopOpenRelativeKey(GuidKey,
                                PropertyLeaf,
                                Create ? (KEY_READ | KEY_WRITE) : KEY_READ,
                                Create,
                                PropertyDataKey);

Cleanup:
    if (GuidKey)
        ZwClose(GuidKey);
    if (PropertiesKey)
        ZwClose(PropertiesKey);

    return Status;
}

static
NTSTATUS
NTAPI
IopOpenDevicePropertyDataKey(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ LCID Lcid,
    _In_ BOOLEAN Create,
    _Out_ PHANDLE PropertyDataKey)
{
    HANDLE DeviceKey;
    NTSTATUS Status;

    *PropertyDataKey = NULL;

    Status = IoOpenDeviceRegistryKey(Pdo,
                                     PLUGPLAY_REGKEY_DEVICE,
                                     Create ? (KEY_READ | KEY_WRITE) : KEY_READ,
                                     &DeviceKey);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IopOpenPropertyDataKeyFromParent(DeviceKey,
                                              PropertyKey,
                                              Lcid,
                                              Create,
                                              PropertyDataKey);
    ZwClose(DeviceKey);
    return Status;
}

static
NTSTATUS
NTAPI
IopOpenInterfacePropertyDataKey(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ LCID Lcid,
    _In_ BOOLEAN Create,
    _Out_ PHANDLE PropertyDataKey)
{
    HANDLE InterfaceKey;
    NTSTATUS Status;

    *PropertyDataKey = NULL;

    Status = IoOpenDeviceInterfaceRegistryKey(SymbolicLinkName,
                                              Create ? (KEY_READ | KEY_WRITE) : KEY_READ,
                                              &InterfaceKey);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IopOpenPropertyDataKeyFromParent(InterfaceKey,
                                              PropertyKey,
                                              Lcid,
                                              Create,
                                              PropertyDataKey);
    ZwClose(InterfaceKey);
    return Status;
}

static
NTSTATUS
NTAPI
IopWritePropertyData(
    _In_ HANDLE PropertyDataKey,
    _In_ DEVPROPTYPE Type,
    _In_ ULONG Size,
    _In_reads_bytes_opt_(Size) PVOID Data)
{
    UNICODE_STRING ValueName;
    ULONG TypeValue;
    NTSTATUS Status;

    TypeValue = Type;
    RtlInitUnicodeString(&ValueName, L"Type");
    Status = ZwSetValueKey(PropertyDataKey,
                           &ValueName,
                           0,
                           REG_DWORD,
                           &TypeValue,
                           sizeof(TypeValue));
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&ValueName, L"Data");
    return ZwSetValueKey(PropertyDataKey,
                         &ValueName,
                         0,
                         REG_BINARY,
                         Data,
                         Size);
}

static
NTSTATUS
NTAPI
IopReadPropertyData(
    _In_ HANDLE PropertyDataKey,
    _In_ ULONG Size,
    _Out_writes_bytes_opt_(Size) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    UNICODE_STRING ValueName;
    ULONG ResultLength;
    NTSTATUS Status;
    UCHAR TypeBuffer[sizeof(KEY_VALUE_PARTIAL_INFORMATION) + sizeof(ULONG)];
    PKEY_VALUE_PARTIAL_INFORMATION TypeInfo;
    PKEY_VALUE_PARTIAL_INFORMATION DataInfo = NULL;

    TypeInfo = (PKEY_VALUE_PARTIAL_INFORMATION)TypeBuffer;

    RtlInitUnicodeString(&ValueName, L"Type");
    Status = ZwQueryValueKey(PropertyDataKey,
                             &ValueName,
                             KeyValuePartialInformation,
                             TypeInfo,
                             sizeof(TypeBuffer),
                             &ResultLength);
    if (!NT_SUCCESS(Status) ||
        TypeInfo->Type != REG_DWORD ||
        TypeInfo->DataLength != sizeof(ULONG))
    {
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    RtlInitUnicodeString(&ValueName, L"Data");
    ResultLength = 0;
    Status = ZwQueryValueKey(PropertyDataKey,
                             &ValueName,
                             KeyValuePartialInformation,
                             NULL,
                             0,
                             &ResultLength);
    if (Status != STATUS_BUFFER_TOO_SMALL &&
        Status != STATUS_BUFFER_OVERFLOW)
    {
        return Status;
    }

    DataInfo = (PKEY_VALUE_PARTIAL_INFORMATION)ExAllocatePoolWithTag(PagedPool,
                                                                     ResultLength,
                                                                     TAG_IOVP);
    if (!DataInfo)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = ZwQueryValueKey(PropertyDataKey,
                             &ValueName,
                             KeyValuePartialInformation,
                             DataInfo,
                             ResultLength,
                             &ResultLength);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    *RequiredSize = DataInfo->DataLength;
    *Type = *(PULONG)TypeInfo->Data;

    if (DataInfo->DataLength != 0 &&
        (Data == NULL || Size < DataInfo->DataLength))
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Cleanup;
    }

    if (DataInfo->DataLength != 0)
        RtlCopyMemory(Data, DataInfo->Data, DataInfo->DataLength);

Cleanup:
    ExFreePoolWithTag(DataInfo, TAG_IOVP);
    return Status;
}

static
NTSTATUS
NTAPI
IopCopyPropertyData(
    _In_ DEVPROPTYPE PropertyType,
    _In_reads_bytes_opt_(DataSize) PVOID PropertyData,
    _In_ ULONG DataSize,
    _In_ ULONG Size,
    _Out_writes_bytes_opt_(Size) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    *RequiredSize = DataSize;
    *Type = PropertyType;

    if (DataSize != 0 && (Data == NULL || Size < DataSize))
        return STATUS_BUFFER_TOO_SMALL;

    if (DataSize != 0)
        RtlCopyMemory(Data, PropertyData, DataSize);

    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
IopQueryDeviceRegistryValueData(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ PCWSTR ValueNameString,
    _In_ ULONG RegistryType,
    _In_ DEVPROPTYPE PropertyType,
    _In_ BOOLEAN ConvertGuidString,
    _In_ ULONG Size,
    _Out_writes_bytes_opt_(Size) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    HANDLE DeviceKey = NULL;
    UNICODE_STRING ValueName;
    ULONG ResultLength = 0;
    PKEY_VALUE_PARTIAL_INFORMATION ValueInfo = NULL;
    NTSTATUS Status;

    Status = IoOpenDeviceRegistryKey(Pdo,
                                     PLUGPLAY_REGKEY_DEVICE,
                                     KEY_READ,
                                     &DeviceKey);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&ValueName, ValueNameString);
    Status = ZwQueryValueKey(DeviceKey,
                             &ValueName,
                             KeyValuePartialInformation,
                             NULL,
                             0,
                             &ResultLength);
    if (Status != STATUS_BUFFER_TOO_SMALL &&
        Status != STATUS_BUFFER_OVERFLOW)
    {
        ZwClose(DeviceKey);
        return Status;
    }

    ValueInfo = ExAllocatePoolWithTag(PagedPool, ResultLength, TAG_IOVP);
    if (!ValueInfo)
    {
        ZwClose(DeviceKey);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = ZwQueryValueKey(DeviceKey,
                             &ValueName,
                             KeyValuePartialInformation,
                             ValueInfo,
                             ResultLength,
                             &ResultLength);
    ZwClose(DeviceKey);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (ValueInfo->Type != RegistryType)
    {
        Status = STATUS_OBJECT_TYPE_MISMATCH;
        goto Cleanup;
    }

    if (ConvertGuidString)
    {
        UNICODE_STRING GuidString;
        GUID GuidValue;

        if (ValueInfo->DataLength < sizeof(WCHAR))
        {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
            goto Cleanup;
        }

        RtlInitUnicodeString(&GuidString, (PCWSTR)ValueInfo->Data);
        Status = RtlGUIDFromString(&GuidString, &GuidValue);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        Status = IopCopyPropertyData(DEVPROP_TYPE_GUID,
                                     &GuidValue,
                                     sizeof(GuidValue),
                                     Size,
                                     Data,
                                     RequiredSize,
                                     Type);
        goto Cleanup;
    }

    Status = IopCopyPropertyData(PropertyType,
                                 ValueInfo->Data,
                                 ValueInfo->DataLength,
                                 Size,
                                 Data,
                                 RequiredSize,
                                 Type);

Cleanup:
    ExFreePoolWithTag(ValueInfo, TAG_IOVP);
    return Status;
}

static
const IOP_DEVPROP_LEGACY_MAP *
NTAPI
IopFindMappedDeviceProperty(
    _In_ CONST DEVPROPKEY *PropertyKey)
{
    static const IOP_DEVPROP_LEGACY_MAP LegacyMappings[] =
    {
        { &DEVPKEY_Device_DeviceDesc,      DevicePropertyDeviceDescription,      DEVPROP_TYPE_STRING,      FALSE },
        { &DEVPKEY_Device_HardwareIds,     DevicePropertyHardwareID,             DEVPROP_TYPE_STRING_LIST, FALSE },
        { &DEVPKEY_Device_CompatibleIds,   DevicePropertyCompatibleIDs,          DEVPROP_TYPE_STRING_LIST, FALSE },
        { &DEVPKEY_Device_Class,           DevicePropertyClassName,              DEVPROP_TYPE_STRING,      FALSE },
        { &DEVPKEY_Device_ClassGuid,       DevicePropertyClassGuid,              DEVPROP_TYPE_GUID,        TRUE  },
        { &DEVPKEY_Device_Driver,          DevicePropertyDriverKeyName,          DEVPROP_TYPE_STRING,      FALSE },
        { &DEVPKEY_Device_Manufacturer,    DevicePropertyManufacturer,           DEVPROP_TYPE_STRING,      FALSE },
        { &DEVPKEY_Device_FriendlyName,    DevicePropertyFriendlyName,           DEVPROP_TYPE_STRING,      FALSE },
        { &DEVPKEY_Device_LocationInfo,    DevicePropertyLocationInformation,    DEVPROP_TYPE_STRING,      FALSE },
        { &DEVPKEY_Device_PDOName,         DevicePropertyPhysicalDeviceObjectName, DEVPROP_TYPE_STRING,    FALSE },
        { &DEVPKEY_Device_BusTypeGuid,     DevicePropertyBusTypeGuid,            DEVPROP_TYPE_GUID,        FALSE },
        { &DEVPKEY_Device_LegacyBusType,   DevicePropertyLegacyBusType,          DEVPROP_TYPE_UINT32,      FALSE },
        { &DEVPKEY_Device_BusNumber,       DevicePropertyBusNumber,              DEVPROP_TYPE_UINT32,      FALSE },
        { &DEVPKEY_Device_EnumeratorName,  DevicePropertyEnumeratorName,         DEVPROP_TYPE_STRING,      FALSE },
        { &DEVPKEY_Device_Address,         DevicePropertyAddress,                DEVPROP_TYPE_UINT32,      FALSE },
        { &DEVPKEY_Device_UINumber,        DevicePropertyUINumber,               DEVPROP_TYPE_UINT32,      FALSE },
        { &DEVPKEY_Device_InstallState,    DevicePropertyInstallState,           DEVPROP_TYPE_UINT32,      FALSE },
        { &DEVPKEY_Device_RemovalPolicy,   DevicePropertyRemovalPolicy,          DEVPROP_TYPE_UINT32,      FALSE },
    };
    ULONG i;

    for (i = 0; i < RTL_NUMBER_OF(LegacyMappings); ++i)
    {
        if (IopIsEqualDevPropKey(PropertyKey, LegacyMappings[i].PropertyKey))
            return &LegacyMappings[i];
    }

    return NULL;
}

static
NTSTATUS
NTAPI
IopQueryMappedDevicePropertyData(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ ULONG Size,
    _Out_writes_bytes_opt_(Size) PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    const IOP_DEVPROP_LEGACY_MAP *Mapping;
    NTSTATUS Status;

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_InLocalMachineContainer))
    {
        DEVPROP_BOOLEAN Value = DEVPROP_TRUE;
        return IopCopyPropertyData(DEVPROP_TYPE_BOOLEAN,
                                   &Value,
                                   sizeof(Value),
                                   Size,
                                   Data,
                                   RequiredSize,
                                   Type);
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_SessionId))
    {
        ULONG Value = MAXULONG;
        return IopCopyPropertyData(DEVPROP_TYPE_UINT32,
                                   &Value,
                                   sizeof(Value),
                                   Size,
                                   Data,
                                   RequiredSize,
                                   Type);
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_Service))
    {
        return IopQueryDeviceRegistryValueData(Pdo,
                                               L"Service",
                                               REG_SZ,
                                               DEVPROP_TYPE_STRING,
                                               FALSE,
                                               Size,
                                               Data,
                                               RequiredSize,
                                               Type);
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_ConfigFlags))
    {
        return IopQueryDeviceRegistryValueData(Pdo,
                                               L"ConfigFlags",
                                               REG_DWORD,
                                               DEVPROP_TYPE_UINT32,
                                               FALSE,
                                               Size,
                                               Data,
                                               RequiredSize,
                                               Type);
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_Capabilities))
    {
        return IopQueryDeviceRegistryValueData(Pdo,
                                               L"Capabilities",
                                               REG_DWORD,
                                               DEVPROP_TYPE_UINT32,
                                               FALSE,
                                               Size,
                                               Data,
                                               RequiredSize,
                                               Type);
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_UpperFilters))
    {
        return IopQueryDeviceRegistryValueData(Pdo,
                                               L"UpperFilters",
                                               REG_MULTI_SZ,
                                               DEVPROP_TYPE_STRING_LIST,
                                               FALSE,
                                               Size,
                                               Data,
                                               RequiredSize,
                                               Type);
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_LowerFilters))
    {
        return IopQueryDeviceRegistryValueData(Pdo,
                                               L"LowerFilters",
                                               REG_MULTI_SZ,
                                               DEVPROP_TYPE_STRING_LIST,
                                               FALSE,
                                               Size,
                                               Data,
                                               RequiredSize,
                                               Type);
    }

    if (IopIsEqualDevPropKey(PropertyKey, &DEVPKEY_Device_ContainerId))
    {
        return IopQueryDeviceRegistryValueData(Pdo,
                                               L"ContainerID",
                                               REG_SZ,
                                               DEVPROP_TYPE_GUID,
                                               TRUE,
                                               Size,
                                               Data,
                                               RequiredSize,
                                               Type);
    }

    Mapping = IopFindMappedDeviceProperty(PropertyKey);
    if (!Mapping)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    if (Mapping->ConvertGuidString)
    {
        PWSTR GuidBuffer;
        ULONG GuidBufferSize = 0;
        UNICODE_STRING GuidString;
        GUID GuidValue;

        Status = IoGetDeviceProperty(Pdo,
                                     Mapping->DeviceProperty,
                                     0,
                                     NULL,
                                     &GuidBufferSize);
        if (Status != STATUS_BUFFER_TOO_SMALL)
            return Status;

        GuidBuffer = (PWSTR)ExAllocatePoolWithTag(PagedPool, GuidBufferSize, TAG_IOVP);
        if (!GuidBuffer)
            return STATUS_INSUFFICIENT_RESOURCES;

        Status = IoGetDeviceProperty(Pdo,
                                     Mapping->DeviceProperty,
                                     GuidBufferSize,
                                     GuidBuffer,
                                     &GuidBufferSize);
        if (NT_SUCCESS(Status))
        {
            RtlInitUnicodeString(&GuidString, GuidBuffer);
            Status = RtlGUIDFromString(&GuidString, &GuidValue);
            if (NT_SUCCESS(Status))
            {
                Status = IopCopyPropertyData(DEVPROP_TYPE_GUID,
                                             &GuidValue,
                                             sizeof(GuidValue),
                                             Size,
                                             Data,
                                             RequiredSize,
                                             Type);
            }
        }

        ExFreePoolWithTag(GuidBuffer, TAG_IOVP);
        return Status;
    }

    Status = IoGetDeviceProperty(Pdo,
                                 Mapping->DeviceProperty,
                                 0,
                                 NULL,
                                 RequiredSize);
    if (Status != STATUS_BUFFER_TOO_SMALL && !NT_SUCCESS(Status))
        return Status;

    *Type = Mapping->Type;
    if (*RequiredSize != 0 &&
        (Data == NULL || Size < *RequiredSize))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    return IoGetDeviceProperty(Pdo,
                               Mapping->DeviceProperty,
                               Size,
                               Data,
                               RequiredSize);
}

NTKRNLVISTAAPI
NTSTATUS
NTAPI
IoGetIrpExtraCreateParameter(IN PIRP Irp,
                             OUT PECP_LIST *ExtraCreateParameter)
{
    /* Check we have a create operation */
    if (!BooleanFlagOn(Irp->Flags, IRP_CREATE_OPERATION))
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* If so, return user buffer */
    *ExtraCreateParameter = Irp->UserBuffer;
    return STATUS_SUCCESS;
}

_Function_class_(IO_WORKITEM_ROUTINE)
static
VOID
NTAPI
IopWorkItemExCallback(
    PDEVICE_OBJECT DeviceObject,
    PVOID Ctx)
{
    PEX_WORKITEM_CONTEXT context = Ctx;

    context->WorkItemRoutineEx(DeviceObject, context->Context, context->WorkItem);
    ExFreePoolWithTag(context, TAG_IOWI);
}

NTKRNLVISTAAPI
VOID
NTAPI
IoQueueWorkItemEx(
    _Inout_ PIO_WORKITEM IoWorkItem,
    _In_ PIO_WORKITEM_ROUTINE_EX WorkerRoutine,
    _In_ WORK_QUEUE_TYPE QueueType,
    _In_opt_ __drv_aliasesMem PVOID Context)
{
    PEX_WORKITEM_CONTEXT newContext = ExAllocatePoolWithTag(NonPagedPoolMustSucceed, sizeof(*newContext), TAG_IOWI);
    newContext->WorkItem = IoWorkItem;
    newContext->WorkItemRoutineEx = WorkerRoutine;
    newContext->Context = Context;

    IoQueueWorkItem(IoWorkItem, IopWorkItemExCallback, QueueType, newContext);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
NTKRNLVISTAAPI
NTSTATUS
NTAPI
IoSetDevicePropertyData(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ LCID Lcid,
    _In_ ULONG Flags,
    _In_ DEVPROPTYPE Type,
    _In_ ULONG Size,
    _In_opt_ PVOID Data)
{
    HANDLE PropertyDataKey;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Flags);

    if (!Pdo || !PropertyKey || (Size != 0 && Data == NULL))
        return STATUS_INVALID_PARAMETER;

    Status = IopOpenDevicePropertyDataKey(Pdo,
                                          PropertyKey,
                                          Lcid,
                                          TRUE,
                                          &PropertyDataKey);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IopWritePropertyData(PropertyDataKey, Type, Size, Data);
    ZwClose(PropertyDataKey);
    return Status;
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
NTKRNLVISTAAPI
NTSTATUS
NTAPI
IoGetDevicePropertyData(
    _In_ PDEVICE_OBJECT Pdo,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ LCID Lcid,
    _Reserved_ ULONG Flags,
    _In_ ULONG Size,
    _Out_ PVOID Data,
    _Out_ PULONG RequiredSize,
    _Out_ PDEVPROPTYPE Type)
{
    HANDLE PropertyDataKey;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Flags);

    if (!Pdo || !PropertyKey || !RequiredSize || !Type)
        return STATUS_INVALID_PARAMETER;

    *RequiredSize = 0;
    *Type = DEVPROP_TYPE_EMPTY;

    Status = IopOpenDevicePropertyDataKey(Pdo,
                                          PropertyKey,
                                          Lcid,
                                          FALSE,
                                          &PropertyDataKey);
    if (NT_SUCCESS(Status))
    {
        Status = IopReadPropertyData(PropertyDataKey,
                                     Size,
                                     Data,
                                     RequiredSize,
                                     Type);
        ZwClose(PropertyDataKey);
        if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_TOO_SMALL)
            return Status;
    }

    return IopQueryMappedDevicePropertyData(Pdo,
                                            PropertyKey,
                                            Size,
                                            Data,
                                            RequiredSize,
                                            Type);
}

_IRQL_requires_max_(PASSIVE_LEVEL)
_Must_inspect_result_
NTKRNLVISTAAPI
NTSTATUS
IoSetDeviceInterfacePropertyData(
    _In_ PUNICODE_STRING SymbolicLinkName,
    _In_ CONST DEVPROPKEY *PropertyKey,
    _In_ LCID Lcid,
    _In_ ULONG Flags,
    _In_ DEVPROPTYPE Type,
    _In_ ULONG Size,
    _In_reads_bytes_opt_(Size) PVOID Data)
{
    HANDLE PropertyDataKey;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(Flags);

    if (!SymbolicLinkName || !PropertyKey || (Size != 0 && Data == NULL))
        return STATUS_INVALID_PARAMETER;

    Status = IopOpenInterfacePropertyDataKey(SymbolicLinkName,
                                             PropertyKey,
                                             Lcid,
                                             TRUE,
                                             &PropertyDataKey);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = IopWritePropertyData(PropertyDataKey, Type, Size, Data);
    ZwClose(PropertyDataKey);
    return Status;
}

NTKRNLVISTAAPI
IO_PRIORITY_HINT
NTAPI
IoGetIoPriorityHint(
    _In_ PIRP Irp)
{
    return IoPriorityNormal;
}

NTKRNLVISTAAPI
VOID
IoSetMasterIrpStatus(
    _Inout_ PIRP MasterIrp,
    _In_ NTSTATUS Status)
{
    NTSTATUS MasterStatus = MasterIrp->IoStatus.Status;

    if (Status == STATUS_FT_READ_FROM_COPY)
    {
        return;
    }

    if ((Status == STATUS_VERIFY_REQUIRED) ||
        (MasterStatus == STATUS_SUCCESS && !NT_SUCCESS(Status)) ||
        (!NT_SUCCESS(MasterStatus) && !NT_SUCCESS(Status) && Status > MasterStatus))
    {
        MasterIrp->IoStatus.Status = Status;
    }
}
