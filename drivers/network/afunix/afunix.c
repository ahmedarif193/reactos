/*
 * PROJECT:     ReactOS AF_UNIX local stream transport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     TDI transport providing local stream sockets
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include "afunix.h"

#include <debug.h>

#define AFUNIX_LOG(fmt, ...) DbgPrint("afunix: " fmt, ##__VA_ARGS__)

static PDEVICE_OBJECT AfunixDeviceObject;
static LIST_ENTRY AfunixEndpointList;
static KSPIN_LOCK AfunixLock;

#define AFUNIX_CTX_OWNER 0
#define AFUNIX_CTX_KIND  1

#define AFUNIX_QUEUE_RECEIVE 1
#define AFUNIX_QUEUE_SEND    2
#define AFUNIX_QUEUE_LISTEN  3
#define AFUNIX_QUEUE_CONNECT 4

static VOID
AfunixReferencePipe(PAFUNIX_PIPE Pipe)
{
    InterlockedIncrement(&Pipe->RefCount);
}

static VOID
AfunixDereferencePipe(PAFUNIX_PIPE Pipe)
{
    if (InterlockedDecrement(&Pipe->RefCount) != 0)
        return;

    if (Pipe->Buffer)
        ExFreePoolWithTag(Pipe->Buffer, TAG_AFUNIX_BUFFER);

    ExFreePoolWithTag(Pipe, TAG_AFUNIX_PIPE);
}

static PAFUNIX_PIPE
AfunixCreatePipe(VOID)
{
    PAFUNIX_PIPE Pipe;

    Pipe = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Pipe), TAG_AFUNIX_PIPE);
    if (!Pipe)
        return NULL;

    RtlZeroMemory(Pipe, sizeof(*Pipe));

    Pipe->Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                         AFUNIX_PIPE_SIZE,
                                         TAG_AFUNIX_BUFFER);
    if (!Pipe->Buffer)
    {
        ExFreePoolWithTag(Pipe, TAG_AFUNIX_PIPE);
        return NULL;
    }

    Pipe->RefCount = 1;
    Pipe->Size = AFUNIX_PIPE_SIZE;
    KeInitializeSpinLock(&Pipe->Lock);
    InitializeListHead(&Pipe->ReceiveQueue);
    InitializeListHead(&Pipe->SendQueue);

    return Pipe;
}

static VOID
AfunixReferenceEndpoint(PAFUNIX_ENDPOINT Endpoint)
{
    InterlockedIncrement(&Endpoint->RefCount);
}

static VOID
AfunixDereferenceEndpoint(PAFUNIX_ENDPOINT Endpoint)
{
    if (InterlockedDecrement(&Endpoint->RefCount) == 0)
        ExFreePoolWithTag(Endpoint, TAG_AFUNIX_ENDPOINT);
}

static VOID
AfunixReferenceConnection(PAFUNIX_CONNECTION Connection)
{
    InterlockedIncrement(&Connection->RefCount);
}

static VOID
AfunixDereferenceConnection(PAFUNIX_CONNECTION Connection)
{
    if (InterlockedDecrement(&Connection->RefCount) != 0)
        return;

    if (Connection->Rx)
        AfunixDereferencePipe(Connection->Rx);

    if (Connection->Tx)
        AfunixDereferencePipe(Connection->Tx);

    if (Connection->Endpoint)
        AfunixDereferenceEndpoint(Connection->Endpoint);

    ExFreePoolWithTag(Connection, TAG_AFUNIX_CONN);
}

static BOOLEAN
AfunixPathEqual(PCCH Left, ULONG LeftLength, PCCH Right, ULONG RightLength)
{
    ULONG Index;

    if (LeftLength != RightLength)
        return FALSE;

    for (Index = 0; Index < LeftLength; Index++)
    {
        CHAR a = Left[Index];
        CHAR b = Right[Index];

        if (a >= 'a' && a <= 'z')
            a = a - 'a' + 'A';
        if (b >= 'a' && b <= 'z')
            b = b - 'a' + 'A';
        if (a == '/')
            a = '\\';
        if (b == '/')
            b = '\\';

        if (a != b)
            return FALSE;
    }

    return TRUE;
}

static ULONG
AfunixPathLength(PCCH Path, ULONG Maximum)
{
    ULONG Index;

    for (Index = 0; Index < Maximum; Index++)
    {
        if (Path[Index] == '\0')
            break;
    }

    return Index;
}

static PAFUNIX_ENDPOINT
AfunixLookupEndpoint(PCCH Path, ULONG PathLength)
{
    PLIST_ENTRY Entry;
    PAFUNIX_ENDPOINT Endpoint;

    for (Entry = AfunixEndpointList.Flink;
         Entry != &AfunixEndpointList;
         Entry = Entry->Flink)
    {
        Endpoint = CONTAINING_RECORD(Entry, AFUNIX_ENDPOINT, ListEntry);

        if (Endpoint->Registered &&
            AfunixPathEqual(Endpoint->Path, Endpoint->PathLength, Path, PathLength))
        {
            return Endpoint;
        }
    }

    return NULL;
}

static PTA_ADDRESS
AfunixGetTaAddress(PTRANSPORT_ADDRESS Address, ULONG Length)
{
    if (!Address || Length < sizeof(LONG) + 2 * sizeof(USHORT))
        return NULL;

    if (Address->TAAddressCount < 1)
        return NULL;

    return &Address->Address[0];
}

static NTSTATUS
AfunixExtractPath(
    PTRANSPORT_ADDRESS Address,
    ULONG Length,
    PCHAR Path,
    PULONG PathLength)
{
    PTA_ADDRESS TaAddress;
    ULONG Available;

    TaAddress = AfunixGetTaAddress(Address, Length);
    if (!TaAddress)
        return STATUS_INVALID_ADDRESS;

    if (TaAddress->AddressType != TDI_ADDRESS_TYPE_UNIX)
        return STATUS_INVALID_ADDRESS;

    Available = TaAddress->AddressLength;
    if (Available > AFUNIX_PATH_LENGTH)
        Available = AFUNIX_PATH_LENGTH;

    RtlZeroMemory(Path, AFUNIX_PATH_LENGTH);
    RtlCopyMemory(Path, TaAddress->Address, Available);

    *PathLength = AfunixPathLength(Path, Available);

    return STATUS_SUCCESS;
}

static VOID
AfunixFillAddress(
    PTRANSPORT_ADDRESS Address,
    PCCH Path,
    ULONG PathLength)
{
    PTA_ADDRESS TaAddress;

    Address->TAAddressCount = 1;
    TaAddress = &Address->Address[0];
    TaAddress->AddressType = TDI_ADDRESS_TYPE_UNIX;
    TaAddress->AddressLength = TDI_ADDRESS_LENGTH_UNIX;

    RtlZeroMemory(TaAddress->Address, TDI_ADDRESS_LENGTH_UNIX);
    if (PathLength > AFUNIX_PATH_LENGTH)
        PathLength = AFUNIX_PATH_LENGTH;
    RtlCopyMemory(TaAddress->Address, Path, PathLength);
}

static VOID
AfunixFillReturnConnectionInfo(
    PTDI_CONNECTION_INFORMATION ReturnInfo,
    PCCH Path,
    ULONG PathLength)
{
    ULONG Required;

    if (!ReturnInfo || !ReturnInfo->RemoteAddress)
        return;

    Required = sizeof(LONG) + 2 * sizeof(USHORT) + TDI_ADDRESS_LENGTH_UNIX;
    if ((ULONG)ReturnInfo->RemoteAddressLength < Required)
        return;

    AfunixFillAddress(ReturnInfo->RemoteAddress, Path, PathLength);
}

static ULONG
AfunixRingWrite(PAFUNIX_PIPE Pipe, PUCHAR Source, ULONG Length)
{
    ULONG Space = Pipe->Size - Pipe->Count;
    ULONG Tail;
    ULONG Chunk;
    ULONG Copied = 0;

    if (Length > Space)
        Length = Space;

    while (Copied < Length)
    {
        Tail = (Pipe->Head + Pipe->Count) % Pipe->Size;
        Chunk = Pipe->Size - Tail;
        if (Chunk > Length - Copied)
            Chunk = Length - Copied;

        RtlCopyMemory(Pipe->Buffer + Tail, Source + Copied, Chunk);
        Pipe->Count += Chunk;
        Copied += Chunk;
    }

    return Copied;
}

static ULONG
AfunixRingRead(PAFUNIX_PIPE Pipe, PUCHAR Target, ULONG Length)
{
    ULONG Chunk;
    ULONG Copied = 0;

    if (Length > Pipe->Count)
        Length = Pipe->Count;

    while (Copied < Length)
    {
        Chunk = Pipe->Size - Pipe->Head;
        if (Chunk > Length - Copied)
            Chunk = Length - Copied;

        RtlCopyMemory(Target + Copied, Pipe->Buffer + Pipe->Head, Chunk);
        Pipe->Head = (Pipe->Head + Chunk) % Pipe->Size;
        Pipe->Count -= Chunk;
        Copied += Chunk;
    }

    return Copied;
}

static VOID
AfunixQueueCompletion(PLIST_ENTRY Completions, PIRP Irp, NTSTATUS Status, ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    InsertTailList(Completions, &Irp->Tail.Overlay.ListEntry);
}

static VOID
AfunixFlushCompletions(PLIST_ENTRY Completions)
{
    PLIST_ENTRY Entry;
    PIRP Irp;

    while (!IsListEmpty(Completions))
    {
        Entry = RemoveHeadList(Completions);
        Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
    }
}

static BOOLEAN
AfunixDequeueIrp(PIRP Irp)
{
    if (IoSetCancelRoutine(Irp, NULL) == NULL)
        return FALSE;

    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    return TRUE;
}

static VOID
AfunixEnqueueIrp(PLIST_ENTRY Queue, PIRP Irp, PVOID Owner, ULONG_PTR Kind, PDRIVER_CANCEL Cancel)
{
    Irp->Tail.Overlay.DriverContext[AFUNIX_CTX_OWNER] = Owner;
    Irp->Tail.Overlay.DriverContext[AFUNIX_CTX_KIND] = (PVOID)Kind;
    InsertTailList(Queue, &Irp->Tail.Overlay.ListEntry);
    IoSetCancelRoutine(Irp, Cancel);
    IoMarkIrpPending(Irp);
}

static VOID
AfunixPipeProcessLocked(PAFUNIX_PIPE Pipe, PLIST_ENTRY Completions)
{
    PLIST_ENTRY Entry;
    PIRP Irp;
    PIO_STACK_LOCATION IrpSp;
    PTDI_REQUEST_KERNEL_SEND SendRequest;
    PTDI_REQUEST_KERNEL_RECEIVE ReceiveRequest;
    PUCHAR Buffer;
    ULONG Transferred;

    while (!IsListEmpty(&Pipe->SendQueue) && Pipe->Count < Pipe->Size)
    {
        Entry = Pipe->SendQueue.Flink;
        Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

        if (!AfunixDequeueIrp(Irp))
            break;

        if (Pipe->ReadClosed)
        {
            AfunixQueueCompletion(Completions, Irp, STATUS_REMOTE_DISCONNECT, 0);
            continue;
        }

        IrpSp = IoGetCurrentIrpStackLocation(Irp);
        SendRequest = (PTDI_REQUEST_KERNEL_SEND)&IrpSp->Parameters;
        Buffer = Irp->MdlAddress ?
                 MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority) : NULL;

        if (!Buffer)
        {
            AfunixQueueCompletion(Completions, Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
            continue;
        }

        Transferred = AfunixRingWrite(Pipe, Buffer, SendRequest->SendLength);
        AfunixQueueCompletion(Completions, Irp, STATUS_SUCCESS, Transferred);
    }

    while (!IsListEmpty(&Pipe->ReceiveQueue) && (Pipe->Count != 0 || Pipe->WriteClosed))
    {
        Entry = Pipe->ReceiveQueue.Flink;
        Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);

        if (!AfunixDequeueIrp(Irp))
            break;

        IrpSp = IoGetCurrentIrpStackLocation(Irp);
        ReceiveRequest = (PTDI_REQUEST_KERNEL_RECEIVE)&IrpSp->Parameters;
        Buffer = Irp->MdlAddress ?
                 MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority) : NULL;

        if (!Buffer)
        {
            AfunixQueueCompletion(Completions, Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
            continue;
        }

        Transferred = AfunixRingRead(Pipe, Buffer, ReceiveRequest->ReceiveLength);
        AfunixQueueCompletion(Completions, Irp, STATUS_SUCCESS, Transferred);
    }
}

static DRIVER_CANCEL AfunixCancelQueuedIrp;
static VOID NTAPI
AfunixCancelQueuedIrp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PAFUNIX_PIPE Pipe = Irp->Tail.Overlay.DriverContext[AFUNIX_CTX_OWNER];
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    KeAcquireSpinLock(&Pipe->Lock, &OldIrql);
    RemoveEntryList(&Irp->Tail.Overlay.ListEntry);
    KeReleaseSpinLock(&Pipe->Lock, OldIrql);

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
}

static DRIVER_CANCEL AfunixCancelListenIrp;
static VOID NTAPI
AfunixCancelListenIrp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PAFUNIX_CONNECTION Connection = Irp->Tail.Overlay.DriverContext[AFUNIX_CTX_OWNER];
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    KeAcquireSpinLock(&AfunixLock, &OldIrql);
    RemoveEntryList(&Connection->ListenEntry);
    InitializeListHead(&Connection->ListenEntry);
    Connection->PendingListen = NULL;
    KeReleaseSpinLock(&AfunixLock, OldIrql);

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
}

static NTSTATUS
AfunixPipeSend(PAFUNIX_PIPE Pipe, PIRP Irp)
{
    KIRQL OldIrql;
    LIST_ENTRY Completions;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PTDI_REQUEST_KERNEL_SEND SendRequest = (PTDI_REQUEST_KERNEL_SEND)&IrpSp->Parameters;
    PUCHAR Buffer;
    ULONG Transferred;
    NTSTATUS Status;

    InitializeListHead(&Completions);

    KeAcquireSpinLock(&Pipe->Lock, &OldIrql);

    if (Pipe->ReadClosed)
    {
        KeReleaseSpinLock(&Pipe->Lock, OldIrql);
        return STATUS_REMOTE_DISCONNECT;
    }

    if (Pipe->Count == Pipe->Size)
    {
        AfunixEnqueueIrp(&Pipe->SendQueue, Irp, Pipe, AFUNIX_QUEUE_SEND, AfunixCancelQueuedIrp);
        KeReleaseSpinLock(&Pipe->Lock, OldIrql);
        return STATUS_PENDING;
    }

    Buffer = Irp->MdlAddress ?
             MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority) : NULL;

    if (!Buffer)
    {
        KeReleaseSpinLock(&Pipe->Lock, OldIrql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Transferred = AfunixRingWrite(Pipe, Buffer, SendRequest->SendLength);
    AfunixPipeProcessLocked(Pipe, &Completions);

    KeReleaseSpinLock(&Pipe->Lock, OldIrql);

    AfunixFlushCompletions(&Completions);

    Irp->IoStatus.Information = Transferred;
    Status = STATUS_SUCCESS;

    return Status;
}

static NTSTATUS
AfunixPipeReceive(PAFUNIX_PIPE Pipe, PIRP Irp)
{
    KIRQL OldIrql;
    LIST_ENTRY Completions;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PTDI_REQUEST_KERNEL_RECEIVE ReceiveRequest = (PTDI_REQUEST_KERNEL_RECEIVE)&IrpSp->Parameters;
    PUCHAR Buffer;
    ULONG Transferred;

    InitializeListHead(&Completions);

    KeAcquireSpinLock(&Pipe->Lock, &OldIrql);

    if (Pipe->Count == 0)
    {
        if (!Pipe->WriteClosed)
        {
            AfunixEnqueueIrp(&Pipe->ReceiveQueue, Irp, Pipe, AFUNIX_QUEUE_RECEIVE, AfunixCancelQueuedIrp);
            KeReleaseSpinLock(&Pipe->Lock, OldIrql);
            return STATUS_PENDING;
        }

        KeReleaseSpinLock(&Pipe->Lock, OldIrql);
        Irp->IoStatus.Information = 0;
        return STATUS_SUCCESS;
    }

    Buffer = Irp->MdlAddress ?
             MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority) : NULL;

    if (!Buffer)
    {
        KeReleaseSpinLock(&Pipe->Lock, OldIrql);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Transferred = AfunixRingRead(Pipe, Buffer, ReceiveRequest->ReceiveLength);
    AfunixPipeProcessLocked(Pipe, &Completions);

    KeReleaseSpinLock(&Pipe->Lock, OldIrql);

    AfunixFlushCompletions(&Completions);

    Irp->IoStatus.Information = Transferred;

    return STATUS_SUCCESS;
}

static VOID
AfunixCloseWriteSide(PAFUNIX_PIPE Pipe)
{
    KIRQL OldIrql;
    LIST_ENTRY Completions;

    if (!Pipe)
        return;

    InitializeListHead(&Completions);

    KeAcquireSpinLock(&Pipe->Lock, &OldIrql);
    Pipe->WriteClosed = TRUE;
    AfunixPipeProcessLocked(Pipe, &Completions);
    KeReleaseSpinLock(&Pipe->Lock, OldIrql);

    AfunixFlushCompletions(&Completions);
}

static VOID
AfunixCloseReadSide(PAFUNIX_PIPE Pipe)
{
    KIRQL OldIrql;
    LIST_ENTRY Completions;
    PLIST_ENTRY Entry;
    PIRP Irp;

    if (!Pipe)
        return;

    InitializeListHead(&Completions);

    KeAcquireSpinLock(&Pipe->Lock, &OldIrql);
    Pipe->ReadClosed = TRUE;

    while (!IsListEmpty(&Pipe->SendQueue))
    {
        Entry = Pipe->SendQueue.Flink;
        Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        if (!AfunixDequeueIrp(Irp))
            break;
        AfunixQueueCompletion(&Completions, Irp, STATUS_REMOTE_DISCONNECT, 0);
    }

    while (!IsListEmpty(&Pipe->ReceiveQueue))
    {
        Entry = Pipe->ReceiveQueue.Flink;
        Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        if (!AfunixDequeueIrp(Irp))
            break;
        AfunixQueueCompletion(&Completions, Irp, STATUS_SUCCESS, 0);
    }

    KeReleaseSpinLock(&Pipe->Lock, OldIrql);

    AfunixFlushCompletions(&Completions);
}


typedef struct _AFUNIX_COMPLETION
{
    PIO_WORKITEM WorkItem;
    PIRP Irp;
    NTSTATUS Status;
    ULONG_PTR Information;
} AFUNIX_COMPLETION, *PAFUNIX_COMPLETION;

static IO_WORKITEM_ROUTINE AfunixCompletionWorker;
static VOID NTAPI
AfunixCompletionWorker(PDEVICE_OBJECT DeviceObject, PVOID Context)
{
    PAFUNIX_COMPLETION Completion = Context;
    PIO_WORKITEM WorkItem = Completion->WorkItem;

    UNREFERENCED_PARAMETER(DeviceObject);

    Completion->Irp->IoStatus.Status = Completion->Status;
    Completion->Irp->IoStatus.Information = Completion->Information;
    IoCompleteRequest(Completion->Irp, IO_NETWORK_INCREMENT);

    ExFreePoolWithTag(Completion, TAG_AFUNIX_REQ);
    IoFreeWorkItem(WorkItem);
}

static VOID
AfunixDeferCompletion(PIRP Irp, NTSTATUS Status, ULONG_PTR Information)
{
    PAFUNIX_COMPLETION Completion;

    Completion = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Completion), TAG_AFUNIX_REQ);

    if (Completion)
    {
        Completion->WorkItem = IoAllocateWorkItem(AfunixDeviceObject);

        if (Completion->WorkItem)
        {
            Completion->Irp = Irp;
            Completion->Status = Status;
            Completion->Information = Information;
            IoQueueWorkItem(Completion->WorkItem,
                            AfunixCompletionWorker,
                            DelayedWorkQueue,
                            Completion);
            return;
        }

        ExFreePoolWithTag(Completion, TAG_AFUNIX_REQ);
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
}

static DRIVER_CANCEL AfunixCancelConnectIrp;
static VOID NTAPI
AfunixCancelConnectIrp(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PAFUNIX_CONNECTION Connection = Irp->Tail.Overlay.DriverContext[AFUNIX_CTX_OWNER];
    PLIST_ENTRY Entry;
    PAFUNIX_CONNREQ Request;
    PAFUNIX_CONNREQ Found = NULL;
    PAFUNIX_ENDPOINT Endpoint;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(DeviceObject);

    IoReleaseCancelSpinLock(Irp->CancelIrql);

    KeAcquireSpinLock(&AfunixLock, &OldIrql);

    Endpoint = (PAFUNIX_ENDPOINT)Irp->Tail.Overlay.DriverContext[2];
    if (Endpoint)
    {
        for (Entry = Endpoint->ConnectQueue.Flink;
             Entry != &Endpoint->ConnectQueue;
             Entry = Entry->Flink)
        {
            Request = CONTAINING_RECORD(Entry, AFUNIX_CONNREQ, ListEntry);
            if (Request->Client == Connection)
            {
                Found = Request;
                RemoveEntryList(Entry);
                Endpoint->ConnectQueueCount--;
                break;
            }
        }
    }

    Connection->PendingConnect = NULL;

    KeReleaseSpinLock(&AfunixLock, OldIrql);

    if (Found)
    {
        AfunixDereferencePipe(Found->ToServer);
        AfunixDereferencePipe(Found->ToClient);
        AfunixDereferenceConnection(Found->Client);
        ExFreePoolWithTag(Found, TAG_AFUNIX_REQ);
    }

    Irp->IoStatus.Status = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
}

static VOID
AfunixPairConnections(
    PAFUNIX_CONNECTION Server,
    PAFUNIX_CONNECTION Client,
    PAFUNIX_PIPE ToServer,
    PAFUNIX_PIPE ToClient)
{
    Server->Rx = ToServer;
    Server->Tx = ToClient;
    Client->Rx = ToClient;
    Client->Tx = ToServer;
    Server->Connected = TRUE;
    Client->Connected = TRUE;
}

static NTSTATUS
AfunixConnect(PAFUNIX_CONNECTION Connection, PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PTDI_REQUEST_KERNEL_CONNECT Request = (PTDI_REQUEST_KERNEL_CONNECT)&IrpSp->Parameters;
    PAFUNIX_ENDPOINT Server;
    PAFUNIX_PIPE ToServer;
    PAFUNIX_PIPE ToClient;
    PAFUNIX_CONNREQ ConnectRequest = NULL;
    PAFUNIX_CONNECTION ServerConnection = NULL;
    PIRP ListenIrp = NULL;
    CHAR Path[AFUNIX_PATH_LENGTH];
    ULONG PathLength = 0;
    KIRQL OldIrql;
    NTSTATUS Status;

    if (Connection->Connected)
        return STATUS_CONNECTION_ACTIVE;

    if (!Request->RequestConnectionInformation)
        return STATUS_INVALID_ADDRESS;

    Status = AfunixExtractPath(Request->RequestConnectionInformation->RemoteAddress,
                               Request->RequestConnectionInformation->RemoteAddressLength,
                               Path,
                               &PathLength);
    if (!NT_SUCCESS(Status))
        return Status;

    if (PathLength == 0)
        return STATUS_INVALID_ADDRESS;

    ToServer = AfunixCreatePipe();
    ToClient = AfunixCreatePipe();
    ConnectRequest = ExAllocatePoolWithTag(NonPagedPool, sizeof(*ConnectRequest), TAG_AFUNIX_REQ);
    if (ConnectRequest)
        RtlZeroMemory(ConnectRequest, sizeof(*ConnectRequest));

    if (!ToServer || !ToClient || !ConnectRequest)
    {
        if (ToServer) AfunixDereferencePipe(ToServer);
        if (ToClient) AfunixDereferencePipe(ToClient);
        if (ConnectRequest) ExFreePoolWithTag(ConnectRequest, TAG_AFUNIX_REQ);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    AfunixReferencePipe(ToServer);
    AfunixReferencePipe(ToClient);

    KeAcquireSpinLock(&AfunixLock, &OldIrql);

    Server = AfunixLookupEndpoint(Path, PathLength);

    if (!Server || !Server->HasListened)
    {
        KeReleaseSpinLock(&AfunixLock, OldIrql);
        AfunixDereferencePipe(ToServer);
        AfunixDereferencePipe(ToServer);
        AfunixDereferencePipe(ToClient);
        AfunixDereferencePipe(ToClient);
        ExFreePoolWithTag(ConnectRequest, TAG_AFUNIX_REQ);
        AFUNIX_LOG("connect refused: endpoint %s listening %u\n",
                   Path, Server ? Server->HasListened : 0);
        return STATUS_CONNECTION_REFUSED;
    }

    RtlCopyMemory(Connection->PeerPath, Path, AFUNIX_PATH_LENGTH);
    Connection->PeerPathLength = PathLength;

    if (!IsListEmpty(&Server->ListenQueue))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&Server->ListenQueue);

        ServerConnection = CONTAINING_RECORD(Entry, AFUNIX_CONNECTION, ListenEntry);
        InitializeListHead(&ServerConnection->ListenEntry);

        ListenIrp = ServerConnection->PendingListen;
        ServerConnection->PendingListen = NULL;

        if (ListenIrp && IoSetCancelRoutine(ListenIrp, NULL) == NULL)
            ListenIrp = NULL;

        AfunixPairConnections(ServerConnection, Connection, ToServer, ToClient);

        RtlCopyMemory(ServerConnection->PeerPath,
                      Connection->Endpoint ? Connection->Endpoint->Path : "",
                      AFUNIX_PATH_LENGTH);
        ServerConnection->PeerPathLength =
            Connection->Endpoint ? Connection->Endpoint->PathLength : 0;

        KeReleaseSpinLock(&AfunixLock, OldIrql);

        ExFreePoolWithTag(ConnectRequest, TAG_AFUNIX_REQ);

        if (ListenIrp)
        {
            PIO_STACK_LOCATION ListenSp = IoGetCurrentIrpStackLocation(ListenIrp);
            PTDI_REQUEST_KERNEL_LISTEN ListenRequest =
                (PTDI_REQUEST_KERNEL_LISTEN)&ListenSp->Parameters;

            AfunixFillReturnConnectionInfo(ListenRequest->ReturnConnectionInformation,
                                           ServerConnection->PeerPath,
                                           ServerConnection->PeerPathLength);

            AfunixDeferCompletion(ListenIrp, STATUS_SUCCESS, 0);
        }

        AfunixFillReturnConnectionInfo(Request->ReturnConnectionInformation,
                                       Path,
                                       PathLength);

        IoMarkIrpPending(Irp);
        AfunixDeferCompletion(Irp, STATUS_SUCCESS, 0);

        return STATUS_PENDING;
    }

    if (Server->ConnectQueueCount >= AFUNIX_MAX_BACKLOG)
    {
        KeReleaseSpinLock(&AfunixLock, OldIrql);
        AfunixDereferencePipe(ToServer);
        AfunixDereferencePipe(ToServer);
        AfunixDereferencePipe(ToClient);
        AfunixDereferencePipe(ToClient);
        ExFreePoolWithTag(ConnectRequest, TAG_AFUNIX_REQ);
        return STATUS_CONNECTION_REFUSED;
    }

    ConnectRequest->ToServer = ToServer;
    ConnectRequest->ToClient = ToClient;
    ConnectRequest->Client = Connection;
    ConnectRequest->PathLength =
        Connection->Endpoint ? Connection->Endpoint->PathLength : 0;
    if (Connection->Endpoint)
    {
        RtlCopyMemory(ConnectRequest->Path,
                      Connection->Endpoint->Path,
                      AFUNIX_PATH_LENGTH);
    }

    AfunixReferenceConnection(Connection);

    Connection->Rx = ToClient;
    Connection->Tx = ToServer;

    InsertTailList(&Server->ConnectQueue, &ConnectRequest->ListEntry);
    Server->ConnectQueueCount++;

    Connection->PendingConnect = Irp;
    Irp->Tail.Overlay.DriverContext[AFUNIX_CTX_OWNER] = Connection;
    Irp->Tail.Overlay.DriverContext[AFUNIX_CTX_KIND] = (PVOID)AFUNIX_QUEUE_CONNECT;
    Irp->Tail.Overlay.DriverContext[2] = Server;
    IoSetCancelRoutine(Irp, AfunixCancelConnectIrp);
    IoMarkIrpPending(Irp);

    KeReleaseSpinLock(&AfunixLock, OldIrql);

    return STATUS_PENDING;
}

static NTSTATUS
AfunixListen(PAFUNIX_CONNECTION Connection, PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PTDI_REQUEST_KERNEL_LISTEN Request = (PTDI_REQUEST_KERNEL_LISTEN)&IrpSp->Parameters;
    PAFUNIX_ENDPOINT Endpoint = Connection->Endpoint;
    PAFUNIX_CONNREQ ConnectRequest = NULL;
    PAFUNIX_CONNECTION Client = NULL;
    PIRP ConnectIrp = NULL;
    KIRQL OldIrql;

    if (!Endpoint)
        return STATUS_INVALID_PARAMETER;

    if (Connection->Connected)
        return STATUS_CONNECTION_ACTIVE;

    KeAcquireSpinLock(&AfunixLock, &OldIrql);

    Endpoint->HasListened = TRUE;

    if (!IsListEmpty(&Endpoint->ConnectQueue))
    {
        PLIST_ENTRY Entry = RemoveHeadList(&Endpoint->ConnectQueue);

        Endpoint->ConnectQueueCount--;
        ConnectRequest = CONTAINING_RECORD(Entry, AFUNIX_CONNREQ, ListEntry);
        Client = ConnectRequest->Client;

        ConnectIrp = Client->PendingConnect;
        Client->PendingConnect = NULL;

        if (ConnectIrp && IoSetCancelRoutine(ConnectIrp, NULL) == NULL)
            ConnectIrp = NULL;

        Connection->Rx = ConnectRequest->ToServer;
        Connection->Tx = ConnectRequest->ToClient;
        Connection->Connected = TRUE;
        Client->Connected = TRUE;

        RtlCopyMemory(Connection->PeerPath, ConnectRequest->Path, AFUNIX_PATH_LENGTH);
        Connection->PeerPathLength = ConnectRequest->PathLength;

        KeReleaseSpinLock(&AfunixLock, OldIrql);

        AfunixFillReturnConnectionInfo(Request->ReturnConnectionInformation,
                                       ConnectRequest->Path,
                                       ConnectRequest->PathLength);

        if (ConnectIrp)
        {
            PIO_STACK_LOCATION ConnectSp = IoGetCurrentIrpStackLocation(ConnectIrp);
            PTDI_REQUEST_KERNEL_CONNECT ConnectKernel =
                (PTDI_REQUEST_KERNEL_CONNECT)&ConnectSp->Parameters;

            AfunixFillReturnConnectionInfo(ConnectKernel->ReturnConnectionInformation,
                                           Endpoint->Path,
                                           Endpoint->PathLength);

            AfunixDeferCompletion(ConnectIrp, STATUS_SUCCESS, 0);
        }

        AfunixDereferenceConnection(Client);
        ExFreePoolWithTag(ConnectRequest, TAG_AFUNIX_REQ);

        IoMarkIrpPending(Irp);
        AfunixDeferCompletion(Irp, STATUS_SUCCESS, 0);

        return STATUS_PENDING;
    }

    Connection->PendingListen = Irp;
    Irp->Tail.Overlay.DriverContext[AFUNIX_CTX_OWNER] = Connection;
    Irp->Tail.Overlay.DriverContext[AFUNIX_CTX_KIND] = (PVOID)AFUNIX_QUEUE_LISTEN;
    InsertTailList(&Endpoint->ListenQueue, &Connection->ListenEntry);
    IoSetCancelRoutine(Irp, AfunixCancelListenIrp);
    IoMarkIrpPending(Irp);

    KeReleaseSpinLock(&AfunixLock, OldIrql);

    return STATUS_PENDING;
}

static VOID
AfunixTearDownConnection(PAFUNIX_CONNECTION Connection)
{
    KIRQL OldIrql;
    PIRP ListenIrp = NULL;

    KeAcquireSpinLock(&AfunixLock, &OldIrql);

    if (Connection->PendingListen)
    {
        ListenIrp = Connection->PendingListen;
        Connection->PendingListen = NULL;

        if (IoSetCancelRoutine(ListenIrp, NULL) == NULL)
        {
            ListenIrp = NULL;
        }
        else
        {
            RemoveEntryList(&Connection->ListenEntry);
            InitializeListHead(&Connection->ListenEntry);
        }
    }

    Connection->Disconnected = TRUE;

    KeReleaseSpinLock(&AfunixLock, OldIrql);

    if (ListenIrp)
    {
        ListenIrp->IoStatus.Status = STATUS_CANCELLED;
        ListenIrp->IoStatus.Information = 0;
        IoCompleteRequest(ListenIrp, IO_NETWORK_INCREMENT);
    }

    AfunixCloseWriteSide(Connection->Tx);
    AfunixCloseReadSide(Connection->Rx);
}


static VOID
AfunixCreateMarkerFile(PAFUNIX_ENDPOINT Endpoint)
{
    OBJECT_ATTRIBUTES Attributes;
    IO_STATUS_BLOCK IoStatus;
    ANSI_STRING AnsiPath;
    UNICODE_STRING Relative;
    UNICODE_STRING Full;
    NTSTATUS Status;
    USHORT Prefix;

    if (Endpoint->PathLength == 0)
        return;

    AnsiPath.Buffer = Endpoint->Path;
    AnsiPath.Length = (USHORT)Endpoint->PathLength;
    AnsiPath.MaximumLength = (USHORT)Endpoint->PathLength;

    Status = RtlAnsiStringToUnicodeString(&Relative, &AnsiPath, TRUE);
    if (!NT_SUCCESS(Status))
        return;

    Prefix = (USHORT)(4 * sizeof(WCHAR));
    Full.Length = 0;
    Full.MaximumLength = Relative.Length + Prefix + sizeof(UNICODE_NULL);
    Full.Buffer = ExAllocatePoolWithTag(PagedPool, Full.MaximumLength, TAG_AFUNIX_ENDPOINT);

    if (!Full.Buffer)
    {
        RtlFreeUnicodeString(&Relative);
        return;
    }

    RtlAppendUnicodeToString(&Full, L"\\??\\");
    RtlAppendUnicodeStringToString(&Full, &Relative);
    RtlFreeUnicodeString(&Relative);

    InitializeObjectAttributes(&Attributes,
                               &Full,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwCreateFile(&Endpoint->MarkerFile,
                          GENERIC_WRITE | DELETE | SYNCHRONIZE,
                          &Attributes,
                          &IoStatus,
                          NULL,
                          FILE_ATTRIBUTE_NORMAL,
                          FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                          FILE_OVERWRITE_IF,
                          FILE_NON_DIRECTORY_FILE | FILE_SYNCHRONOUS_IO_NONALERT,
                          NULL,
                          0);

    if (!NT_SUCCESS(Status))
    {
        AFUNIX_LOG("marker create failed %wZ status %08lx\n", &Full, Status);
        Endpoint->MarkerFile = NULL;
        ExFreePoolWithTag(Full.Buffer, TAG_AFUNIX_ENDPOINT);
        return;
    }

    Endpoint->MarkerPath = Full;
}

static VOID
AfunixDeleteMarkerFile(PAFUNIX_ENDPOINT Endpoint)
{
    if (Endpoint->MarkerFile)
    {
        ZwClose(Endpoint->MarkerFile);
        Endpoint->MarkerFile = NULL;
    }

    if (Endpoint->MarkerPath.Buffer)
    {
        ExFreePoolWithTag(Endpoint->MarkerPath.Buffer, TAG_AFUNIX_ENDPOINT);
        Endpoint->MarkerPath.Buffer = NULL;
    }
}

static NTSTATUS
AfunixCreateAddress(PAFUNIX_FCB Fcb, PFILE_FULL_EA_INFORMATION EaInfo)
{
    PAFUNIX_ENDPOINT Endpoint;
    PTRANSPORT_ADDRESS Address;
    CHAR Path[AFUNIX_PATH_LENGTH];
    ULONG PathLength = 0;
    KIRQL OldIrql;
    NTSTATUS Status;

    Address = (PTRANSPORT_ADDRESS)(EaInfo->EaName + EaInfo->EaNameLength + 1);

    Status = AfunixExtractPath(Address, AFUNIX_PATH_LENGTH + 32, Path, &PathLength);
    if (!NT_SUCCESS(Status))
        return Status;

    Endpoint = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Endpoint), TAG_AFUNIX_ENDPOINT);
    if (!Endpoint)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Endpoint, sizeof(*Endpoint));

    Endpoint->RefCount = 1;
    Endpoint->PathLength = PathLength;
    RtlCopyMemory(Endpoint->Path, Path, AFUNIX_PATH_LENGTH);
    InitializeListHead(&Endpoint->ListenQueue);
    InitializeListHead(&Endpoint->ConnectQueue);
    InitializeListHead(&Endpoint->ListEntry);

    if (PathLength != 0)
    {
        KeAcquireSpinLock(&AfunixLock, &OldIrql);

        if (AfunixLookupEndpoint(Path, PathLength))
        {
            KeReleaseSpinLock(&AfunixLock, OldIrql);
            ExFreePoolWithTag(Endpoint, TAG_AFUNIX_ENDPOINT);
            return STATUS_ADDRESS_ALREADY_EXISTS;
        }

        Endpoint->Registered = TRUE;
        InsertTailList(&AfunixEndpointList, &Endpoint->ListEntry);

        KeReleaseSpinLock(&AfunixLock, OldIrql);

        AfunixCreateMarkerFile(Endpoint);
    }

    Fcb->Type = AfunixFileAddress;
    Fcb->Endpoint = Endpoint;

    return STATUS_SUCCESS;
}

static NTSTATUS
AfunixCreateConnection(PAFUNIX_FCB Fcb)
{
    PAFUNIX_CONNECTION Connection;

    Connection = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Connection), TAG_AFUNIX_CONN);
    if (!Connection)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Connection, sizeof(*Connection));

    Connection->RefCount = 1;
    InitializeListHead(&Connection->ListenEntry);

    Fcb->Type = AfunixFileConnection;
    Fcb->Connection = Connection;

    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
AfunixCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PFILE_FULL_EA_INFORMATION EaInfo = Irp->AssociatedIrp.SystemBuffer;
    PAFUNIX_FCB Fcb;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(DeviceObject);

    Fcb = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Fcb), TAG_AFUNIX_ENDPOINT);
    if (!Fcb)
    {
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Fcb, sizeof(*Fcb));
    Fcb->Type = AfunixFileControl;

    if (EaInfo)
    {
        if (EaInfo->EaNameLength == TDI_TRANSPORT_ADDRESS_LENGTH &&
            RtlCompareMemory(EaInfo->EaName,
                             TdiTransportAddress,
                             TDI_TRANSPORT_ADDRESS_LENGTH) == TDI_TRANSPORT_ADDRESS_LENGTH)
        {
            Status = AfunixCreateAddress(Fcb, EaInfo);
        }
        else if (EaInfo->EaNameLength == TDI_CONNECTION_CONTEXT_LENGTH &&
                 RtlCompareMemory(EaInfo->EaName,
                                  TdiConnectionContext,
                                  TDI_CONNECTION_CONTEXT_LENGTH) == TDI_CONNECTION_CONTEXT_LENGTH)
        {
            Status = AfunixCreateConnection(Fcb);
        }
    }

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Fcb, TAG_AFUNIX_ENDPOINT);
    }
    else
    {
        IrpSp->FileObject->FsContext = Fcb;
        IrpSp->FileObject->FsContext2 = (PVOID)(ULONG_PTR)Fcb->Type;
    }

    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return Status;
}

static VOID
AfunixUnregisterEndpoint(PAFUNIX_ENDPOINT Endpoint)
{
    LIST_ENTRY Orphans;
    PLIST_ENTRY Entry;
    PAFUNIX_CONNREQ Request;
    KIRQL OldIrql;

    InitializeListHead(&Orphans);

    KeAcquireSpinLock(&AfunixLock, &OldIrql);

    if (Endpoint->Registered)
    {
        RemoveEntryList(&Endpoint->ListEntry);
        InitializeListHead(&Endpoint->ListEntry);
        Endpoint->Registered = FALSE;
    }

    while (!IsListEmpty(&Endpoint->ConnectQueue))
    {
        Entry = RemoveHeadList(&Endpoint->ConnectQueue);
        Endpoint->ConnectQueueCount--;
        InsertTailList(&Orphans, Entry);
    }

    KeReleaseSpinLock(&AfunixLock, OldIrql);

    AfunixDeleteMarkerFile(Endpoint);

    while (!IsListEmpty(&Orphans))
    {
        PIRP ConnectIrp;

        Entry = RemoveHeadList(&Orphans);
        Request = CONTAINING_RECORD(Entry, AFUNIX_CONNREQ, ListEntry);

        KeAcquireSpinLock(&AfunixLock, &OldIrql);
        ConnectIrp = Request->Client->PendingConnect;
        Request->Client->PendingConnect = NULL;
        if (ConnectIrp && IoSetCancelRoutine(ConnectIrp, NULL) == NULL)
            ConnectIrp = NULL;
        KeReleaseSpinLock(&AfunixLock, OldIrql);

        if (ConnectIrp)
        {
            ConnectIrp->IoStatus.Status = STATUS_CONNECTION_REFUSED;
            ConnectIrp->IoStatus.Information = 0;
            IoCompleteRequest(ConnectIrp, IO_NETWORK_INCREMENT);
        }

        AfunixDereferencePipe(Request->ToServer);
        AfunixDereferencePipe(Request->ToClient);
        AfunixDereferenceConnection(Request->Client);
        ExFreePoolWithTag(Request, TAG_AFUNIX_REQ);
    }
}

static NTSTATUS NTAPI
AfunixCleanup(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PAFUNIX_FCB Fcb = IrpSp->FileObject->FsContext;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Fcb)
    {
        if (Fcb->Type == AfunixFileAddress && Fcb->Endpoint)
            AfunixUnregisterEndpoint(Fcb->Endpoint);
        else if (Fcb->Type == AfunixFileConnection && Fcb->Connection)
            AfunixTearDownConnection(Fcb->Connection);
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI
AfunixClose(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PAFUNIX_FCB Fcb = IrpSp->FileObject->FsContext;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (Fcb)
    {
        if (Fcb->Type == AfunixFileAddress && Fcb->Endpoint)
            AfunixDereferenceEndpoint(Fcb->Endpoint);
        else if (Fcb->Type == AfunixFileConnection && Fcb->Connection)
            AfunixDereferenceConnection(Fcb->Connection);

        IrpSp->FileObject->FsContext = NULL;
        ExFreePoolWithTag(Fcb, TAG_AFUNIX_ENDPOINT);
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

static NTSTATUS
AfunixAssociateAddress(PAFUNIX_CONNECTION Connection, PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PTDI_REQUEST_KERNEL_ASSOCIATE Request = (PTDI_REQUEST_KERNEL_ASSOCIATE)&IrpSp->Parameters;
    PFILE_OBJECT AddressFile = NULL;
    PAFUNIX_FCB AddressFcb;
    NTSTATUS Status;

    if (Connection->Endpoint)
        return STATUS_INVALID_PARAMETER;

    Status = ObReferenceObjectByHandle(Request->AddressHandle,
                                       0,
                                       *IoFileObjectType,
                                       KernelMode,
                                       (PVOID *)&AddressFile,
                                       NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    AddressFcb = AddressFile->FsContext;

    if (!AddressFcb || AddressFcb->Type != AfunixFileAddress || !AddressFcb->Endpoint)
    {
        ObDereferenceObject(AddressFile);
        return STATUS_INVALID_PARAMETER;
    }

    AfunixReferenceEndpoint(AddressFcb->Endpoint);
    Connection->Endpoint = AddressFcb->Endpoint;

    ObDereferenceObject(AddressFile);

    return STATUS_SUCCESS;
}

static NTSTATUS
AfunixQueryInformation(PAFUNIX_FCB Fcb, PIRP Irp, PULONG_PTR Information)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PTDI_REQUEST_KERNEL_QUERY_INFORMATION Request =
        (PTDI_REQUEST_KERNEL_QUERY_INFORMATION)&IrpSp->Parameters;
    PVOID Buffer;
    ULONG Length;

    *Information = 0;

    if (!Irp->MdlAddress)
        return STATUS_INVALID_PARAMETER;

    Buffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    Length = MmGetMdlByteCount(Irp->MdlAddress);

    switch (Request->QueryType)
    {
        case TDI_QUERY_MAX_DATAGRAM_INFO:
        {
            PTDI_MAX_DATAGRAM_INFO Info = Buffer;

            if (Length < sizeof(*Info))
                return STATUS_BUFFER_TOO_SMALL;

            Info->MaxDatagramSize = AFUNIX_PIPE_SIZE;
            *Information = sizeof(*Info);
            return STATUS_SUCCESS;
        }

        case TDI_QUERY_ADDRESS_INFO:
        {
            PTDI_ADDRESS_INFO Info = Buffer;
            ULONG Required = FIELD_OFFSET(TDI_ADDRESS_INFO, Address) +
                             sizeof(LONG) + 2 * sizeof(USHORT) + TDI_ADDRESS_LENGTH_UNIX;
            PAFUNIX_ENDPOINT Endpoint = NULL;

            if (Length < Required)
                return STATUS_BUFFER_TOO_SMALL;

            if (Fcb->Type == AfunixFileAddress)
                Endpoint = Fcb->Endpoint;
            else if (Fcb->Type == AfunixFileConnection && Fcb->Connection)
                Endpoint = Fcb->Connection->Endpoint;

            RtlZeroMemory(Info, Required);
            Info->ActivityCount = 1;
            AfunixFillAddress(&Info->Address,
                              Endpoint ? Endpoint->Path : "",
                              Endpoint ? Endpoint->PathLength : 0);

            *Information = Required;
            return STATUS_SUCCESS;
        }

        case TDI_QUERY_CONNECTION_INFO:
        {
            PTDI_CONNECTION_INFO Info = Buffer;

            if (Length < sizeof(*Info))
                return STATUS_BUFFER_TOO_SMALL;

            RtlZeroMemory(Info, sizeof(*Info));
            *Information = sizeof(*Info);
            return STATUS_SUCCESS;
        }

        default:
            return STATUS_NOT_IMPLEMENTED;
    }
}

static NTSTATUS NTAPI
AfunixInternalDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation(Irp);
    PAFUNIX_FCB Fcb = IrpSp->FileObject->FsContext;
    PAFUNIX_CONNECTION Connection = NULL;
    NTSTATUS Status;
    ULONG_PTR Information = 0;

    UNREFERENCED_PARAMETER(DeviceObject);

    if (!Fcb)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto complete;
    }

    if (Fcb->Type == AfunixFileConnection)
        Connection = Fcb->Connection;

    switch (IrpSp->MinorFunction)
    {
        case TDI_ASSOCIATE_ADDRESS:
            Status = Connection ? AfunixAssociateAddress(Connection, Irp)
                                : STATUS_INVALID_DEVICE_REQUEST;
            break;

        case TDI_DISASSOCIATE_ADDRESS:
            Status = STATUS_SUCCESS;
            break;

        case TDI_CONNECT:
            Status = Connection ? AfunixConnect(Connection, Irp)
                                : STATUS_INVALID_DEVICE_REQUEST;
            break;

        case TDI_LISTEN:
            Status = Connection ? AfunixListen(Connection, Irp)
                                : STATUS_INVALID_DEVICE_REQUEST;
            break;

        case TDI_ACCEPT:
            Status = STATUS_SUCCESS;
            break;

        case TDI_DISCONNECT:
            if (Connection)
            {
                AfunixTearDownConnection(Connection);
                Status = STATUS_SUCCESS;
            }
            else
            {
                Status = STATUS_INVALID_DEVICE_REQUEST;
            }
            break;

        case TDI_SEND:
            if (Connection && Connection->Tx)
            {
                Status = AfunixPipeSend(Connection->Tx, Irp);
                Information = Irp->IoStatus.Information;
            }
            else
            {
                AFUNIX_LOG("send on unconnected fcb %p conn %p tx %p\n",
                           Fcb, Connection, Connection ? Connection->Tx : NULL);
                Status = STATUS_INVALID_CONNECTION;
            }
            break;

        case TDI_RECEIVE:
            if (Connection && Connection->Rx)
            {
                Status = AfunixPipeReceive(Connection->Rx, Irp);
                Information = Irp->IoStatus.Information;
            }
            else
            {
                AFUNIX_LOG("recv on unconnected fcb %p conn %p rx %p\n",
                           Fcb, Connection, Connection ? Connection->Rx : NULL);
                Status = STATUS_INVALID_CONNECTION;
            }
            break;

        case TDI_QUERY_INFORMATION:
            Status = AfunixQueryInformation(Fcb, Irp, &Information);
            break;

        case TDI_SET_INFORMATION:
        case TDI_SET_EVENT_HANDLER:
        case TDI_ACTION:
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_NOT_IMPLEMENTED;
            break;
    }

    if (Status == STATUS_PENDING)
        return Status;

complete:
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);

    return Status;
}

static NTSTATUS NTAPI
AfunixDeviceControl(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_NOT_IMPLEMENTED;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_NOT_IMPLEMENTED;
}

static NTSTATUS NTAPI
AfunixDispatchSuccess(PDEVICE_OBJECT DeviceObject, PIRP Irp)
{
    UNREFERENCED_PARAMETER(DeviceObject);

    Irp->IoStatus.Status = STATUS_SUCCESS;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);

    return STATUS_SUCCESS;
}

static VOID NTAPI
AfunixUnload(PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (AfunixDeviceObject)
        IoDeleteDevice(AfunixDeviceObject);
}

NTSTATUS NTAPI
DriverEntry(PDRIVER_OBJECT DriverObject, PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(AFUNIX_DEVICE_NAME);
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);

    InitializeListHead(&AfunixEndpointList);
    KeInitializeSpinLock(&AfunixLock);

    Status = IoCreateDevice(DriverObject,
                            0,
                            &DeviceName,
                            FILE_DEVICE_TRANSPORT,
                            0,
                            FALSE,
                            &AfunixDeviceObject);
    if (!NT_SUCCESS(Status))
        return Status;

    DriverObject->MajorFunction[IRP_MJ_CREATE] = AfunixCreate;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = AfunixCleanup;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = AfunixClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = AfunixDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_INTERNAL_DEVICE_CONTROL] = AfunixInternalDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_READ] = AfunixDispatchSuccess;
    DriverObject->MajorFunction[IRP_MJ_WRITE] = AfunixDispatchSuccess;
    DriverObject->DriverUnload = AfunixUnload;

    AfunixDeviceObject->Flags |= DO_DIRECT_IO;
    AfunixDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}
