/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Advanced Local Procedure Call kernel infrastructure
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <ntoskrnl.h>

POBJECT_TYPE AlpcPortObjectType;
POBJECT_TYPE LpcPortObjectType;
POBJECT_TYPE LpcWaitablePortObjectType;
KGUARDED_MUTEX AlpcpLock;
ULONG AlpcpNextMessageId = 1;
ULONG AlpcpNextCallbackId = 1;
LIST_ENTRY AlpcpPortList;
static LIST_ENTRY AlpcpDeferredFreeList;
static LIST_ENTRY AlpcpDeferredCommunicationList;

static PAGED_LOOKASIDE_LIST AlpcpSmallMessageLookaside;

static
VOID
AlpcpFreeMessageContents(
    _In_ PKALPC_MESSAGE Message);

static
VOID
AlpcpFreeCommunicationInfoContents(
    _In_ PALPC_COMMUNICATION_INFO Info);

static GENERIC_MAPPING AlpcpPortMapping =
{
    READ_CONTROL | PORT_CONNECT,
    DELETE | PORT_CONNECT,
    0,
    PORT_ALL_ACCESS
};

#define ALPC_SMALL_MESSAGE_ALLOCATION \
    (FIELD_OFFSET(KALPC_MESSAGE, PortMessage) + ALPC_SMALL_MESSAGE_SIZE)

CODE_SEG("INIT")
BOOLEAN
NTAPI
LpcInitSystem(VOID)
{
    OBJECT_TYPE_INITIALIZER ObjectTypeInitializer;
    UNICODE_STRING Name;

    KeInitializeGuardedMutex(&AlpcpLock);
    InitializeListHead(&AlpcpPortList);
    InitializeListHead(&AlpcpDeferredFreeList);
    InitializeListHead(&AlpcpDeferredCommunicationList);

    RtlZeroMemory(&ObjectTypeInitializer, sizeof(ObjectTypeInitializer));
    RtlInitUnicodeString(&Name, L"AlpcPort");
    ObjectTypeInitializer.Length = sizeof(ObjectTypeInitializer);
    ObjectTypeInitializer.DefaultNonPagedPoolCharge = sizeof(ALPC_PORT);
    ObjectTypeInitializer.DefaultPagedPoolCharge = 0;
    ObjectTypeInitializer.GenericMapping = AlpcpPortMapping;
    ObjectTypeInitializer.PoolType = NonPagedPool;
    ObjectTypeInitializer.UseDefaultObject = FALSE;
    ObjectTypeInitializer.CloseProcedure = AlpcpClosePort;
    ObjectTypeInitializer.DeleteProcedure = AlpcpDeletePort;
    ObjectTypeInitializer.ValidAccessMask = PORT_ALL_ACCESS;
    ObjectTypeInitializer.InvalidAttributes = OBJ_VALID_ATTRIBUTES & ~OBJ_CASE_INSENSITIVE;
    ObCreateObjectTypeEx(&Name, &ObjectTypeInitializer, NULL, FIELD_OFFSET(ALPC_PORT, WaitEvent), &AlpcPortObjectType);
    if (!AlpcPortObjectType) return FALSE;

    LpcPortObjectType = AlpcPortObjectType;
    LpcWaitablePortObjectType = AlpcPortObjectType;

    ExInitializePagedLookasideList(&AlpcpSmallMessageLookaside, NULL, NULL, 0, ALPC_SMALL_MESSAGE_ALLOCATION, 'McpA', 32);
    return TRUE;
}

PKALPC_MESSAGE
NTAPI
AlpcpAllocateMessage(
    _In_ ULONG Capacity,
    _In_ PALPC_PORT Port)
{
    PKALPC_MESSAGE Message;
    ULONG Allocated;

    UNREFERENCED_PARAMETER(Port);

    if (Capacity < sizeof(PORT_MESSAGE)) Capacity = sizeof(PORT_MESSAGE);

    if (Capacity <= ALPC_SMALL_MESSAGE_SIZE)
    {
        Message = ExAllocateFromPagedLookasideList(&AlpcpSmallMessageLookaside);
        Allocated = ALPC_SMALL_MESSAGE_SIZE;
    }
    else
    {
        Message = ExAllocatePoolWithTag(PagedPool, FIELD_OFFSET(KALPC_MESSAGE, PortMessage) + Capacity, 'McpA');
        Allocated = Capacity;
    }
    if (!Message) return NULL;

    RtlZeroMemory(Message, FIELD_OFFSET(KALPC_MESSAGE, PortMessage) + sizeof(PORT_MESSAGE));
    InitializeListHead(&Message->Entry);
    InitializeListHead(&Message->CanceledEntry);
    InitializeListHead(&Message->DeferredFreeEntry);
    Message->AllocatedLength = Allocated;
    return Message;
}

