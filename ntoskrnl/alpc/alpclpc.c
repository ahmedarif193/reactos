/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Legacy LPC compatibility over the ALPC implementation
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <ntoskrnl.h>

#define ALPCP_LEGACY_MESSAGE_TYPE_MASK (LPC_NO_IMPERSONATE | LPC_KERNELMODE_MESSAGE | 0x000F)

static
KPROCESSOR_MODE
AlpcpLegacyWaitMode(
    _In_ KPROCESSOR_MODE PreviousMode)
{
    if ((PreviousMode == KernelMode) && PsGetCurrentThread()->SystemThread) return UserMode;
    return PreviousMode;
}

static
NTSTATUS
AlpcpCaptureLegacyHeader(
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
        (DataLength + sizeof(PORT_MESSAGE) > TotalLength))
    {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

static
NTSTATUS
AlpcpVerifyDataInfo(
    _In_ PPORT_MESSAGE Header)
{
    if (Header->u2.s2.DataInfoOffset == 0) return STATUS_SUCCESS;
    if (((USHORT)Header->u1.s1.TotalLength < sizeof(PORT_MESSAGE) + sizeof(LPCP_DATA_INFO)) ||
        ((ULONG)Header->u2.s2.DataInfoOffset < sizeof(PORT_MESSAGE)) ||
        ((ULONG)Header->u2.s2.DataInfoOffset > ((USHORT)Header->u1.s1.TotalLength - sizeof(LPCP_DATA_INFO))))
    {
        return STATUS_INVALID_PARAMETER;
    }
    return STATUS_SUCCESS;
}

static
ULONG
AlpcpLegacyCopyLength(
    _In_ PPORT_MESSAGE Header)
{
    if (Header->u2.s2.DataInfoOffset)
    {
        return (USHORT)Header->u1.s1.TotalLength - sizeof(PORT_MESSAGE);
    }
    return (USHORT)Header->u1.s1.DataLength;
}

static
NTSTATUS
AlpcpBuildLegacyMessage(
    _In_ PALPC_PORT Port,
    _In_ PPORT_MESSAGE Header,
    _In_ PVOID Data,
    _In_ ULONG MessageType,
    _In_ ULONG State,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PKALPC_MESSAGE *OutMessage)
{
    PKALPC_MESSAGE Message;
    ULONG Capacity = Port->MaxMessageLength;
    ULONG CopyLength;

    if (Capacity < (USHORT)Header->u1.s1.TotalLength) Capacity = (USHORT)Header->u1.s1.TotalLength;

    Message = AlpcpAllocateMessage(Capacity, Port);
    if (!Message) return STATUS_NO_MEMORY;

    Message->PortMessage = *Header;
    Message->PortMessage.u2.s2.Type = (CSHORT)MessageType;
    Message->PortMessage.ClientId = PsGetCurrentThread()->Cid;
    Message->PortMessage.CallbackId = 0;
    Message->State = State;
    AlpcpSetMessageSenderPort(Message, Port);
    if (Header->u2.s2.DataInfoOffset) Message->State |= ALPC_MSG_STATE_DATA_INFO;

    CopyLength = AlpcpLegacyCopyLength(Header);
    if (CopyLength)
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                ProbeForRead(Data, CopyLength, sizeof(UCHAR));
                RtlCopyMemory(&Message->PortMessage + 1, Data, CopyLength);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                AlpcpFreeMessage(Message);
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            RtlCopyMemory(&Message->PortMessage + 1, Data, CopyLength);
        }
    }

    *OutMessage = Message;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AlpcpDeliverLegacyMessage(
    _In_ PALPC_PORT Port,
    _In_ PKALPC_MESSAGE Message,
    _In_ BOOLEAN Synchronous)
{
    PALPC_PORT QueuePort;
    PVOID PortContext;
    PETHREAD Thread = PsGetCurrentThread();

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
    AlpcpQueueMessage(QueuePort, Message);
    AlpcpReleaseLock();
    return STATUS_SUCCESS;
}

static
NTSTATUS
AlpcpDeliverLegacyCallback(
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
        AlpcpFreeMessage(Callback);
        return STATUS_REPLY_MESSAGE_MISMATCH;
    }

    TargetPort = Parent->SenderPort;
    TargetThread = Parent->WaitingThread;
    if (!TargetPort ||
        (TargetPort->Flags & (ALPC_PORT_FLAG_CLOSED | ALPC_PORT_FLAG_DISCONNECTED)) ||
        TargetThread->LpcExitThreadCalled)
    {
        AlpcpReleaseLock();
        AlpcpFreeMessage(Callback);
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
AlpcpFinishSynchronousRequest(
    _In_ PKALPC_MESSAGE RequestMessage,
    _In_opt_ PKALPC_MESSAGE ReceivedMessage,
    _In_ NTSTATUS WaitStatus,
    _In_ PPORT_MESSAGE LpcReply,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status;
    ULONG CopyLength;
    PKALPC_MESSAGE Message = ReceivedMessage ? ReceivedMessage : RequestMessage;

    if (WaitStatus != STATUS_SUCCESS) return WaitStatus;

    if (Message != RequestMessage)
    {
        CopyLength = AlpcpLegacyCopyLength(&Message->PortMessage);
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                ProbeForWrite(LpcReply, sizeof(PORT_MESSAGE) + CopyLength, sizeof(ULONG));
                RtlCopyMemory(LpcReply, &Message->PortMessage, sizeof(PORT_MESSAGE) + CopyLength);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            RtlCopyMemory(LpcReply, &Message->PortMessage, sizeof(PORT_MESSAGE) + CopyLength);
        }
        return STATUS_SUCCESS;
    }

    if (!(Message->State & ALPC_MSG_STATE_REPLIED))
    {
        Status = (Message->State & ALPC_MSG_STATE_DISCONNECTED) ? STATUS_LPC_REPLY_LOST : STATUS_PORT_DISCONNECTED;
        AlpcpFreeMessage(Message);
        return Status;
    }

    Status = STATUS_SUCCESS;
    CopyLength = (USHORT)Message->PortMessage.u1.s1.DataLength;
    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(LpcReply, sizeof(PORT_MESSAGE) + CopyLength, sizeof(ULONG));
            RtlCopyMemory(LpcReply, &Message->PortMessage, sizeof(PORT_MESSAGE) + CopyLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(LpcReply, &Message->PortMessage, sizeof(PORT_MESSAGE) + CopyLength);
    }

    AlpcpFreeMessage(Message);
    return Status;
}

