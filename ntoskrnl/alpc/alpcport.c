/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Advanced Local Procedure Call port services
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <ntoskrnl.h>

#define ALPC_VALID_PORT_ATTRIBUTE_FLAGS 0x03FF0000UL

typedef struct _ALPC_PORT_MESSAGE32
{
    USHORT DataLength;
    USHORT TotalLength;
    USHORT Type;
    USHORT DataInfoOffset;
    ULONG UniqueProcess;
    ULONG UniqueThread;
    ULONG MessageId;
    ULONG CallbackId;
} ALPC_PORT_MESSAGE32, *PALPC_PORT_MESSAGE32;

C_ASSERT(sizeof(ALPC_PORT_MESSAGE32) == 0x18);

static
NTSTATUS
AlpcpCaptureMessageHeader(
    _In_ PPORT_MESSAGE UserMessage,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PPORT_MESSAGE Header)
{
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
    return STATUS_SUCCESS;
}

static
NTSTATUS
AlpcpWriteInformation(
    _Out_ PVOID UserBuffer,
    _In_ ULONG UserLength,
    _In_ PVOID Data,
    _In_ ULONG DataLength,
    _Out_opt_ PULONG ReturnLength,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status = STATUS_SUCCESS;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            if (ReturnLength)
            {
                ProbeForWriteUlong(ReturnLength);
                *ReturnLength = DataLength;
            }
            if (UserLength < DataLength)
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
            }
            else
            {
                ProbeForWrite(UserBuffer, DataLength, sizeof(ULONG));
                RtlCopyMemory(UserBuffer, Data, DataLength);
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
        if (ReturnLength) *ReturnLength = DataLength;
        if (UserLength < DataLength) return STATUS_INFO_LENGTH_MISMATCH;
        RtlCopyMemory(UserBuffer, Data, DataLength);
    }
    return Status;
}

VOID
NTAPI
AlpcpSignalPortReferenceWaiter(
    _In_ PALPC_PORT Port)
{
    PKEVENT WaitEvent;

    AlpcpAcquireLock();
    WaitEvent = Port->ReferenceWaiter;
    if (WaitEvent)
        KeSetEvent(WaitEvent, IO_NO_INCREMENT, FALSE);
    AlpcpReleaseLock();
}

VOID
NTAPI
AlpcpTrackPortReferences(
    _In_ PALPC_PORT Port)
{
    ULONG ReferenceCount;

    ReferenceCount = (ULONG)InterlockedIncrement((PLONG)&Port->ReferenceCount);
    if (!Port->ReferenceWaiter) return;

    AlpcpAcquireLock();
    if (Port->ReferenceWaiter &&
        (ReferenceCount == Port->ReferenceWaitTarget))
    {
        KeSetEvent(Port->ReferenceWaiter, IO_NO_INCREMENT, FALSE);
    }
    AlpcpReleaseLock();
}

NTSTATUS
NTAPI
AlpcpWaitForPortReferences(
    _In_ PALPC_PORT Port,
    _In_ ULONG ReferenceCount)
{
    KEVENT WaitEvent;
    NTSTATUS Status;

    if (Port->ReferenceCount == ReferenceCount)
        return STATUS_SUCCESS;

    KeInitializeEvent(&WaitEvent, SynchronizationEvent, FALSE);
    AlpcpAcquireLock();

    /* The tracked counter changes without AlpcpLock. Recheck after taking the
       lock so a tracker cannot reach the target between the fast check and
       publication of the waiter. */
    if (Port->ReferenceCount == ReferenceCount)
    {
        AlpcpReleaseLock();
        return STATUS_SUCCESS;
    }

    if (Port->ReferenceWaiter)
    {
        AlpcpReleaseLock();
        return STATUS_INVALID_PARAMETER;
    }

    Port->ReferenceWaiter = &WaitEvent;
    Port->ReferenceWaitTarget = ReferenceCount;
    AlpcpReleaseLock();

    do
    {
        Status = KeWaitForSingleObject(&WaitEvent, WrLpcReply, KernelMode, TRUE, NULL);

        AlpcpAcquireLock();
        if (Port->ReferenceCount == ReferenceCount)
        {
            Port->ReferenceWaiter = NULL;
            Port->ReferenceWaitTarget = 0;
            AlpcpReleaseLock();
            return STATUS_SUCCESS;
        }
        AlpcpReleaseLock();
    } while (Status == STATUS_KERNEL_APC);

    AlpcpAcquireLock();
    if (Port->ReferenceWaiter == &WaitEvent)
    {
        Port->ReferenceWaiter = NULL;
        Port->ReferenceWaitTarget = 0;
    }
    AlpcpReleaseLock();
    return Status;
}