VOID
NTAPI
AlpcpReleaseLock(VOID)
{
    LIST_ENTRY DeferredList;
    LIST_ENTRY DeferredCommunicationList;
    PLIST_ENTRY Entry;
    PKALPC_MESSAGE Message;
    PALPC_COMMUNICATION_INFO Info;

    ASSERT(AlpcpLock.Owner == KeGetCurrentThread());
    InitializeListHead(&DeferredList);
    InitializeListHead(&DeferredCommunicationList);
    while (!IsListEmpty(&AlpcpDeferredFreeList))
    {
        Entry = RemoveHeadList(&AlpcpDeferredFreeList);
        InsertTailList(&DeferredList, Entry);
    }
    while (!IsListEmpty(&AlpcpDeferredCommunicationList))
    {
        Entry = RemoveHeadList(&AlpcpDeferredCommunicationList);
        InsertTailList(&DeferredCommunicationList, Entry);
    }
    KeReleaseGuardedMutex(&AlpcpLock);

    while (!IsListEmpty(&DeferredList))
    {
        Entry = RemoveHeadList(&DeferredList);
        InitializeListHead(Entry);
        Message = CONTAINING_RECORD(Entry, KALPC_MESSAGE, DeferredFreeEntry);
        AlpcpFreeMessageContents(Message);
    }
    while (!IsListEmpty(&DeferredCommunicationList))
    {
        Entry = RemoveHeadList(&DeferredCommunicationList);
        InitializeListHead(Entry);
        Info = CONTAINING_RECORD(Entry, ALPC_COMMUNICATION_INFO, CommunicationList);
        AlpcpFreeCommunicationInfoContents(Info);
    }
}

VOID
NTAPI
AlpcpSetMessageSenderPort(
    _Inout_ PKALPC_MESSAGE Message,
    _In_ PALPC_PORT Port)
{
    ASSERT(!Message->SenderPort);
    ASSERT(!Message->SenderProcess);
    ASSERT(Port->OwnerProcess);

    Message->SenderPort = Port;
    Message->SenderProcess = Port->OwnerProcess;
    ObReferenceObject(Message->SenderProcess);
}

VOID
NTAPI
AlpcpFreeMessage(
    _In_ PKALPC_MESSAGE Message)
{
    ASSERT(IsListEmpty(&Message->Entry));
    ASSERT(IsListEmpty(&Message->CanceledEntry));
    ASSERT(IsListEmpty(&Message->DeferredFreeEntry));

    if (AlpcpLock.Owner == KeGetCurrentThread())
    {
        InsertTailList(&AlpcpDeferredFreeList, &Message->DeferredFreeEntry);
        return;
    }

    AlpcpFreeMessageContents(Message);
}

static
VOID
AlpcpFreeMessageContents(
    _In_ PKALPC_MESSAGE Message)
{
    PVOID Object;
    PEPROCESS SenderProcess;

    ASSERT(IsListEmpty(&Message->Entry));
    ASSERT(IsListEmpty(&Message->CanceledEntry));
    ASSERT(IsListEmpty(&Message->DeferredFreeEntry));

    AlpcpReleaseMessageAttributes(Message);

    Object = Message->Connection.ClientPort;
    Message->Connection.ClientPort = NULL;
    if (Object) ObDereferenceObject(Object);

    Object = Message->Connection.SectionToMap;
    Message->Connection.SectionToMap = NULL;
    if (Object) ObDereferenceObject(Object);

    SenderProcess = Message->SenderProcess;
    Message->SenderProcess = NULL;
    Message->SenderPort = NULL;
    if (SenderProcess) ObDereferenceObject(SenderProcess);

    if (Message->AllocatedLength <= ALPC_SMALL_MESSAGE_SIZE)
    {
        ExFreeToPagedLookasideList(&AlpcpSmallMessageLookaside, Message);
    }
    else
    {
        ExFreePoolWithTag(Message, 'McpA');
    }
}

VOID
NTAPI
AlpcpCopyMessage(
    _Out_ PPORT_MESSAGE Destination,
    _In_ PPORT_MESSAGE Origin,
    _In_ PVOID Data,
    _In_ ULONG MessageType,
    _In_opt_ PCLIENT_ID ClientId)
{
    Destination->u1.Length = Origin->u1.Length;
    Destination->u2.s2.Type = MessageType ? (CSHORT)(MessageType & 0xFFFF) : Origin->u2.s2.Type;
    Destination->u2.s2.DataInfoOffset = Origin->u2.s2.DataInfoOffset;
    if (ClientId)
    {
        Destination->ClientId = *ClientId;
    }
    else
    {
        Destination->ClientId = Origin->ClientId;
    }
    Destination->MessageId = Origin->MessageId;
    Destination->ClientViewSize = Origin->ClientViewSize;
    if ((USHORT)Origin->u1.s1.DataLength)
    {
        RtlCopyMemory(Destination + 1, Data, (USHORT)Origin->u1.s1.DataLength);
    }
}

NTSTATUS
NTAPI
AlpcpReferencePortByHandle(
    _In_ HANDLE PortHandle,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PALPC_PORT *Port)
{
    return ObReferenceObjectByHandle(PortHandle, DesiredAccess, AlpcPortObjectType, AccessMode, (PVOID*)Port, NULL);
}