static
NTSTATUS
AlpcpLegacyRequestWaitReply(
    _In_ PALPC_PORT Port,
    _In_ PPORT_MESSAGE LpcRequest,
    _Out_ PPORT_MESSAGE LpcReply,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_ KPROCESSOR_MODE WaitMode)
{
    NTSTATUS Status;
    PORT_MESSAGE Header;
    PKALPC_MESSAGE Message;
    PKALPC_MESSAGE ReceivedMessage = NULL;
    ULONG MessageType;
    BOOLEAN Callback = FALSE;

    if (PsGetCurrentThread()->LpcExitThreadCalled) return STATUS_THREAD_IS_TERMINATING;

    Status = AlpcpCaptureLegacyHeader(LpcRequest, PreviousMode, &Header);
    if (!NT_SUCCESS(Status)) return Status;

    MessageType = Header.u2.s2.Type & ~LPC_KERNELMODE_MESSAGE;
    switch (MessageType)
    {
        case 0:
            MessageType = LPC_REQUEST;
            break;
        case LPC_REQUEST:
            Callback = TRUE;
            break;
        case LPC_CLIENT_DIED:
        case LPC_PORT_CLOSED:
        case LPC_EXCEPTION:
        case LPC_DEBUG_EVENT:
        case LPC_ERROR_EVENT:
            break;
        default:
            return STATUS_INVALID_PARAMETER;
    }

    Status = AlpcpVerifyDataInfo(&Header);
    if (!NT_SUCCESS(Status)) return Status;

    if (((USHORT)Header.u1.s1.TotalLength > Port->MaxMessageLength) ||
        ((USHORT)Header.u1.s1.TotalLength <= (USHORT)Header.u1.s1.DataLength))
    {
        return STATUS_PORT_MESSAGE_TOO_LONG;
    }

    Status = AlpcpBuildLegacyMessage(Port, &Header, LpcRequest + 1, MessageType, ALPC_MSG_STATE_SYNC | ALPC_MSG_STATE_LPC_MODE | ((PreviousMode == KernelMode) ? ALPC_MSG_STATE_KERNEL : 0), PreviousMode, &Message);
    if (!NT_SUCCESS(Status)) return Status;

    if (Callback)
        Status = AlpcpDeliverLegacyCallback(Port, Message, &Header);
    else
        Status = AlpcpDeliverLegacyMessage(Port, Message, TRUE);
    if (!NT_SUCCESS(Status)) return Status;

    Status = AlpcpWaitForReply(Message, WaitMode, FALSE, NULL, &ReceivedMessage);
    return AlpcpFinishSynchronousRequest(Message, ReceivedMessage, Status, LpcReply, PreviousMode);
}

NTSTATUS
NTAPI
LpcRequestWaitReplyPort(
    _In_ PVOID PortObject,
    _In_ PPORT_MESSAGE LpcRequest,
    _Out_ PPORT_MESSAGE LpcReply)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(PortObject);
    UNREFERENCED_PARAMETER(LpcRequest);
    UNREFERENCED_PARAMETER(LpcReply);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
LpcRequestWaitReplyPortEx(
    _In_ PVOID PortObject,
    _In_ PPORT_MESSAGE LpcRequest,
    _Out_ PPORT_MESSAGE LpcReply)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(PortObject);
    UNREFERENCED_PARAMETER(LpcRequest);
    UNREFERENCED_PARAMETER(LpcReply);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
LpcReplyWaitReplyPort(
    _In_ PVOID PortObject,
    _In_ KPROCESSOR_MODE WaitMode,
    _Inout_ PPORT_MESSAGE LpcReply)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(PortObject);
    UNREFERENCED_PARAMETER(WaitMode);
    UNREFERENCED_PARAMETER(LpcReply);
    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
LpcSendWaitReceivePort(
    _In_ PVOID PortObject,
    _In_ ULONG Flags,
    _In_opt_ PPORT_MESSAGE SendMessage,
    _Out_opt_ PPORT_MESSAGE ReceiveMessage,
    _Inout_opt_ PSIZE_T BufferLength,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    NTSTATUS Status;

    PAGED_CODE();

    Status = AlpcpSendWaitReceivePort(PortObject, Flags | ALPC_MSGFLG_LPC_MODE_INTERNAL, SendMessage, NULL, ReceiveMessage, BufferLength, NULL, Timeout, KernelMode);
    if (Status == STATUS_REQUEST_CANCELED) return STATUS_PORT_DISCONNECTED;
    if (Status == STATUS_MESSAGE_LOST) return STATUS_LPC_REPLY_LOST;
    return Status;
}

NTSTATUS
NTAPI
NtRequestWaitReplyPort(
    _In_ HANDLE PortHandle,
    _In_ PPORT_MESSAGE LpcRequest,
    _Out_ PPORT_MESSAGE LpcReply)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;

    PAGED_CODE();

    Status = AlpcpReferencePortByHandle(PortHandle, 0, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    Status = AlpcpLegacyRequestWaitReply(Port, LpcRequest, LpcReply, PreviousMode, PreviousMode);
    ObDereferenceObject(Port);
    return Status;
}

static
NTSTATUS
AlpcpLegacyRequest(
    _In_ PALPC_PORT Port,
    _In_ PPORT_MESSAGE LpcRequest,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status;
    PORT_MESSAGE Header;
    PKALPC_MESSAGE Message;
    ULONG MessageType;

    Status = AlpcpCaptureLegacyHeader(LpcRequest, PreviousMode, &Header);
    if (!NT_SUCCESS(Status)) return Status;

    if (Header.u2.s2.DataInfoOffset) return STATUS_INVALID_PARAMETER;

    MessageType = Header.u2.s2.Type & ~LPC_KERNELMODE_MESSAGE;
    switch (MessageType)
    {
        case 0:
        case LPC_DATAGRAM:
            MessageType = LPC_DATAGRAM;
            break;
        case LPC_CLIENT_DIED:
        case LPC_PORT_CLOSED:
        case LPC_EXCEPTION:
        case LPC_DEBUG_EVENT:
        case LPC_ERROR_EVENT:
            break;
        default:
            return STATUS_INVALID_PARAMETER;
    }

    if (((USHORT)Header.u1.s1.TotalLength > Port->MaxMessageLength) ||
        ((USHORT)Header.u1.s1.TotalLength <= (USHORT)Header.u1.s1.DataLength))
    {
        return STATUS_PORT_MESSAGE_TOO_LONG;
    }

    Status = AlpcpBuildLegacyMessage(Port, &Header, LpcRequest + 1, MessageType, ALPC_MSG_STATE_LPC_MODE | ((PreviousMode == KernelMode) ? ALPC_MSG_STATE_KERNEL : 0), PreviousMode, &Message);
    if (!NT_SUCCESS(Status)) return Status;

    return AlpcpDeliverLegacyMessage(Port, Message, FALSE);
}

NTSTATUS
NTAPI
LpcRequestPort(
    _In_ PVOID PortObject,
    _In_ PPORT_MESSAGE LpcMessage)
{
    PAGED_CODE();
    return AlpcpLegacyRequest(PortObject, LpcMessage, KernelMode);
}

NTSTATUS
NTAPI
NtRequestPort(
    _In_ HANDLE PortHandle,
    _In_ PPORT_MESSAGE LpcRequest)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;

    PAGED_CODE();

    Status = AlpcpReferencePortByHandle(PortHandle, 0, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    Status = AlpcpLegacyRequest(Port, LpcRequest, PreviousMode);
    ObDereferenceObject(Port);
    return Status;
}