static
NTSTATUS
AlpcpQueryServerInformation(
    _In_opt_ PALPC_PORT Port,
    _Inout_updates_bytes_(Length) PALPC_SERVER_INFORMATION ServerInformation,
    _In_ ULONG Length,
    _Out_opt_ PULONG ReturnLength,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status;
    HANDLE ThreadHandle;
    PETHREAD Thread;
    PEPROCESS ThreadProcess, ConnectedProcess = NULL;
    PALPC_COMMUNICATION_INFO CommunicationInfo;
    PALPC_PORT ConnectionPort = NULL, ClientPort, ServerPort;
    PKALPC_MESSAGE Message;
    POBJECT_NAME_INFORMATION NameInformation = NULL;
    ULONG NameInformationLength = 0, RequiredLength;
    BOOLEAN ThreadBlocked = FALSE;
    UNICODE_STRING PortName = {0};

    /* This class is a thread query, not a port query.  Native requires the
       caller to pass a NULL port handle. */
    if (Port || !ServerInformation || Length < sizeof(HANDLE))
        return STATUS_INVALID_PARAMETER;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(ServerInformation, sizeof(HANDLE), sizeof(ULONG));
            ThreadHandle = ServerInformation->In.ThreadHandle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        ThreadHandle = ServerInformation->In.ThreadHandle;
    }

    Status = ObReferenceObjectByHandle(ThreadHandle, THREAD_QUERY_INFORMATION, PsThreadType, PreviousMode, (PVOID *)&Thread, NULL);
    if (!NT_SUCCESS(Status)) return Status;

    ThreadProcess = PsGetThreadProcess(Thread);
    AlpcpAcquireLock();
    Message = Thread->AlpcMessage;
    CommunicationInfo = Message ? Message->CommunicationInfo : NULL;
    if (!CommunicationInfo && Message && Message->QueuePort)
        CommunicationInfo = Message->QueuePort->CommunicationInfo;

    if (Message && CommunicationInfo)
    {
        ThreadBlocked = TRUE;
        ConnectionPort = CommunicationInfo->ConnectionPort;
        ClientPort = CommunicationInfo->ClientCommunicationPort;
        ServerPort = CommunicationInfo->ServerCommunicationPort;

        if (ClientPort && ClientPort->OwnerProcess == ThreadProcess)
            ConnectedProcess = ServerPort ? ServerPort->OwnerProcess : NULL;
        else if (ServerPort && ServerPort->OwnerProcess == ThreadProcess)
            ConnectedProcess = ClientPort ? ClientPort->OwnerProcess : NULL;
        else if (Message->SenderPort && Message->SenderPort->OwnerProcess != ThreadProcess)
            ConnectedProcess = Message->SenderPort->OwnerProcess;

        if (ConnectionPort && !ObReferenceObjectSafe(ConnectionPort))
            ConnectionPort = NULL;
        if (ConnectedProcess && !ObReferenceObjectSafe(ConnectedProcess))
            ConnectedProcess = NULL;
    }
    AlpcpReleaseLock();
    ObDereferenceObject(Thread);

    if (ConnectionPort)
    {
        Status = ObQueryNameString(ConnectionPort, NULL, 0, &NameInformationLength);
        if ((Status == STATUS_INFO_LENGTH_MISMATCH || Status == STATUS_BUFFER_TOO_SMALL) &&
            NameInformationLength >= sizeof(OBJECT_NAME_INFORMATION))
        {
            NameInformation = ExAllocatePoolWithTag(PagedPool, NameInformationLength, 'NcpA');
            if (NameInformation &&
                NT_SUCCESS(ObQueryNameString(ConnectionPort, NameInformation, NameInformationLength, &NameInformationLength)))
            {
                PortName = NameInformation->Name;
            }
        }
        ObDereferenceObject(ConnectionPort);
    }

    RequiredLength = sizeof(ALPC_SERVER_INFORMATION) + PortName.MaximumLength;
    Status = (Length < RequiredLength) ? STATUS_INFO_LENGTH_MISMATCH : STATUS_SUCCESS;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            if (ReturnLength)
            {
                ProbeForWriteUlong(ReturnLength);
                *ReturnLength = RequiredLength;
            }
            if (NT_SUCCESS(Status))
            {
                ProbeForWrite(ServerInformation, RequiredLength, sizeof(ULONG));
                RtlZeroMemory(ServerInformation, sizeof(*ServerInformation));
                ServerInformation->Out.ThreadBlocked = ThreadBlocked;
                ServerInformation->Out.ConnectedProcessId =
                    ConnectedProcess ? PsGetProcessId(ConnectedProcess) : NULL;
                if (PortName.MaximumLength)
                {
                    ServerInformation->Out.ConnectionPortName.Length = PortName.Length;
                    ServerInformation->Out.ConnectionPortName.MaximumLength = PortName.MaximumLength;
                    ServerInformation->Out.ConnectionPortName.Buffer =
                        (PWSTR)((PUCHAR)ServerInformation + sizeof(*ServerInformation));
                    RtlCopyMemory(ServerInformation->Out.ConnectionPortName.Buffer,
                                  PortName.Buffer,
                                  PortName.MaximumLength);
                }
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
        if (ReturnLength) *ReturnLength = RequiredLength;
        if (NT_SUCCESS(Status))
        {
            RtlZeroMemory(ServerInformation, sizeof(*ServerInformation));
            ServerInformation->Out.ThreadBlocked = ThreadBlocked;
            ServerInformation->Out.ConnectedProcessId =
                ConnectedProcess ? PsGetProcessId(ConnectedProcess) : NULL;
            if (PortName.MaximumLength)
            {
                ServerInformation->Out.ConnectionPortName.Length = PortName.Length;
                ServerInformation->Out.ConnectionPortName.MaximumLength = PortName.MaximumLength;
                ServerInformation->Out.ConnectionPortName.Buffer =
                    (PWSTR)((PUCHAR)ServerInformation + sizeof(*ServerInformation));
                RtlCopyMemory(ServerInformation->Out.ConnectionPortName.Buffer,
                              PortName.Buffer,
                              PortName.MaximumLength);
            }
        }
    }

    if (ConnectedProcess) ObDereferenceObject(ConnectedProcess);
    if (NameInformation) ExFreePoolWithTag(NameInformation, 'NcpA');
    return Status;
}

