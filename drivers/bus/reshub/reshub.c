/*
 * PROJECT:     ReactOS Resource Hub
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Broker firmware connection properties by connection ID
 */

#include <ntddk.h>
#include <reactos/drivers/reshubio.h>

#define NDEBUG
#include <debug.h>

#define RH_TAG 'bHuR'

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
NTSTATUS
NTAPI
RhDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpStack = IoGetCurrentIrpStackLocation(Irp);
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    Irp->IoStatus.Information = 0;
    switch (IrpStack->Parameters.DeviceIoControl.IoControlCode)
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