NTSTATUS
NTAPI
AlpcpCreatePort(
    _Out_ PALPC_PORT *NewPort,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PALPC_PORT_ATTRIBUTES PortAttributes,
    _In_ ULONG Type,
    _In_ ULONG Flags,
    _In_ ULONG MaxConnectionInfoLength,
    _In_ ULONG MaxMessageLength)
{
    NTSTATUS Status;
    PALPC_PORT Port;

    Status = ObCreateObject(AccessMode, AlpcPortObjectType, ObjectAttributes, AccessMode, NULL, sizeof(ALPC_PORT), 0, 0, (PVOID*)&Port);
    if (!NT_SUCCESS(Status)) return Status;

    RtlZeroMemory(Port, sizeof(ALPC_PORT));
    KeInitializeEvent(&Port->WaitEvent, NotificationEvent, FALSE);
    InitializeListHead(&Port->PortListEntry);
    InitializeListHead(&Port->MainQueue);
    InitializeListHead(&Port->PendingQueue);
    InitializeListHead(&Port->CanceledQueue);
    InitializeListHead(&Port->Waiters);
    InitializeListHead(&Port->CommunicationPorts);
    InitializeListHead(&Port->SectionList);
    InitializeListHead(&Port->ViewList);
    InitializeListHead(&Port->SecurityList);
    InitializeListHead(&Port->ReserveList);

    Port->Flags = Type | Flags;
    Port->OwnerProcess = PsGetCurrentProcess();
    ObReferenceObject(Port->OwnerProcess);
    Port->Creator = PsGetCurrentThread()->Cid;
    Port->NextResourceHandle = 1;
    Port->MaxMessageLength = MaxMessageLength;
    Port->MaxConnectionInfoLength = MaxConnectionInfoLength;
    if (PortAttributes)
    {
        Port->PortAttributes = *PortAttributes;
        if (Port->PortAttributes.MaxMessageLength > MaxMessageLength)
            Port->PortAttributes.MaxMessageLength = MaxMessageLength;
        Port->MaxMessageLength = (ULONG)Port->PortAttributes.MaxMessageLength;
        Port->SecurityQos = PortAttributes->SecurityQos;
        if (PortAttributes->Flags & ALPC_PORFLG_WAITABLE_PORT) Port->Flags |= ALPC_PORT_FLAG_WAITABLE;
        if (PortAttributes->Flags & ALPC_PORFLG_ALLOW_LPC_REQUESTS) Port->Flags |= ALPC_PORT_FLAG_ALLOW_LPC;
    }
    else
    {
        Port->PortAttributes.Flags = ALPC_PORFLG_ALLOW_IMPERSONATION;
        Port->PortAttributes.MaxMessageLength = MaxMessageLength;
        Port->PortAttributes.MaxPoolUsage = 0x4000;
        Port->PortAttributes.MaxSectionSize = 0x4000;
        Port->PortAttributes.MaxTotalSectionSize = 0x20000;
        Port->PortAttributes.SecurityQos.Length = sizeof(SECURITY_QUALITY_OF_SERVICE);
        Port->PortAttributes.SecurityQos.ImpersonationLevel = SecurityIdentification;
        Port->PortAttributes.SecurityQos.ContextTrackingMode = SECURITY_DYNAMIC_TRACKING;
        Port->PortAttributes.SecurityQos.EffectiveOnly = FALSE;
        Port->SecurityQos = Port->PortAttributes.SecurityQos;
    }

    AlpcpAcquireLock();
    InsertTailList(&AlpcpPortList, &Port->PortListEntry);
    AlpcpReleaseLock();

    *NewPort = Port;
    return STATUS_SUCCESS;
}

PALPC_PORT
NTAPI
AlpcpGetQueuePort(
    _In_ PALPC_PORT Port,
    _Out_opt_ PVOID *PortContext)
{
    PALPC_COMMUNICATION_INFO Info = Port->CommunicationInfo;

    if (PortContext) *PortContext = NULL;

    switch (AlpcpPortType(Port))
    {
        case ALPC_PORT_TYPE_CONNECTION:
            return Port;

        case ALPC_PORT_TYPE_CLIENT:
            if (!Info || !Info->ServerCommunicationPort || !Info->ConnectionPort) return NULL;
            if (Info->ServerCommunicationPort->Flags & ALPC_PORT_FLAG_CLOSED) return NULL;
            if (PortContext) *PortContext = Info->ServerCommunicationPort->PortContext;
            if (Info->ConnectionPort->Flags & ALPC_PORT_FLAG_CLOSED) return NULL;
            return Info->ConnectionPort;

        case ALPC_PORT_TYPE_SERVER:
            if (!Info || !Info->ClientCommunicationPort) return NULL;
            if (Info->ClientCommunicationPort->Flags & ALPC_PORT_FLAG_CLOSED) return NULL;
            if (PortContext) *PortContext = Info->ClientCommunicationPort->PortContext;
            return Info->ClientCommunicationPort;
    }
    return NULL;
}

PALPC_PORT
NTAPI
AlpcpGetReceivePort(
    _In_ PALPC_PORT Port)
{
    PALPC_COMMUNICATION_INFO Info = Port->CommunicationInfo;

    if ((AlpcpPortType(Port) == ALPC_PORT_TYPE_SERVER) && Info && Info->ConnectionPort &&
        IsListEmpty(&Port->MainQueue))
    {
        return Info->ConnectionPort;
    }
    return Port;
}

VOID
NTAPI
AlpcpSignalWaiter(
    _In_ PETHREAD Thread)
{
    if (!KeReadStateSemaphore(&Thread->AlpcWaitSemaphore))
    {
        KeReleaseSemaphore(&Thread->AlpcWaitSemaphore, 1, 1, FALSE);
    }
}

VOID
NTAPI
AlpcpWakeWaiter(
    _In_ PALPC_PORT Port)
{
    PLIST_ENTRY Entry;
    PETHREAD Thread;

    if (IsListEmpty(&Port->Waiters)) return;
    Entry = RemoveHeadList(&Port->Waiters);
    InitializeListHead(Entry);
    Port->Waiting--;
    Thread = CONTAINING_RECORD(Entry, ETHREAD, AlpcWaitListEntry);
    AlpcpSignalWaiter(Thread);
}

