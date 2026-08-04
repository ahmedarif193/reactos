/*
 * PROJECT:     ReactOS Resource Hub
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Broker firmware connection properties by connection ID
 */

#include <ntddk.h>
#include <initguid.h>
#include <gpio.h>
#include <reactos/drivers/reshubio.h>
#include <reactos/drivers/intelgpio.h>

#define NDEBUG
#include <debug.h>

#define RH_TAG 'bHuR'

#define RH_AML_LARGE_HEADER_LENGTH 3
#define RH_AML_GPIO_MINIMUM_LENGTH 23
#define RH_AML_GPIO_CONNECTION_TYPE 4
#define RH_AML_GPIO_INT_FLAGS 7
#define RH_AML_GPIO_PIN_CONFIG 9
#define RH_AML_GPIO_DEBOUNCE 12
#define RH_AML_GPIO_PIN_TABLE_OFFSET 14
#define RH_AML_GPIO_RESOURCE_SOURCE_OFFSET 17

typedef struct _RH_CONNECTION_ENTRY
{
    LIST_ENTRY ListEntry;
    LARGE_INTEGER ConnectionId;
    LONG ReferenceCount;
    BOOLEAN Deleted;
    UCHAR Class;
    UCHAR Type;
    UCHAR Reserved;
    ULONG PropertiesLength;
    UCHAR Properties[ANYSIZE_ARRAY];
} RH_CONNECTION_ENTRY, *PRH_CONNECTION_ENTRY;

static LIST_ENTRY RhConnectionList;
static FAST_MUTEX RhConnectionLock;
static PDEVICE_OBJECT RhDeviceObject;
static UNICODE_STRING RhSymbolicName;

static
PRH_CONNECTION_ENTRY
RhFindConnectionLocked(
    _In_ LARGE_INTEGER ConnectionId)
{
    PLIST_ENTRY Link;

    for (Link = RhConnectionList.Flink; Link != &RhConnectionList; Link = Link->Flink)
    {
        PRH_CONNECTION_ENTRY Entry = CONTAINING_RECORD(Link, RH_CONNECTION_ENTRY, ListEntry);

        if (!Entry->Deleted && Entry->ConnectionId.QuadPart == ConnectionId.QuadPart)
            return Entry;
    }
    return NULL;
}

static
BOOLEAN
RhParseConnectionId(
    _In_ PUNICODE_STRING FileName,
    _Out_ PLARGE_INTEGER ConnectionId)
{
    ULONGLONG Value = 0;
    USHORT Index = 0;
    USHORT Digits = 0;

    if (!FileName || !FileName->Buffer)
        return FALSE;
    if (FileName->Length >= sizeof(WCHAR) && FileName->Buffer[0] == L'\\')
        Index++;
    while (Index < FileName->Length / sizeof(WCHAR))
    {
        WCHAR Character = FileName->Buffer[Index++];
        UCHAR Digit;

        if (Character >= L'0' && Character <= L'9')
            Digit = (UCHAR)(Character - L'0');
        else if (Character >= L'a' && Character <= L'f')
            Digit = (UCHAR)(Character - L'a' + 10);
        else if (Character >= L'A' && Character <= L'F')
            Digit = (UCHAR)(Character - L'A' + 10);
        else
            return FALSE;
        if (++Digits > 16)
            return FALSE;
        Value = (Value << 4) | Digit;
    }
    if (Digits != 16)
        return FALSE;
    ConnectionId->QuadPart = Value;
    return TRUE;
}