static
NTSTATUS
AlpcpLegacyReply(
    _In_ PALPC_PORT Port,
    _In_ PPORT_MESSAGE ReplyMessage,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status;
    PORT_MESSAGE Header;
    PKALPC_MESSAGE Message;
    PVOID Buffer = NULL;
    ULONG CopyLength;

    Status = AlpcpCaptureLegacyHeader(ReplyMessage, PreviousMode, &Header);
    if (!NT_SUCCESS(Status)) return Status;

    if (!Header.MessageId) return STATUS_INVALID_PARAMETER;

    if (((USHORT)Header.u1.s1.TotalLength > Port->MaxMessageLength) ||
        ((USHORT)Header.u1.s1.TotalLength <= (USHORT)Header.u1.s1.DataLength))
    {
        return STATUS_PORT_MESSAGE_TOO_LONG;
    }

    CopyLength = (USHORT)Header.u1.s1.DataLength;
    if (CopyLength)
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, CopyLength, 'RcpA');
        if (!Buffer) return STATUS_NO_MEMORY;

        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                ProbeForRead(ReplyMessage + 1, CopyLength, sizeof(UCHAR));
                RtlCopyMemory(Buffer, ReplyMessage + 1, CopyLength);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                ExFreePoolWithTag(Buffer, 'RcpA');
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            RtlCopyMemory(Buffer, ReplyMessage + 1, CopyLength);
        }
    }

    AlpcpAcquireLock();
    Message = AlpcpFindPendingMessageForReply(Port, Header.MessageId, &Header.ClientId);
    if (!Message ||
        (Message->State & ALPC_MSG_STATE_CONNECTION) ||
        (Message->PortMessage.CallbackId != Header.CallbackId) ||
        Message->ActiveCallback)
    {
        AlpcpReleaseLock();
        if (Buffer) ExFreePoolWithTag(Buffer, 'RcpA');
        return STATUS_REPLY_MESSAGE_MISMATCH;
    }

    if (sizeof(PORT_MESSAGE) + CopyLength > Message->AllocatedLength)
    {
        AlpcpReleaseLock();
        if (Buffer) ExFreePoolWithTag(Buffer, 'RcpA');
        return STATUS_PORT_MESSAGE_TOO_LONG;
    }

    Message->PortMessage.u1.Length = Header.u1.Length;
    Message->PortMessage.u2.ZeroInit = 0;
    Message->PortMessage.u2.s2.Type = LPC_REPLY;
    Message->PortMessage.ClientViewSize = Header.ClientViewSize;
    if (CopyLength) RtlCopyMemory(&Message->PortMessage + 1, Buffer, CopyLength);
    if (Message->CallbackParent &&
        Message->CallbackParent->ActiveCallback == Message)
    {
        Message->CallbackParent->ActiveCallback = NULL;
        Message->CallbackParent = NULL;
    }
    AlpcpCompleteReply(Message);
    AlpcpReleaseLock();

    if (Buffer) ExFreePoolWithTag(Buffer, 'RcpA');
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtReplyPort(
    _In_ HANDLE PortHandle,
    _In_ PPORT_MESSAGE ReplyMessage)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;

    PAGED_CODE();

    Status = AlpcpReferencePortByHandle(PortHandle, 0, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    Status = AlpcpLegacyReply(Port, ReplyMessage, PreviousMode);
    ObDereferenceObject(Port);
    return Status;
}

static
NTSTATUS
AlpcpLegacyReceive(
    _In_ PALPC_PORT Port,
    _Out_opt_ PVOID *PortContext,
    _Out_ PPORT_MESSAGE ReceiveMessage,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status;
    PKALPC_MESSAGE Message;
    PVOID Context = NULL;
    BOOLEAN Pending = FALSE;
    ULONG CopyLength;

    Status = AlpcpWaitForMessage(Port, AlpcpLegacyWaitMode(PreviousMode), FALSE, Timeout, &Message);
    if (Status != STATUS_SUCCESS) return Status;

    AlpcpAcquireLock();
    if (Message->State & (ALPC_MSG_STATE_CONNECTION | ALPC_MSG_STATE_SYNC))
    {
        AlpcpMakePending(Port, Message);
        Message->ServerThread = PsGetCurrentThread();
        Pending = TRUE;
    }
    if (!(Message->State & ALPC_MSG_STATE_CONNECTION)) Context = Message->PortContext;
    CopyLength = AlpcpLegacyCopyLength(&Message->PortMessage);
    AlpcpReleaseLock();
    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(ReceiveMessage, sizeof(PORT_MESSAGE) + CopyLength, sizeof(ULONG));
            RtlCopyMemory(ReceiveMessage, &Message->PortMessage, sizeof(PORT_MESSAGE) + CopyLength);
            ReceiveMessage->u2.s2.Type &= ALPCP_LEGACY_MESSAGE_TYPE_MASK;
            if (PortContext)
            {
                ProbeForWritePointer(PortContext);
                *PortContext = Context;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        RtlCopyMemory(ReceiveMessage, &Message->PortMessage, sizeof(PORT_MESSAGE) + CopyLength);
        ReceiveMessage->u2.s2.Type &= ALPCP_LEGACY_MESSAGE_TYPE_MASK;
        if (PortContext) *PortContext = Context;
    }

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

static
NTSTATUS
AlpcpReferenceLegacyReceivePort(
    _In_ PALPC_PORT Port,
    _Out_ PALPC_PORT *ReceivePort)
{
    PALPC_COMMUNICATION_INFO Info;
    PALPC_PORT Target = NULL;

    *ReceivePort = NULL;

    AlpcpAcquireLock();
    if ((AlpcpPortType(Port) == ALPC_PORT_TYPE_CLIENT) ||
        (AlpcpPortType(Port) == ALPC_PORT_TYPE_CONNECTION))
    {
        Target = Port;
    }
    else if (AlpcpPortType(Port) == ALPC_PORT_TYPE_SERVER)
    {
        Info = Port->CommunicationInfo;
        if (Info) Target = Info->ConnectionPort;
    }

    if (Target && ObReferenceObjectSafe(Target))
    {
        *ReceivePort = Target;
    }
    AlpcpReleaseLock();

    return *ReceivePort ? STATUS_SUCCESS : STATUS_PORT_DISCONNECTED;
}

NTSTATUS
NTAPI
NtReplyWaitReceivePortEx(
    _In_ HANDLE PortHandle,
    _Out_opt_ PVOID *PortContext,
    _In_opt_ PPORT_MESSAGE ReplyMessage,
    _Out_ PPORT_MESSAGE ReceiveMessage,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port, ReceivePort;
    LARGE_INTEGER CapturedTimeout;

    PAGED_CODE();

    if ((PreviousMode != KernelMode) && Timeout)
    {
        _SEH2_TRY
        {
            ProbeForReadLargeInteger(Timeout);
            CapturedTimeout = *(volatile LARGE_INTEGER*)Timeout;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
        Timeout = &CapturedTimeout;
    }

    Status = AlpcpReferencePortByHandle(PortHandle, 0, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    Status = AlpcpReferenceLegacyReceivePort(Port, &ReceivePort);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Port);
        return Status;
    }

    if (ReplyMessage)
    {
        Status = AlpcpLegacyReply(Port, ReplyMessage, PreviousMode);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(ReceivePort);
            ObDereferenceObject(Port);
            return Status;
        }
    }

    Status = AlpcpLegacyReceive(ReceivePort, PortContext, ReceiveMessage, Timeout, PreviousMode);
    ObDereferenceObject(ReceivePort);
    ObDereferenceObject(Port);
    return Status;
}

