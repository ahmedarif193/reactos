/*
 * PROJECT:     LiberNT Filter Manager
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Communication ports
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "fltmgr.h"

#define NDEBUG
#include <debug.h>

typedef struct _FLTP_SERVER_PORT
{
    PFLT_FILTER Filter;
    PVOID Cookie;
    PFLT_CONNECT_NOTIFY ConnectNotify;
    PFLT_DISCONNECT_NOTIFY DisconnectNotify;
    PFLT_MESSAGE_NOTIFY MessageNotify;
    LONG MaxConnections;
    volatile LONG ConnectionCount;
} FLTP_SERVER_PORT, *PFLTP_SERVER_PORT;

typedef struct _FLTP_CLIENT_PORT
{
    LIST_ENTRY FilterLink;
    volatile LONG ReferenceCount;
    PFLTP_SERVER_PORT ServerPort;
    PFLT_FILTER Filter;
    PVOID Cookie;
    EX_RUNDOWN_REF Rundown;
    KSPIN_LOCK Lock;
    LIST_ENTRY MessageList;
    LIST_ENTRY ReplyList;
    LIST_ENTRY IrpList;
    IO_CSQ Csq;
    ULONGLONG NextMessageId;
    BOOLEAN Disconnected;
    volatile LONG DisconnectNotified;
} FLTP_CLIENT_PORT, *PFLTP_CLIENT_PORT;

#define FLTP_MESSAGE_QUEUED    0
#define FLTP_MESSAGE_DELIVERED 1
#define FLTP_MESSAGE_DONE      2

typedef struct _FLTP_MESSAGE
{
    LIST_ENTRY Link;
    KEVENT Event;
    ULONGLONG MessageId;
    NTSTATUS Status;
    ULONG State;
    ULONG SenderLength;
    ULONG ReplyCapacity;
    ULONG ReplyLength;
    BOOLEAN ReplyExpected;
    PUCHAR Reply;
    UCHAR Sender[ANYSIZE_ARRAY];
} FLTP_MESSAGE, *PFLTP_MESSAGE;

static GENERIC_MAPPING FltpPortMapping =
{
    STANDARD_RIGHTS_READ,
    STANDARD_RIGHTS_WRITE,
    STANDARD_RIGHTS_EXECUTE | FLT_PORT_CONNECT,
    FLT_PORT_ALL_ACCESS
};

static
VOID
NTAPI
FltpDeleteServerPort(
    _In_ PVOID Object)
{
    PFLTP_SERVER_PORT Port = Object;

    if (Port->Filter != NULL)
    {
        FltpDereferencePointer(&Port->Filter->Base);
    }
}

static
VOID
FltpDereferenceClientPort(
    _In_ PFLTP_CLIENT_PORT Port)
{
    if (InterlockedDecrement(&Port->ReferenceCount) == 0)
    {
        ObDereferenceObject(Port->ServerPort);
        FltpDereferencePointer(&Port->Filter->Base);
        ExFreePoolWithTag(Port, FLT_TAG_PORT);
    }
}

static
VOID
NTAPI
FltpCsqAcquireLock(
    _In_ PIO_CSQ Csq,
    _Out_ PKIRQL Irql)
{
    KeAcquireSpinLock(&CONTAINING_RECORD(Csq, FLTP_CLIENT_PORT, Csq)->Lock, Irql);
}

static
VOID
NTAPI
FltpCsqReleaseLock(
    _In_ PIO_CSQ Csq,
    _In_ KIRQL Irql)
{
    KeReleaseSpinLock(&CONTAINING_RECORD(Csq, FLTP_CLIENT_PORT, Csq)->Lock, Irql);
}

static
NTSTATUS
NTAPI
FltpCsqInsertIrp(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp,
    _In_ PVOID InsertContext)
{
    PFLTP_CLIENT_PORT Port = CONTAINING_RECORD(Csq, FLTP_CLIENT_PORT, Csq);

    UNREFERENCED_PARAMETER(InsertContext);

    if (Port->Disconnected)
    {
        return STATUS_PORT_DISCONNECTED;
    }

    InsertTailList(&Port->IrpList, &Irp->Tail.Overlay.ListEntry);
    return STATUS_SUCCESS;
}

static
VOID
NTAPI
FltpCsqRemoveIrp(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);

    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
}

static
PIRP
NTAPI
FltpCsqPeekNextIrp(
    _In_ PIO_CSQ Csq,
    _In_opt_ PIRP Irp,
    _In_opt_ PVOID PeekContext)
{
    PFLTP_CLIENT_PORT Port = CONTAINING_RECORD(Csq, FLTP_CLIENT_PORT, Csq);
    PLIST_ENTRY Link;

    if (PeekContext != NULL && IsListEmpty(&Port->MessageList))
    {
        return NULL;
    }

    Link = (Irp != NULL) ? Irp->Tail.Overlay.ListEntry.Flink : Port->IrpList.Flink;
    if (Link == &Port->IrpList)
    {
        return NULL;
    }

    return CONTAINING_RECORD(Link, IRP, Tail.Overlay.ListEntry);
}

static
VOID
NTAPI
FltpCsqCompleteCanceledIrp(
    _In_ PIO_CSQ Csq,
    _In_ PIRP Irp)
{
    UNREFERENCED_PARAMETER(Csq);

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
VOID
FltpCompleteIrp(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
VOID
FltpPumpMessages(
    _In_ PFLTP_CLIENT_PORT Port)
{
    PIO_STACK_LOCATION Stack;
    PFILTER_MESSAGE_HEADER Header;
    PFLTP_MESSAGE Message;
    ULONG Required;
    NTSTATUS Status;
    KIRQL Irql;
    PIRP Irp;

    for (;;)
    {
        Irp = IoCsqRemoveNextIrp(&Port->Csq, Port);
        if (Irp == NULL)
        {
            return;
        }

        Stack = IoGetCurrentIrpStackLocation(Irp);
        Header = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
        if (Header == NULL)
        {
            FltpCompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
            continue;
        }

        KeAcquireSpinLock(&Port->Lock, &Irql);
        if (IsListEmpty(&Port->MessageList))
        {
            KeReleaseSpinLock(&Port->Lock, Irql);
            Status = IoCsqInsertIrpEx(&Port->Csq, Irp, NULL, NULL);
            if (!NT_SUCCESS(Status))
            {
                FltpCompleteIrp(Irp, Status, 0);
            }
            return;
        }

        Message = CONTAINING_RECORD(Port->MessageList.Flink, FLTP_MESSAGE, Link);
        Required = sizeof(FILTER_MESSAGE_HEADER) + Message->SenderLength;
        if (Stack->Parameters.DeviceIoControl.OutputBufferLength < Required)
        {
            KeReleaseSpinLock(&Port->Lock, Irql);
            FltpCompleteIrp(Irp, STATUS_BUFFER_TOO_SMALL, 0);
            continue;
        }

        RemoveEntryList(&Message->Link);
        Header->ReplyLength = Message->ReplyExpected ? Message->ReplyCapacity + sizeof(FILTER_REPLY_HEADER) : 0;
        Header->MessageId = Message->MessageId;
        RtlCopyMemory(Header + 1, Message->Sender, Message->SenderLength);

        if (Message->ReplyExpected)
        {
            Message->State = FLTP_MESSAGE_DELIVERED;
            InsertTailList(&Port->ReplyList, &Message->Link);
        }
        else
        {
            Message->State = FLTP_MESSAGE_DONE;
            Message->Status = STATUS_SUCCESS;
            KeSetEvent(&Message->Event, IO_NO_INCREMENT, FALSE);
        }
        KeReleaseSpinLock(&Port->Lock, Irql);

        FltpCompleteIrp(Irp, STATUS_SUCCESS, Required);
    }
}

static
VOID
FltpDisconnectClientPort(
    _In_ PFLTP_CLIENT_PORT Port)
{
    PFLTP_MESSAGE Message;
    PLIST_ENTRY Link;
    BOOLEAN WasConnected;
    KIRQL Irql;
    PIRP Irp;

    KeAcquireSpinLock(&Port->Lock, &Irql);
    WasConnected = !Port->Disconnected;
    Port->Disconnected = TRUE;
    while (!IsListEmpty(&Port->MessageList) || !IsListEmpty(&Port->ReplyList))
    {
        Link = !IsListEmpty(&Port->MessageList) ? RemoveHeadList(&Port->MessageList)
                                                : RemoveHeadList(&Port->ReplyList);
        Message = CONTAINING_RECORD(Link, FLTP_MESSAGE, Link);
        Message->State = FLTP_MESSAGE_DONE;
        Message->Status = STATUS_PORT_DISCONNECTED;
        KeSetEvent(&Message->Event, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&Port->Lock, Irql);

    if (!WasConnected)
    {
        return;
    }

    while ((Irp = IoCsqRemoveNextIrp(&Port->Csq, NULL)) != NULL)
    {
        FltpCompleteIrp(Irp, STATUS_PORT_DISCONNECTED, 0);
    }

    ExWaitForRundownProtectionRelease(&Port->Rundown);

    ExAcquireFastMutex(&Port->Filter->PortLock);
    RemoveEntryList(&Port->FilterLink);
    InitializeListHead(&Port->FilterLink);
    ExReleaseFastMutex(&Port->Filter->PortLock);

    InterlockedDecrement(&Port->ServerPort->ConnectionCount);
    InterlockedDecrement(&Port->Filter->ClientPortCount);
}

static
VOID
FltpNotifyDisconnect(
    _In_ PFLTP_CLIENT_PORT Port)
{
    if (InterlockedExchange(&Port->DisconnectNotified, TRUE) == FALSE)
    {
        Port->ServerPort->DisconnectNotify(Port->Cookie);
    }
}

NTSTATUS
FltpInitializePorts(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNICODE_STRING TypeName = RTL_CONSTANT_STRING(L"FilterConnectionPort");
    OBJECT_TYPE_INITIALIZER Initializer;

    UNREFERENCED_PARAMETER(DriverObject);

    RtlZeroMemory(&Initializer, sizeof(Initializer));
    Initializer.Length = sizeof(Initializer);
    Initializer.GenericMapping = FltpPortMapping;
    Initializer.ValidAccessMask = FLT_PORT_ALL_ACCESS;
    Initializer.PoolType = NonPagedPool;
    Initializer.SecurityRequired = TRUE;
    Initializer.DeleteProcedure = FltpDeleteServerPort;

    return ObCreateObjectType(&TypeName, &Initializer, NULL, &FltGlobals.ServerPortType);
}

VOID
FltpCloseFilterPorts(
    _In_ PFLT_FILTER Filter)
{
    PFLTP_CLIENT_PORT Port;
    PLIST_ENTRY Link;

    for (;;)
    {
        Port = NULL;

        ExAcquireFastMutex(&Filter->PortLock);
        for (Link = Filter->PortList.Flink; Link != &Filter->PortList; Link = Link->Flink)
        {
            Port = CONTAINING_RECORD(Link, FLTP_CLIENT_PORT, FilterLink);
            InterlockedIncrement(&Port->ReferenceCount);
            break;
        }
        ExReleaseFastMutex(&Filter->PortLock);

        if (Port == NULL)
        {
            break;
        }

        FltpNotifyDisconnect(Port);
        FltpDisconnectClientPort(Port);
        FltpDereferenceClientPort(Port);
    }
}

NTSTATUS
FLTAPI
FltCreateCommunicationPort(
    _In_ PFLT_FILTER Filter,
    _Outptr_ PFLT_PORT *ServerPort,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PVOID ServerPortCookie,
    _In_ PFLT_CONNECT_NOTIFY ConnectNotifyCallback,
    _In_ PFLT_DISCONNECT_NOTIFY DisconnectNotifyCallback,
    _In_opt_ PFLT_MESSAGE_NOTIFY MessageNotifyCallback,
    _In_ LONG MaxConnections)
{
    PFLTP_SERVER_PORT Port;
    HANDLE Handle;
    NTSTATUS Status;
    PAGED_CODE();

    *ServerPort = NULL;

    if (ObjectAttributes == NULL ||
        !(ObjectAttributes->Attributes & OBJ_KERNEL_HANDLE) ||
        ConnectNotifyCallback == NULL ||
        DisconnectNotifyCallback == NULL ||
        MaxConnections <= 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!ExAcquireRundownProtection(&Filter->Base.RundownRef))
    {
        return STATUS_FLT_DELETING_OBJECT;
    }

    Status = ObCreateObject(KernelMode,
                            FltGlobals.ServerPortType,
                            ObjectAttributes,
                            KernelMode,
                            NULL,
                            sizeof(FLTP_SERVER_PORT),
                            0,
                            0,
                            (PVOID *)&Port);
    if (NT_SUCCESS(Status))
    {
        RtlZeroMemory(Port, sizeof(*Port));
        Port->Filter = Filter;
        Port->Cookie = ServerPortCookie;
        Port->ConnectNotify = ConnectNotifyCallback;
        Port->DisconnectNotify = DisconnectNotifyCallback;
        Port->MessageNotify = MessageNotifyCallback;
        Port->MaxConnections = MaxConnections;
        FltpReferencePointer(&Filter->Base);

        Status = ObInsertObject(Port, NULL, FLT_PORT_ALL_ACCESS, 0, NULL, &Handle);
        if (NT_SUCCESS(Status))
        {
            *ServerPort = (PFLT_PORT)Handle;
        }
    }

    ExReleaseRundownProtection(&Filter->Base.RundownRef);
    return Status;
}

VOID
FLTAPI
FltCloseCommunicationPort(
    _In_ PFLT_PORT ServerPort)
{
    PAGED_CODE();

    ZwClose((HANDLE)ServerPort);
}

VOID
FLTAPI
FltCloseClientPort(
    _In_ PFLT_FILTER Filter,
    _Inout_ PFLT_PORT *ClientPort)
{
    PFLTP_CLIENT_PORT Port;
    PAGED_CODE();

    ExAcquireFastMutex(&Filter->PortLock);
    Port = (PFLTP_CLIENT_PORT)*ClientPort;
    *ClientPort = NULL;
    ExReleaseFastMutex(&Filter->PortLock);

    if (Port == NULL)
    {
        return;
    }

    InterlockedExchange(&Port->DisconnectNotified, TRUE);
    FltpDisconnectClientPort(Port);
    FltpDereferenceClientPort(Port);
}

NTSTATUS
FLTAPI
FltSendMessage(
    _In_ PFLT_FILTER Filter,
    _In_ PFLT_PORT *ClientPort,
    _In_reads_bytes_(SenderBufferLength) PVOID SenderBuffer,
    _In_ ULONG SenderBufferLength,
    _Out_writes_bytes_opt_(*ReplyLength) PVOID ReplyBuffer,
    _Inout_opt_ PULONG ReplyLength,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    PFLTP_CLIENT_PORT Port;
    PFLTP_MESSAGE Message;
    ULONG ReplyCapacity = 0;
    SIZE_T Size;
    NTSTATUS Status, WaitStatus;
    KIRQL Irql;

    if (SenderBuffer == NULL || (ReplyBuffer != NULL && ReplyLength == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (ReplyBuffer != NULL)
    {
        ReplyCapacity = *ReplyLength;
    }

    Size = FIELD_OFFSET(FLTP_MESSAGE, Sender) + (SIZE_T)SenderBufferLength + ReplyCapacity;
    if (Size < SenderBufferLength)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ExAcquireFastMutex(&Filter->PortLock);
    Port = (PFLTP_CLIENT_PORT)*ClientPort;
    if (Port != NULL)
    {
        InterlockedIncrement(&Port->ReferenceCount);
    }
    ExReleaseFastMutex(&Filter->PortLock);

    if (Port == NULL)
    {
        return STATUS_PORT_DISCONNECTED;
    }

    Message = ExAllocatePoolWithTag(NonPagedPoolNx, Size, FLT_TAG_PORT);
    if (Message == NULL)
    {
        FltpDereferenceClientPort(Port);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeInitializeEvent(&Message->Event, NotificationEvent, FALSE);
    Message->Status = STATUS_PENDING;
    Message->State = FLTP_MESSAGE_QUEUED;
    Message->SenderLength = SenderBufferLength;
    Message->ReplyCapacity = ReplyCapacity;
    Message->ReplyLength = 0;
    Message->ReplyExpected = (ReplyBuffer != NULL);
    Message->Reply = Message->Sender + SenderBufferLength;
    RtlCopyMemory(Message->Sender, SenderBuffer, SenderBufferLength);

    KeAcquireSpinLock(&Port->Lock, &Irql);
    if (Port->Disconnected)
    {
        KeReleaseSpinLock(&Port->Lock, Irql);
        ExFreePoolWithTag(Message, FLT_TAG_PORT);
        FltpDereferenceClientPort(Port);
        return STATUS_PORT_DISCONNECTED;
    }
    Message->MessageId = ++Port->NextMessageId;
    InsertTailList(&Port->MessageList, &Message->Link);
    KeReleaseSpinLock(&Port->Lock, Irql);

    FltpPumpMessages(Port);

    WaitStatus = KeWaitForSingleObject(&Message->Event, Executive, UserMode, FALSE, Timeout);

    KeAcquireSpinLock(&Port->Lock, &Irql);
    if (Message->State != FLTP_MESSAGE_DONE)
    {
        RemoveEntryList(&Message->Link);
        Message->State = FLTP_MESSAGE_DONE;
        Message->Status = (WaitStatus == STATUS_TIMEOUT) ? STATUS_TIMEOUT : STATUS_THREAD_IS_TERMINATING;
    }
    KeReleaseSpinLock(&Port->Lock, Irql);

    Status = Message->Status;
    if (Message->ReplyExpected && Status != STATUS_TIMEOUT && NT_SUCCESS(Status))
    {
        RtlCopyMemory(ReplyBuffer, Message->Reply, Message->ReplyLength);
        *ReplyLength = Message->ReplyLength;
    }

    ExFreePoolWithTag(Message, FLT_TAG_PORT);
    FltpDereferenceClientPort(Port);
    return Status;
}

NTSTATUS
FLTAPI
FltBuildDefaultSecurityDescriptor(
    _Outptr_ PSECURITY_DESCRIPTOR *SecurityDescriptor,
    _In_ ACCESS_MASK DesiredAccess)
{
    PSID Sids[2];
    PSECURITY_DESCRIPTOR Descriptor;
    ULONG AclLength, Index;
    NTSTATUS Status;
    PACL Dacl;
    PAGED_CODE();

    *SecurityDescriptor = NULL;

    Sids[0] = SeExports->SeLocalSystemSid;
    Sids[1] = SeExports->SeAliasAdminsSid;

    AclLength = sizeof(ACL);
    for (Index = 0; Index < RTL_NUMBER_OF(Sids); Index++)
    {
        AclLength += FIELD_OFFSET(ACCESS_ALLOWED_ACE, SidStart) + RtlLengthSid(Sids[Index]);
    }

    Descriptor = ExAllocatePoolWithTag(PagedPool, sizeof(SECURITY_DESCRIPTOR) + AclLength, FLT_TAG_PORT);
    if (Descriptor == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Dacl = (PACL)((PUCHAR)Descriptor + sizeof(SECURITY_DESCRIPTOR));
    Status = RtlCreateSecurityDescriptor(Descriptor, SECURITY_DESCRIPTOR_REVISION);
    if (NT_SUCCESS(Status))
    {
        Status = RtlCreateAcl(Dacl, AclLength, ACL_REVISION);
    }
    for (Index = 0; NT_SUCCESS(Status) && Index < RTL_NUMBER_OF(Sids); Index++)
    {
        Status = RtlAddAccessAllowedAce(Dacl, ACL_REVISION, DesiredAccess, Sids[Index]);
    }
    if (NT_SUCCESS(Status))
    {
        Status = RtlSetDaclSecurityDescriptor(Descriptor, TRUE, Dacl, FALSE);
    }
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Descriptor, FLT_TAG_PORT);
        return Status;
    }

    *SecurityDescriptor = Descriptor;
    return STATUS_SUCCESS;
}

VOID
FLTAPI
FltFreeSecurityDescriptor(
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor)
{
    PAGED_CODE();

    ExFreePoolWithTag(SecurityDescriptor, FLT_TAG_PORT);
}

static
NTSTATUS
FltpCheckPortAccess(
    _In_ PFLTP_SERVER_PORT ServerPort,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PSECURITY_SUBJECT_CONTEXT Subject;
    PSECURITY_DESCRIPTOR Descriptor;
    BOOLEAN Allocated, Granted;
    ACCESS_MASK GrantedAccess;
    NTSTATUS Status;

    if (Irp->RequestorMode == KernelMode)
    {
        return STATUS_SUCCESS;
    }

    Status = ObGetObjectSecurity(ServerPort, &Descriptor, &Allocated);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (Descriptor == NULL)
    {
        return STATUS_SUCCESS;
    }

    Subject = &Stack->Parameters.Create.SecurityContext->AccessState->SubjectSecurityContext;
    SeLockSubjectContext(Subject);
    Granted = SeAccessCheck(Descriptor,
                            Subject,
                            TRUE,
                            FLT_PORT_CONNECT,
                            0,
                            NULL,
                            &FltpPortMapping,
                            UserMode,
                            &GrantedAccess,
                            &Status);
    SeUnlockSubjectContext(Subject);
    ObReleaseObjectSecurity(Descriptor, Allocated);

    return Granted ? STATUS_SUCCESS : Status;
}

static
NTSTATUS
FltpConnectPort(
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PFILE_FULL_EA_INFORMATION Ea = Irp->AssociatedIrp.SystemBuffer;
    ULONG EaLength = Stack->Parameters.Create.EaLength;
    PFILTER_PORT_DATA PortData;
    PFLTP_SERVER_PORT ServerPort;
    PFLTP_CLIENT_PORT Port;
    UNICODE_STRING PortName;
    PVOID Context = NULL;
    ULONG ValueLength;
    PFLT_FILTER Filter;
    NTSTATUS Status;

    if (Ea == NULL ||
        EaLength < FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + FLT_PORT_EA_NAME_LENGTH + 1 ||
        Ea->EaNameLength != FLT_PORT_EA_NAME_LENGTH ||
        RtlCompareMemory(Ea->EaName, FLT_PORT_EA_NAME, FLT_PORT_EA_NAME_LENGTH) != FLT_PORT_EA_NAME_LENGTH)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ValueLength = Ea->EaValueLength;
    if (ValueLength < FIELD_OFFSET(FILTER_PORT_DATA, PortName) ||
        EaLength < FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + FLT_PORT_EA_NAME_LENGTH + 1 + ValueLength)
    {
        return STATUS_INVALID_PARAMETER;
    }

    PortData = (PFILTER_PORT_DATA)&Ea->EaName[FLT_PORT_EA_NAME_LENGTH + 1];
    if (PortData->PortNameLength == 0 ||
        (PortData->PortNameLength & 1) ||
        ValueLength < FIELD_OFFSET(FILTER_PORT_DATA, PortName) + (ULONG)PortData->PortNameLength + PortData->ContextSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    PortName.Length = PortName.MaximumLength = PortData->PortNameLength;
    PortName.Buffer = ExAllocatePoolWithTag(PagedPool, PortName.Length, FLT_TAG_PORT);
    if (PortName.Buffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlCopyMemory(PortName.Buffer, PortData->PortName, PortName.Length);

    Status = ObReferenceObjectByName(&PortName,
                                     OBJ_CASE_INSENSITIVE,
                                     NULL,
                                     0,
                                     FltGlobals.ServerPortType,
                                     KernelMode,
                                     NULL,
                                     (PVOID *)&ServerPort);
    ExFreePoolWithTag(PortName.Buffer, FLT_TAG_PORT);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = FltpCheckPortAccess(ServerPort, Irp, Stack);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(ServerPort);
        return Status;
    }

    Filter = ServerPort->Filter;
    if (!ExAcquireRundownProtection(&Filter->Base.RundownRef))
    {
        ObDereferenceObject(ServerPort);
        return STATUS_FLT_DELETING_OBJECT;
    }

    if (InterlockedIncrement(&ServerPort->ConnectionCount) > ServerPort->MaxConnections)
    {
        Status = STATUS_CONNECTION_COUNT_LIMIT;
        goto Fail;
    }

    Port = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Port), FLT_TAG_PORT);
    if (Port == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Fail;
    }

    if (PortData->ContextSize != 0)
    {
        Context = ExAllocatePoolWithTag(PagedPool, PortData->ContextSize, FLT_TAG_PORT);
        if (Context == NULL)
        {
            ExFreePoolWithTag(Port, FLT_TAG_PORT);
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Fail;
        }
        RtlCopyMemory(Context,
                      (PUCHAR)PortData->PortName + PortData->PortNameLength,
                      PortData->ContextSize);
    }

    RtlZeroMemory(Port, sizeof(*Port));
    Port->ReferenceCount = 2;
    Port->ServerPort = ServerPort;
    Port->Filter = Filter;
    ExInitializeRundownProtection(&Port->Rundown);
    KeInitializeSpinLock(&Port->Lock);
    InitializeListHead(&Port->MessageList);
    InitializeListHead(&Port->ReplyList);
    InitializeListHead(&Port->IrpList);
    IoCsqInitializeEx(&Port->Csq,
                      FltpCsqInsertIrp,
                      FltpCsqRemoveIrp,
                      FltpCsqPeekNextIrp,
                      FltpCsqAcquireLock,
                      FltpCsqReleaseLock,
                      FltpCsqCompleteCanceledIrp);
    FltpReferencePointer(&Filter->Base);

    ExAcquireFastMutex(&Filter->PortLock);
    InsertTailList(&Filter->PortList, &Port->FilterLink);
    ExReleaseFastMutex(&Filter->PortLock);
    InterlockedIncrement(&Filter->ClientPortCount);

    Status = ServerPort->ConnectNotify((PFLT_PORT)Port,
                                       ServerPort->Cookie,
                                       Context,
                                       PortData->ContextSize,
                                       &Port->Cookie);
    if (Context != NULL)
    {
        ExFreePoolWithTag(Context, FLT_TAG_PORT);
    }

    if (!NT_SUCCESS(Status))
    {
        InterlockedExchange(&Port->DisconnectNotified, TRUE);
        FltpDisconnectClientPort(Port);
        FltpDereferenceClientPort(Port);
        FltpDereferenceClientPort(Port);
        ExReleaseRundownProtection(&Filter->Base.RundownRef);
        return Status;
    }

    Stack->FileObject->FsContext2 = Port;
    ExReleaseRundownProtection(&Filter->Base.RundownRef);
    return STATUS_SUCCESS;

Fail:
    InterlockedDecrement(&ServerPort->ConnectionCount);
    ExReleaseRundownProtection(&Filter->Base.RundownRef);
    ObDereferenceObject(ServerPort);
    return Status;
}

static
NTSTATUS
FltpPortSendMessage(
    _In_ PFLTP_CLIENT_PORT Port,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    PFLTP_SERVER_PORT ServerPort = Port->ServerPort;
    ULONG InputLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    ULONG Returned = 0;
    NTSTATUS Status;

    if (ServerPort->MessageNotify == NULL)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (Port->Disconnected || !ExAcquireRundownProtection(&Port->Rundown))
    {
        return STATUS_PORT_DISCONNECTED;
    }

    Status = ServerPort->MessageNotify(Port->Cookie,
                                       InputLength ? Stack->Parameters.DeviceIoControl.Type3InputBuffer : NULL,
                                       InputLength,
                                       OutputLength ? Irp->UserBuffer : NULL,
                                       OutputLength,
                                       &Returned);
    ExReleaseRundownProtection(&Port->Rundown);

    Irp->IoStatus.Information = Returned;
    return Status;
}

static
NTSTATUS
FltpPortGetMessage(
    _In_ PFLTP_CLIENT_PORT Port,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    ULONG OutputLength = Stack->Parameters.DeviceIoControl.OutputBufferLength;
    NTSTATUS Status;
    PMDL Mdl;

    if (Irp->UserBuffer == NULL || OutputLength < sizeof(FILTER_MESSAGE_HEADER))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Mdl = IoAllocateMdl(Irp->UserBuffer, OutputLength, FALSE, TRUE, Irp);
    if (Mdl == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    _SEH2_TRY
    {
        MmProbeAndLockPages(Mdl, Irp->RequestorMode, IoWriteAccess);
        Status = STATUS_SUCCESS;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        IoFreeMdl(Mdl);
        Irp->MdlAddress = NULL;
        return Status;
    }

    Status = IoCsqInsertIrpEx(&Port->Csq, Irp, NULL, NULL);
    if (!NT_SUCCESS(Status))
    {
        FltpCompleteIrp(Irp, Status, 0);
        return STATUS_PENDING;
    }

    FltpPumpMessages(Port);
    return STATUS_PENDING;
}

static
NTSTATUS
FltpPortReplyMessage(
    _In_ PFLTP_CLIENT_PORT Port,
    _In_ PIRP Irp,
    _In_ PIO_STACK_LOCATION Stack)
{
    ULONG InputLength = Stack->Parameters.DeviceIoControl.InputBufferLength;
    PVOID Input = Stack->Parameters.DeviceIoControl.Type3InputBuffer;
    PFILTER_REPLY_HEADER Header;
    PFLTP_MESSAGE Message = NULL, Candidate;
    NTSTATUS Status = STATUS_SUCCESS;
    PLIST_ENTRY Link;
    ULONG DataLength;
    KIRQL Irql;

    if (Input == NULL || InputLength < sizeof(FILTER_REPLY_HEADER))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Header = ExAllocatePoolWithTag(NonPagedPoolNx, InputLength, FLT_TAG_PORT);
    if (Header == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    _SEH2_TRY
    {
        if (Irp->RequestorMode != KernelMode)
        {
            ProbeForRead(Input, InputLength, 1);
        }
        RtlCopyMemory(Header, Input, InputLength);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Header, FLT_TAG_PORT);
        return Status;
    }

    DataLength = InputLength - sizeof(FILTER_REPLY_HEADER);

    KeAcquireSpinLock(&Port->Lock, &Irql);
    for (Link = Port->ReplyList.Flink; Link != &Port->ReplyList; Link = Link->Flink)
    {
        Candidate = CONTAINING_RECORD(Link, FLTP_MESSAGE, Link);
        if (Candidate->MessageId == Header->MessageId)
        {
            Message = Candidate;
            break;
        }
    }

    if (Message == NULL)
    {
        Status = Port->Disconnected ? STATUS_PORT_DISCONNECTED : STATUS_FLT_NO_WAITER_FOR_REPLY;
    }
    else
    {
        RemoveEntryList(&Message->Link);
        if (DataLength > Message->ReplyCapacity)
        {
            Status = STATUS_BUFFER_OVERFLOW;
            Message->Status = STATUS_BUFFER_OVERFLOW;
        }
        else
        {
            RtlCopyMemory(Message->Reply, Header + 1, DataLength);
            Message->ReplyLength = DataLength;
            Message->Status = Header->Status;
        }
        Message->State = FLTP_MESSAGE_DONE;
        KeSetEvent(&Message->Event, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&Port->Lock, Irql);

    ExFreePoolWithTag(Header, FLT_TAG_PORT);
    return Status;
}

NTSTATUS
NTAPI
FltpMessageDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack = IoGetCurrentIrpStackLocation(Irp);
    PFLTP_CLIENT_PORT Port = Stack->FileObject ? Stack->FileObject->FsContext2 : NULL;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Information = 0;

    switch (Stack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            Status = FltpConnectPort(Irp, Stack);
            break;

        case IRP_MJ_CLEANUP:
            if (Port != NULL)
            {
                FltpNotifyDisconnect(Port);
                FltpDisconnectClientPort(Port);
            }
            Status = STATUS_SUCCESS;
            break;

        case IRP_MJ_CLOSE:
            if (Port != NULL)
            {
                Stack->FileObject->FsContext2 = NULL;
                FltpDereferenceClientPort(Port);
            }
            Status = STATUS_SUCCESS;
            break;

        case IRP_MJ_DEVICE_CONTROL:
            if (Port == NULL)
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
                break;
            }

            switch (Stack->Parameters.DeviceIoControl.IoControlCode)
            {
                case IOCTL_FILTER_SEND_MESSAGE:
                    Status = FltpPortSendMessage(Port, Irp, Stack);
                    break;

                case IOCTL_FILTER_GET_MESSAGE:
                    Status = FltpPortGetMessage(Port, Irp, Stack);
                    if (Status == STATUS_PENDING)
                    {
                        return STATUS_PENDING;
                    }
                    break;

                case IOCTL_FILTER_REPLY_MESSAGE:
                    Status = FltpPortReplyMessage(Port, Irp, Stack);
                    break;

                default:
                    Status = STATUS_INVALID_DEVICE_REQUEST;
                    break;
            }
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

    Irp->IoStatus.Status = Status;
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Information = 0;
    }
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