VOID
NTAPI
AlpcpQueueMessage(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message)
{
    if (AlpcpQueueCompletionListMessage(Port, Message))
    {
        Message->QueuePort = Port;
        if ((Message->State & (ALPC_MSG_STATE_CONNECTION | ALPC_MSG_STATE_SYNC)) &&
            !(Message->State & ALPC_MSG_STATE_REPLIED))
        {
            AlpcpMakePending(Port, Message);
        }
        else
        {
            AlpcpFreeMessage(Message);
        }
        if (Port->CompletionPort)
        {
            IoSetIoCompletion(Port->CompletionPort, Port->CompletionKey, NULL, STATUS_SUCCESS, 0, TRUE);
        }
        return;
    }

    Message->QueuePort = Port;
    Message->State |= ALPC_MSG_STATE_QUEUED;
    InsertTailList(&Port->MainQueue, &Message->Entry);
    Port->MainQueueLength++;
    if (Port->Flags & ALPC_PORT_FLAG_WAITABLE)
    {
        KeSetEvent(&Port->WaitEvent, IO_NO_INCREMENT, FALSE);
    }
    if (Port->CompletionPort)
    {
        IoSetIoCompletion(Port->CompletionPort, Port->CompletionKey, NULL, STATUS_SUCCESS, 0, TRUE);
    }
    AlpcpWakeWaiter(Port);
}

static
VOID
AlpcpComputeTimeout(
    _In_opt_ PLARGE_INTEGER Timeout,
    _Out_ PLARGE_INTEGER Absolute,
    _Out_ PLARGE_INTEGER *Effective)
{
    LARGE_INTEGER Now;

    *Effective = NULL;
    if (!Timeout) return;
    if (Timeout->QuadPart >= 0)
    {
        *Absolute = *Timeout;
    }
    else
    {
        KeQuerySystemTime(&Now);
        Absolute->QuadPart = Now.QuadPart - Timeout->QuadPart;
    }
    *Effective = Absolute;
}

NTSTATUS
NTAPI
AlpcpWaitForMessage(
    _In_ PALPC_PORT Port,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout,
    _Out_ PKALPC_MESSAGE *Message)
{
    PETHREAD Thread = PsGetCurrentThread();
    NTSTATUS Status;
    LARGE_INTEGER Absolute;
    PLARGE_INTEGER Effective;
    PLIST_ENTRY Entry;

    *Message = NULL;
    AlpcpComputeTimeout(Timeout, &Absolute, &Effective);

    AlpcpAcquireLock();
    for (;;)
    {
        if (!IsListEmpty(&Port->MainQueue))
        {
            Entry = RemoveHeadList(&Port->MainQueue);
            InitializeListHead(Entry);
            Port->MainQueueLength--;
            if (IsListEmpty(&Port->MainQueue) && (Port->Flags & ALPC_PORT_FLAG_WAITABLE))
            {
                KeClearEvent(&Port->WaitEvent);
            }
            *Message = CONTAINING_RECORD(Entry, KALPC_MESSAGE, Entry);
            (*Message)->State &= ~ALPC_MSG_STATE_QUEUED;
            AlpcpReleaseLock();
            return STATUS_SUCCESS;
        }

        if (Port->Flags & ALPC_PORT_FLAG_CLOSED)
        {
            AlpcpReleaseLock();
            return STATUS_PORT_DISCONNECTED;
        }

        if (Effective && (Effective->QuadPart == 0))
        {
            AlpcpReleaseLock();
            return STATUS_TIMEOUT;
        }

        InsertTailList(&Port->Waiters, &Thread->AlpcWaitListEntry);
        Port->Waiting++;
        AlpcpReleaseLock();

        Status = KeWaitForSingleObject(&Thread->AlpcWaitSemaphore, WrLpcReceive, WaitMode, Alertable, Effective);

        AlpcpAcquireLock();
        if (!IsListEmpty(&Thread->AlpcWaitListEntry))
        {
            RemoveEntryList(&Thread->AlpcWaitListEntry);
            InitializeListHead(&Thread->AlpcWaitListEntry);
            Port->Waiting--;
        }
        if (Status != STATUS_SUCCESS)
        {
            if (!IsListEmpty(&Port->MainQueue) && !IsListEmpty(&Port->Waiters))
            {
                AlpcpWakeWaiter(Port);
            }
            AlpcpReleaseLock();
            return Status;
        }
    }
}

