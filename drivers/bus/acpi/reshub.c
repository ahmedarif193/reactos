/*
 * PROJECT:     ReactOS ACPI bus driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ACPI Resource Hub and Simple Peripheral Bus routing
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "precomp.h"

#include <initguid.h>
#include <reshub.h>
#include <reactos/drivers/spb/spb.h>

ACPI_STATUS
AcpiRsCreateAmlResources(
    _In_ ACPI_BUFFER *ResourceList,
    _Inout_ ACPI_BUFFER *OutputBuffer);

#define NDEBUG
#include <debug.h>

#define RH_CONTEXT_TAG 'cHRA'
#define RH_RESOURCE_TAG 'rHRA'
#define RH_LIST_TAG 'lHRA'
#define RH_CONTEXT_SIGNATURE 'fHRA'

typedef struct _ACPI_RESOURCE_HUB_FILE_CONTEXT
{
    ULONG Signature;
    LARGE_INTEGER ConnectionId;
    PDEVICE_OBJECT OwnerPdo;
    ACPI_HANDLE OwnerHandle;
    ACPI_BUFFER ResourceBuffer;
    ACPI_RESOURCE *Resource;
    PFILE_OBJECT ControllerFileObject;
    PDEVICE_OBJECT ControllerDeviceObject;
    BOOLEAN ControllerLocked;
    /* Serializes controller binding and transfer forwarding. Deliberately
       not a fast mutex: holding one raises to APC_LEVEL, and the SPB
       controller dispatch below us enforces PASSIVE_LEVEL, so a fast
       mutex here made every forwarded IOCTL fail with
       STATUS_INVALID_DEVICE_STATE. A signaled synchronization event is a
       passive-level mutex without the IRQL side effect. */
    KEVENT ControllerLock;
} ACPI_RESOURCE_HUB_FILE_CONTEXT, *PACPI_RESOURCE_HUB_FILE_CONTEXT;

static PDEVICE_OBJECT AcpiResourceHubDeviceObject;
static PFDO_DEVICE_DATA AcpiResourceHubFdoData;
static FAST_MUTEX AcpiResourceHubMutex;