NTSTATUS
NTAPI
NtAlpcQueryInformation(
    _In_opt_ HANDLE PortHandle,
    _In_ ALPC_PORT_INFORMATION_CLASS PortInformationClass,
    _Inout_updates_bytes_to_(Length, *ReturnLength) PVOID PortInformation,
    _In_ ULONG Length,
    _Out_opt_ PULONG ReturnLength)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;
    ALPC_BASIC_INFORMATION Basic;
    ALPC_SERVER_SESSION_INFORMATION Session;
    PALPC_PORT Peer;
    PEPROCESS Process;
    PSID Sid = NULL;
    PTOKEN Token;
    PTOKEN_USER UserInfo;
    ULONG ReferenceCount;

    PAGED_CODE();

    if (!PortInformation) return STATUS_INVALID_PARAMETER;

    Port = NULL;
    if (PortHandle)
    {
        Status = AlpcpReferencePortByHandle(PortHandle, READ_CONTROL, PreviousMode, &Port);
        if (!NT_SUCCESS(Status)) return Status;
    }

    switch (PortInformationClass)
    {
        case AlpcBasicInformation:
            if (!Port)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            AlpcpAcquireLock();
            Basic.Flags = Port->PortAttributes.Flags;
            Basic.SequenceNo = Port->SequenceNo;
            Basic.PortContext = Port->PortContext;
            AlpcpReleaseLock();
            Status = AlpcpWriteInformation(PortInformation, Length, &Basic, sizeof(Basic), ReturnLength, PreviousMode);
            break;

        case AlpcConnectedSIDInformation:
            if (!Port)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            if (PreviousMode != KernelMode)
            {
                _SEH2_TRY
                {
                    if (ReturnLength)
                    {
                        ProbeForWriteUlong(ReturnLength);
                        *ReturnLength = 0;
                    }
                    Status = SepCaptureSid(PortInformation, PreviousMode, PagedPool, TRUE, &Sid);
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
            }
            else
            {
                if (ReturnLength) *ReturnLength = 0;
                Sid = PortInformation;
            }
            if (!NT_SUCCESS(Status)) break;

            AlpcpAcquireLock();
            Peer = NULL;
            if (Port->CommunicationInfo)
            {
                Peer = (AlpcpPortType(Port) == ALPC_PORT_TYPE_SERVER) ? Port->CommunicationInfo->ClientCommunicationPort
                                                                       : Port->CommunicationInfo->ServerCommunicationPort;
            }
            Process = (Peer && Peer->OwnerProcess && ObReferenceObjectSafe(Peer->OwnerProcess)) ? Peer->OwnerProcess : NULL;
            AlpcpReleaseLock();

            if (!Process)
            {
                Status = STATUS_PORT_DISCONNECTED;
            }
            else
            {
                Token = PsReferencePrimaryToken(Process);
                Status = SeQueryInformationToken(Token, TokenUser, (PVOID*)&UserInfo);
                PsDereferencePrimaryToken(Token);
                ObDereferenceObject(Process);
                if (NT_SUCCESS(Status))
                {
                    if (!RtlEqualSid(Sid, UserInfo->User.Sid)) Status = STATUS_SERVER_SID_MISMATCH;
                    ExFreePoolWithTag(UserInfo, TAG_SE);
                }
            }
            if ((PreviousMode != KernelMode) && Sid) SepReleaseSid(Sid, PreviousMode, TRUE);
            break;

        case AlpcServerSessionInformation:
            if (!Port)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            AlpcpAcquireLock();
            Peer = NULL;
            if (Port->CommunicationInfo) Peer = Port->CommunicationInfo->ServerCommunicationPort;
            if (AlpcpPortType(Port) == ALPC_PORT_TYPE_CONNECTION) Peer = Port;
            Process = (Peer && Peer->OwnerProcess) ? Peer->OwnerProcess : NULL;
            Session.SessionId = Process ? PsGetProcessSessionId(Process) : 0;
            Session.ProcessId = Process ? HandleToUlong(PsGetProcessId(Process)) : 0;
            AlpcpReleaseLock();
            Status = AlpcpWriteInformation(PortInformation, Length, &Session, sizeof(Session), ReturnLength, PreviousMode);
            break;

        case AlpcServerInformation:
            Status = AlpcpQueryServerInformation(Port, PortInformation, Length, ReturnLength, PreviousMode);
            break;

        case AlpcWaitForPortReferences:
            if (!Port)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            if (Length != sizeof(ReferenceCount))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }
            if (PreviousMode != KernelMode)
            {
                _SEH2_TRY
                {
                    ProbeForRead(PortInformation, sizeof(ReferenceCount), sizeof(ULONG));
                    ReferenceCount = *(volatile ULONG *)PortInformation;
                    if (ReturnLength)
                    {
                        ProbeForWriteUlong(ReturnLength);
                        *ReturnLength = 0;
                    }
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                if (!NT_SUCCESS(Status)) break;
            }
            else
            {
                ReferenceCount = *(PULONG)PortInformation;
                if (ReturnLength) *ReturnLength = 0;
            }
            Status = AlpcpWaitForPortReferences(Port, ReferenceCount);
            break;

        default:
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }

    if (Port) ObDereferenceObject(Port);
    return Status;
}