NTSTATUS
NTAPI
AlpcpWaitForReply(
    _In_ PKALPC_MESSAGE Message,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout,
    _Out_opt_ PKALPC_MESSAGE *ReceivedMessage)
{
    PETHREAD Thread = PsGetCurrentThread();
    NTSTATUS Status;
    LARGE_INTEGER Absolute;
    PLARGE_INTEGER Effective;

    AlpcpComputeTimeout(Timeout, &Absolute, &Effective);
    if (ReceivedMessage) *ReceivedMessage = NULL;

    for (;;)
    {
        Status = KeWaitForSingleObject(&Thread->AlpcWaitSemaphore, WrLpcReply, WaitMode, Alertable, Effective);

        AlpcpAcquireLock();
        if (Thread->AlpcMessage && Thread->AlpcMessage != Message)
        {
            if (ReceivedMessage)
                *ReceivedMessage = Thread->AlpcMessage;
            AlpcpReleaseLock();
            return STATUS_SUCCESS;
        }
        if (Message->State & ALPC_MSG_STATE_CANCELED)
        {
            Status = Message->CompletionStatus ? Message->CompletionStatus : STATUS_MESSAGE_LOST;
            Thread->AlpcMessage = NULL;
            Thread->AlpcMessageId = 0;
            Message->WaitingThread = NULL;
            if (Message->State & ALPC_MSG_STATE_IN_CANCELED_QUEUE)
            {
                RemoveEntryList(&Message->CanceledEntry);
                InitializeListHead(&Message->CanceledEntry);
                Message->State &= ~ALPC_MSG_STATE_IN_CANCELED_QUEUE;
                if (Message->CancelQueuePort) Message->CancelQueuePort->CanceledQueueLength--;
                Message->CancelQueuePort = NULL;
            }
            AlpcpFreeMessage(Message);
            AlpcpReleaseLock();
            return Status;
        }
        if (Message->State & (ALPC_MSG_STATE_REPLIED |
                              ALPC_MSG_STATE_REFUSED |
                              ALPC_MSG_STATE_ACCEPTED |
                              ALPC_MSG_STATE_DISCONNECTED))
        {
            if (ReceivedMessage) *ReceivedMessage = Message;
            Thread->AlpcMessage = NULL;
            Thread->AlpcMessageId = 0;
            AlpcpReleaseLock();
            return STATUS_SUCCESS;
        }

        if (Status != STATUS_SUCCESS)
        {
            AlpcpDetachWaiter(Message);
            Thread->AlpcMessage = NULL;
            Thread->AlpcMessageId = 0;
            AlpcpReleaseLock();
            return Status;
        }
        AlpcpReleaseLock();
    }
}

VOID
NTAPI
AlpcpDetachWaiter(
    _In_ PKALPC_MESSAGE Message)
{
    PKALPC_MESSAGE Parent;

    Message->WaitingThread = NULL;
    if (Message->State & ALPC_MSG_STATE_QUEUED)
    {
        RemoveEntryList(&Message->Entry);
        InitializeListHead(&Message->Entry);
        Message->State &= ~ALPC_MSG_STATE_QUEUED;
        if (Message->QueuePort)
        {
            Message->QueuePort->MainQueueLength--;
            if (IsListEmpty(&Message->QueuePort->MainQueue) &&
                (Message->QueuePort->Flags & ALPC_PORT_FLAG_WAITABLE))
            {
                KeClearEvent(&Message->QueuePort->WaitEvent);
            }
        }
        AlpcpFreeMessage(Message);
        return;
    }

    Parent = Message->CallbackParent;
    if (Parent && (Parent->ActiveCallback == Message))
    {
        Parent->ActiveCallback = NULL;
        if (Message->ServerThread &&
            (Message->ServerThread->AlpcMessage == Message))
        {
            Message->ServerThread->AlpcMessage = Parent;
            Message->ServerThread->AlpcMessageId =
                Parent->PortMessage.MessageId;
        }
        Message->CallbackParent = NULL;
    }
    AlpcpCompleteWithStatus(Message, ALPC_MSG_STATE_CANCELED, STATUS_MESSAGE_LOST);
}

PKALPC_MESSAGE
NTAPI
AlpcpFindPendingMessage(
    _In_ PALPC_PORT Port,
    _In_ ULONG MessageId,
    _In_opt_ PCLIENT_ID ClientId)
{
    PLIST_ENTRY Entry;
    PKALPC_MESSAGE Message;

    for (Entry = Port->PendingQueue.Flink; Entry != &Port->PendingQueue; Entry = Entry->Flink)
    {
        Message = CONTAINING_RECORD(Entry, KALPC_MESSAGE, Entry);
        if (Message->PortMessage.MessageId != MessageId) continue;
        if (ClientId && ((Message->PortMessage.ClientId.UniqueProcess != ClientId->UniqueProcess) || (Message->PortMessage.ClientId.UniqueThread != ClientId->UniqueThread))) continue;
        return Message;
    }
    return NULL;
}

PKALPC_MESSAGE
NTAPI
AlpcpFindPendingMessageForReply(
    _In_ PALPC_PORT Port,
    _In_ ULONG MessageId,
    _In_opt_ PCLIENT_ID ClientId)
{
    PKALPC_MESSAGE Message;
    PALPC_COMMUNICATION_INFO Info = Port->CommunicationInfo;

    Message = AlpcpFindPendingMessage(Port, MessageId, ClientId);
    if (Message) return Message;

    if ((AlpcpPortType(Port) == ALPC_PORT_TYPE_SERVER) && Info && Info->ConnectionPort)
    {
        return AlpcpFindPendingMessage(Info->ConnectionPort, MessageId, ClientId);
    }
    return NULL;
}

PKALPC_MESSAGE
NTAPI
AlpcpFindCanceledMessage(
    _In_ PALPC_PORT Port,
    _In_ ULONG MessageId,
    _In_ ULONG CallbackId)
{
    PLIST_ENTRY Entry;
    PKALPC_MESSAGE Message;

    for (Entry = Port->CanceledQueue.Flink; Entry != &Port->CanceledQueue; Entry = Entry->Flink)
    {
        Message = CONTAINING_RECORD(Entry, KALPC_MESSAGE, CanceledEntry);
        if ((Message->PortMessage.MessageId == MessageId) && (Message->PortMessage.CallbackId == CallbackId)) return Message;
    }
    return NULL;
}

