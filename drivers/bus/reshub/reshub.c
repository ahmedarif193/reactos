/*
 * PROJECT:     ReactOS Resource Hub
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Broker firmware connection properties by connection ID
 */

#include <ntddk.h>
#include <initguid.h>
#include <gpio.h>
#include <spb.h>
#include <reactos/drivers/reshubio.h>
#include <reactos/drivers/intelgpio.h>
#include <reactos/drivers/inteli2c.h>

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

#define RH_AML_SERIAL_TYPE 5
#define RH_AML_SERIAL_TYPE_FLAGS 7
#define RH_AML_SERIAL_TYPE_DATA_LENGTH 10
#define RH_AML_SERIAL_TYPE_DATA 12
#define RH_AML_I2C_MINIMUM_LENGTH 18
#define RH_AML_I2C_TYPE 1
#define RH_AML_I2C_CONNECTION_SPEED 12
#define RH_AML_I2C_SLAVE_ADDRESS 16

#define RH_SPB_MAXIMUM_TRANSFERS 64
#define RH_SPB_MAXIMUM_BUFFER_LIST_ENTRIES 1024
#define RH_SPB_MAXIMUM_REQUEST_LENGTH (1024 * 1024)

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

typedef struct _RH_FILE_CONTEXT
{
    PRH_CONNECTION_ENTRY Connection;
    ULONG ControllerIndex;
    ULONG LockDepth;
} RH_FILE_CONTEXT, *PRH_FILE_CONTEXT;

typedef struct _RH_I2C_GATE
{
    KSPIN_LOCK StateLock;
    KEVENT StateChanged;
    PRH_FILE_CONTEXT Owner;
    ULONG OwnerDepth;
    ULONG ActiveTransfers;
    ULONG LockWaiters;
} RH_I2C_GATE, *PRH_I2C_GATE;

static LIST_ENTRY RhConnectionList;
static FAST_MUTEX RhConnectionLock;
static PDEVICE_OBJECT RhDeviceObject;
static UNICODE_STRING RhSymbolicName;
static RH_I2C_GATE RhI2cGates[6];

static
NTSTATUS
RhSendSynchronousIoctl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG IoControlCode,
    _Inout_updates_bytes_(BufferLength) PVOID Buffer,
    _In_ ULONG BufferLength);

static
VOID
RhReleaseAllI2cLocks(
    _Inout_ PRH_FILE_CONTEXT Context)
{
    PRH_I2C_GATE Gate;
    KIRQL OldIrql;

    if (!Context->LockDepth || Context->ControllerIndex >= RTL_NUMBER_OF(RhI2cGates))
        return;
    Gate = &RhI2cGates[Context->ControllerIndex];
    KeAcquireSpinLock(&Gate->StateLock, &OldIrql);
    if (Gate->Owner == Context)
    {
        Gate->Owner = NULL;
        Gate->OwnerDepth = 0;
        Context->LockDepth = 0;
        KeSetEvent(&Gate->StateChanged, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&Gate->StateLock, OldIrql);
}

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
    PRH_FILE_CONTEXT Context;
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
            Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Context), RH_TAG);
            if (!Context)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Complete;
            }
            RtlZeroMemory(Context, sizeof(*Context));
            Context->ControllerIndex = MAXULONG;
            ExAcquireFastMutex(&RhConnectionLock);
            Entry = RhFindConnectionLocked(ConnectionId);
            if (Entry)
            {
                InterlockedIncrement(&Entry->ReferenceCount);
                Context->Connection = Entry;
                FileObject->FsContext = Context;
            }
            else
            {
                Status = STATUS_OBJECT_NAME_NOT_FOUND;
            }
            ExReleaseFastMutex(&RhConnectionLock);
            if (!Entry)
                ExFreePoolWithTag(Context, RH_TAG);
        }
    }
    else if (IrpStack->MajorFunction == IRP_MJ_CLOSE && FileObject->FsContext)
    {
        Context = FileObject->FsContext;
        FileObject->FsContext = NULL;
        Entry = Context->Connection;
        RhReleaseAllI2cLocks(Context);
        ExAcquireFastMutex(&RhConnectionLock);
        if (InterlockedDecrement(&Entry->ReferenceCount) == 0 && Entry->Deleted)
            ExFreePoolWithTag(Entry, RH_TAG);
        ExReleaseFastMutex(&RhConnectionLock);
        ExFreePoolWithTag(Context, RH_TAG);
    }