NTSTATUS
NTAPI
NtAlpcSetInformation(
    _In_ HANDLE PortHandle,
    _In_ ALPC_PORT_INFORMATION_CLASS PortInformationClass,
    _In_reads_bytes_opt_(Length) PVOID PortInformation,
    _In_ ULONG Length)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;
    ALPC_PORT_ASSOCIATE_COMPLETION_PORT Associate;
    ALPC_PORT_ATTRIBUTES Attributes;
    ALPC_PORT_COMPLETION_LIST_INFORMATION CompletionListInformation;
    ALPC_PORT_MESSAGE_ZONE_INFORMATION MessageZoneInformation;
    ULONG ConcurrencyCount;
    PVOID CallbackObject = NULL, OldCallbackObject;
    PVOID CompletionPort = NULL, OldCompletionPort;

    PAGED_CODE();

    if (!PortHandle ||
        (!PortInformation &&
         PortInformationClass != AlpcUnregisterCompletionListInformation &&
         PortInformationClass != AlpcCompletionListRundownInformation))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_CONNECT, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    switch (PortInformationClass)
    {
        case AlpcAssociateCompletionPortInformation:
            if (Length != sizeof(Associate))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }
            if (PreviousMode != KernelMode)
            {
                _SEH2_TRY
                {
                    ProbeForRead(PortInformation, sizeof(Associate), sizeof(ULONG));
                    Associate = *(volatile ALPC_PORT_ASSOCIATE_COMPLETION_PORT*)PortInformation;
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                if (!NT_SUCCESS(Status)) break;
            }
            else
            {
                Associate = *(PALPC_PORT_ASSOCIATE_COMPLETION_PORT)PortInformation;
            }
            if (Associate.CompletionPort)
            {
                Status = ObReferenceObjectByHandle(Associate.CompletionPort, IO_COMPLETION_MODIFY_STATE, IoCompletionType, PreviousMode, &CompletionPort, NULL);
                if (!NT_SUCCESS(Status)) break;
            }
            AlpcpAcquireLock();
            OldCompletionPort = Port->CompletionPort;
            Port->CompletionPort = CompletionPort;
            Port->CompletionKey = Associate.CompletionKey;
            AlpcpReleaseLock();
            if (OldCompletionPort) ObDereferenceObject(OldCompletionPort);
            break;

        case AlpcPortInformation:
            if (Length != sizeof(Attributes))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }
            if (PreviousMode != KernelMode)
            {
                _SEH2_TRY
                {
                    ProbeForRead(PortInformation, sizeof(Attributes), sizeof(ULONG));
                    Attributes = *(volatile ALPC_PORT_ATTRIBUTES*)PortInformation;
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                if (!NT_SUCCESS(Status)) break;
            }
            else
            {
                Attributes = *(PALPC_PORT_ATTRIBUTES)PortInformation;
            }
            if (Attributes.Flags & ~ALPC_VALID_PORT_ATTRIBUTE_FLAGS)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }

            /* Native accepts the complete structure here for ABI stability,
               but only the legacy-request compatibility bit is mutable after
               port creation.  Quotas and security properties are immutable. */
            AlpcpAcquireLock();
            Port->PortAttributes.Flags =
                (Port->PortAttributes.Flags & ~ALPC_PORFLG_ALLOW_LPC_REQUESTS) |
                (Attributes.Flags & ALPC_PORFLG_ALLOW_LPC_REQUESTS);
            AlpcpReleaseLock();
            Status = STATUS_SUCCESS;
            break;

        case AlpcCompletionListRundownInformation:
            Status = (Length == 0) ? AlpcpRundownCompletionList(Port) :
                                     STATUS_INFO_LENGTH_MISMATCH;
            break;

        case AlpcUnregisterCompletionListInformation:
            Status = (Length == 0) ? AlpcpUnregisterCompletionList(Port, FALSE) :
                                     STATUS_INFO_LENGTH_MISMATCH;
            break;

        case AlpcRegisterCompletionListInformation:
            if (Length != sizeof(CompletionListInformation))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }
            if (PreviousMode != KernelMode)
            {
                _SEH2_TRY
                {
                    ProbeForRead(PortInformation,
                                 sizeof(CompletionListInformation),
                                 sizeof(ULONG));
                    CompletionListInformation =
                        *(volatile ALPC_PORT_COMPLETION_LIST_INFORMATION *)PortInformation;
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                if (!NT_SUCCESS(Status)) break;
            }
            else
            {
                CompletionListInformation =
                    *(PALPC_PORT_COMPLETION_LIST_INFORMATION)PortInformation;
            }
            Status = AlpcpRegisterCompletionList(Port, &CompletionListInformation, PreviousMode);
            break;

        case AlpcAdjustCompletionListConcurrencyCountInformation:
            if (Length != sizeof(ConcurrencyCount))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }
            if (PreviousMode != KernelMode)
            {
                _SEH2_TRY
                {
                    ProbeForRead(PortInformation, sizeof(ConcurrencyCount), sizeof(ULONG));
                    ConcurrencyCount = *(volatile ULONG *)PortInformation;
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                if (!NT_SUCCESS(Status)) break;
            }
            else
            {
                ConcurrencyCount = *(PULONG)PortInformation;
            }
            Status = AlpcpAdjustCompletionListConcurrencyCount(Port, ConcurrencyCount);
            break;

        case AlpcMessageZoneInformation:
            if (Length != sizeof(MessageZoneInformation))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                break;
            }
            if (PreviousMode != KernelMode)
            {
                _SEH2_TRY
                {
                    ProbeForRead(PortInformation, sizeof(MessageZoneInformation), sizeof(ULONG));
                    MessageZoneInformation =
                        *(volatile ALPC_PORT_MESSAGE_ZONE_INFORMATION *)PortInformation;
                }
                _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
                {
                    Status = _SEH2_GetExceptionCode();
                }
                _SEH2_END;
                if (!NT_SUCCESS(Status)) break;
            }
            else
            {
                MessageZoneInformation = *(PALPC_PORT_MESSAGE_ZONE_INFORMATION)PortInformation;
            }
            /* This information class is retained for ABI compatibility. The
               current native implementation validates the 16-byte payload but
               does not install a user-owned allocation zone on the port. */
            UNREFERENCED_PARAMETER(MessageZoneInformation);
            Status = STATUS_SUCCESS;
            break;

        case AlpcRegisterCallbackInformation:
            if ((PreviousMode != KernelMode) || (Length != sizeof(PVOID)) || !PortInformation)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            CallbackObject = *(PVOID *)PortInformation;
            if (!CallbackObject)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            ObReferenceObject(CallbackObject);
            AlpcpAcquireLock();
            OldCallbackObject = Port->CallbackObject;
            Port->CallbackObject = CallbackObject;
            AlpcpReleaseLock();
            if (OldCallbackObject) ObDereferenceObject(OldCallbackObject);
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_INVALID_INFO_CLASS;
            break;
    }

    ObDereferenceObject(Port);
    return Status;
}