NTSTATUS
NTAPI
NtReplyWaitReceivePort(
    _In_ HANDLE PortHandle,
    _Out_opt_ PVOID *PortContext,
    _In_opt_ PPORT_MESSAGE ReplyMessage,
    _Out_ PPORT_MESSAGE ReceiveMessage)
{
    return NtReplyWaitReceivePortEx(PortHandle, PortContext, ReplyMessage, ReceiveMessage, NULL);
}

NTSTATUS
NTAPI
NtReplyWaitReplyPort(
    _In_ HANDLE PortHandle,
    _Inout_ PPORT_MESSAGE ReplyMessage)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;
    PORT_MESSAGE Header;
    PKALPC_MESSAGE Callback, Parent, ReceivedMessage = NULL;
    PETHREAD Thread = PsGetCurrentThread();

    PAGED_CODE();

    Status = AlpcpCaptureLegacyHeader(ReplyMessage, PreviousMode, &Header);
    if (!NT_SUCCESS(Status)) return Status;

    Status = AlpcpReferencePortByHandle(PortHandle, 0, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    AlpcpAcquireLock();
    Callback = AlpcpFindPendingMessageForReply(Port, Header.MessageId, &Header.ClientId);
    if (!Callback ||
        !(Callback->State & ALPC_MSG_STATE_CALLBACK) ||
        (Callback->PortMessage.CallbackId != Header.CallbackId) ||
        (Callback->ServerThread != Thread) ||
        !Callback->CallbackParent)
    {
        AlpcpReleaseLock();
        ObDereferenceObject(Port);
        return STATUS_REPLY_MESSAGE_MISMATCH;
    }
    Parent = Callback->CallbackParent;
    AlpcpReleaseLock();

    Status = AlpcpLegacyReply(Port, ReplyMessage, PreviousMode);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Port);
        return Status;
    }

    AlpcpAcquireLock();
    Thread->AlpcMessage = Parent;
    Thread->AlpcMessageId = Parent->PortMessage.MessageId;
    AlpcpReleaseLock();

    Status = AlpcpWaitForReply(Parent, AlpcpLegacyWaitMode(PreviousMode), FALSE, NULL, &ReceivedMessage);
    Status = AlpcpFinishSynchronousRequest(Parent, ReceivedMessage, Status, ReplyMessage, PreviousMode);
    ObDereferenceObject(Port);
    return Status;
}

NTSTATUS
NTAPI
NtListenPort(
    _In_ HANDLE PortHandle,
    _Out_ PPORT_MESSAGE ConnectMessage)
{
    NTSTATUS Status;

    PAGED_CODE();

    for (;;)
    {
        Status = NtReplyWaitReceivePortEx(PortHandle, NULL, NULL, ConnectMessage, NULL);
        if (!NT_SUCCESS(Status)) return Status;
        if ((ConnectMessage->u2.s2.Type & ~LPC_KERNELMODE_MESSAGE) == LPC_CONNECTION_REQUEST)
        {
            return STATUS_SUCCESS;
        }
    }
}

static
NTSTATUS
AlpcpCreateLegacyPort(
    _Out_ PHANDLE PortHandle,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG MaxConnectionInfoLength,
    _In_ ULONG MaxMessageLength,
    _In_ BOOLEAN Waitable)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;
    HANDLE Handle;
    ULONG Limit = LPC_MAX_MESSAGE_LENGTH;
    ULONG InfoLimit = LPC_MAX_MESSAGE_LENGTH - sizeof(PORT_MESSAGE) - sizeof(LPCP_CONNECTION_MESSAGE);

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(PortHandle);
            ProbeForRead(ObjectAttributes, sizeof(*ObjectAttributes), sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    if (InfoLimit < MaxConnectionInfoLength) return STATUS_INVALID_PARAMETER_3;
    if (Limit < MaxMessageLength) return STATUS_INVALID_PARAMETER_4;
    if (!MaxMessageLength) MaxMessageLength = Limit;

    Status = AlpcpCreatePort(&Port, PreviousMode, ObjectAttributes, NULL, ALPC_PORT_TYPE_CONNECTION, ALPC_PORT_FLAG_LPC | (Waitable ? ALPC_PORT_FLAG_WAITABLE : 0), MaxConnectionInfoLength, MaxMessageLength);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObInsertObject(Port, NULL, PORT_ALL_ACCESS, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status)) return Status;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *PortHandle = Handle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ObCloseHandle(Handle, PreviousMode);
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *PortHandle = Handle;
    }
    return Status;
}

NTSTATUS
NTAPI
NtCreatePort(
    _Out_ PHANDLE PortHandle,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG MaxConnectInfoLength,
    _In_ ULONG MaxDataLength,
    _In_ ULONG MaxPoolUsage)
{
    UNREFERENCED_PARAMETER(MaxPoolUsage);
    return AlpcpCreateLegacyPort(PortHandle, ObjectAttributes, MaxConnectInfoLength, MaxDataLength, FALSE);
}

NTSTATUS
NTAPI
NtCreateWaitablePort(
    _Out_ PHANDLE PortHandle,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_ ULONG MaxConnectInfoLength,
    _In_ ULONG MaxDataLength,
    _In_ ULONG MaxPoolUsage)
{
    UNREFERENCED_PARAMETER(MaxPoolUsage);
    return AlpcpCreateLegacyPort(PortHandle, ObjectAttributes, MaxConnectInfoLength, MaxDataLength, TRUE);
}

NTSTATUS
NTAPI
AlpcpCheckServerSid(
    _In_ PALPC_PORT Port,
    _In_ PSID ServerSid)
{
    NTSTATUS Status;
    PTOKEN Token;
    PTOKEN_USER TokenUserInfo;

    if (!Port->OwnerProcess) return STATUS_SERVER_SID_MISMATCH;

    Token = PsReferencePrimaryToken(Port->OwnerProcess);
    Status = SeQueryInformationToken(Token, TokenUser, (PVOID*)&TokenUserInfo);
    PsDereferencePrimaryToken(Token);
    if (!NT_SUCCESS(Status)) return Status;

    if (!RtlEqualSid(ServerSid, TokenUserInfo->User.Sid)) Status = STATUS_SERVER_SID_MISMATCH;
    ExFreePoolWithTag(TokenUserInfo, TAG_SE);
    return Status;
}