Complete:
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
ULONG
RhReadUlong(
    _In_reads_bytes_(sizeof(ULONG)) const UCHAR *Buffer)
{
    ULONG Value;

    RtlCopyMemory(&Value, Buffer, sizeof(Value));
    return Value;
}

static
NTSTATUS
RhParseI2cConnection(
    _In_ PRH_CONNECTION_ENTRY Entry,
    _Out_ PULONG ControllerIndex,
    _Out_ PULONG ConnectionSpeed,
    _Out_ PUSHORT SlaveAddress,
    _Out_ PUSHORT AddressMode)
{
    PUCHAR Properties = (PUCHAR)Entry->Properties;
    ULONG DescriptorLength;
    USHORT TypeDataLength;
    ULONG ResourceSourceOffset;
    ULONG Index;

    if (Entry->Class != CM_RESOURCE_CONNECTION_CLASS_SERIAL || Entry->Type != CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C || Entry->PropertiesLength < RH_AML_I2C_MINIMUM_LENGTH)
        return STATUS_INVALID_DEVICE_REQUEST;
    if (!(Properties[0] & 0x80) || Properties[RH_AML_SERIAL_TYPE] != RH_AML_I2C_TYPE)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    DescriptorLength = RH_AML_LARGE_HEADER_LENGTH + RhReadUshort(Properties + 1);
    TypeDataLength = RhReadUshort(Properties + RH_AML_SERIAL_TYPE_DATA_LENGTH);
    if (DescriptorLength > Entry->PropertiesLength || DescriptorLength < RH_AML_I2C_MINIMUM_LENGTH || TypeDataLength < 6 || TypeDataLength > DescriptorLength - RH_AML_SERIAL_TYPE_DATA)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    ResourceSourceOffset = RH_AML_SERIAL_TYPE_DATA + TypeDataLength;
    if (ResourceSourceOffset >= DescriptorLength)
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    *ConnectionSpeed = RhReadUlong(Properties + RH_AML_I2C_CONNECTION_SPEED);
    *SlaveAddress = RhReadUshort(Properties + RH_AML_I2C_SLAVE_ADDRESS);
    *AddressMode = (RhReadUshort(Properties + RH_AML_SERIAL_TYPE_FLAGS) & 1) ? INTELI2C_ADDRESS_MODE_10BIT : INTELI2C_ADDRESS_MODE_7BIT;
    for (Index = ResourceSourceOffset; Index + 3 < DescriptorLength; Index++)
    {
        if (RtlUpcaseUnicodeChar((WCHAR)Properties[Index]) == L'I' && Properties[Index + 1] == '2' && RtlUpcaseUnicodeChar((WCHAR)Properties[Index + 2]) == L'C' && Properties[Index + 3] >= '0' && Properties[Index + 3] <= '5')
        {
            *ControllerIndex = Properties[Index + 3] - '0';
            return STATUS_SUCCESS;
        }
    }
    return STATUS_DEVICE_CONFIGURATION_ERROR;
}

static
NTSTATUS
RhPrepareI2cContext(
    _Inout_ PRH_FILE_CONTEXT Context)
{
    ULONG ControllerIndex;
    ULONG ConnectionSpeed;
    USHORT SlaveAddress;
    USHORT AddressMode;
    NTSTATUS Status;

    if (Context->ControllerIndex < RTL_NUMBER_OF(RhI2cGates))
        return STATUS_SUCCESS;
    Status = RhParseI2cConnection(Context->Connection, &ControllerIndex, &ConnectionSpeed, &SlaveAddress, &AddressMode);
    if (!NT_SUCCESS(Status))
        return Status;
    Context->ControllerIndex = ControllerIndex;
    return STATUS_SUCCESS;
}