static
PKALPC_MESSAGE
AlpcpFindMessageForQuery(
    _In_ PALPC_PORT Port,
    _In_ PPORT_MESSAGE Header)
{
    PKALPC_MESSAGE Message;

    Message = AlpcpFindPendingMessageForReply(Port, Header->MessageId, &Header->ClientId);
    if (Message) return Message;
    if (Port->CommunicationInfo && Port->CommunicationInfo->ClientCommunicationPort)
    {
        return AlpcpFindPendingMessage(Port->CommunicationInfo->ClientCommunicationPort, Header->MessageId, &Header->ClientId);
    }
    return NULL;
}

NTSTATUS
NTAPI
NtAlpcQueryInformationMessage(
    _In_ HANDLE PortHandle,
    _In_ PPORT_MESSAGE PortMessage,
    _In_ ALPC_MESSAGE_INFORMATION_CLASS MessageInformationClass,
    _Out_writes_bytes_to_opt_(Length, *ReturnLength) PVOID MessageInformation,
    _In_ ULONG Length,
    _Out_opt_ PULONG ReturnLength)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;
    PORT_MESSAGE Header;
    PKALPC_MESSAGE Message;
    CLIENT_ID ClientId;
    PEPROCESS Process;
    PETHREAD Thread;
    PTOKEN Token;
    PTOKEN_USER UserInfo;
    PTOKEN_STATISTICS Statistics;
    ALPC_MESSAGE_HANDLE_INFORMATION HandleInfo;
    ULONG HandleIndex = 0;
    BOOLEAN Found, HandleFound = FALSE, DirectCompleted = FALSE;

    PAGED_CODE();

    Status = AlpcpCaptureMessageHeader(PortMessage, PreviousMode, &Header);
    if (!NT_SUCCESS(Status)) return Status;

    if ((MessageInformationClass == AlpcMessageHandleInformation) &&
        MessageInformation && (Length >= sizeof(HandleInfo)))
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                ProbeForRead(MessageInformation, sizeof(HandleInfo), sizeof(ULONG));
                HandleIndex = ((volatile ALPC_MESSAGE_HANDLE_INFORMATION *)MessageInformation)->Index;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            HandleIndex = ((PALPC_MESSAGE_HANDLE_INFORMATION)MessageInformation)->Index;
        }
    }

    if (!Header.MessageId) return STATUS_INVALID_PARAMETER;

    Status = AlpcpReferencePortByHandle(PortHandle, READ_CONTROL, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    AlpcpAcquireLock();
    Message = AlpcpFindMessageForQuery(Port, &Header);
    Found = (Message != NULL);
    if (Found)
    {
        ClientId = Message->PortMessage.ClientId;
        DirectCompleted = (Message->State & ALPC_MSG_STATE_REPLIED) != 0;
        if (Message->Attributes.HandleData &&
            HandleIndex < Message->Attributes.HandleData->Count)
        {
            HandleInfo.Index = HandleIndex;
            HandleInfo.Flags = Message->Attributes.HandleData->Entries[HandleIndex].Flags;
            HandleInfo.Handle = 0;
            HandleInfo.ObjectType = Message->Attributes.HandleData->Entries[HandleIndex].ObjectType;
            HandleInfo.GrantedAccess = Message->Attributes.HandleData->Entries[HandleIndex].DesiredAccess;
            HandleFound = TRUE;
        }
    }
    AlpcpReleaseLock();

    if (!Found)
    {
        ObDereferenceObject(Port);
        return STATUS_REQUEST_CANCELED;
    }

    switch (MessageInformationClass)
    {
        case AlpcMessageSidInformation:
        case AlpcMessageTokenModifiedIdInformation:
            Status = PsLookupProcessThreadByCid(&ClientId, &Process, &Thread);
            if (!NT_SUCCESS(Status)) break;
            Token = PsReferencePrimaryToken(Process);
            if (MessageInformationClass == AlpcMessageSidInformation)
            {
                Status = SeQueryInformationToken(Token, TokenUser, (PVOID*)&UserInfo);
                if (NT_SUCCESS(Status))
                {
                    Status = AlpcpWriteInformation(MessageInformation, Length, UserInfo->User.Sid, RtlLengthSid(UserInfo->User.Sid), ReturnLength, PreviousMode);
                    ExFreePoolWithTag(UserInfo, TAG_SE);
                }
            }
            else
            {
                Status = SeQueryInformationToken(Token, TokenStatistics, (PVOID*)&Statistics);
                if (NT_SUCCESS(Status))
                {
                    Status = AlpcpWriteInformation(MessageInformation, Length, &Statistics->ModifiedId, sizeof(LUID), ReturnLength, PreviousMode);
                    ExFreePoolWithTag(Statistics, TAG_SE);
                }
            }
            PsDereferencePrimaryToken(Token);
            ObDereferenceObject(Thread);
            ObDereferenceObject(Process);
            break;

        case AlpcMessageHandleInformation:
            if (Length != sizeof(HandleInfo))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
            }
            else if (!HandleFound)
            {
                Status = STATUS_INVALID_HANDLE;
            }
            else
            {
                Status = AlpcpWriteInformation(MessageInformation, Length, &HandleInfo, sizeof(HandleInfo), ReturnLength, PreviousMode);
            }
            break;

        case AlpcMessageDirectStatusInformation:
            if (MessageInformation || Length || ReturnLength)
                Status = STATUS_INVALID_PARAMETER;
            else
                Status = DirectCompleted ? STATUS_SUCCESS : STATUS_PENDING;
            break;

        default:
            Status = STATUS_INVALID_PARAMETER;
            break;
    }

    ObDereferenceObject(Port);
    return Status;
}