static
NTSTATUS
NTAPI
RhCreateClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpStack->FileObject;
    PRH_CONNECTION_ENTRY Entry;
    LARGE_INTEGER ConnectionId;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (IrpStack->MajorFunction == IRP_MJ_CREATE && FileObject->FileName.Length)
    {
        if (!RhParseConnectionId(&FileObject->FileName, &ConnectionId))
            Status = STATUS_OBJECT_NAME_INVALID;
        else
        {
            ExAcquireFastMutex(&RhConnectionLock);
            Entry = RhFindConnectionLocked(ConnectionId);
            if (Entry)
            {
                InterlockedIncrement(&Entry->ReferenceCount);
                FileObject->FsContext = Entry;
            }
            else
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
            }
            ExReleaseFastMutex(&RhConnectionLock);
        }
    }
    else if (IrpStack->MajorFunction == IRP_MJ_CLOSE && FileObject->FsContext)
    {
        Entry = FileObject->FsContext;
        FileObject->FsContext = NULL;
        ExAcquireFastMutex(&RhConnectionLock);
        if (InterlockedDecrement(&Entry->ReferenceCount) == 0 && Entry->Deleted)
            ExFreePoolWithTag(Entry, RH_TAG);
        ExReleaseFastMutex(&RhConnectionLock);
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
NTSTATUS
RhRegisterConnection(
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpStack)
{
    PRH_REGISTER_CONNECTION_INPUT Input = Irp->AssociatedIrp.SystemBuffer;
    PRH_CONNECTION_ENTRY NewEntry;
    PRH_CONNECTION_ENTRY OldEntry;
    ULONG InputLength = IrpStack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG RequiredLength;
    ULONG EntryLength;
    BOOLEAN FreeOld = FALSE;

    if (InputLength < FIELD_OFFSET(RH_REGISTER_CONNECTION_INPUT, Properties) || !Input)
        return STATUS_BUFFER_TOO_SMALL;
    if (Input->Version != RH_REGISTER_CONNECTION_VERSION || !Input->ConnectionId.QuadPart || !Input->PropertiesLength)
        return STATUS_INVALID_PARAMETER;
    if (Input->PropertiesLength > MAXULONG - FIELD_OFFSET(RH_REGISTER_CONNECTION_INPUT, Properties))
        return STATUS_INTEGER_OVERFLOW;
    RequiredLength = FIELD_OFFSET(RH_REGISTER_CONNECTION_INPUT, Properties) + Input->PropertiesLength;
    if (InputLength < RequiredLength)
        return STATUS_BUFFER_TOO_SMALL;
    if (Input->PropertiesLength > MAXULONG - FIELD_OFFSET(RH_CONNECTION_ENTRY, Properties))
        return STATUS_INTEGER_OVERFLOW;
    EntryLength = FIELD_OFFSET(RH_CONNECTION_ENTRY, Properties) + Input->PropertiesLength;
    NewEntry = ExAllocatePoolWithTag(NonPagedPool, EntryLength, RH_TAG);
    if (!NewEntry)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(NewEntry, FIELD_OFFSET(RH_CONNECTION_ENTRY, Properties));
    NewEntry->ConnectionId = Input->ConnectionId;
    NewEntry->Class = Input->Class;
    NewEntry->Type = Input->Type;
    NewEntry->PropertiesLength = Input->PropertiesLength;
    RtlCopyMemory(NewEntry->Properties, Input->Properties, Input->PropertiesLength);

    ExAcquireFastMutex(&RhConnectionLock);
    OldEntry = RhFindConnectionLocked(Input->ConnectionId);
    if (OldEntry && OldEntry->Class == Input->Class && OldEntry->Type == Input->Type && OldEntry->PropertiesLength == Input->PropertiesLength && RtlCompareMemory(OldEntry->Properties, Input->Properties, Input->PropertiesLength) == Input->PropertiesLength)
    {
        ExReleaseFastMutex(&RhConnectionLock);
        ExFreePoolWithTag(NewEntry, RH_TAG);
        return STATUS_SUCCESS;
    }
    if (OldEntry)
    {
        RemoveEntryList(&OldEntry->ListEntry);
        OldEntry->Deleted = TRUE;
        FreeOld = OldEntry->ReferenceCount == 0;
    }
    InsertTailList(&RhConnectionList, &NewEntry->ListEntry);
    ExReleaseFastMutex(&RhConnectionLock);
    if (FreeOld)
        ExFreePoolWithTag(OldEntry, RH_TAG);
    return STATUS_SUCCESS;
}

static
NTSTATUS
RhQueryConnection(
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpStack)
{
    PRH_QUERY_CONNECTION_PROPERTIES_INPUT_BUFFER Input = Irp->AssociatedIrp.SystemBuffer;
    PRH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER Output = Irp->AssociatedIrp.SystemBuffer;
    RH_QUERY_CONNECTION_PROPERTIES_INPUT_BUFFER InputCopy;
    PRH_CONNECTION_ENTRY Entry;
    ULONG InputLength = IrpStack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = IrpStack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG HeaderLength = FIELD_OFFSET(RH_QUERY_CONNECTION_PROPERTIES_OUTPUT_BUFFER, ConnectionProperties);
    ULONG RequiredLength;

    if (!Input || InputLength < sizeof(*Input))
        return STATUS_BUFFER_TOO_SMALL;
    InputCopy = *Input;
    if (InputCopy.Version != RH_QUERY_CONNECTION_PROPERTIES_INPUT_VERSION || InputCopy.QueryType != ConnectionIdType)
        return STATUS_INVALID_PARAMETER;

    ExAcquireFastMutex(&RhConnectionLock);
    Entry = RhFindConnectionLocked(InputCopy.u.ConnectionId);
    if (!Entry)
    {
        ExReleaseFastMutex(&RhConnectionLock);
        return STATUS_OBJECT_NAME_NOT_FOUND;
    }
    if (Entry->PropertiesLength > MAXULONG - HeaderLength)
    {
        ExReleaseFastMutex(&RhConnectionLock);
        return STATUS_INTEGER_OVERFLOW;
    }
    RequiredLength = HeaderLength + Entry->PropertiesLength;
    if (OutputLength >= HeaderLength)
    {
        Output->Version = RH_QUERY_CONNECTION_PROPERTIES_OUTPUT_VERSION;
        Output->PropertiesLength = Entry->PropertiesLength;
        Irp->IoStatus.Information = HeaderLength;
    }
    if (OutputLength < RequiredLength)
    {
        ExReleaseFastMutex(&RhConnectionLock);
        return STATUS_BUFFER_TOO_SMALL;
    }
    RtlCopyMemory(Output->ConnectionProperties, Entry->Properties, Entry->PropertiesLength);
    Irp->IoStatus.Information = RequiredLength;
    ExReleaseFastMutex(&RhConnectionLock);
    return STATUS_SUCCESS;
}

static
USHORT
RhReadUshort(
    _In_reads_bytes_(sizeof(USHORT)) const UCHAR *Buffer)
{
    USHORT Value;

    RtlCopyMemory(&Value, Buffer, sizeof(Value));
    return Value;
}

static
NTSTATUS
RhParseGpioConnection(
    _In_ PRH_CONNECTION_ENTRY Entry,
    _Out_ PUSHORT PinTableOffset,
    _Out_ PUSHORT PinCount,
    _Out_ PUCHAR IoRestriction,
    _Out_ PUCHAR PinConfiguration,
    _Out_ PUSHORT DebounceTimeout)
{
    PUCHAR Properties = (PUCHAR)Entry->Properties;
    ULONG DescriptorLength;
    USHORT ResourceSourceOffset;

    if (Entry->Class != CM_RESOURCE_CONNECTION_CLASS_GPIO || Entry->Type != CM_RESOURCE_CONNECTION_TYPE_GPIO_IO || Entry->PropertiesLength < RH_AML_GPIO_MINIMUM_LENGTH)
        return STATUS_INVALID_DEVICE_REQUEST;
    if (!(Properties[0] & 0x80) || Properties[RH_AML_GPIO_CONNECTION_TYPE] != 1)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    DescriptorLength = RH_AML_LARGE_HEADER_LENGTH + RhReadUshort(Properties + 1);
    if (DescriptorLength > Entry->PropertiesLength || DescriptorLength < RH_AML_GPIO_MINIMUM_LENGTH)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    *PinTableOffset = RhReadUshort(Properties + RH_AML_GPIO_PIN_TABLE_OFFSET);
    ResourceSourceOffset = RhReadUshort(Properties + RH_AML_GPIO_RESOURCE_SOURCE_OFFSET);
    if (*PinTableOffset < RH_AML_GPIO_MINIMUM_LENGTH || ResourceSourceOffset < *PinTableOffset || ResourceSourceOffset > DescriptorLength || (ResourceSourceOffset - *PinTableOffset) % sizeof(USHORT))
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    *PinCount = (ResourceSourceOffset - *PinTableOffset) / sizeof(USHORT);
    if (!*PinCount)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    *IoRestriction = Properties[RH_AML_GPIO_INT_FLAGS] & 3;
    *PinConfiguration = Properties[RH_AML_GPIO_PIN_CONFIG];
    *DebounceTimeout = RhReadUshort(Properties + RH_AML_GPIO_DEBOUNCE);
    return STATUS_SUCCESS;
}

static
ULONG
RhGpioDebounceExponent(
    _In_ USHORT DebounceTimeout)
{
    ULONGLONG PeriodUnits;
    ULONG Exponent = 0;

    if (!DebounceTimeout)
        return INTELGPIO_DEBOUNCE_PRESERVE;
    PeriodUnits = ((ULONGLONG)DebounceTimeout * 1000 + 31249) / 31250;
    while (PeriodUnits > 1 && Exponent < 15)
    {
        PeriodUnits = (PeriodUnits + 1) >> 1;
        Exponent++;
    }
    return Exponent;
}

static
NTSTATUS
RhOpenIntelGpio(
    _Out_ PFILE_OBJECT *FileObject,
    _Out_ PDEVICE_OBJECT *DeviceObject)
{
    PWCHAR InterfaceList = NULL;
    UNICODE_STRING InterfaceName;
    NTSTATUS Status;

    *FileObject = NULL;
    *DeviceObject = NULL;
    Status = IoGetDeviceInterfaces(&GUID_DEVINTERFACE_INTEL_GPIO, NULL, 0, &InterfaceList);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!InterfaceList[0])
    {
        ExFreePool(InterfaceList);
        return STATUS_DEVICE_NOT_CONNECTED;
    }
    RtlInitUnicodeString(&InterfaceName, InterfaceList);
    Status = IoGetDeviceObjectPointer(&InterfaceName, FILE_READ_DATA | FILE_WRITE_DATA, FileObject, DeviceObject);
    ExFreePool(InterfaceList);
    return Status;
}