static
NTSTATUS
RhCompleteIrp(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
BOOLEAN
RhIsConnectionResource(
    _In_ ACPI_RESOURCE *Resource)
{
    ACPI_RESOURCE_COMMON_SERIALBUS *SerialBus;

    if (Resource->Type == ACPI_RESOURCE_TYPE_SERIAL_BUS)
    {
        SerialBus = &Resource->Data.CommonSerialBus;
        if (SerialBus->ProducerConsumer != ACPI_CONSUMER)
            return FALSE;

        return (SerialBus->Type == ACPI_RESOURCE_SERIAL_TYPE_I2C ||
                SerialBus->Type == ACPI_RESOURCE_SERIAL_TYPE_SPI ||
                SerialBus->Type == ACPI_RESOURCE_SERIAL_TYPE_UART);
    }

    if (Resource->Type == ACPI_RESOURCE_TYPE_GPIO)
    {
        ACPI_RESOURCE_GPIO *Gpio = &Resource->Data.Gpio;

        return (Gpio->ProducerConsumer == ACPI_CONSUMER &&
                Gpio->ConnectionType == ACPI_RESOURCE_GPIO_TYPE_IO);
    }

    return FALSE;
}

static
NTSTATUS
RhParseConnectionId(
    _In_ PUNICODE_STRING FileName,
    _Out_ PLARGE_INTEGER ConnectionId)
{
    ULONG HighPart = 0, LowPart = 0, Digit;
    USHORT Index = 0, StartIndex, EndIndex;
    WCHAR Character;

    if (FileName->Length == 0)
    {
        ConnectionId->QuadPart = 0;
        return STATUS_SUCCESS;
    }

    if (FileName->Buffer[0] == OBJ_NAME_PATH_SEPARATOR)
        Index = 1;

    if (FileName->Length != (Index + 16) * sizeof(WCHAR))
        return STATUS_OBJECT_NAME_INVALID;

    StartIndex = Index;
    EndIndex = StartIndex + 16;
    for (; Index < EndIndex; Index++)
    {
        Character = FileName->Buffer[Index];
        if (Character >= L'0' && Character <= L'9')
            Digit = Character - L'0';
        else if (Character >= L'a' && Character <= L'f')
            Digit = Character - L'a' + 10;
        else if (Character >= L'A' && Character <= L'F')
            Digit = Character - L'A' + 10;
        else
            return STATUS_OBJECT_NAME_INVALID;

        if ((Index - StartIndex) < 8)
            HighPart = (HighPart << 4) | Digit;
        else
            LowPart = (LowPart << 4) | Digit;
    }

    if (HighPart == 0 || LowPart == 0)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    ConnectionId->HighPart = HighPart;
    ConnectionId->LowPart = LowPart;
    return STATUS_SUCCESS;
}

static
NTSTATUS
RhGetConnectionResource(
    _In_ LARGE_INTEGER ConnectionId,
    _Out_ PDEVICE_OBJECT *OwnerPdo,
    _Out_ ACPI_HANDLE *OwnerHandle,
    _Out_ ACPI_BUFFER *ResourceBuffer,
    _Out_ ACPI_RESOURCE **ConnectionResource)
{
    PFDO_DEVICE_DATA FdoData;
    PPDO_DEVICE_DATA PdoData = NULL;
    PDEVICE_OBJECT FdoObject = NULL;
    PLIST_ENTRY Entry;
    ACPI_RESOURCE *Resource;
    ACPI_STATUS AcpiStatus;
    ULONG ResourceIndex;
    NTSTATUS Status = STATUS_OBJECT_NAME_NOT_FOUND;

    *OwnerPdo = NULL;
    *OwnerHandle = NULL;
    ResourceBuffer->Length = 0;
    ResourceBuffer->Pointer = NULL;
    *ConnectionResource = NULL;

    if (ConnectionId.HighPart == 0 || ConnectionId.LowPart == 0)
        return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&AcpiResourceHubMutex);
    FdoData = AcpiResourceHubFdoData;
    if (FdoData != NULL)
    {
        FdoObject = FdoData->Common.Self;
        ObReferenceObject(FdoObject);
    }
    ExReleaseFastMutex(&AcpiResourceHubMutex);

    if (FdoData == NULL)
        return STATUS_DEVICE_NOT_READY;

    ExAcquireFastMutex(&FdoData->Mutex);
    for (Entry = FdoData->ListOfPDOs.Flink;
         Entry != &FdoData->ListOfPDOs;
         Entry = Entry->Flink)
    {
        PdoData = CONTAINING_RECORD(Entry, PDO_DEVICE_DATA, Link);
        if (PdoData->ResourceHubId == (ULONG)ConnectionId.HighPart)
        {
            ObReferenceObject(PdoData->Common.Self);
            break;
        }
        PdoData = NULL;
    }
    ExReleaseFastMutex(&FdoData->Mutex);
    ObDereferenceObject(FdoObject);

    if (PdoData == NULL)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    if (PdoData->AcpiHandle == NULL)
    {
        ObDereferenceObject(PdoData->Common.Self);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }

    ResourceBuffer->Length = 0;
    AcpiStatus = AcpiGetCurrentResources(PdoData->AcpiHandle, ResourceBuffer);
    if ((ACPI_FAILURE(AcpiStatus) && AcpiStatus != AE_BUFFER_OVERFLOW) ||
        ResourceBuffer->Length == 0)
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Failure;
    }

    ResourceBuffer->Pointer = ExAllocatePoolWithTag(PagedPool,
                                                     ResourceBuffer->Length,
                                                     RH_RESOURCE_TAG);
    if (ResourceBuffer->Pointer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }

    AcpiStatus = AcpiGetCurrentResources(PdoData->AcpiHandle, ResourceBuffer);
    if (ACPI_FAILURE(AcpiStatus))
    {
        Status = STATUS_DEVICE_CONFIGURATION_ERROR;
        goto Failure;
    }

    Resource = ResourceBuffer->Pointer;
    ResourceIndex = 1;
    while (Resource->Type != ACPI_RESOURCE_TYPE_END_TAG)
    {
        if (ResourceIndex == ConnectionId.LowPart)
        {
            if (!RhIsConnectionResource(Resource))
                goto Failure;

            *OwnerPdo = PdoData->Common.Self;
            *OwnerHandle = PdoData->AcpiHandle;
            *ConnectionResource = Resource;
            return STATUS_SUCCESS;
        }

        if (Resource->Length < ACPI_RS_SIZE_NO_DATA)
        {
            Status = STATUS_ACPI_INVALID_DATA;
            goto Failure;
        }

        Resource = ACPI_NEXT_RESOURCE(Resource);
        ResourceIndex++;
    }