NTSTATUS
NTAPI
NtAlpcImpersonateClientOfPort(
    _In_ HANDLE PortHandle,
    _In_ PPORT_MESSAGE Message,
    _In_ PVOID Flags)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;
    PORT_MESSAGE Header;
    PKALPC_MESSAGE Pending;
    BOOLEAN FreePending;

    PAGED_CODE();

    Status = AlpcpCaptureMessageHeader(Message, PreviousMode, &Header);
    if (!NT_SUCCESS(Status)) return Status;

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_CONNECT, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    if (AlpcpPortType(Port) == ALPC_PORT_TYPE_CLIENT)
    {
        ObDereferenceObject(Port);
        return STATUS_INVALID_PORT_HANDLE;
    }

    AlpcpAcquireLock();
    Pending = AlpcpFindPendingMessageForReply(Port, Header.MessageId, &Header.ClientId);
    if (!Pending || (Pending->State & ALPC_MSG_STATE_CONNECTION))
    {
        AlpcpReleaseLock();
        ObDereferenceObject(Port);
        return STATUS_REPLY_MESSAGE_MISMATCH;
    }
    Pending->State |= ALPC_MSG_STATE_IMPERSONATING;
    AlpcpReleaseLock();

    Status = AlpcpImpersonateMessage(Port, Pending, PtrToUlong(Flags));

    AlpcpAcquireLock();
    Pending->State &= ~ALPC_MSG_STATE_IMPERSONATING;
    FreePending = !Pending->WaitingThread &&
                  !(Pending->State & ALPC_MSG_STATE_IN_CANCELED_QUEUE) &&
                  (Pending->State & (ALPC_MSG_STATE_REPLIED |
                                     ALPC_MSG_STATE_REFUSED |
                                     ALPC_MSG_STATE_ACCEPTED |
                                     ALPC_MSG_STATE_CANCELED |
                                     ALPC_MSG_STATE_DISCONNECTED));
    if (FreePending) AlpcpFreeMessage(Pending);
    AlpcpReleaseLock();

    ObDereferenceObject(Port);
    return Status;
}