NTSTATUS
NTAPI
NtSecureConnectPort(
    _Out_ PHANDLE PortHandle,
    _In_ PUNICODE_STRING PortName,
    _In_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _Inout_opt_ PPORT_VIEW ClientView,
    _In_opt_ PSID ServerSid,
    _Inout_opt_ PREMOTE_PORT_VIEW ServerView,
    _Out_opt_ PULONG MaxMessageLength,
    _Inout_opt_ PVOID ConnectionInformation,
    _Inout_opt_ PULONG ConnectionInformationLength)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PETHREAD Thread = PsGetCurrentThread();
    SECURITY_QUALITY_OF_SERVICE CapturedQos;
    PORT_VIEW CapturedClientView;
    PSID CapturedServerSid = NULL;
    ULONG ConnectionInfoLength = 0;
    PALPC_PORT Port, ClientPort = NULL;
    PKALPC_MESSAGE Message = NULL;
    PVOID SectionToMap = NULL;
    LARGE_INTEGER SectionOffset;
    HANDLE Handle;
    ULONG ReplyLength;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(PortHandle);
            ProbeForRead(SecurityQos, sizeof(*SecurityQos), sizeof(ULONG));
            CapturedQos = *(volatile SECURITY_QUALITY_OF_SERVICE*)SecurityQos;
            if (ClientView)
            {
                ProbeForWrite(ClientView, sizeof(*ClientView), sizeof(ULONG));
                CapturedClientView = *(volatile PORT_VIEW*)ClientView;
                if (CapturedClientView.Length != sizeof(CapturedClientView))
                {
                    _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
                }
            }
            if (ServerView)
            {
                ProbeForWrite(ServerView, sizeof(*ServerView), sizeof(ULONG));
                if (((volatile REMOTE_PORT_VIEW*)ServerView)->Length != sizeof(*ServerView))
                {
                    _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
                }
            }
            if (MaxMessageLength) ProbeForWriteUlong(MaxMessageLength);
            if (ConnectionInformationLength)
            {
                ProbeForWriteUlong(ConnectionInformationLength);
                ConnectionInfoLength = *(volatile ULONG*)ConnectionInformationLength;
            }
            if (ConnectionInformation)
            {
                ProbeForWrite(ConnectionInformation, ConnectionInfoLength, sizeof(UCHAR));
            }
            if (ServerSid)
            {
                Status = SepCaptureSid(ServerSid, PreviousMode, PagedPool, TRUE, &CapturedServerSid);
                if (!NT_SUCCESS(Status)) _SEH2_YIELD(return Status);
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
        CapturedQos = *SecurityQos;
        if (ClientView)
        {
            if (ClientView->Length != sizeof(*ClientView)) return STATUS_INVALID_PARAMETER;
            CapturedClientView = *ClientView;
        }
        if (ServerView && (ServerView->Length != sizeof(*ServerView))) return STATUS_INVALID_PARAMETER;
        if (ConnectionInformationLength) ConnectionInfoLength = *ConnectionInformationLength;
        CapturedServerSid = ServerSid;
    }

    Status = ObReferenceObjectByName(PortName, 0, NULL, PORT_CONNECT, AlpcPortObjectType, PreviousMode, NULL, (PVOID*)&Port);
    if (!NT_SUCCESS(Status))
    {
        if (CapturedServerSid && (CapturedServerSid != ServerSid)) SepReleaseSid(CapturedServerSid, PreviousMode, TRUE);
        return Status;
    }

    if (AlpcpPortType(Port) != ALPC_PORT_TYPE_CONNECTION)
    {
        Status = STATUS_INVALID_PORT_HANDLE;
    }
    else if (CapturedServerSid)
    {
        Status = AlpcpCheckServerSid(Port, CapturedServerSid);
    }
    if (CapturedServerSid && (CapturedServerSid != ServerSid)) SepReleaseSid(CapturedServerSid, PreviousMode, TRUE);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Port);
        return Status;
    }

    Status = AlpcpCreatePort(&ClientPort, PreviousMode, NULL, NULL, ALPC_PORT_TYPE_CLIENT, ALPC_PORT_FLAG_LPC, Port->MaxConnectionInfoLength, Port->MaxMessageLength);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Port);
        return Status;
    }

    ClientPort->SecurityQos = CapturedQos;
    if (CapturedQos.ContextTrackingMode == SECURITY_DYNAMIC_TRACKING)
    {
        ClientPort->Flags |= ALPC_PORT_FLAG_DYNAMIC_SECURITY;
    }
    else
    {
        Status = SeCreateClientSecurity(Thread, &CapturedQos, FALSE, &ClientPort->StaticSecurity);
        if (!NT_SUCCESS(Status)) goto Failure;
    }

    if (ClientView)
    {
        Status = ObReferenceObjectByHandle(CapturedClientView.SectionHandle, SECTION_MAP_READ | SECTION_MAP_WRITE, MmSectionObjectType, PreviousMode, &SectionToMap, NULL);
        if (!NT_SUCCESS(Status)) goto Failure;

        SectionOffset.QuadPart = CapturedClientView.SectionOffset;
        Status = MmMapViewOfSection(SectionToMap, PsGetCurrentProcess(), &ClientPort->ClientSectionBase, 0, 0, &SectionOffset, &CapturedClientView.ViewSize, ViewUnmap, 0, PAGE_READWRITE);
        if (!NT_SUCCESS(Status)) goto Failure;
        CapturedClientView.SectionOffset = SectionOffset.LowPart;
        CapturedClientView.ViewBase = ClientPort->ClientSectionBase;
    }

    if (ConnectionInfoLength > Port->MaxConnectionInfoLength)
    {
        ConnectionInfoLength = Port->MaxConnectionInfoLength;
    }

    Message = AlpcpAllocateMessage(Port->MaxMessageLength, Port);
    if (!Message)
    {
        Status = STATUS_NO_MEMORY;
        goto Failure;
    }

    Message->PortMessage.u1.s1.DataLength = (CSHORT)ConnectionInfoLength;
    Message->PortMessage.u1.s1.TotalLength = (CSHORT)(sizeof(PORT_MESSAGE) + ConnectionInfoLength);
    Message->PortMessage.u2.s2.Type = LPC_CONNECTION_REQUEST;
    Message->PortMessage.ClientId = Thread->Cid;
    Message->PortMessage.ClientViewSize = ClientView ? CapturedClientView.ViewSize : 0;
    Message->State = ALPC_MSG_STATE_CONNECTION | ALPC_MSG_STATE_SYNC | ALPC_MSG_STATE_LPC_MODE;
    AlpcpSetMessageSenderPort(Message, ClientPort);
    Message->Connection.SecurityQos = CapturedQos;
    if (ClientView) Message->Connection.ClientView = CapturedClientView;
    Message->Connection.SectionToMap = SectionToMap;
    SectionToMap = NULL;
    ObReferenceObject(ClientPort);
    Message->Connection.ClientPort = ClientPort;

    if (ConnectionInformation && ConnectionInfoLength)
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                RtlCopyMemory(&Message->PortMessage + 1, ConnectionInformation, ConnectionInfoLength);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
            if (!NT_SUCCESS(Status)) goto Failure;
        }
        else
        {
            RtlCopyMemory(&Message->PortMessage + 1, ConnectionInformation, ConnectionInfoLength);
        }
    }

    AlpcpAcquireLock();
    if (Port->Flags & (ALPC_PORT_FLAG_NAME_DELETED | ALPC_PORT_FLAG_CLOSED))
    {
        AlpcpReleaseLock();
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
        goto Failure;
    }
    Message->OwnerPort = Port;
    Message->PortMessage.MessageId = AlpcpNextMessageId++;
    if (!AlpcpNextMessageId) AlpcpNextMessageId = 1;
    Message->WaitingThread = Thread;
    Thread->AlpcMessage = Message;
    Thread->AlpcMessageId = Message->PortMessage.MessageId;
    AlpcpQueueMessage(Port, Message);
    AlpcpReleaseLock();

    Status = AlpcpWaitForReply(Message, PreviousMode, FALSE, NULL, NULL);
    if (Status != STATUS_SUCCESS)
    {
        Message = NULL;
        goto Failure;
    }

    if (!(Message->State & ALPC_MSG_STATE_ACCEPTED))
    {
        if (Message->State & ALPC_MSG_STATE_DISCONNECTED)
        {
            Status = STATUS_OBJECT_NAME_NOT_FOUND;
        }
        else
        {
            Status = STATUS_PORT_CONNECTION_REFUSED;
        }
        if (Port->Flags & ALPC_PORT_FLAG_NAME_DELETED) Status = STATUS_OBJECT_NAME_NOT_FOUND;
        goto Failure;
    }

    ReplyLength = (USHORT)Message->PortMessage.u1.s1.DataLength;
    if (ReplyLength < ConnectionInfoLength) ConnectionInfoLength = ReplyLength;

    Status = ObInsertObject(ClientPort, NULL, PORT_ALL_ACCESS, 0, NULL, &Handle);
    ClientPort = NULL;
    if (!NT_SUCCESS(Status)) goto Failure;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *PortHandle = Handle;
            if (MaxMessageLength) *MaxMessageLength = Port->MaxMessageLength;
            if (ClientView) *ClientView = Message->Connection.ClientView;
            if (ServerView) *ServerView = Message->Connection.ServerView;
            if (ConnectionInformationLength) *ConnectionInformationLength = ConnectionInfoLength;
            if (ConnectionInformation && ConnectionInfoLength)
            {
                RtlCopyMemory(ConnectionInformation, &Message->PortMessage + 1, ConnectionInfoLength);
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        if (!NT_SUCCESS(Status)) ObCloseHandle(Handle, PreviousMode);
    }
    else
    {
        *PortHandle = Handle;
        if (MaxMessageLength) *MaxMessageLength = Port->MaxMessageLength;
        if (ClientView) *ClientView = Message->Connection.ClientView;
        if (ServerView) *ServerView = Message->Connection.ServerView;
        if (ConnectionInformationLength) *ConnectionInformationLength = ConnectionInfoLength;
        if (ConnectionInformation && ConnectionInfoLength)
        {
            RtlCopyMemory(ConnectionInformation, &Message->PortMessage + 1, ConnectionInfoLength);
        }
    }

    AlpcpFreeMessage(Message);
    ObDereferenceObject(Port);
    return Status;

