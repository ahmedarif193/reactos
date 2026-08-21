/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Advanced Local Procedure Call message transport
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <ntoskrnl.h>

typedef struct _ALPC_CONTEXT_ATTR32
{
    ULONG PortContext;
    ULONG MessageContext;
    ULONG Sequence;
    ULONG MessageId;
    ULONG CallbackId;
} ALPC_CONTEXT_ATTR32, *PALPC_CONTEXT_ATTR32;

C_ASSERT(sizeof(ALPC_CONTEXT_ATTR32) == 0x14);

static
NTSTATUS
AlpcpCaptureHeader(
    _In_ PPORT_MESSAGE UserMessage,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PPORT_MESSAGE Header)
{
    ULONG DataLength;
    ULONG TotalLength;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(UserMessage, sizeof(*UserMessage), sizeof(ULONG));
            *Header = *(volatile PORT_MESSAGE*)UserMessage;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        *Header = *UserMessage;
    }

    DataLength = (USHORT)Header->u1.s1.DataLength;
    TotalLength = (USHORT)Header->u1.s1.TotalLength;
    if ((DataLength > MAXUSHORT - sizeof(PORT_MESSAGE)) ||
        (DataLength + sizeof(PORT_MESSAGE) != TotalLength))
    {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
AlpcpCopyIn(
    _Out_ PVOID Destination,
    _In_ PVOID Source,
    _In_ SIZE_T Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    if (!Length) return STATUS_SUCCESS;
    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(Source, Length, sizeof(UCHAR));
            RtlCopyMemory(Destination, Source, Length);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(Destination, Source, Length);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
AlpcpCopyOut(
    _Out_ PVOID Destination,
    _In_ PVOID Source,
    _In_ SIZE_T Length,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    if (!Length) return STATUS_SUCCESS;
    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(Destination, Length, sizeof(UCHAR));
            RtlCopyMemory(Destination, Source, Length);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(Destination, Source, Length);
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
AlpcpReplyToMessage(
    _In_ PALPC_PORT Port,
    _In_ PPORT_MESSAGE Header,
    _In_ PVOID Data,
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES SendAttributes,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_opt_ PKALPC_MESSAGE *ContinueMessage)
{
    NTSTATUS Status;
    PKALPC_MESSAGE Message;
    KALPC_MESSAGE CapturedMessage;
    PVOID Buffer = NULL;
    ULONG CopyLength = (USHORT)Header->u1.s1.DataLength;
    PALPC_PORT ReplyPort;
    PALPC_COMMUNICATION_INFO CommunicationInfo;

    if (ContinueMessage) *ContinueMessage = NULL;
    RtlZeroMemory(&CapturedMessage, sizeof(CapturedMessage));

    if (!Header->MessageId) return STATUS_INVALID_PARAMETER;

    if (CopyLength)
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, CopyLength, 'RcpA');
        if (!Buffer) return STATUS_NO_MEMORY;
        Status = AlpcpCopyIn(Buffer, Data, CopyLength, PreviousMode);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(Buffer, 'RcpA');
            return Status;
        }
    }

    Status = AlpcpCaptureSendAttributes(Port, &CapturedMessage, SendAttributes, PreviousMode);
    if (!NT_SUCCESS(Status))
    {
        AlpcpReleaseMessageAttributes(&CapturedMessage);
        if (Buffer) ExFreePoolWithTag(Buffer, 'RcpA');
        return Status;
    }

    AlpcpAcquireLock();
    Message = AlpcpFindPendingMessageForReply(Port, Header->MessageId, &Header->ClientId);
    if (!Message)
    {
        Message = AlpcpFindCanceledMessage(Port, Header->MessageId, Header->CallbackId);
        CommunicationInfo = Port->CommunicationInfo;
        if (!Message &&
            (AlpcpPortType(Port) == ALPC_PORT_TYPE_SERVER) &&
            CommunicationInfo &&
            CommunicationInfo->ConnectionPort)
        {
            Message = AlpcpFindCanceledMessage(CommunicationInfo->ConnectionPort, Header->MessageId, Header->CallbackId);
        }
        if (Message &&
            (Message->PortMessage.ClientId.UniqueProcess == Header->ClientId.UniqueProcess) &&
            (Message->PortMessage.ClientId.UniqueThread == Header->ClientId.UniqueThread))
        {
            AlpcpReleaseLock();
            AlpcpReleaseMessageAttributes(&CapturedMessage);
            if (Buffer) ExFreePoolWithTag(Buffer, 'RcpA');
            return STATUS_REQUEST_CANCELED;
        }
        Message = NULL;
    }
    if (!Message ||
        (Message->State & ALPC_MSG_STATE_CONNECTION) ||
        (Message->PortMessage.CallbackId != Header->CallbackId) ||
        Message->ActiveCallback)
    {
        AlpcpReleaseLock();
        AlpcpReleaseMessageAttributes(&CapturedMessage);
        if (Buffer) ExFreePoolWithTag(Buffer, 'RcpA');
        return STATUS_INVALID_MESSAGE;
    }
    if (sizeof(PORT_MESSAGE) + CopyLength > Message->AllocatedLength)
    {
        AlpcpReleaseLock();
        AlpcpReleaseMessageAttributes(&CapturedMessage);
        if (Buffer) ExFreePoolWithTag(Buffer, 'RcpA');
        return STATUS_PORT_MESSAGE_TOO_LONG;
    }
    AlpcpRemovePending(Message);
    Message->State |= ALPC_MSG_STATE_REPLY_IN_PROGRESS;
    AlpcpReleaseLock();

    if ((CapturedMessage.Attributes.ViewFlags & ALPC_VIEWFLG_UNMAP_EXISTING) && Message->Attributes.View) AlpcpDeleteView(Message->Attributes.View);
    AlpcpReleaseMessageAttributes(Message);
    Message->Attributes = CapturedMessage.Attributes;
    RtlZeroMemory(&CapturedMessage.Attributes, sizeof(CapturedMessage.Attributes));

    Message->PortMessage.u1.Length = Header->u1.Length;
    Message->PortMessage.u2.ZeroInit = 0;
    Message->PortMessage.u2.s2.Type = LPC_REPLY;
    Message->PortMessage.ClientViewSize = Header->ClientViewSize;
    if (CopyLength) RtlCopyMemory(&Message->PortMessage + 1, Buffer, CopyLength);
    if (Buffer) ExFreePoolWithTag(Buffer, 'RcpA');

    AlpcpAcquireLock();
    Message->State &= ~ALPC_MSG_STATE_REPLY_IN_PROGRESS;
    if (Message->State & ALPC_MSG_STATE_CANCELED)
    {
        AlpcpCompleteWithStatus(Message, ALPC_MSG_STATE_CANCELED | ALPC_MSG_STATE_DISCONNECTED, STATUS_MESSAGE_LOST);
        AlpcpReleaseLock();
        return STATUS_REQUEST_CANCELED;
    }
    if (Message->CallbackParent &&
        Message->CallbackParent->ActiveCallback == Message)
    {
        PKALPC_MESSAGE Parent = Message->CallbackParent;

        Parent->ActiveCallback = NULL;
        Message->CallbackParent = NULL;
        if (Message->ServerThread && Message->ServerThread->AlpcMessage == Message)
        {
            Message->ServerThread->AlpcMessage = NULL;
            Message->ServerThread->AlpcMessageId = 0;
        }
        Parent->WaitingThread = NULL;
        AlpcpRemovePending(Parent);
        AlpcpFreeMessage(Parent);
    }
    ReplyPort = Message->ReplyPort;
    if (ReplyPort)
    {
        Message->ReplyPort = NULL;
        Message->State |= ALPC_MSG_STATE_REPLIED;
        if (ReplyPort->Flags & ALPC_PORT_FLAG_CLOSED)
        {
            AlpcpFreeMessage(Message);
        }
        else
        {
            Message->PortContext = ReplyPort->PortContext;
            AlpcpQueueMessage(ReplyPort, Message);
        }
        AlpcpReleaseLock();
        ObDereferenceObject(ReplyPort);
    }
    else
    {
        AlpcpCompleteReply(Message);
        AlpcpReleaseLock();
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
AlpcpDeliverCallbackMessage(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Callback,
    _In_ PPORT_MESSAGE Header)
{
    PKALPC_MESSAGE Parent;
    PALPC_PORT TargetPort;
    PETHREAD TargetThread;
    PETHREAD CurrentThread = PsGetCurrentThread();

    AlpcpAcquireLock();
    Parent = AlpcpFindPendingMessageForReply(Port, Header->MessageId, &Header->ClientId);
    if (!Parent ||
        (Parent->ServerThread != CurrentThread) ||
        (Parent->PortMessage.CallbackId != Header->CallbackId) ||
        Parent->ActiveCallback ||
        !Parent->WaitingThread)
    {
        AlpcpReleaseLock();
        return STATUS_INVALID_MESSAGE;
    }

    TargetPort = Parent->SenderPort;
    TargetThread = Parent->WaitingThread;
    if (!TargetPort ||
        (TargetPort->Flags & (ALPC_PORT_FLAG_CLOSED |
                              ALPC_PORT_FLAG_DISCONNECTED)) ||
        TargetThread->LpcExitThreadCalled)
    {
        AlpcpReleaseLock();
        return STATUS_PORT_DISCONNECTED;
    }

    Callback->State |= ALPC_MSG_STATE_CALLBACK;
    Callback->OwnerPort = TargetPort;
    Callback->QueuePort = TargetPort;
    Callback->CallbackParent = Parent;
    Callback->WaitingThread = CurrentThread;
    Callback->ServerThread = TargetThread;
    Callback->PortMessage.MessageId = Parent->PortMessage.MessageId;
    Callback->PortMessage.CallbackId = AlpcpNextCallbackId++;
    if (!AlpcpNextCallbackId) AlpcpNextCallbackId = 1;
    Callback->PortMessage.ClientId = Parent->PortMessage.ClientId;
    Callback->Sequence = TargetPort->SequenceNo++;

    Parent->ActiveCallback = Callback;
    AlpcpMakePending(TargetPort, Callback);
    CurrentThread->AlpcMessage = Callback;
    CurrentThread->AlpcMessageId = Callback->PortMessage.MessageId;
    TargetThread->AlpcMessage = Callback;
    TargetThread->AlpcMessageId = Callback->PortMessage.MessageId;
    AlpcpSignalWaiter(TargetThread);
    AlpcpReleaseLock();
    return STATUS_SUCCESS;
}

static
NTSTATUS
AlpcpQueueNewMessage(
    _In_ PALPC_PORT Port,
    _In_ ULONG Flags,
    _In_ PPORT_MESSAGE Header,
    _In_ PVOID Data,
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES SendAttributes,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PKALPC_MESSAGE *OutMessage)
{
    NTSTATUS Status;
    PKALPC_MESSAGE Message;
    PALPC_PORT QueuePort;
    PVOID PortContext;
    PETHREAD Thread = PsGetCurrentThread();
    ULONG MessageType;
    ULONG Capacity = Port->MaxMessageLength;
    BOOLEAN LpcMode = (Flags & ALPC_MSGFLG_LPC_MODE_INTERNAL) != 0;
    BOOLEAN Synchronous = (Flags & ALPC_MSGFLG_SYNC_REQUEST) != 0;
    BOOLEAN AsyncRequest = FALSE;
    BOOLEAN CallbackRequest;
    BOOLEAN LegacySpecialMessage = FALSE;
    USHORT LegacyType = Header->u2.s2.Type & ~LPC_KERNELMODE_MESSAGE;

    CallbackRequest = Synchronous && Header->MessageId;

    *OutMessage = NULL;

    if ((USHORT)Header->u1.s1.TotalLength > Port->MaxMessageLength) return STATUS_PORT_MESSAGE_TOO_LONG;
    if (!LpcMode && !Header->MessageId && (Header->ClientId.UniqueProcess || Header->ClientId.UniqueThread)) return STATUS_REPLY_MESSAGE_MISMATCH;

    if (LpcMode)
    {
        switch (LegacyType)
        {
            case LPC_NEW_MESSAGE:
            case LPC_REQUEST:
                break;

            case LPC_CLIENT_DIED:
            case LPC_PORT_CLOSED:
            case LPC_EXCEPTION:
            case LPC_DEBUG_EVENT:
            case LPC_ERROR_EVENT:
                LegacySpecialMessage = TRUE;
                break;

            default:
                return STATUS_INVALID_PARAMETER;
        }
    }

    if (LegacySpecialMessage)
    {
        MessageType = LegacyType;
    }
    else if ((Flags & ALPC_MSGFLG_REPLY_MESSAGE) && !Header->MessageId)
    {
        MessageType = LPC_DATAGRAM | (Header->u2.s2.Type & (LPC_NO_IMPERSONATE | LPC_KERNELMODE_MESSAGE));
    }
    else
    {
        MessageType = LPC_REQUEST | LPC_CONTINUATION_REQUIRED | (Header->u2.s2.Type & (LPC_NO_IMPERSONATE | LPC_KERNELMODE_MESSAGE));
        AsyncRequest = !Synchronous;
    }

    if (Capacity < (USHORT)Header->u1.s1.TotalLength) Capacity = (USHORT)Header->u1.s1.TotalLength;
    Message = AlpcpAllocateMessage(Capacity, Port);
    if (!Message) return STATUS_NO_MEMORY;

    Message->PortMessage = *Header;
    Message->PortMessage.u2.s2.Type = (CSHORT)MessageType;
    Message->PortMessage.ClientId = Thread->Cid;
    Message->PortMessage.CallbackId = 0;
    AlpcpSetMessageSenderPort(Message, Port);
    Message->State = (PreviousMode == KernelMode) ? ALPC_MSG_STATE_KERNEL : 0;
    if (LpcMode) Message->State |= ALPC_MSG_STATE_LPC_MODE;
    if (Synchronous) Message->State |= ALPC_MSG_STATE_SYNC;
    if (AsyncRequest) Message->State |= ALPC_MSG_STATE_SYNC | ALPC_MSG_STATE_ASYNC_REPLY;

    Status = AlpcpCopyIn(&Message->PortMessage + 1, Data, (USHORT)Header->u1.s1.DataLength, PreviousMode);
    if (!NT_SUCCESS(Status))
    {
        AlpcpFreeMessage(Message);
        return Status;
    }

    Status = AlpcpCaptureSendAttributes(Port, Message, SendAttributes, PreviousMode);
    if (!NT_SUCCESS(Status))
    {
        AlpcpFreeMessage(Message);
        return Status;
    }

    if (CallbackRequest)
    {
        Status = AlpcpDeliverCallbackMessage(Port, Message, Header);
        if (!NT_SUCCESS(Status))
        {
            AlpcpFreeMessage(Message);
            return Status;
        }
        *OutMessage = Message;
        return STATUS_SUCCESS;
    }

    AlpcpAcquireLock();
    QueuePort = AlpcpGetQueuePort(Port, &PortContext);
    if (!QueuePort || (Port->Flags & (ALPC_PORT_FLAG_CLOSED | ALPC_PORT_FLAG_DISCONNECTED)))
    {
        AlpcpReleaseLock();
        AlpcpFreeMessage(Message);
        return STATUS_PORT_DISCONNECTED;
    }
    Message->PortContext = PortContext;
    Message->OwnerPort = QueuePort;
    Message->PortMessage.MessageId = AlpcpNextMessageId++;
    if (!AlpcpNextMessageId) AlpcpNextMessageId = 1;
    Message->Sequence = QueuePort->SequenceNo++;
    if (Synchronous)
    {
        Message->WaitingThread = Thread;
        Thread->AlpcMessage = Message;
        Thread->AlpcMessageId = Message->PortMessage.MessageId;
    }
    else if (AsyncRequest)
    {
        ObReferenceObject(Port);
        Message->ReplyPort = Port;
    }
    AlpcpQueueMessage(QueuePort, Message);
    AlpcpReleaseLock();

    *OutMessage = Message;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AlpcpDeliverToUser(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message,
    _In_ PPORT_MESSAGE ReceiveMessage,
    _Inout_opt_ PSIZE_T BufferLength,
    _In_ SIZE_T AvailableLength,
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES ReceiveAttributes,
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES UserReceiveAttributes,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status;
    SIZE_T TotalLength = (USHORT)Message->PortMessage.u1.s1.TotalLength;

    if (AvailableLength < TotalLength)
    {
        Status = BufferLength ? AlpcpCopyOut(BufferLength, &TotalLength, sizeof(SIZE_T), PreviousMode) : STATUS_SUCCESS;
        if (!NT_SUCCESS(Status)) return Status;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Status = AlpcpCopyOut(ReceiveMessage, &Message->PortMessage, TotalLength, PreviousMode);
    if (!NT_SUCCESS(Status)) return Status;

    if (ReceiveAttributes)
    {
        Status = AlpcpExposeReceiveAttributes(Port, Message, ReceiveAttributes, UserReceiveAttributes, PreviousMode);
        if (!NT_SUCCESS(Status)) return Status;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
AlpcpReturnMessageToQueue(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message,
    _Inout_opt_ PSIZE_T BufferLength,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status;
    SIZE_T RequiredLength = (USHORT)Message->PortMessage.u1.s1.TotalLength;

    AlpcpAcquireLock();
    if (Port->Flags & ALPC_PORT_FLAG_CLOSED)
    {
        AlpcpCompleteWithStatus(Message, ALPC_MSG_STATE_DISCONNECTED, STATUS_PORT_DISCONNECTED);
        AlpcpReleaseLock();
        return STATUS_PORT_DISCONNECTED;
    }
    Message->QueuePort = Port;
    Message->State |= ALPC_MSG_STATE_QUEUED;
    InsertHeadList(&Port->MainQueue, &Message->Entry);
    Port->MainQueueLength++;
    if (Port->Flags & ALPC_PORT_FLAG_WAITABLE) KeSetEvent(&Port->WaitEvent, IO_NO_INCREMENT, FALSE);
    AlpcpWakeWaiter(Port);
    AlpcpReleaseLock();

    Status = BufferLength ? AlpcpCopyOut(BufferLength, &RequiredLength, sizeof(RequiredLength), PreviousMode) : STATUS_SUCCESS;
    return NT_SUCCESS(Status) ? STATUS_BUFFER_TOO_SMALL : Status;
}

static
NTSTATUS
AlpcpReceiveToUser(
    _In_ PALPC_PORT Port,
    _In_ KPROCESSOR_MODE WaitMode,
    _In_ BOOLEAN Alertable,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ PPORT_MESSAGE ReceiveMessage,
    _Inout_opt_ PSIZE_T BufferLength,
    _In_ SIZE_T AvailableLength,
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES ReceiveAttributes,
    _In_opt_ PALPC_MESSAGE_ATTRIBUTES UserReceiveAttributes,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    PETHREAD Thread = PsGetCurrentThread();
    NTSTATUS Status;
    PKALPC_MESSAGE Message = NULL;
    PALPC_PORT ReceivePort;
    BOOLEAN Pending = FALSE;
    BOOLEAN ThreadCallback = FALSE;

    AlpcpAcquireLock();
    ReceivePort = AlpcpGetReceivePort(Port);
    Message = (PKALPC_MESSAGE)Thread->AlpcMessage;
    if (Message && (Message->State & ALPC_MSG_STATE_CALLBACK) && (Message->ServerThread == Thread))
    {
        ThreadCallback = TRUE;
    }
    else
    {
        Message = NULL;
    }
    AlpcpReleaseLock();

    if (!ThreadCallback)
    {
        Status = AlpcpWaitForMessage(ReceivePort, WaitMode, Alertable, Timeout, &Message);
        if (Status != STATUS_SUCCESS) return Status;
    }

    if (AvailableLength < (USHORT)Message->PortMessage.u1.s1.TotalLength)
    {
        if (!ThreadCallback) return AlpcpReturnMessageToQueue(ReceivePort, Message, BufferLength, PreviousMode);
        return AlpcpDeliverToUser(Port, Message, ReceiveMessage, BufferLength, AvailableLength, ReceiveAttributes, UserReceiveAttributes, PreviousMode);
    }

    if (ThreadCallback)
    {
        Pending = TRUE;
    }
    else
    {
        AlpcpAcquireLock();
        if ((Message->State & (ALPC_MSG_STATE_CONNECTION | ALPC_MSG_STATE_SYNC)) &&
            !(Message->State & ALPC_MSG_STATE_REPLIED))
        {
            AlpcpMakePending(ReceivePort, Message);
            Message->ServerThread = Thread;
            Pending = TRUE;
        }
        AlpcpReleaseLock();
    }

    Status = AlpcpDeliverToUser(Port, Message, ReceiveMessage, BufferLength, AvailableLength, ReceiveAttributes, UserReceiveAttributes, PreviousMode);

    if (!Pending)
    {
        AlpcpFreeMessage(Message);
    }
    else if (!NT_SUCCESS(Status))
    {
        AlpcpAcquireLock();
        AlpcpCompleteWithStatus(Message, ALPC_MSG_STATE_DISCONNECTED, STATUS_LPC_REPLY_LOST);
        AlpcpReleaseLock();
    }
    return Status;
}

NTSTATUS
NTAPI
AlpcpSendWaitReceivePort(
    _In_ PALPC_PORT Port,
    _In_ ULONG Flags,
    _In_opt_ PPORT_MESSAGE SendMessage,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES SendMessageAttributes,
    _Out_opt_ PPORT_MESSAGE ReceiveMessage,
    _Inout_opt_ PSIZE_T BufferLength,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES ReceiveMessageAttributes,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status = STATUS_SUCCESS;
    KPROCESSOR_MODE WaitMode;
    BOOLEAN Alertable;
    PORT_MESSAGE Header;
    LARGE_INTEGER CapturedTimeout;
    SIZE_T AvailableLength = MAXULONG_PTR;
    ULONG ReceiveAttributeSize;
    PALPC_MESSAGE_ATTRIBUTES SendAttributes = NULL, ReceiveAttributes = NULL;
    PKALPC_MESSAGE Message = NULL, ReceivedMessage = NULL, ContinueMessage = NULL;

    PAGED_CODE();

    Flags &= (0xFFFF0000UL | ALPC_MSGFLG_LPC_MODE_INTERNAL);

    WaitMode = (Flags & ALPC_MSGFLG_WAIT_USER_MODE) ? UserMode : PreviousMode;
    Alertable = (Flags & ALPC_MSGFLG_WAIT_ALERTABLE) != 0;

    if (Flags & ALPC_MSGFLG_TRACK_PORT_REFERENCES) AlpcpTrackPortReferences(Port);

    if (Flags & ALPC_MSGFLG_SYNC_REQUEST)
    {
        if (!SendMessage || (Flags & (ALPC_MSGFLG_REPLY_MESSAGE | 0x01000000UL)))
        {
            Status = STATUS_INVALID_PARAMETER_2;
            goto Exit;
        }
        if (!ReceiveMessage)
        {
            Status = STATUS_LPC_RECEIVE_BUFFER_EXPECTED;
            goto Exit;
        }
    }
    else if (SendMessage && (Flags & 0x01000000UL))
    {
        Status = STATUS_INVALID_PARAMETER_2;
        goto Exit;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            if (Timeout)
            {
                ProbeForReadLargeInteger(Timeout);
                CapturedTimeout = *(volatile LARGE_INTEGER*)Timeout;
                Timeout = &CapturedTimeout;
            }
            if (ReceiveMessage) ProbeForWrite(ReceiveMessage, sizeof(*ReceiveMessage), sizeof(ULONG));
            if (BufferLength)
            {
                ProbeForWrite(BufferLength, sizeof(SIZE_T), sizeof(SIZE_T));
                AvailableLength = *(volatile SIZE_T*)BufferLength;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
            _SEH2_YIELD(goto Exit);
        }
        _SEH2_END;
    }
    else if (BufferLength)
    {
        AvailableLength = *BufferLength;
    }

    Status = AlpcpCaptureAttributes(SendMessageAttributes, PreviousMode, &SendAttributes);
    if (!NT_SUCCESS(Status)) goto Exit;
    Status = AlpcpCaptureAttributes(ReceiveMessageAttributes, PreviousMode, &ReceiveAttributes);
    if (!NT_SUCCESS(Status)) goto Exit;
    if ((PreviousMode != KernelMode) && ReceiveAttributes)
    {
        ReceiveAttributeSize = AlpcpAttributesSize(ReceiveAttributes->AllocatedAttributes);
        _SEH2_TRY
        {
            ProbeForWrite(ReceiveMessageAttributes, ReceiveAttributeSize, sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        if (!NT_SUCCESS(Status)) goto Exit;
    }

    if (SendMessage)
    {
        Status = AlpcpCaptureHeader(SendMessage, PreviousMode, &Header);
        if (!NT_SUCCESS(Status)) goto Exit;

        if ((Flags & ALPC_MSGFLG_REPLY_MESSAGE) && Header.MessageId)
        {
            Status = AlpcpReplyToMessage(Port, &Header, SendMessage + 1, SendAttributes, PreviousMode, &ContinueMessage);
            if (!NT_SUCCESS(Status)) goto Exit;

            if (ContinueMessage)
            {
                AlpcpAcquireLock();
                PsGetCurrentThread()->AlpcMessage = ContinueMessage;
                PsGetCurrentThread()->AlpcMessageId = ContinueMessage->PortMessage.MessageId;
                AlpcpReleaseLock();

                if (ReceiveMessage)
                {
                    Status = AlpcpWaitForReply(ContinueMessage, WaitMode, Alertable, Timeout, &ReceivedMessage);
                    if (Status != STATUS_SUCCESS) goto Exit;

                    if (!ReceivedMessage) ReceivedMessage = ContinueMessage;
                    if ((ReceivedMessage == ContinueMessage) && (AvailableLength < (USHORT)ContinueMessage->PortMessage.u1.s1.TotalLength))
                    {
                        Status = AlpcpReturnMessageToQueue(Port, ContinueMessage, BufferLength, PreviousMode);
                        goto Exit;
                    }
                    Status = AlpcpDeliverToUser(Port, ReceivedMessage, ReceiveMessage, BufferLength, AvailableLength, ReceiveAttributes, ReceiveMessageAttributes, PreviousMode);
                    if (ReceivedMessage == ContinueMessage)
                        AlpcpFreeMessage(ContinueMessage);
                    goto Exit;
                }
            }
        }
        else
        {
            Status = AlpcpQueueNewMessage(Port, Flags, &Header, SendMessage + 1, SendAttributes, PreviousMode, &Message);
            if (!NT_SUCCESS(Status)) goto Exit;

            if (Flags & ALPC_MSGFLG_SYNC_REQUEST)
            {
                Status = AlpcpWaitForReply(Message, WaitMode, Alertable, Timeout, &ReceivedMessage);
                if (Status != STATUS_SUCCESS) goto Exit;

                if (ReceivedMessage && ReceivedMessage != Message)
                {
                    Status = AlpcpDeliverToUser(Port, ReceivedMessage, ReceiveMessage, BufferLength, AvailableLength, ReceiveAttributes, ReceiveMessageAttributes, PreviousMode);
                    goto Exit;
                }

                if (!(Message->State & ALPC_MSG_STATE_REPLIED))
                {
                    Status = Message->CompletionStatus;
                    if (Status == STATUS_SUCCESS) Status = (Message->State & ALPC_MSG_STATE_CANCELED) ? STATUS_CANCELLED : STATUS_PORT_DISCONNECTED;
                    AlpcpFreeMessage(Message);
                    goto Exit;
                }

                if (AvailableLength < (USHORT)Message->PortMessage.u1.s1.TotalLength)
                {
                    Status = AlpcpReturnMessageToQueue(Port, Message, BufferLength, PreviousMode);
                    goto Exit;
                }

                Status = AlpcpDeliverToUser(Port, Message, ReceiveMessage, BufferLength, AvailableLength, ReceiveAttributes, ReceiveMessageAttributes, PreviousMode);
                AlpcpFreeMessage(Message);
                goto Exit;
            }
        }
    }

    if (ReceiveMessage)
    {
        Status = AlpcpReceiveToUser(Port, WaitMode, Alertable, Timeout, ReceiveMessage, BufferLength, AvailableLength, ReceiveAttributes, ReceiveMessageAttributes, PreviousMode);
    }

Exit:
    if (SendAttributes) ExFreePoolWithTag(SendAttributes, 'AcpA');
    if (ReceiveAttributes) ExFreePoolWithTag(ReceiveAttributes, 'AcpA');
    return Status;
}

NTSTATUS
NTAPI
NtAlpcSendWaitReceivePort(
    _In_ HANDLE PortHandle,
    _In_ ULONG Flags,
    _In_opt_ PPORT_MESSAGE SendMessage,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES SendMessageAttributes,
    _Out_opt_ PPORT_MESSAGE ReceiveMessage,
    _Inout_opt_ PSIZE_T BufferLength,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES ReceiveMessageAttributes,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;

    PAGED_CODE();

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_CONNECT, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    Flags &= 0xFFFF0000UL;
    Status = AlpcpSendWaitReceivePort(Port, Flags, SendMessage, SendMessageAttributes, ReceiveMessage, BufferLength, ReceiveMessageAttributes, Timeout, PreviousMode);
    ObDereferenceObject(Port);
    return Status;
}

NTSTATUS
NTAPI
NtAlpcCancelMessage(
    _In_ HANDLE PortHandle,
    _In_ ULONG Flags,
    _In_ PALPC_CONTEXT_ATTR MessageContext)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port, QueuePort;
    ALPC_CONTEXT_ATTR Context;
    ALPC_CONTEXT_ATTR32 Context32;
    PKALPC_MESSAGE Message = NULL;
    PLIST_ENTRY Entry;

    PAGED_CODE();

    if (Flags & ~0xFUL) return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            if (Flags & 0x4)
            {
                ProbeForRead(MessageContext, sizeof(Context32), sizeof(ULONG));
                Context32 = *(volatile ALPC_CONTEXT_ATTR32 *)MessageContext;
                Context.PortContext = UlongToPtr(Context32.PortContext);
                Context.MessageContext = UlongToPtr(Context32.MessageContext);
                Context.Sequence = Context32.Sequence;
                Context.MessageId = Context32.MessageId;
                Context.CallbackId = Context32.CallbackId;
            }
            else
            {
                ProbeForRead(MessageContext, sizeof(*MessageContext), sizeof(ULONG));
                Context = *(volatile ALPC_CONTEXT_ATTR *)MessageContext;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        Context = *MessageContext;
    }

    if (!Context.MessageId) return STATUS_MESSAGE_NOT_FOUND;

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_CONNECT, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    AlpcpAcquireLock();
    QueuePort = AlpcpGetQueuePort(Port, NULL);
    if (QueuePort)
    {
        for (Entry = QueuePort->MainQueue.Flink; Entry != &QueuePort->MainQueue; Entry = Entry->Flink)
        {
            Message = CONTAINING_RECORD(Entry, KALPC_MESSAGE, Entry);
            if ((Message->PortMessage.MessageId == Context.MessageId) &&
                (Message->PortMessage.CallbackId == Context.CallbackId)) break;
            Message = NULL;
        }
        if (!Message) Message = AlpcpFindPendingMessage(QueuePort, Context.MessageId, NULL);
        if (Message && (Message->PortMessage.CallbackId != Context.CallbackId)) Message = NULL;
    }
    if (!Message) Message = AlpcpFindPendingMessage(Port, Context.MessageId, NULL);
    if (Message && (Message->PortMessage.CallbackId != Context.CallbackId)) Message = NULL;

    if (!Message && QueuePort) Message = AlpcpFindCanceledMessage(QueuePort, Context.MessageId, Context.CallbackId);
    if (!Message) Message = AlpcpFindCanceledMessage(Port, Context.MessageId, Context.CallbackId);

    if (!Message)
    {
        AlpcpReleaseLock();
        ObDereferenceObject(Port);
        return STATUS_MESSAGE_NOT_FOUND;
    }

    if (Flags & ALPC_CANCELFLG_NO_CONTEXT_CHECK)
    {
        PVOID MessageContext;

        MessageContext = (AlpcpPortType(Port) == ALPC_PORT_TYPE_SERVER) ?
                         Message->Attributes.ServerContext :
                         Message->Attributes.ClientContext;
        if (MessageContext != Context.MessageContext)
        {
            AlpcpReleaseLock();
            ObDereferenceObject(Port);
            return STATUS_CONTEXT_MISMATCH;
        }
    }

    if (Message->State & (ALPC_MSG_STATE_CANCELED |
                          ALPC_MSG_STATE_DISCONNECTED |
                          ALPC_MSG_STATE_REPLIED))
    {
        AlpcpReleaseLock();
        ObDereferenceObject(Port);
        return STATUS_REQUEST_CANCELED;
    }

    if ((Flags & ALPC_CANCELFLG_TRY_CANCEL) && !(Message->State & ALPC_MSG_STATE_QUEUED))
    {
        AlpcpReleaseLock();
        ObDereferenceObject(Port);
        return STATUS_MESSAGE_RETRIEVED;
    }

    Status = (Message->State & ALPC_MSG_STATE_QUEUED) ? STATUS_SUCCESS : STATUS_MESSAGE_RETRIEVED;

    Message->PortMessage.u1.s1.DataLength = 0;
    Message->PortMessage.u1.s1.TotalLength = sizeof(PORT_MESSAGE);
    Message->PortMessage.u2.s2.Type = (CSHORT)(((USHORT)Message->PortMessage.u2.s2.Type & ~(0x00FF | LPC_CONTINUATION_REQUIRED)) | LPC_CANCELED);
    AlpcpCompleteWithStatus(Message, ALPC_MSG_STATE_CANCELED, STATUS_MESSAGE_LOST);
    AlpcpReleaseLock();

    ObDereferenceObject(Port);
    return Status;
}