static
NTSTATUS
RhSendSynchronousIoctl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _Inout_updates_bytes_(BufferLength) PVOID Buffer,
    _In_ ULONG BufferLength)
{
    IO_STATUS_BLOCK IoStatus;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildDeviceIoControlRequest(IoControlCode, DeviceObject, Buffer, BufferLength, Buffer, BufferLength, FALSE, &Event, &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = IoCallDriver(DeviceObject, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }
    return Status;
}

static
NTSTATUS
RhAccessGpioConnection(
    _In_ PRH_CONNECTION_ENTRY Entry,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpStack)
{
    PFILE_OBJECT GpioFileObject;
    PDEVICE_OBJECT GpioDeviceObject;
    PUCHAR Buffer = Irp->AssociatedIrp.SystemBuffer;
    ULONG IoControlCode = IrpStack->Parameters.DeviceIoControl.IoControlCode;
    ULONG BufferLength;
    USHORT PinTableOffset;
    USHORT PinCount;
    USHORT DebounceTimeout;
    UCHAR IoRestriction;
    UCHAR PinConfiguration;
    ULONG PinIndex;
    NTSTATUS Status;

    Status = RhParseGpioConnection(Entry, &PinTableOffset, &PinCount, &IoRestriction, &PinConfiguration, &DebounceTimeout);
    if (!NT_SUCCESS(Status))
        return Status;
    BufferLength = (PinCount + 7) / 8;
    if (IoControlCode == IOCTL_GPIO_READ_PINS)
    {
        if (IoRestriction == 2)
            return STATUS_ACCESS_DENIED;
        if (!Buffer || IrpStack->Parameters.DeviceIoControl.OutputBufferLength < BufferLength)
            return STATUS_BUFFER_TOO_SMALL;
        RtlZeroMemory(Buffer, BufferLength);
    }
    else if (IoControlCode == IOCTL_GPIO_WRITE_PINS)
    {
        if (IoRestriction == 1)
            return STATUS_ACCESS_DENIED;
        if (!Buffer || IrpStack->Parameters.DeviceIoControl.InputBufferLength < BufferLength)
            return STATUS_BUFFER_TOO_SMALL;
    }
    else
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Status = RhOpenIntelGpio(&GpioFileObject, &GpioDeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    for (PinIndex = 0; PinIndex < PinCount; PinIndex++)
    {
        ULONG PinNumber = RhReadUshort(Entry->Properties + PinTableOffset + PinIndex * sizeof(USHORT));

        if (IoRestriction != 3)
        {
            INTELGPIO_PIN_CONFIGURATION Configuration;

            RtlZeroMemory(&Configuration, sizeof(Configuration));
            Configuration.Version = INTELGPIO_INTERFACE_VERSION;
            Configuration.PinNumber = PinNumber;
            Configuration.Direction = IoControlCode == IOCTL_GPIO_READ_PINS ? IntelGpioDirectionInput : IntelGpioDirectionOutput;
            Configuration.InitialValue = !!(Buffer[PinIndex / 8] & (1U << (PinIndex % 8)));
            Configuration.PullConfiguration = PinConfiguration == 1 ? IntelGpioPullUp20K : PinConfiguration == 2 ? IntelGpioPullDown20K : PinConfiguration == 3 ? IntelGpioPullNone : IntelGpioPullPreserve;
            Configuration.DebounceExponent = RhGpioDebounceExponent(DebounceTimeout);
            Status = RhSendSynchronousIoctl(GpioDeviceObject, IOCTL_INTELGPIO_CONFIGURE_PIN, &Configuration, sizeof(Configuration));
            if (!NT_SUCCESS(Status))
                break;
        }
        if (IoControlCode == IOCTL_GPIO_READ_PINS)
        {
            INTELGPIO_PIN_INFORMATION Information;

            RtlZeroMemory(&Information, sizeof(Information));
            Information.Version = INTELGPIO_INTERFACE_VERSION;
            Information.PinNumber = PinNumber;
            Status = RhSendSynchronousIoctl(GpioDeviceObject, IOCTL_INTELGPIO_QUERY_PIN, &Information, sizeof(Information));
            if (!NT_SUCCESS(Status))
                break;
            if (Information.Value)
                Buffer[PinIndex / 8] |= 1U << (PinIndex % 8);
        }
        else
        {
            INTELGPIO_PIN_WRITE Write;

            Write.Version = INTELGPIO_INTERFACE_VERSION;
            Write.PinNumber = PinNumber;
            Write.Value = !!(Buffer[PinIndex / 8] & (1U << (PinIndex % 8)));
            Status = RhSendSynchronousIoctl(GpioDeviceObject, IOCTL_INTELGPIO_WRITE_PIN, &Write, sizeof(Write));
            if (!NT_SUCCESS(Status))
                break;
        }
    }
    ObDereferenceObject(GpioFileObject);
    if (NT_SUCCESS(Status))
        Irp->IoStatus.Information = IoControlCode == IOCTL_GPIO_READ_PINS ? BufferLength : 0;
    return Status;
}

static
NTSTATUS
NTAPI
RhDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    PRH_CONNECTION_ENTRY Entry = IrpStack->FileObject ? IrpStack->FileObject->FsContext : NULL;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Information = 0;
    if (Entry && Entry->Class == CM_RESOURCE_CONNECTION_CLASS_GPIO)
    {
        Status = RhAccessGpioConnection(Entry, Irp, IrpStack);
    }
    else switch (IrpStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_RH_QUERY_CONNECTION_PROPERTIES:
            Status = RhQueryConnection(Irp, IrpStack);
            break;

        case IOCTL_RH_REGISTER_CONNECTION:
            Status = RhRegisterConnection(Irp, IrpStack);
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
VOID
NTAPI
RhUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
    IoDeleteSymbolicLink(&RhSymbolicName);
    while (!IsListEmpty(&RhConnectionList))
    {
        PLIST_ENTRY Link = RemoveHeadList(&RhConnectionList);
        PRH_CONNECTION_ENTRY Entry = CONTAINING_RECORD(Link, RH_CONNECTION_ENTRY, ListEntry);

        ExFreePoolWithTag(Entry, RH_TAG);
    }
    if (RhDeviceObject)
        IoDeleteDevice(RhDeviceObject);
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(RESOURCE_HUB_DEVICE_NAME);
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);
    InitializeListHead(&RhConnectionList);
    ExInitializeFastMutex(&RhConnectionLock);
    RtlInitUnicodeString(&RhSymbolicName, RESOURCE_HUB_SYMBOLIC_NAME);
    Status = IoCreateDevice(DriverObject, 0, &DeviceName, FILE_DEVICE_RESOURCE_HUB, FILE_DEVICE_SECURE_OPEN, FALSE, &RhDeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;
    RhDeviceObject->Flags |= DO_BUFFERED_IO;
    Status = IoCreateSymbolicLink(&RhSymbolicName, &DeviceName);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(RhDeviceObject);
        RhDeviceObject = NULL;
        return Status;
    }
    DriverObject->MajorFunction[IRP_MJ_CREATE] = RhCreateClose;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = RhCreateClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = RhDeviceControl;
    DriverObject->DriverUnload = RhUnload;
    RhDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    DPRINT1("RESHUB: connection broker initialized\n");
    return STATUS_SUCCESS;
}