Failure:
    if (Message) AlpcpFreeMessage(Message);
    if (SectionToMap) ObDereferenceObject(SectionToMap);
    if (ClientPort) ObDereferenceObject(ClientPort);
    ObDereferenceObject(Port);
    return Status;
}

NTSTATUS
NTAPI
NtConnectPort(
    _Out_ PHANDLE PortHandle,
    _In_ PUNICODE_STRING PortName,
    _In_ PSECURITY_QUALITY_OF_SERVICE SecurityQos,
    _Inout_opt_ PPORT_VIEW ClientView,
    _Inout_opt_ PREMOTE_PORT_VIEW ServerView,
    _Out_opt_ PULONG MaxMessageLength,
    _Inout_opt_ PVOID ConnectionInformation,
    _Inout_opt_ PULONG ConnectionInformationLength)
{
    return NtSecureConnectPort(PortHandle, PortName, SecurityQos, ClientView, NULL, ServerView, MaxMessageLength, ConnectionInformation, ConnectionInformationLength);
}

NTSTATUS
NTAPI
AlpcpAcceptLegacyConnection(
    _In_ PKALPC_MESSAGE Message,
    _In_ PALPC_PORT ConnectionPort,
    _In_opt_ PVOID PortContext,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PALPC_PORT *OutServerPort,
    _Out_ PHANDLE OutHandle)
{
    NTSTATUS Status;
    PALPC_PORT ServerPort;
    PALPC_COMMUNICATION_INFO Info;
    LARGE_INTEGER SectionOffset;
    HANDLE Handle;

    Status = AlpcpCreatePort(&ServerPort, PreviousMode, NULL, NULL, ALPC_PORT_TYPE_SERVER, ALPC_PORT_FLAG_LPC, ConnectionPort->MaxConnectionInfoLength, ConnectionPort->MaxMessageLength);
    if (!NT_SUCCESS(Status)) return Status;

    ServerPort->PortContext = PortContext;

    Info = ExAllocatePoolWithTag(PagedPool, sizeof(*Info), 'IcpA');
    if (!Info)
    {
        ObDereferenceObject(ServerPort);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Info, sizeof(*Info));
    ObReferenceObject(ConnectionPort);
    Info->ConnectionPort = ConnectionPort;
    Info->ServerCommunicationPort = ServerPort;
    Info->ClientCommunicationPort = Message->Connection.ClientPort;
    Info->ReferenceCount = 2;
    InitializeListHead(&Info->CommunicationList);

    if (Message->Connection.SectionToMap)
    {
        SectionOffset.QuadPart = Message->Connection.ClientView.SectionOffset;
        Status = MmMapViewOfSection(Message->Connection.SectionToMap, PsGetCurrentProcess(), &ServerPort->ClientSectionBase, 0, 0, &SectionOffset, &Message->Connection.ClientView.ViewSize, ViewUnmap, 0, PAGE_READWRITE);
        if (!NT_SUCCESS(Status))
        {
            ObDereferenceObject(ConnectionPort);
            ExFreePoolWithTag(Info, 'IcpA');
            ObDereferenceObject(ServerPort);
            return Status;
        }
        Message->Connection.ClientView.SectionOffset = SectionOffset.LowPart;
        Message->Connection.ClientView.ViewRemoteBase = ServerPort->ClientSectionBase;
    }

    ObReferenceObject(ServerPort);
    Status = ObInsertObject(ServerPort, NULL, PORT_ALL_ACCESS, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(ConnectionPort);
        ExFreePoolWithTag(Info, 'IcpA');
        ObDereferenceObject(ServerPort);
        return Status;
    }

    AlpcpAcquireLock();
    if (!PortContext) ServerPort->PortContext = Handle;
    ServerPort->CommunicationInfo = Info;
    Message->Connection.ClientPort->CommunicationInfo = Info;
    InsertTailList(&ConnectionPort->CommunicationPorts, &Info->CommunicationList);
    ServerPort->Creator = PsGetCurrentThread()->Cid;
    Message->Connection.ClientPort->Creator = Message->PortMessage.ClientId;
    AlpcpReleaseLock();

    *OutServerPort = ServerPort;
    *OutHandle = Handle;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtAcceptConnectPort(
    _Out_ PHANDLE PortHandle,
    _In_opt_ PVOID PortContext,
    _In_ PPORT_MESSAGE ReplyMessage,
    _In_ BOOLEAN AcceptConnection,
    _Inout_opt_ PPORT_VIEW ServerView,
    _Out_opt_ PREMOTE_PORT_VIEW ClientView)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PORT_MESSAGE Header;
    ULONG ConnectionInfoLength;
    PALPC_PORT ConnectionPort, ServerPort = NULL;
    PKALPC_MESSAGE Message;
    PEPROCESS ClientProcess;
    PETHREAD ClientThread;
    HANDLE Handle = NULL;
    PVOID Buffer = NULL;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(PortHandle);
            ProbeForRead(ReplyMessage, sizeof(*ReplyMessage), sizeof(ULONG));
            Header = *(volatile PORT_MESSAGE*)ReplyMessage;
            ConnectionInfoLength = (USHORT)Header.u1.s1.DataLength;
            ProbeForRead(ReplyMessage + 1, ConnectionInfoLength, 1);
            if (ServerView)
            {
                ProbeForWrite(ServerView, sizeof(*ServerView), sizeof(ULONG));
                if (((volatile PORT_VIEW*)ServerView)->Length != sizeof(*ServerView))
                {
                    _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
                }
            }
            if (ClientView)
            {
                ProbeForWrite(ClientView, sizeof(*ClientView), sizeof(ULONG));
                if (((volatile REMOTE_PORT_VIEW*)ClientView)->Length != sizeof(*ClientView))
                {
                    _SEH2_YIELD(return STATUS_INVALID_PARAMETER);
                }
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
        Header = *ReplyMessage;
        ConnectionInfoLength = (USHORT)Header.u1.s1.DataLength;
        if (ServerView && (ServerView->Length != sizeof(*ServerView))) return STATUS_INVALID_PARAMETER;
        if (ClientView && (ClientView->Length != sizeof(*ClientView))) return STATUS_INVALID_PARAMETER;
    }

    if (ConnectionInfoLength)
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, ConnectionInfoLength, 'RcpA');
        if (!Buffer) return STATUS_NO_MEMORY;
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                RtlCopyMemory(Buffer, ReplyMessage + 1, ConnectionInfoLength);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                ExFreePoolWithTag(Buffer, 'RcpA');
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            RtlCopyMemory(Buffer, ReplyMessage + 1, ConnectionInfoLength);
        }
    }

    Status = PsLookupProcessThreadByCid(&Header.ClientId, &ClientProcess, &ClientThread);
    if (!NT_SUCCESS(Status))
    {
        if (Buffer) ExFreePoolWithTag(Buffer, 'RcpA');
        return Status;
    }
    ObDereferenceObject(ClientProcess);

    AlpcpAcquireLock();
    Message = ClientThread->AlpcMessage;
    if (!Message ||
        !Header.MessageId ||
        (ClientThread->AlpcMessageId != Header.MessageId) ||
        (Message->WaitingThread != ClientThread) ||
        !(Message->State & ALPC_MSG_STATE_CONNECTION) ||
        !(Message->State & ALPC_MSG_STATE_PENDING) ||
        (Message->State & (ALPC_MSG_STATE_ACCEPTED | ALPC_MSG_STATE_REFUSED)) ||
        !Message->QueuePort ||
        (Message->QueuePort->OwnerProcess != PsGetCurrentProcess()))
    {
        AlpcpReleaseLock();
        ObDereferenceObject(ClientThread);
        if (Buffer) ExFreePoolWithTag(Buffer, 'RcpA');
        return STATUS_REPLY_MESSAGE_MISMATCH;
    }
    ConnectionPort = Message->QueuePort;
    ObReferenceObject(ConnectionPort);
    AlpcpRemovePending(Message);
    Message->State |= ALPC_MSG_STATE_ACCEPT_IN_PROGRESS;
    AlpcpReleaseLock();

    if (ConnectionInfoLength > ConnectionPort->MaxConnectionInfoLength)
    {
        ConnectionInfoLength = ConnectionPort->MaxConnectionInfoLength;
    }
    Message->PortMessage.u1.s1.DataLength = (CSHORT)ConnectionInfoLength;
    Message->PortMessage.u1.s1.TotalLength = (CSHORT)(sizeof(PORT_MESSAGE) + ConnectionInfoLength);
    Message->PortMessage.u2.s2.Type = LPC_REPLY;
    Message->PortMessage.u2.s2.DataInfoOffset = 0;
    Message->PortMessage.ClientViewSize = 0;
    if (ConnectionInfoLength) RtlCopyMemory(&Message->PortMessage + 1, Buffer, ConnectionInfoLength);
    if (Buffer) ExFreePoolWithTag(Buffer, 'RcpA');

    if (!AcceptConnection)
    {
        AlpcpAcquireLock();
        Message->State &= ~ALPC_MSG_STATE_ACCEPT_IN_PROGRESS;
        AlpcpCompleteWithStatus(Message, ALPC_MSG_STATE_REFUSED, STATUS_PORT_CONNECTION_REFUSED);
        AlpcpReleaseLock();
        ObDereferenceObject(ConnectionPort);
        ObDereferenceObject(ClientThread);
        return STATUS_SUCCESS;
    }

    Status = AlpcpAcceptLegacyConnection(Message, ConnectionPort, PortContext, PreviousMode, &ServerPort, &Handle);
    if (!NT_SUCCESS(Status))
    {
        AlpcpAcquireLock();
        Message->State &= ~ALPC_MSG_STATE_ACCEPT_IN_PROGRESS;
        AlpcpCompleteWithStatus(Message, ALPC_MSG_STATE_REFUSED, STATUS_PORT_CONNECTION_REFUSED);
        AlpcpReleaseLock();
        ObDereferenceObject(ConnectionPort);
        ObDereferenceObject(ClientThread);
        return Status;
    }

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *PortHandle = Handle;
            if (ClientView)
            {
                ClientView->ViewBase = Message->Connection.ClientView.ViewRemoteBase;
                ClientView->ViewSize = Message->Connection.ClientView.ViewSize;
            }
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *PortHandle = Handle;
        if (ClientView)
        {
            ClientView->ViewBase = Message->Connection.ClientView.ViewRemoteBase;
            ClientView->ViewSize = Message->Connection.ClientView.ViewSize;
        }
    }

    AlpcpAcquireLock();
    Message->State &= ~ALPC_MSG_STATE_ACCEPT_IN_PROGRESS;
    Message->State |= ALPC_MSG_STATE_ACCEPTED;
    AlpcpSignalWaiter(ClientThread);
    AlpcpReleaseLock();
    ObDereferenceObject(ClientThread);

    ObDereferenceObject(ServerPort);
    ObDereferenceObject(ConnectionPort);
    return Status;
}