Failure:
    if (ResourceBuffer->Pointer != NULL)
    {
        ExFreePoolWithTag(ResourceBuffer->Pointer, RH_RESOURCE_TAG);
        ResourceBuffer->Pointer = NULL;
        ResourceBuffer->Length = 0;
    }
    ObDereferenceObject(PdoData->Common.Self);
    return Status;
}

static
VOID
RhReleaseConnectionResource(
    _In_opt_ PDEVICE_OBJECT OwnerPdo,
    _Inout_ ACPI_BUFFER *ResourceBuffer)
{
    if (ResourceBuffer->Pointer != NULL)
    {
        ExFreePoolWithTag(ResourceBuffer->Pointer, RH_RESOURCE_TAG);
        ResourceBuffer->Pointer = NULL;
        ResourceBuffer->Length = 0;
    }

    if (OwnerPdo != NULL)
        ObDereferenceObject(OwnerPdo);
}

static
NTSTATUS
RhSerializeConnectionProperties(
    _In_ ACPI_RESOURCE *Resource,
    _Out_ PUCHAR *Properties,
    _Out_ PULONG PropertiesLength)
{
    ACPI_BUFFER ResourceList, AmlBuffer;
    ACPI_RESOURCE *EndTag;
    PUCHAR Buffer;
    ULONG ResourceListLength;
    ACPI_STATUS AcpiStatus;

    *Properties = NULL;
    *PropertiesLength = 0;

    ResourceListLength = Resource->Length + ACPI_RS_SIZE(ACPI_RESOURCE_END_TAG);
    Buffer = ExAllocatePoolWithTag(PagedPool, ResourceListLength, RH_LIST_TAG);
    if (Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Buffer, ResourceListLength);
    RtlCopyMemory(Buffer, Resource, Resource->Length);
    EndTag = (ACPI_RESOURCE *)(Buffer + Resource->Length);
    EndTag->Type = ACPI_RESOURCE_TYPE_END_TAG;
    EndTag->Length = ACPI_RS_SIZE(ACPI_RESOURCE_END_TAG);

    ResourceList.Length = ResourceListLength;
    ResourceList.Pointer = Buffer;
    AmlBuffer.Length = ACPI_ALLOCATE_LOCAL_BUFFER;
    AmlBuffer.Pointer = NULL;
    AcpiStatus = AcpiRsCreateAmlResources(&ResourceList, &AmlBuffer);
    ExFreePoolWithTag(Buffer, RH_LIST_TAG);
    if (ACPI_FAILURE(AcpiStatus))
        return STATUS_ACPI_INVALID_DATA;

    if (AmlBuffer.Length < 2 ||
        ((((PUCHAR)AmlBuffer.Pointer)[AmlBuffer.Length - 2] & 0xF8) != 0x78))
    {
        AcpiOsFree(AmlBuffer.Pointer);
        return STATUS_ACPI_INVALID_DATA;
    }

    *Properties = AmlBuffer.Pointer;
    *PropertiesLength = (ULONG)AmlBuffer.Length - 2;
    return STATUS_SUCCESS;
}

static
NTSTATUS
RhSendInternalIoctl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _In_reads_bytes_opt_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength,
    _Out_writes_bytes_opt_(OutputLength) PVOID OutputBuffer,
    _In_ ULONG OutputLength,
    _Out_opt_ PULONG_PTR Information)
{
    IO_STATUS_BLOCK IoStatusBlock;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode,
                                        DeviceObject,
                                        InputBuffer,
                                        InputLength,
                                        OutputBuffer,
                                        OutputLength,
                                        TRUE,
                                        &Event,
                                        &IoStatusBlock);
    if (Irp == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    if (Information != NULL)
        *Information = IoStatusBlock.Information;

    return Status;
}