NTSTATUS
NTAPI
NtAlpcImpersonateClientContainerOfPort(
    _In_ HANDLE PortHandle,
    _In_ PPORT_MESSAGE Message,
    _In_ ULONG Flags)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PORT_MESSAGE Header;
    PALPC_PORT Port;
    PKALPC_MESSAGE Pending;
    CLIENT_ID ClientId;
    PEPROCESS ClientProcess;
    PETHREAD ClientThread;

    PAGED_CODE();

    if (Flags) return STATUS_INVALID_PARAMETER;

    Status = AlpcpCaptureMessageHeader(Message, PreviousMode, &Header);
    if (!NT_SUCCESS(Status)) return Status;

    Status = AlpcpReferencePortByHandle(PortHandle, READ_CONTROL, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    if ((AlpcpPortType(Port) != ALPC_PORT_TYPE_SERVER) ||
        (Port->OwnerProcess != PsGetCurrentProcess()))
    {
        ObDereferenceObject(Port);
        return STATUS_ACCESS_DENIED;
    }

    AlpcpAcquireLock();
    Pending = AlpcpFindMessageForQuery(Port, &Header);
    if (Pending && !(Pending->State & (ALPC_MSG_STATE_CONNECTION |
                                       ALPC_MSG_STATE_CANCELED |
                                       ALPC_MSG_STATE_DISCONNECTED)))
    {
        ClientId = Pending->PortMessage.ClientId;
    }
    else
    {
        Pending = NULL;
    }
    AlpcpReleaseLock();
    ObDereferenceObject(Port);

    if (!Pending) return STATUS_REPLY_MESSAGE_MISMATCH;

    Status = PsLookupProcessThreadByCid(&ClientId, &ClientProcess, &ClientThread);
    if (!NT_SUCCESS(Status)) return Status;

    /*
     * ReactOS has no server-silo or AppContainer execution context.  Native
     * returns success after validating the message when the sender has no
     * container context, which is therefore the complete local behavior.
     */
    ObDereferenceObject(ClientThread);
    ObDereferenceObject(ClientProcess);
    return STATUS_SUCCESS;
}