static
NTSTATUS
RhAcquireI2cLock(
    _Inout_ PRH_FILE_CONTEXT Context)
{
    PRH_I2C_GATE Gate;
    BOOLEAN Waiting = FALSE;
    KIRQL OldIrql;
    NTSTATUS Status;

    Status = RhPrepareI2cContext(Context);
    if (!NT_SUCCESS(Status))
        return Status;
    Gate = &RhI2cGates[Context->ControllerIndex];
    for (;;)
    {
        KeAcquireSpinLock(&Gate->StateLock, &OldIrql);
        if (Gate->Owner == Context)
        {
            Gate->OwnerDepth++;
            Context->LockDepth++;
            KeReleaseSpinLock(&Gate->StateLock, OldIrql);
            return STATUS_SUCCESS;
        }
        if (!Waiting)
        {
            Gate->LockWaiters++;
            Waiting = TRUE;
        }
        if (!Gate->Owner && !Gate->ActiveTransfers)
        {
            Gate->LockWaiters--;
            Gate->Owner = Context;
            Gate->OwnerDepth = 1;
            Context->LockDepth++;
            KeResetEvent(&Gate->StateChanged);
            KeReleaseSpinLock(&Gate->StateLock, OldIrql);
            return STATUS_SUCCESS;
        }
        KeResetEvent(&Gate->StateChanged);
        KeReleaseSpinLock(&Gate->StateLock, OldIrql);
        Status = KeWaitForSingleObject(&Gate->StateChanged, Executive, KernelMode, FALSE, NULL);
        if (!NT_SUCCESS(Status))
            return Status;
    }
}