VOID
NTAPI
AlpcpRemovePending(
    _In_ PKALPC_MESSAGE Message)
{
    if (Message->State & ALPC_MSG_STATE_PENDING)
    {
        RemoveEntryList(&Message->Entry);
        InitializeListHead(&Message->Entry);
        Message->State &= ~ALPC_MSG_STATE_PENDING;
        if (Message->QueuePort) Message->QueuePort->PendingQueueLength--;
    }
}

VOID
NTAPI
AlpcpMakePending(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message)
{
    Message->QueuePort = Port;
    Message->State |= ALPC_MSG_STATE_PENDING;
    InsertTailList(&Port->PendingQueue, &Message->Entry);
    Port->PendingQueueLength++;
}

VOID
NTAPI
AlpcpCompleteReply(
    _In_ PKALPC_MESSAGE Message)
{
    AlpcpRemovePending(Message);
    Message->State |= ALPC_MSG_STATE_REPLIED;
    if (Message->WaitingThread)
    {
        AlpcpSignalWaiter(Message->WaitingThread);
    }
    else if (!(Message->State & ALPC_MSG_STATE_IMPERSONATING))
    {
        AlpcpFreeMessage(Message);
    }
}

VOID
NTAPI
AlpcpCompleteWithStatus(
    _In_ PKALPC_MESSAGE Message,
    _In_ ULONG StateBits,
    _In_ NTSTATUS Status)
{
    PKALPC_MESSAGE Callback, Parent;

    Callback = Message->ActiveCallback;
    if (Callback)
    {
        Message->ActiveCallback = NULL;
        Callback->CallbackParent = NULL;
        AlpcpCompleteWithStatus(Callback, ALPC_MSG_STATE_CANCELED | ALPC_MSG_STATE_DISCONNECTED, Status);
    }

    Parent = Message->CallbackParent;
    if (Parent && (Parent->ActiveCallback == Message))
    {
        Parent->ActiveCallback = NULL;
        if (Message->ServerThread &&
            (Message->ServerThread->AlpcMessage == Message))
        {
            Message->ServerThread->AlpcMessage = Parent;
            Message->ServerThread->AlpcMessageId =
                Parent->PortMessage.MessageId;
        }
        Message->CallbackParent = NULL;
    }

    if (Message->State & ALPC_MSG_STATE_IN_CANCELED_QUEUE)
    {
        RemoveEntryList(&Message->CanceledEntry);
        InitializeListHead(&Message->CanceledEntry);
        Message->State &= ~ALPC_MSG_STATE_IN_CANCELED_QUEUE;
        if (Message->CancelQueuePort) Message->CancelQueuePort->CanceledQueueLength--;
        Message->CancelQueuePort = NULL;
    }
    if (Message->State & ALPC_MSG_STATE_QUEUED)
    {
        RemoveEntryList(&Message->Entry);
        InitializeListHead(&Message->Entry);
        Message->State &= ~ALPC_MSG_STATE_QUEUED;
        if (Message->QueuePort) Message->QueuePort->MainQueueLength--;
    }
    AlpcpRemovePending(Message);
    Message->State |= StateBits;
    Message->CompletionStatus = Status;
    if ((StateBits == ALPC_MSG_STATE_CANCELED) && Message->QueuePort)
    {
        Message->CancelQueuePort = Message->QueuePort;
        Message->State |= ALPC_MSG_STATE_IN_CANCELED_QUEUE;
        InsertTailList(&Message->CancelQueuePort->CanceledQueue, &Message->CanceledEntry);
        Message->CancelQueuePort->CanceledQueueLength++;
    }
    if (Message->WaitingThread)
    {
        AlpcpSignalWaiter(Message->WaitingThread);
    }
    else if (!(Message->State & (ALPC_MSG_STATE_IN_CANCELED_QUEUE | ALPC_MSG_STATE_IMPERSONATING)))
    {
        AlpcpFreeMessage(Message);
    }
}

VOID
NTAPI
AlpcpRundownQueues(
    _In_ PALPC_PORT Port)
{
    PLIST_ENTRY Entry;
    PKALPC_MESSAGE Message;

    while (!IsListEmpty(&Port->MainQueue))
    {
        Entry = Port->MainQueue.Flink;
        Message = CONTAINING_RECORD(Entry, KALPC_MESSAGE, Entry);
        AlpcpCompleteWithStatus(Message, ALPC_MSG_STATE_DISCONNECTED, STATUS_PORT_DISCONNECTED);
    }
    while (!IsListEmpty(&Port->PendingQueue))
    {
        Entry = Port->PendingQueue.Flink;
        Message = CONTAINING_RECORD(Entry, KALPC_MESSAGE, Entry);
        AlpcpCompleteWithStatus(Message, ALPC_MSG_STATE_DISCONNECTED, STATUS_PORT_DISCONNECTED);
    }
    while (!IsListEmpty(&Port->CanceledQueue))
    {
        Entry = Port->CanceledQueue.Flink;
        Message = CONTAINING_RECORD(Entry, KALPC_MESSAGE, CanceledEntry);
        AlpcpCompleteWithStatus(Message, ALPC_MSG_STATE_CANCELED | ALPC_MSG_STATE_DISCONNECTED, STATUS_PORT_DISCONNECTED);
    }
    while (!IsListEmpty(&Port->Waiters))
    {
        AlpcpWakeWaiter(Port);
    }
    if (Port->Flags & ALPC_PORT_FLAG_WAITABLE)
    {
        KeSetEvent(&Port->WaitEvent, IO_NO_INCREMENT, FALSE);
    }
}