static
NTSTATUS
RhBindSpbController(
    _Inout_ PACPI_RESOURCE_HUB_FILE_CONTEXT Context)
{
    ACPI_RESOURCE_COMMON_SERIALBUS *SerialBus;
    PWSTR InterfaceList = NULL, InterfaceName;
    UNICODE_STRING SymbolicLink;
    PFILE_OBJECT FileObject;
    PDEVICE_OBJECT DeviceObject;
    REACTOS_SPB_CONTROLLER_INFO ControllerInfo;
    ACPI_HANDLE ControllerHandle, CandidateHandle;
    ACPI_STATUS AcpiStatus;
    NTSTATUS Status, LastStatus = STATUS_DEVICE_NOT_READY;

    if (Context->ControllerDeviceObject != NULL)
        return STATUS_SUCCESS;

    if (Context->Resource->Type != ACPI_RESOURCE_TYPE_SERIAL_BUS)
        return STATUS_NOT_SUPPORTED;

    SerialBus = &Context->Resource->Data.CommonSerialBus;
    if (SerialBus->ResourceSource.StringPtr == NULL ||
        SerialBus->ResourceSource.StringLength == 0)
    {
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    AcpiStatus = AcpiGetHandle(Context->OwnerHandle,
                               SerialBus->ResourceSource.StringPtr,
                               &ControllerHandle);
    if (ACPI_FAILURE(AcpiStatus))
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    Status = IoGetDeviceInterfaces(&GUID_REACTOS_SPB_CONTROLLER,
                                   NULL,
                                   0,
                                   &InterfaceList);
    if (!NT_SUCCESS(Status))
        return Status;

    for (InterfaceName = InterfaceList;
         *InterfaceName != UNICODE_NULL;
         InterfaceName += wcslen(InterfaceName) + 1)
    {
        RtlInitUnicodeString(&SymbolicLink, InterfaceName);
        Status = IoGetDeviceObjectPointer(&SymbolicLink,
                                          FILE_READ_DATA | FILE_WRITE_DATA,
                                          &FileObject,
                                          &DeviceObject);
        if (!NT_SUCCESS(Status))
        {
            LastStatus = Status;
            continue;
        }

        RtlZeroMemory(&ControllerInfo, sizeof(ControllerInfo));
        Status = RhSendInternalIoctl(DeviceObject,
                                     IOCTL_INTERNAL_SPB_QUERY_CONTROLLER,
                                     NULL,
                                     0,
                                     &ControllerInfo,
                                     sizeof(ControllerInfo),
                                     NULL);
        if (NT_SUCCESS(Status) &&
            ControllerInfo.Version == REACTOS_SPB_INTERFACE_VERSION &&
            AcpiFindPciDeviceInNamespace(ControllerInfo.Segment,
                                         ControllerInfo.BusNumber,
                                         (ControllerInfo.Address >> 16) & 0xFFFF,
                                         ControllerInfo.Address & 0xFFFF,
                                         &CandidateHandle) &&
            CandidateHandle == ControllerHandle)
        {
            Context->ControllerFileObject = FileObject;
            Context->ControllerDeviceObject = DeviceObject;
            LastStatus = STATUS_SUCCESS;
            break;
        }

        ObDereferenceObject(FileObject);
        LastStatus = NT_SUCCESS(Status) ? STATUS_NOT_FOUND : Status;
    }

    ExFreePool(InterfaceList);
    return LastStatus;
}

static
NTSTATUS
RhNormalizeTransferList(
    _In_ PIRP Irp,
    _In_ PSPB_TRANSFER_LIST TransferList,
    _In_ ULONG InputLength,
    _In_ PACPI_RESOURCE_HUB_FILE_CONTEXT Context,
    _Out_ PREACTOS_SPB_TRANSFER_LIST NormalizedList)
{
    ULONG RequiredLength, Index;
    PSPB_TRANSFER_LIST_ENTRY Entry;
    PVOID Buffer;

    if (Irp->RequestorMode != KernelMode)
        return STATUS_ACCESS_DENIED;

    if (InputLength < FIELD_OFFSET(SPB_TRANSFER_LIST, Transfers) ||
        TransferList->Size < sizeof(SPB_TRANSFER_LIST) ||
        TransferList->TransferCount == 0 ||
        TransferList->TransferCount > REACTOS_SPB_MAX_TRANSFERS)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RequiredLength = FIELD_OFFSET(SPB_TRANSFER_LIST, Transfers) +
                     TransferList->TransferCount * sizeof(SPB_TRANSFER_LIST_ENTRY);
    if (InputLength < RequiredLength)
        return STATUS_BUFFER_TOO_SMALL;

    if (Context->Resource->Type != ACPI_RESOURCE_TYPE_SERIAL_BUS ||
        Context->Resource->Data.CommonSerialBus.Type != ACPI_RESOURCE_SERIAL_TYPE_I2C)
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlZeroMemory(NormalizedList, sizeof(*NormalizedList));
    NormalizedList->Version = REACTOS_SPB_INTERFACE_VERSION;
    NormalizedList->SlaveAddress = Context->Resource->Data.I2cSerialBus.SlaveAddress;
    NormalizedList->TypeSpecificFlags = Context->Resource->Data.I2cSerialBus.AccessMode;
    NormalizedList->ConnectionSpeed = Context->Resource->Data.I2cSerialBus.ConnectionSpeed;
    NormalizedList->TransferCount = TransferList->TransferCount;

    for (Index = 0; Index < TransferList->TransferCount; Index++)
    {
        Entry = &TransferList->Transfers[Index];
        if (Entry->Direction != SpbTransferDirectionFromDevice &&
            Entry->Direction != SpbTransferDirectionToDevice)
        {
            return STATUS_INVALID_PARAMETER;
        }

        switch (Entry->Buffer.Format)
        {
            case SpbTransferBufferFormatSimple:
            case SpbTransferBufferFormatSimpleNonPaged:
                Buffer = Entry->Buffer.Simple.Buffer;
                NormalizedList->Transfers[Index].Length = Entry->Buffer.Simple.BufferCb;
                break;

            case SpbTransferBufferFormatMdl:
                if (Entry->Buffer.Mdl == NULL)
                    return STATUS_INVALID_PARAMETER;
                Buffer = MmGetSystemAddressForMdlSafe(Entry->Buffer.Mdl,
                                                      NormalPagePriority);
                NormalizedList->Transfers[Index].Length =
                    MmGetMdlByteCount(Entry->Buffer.Mdl);
                break;

            case SpbTransferBufferFormatList:
                if (Entry->Buffer.BufferList.List == NULL ||
                    Entry->Buffer.BufferList.ListCe != 1)
                {
                    return STATUS_NOT_SUPPORTED;
                }
                Buffer = Entry->Buffer.BufferList.List[0].Buffer;
                NormalizedList->Transfers[Index].Length =
                    Entry->Buffer.BufferList.List[0].BufferCb;
                break;

            default:
                return STATUS_INVALID_PARAMETER;
        }

        if (Buffer == NULL || NormalizedList->Transfers[Index].Length == 0)
            return STATUS_INVALID_PARAMETER;

        NormalizedList->Transfers[Index].Direction = Entry->Direction;
        NormalizedList->Transfers[Index].DelayInUs = Entry->DelayInUs;
        NormalizedList->Transfers[Index].Buffer = Buffer;
    }

    return STATUS_SUCCESS;
}

static
VOID
RhAcquireControllerLock(
    _Inout_ PACPI_RESOURCE_HUB_FILE_CONTEXT Context)
{
    KeWaitForSingleObject(&Context->ControllerLock, Executive, KernelMode, FALSE, NULL);
}

static
VOID
RhReleaseControllerLock(
    _Inout_ PACPI_RESOURCE_HUB_FILE_CONTEXT Context)
{
    KeSetEvent(&Context->ControllerLock, IO_NO_INCREMENT, FALSE);
}

static
NTSTATUS
RhExecuteTransferList(
    _Inout_ PACPI_RESOURCE_HUB_FILE_CONTEXT Context,
    _In_ PREACTOS_SPB_TRANSFER_LIST TransferList,
    _Out_opt_ PULONG_PTR Information)
{
    NTSTATUS Status;

    RhAcquireControllerLock(Context);
    Status = RhBindSpbController(Context);
    if (NT_SUCCESS(Status))
    {
        if (Context->ControllerLocked)
            TransferList->Flags |= REACTOS_SPB_TRANSFER_FLAG_CONTROLLER_LOCKED;

        Status = RhSendInternalIoctl(Context->ControllerDeviceObject,
                                     IOCTL_INTERNAL_SPB_EXECUTE_TRANSFER_LIST,
                                     TransferList,
                                     sizeof(*TransferList),
                                     NULL,
                                     0,
                                     Information);
    }
    RhReleaseControllerLock(Context);
    return Status;
}

static
NTSTATUS
RhSetControllerLock(
    _Inout_ PACPI_RESOURCE_HUB_FILE_CONTEXT Context,
    _In_ BOOLEAN Lock)
{
    NTSTATUS Status;

    RhAcquireControllerLock(Context);
    if (Context->ControllerLocked == Lock)
    {
        Status = Lock ? STATUS_DEVICE_BUSY : STATUS_INVALID_DEVICE_STATE;
        RhReleaseControllerLock(Context);
        return Status;
    }

    Status = RhBindSpbController(Context);
    if (NT_SUCCESS(Status))
    {
        Status = RhSendInternalIoctl(
                     Context->ControllerDeviceObject,
                     Lock ? IOCTL_INTERNAL_SPB_LOCK_CONTROLLER :
                            IOCTL_INTERNAL_SPB_UNLOCK_CONTROLLER,
                     NULL,
                     0,
                     NULL,
                     0,
                     NULL);
        if (NT_SUCCESS(Status))
            Context->ControllerLocked = Lock;
    }
    RhReleaseControllerLock(Context);
    return Status;
}

NTSTATUS
AcpiResourceHubInitialize(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING DeviceName, SymbolicName;
    NTSTATUS Status;

    ExInitializeFastMutex(&AcpiResourceHubMutex);
    RtlInitUnicodeString(&DeviceName, RESOURCE_HUB_DEVICE_NAME);
    Status = IoCreateDevice(DriverObject,
                            0,
                            &DeviceName,
                            FILE_DEVICE_RESOURCE_HUB,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &AcpiResourceHubDeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    AcpiResourceHubDeviceObject->Flags |= DO_DIRECT_IO;
    RtlInitUnicodeString(&SymbolicName, RESOURCE_HUB_SYMBOLIC_NAME);
    Status = IoCreateSymbolicLink(&SymbolicName, &DeviceName);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(AcpiResourceHubDeviceObject);
        AcpiResourceHubDeviceObject = NULL;
        return Status;
    }

    AcpiResourceHubDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

VOID
AcpiResourceHubRegisterFdo(
    _In_ PFDO_DEVICE_DATA FdoData)
{
    ExAcquireFastMutex(&AcpiResourceHubMutex);
    AcpiResourceHubFdoData = FdoData;
    ExReleaseFastMutex(&AcpiResourceHubMutex);
}

VOID
AcpiResourceHubUnregisterFdo(
    _In_ PFDO_DEVICE_DATA FdoData)
{
    ExAcquireFastMutex(&AcpiResourceHubMutex);
    if (AcpiResourceHubFdoData == FdoData)
        AcpiResourceHubFdoData = NULL;
    ExReleaseFastMutex(&AcpiResourceHubMutex);
}

BOOLEAN
AcpiResourceHubIsDevice(
    _In_ PDEVICE_OBJECT DeviceObject)
{
    return DeviceObject == AcpiResourceHubDeviceObject;
}

NTSTATUS
NTAPI
AcpiResourceHubCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpStack;
    PFILE_OBJECT FileObject;
    PACPI_RESOURCE_HUB_FILE_CONTEXT Context;
    LARGE_INTEGER ConnectionId;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    FileObject = IrpStack->FileObject;

    if (IrpStack->MajorFunction == IRP_MJ_CLOSE)
    {
        Context = FileObject->FsContext;
        FileObject->FsContext = NULL;
        if (Context != NULL && Context->Signature == RH_CONTEXT_SIGNATURE)
        {
            if (Context->ControllerLocked)
                RhSetControllerLock(Context, FALSE);
            if (Context->ControllerFileObject != NULL)
                ObDereferenceObject(Context->ControllerFileObject);
            RhReleaseConnectionResource(Context->OwnerPdo,
                                        &Context->ResourceBuffer);
            ExFreePoolWithTag(Context, RH_CONTEXT_TAG);
        }
        return RhCompleteIrp(Irp, STATUS_SUCCESS, 0);
    }

    Status = RhParseConnectionId(&FileObject->FileName, &ConnectionId);
    if (!NT_SUCCESS(Status))
        return RhCompleteIrp(Irp, Status, 0);

    Context = ExAllocatePoolWithTag(NonPagedPool,
                                    sizeof(*Context),
                                    RH_CONTEXT_TAG);
    if (Context == NULL)
        return RhCompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);

    RtlZeroMemory(Context, sizeof(*Context));
    Context->Signature = RH_CONTEXT_SIGNATURE;
    Context->ConnectionId = ConnectionId;
    KeInitializeEvent(&Context->ControllerLock, SynchronizationEvent, TRUE);

    if (ConnectionId.QuadPart != 0)
    {
        Status = RhGetConnectionResource(ConnectionId,
                                         &Context->OwnerPdo,
                                         &Context->OwnerHandle,
                                         &Context->ResourceBuffer,
                                         &Context->Resource);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Context, RH_CONTEXT_TAG);
            return RhCompleteIrp(Irp, Status, 0);
        }
    }

    FileObject->FsContext = Context;
    return RhCompleteIrp(Irp, STATUS_SUCCESS, 0);
}