NTSTATUS
NTAPI
NtCompleteConnectPort(
    _In_ HANDLE PortHandle)
{
    UNREFERENCED_PARAMETER(PortHandle);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtImpersonateClientOfPort(
    _In_ HANDLE PortHandle,
    _In_ PPORT_MESSAGE ClientMessage)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PORT_MESSAGE Header;
    PALPC_PORT Port, ClientPort = NULL;
    PETHREAD ClientThread = NULL;
    PKALPC_MESSAGE Message;
    SECURITY_CLIENT_CONTEXT ClientContext;
    SECURITY_QUALITY_OF_SERVICE Qos;
    BOOLEAN Dynamic;

    PAGED_CODE();

    Status = AlpcpCaptureLegacyHeader(ClientMessage, PreviousMode, &Header);
    if (!NT_SUCCESS(Status)) return Status;

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_ALL_ACCESS, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    if (AlpcpPortType(Port) != ALPC_PORT_TYPE_SERVER)
    {
        ObDereferenceObject(Port);
        return STATUS_INVALID_PORT_HANDLE;
    }

    Status = PsLookupProcessThreadByCid(&Header.ClientId, NULL, &ClientThread);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Port);
        return Status;
    }

    AlpcpAcquireLock();
    Message = AlpcpFindPendingMessageForReply(Port, Header.MessageId, &Header.ClientId);
    if (!Message || !Header.MessageId)
    {
        Status = STATUS_REPLY_MESSAGE_MISMATCH;
    }
    else if (!Port->CommunicationInfo || !Port->CommunicationInfo->ClientCommunicationPort ||
             !ObReferenceObjectSafe(Port->CommunicationInfo->ClientCommunicationPort))
    {
        Status = STATUS_PORT_DISCONNECTED;
    }
    else
    {
        ClientPort = Port->CommunicationInfo->ClientCommunicationPort;
        Dynamic = (ClientPort->Flags & ALPC_PORT_FLAG_DYNAMIC_SECURITY) != 0;
        Qos = ClientPort->SecurityQos;
    }
    AlpcpReleaseLock();

    if (NT_SUCCESS(Status))
    {
        if (!Dynamic)
        {
            Status = SeImpersonateClientEx(&ClientPort->StaticSecurity, NULL);
        }
        else
        {
            Status = SeCreateClientSecurity(ClientThread, &Qos, FALSE, &ClientContext);
            if (NT_SUCCESS(Status))
            {
                Status = SeImpersonateClientEx(&ClientContext, NULL);
                SeDeleteClientSecurity(&ClientContext);
            }
        }
        ObDereferenceObject(ClientPort);
    }

    ObDereferenceObject(ClientThread);
    ObDereferenceObject(Port);
    return Status;
}