VOID
NTAPI
AlpcpSendPortClosed(
    _In_ PALPC_PORT Port)
{
    PALPC_PORT QueuePort;
    PVOID PortContext;
    PKALPC_MESSAGE Message;
    PCLIENT_DIED_MSG Closed;

    QueuePort = AlpcpGetQueuePort(Port, &PortContext);
    if (!QueuePort) return;

    Message = AlpcpAllocateMessage(sizeof(CLIENT_DIED_MSG), QueuePort);
    if (!Message) return;

    Closed = (PCLIENT_DIED_MSG)&Message->PortMessage;
    Closed->h.u1.s1.TotalLength = sizeof(CLIENT_DIED_MSG);
    Closed->h.u1.s1.DataLength = sizeof(Closed->CreateTime);
    Closed->h.u2.ZeroInit = 0;
    Closed->h.u2.s2.Type = LPC_PORT_CLOSED;
    Closed->h.ClientId = Port->Creator;
    Closed->h.MessageId = AlpcpNextMessageId++;
    if (!AlpcpNextMessageId) AlpcpNextMessageId = 1;
    Closed->CreateTime = PsGetCurrentProcess()->CreateTime;
    Message->PortContext = PortContext;
    AlpcpSetMessageSenderPort(Message, Port);
    Message->OwnerPort = QueuePort;
    AlpcpQueueMessage(QueuePort, Message);
}

static
VOID
AlpcpMarkMessageSenderDisconnected(
    _Inout_ PKALPC_MESSAGE Message,
    _In_ PALPC_PORT Port)
{
    if (Message->SenderPort == Port)
    {
        Message->SenderPort = NULL;
        Message->State |= ALPC_MSG_STATE_SENDER_DISCONNECTED;
    }
    if (Message->ActiveCallback) AlpcpMarkMessageSenderDisconnected(Message->ActiveCallback, Port);
}

static
VOID
AlpcpMarkQueueSenderDisconnected(
    _In_ PLIST_ENTRY Queue,
    _In_ PALPC_PORT Port)
{
    PLIST_ENTRY Entry;
    PKALPC_MESSAGE Message;

    for (Entry = Queue->Flink; Entry != Queue; Entry = Entry->Flink)
    {
        Message = CONTAINING_RECORD(Entry, KALPC_MESSAGE, Entry);
        AlpcpMarkMessageSenderDisconnected(Message, Port);
    }
}

static
VOID
AlpcpMarkSenderDisconnected(
    _In_ PALPC_PORT Port)
{
    PLIST_ENTRY Entry;
    PALPC_PORT Current;

    for (Entry = AlpcpPortList.Flink; Entry != &AlpcpPortList; Entry = Entry->Flink)
    {
        Current = CONTAINING_RECORD(Entry, ALPC_PORT, PortListEntry);
        AlpcpMarkQueueSenderDisconnected(&Current->MainQueue, Port);
        AlpcpMarkQueueSenderDisconnected(&Current->PendingQueue, Port);
    }
}

VOID
NTAPI
AlpcpDereferenceCommunicationInfo(
    _In_ PALPC_COMMUNICATION_INFO Info)
{
    if (InterlockedDecrement(&Info->ReferenceCount) != 0) return;

    if (!IsListEmpty(&Info->CommunicationList))
    {
        RemoveEntryList(&Info->CommunicationList);
        InitializeListHead(&Info->CommunicationList);
    }

    if (AlpcpLock.Owner == KeGetCurrentThread())
    {
        InsertTailList(&AlpcpDeferredCommunicationList, &Info->CommunicationList);
        return;
    }

    AlpcpFreeCommunicationInfoContents(Info);
}

static
VOID
AlpcpFreeCommunicationInfoContents(
    _In_ PALPC_COMMUNICATION_INFO Info)
{
    PALPC_PORT ConnectionPort = Info->ConnectionPort;

    ASSERT(IsListEmpty(&Info->CommunicationList));
    Info->ConnectionPort = NULL;
    ExFreePoolWithTag(Info, 'IcpA');
    if (ConnectionPort) ObDereferenceObject(ConnectionPort);
}