static
NTSTATUS
RhReleaseI2cLock(
    _Inout_ PRH_FILE_CONTEXT Context)
{
    PRH_I2C_GATE Gate;
    KIRQL OldIrql;
    NTSTATUS Status;

    Status = RhPrepareI2cContext(Context);
    if (!NT_SUCCESS(Status))
        return Status;
    Gate = &RhI2cGates[Context->ControllerIndex];
    KeAcquireSpinLock(&Gate->StateLock, &OldIrql);
    if (Gate->Owner != Context || !Gate->OwnerDepth || !Context->LockDepth)
    {
        KeReleaseSpinLock(&Gate->StateLock, OldIrql);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Gate->OwnerDepth--;
    Context->LockDepth--;
    if (!Gate->OwnerDepth)
    {
        Gate->Owner = NULL;
        KeSetEvent(&Gate->StateChanged, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&Gate->StateLock, OldIrql);
    return STATUS_SUCCESS;
}

static
NTSTATUS
RhEnterI2cTransfer(
    _Inout_ PRH_FILE_CONTEXT Context)
{
    PRH_I2C_GATE Gate;
    KIRQL OldIrql;
    NTSTATUS Status;

    Status = RhPrepareI2cContext(Context);
    if (!NT_SUCCESS(Status))
        return Status;
    Gate = &RhI2cGates[Context->ControllerIndex];
    for (;;)
    {
        KeAcquireSpinLock(&Gate->StateLock, &OldIrql);
        if (Gate->Owner == Context || (!Gate->Owner && !Gate->LockWaiters))
        {
            Gate->ActiveTransfers++;
            KeReleaseSpinLock(&Gate->StateLock, OldIrql);
            return STATUS_SUCCESS;
        }
        KeResetEvent(&Gate->StateChanged);
        KeReleaseSpinLock(&Gate->StateLock, OldIrql);
        Status = KeWaitForSingleObject(&Gate->StateChanged, Executive, KernelMode, FALSE, NULL);
        if (!NT_SUCCESS(Status))
            return Status;
    }
}

static
VOID
RhLeaveI2cTransfer(
    _In_ PRH_FILE_CONTEXT Context)
{
    PRH_I2C_GATE Gate = &RhI2cGates[Context->ControllerIndex];
    KIRQL OldIrql;

    KeAcquireSpinLock(&Gate->StateLock, &OldIrql);
    if (Gate->ActiveTransfers)
        Gate->ActiveTransfers--;
    if (!Gate->ActiveTransfers)
        KeSetEvent(&Gate->StateChanged, IO_NO_INCREMENT, FALSE);
    KeReleaseSpinLock(&Gate->StateLock, OldIrql);
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
ULONG
RhUnicodeStringLength(
    _In_ PCWSTR String)
{
    ULONG Length = 0;

    while (String[Length])
        Length++;
    return Length;
}

static
NTSTATUS
RhOpenIntelI2c(
    _In_ ULONG ControllerIndex,
    _Out_ PFILE_OBJECT *FileObject,
    _Out_ PDEVICE_OBJECT *DeviceObject)
{
    PWCHAR InterfaceList = NULL;
    PWCHAR CurrentInterface;
    NTSTATUS Status;

    *FileObject = NULL;
    *DeviceObject = NULL;
    Status = IoGetDeviceInterfaces(&GUID_DEVINTERFACE_INTEL_I2C, NULL, 0, &InterfaceList);
    if (!NT_SUCCESS(Status))
        return Status;
    Status = STATUS_DEVICE_NOT_CONNECTED;
    for (CurrentInterface = InterfaceList; *CurrentInterface; CurrentInterface += RhUnicodeStringLength(CurrentInterface) + 1)
    {
        INTELI2C_CONTROLLER_INFORMATION Information;
        UNICODE_STRING InterfaceName;
        PFILE_OBJECT CandidateFileObject = NULL;
        PDEVICE_OBJECT CandidateDeviceObject = NULL;

        RtlInitUnicodeString(&InterfaceName, CurrentInterface);
        Status = IoGetDeviceObjectPointer(&InterfaceName, FILE_READ_DATA | FILE_WRITE_DATA, &CandidateFileObject, &CandidateDeviceObject);
        if (!NT_SUCCESS(Status))
            continue;
        RtlZeroMemory(&Information, sizeof(Information));
        Information.Version = INTELI2C_INTERFACE_VERSION;
        Status = RhSendSynchronousIoctl(CandidateDeviceObject, IOCTL_INTELI2C_QUERY_CONTROLLER, &Information, sizeof(Information));
        if (NT_SUCCESS(Status) && Information.Version == INTELI2C_INTERFACE_VERSION && Information.ControllerIndex == ControllerIndex)
        {
            *FileObject = CandidateFileObject;
            *DeviceObject = CandidateDeviceObject;
            break;
        }
        ObDereferenceObject(CandidateFileObject);
        Status = STATUS_DEVICE_NOT_CONNECTED;
    }
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
RhGetSpbBufferLength(
    _In_ PSPB_TRANSFER_LIST_ENTRY Transfer,
    _Out_ PULONG BufferLength)
{
    ULONG Length = 0;
    ULONG Index;

    switch (Transfer->Buffer.Format)
    {
        case SpbTransferBufferFormatSimple:
        case SpbTransferBufferFormatSimpleNonPaged:
            if (!Transfer->Buffer.Simple.Buffer && Transfer->Buffer.Simple.BufferCb)
                return STATUS_INVALID_PARAMETER;
            Length = Transfer->Buffer.Simple.BufferCb;
            break;

        case SpbTransferBufferFormatList:
            if (!Transfer->Buffer.BufferList.List || !Transfer->Buffer.BufferList.ListCe || Transfer->Buffer.BufferList.ListCe > RH_SPB_MAXIMUM_BUFFER_LIST_ENTRIES)
                return STATUS_INVALID_PARAMETER;
            for (Index = 0; Index < Transfer->Buffer.BufferList.ListCe; Index++)
            {
                PSPB_TRANSFER_BUFFER_LIST_ENTRY Entry = &Transfer->Buffer.BufferList.List[Index];

                if ((!Entry->Buffer && Entry->BufferCb) || Entry->BufferCb > MAXULONG - Length)
                    return STATUS_INVALID_PARAMETER;
                Length += Entry->BufferCb;
            }
            break;

        case SpbTransferBufferFormatMdl:
            if (!Transfer->Buffer.Mdl)
                return STATUS_INVALID_PARAMETER;
            Length = MmGetMdlByteCount(Transfer->Buffer.Mdl);
            break;

        default:
            return STATUS_INVALID_PARAMETER;
    }
    if (!Length || Length > RH_SPB_MAXIMUM_REQUEST_LENGTH)
        return STATUS_INVALID_PARAMETER;
    *BufferLength = Length;
    return STATUS_SUCCESS;
}

static
NTSTATUS
RhCopySpbBuffer(
    _In_ PSPB_TRANSFER_LIST_ENTRY Transfer,
    _Inout_updates_bytes_(BufferLength) PUCHAR Buffer,
    _In_ ULONG BufferLength,
    _In_ BOOLEAN ToTransferBuffer)
{
    ULONG Offset = 0;
    ULONG Index;

    switch (Transfer->Buffer.Format)
    {
        case SpbTransferBufferFormatSimple:
        case SpbTransferBufferFormatSimpleNonPaged:
            if (ToTransferBuffer)
                RtlCopyMemory(Transfer->Buffer.Simple.Buffer, Buffer, BufferLength);
            else
                RtlCopyMemory(Buffer, Transfer->Buffer.Simple.Buffer, BufferLength);
            return STATUS_SUCCESS;

        case SpbTransferBufferFormatList:
            for (Index = 0; Index < Transfer->Buffer.BufferList.ListCe; Index++)
            {
                PSPB_TRANSFER_BUFFER_LIST_ENTRY Entry = &Transfer->Buffer.BufferList.List[Index];
                ULONG CopyLength;

                if (Offset == BufferLength)
                    break;
                CopyLength = min(Entry->BufferCb, BufferLength - Offset);
                if (ToTransferBuffer)
                    RtlCopyMemory(Entry->Buffer, Buffer + Offset, CopyLength);
                else
                    RtlCopyMemory(Buffer + Offset, Entry->Buffer, CopyLength);
                Offset += CopyLength;
            }
            return Offset == BufferLength ? STATUS_SUCCESS : STATUS_INVALID_PARAMETER;

        case SpbTransferBufferFormatMdl:
        {
            PVOID MappedBuffer = MmGetSystemAddressForMdlSafe(Transfer->Buffer.Mdl, NormalPagePriority);

            if (!MappedBuffer || MmGetMdlByteCount(Transfer->Buffer.Mdl) < BufferLength)
                return STATUS_INSUFFICIENT_RESOURCES;
            if (ToTransferBuffer)
                RtlCopyMemory(MappedBuffer, Buffer, BufferLength);
            else
                RtlCopyMemory(Buffer, MappedBuffer, BufferLength);
            return STATUS_SUCCESS;
        }

        default:
            return STATUS_INVALID_PARAMETER;
    }
}

static
NTSTATUS
RhExecuteSpbTransfers(
    _Inout_ PRH_FILE_CONTEXT Context,
    _In_reads_(TransferCount) PSPB_TRANSFER_LIST_ENTRY Transfers,
    _In_ ULONG TransferCount,
    _Out_opt_ PULONG BytesTransferred)
{
    PINTELI2C_TRANSFER_REQUEST Request = NULL;
    PFILE_OBJECT I2cFileObject = NULL;
    PDEVICE_OBJECT I2cDeviceObject = NULL;
    ULONG ControllerIndex;
    ULONG ConnectionSpeed;
    USHORT SlaveAddress;
    USHORT AddressMode;
    ULONG HeaderLength;
    ULONG RequestLength;
    ULONG Offset;
    ULONG Index;
    ULONG CompletedBytes = 0;
    BOOLEAN GateEntered = FALSE;
    NTSTATUS Status;

    if (!TransferCount || TransferCount > RH_SPB_MAXIMUM_TRANSFERS)
        return STATUS_INVALID_PARAMETER;
    Status = RhParseI2cConnection(Context->Connection, &ControllerIndex, &ConnectionSpeed, &SlaveAddress, &AddressMode);
    if (!NT_SUCCESS(Status))
        return Status;
    if (TransferCount > (MAXULONG - FIELD_OFFSET(INTELI2C_TRANSFER_REQUEST, Transfers)) / sizeof(INTELI2C_TRANSFER_ENTRY))
        return STATUS_INTEGER_OVERFLOW;
    HeaderLength = FIELD_OFFSET(INTELI2C_TRANSFER_REQUEST, Transfers) + TransferCount * sizeof(INTELI2C_TRANSFER_ENTRY);
    RequestLength = HeaderLength;
    for (Index = 0; Index < TransferCount; Index++)
    {
        ULONG BufferLength;

        if (Transfers[Index].Direction != SpbTransferDirectionFromDevice && Transfers[Index].Direction != SpbTransferDirectionToDevice)
            return STATUS_INVALID_PARAMETER;
        Status = RhGetSpbBufferLength(&Transfers[Index], &BufferLength);
        if (!NT_SUCCESS(Status))
            return Status;
        if (BufferLength > RH_SPB_MAXIMUM_REQUEST_LENGTH - RequestLength)
            return STATUS_INVALID_BUFFER_SIZE;
        RequestLength += BufferLength;
    }
    Request = ExAllocatePoolWithTag(NonPagedPool, RequestLength, RH_TAG);
    if (!Request)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Request, RequestLength);
    Request->Version = INTELI2C_INTERFACE_VERSION;
    Request->ControllerIndex = ControllerIndex;
    Request->SlaveAddress = SlaveAddress;
    Request->AddressMode = AddressMode;
    Request->ConnectionSpeed = ConnectionSpeed ? ConnectionSpeed : 100000;
    Request->TimeoutMilliseconds = 2000;
    Request->TransferCount = TransferCount;
    Offset = HeaderLength;
    for (Index = 0; Index < TransferCount; Index++)
    {
        ULONG BufferLength;

        Status = RhGetSpbBufferLength(&Transfers[Index], &BufferLength);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
        Request->Transfers[Index].Direction = Transfers[Index].Direction == SpbTransferDirectionFromDevice ? IntelI2cTransferDirectionRead : IntelI2cTransferDirectionWrite;
        Request->Transfers[Index].DelayInUs = Transfers[Index].DelayInUs;
        Request->Transfers[Index].BufferOffset = Offset;
        Request->Transfers[Index].BufferLength = BufferLength;
        if (Transfers[Index].Direction == SpbTransferDirectionToDevice)
        {
            Status = RhCopySpbBuffer(&Transfers[Index], (PUCHAR)Request + Offset, BufferLength, FALSE);
            if (!NT_SUCCESS(Status))
                goto Cleanup;
        }
        Offset += BufferLength;
    }
    Status = RhEnterI2cTransfer(Context);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    GateEntered = TRUE;
    Status = RhOpenIntelI2c(ControllerIndex, &I2cFileObject, &I2cDeviceObject);
    if (!NT_SUCCESS(Status))
        goto Cleanup;
    Status = RhSendSynchronousIoctl(I2cDeviceObject, IOCTL_INTELI2C_EXECUTE_TRANSFER, Request, RequestLength);
    for (Index = 0; Index < TransferCount; Index++)
    {
        ULONG Transferred = min(Request->Transfers[Index].Transferred, Request->Transfers[Index].BufferLength);

        if (Transferred > MAXULONG - CompletedBytes)
        {
            Status = STATUS_INTEGER_OVERFLOW;
            break;
        }
        if (Transfers[Index].Direction == SpbTransferDirectionFromDevice && Transferred)
        {
            NTSTATUS CopyStatus = RhCopySpbBuffer(&Transfers[Index], (PUCHAR)Request + Request->Transfers[Index].BufferOffset, Transferred, TRUE);

            if (!NT_SUCCESS(CopyStatus) && NT_SUCCESS(Status))
                Status = CopyStatus;
        }
        CompletedBytes += Transferred;
    }

Cleanup:
    if (I2cFileObject)
        ObDereferenceObject(I2cFileObject);
    if (GateEntered)
        RhLeaveI2cTransfer(Context);
    if (Request)
        ExFreePoolWithTag(Request, RH_TAG);
    if (BytesTransferred)
        *BytesTransferred = CompletedBytes;
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
RhAccessSpbConnection(
    _Inout_ PRH_FILE_CONTEXT Context,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpStack)
{
    PSPB_TRANSFER_LIST TransferList = Irp->AssociatedIrp.SystemBuffer;
    ULONG InputLength = IrpStack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG RequiredLength;

    switch (IrpStack->Parameters.DeviceIoControl.IoControlCode)
    {
        case IOCTL_SPB_LOCK_CONTROLLER:
        case IOCTL_SPB_LOCK_CONNECTION:
            return RhAcquireI2cLock(Context);

        case IOCTL_SPB_UNLOCK_CONTROLLER:
        case IOCTL_SPB_UNLOCK_CONNECTION:
            return RhReleaseI2cLock(Context);

        case IOCTL_SPB_EXECUTE_SEQUENCE:
            if (Irp->RequestorMode != KernelMode)
                return STATUS_ACCESS_DENIED;
            if (!TransferList || InputLength < sizeof(SPB_TRANSFER_LIST) || TransferList->Size != sizeof(SPB_TRANSFER_LIST) || TransferList->Reserved || !TransferList->TransferCount || TransferList->TransferCount > RH_SPB_MAXIMUM_TRANSFERS)
                return STATUS_INVALID_PARAMETER;
            if (TransferList->TransferCount > (MAXULONG - FIELD_OFFSET(SPB_TRANSFER_LIST, Transfers)) / sizeof(SPB_TRANSFER_LIST_ENTRY))
                return STATUS_INTEGER_OVERFLOW;
            RequiredLength = FIELD_OFFSET(SPB_TRANSFER_LIST, Transfers) + TransferList->TransferCount * sizeof(SPB_TRANSFER_LIST_ENTRY);
            if (InputLength < RequiredLength)
                return STATUS_BUFFER_TOO_SMALL;
            return RhExecuteSpbTransfers(Context, TransferList->Transfers, TransferList->TransferCount, NULL);

        default:
            return STATUS_INVALID_DEVICE_REQUEST;
    }
}

static
NTSTATUS
NTAPI
RhReadWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    PRH_FILE_CONTEXT Context = IrpStack->FileObject ? IrpStack->FileObject->FsContext : NULL;
    PRH_CONNECTION_ENTRY Entry = Context ? Context->Connection : NULL;
    SPB_TRANSFER_LIST_ENTRY Transfer;
    ULONG BufferLength;
    ULONG BytesTransferred = 0;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Information = 0;
    if (!Entry || Entry->Class != CM_RESOURCE_CONNECTION_CLASS_SERIAL || Entry->Type != CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Complete;
    }
    BufferLength = IrpStack->MajorFunction == IRP_MJ_READ ? IrpStack->Parameters.Read.Length : IrpStack->Parameters.Write.Length;
    if (!Irp->AssociatedIrp.SystemBuffer || !BufferLength)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }
    RtlZeroMemory(&Transfer, sizeof(Transfer));
    Transfer.Direction = IrpStack->MajorFunction == IRP_MJ_READ ? SpbTransferDirectionFromDevice : SpbTransferDirectionToDevice;
    Transfer.Buffer.Format = SpbTransferBufferFormatSimpleNonPaged;
    Transfer.Buffer.Simple.Buffer = Irp->AssociatedIrp.SystemBuffer;
    Transfer.Buffer.Simple.BufferCb = BufferLength;
    Status = RhExecuteSpbTransfers(Context, &Transfer, 1, &BytesTransferred);
    Irp->IoStatus.Information = BytesTransferred;

Complete:
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
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
    PRH_FILE_CONTEXT Context = IrpStack->FileObject ? IrpStack->FileObject->FsContext : NULL;
    PRH_CONNECTION_ENTRY Entry = Context ? Context->Connection : NULL;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Information = 0;
    if (Entry && Entry->Class == CM_RESOURCE_CONNECTION_CLASS_GPIO)
    {
        Status = RhAccessGpioConnection(Entry, Irp, IrpStack);
    }
    else if (Entry && Entry->Class == CM_RESOURCE_CONNECTION_CLASS_SERIAL && Entry->Type == CM_RESOURCE_CONNECTION_TYPE_SERIAL_I2C)
    {
        Status = RhAccessSpbConnection(Context, Irp, IrpStack);
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
    ULONG Index;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);
    InitializeListHead(&RhConnectionList);
    ExInitializeFastMutex(&RhConnectionLock);
    for (Index = 0; Index < RTL_NUMBER_OF(RhI2cGates); Index++)
    {
        KeInitializeSpinLock(&RhI2cGates[Index].StateLock);
        KeInitializeEvent(&RhI2cGates[Index].StateChanged, NotificationEvent, TRUE);
    }
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
    DriverObject->MajorFunction[IRP_MJ_READ] = RhReadWrite;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = RhReadWrite;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = RhDeviceControl;
    DriverObject->DriverUnload = RhUnload;
    RhDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    DPRINT1("RESHUB: connection broker initialized\n");
    return STATUS_SUCCESS;
}