NTSTATUS
NTAPI
AcpiResourceHubDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpStack;
    PACPI_RESOURCE_HUB_FILE_CONTEXT Context;
    PRH_QUERY_CONNECTION_PROPERTIES_INPUT_BUFFER Input;
    PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER Output;
    LARGE_INTEGER ConnectionId;
    PDEVICE_OBJECT OwnerPdo = NULL;
    ACPI_HANDLE OwnerHandle;
    ACPI_BUFFER ResourceBuffer;
    ACPI_RESOURCE *Resource;
    REACTOS_SPB_TRANSFER_LIST NormalizedList;
    PUCHAR Properties = NULL;
    ULONG PropertiesLength = 0, RequiredLength;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return RhCompleteIrp(Irp, STATUS_INVALID_DEVICE_STATE, 0);

    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    Context = IrpStack->FileObject->FsContext;
    if (Context == NULL || Context->Signature != RH_CONTEXT_SIGNATURE)
        return RhCompleteIrp(Irp, STATUS_INVALID_HANDLE, 0);

    switch (IrpStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_RH_QUERY_CONNECTION_PROPERTIES:
            if (IrpStack->Parameters.DeviceIoControl.InputBufferLength >= sizeof(*Input))
            {
                Input = Irp->AssociatedIrp.SystemBuffer;
                if (Input->Version != RH_QUERY_CONNECTION_PROPERTIES_INPUT_VERSION ||
                    Input->QueryType != ConnectionIdType)
                {
                    return RhCompleteIrp(Irp, STATUS_INVALID_PARAMETER, 0);
                }
                ConnectionId = Input->u.ConnectionId;
            }
            else if (Context->ConnectionId.QuadPart != 0)
            {
                ConnectionId = Context->ConnectionId;
            }
            else
            {
                return RhCompleteIrp(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            }

            Status = RhGetConnectionResource(ConnectionId,
                                             &OwnerPdo,
                                             &OwnerHandle,
                                             &ResourceBuffer,
                                             &Resource);
            if (!NT_SUCCESS(Status))
                return RhCompleteIrp(Irp, Status, 0);

            Status = RhSerializeConnectionProperties(Resource,
                                                     &Properties,
                                                     &PropertiesLength);
            RhReleaseConnectionResource(OwnerPdo, &ResourceBuffer);
            if (!NT_SUCCESS(Status))
                return RhCompleteIrp(Irp, Status, 0);

            RequiredLength = FIELD_OFFSET(RH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER,
                                          ConnectionProperties) + PropertiesLength;
            if (IrpStack->Parameters.DeviceIoControl.OutputBufferLength <
                FIELD_OFFSET(RH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER,
                             ConnectionProperties))
            {
                AcpiOsFree(Properties);
                return RhCompleteIrp(Irp, STATUS_BUFFER_TOO_SMALL, RequiredLength);
            }

            Output = Irp->AssociatedIrp.SystemBuffer;
            Output->Version = RH_QUERY_CONNECTION_PROPERTIES_OUTPUT_VERSION;
            Output->PropertiesLength = PropertiesLength;
            if (IrpStack->Parameters.DeviceIoControl.OutputBufferLength < RequiredLength)
            {
                AcpiOsFree(Properties);
                return RhCompleteIrp(Irp,
                                     STATUS_BUFFER_OVERFLOW,
                                     FIELD_OFFSET(RH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER,
                                                  ConnectionProperties));
            }

            RtlCopyMemory(Output->ConnectionProperties,
                          Properties,
                          PropertiesLength);
            AcpiOsFree(Properties);
            return RhCompleteIrp(Irp, STATUS_SUCCESS, RequiredLength);

        case IOCTL_SPB_EXECUTE_SEQUENCE:
            if (Context->Resource == NULL)
                return RhCompleteIrp(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

            Status = RhNormalizeTransferList(
                         Irp,
                         Irp->AssociatedIrp.SystemBuffer,
                         IrpStack->Parameters.DeviceIoControl.InputBufferLength,
                         Context,
                         &NormalizedList);
            if (NT_SUCCESS(Status))
                Status = RhExecuteTransferList(Context, &NormalizedList, &Information);
            return RhCompleteIrp(Irp, Status, Information);

        case IOCTL_SPB_LOCK_CONTROLLER:
        case IOCTL_SPB_LOCK_CONNECTION:
            if (Context->Resource == NULL)
                return RhCompleteIrp(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
            Status = RhSetControllerLock(Context, TRUE);
            return RhCompleteIrp(Irp, Status, 0);

        case IOCTL_SPB_UNLOCK_CONTROLLER:
        case IOCTL_SPB_UNLOCK_CONNECTION:
            if (Context->Resource == NULL)
                return RhCompleteIrp(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
            Status = RhSetControllerLock(Context, FALSE);
            return RhCompleteIrp(Irp, Status, 0);

        default:
            return RhCompleteIrp(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }
}

NTSTATUS
NTAPI
AcpiResourceHubReadWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpStack;
    PACPI_RESOURCE_HUB_FILE_CONTEXT Context;
    REACTOS_SPB_TRANSFER_LIST TransferList;
    PVOID Buffer;
    ULONG Length;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    if (!AcpiResourceHubIsDevice(DeviceObject))
        return RhCompleteIrp(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);

    if (KeGetCurrentIrql() != PASSIVE_LEVEL)
        return RhCompleteIrp(Irp, STATUS_INVALID_DEVICE_STATE, 0);

    IrpStack = IoGetCurrentIrpStackLocation(Irp);
    Context = IrpStack->FileObject->FsContext;
    if (Context == NULL || Context->Signature != RH_CONTEXT_SIGNATURE ||
        Context->Resource == NULL)
    {
        return RhCompleteIrp(Irp, STATUS_INVALID_HANDLE, 0);
    }

    if (Irp->MdlAddress == NULL)
        return RhCompleteIrp(Irp, STATUS_INVALID_USER_BUFFER, 0);

    Buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    if (Buffer == NULL)
        return RhCompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);

    Length = (IrpStack->MajorFunction == IRP_MJ_READ) ?
             IrpStack->Parameters.Read.Length :
             IrpStack->Parameters.Write.Length;
    if (Length == 0)
        return RhCompleteIrp(Irp, STATUS_SUCCESS, 0);

    if (Context->Resource->Type != ACPI_RESOURCE_TYPE_SERIAL_BUS ||
        Context->Resource->Data.CommonSerialBus.Type != ACPI_RESOURCE_SERIAL_TYPE_I2C)
    {
        return RhCompleteIrp(Irp, STATUS_NOT_SUPPORTED, 0);
    }

    RtlZeroMemory(&TransferList, sizeof(TransferList));
    TransferList.Version = REACTOS_SPB_INTERFACE_VERSION;
    TransferList.SlaveAddress = Context->Resource->Data.I2cSerialBus.SlaveAddress;
    TransferList.TypeSpecificFlags = Context->Resource->Data.I2cSerialBus.AccessMode;
    TransferList.ConnectionSpeed = Context->Resource->Data.I2cSerialBus.ConnectionSpeed;
    TransferList.TransferCount = 1;
    TransferList.Transfers[0].Direction =
        (IrpStack->MajorFunction == IRP_MJ_READ) ?
        SpbTransferDirectionFromDevice : SpbTransferDirectionToDevice;
    TransferList.Transfers[0].Buffer = Buffer;
    TransferList.Transfers[0].Length = Length;

    Status = RhExecuteTransferList(Context, &TransferList, &Information);
    if (NT_SUCCESS(Status))
        Information = Length;
    return RhCompleteIrp(Irp, Status, Information);
}