NTSTATUS
NTAPI
AlpcpDisconnectPort(
    _In_ PALPC_PORT Port,
    _In_ BOOLEAN Flush)
{
    PALPC_COMMUNICATION_INFO Info;
    PALPC_PORT Peer = NULL;
    PLIST_ENTRY Entry;

    AlpcpAcquireLock();
    if (Port->Flags & ALPC_PORT_FLAG_CLOSED)
    {
        AlpcpReleaseLock();
        return STATUS_PORT_DISCONNECTED;
    }
    Port->Flags |= ALPC_PORT_FLAG_CLOSED;
    if (!Flush) Port->Flags |= ALPC_PORT_FLAG_NO_FLUSH_ON_CLOSE;

    Info = Port->CommunicationInfo;
    switch (AlpcpPortType(Port))
    {
        case ALPC_PORT_TYPE_CONNECTION:
            Port->Flags |= ALPC_PORT_FLAG_NAME_DELETED;
            for (Entry = Port->CommunicationPorts.Flink;
                 Entry != &Port->CommunicationPorts;
                 Entry = Entry->Flink)
            {
                Info = CONTAINING_RECORD(Entry, ALPC_COMMUNICATION_INFO, CommunicationList);
                if (Info->ClientCommunicationPort)
                    Info->ClientCommunicationPort->Flags |= ALPC_PORT_FLAG_DISCONNECTED;
                if (Info->ServerCommunicationPort)
                    Info->ServerCommunicationPort->Flags |= ALPC_PORT_FLAG_DISCONNECTED;
            }
            break;

        case ALPC_PORT_TYPE_CLIENT:
            if (Info) Peer = Info->ServerCommunicationPort;
            if (Peer) Peer->Flags |= ALPC_PORT_FLAG_DISCONNECTED;
            break;

        case ALPC_PORT_TYPE_SERVER:
            if (Info) Peer = Info->ClientCommunicationPort;
            if (Peer) Peer->Flags |= ALPC_PORT_FLAG_DISCONNECTED;
            break;
    }

    if (AlpcpPortType(Port) == ALPC_PORT_TYPE_CLIENT)
    {
        AlpcpReleaseLock();
        AlpcpSendPortClosed(Port);
        AlpcpAcquireLock();
    }

    AlpcpMarkSenderDisconnected(Port);
    AlpcpRundownQueues(Port);
    AlpcpReleaseLock();
    return STATUS_SUCCESS;
}

VOID
NTAPI
AlpcpClosePort(
    _In_opt_ PEPROCESS Process,
    _In_ PVOID Object,
    _In_ ULONG_PTR ProcessHandleCount,
    _In_ ULONG_PTR SystemHandleCount)
{
    PALPC_PORT Port = Object;

    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(ProcessHandleCount);

    if (SystemHandleCount != 1) return;
    AlpcpDisconnectPort(Port, TRUE);
}

VOID
NTAPI
AlpcpDeletePort(
    _In_ PVOID ObjectBody)
{
    PALPC_PORT Port = ObjectBody;
    PALPC_COMMUNICATION_INFO Info;
    PKALPC_COMPLETION_LIST CompletionList;
    PVOID CallbackObject;
    PEPROCESS OwnerProcess;

    AlpcpDisconnectPort(Port, TRUE);

    AlpcpAcquireLock();
    Info = Port->CommunicationInfo;
    Port->CommunicationInfo = NULL;
    CompletionList = Port->CompletionList;
    Port->CompletionList = NULL;
    CallbackObject = Port->CallbackObject;
    Port->CallbackObject = NULL;
    if (Info)
    {
        if (Info->ClientCommunicationPort == Port) Info->ClientCommunicationPort = NULL;
        if (Info->ServerCommunicationPort == Port) Info->ServerCommunicationPort = NULL;
    }
    if (!IsListEmpty(&Port->PortListEntry))
    {
        RemoveEntryList(&Port->PortListEntry);
        InitializeListHead(&Port->PortListEntry);
    }
    AlpcpRundownQueues(Port);
    AlpcpReleaseLock();

    AlpcpRundownResources(Port);

    if (CompletionList) AlpcpFreeCompletionList(CompletionList);
    if (CallbackObject) ObDereferenceObject(CallbackObject);

    if (Port->ClientSectionBase)
    {
        MmUnmapViewOfSection(Port->OwnerProcess, Port->ClientSectionBase);
        Port->ClientSectionBase = NULL;
    }
    if (Port->ServerSectionBase)
    {
        MmUnmapViewOfSection(Port->OwnerProcess, Port->ServerSectionBase);
        Port->ServerSectionBase = NULL;
    }

    if (Info)
    {
        AlpcpAcquireLock();
        AlpcpDereferenceCommunicationInfo(Info);
        AlpcpReleaseLock();
    }

    if (!(Port->Flags & ALPC_PORT_FLAG_DYNAMIC_SECURITY) && Port->StaticSecurity.ClientToken)
    {
        SeDeleteClientSecurity(&Port->StaticSecurity);
    }

    OwnerProcess = Port->OwnerProcess;
    Port->OwnerProcess = NULL;
    if (OwnerProcess) ObDereferenceObject(OwnerProcess);
}

VOID
NTAPI
LpcExitThread(
    _In_ PETHREAD Thread)
{
    PKALPC_MESSAGE Message;

    AlpcpAcquireLock();
    Thread->LpcExitThreadCalled = TRUE;
    if (!IsListEmpty(&Thread->AlpcWaitListEntry))
    {
        RemoveEntryList(&Thread->AlpcWaitListEntry);
        InitializeListHead(&Thread->AlpcWaitListEntry);
    }
    Message = Thread->AlpcMessage;
    Thread->AlpcMessage = NULL;
    Thread->AlpcMessageId = 0;
    if (Message && (Message->WaitingThread == Thread))
    {
        if (Message->State & ALPC_MSG_STATE_CANCELED)
        {
            Message->WaitingThread = NULL;
            AlpcpCompleteWithStatus(Message, ALPC_MSG_STATE_CANCELED | ALPC_MSG_STATE_DISCONNECTED, STATUS_MESSAGE_LOST);
        }
        else if (Message->State & (ALPC_MSG_STATE_REPLIED |
                                   ALPC_MSG_STATE_REFUSED |
                                   ALPC_MSG_STATE_ACCEPTED |
                                   ALPC_MSG_STATE_DISCONNECTED))
        {
            AlpcpFreeMessage(Message);
        }
        else
        {
            AlpcpDetachWaiter(Message);
        }
    }
    AlpcpReleaseLock();
}