static
NTSTATUS
AlpcpCopyRequestData(
    _In_ BOOLEAN Write,
    _In_ HANDLE PortHandle,
    _In_ PPORT_MESSAGE Message,
    _In_ ULONG Index,
    _In_ PVOID Buffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PULONG ReturnLength)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PORT_MESSAGE Header;
    PALPC_PORT Port;
    PETHREAD ClientThread = NULL;
    PKALPC_MESSAGE Pending;
    PLPCP_DATA_INFO DataInfo;
    PVOID DataInfoBaseAddress;
    SIZE_T LocalReturnLength = 0;

    PAGED_CODE();

    Status = AlpcpCaptureLegacyHeader(Message, PreviousMode, &Header);
    if (!NT_SUCCESS(Status)) return Status;
    if (Header.u2.s2.DataInfoOffset == 0) return STATUS_INVALID_PARAMETER;

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_ALL_ACCESS, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    Status = PsLookupProcessThreadByCid(&Header.ClientId, NULL, &ClientThread);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(Port);
        return Status;
    }

    AlpcpAcquireLock();
    Pending = AlpcpFindPendingMessageForReply(Port, Header.MessageId, &Header.ClientId);
    if (!Pending || !Header.MessageId || !(Pending->State & ALPC_MSG_STATE_DATA_INFO))
    {
        AlpcpReleaseLock();
        Status = STATUS_REPLY_MESSAGE_MISMATCH;
        goto Cleanup;
    }

    DataInfo = (PLPCP_DATA_INFO)((ULONG_PTR)&Pending->PortMessage + Pending->PortMessage.u2.s2.DataInfoOffset);
    if ((Index >= DataInfo->NumberOfEntries) ||
        (BufferLength > DataInfo->Entries[Index].DataLength))
    {
        AlpcpReleaseLock();
        Status = STATUS_INVALID_PARAMETER;
        goto Cleanup;
    }
    DataInfoBaseAddress = DataInfo->Entries[Index].BaseAddress;
    AlpcpReleaseLock();

    if (Write)
    {
        Status = MmCopyVirtualMemory(PsGetCurrentProcess(), Buffer, (PEPROCESS)ClientThread->ThreadsProcess, DataInfoBaseAddress, BufferLength, PreviousMode, &LocalReturnLength);
    }
    else
    {
        Status = MmCopyVirtualMemory((PEPROCESS)ClientThread->ThreadsProcess, DataInfoBaseAddress, PsGetCurrentProcess(), Buffer, BufferLength, PreviousMode, &LocalReturnLength);
    }

    if (NT_SUCCESS(Status) && ReturnLength)
    {
        _SEH2_TRY
        {
            if (PreviousMode != KernelMode) ProbeForWriteUlong(ReturnLength);
            *ReturnLength = (ULONG)LocalReturnLength;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }

Cleanup:
    ObDereferenceObject(ClientThread);
    ObDereferenceObject(Port);
    return Status;
}

NTSTATUS
NTAPI
NtReadRequestData(
    _In_ HANDLE PortHandle,
    _In_ PPORT_MESSAGE Message,
    _In_ ULONG Index,
    _Out_ PVOID Buffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PULONG ReturnLength)
{
    return AlpcpCopyRequestData(FALSE, PortHandle, Message, Index, Buffer, BufferLength, ReturnLength);
}

NTSTATUS
NTAPI
NtWriteRequestData(
    _In_ HANDLE PortHandle,
    _In_ PPORT_MESSAGE Message,
    _In_ ULONG Index,
    _In_ PVOID Buffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PULONG ReturnLength)
{
    return AlpcpCopyRequestData(TRUE, PortHandle, Message, Index, Buffer, BufferLength, ReturnLength);
}

NTSTATUS
NTAPI
NtQueryPortInformationProcess(VOID)
{
    return STATUS_WAIT_1;
}

NTSTATUS
NTAPI
NtQueryInformationPort(
    _In_ HANDLE PortHandle,
    _In_ PORT_INFORMATION_CLASS PortInformationClass,
    _Out_ PVOID PortInformation,
    _In_ ULONG PortInformationLength,
    _Out_ PULONG ReturnLength)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;

    UNREFERENCED_PARAMETER(PortInformationClass);

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWrite(PortInformation, PortInformationLength, sizeof(ULONG));
            if (ReturnLength)
                ProbeForWriteUlong(ReturnLength);
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    if (!PortHandle) return STATUS_INVALID_INFO_CLASS;

    Status = AlpcpReferencePortByHandle(PortHandle, READ_CONTROL, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    if (ReturnLength) *ReturnLength = 0;
    ObDereferenceObject(Port);
    return STATUS_SUCCESS;
}