static
NTSTATUS
AlpcpOpenSender(
    _In_ BOOLEAN OpenThread,
    _Out_ PHANDLE Handle,
    _In_ HANDLE PortHandle,
    _In_ PPORT_MESSAGE PortMessage,
    _In_ ULONG Flags,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port = NULL;
    PORT_MESSAGE Header;
    OBJECT_ATTRIBUTES CapturedAttributes;
    ALPC_PORT_MESSAGE32 Header32;
    PKALPC_MESSAGE Pending;
    PVOID SenderObject = NULL;
    PETHREAD SenderThread;
    PEPROCESS SenderProcess;
    POBJECT_TYPE ObjectType;
    ACCESS_STATE AccessState;
    AUX_ACCESS_DATA AuxData;
    ULONG HandleAttributes;
    HANDLE LocalHandle = NULL;

    PAGED_CODE();
    KeEnterCriticalRegion();

    Status = AlpcpReferencePortByHandle(PortHandle, READ_CONTROL, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) goto Exit;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(Handle);
            if ((Flags & 0xC0000000UL) == ALPC_MSGFLG_WOW64_CALL)
            {
                ProbeForRead(PortMessage, sizeof(Header32), sizeof(ULONG));
                Header32 = *(volatile ALPC_PORT_MESSAGE32 *)PortMessage;
                RtlZeroMemory(&Header, sizeof(Header));
                Header.u1.s1.DataLength = Header32.DataLength;
                Header.u1.s1.TotalLength = Header32.TotalLength;
                Header.u2.s2.Type = Header32.Type;
                Header.u2.s2.DataInfoOffset = Header32.DataInfoOffset;
                Header.ClientId.UniqueProcess = UlongToHandle(Header32.UniqueProcess);
                Header.ClientId.UniqueThread = UlongToHandle(Header32.UniqueThread);
                Header.MessageId = Header32.MessageId;
                Header.CallbackId = Header32.CallbackId;
            }
            else
            {
                ProbeForRead(PortMessage, sizeof(Header), sizeof(ULONG));
                Header = *(volatile PORT_MESSAGE *)PortMessage;
            }
            ProbeForRead(ObjectAttributes, sizeof(CapturedAttributes), sizeof(ULONG));
            CapturedAttributes = *(volatile OBJECT_ATTRIBUTES *)ObjectAttributes;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
        if (!NT_SUCCESS(Status)) goto Exit;
    }
    else
    {
        Header = *PortMessage;
        CapturedAttributes = *ObjectAttributes;
    }

    AlpcpAcquireLock();
    Pending = AlpcpFindMessageForQuery(Port, &Header);
    if (!Pending)
    {
        Status = STATUS_REQUEST_CANCELED;
    }
    else if (Pending->State & ALPC_MSG_STATE_CANCELED)
    {
        Status = STATUS_REQUEST_CANCELED;
    }
    else if (OpenThread)
    {
        SenderThread = Pending->WaitingThread;
        if (!SenderThread ||
            (SenderThread->Cid.UniqueProcess != Header.ClientId.UniqueProcess) ||
            (SenderThread->Cid.UniqueThread != Header.ClientId.UniqueThread))
        {
            Status = STATUS_ACCESS_DENIED;
        }
        else
        {
            ObReferenceObject(SenderThread);
            SenderObject = SenderThread;
            Status = STATUS_SUCCESS;
        }
    }
    else if (Pending->WaitingThread)
    {
        SenderThread = Pending->WaitingThread;
        if ((SenderThread->Cid.UniqueProcess != Header.ClientId.UniqueProcess) ||
            (SenderThread->Cid.UniqueThread != Header.ClientId.UniqueThread))
        {
            Status = STATUS_INVALID_CID;
        }
        else
        {
            SenderProcess = PsGetThreadProcess(SenderThread);
            ObReferenceObject(SenderProcess);
            SenderObject = SenderProcess;
            Status = STATUS_SUCCESS;
        }
    }
    else if (Pending->State & ALPC_MSG_STATE_SENDER_DISCONNECTED)
    {
        Status = STATUS_PORT_DISCONNECTED;
    }
    else if (!Pending->SenderProcess)
    {
        Status = STATUS_ACCESS_DENIED;
    }
    else if (PsGetProcessId(Pending->SenderProcess) != Header.ClientId.UniqueProcess)
    {
        Status = STATUS_INVALID_CID;
    }
    else
    {
        ObReferenceObject(Pending->SenderProcess);
        SenderObject = Pending->SenderProcess;
        Status = STATUS_SUCCESS;
    }
    AlpcpReleaseLock();

    if (!NT_SUCCESS(Status)) goto Exit;

    if (CapturedAttributes.ObjectName)
    {
        Status = STATUS_INVALID_PARAMETER_MIX;
        goto Exit;
    }

    ObjectType = OpenThread ? PsThreadType : PsProcessType;
    HandleAttributes = ObpValidateAttributes(CapturedAttributes.Attributes, PreviousMode);
    Status = SeCreateAccessState(&AccessState, &AuxData, DesiredAccess, &ObjectType->TypeInfo.GenericMapping);
    if (!NT_SUCCESS(Status)) goto Exit;

    if (SeSinglePrivilegeCheck(SeDebugPrivilege, PreviousMode))
    {
        if (AccessState.RemainingDesiredAccess & MAXIMUM_ALLOWED)
            AccessState.PreviouslyGrantedAccess |= OpenThread ? THREAD_ALL_ACCESS : PROCESS_ALL_ACCESS;
        else
            AccessState.PreviouslyGrantedAccess |= AccessState.RemainingDesiredAccess;
        AccessState.RemainingDesiredAccess = 0;
    }

    Status = ObOpenObjectByPointer(SenderObject, HandleAttributes, &AccessState, 0, ObjectType, PreviousMode, &LocalHandle);
    SeDeleteAccessState(&AccessState);
    if (!NT_SUCCESS(Status)) goto Exit;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *Handle = LocalHandle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            Status = _SEH2_GetExceptionCode();
        }
        _SEH2_END;
    }
    else
    {
        *Handle = LocalHandle;
    }

    if (!NT_SUCCESS(Status))
    {
        ObCloseHandle(LocalHandle, PreviousMode);
        LocalHandle = NULL;
    }

Exit:
    if (SenderObject) ObDereferenceObject(SenderObject);
    if (Port) ObDereferenceObject(Port);
    KeLeaveCriticalRegion();
    return Status;
}

NTSTATUS
NTAPI
NtAlpcOpenSenderProcess(
    _Out_ PHANDLE ProcessHandle,
    _In_ HANDLE PortHandle,
    _In_ PPORT_MESSAGE PortMessage,
    _In_ ULONG Flags,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    return AlpcpOpenSender(FALSE, ProcessHandle, PortHandle, PortMessage, Flags, DesiredAccess, ObjectAttributes);
}

NTSTATUS
NTAPI
NtAlpcOpenSenderThread(
    _Out_ PHANDLE ThreadHandle,
    _In_ HANDLE PortHandle,
    _In_ PPORT_MESSAGE PortMessage,
    _In_ ULONG Flags,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ POBJECT_ATTRIBUTES ObjectAttributes)
{
    return AlpcpOpenSender(TRUE, ThreadHandle, PortHandle, PortMessage, Flags, DesiredAccess, ObjectAttributes);
}
