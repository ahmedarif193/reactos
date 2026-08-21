/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Advanced Local Procedure Call port connection management
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <ntoskrnl.h>

#define ALPC_VALID_PORT_ATTRIBUTE_FLAGS 0x03FF0000UL
#define ALPC_VALID_DUP_OBJECT_TYPES     0x00000FFDUL

typedef struct _ALPC_CONNECTION_MESSAGE32
{
    USHORT DataLength;
    USHORT TotalLength;
    USHORT Type;
    USHORT DataInfoOffset;
    ULONG UniqueProcess;
    ULONG UniqueThread;
    ULONG MessageId;
    ULONG CallbackId;
} ALPC_CONNECTION_MESSAGE32, *PALPC_CONNECTION_MESSAGE32;

C_ASSERT(sizeof(ALPC_CONNECTION_MESSAGE32) == 0x18);

static
NTSTATUS
AlpcpCapturePortAttributes(
    _In_opt_ PALPC_PORT_ATTRIBUTES UserAttributes,
    _In_ KPROCESSOR_MODE PreviousMode,
    _Out_ PALPC_PORT_ATTRIBUTES Attributes,
    _Out_ PBOOLEAN Present)
{
    *Present = FALSE;
    if (!UserAttributes) return STATUS_SUCCESS;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForRead(UserAttributes, sizeof(*UserAttributes), sizeof(ULONG));
            *Attributes = *(volatile ALPC_PORT_ATTRIBUTES*)UserAttributes;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        *Attributes = *UserAttributes;
    }

    if ((Attributes->MaxMessageLength < sizeof(PORT_MESSAGE)) ||
        (Attributes->MaxMessageLength > ALPC_MAX_ALLOWED_MESSAGE_LENGTH))
        return STATUS_INVALID_PARAMETER;

    if ((PreviousMode != KernelMode) &&
        (Attributes->Flags & ALPC_PORFLG_SYSTEM_PROCESS))
        return STATUS_INVALID_PARAMETER;

    if ((Attributes->SecurityQos.Length != sizeof(SECURITY_QUALITY_OF_SERVICE)) ||
        ((ULONG)Attributes->SecurityQos.ImpersonationLevel > SecurityDelegation) ||
        (Attributes->SecurityQos.ContextTrackingMode != SECURITY_STATIC_TRACKING &&
         Attributes->SecurityQos.ContextTrackingMode != SECURITY_DYNAMIC_TRACKING) ||
        (Attributes->SecurityQos.EffectiveOnly > TRUE))
        return STATUS_INVALID_PARAMETER;

    /* Native ignores unknown policy and duplicate-object bits after capture. */
    Attributes->Flags &= ALPC_VALID_PORT_ATTRIBUTE_FLAGS;
    Attributes->DupObjectTypes &= ALPC_VALID_DUP_OBJECT_TYPES;
    *Present = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AlpcpWriteHandle(
    _Out_ PHANDLE UserHandle,
    _In_ HANDLE Handle,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            *UserHandle = Handle;
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            ObCloseHandle(Handle, PreviousMode);
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }
    else
    {
        *UserHandle = Handle;
    }
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
NtAlpcCreatePort(
    _Out_ PHANDLE PortHandle,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PALPC_PORT_ATTRIBUTES PortAttributes)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    ALPC_PORT_ATTRIBUTES Attributes;
    BOOLEAN Present;
    PALPC_PORT Port;
    HANDLE Handle;
    ULONG MaxMessageLength;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(PortHandle);
            if (ObjectAttributes) ProbeForRead(ObjectAttributes, sizeof(*ObjectAttributes), sizeof(ULONG));
        }
        _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
        {
            _SEH2_YIELD(return _SEH2_GetExceptionCode());
        }
        _SEH2_END;
    }

    Status = AlpcpCapturePortAttributes(PortAttributes, PreviousMode, &Attributes, &Present);
    if (!NT_SUCCESS(Status)) return Status;

    MaxMessageLength = Present ? (ULONG)Attributes.MaxMessageLength : PORT_MAXIMUM_MESSAGE_LENGTH;

    Status = AlpcpCreatePort(&Port, PreviousMode, ObjectAttributes, Present ? &Attributes : NULL, ALPC_PORT_TYPE_CONNECTION, 0, MaxMessageLength - sizeof(PORT_MESSAGE), MaxMessageLength);
    if (!NT_SUCCESS(Status)) return Status;

    Status = ObInsertObject(Port, NULL, PORT_ALL_ACCESS, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status)) return Status;

    return AlpcpWriteHandle(PortHandle, Handle, PreviousMode);
}

NTSTATUS
NTAPI
NtAlpcDisconnectPort(
    _In_ HANDLE PortHandle,
    _In_ ULONG Flags)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PALPC_PORT Port;

    PAGED_CODE();

    if (Flags & ~ALPC_DISCONNECT_NO_FLUSH_ON_CLOSE) return STATUS_INVALID_PARAMETER;

    Status = AlpcpReferencePortByHandle(PortHandle, PORT_CONNECT, PreviousMode, &Port);
    if (!NT_SUCCESS(Status)) return Status;

    Status = AlpcpDisconnectPort(Port, !(Flags & ALPC_DISCONNECT_NO_FLUSH_ON_CLOSE));
    ObDereferenceObject(Port);
    return Status;
}

static
NTSTATUS
AlpcpCheckServerSecurityDescriptor(
    _In_ PALPC_PORT Port,
    _In_ PSECURITY_DESCRIPTOR SecurityDescriptor,
    _In_ KPROCESSOR_MODE AccessMode)
{
    SECURITY_SUBJECT_CONTEXT SubjectContext;
    PPRIVILEGE_SET Privileges = NULL;
    ACCESS_MASK GrantedAccess = 0;
    NTSTATUS AccessStatus = STATUS_ACCESS_DENIED;
    BOOLEAN AccessGranted;

    SeCaptureSubjectContextEx(NULL, Port->OwnerProcess, &SubjectContext);
    AccessGranted = SeAccessCheck(SecurityDescriptor, &SubjectContext, FALSE, MAXIMUM_ALLOWED, 0, &Privileges, &AlpcPortObjectType->TypeInfo.GenericMapping, AccessMode, &GrantedAccess, &AccessStatus);
    SeReleaseSubjectContext(&SubjectContext);
    if (Privileges) SeFreePrivileges(Privileges);

    if (!AccessGranted) return AccessStatus;
    if (!(GrantedAccess & PORT_CONNECT)) return STATUS_ACCESS_DENIED;
    return STATUS_SUCCESS;
}

static
NTSTATUS
AlpcpConnectPortInternal(
    _Out_ PHANDLE PortHandle,
    _In_ PALPC_PORT ConnectionPort,
    _In_opt_ POBJECT_ATTRIBUTES ClientObjectAttributes,
    _In_opt_ PALPC_PORT_ATTRIBUTES PortAttributes,
    _In_ ULONG Flags,
    _In_opt_ PSID RequiredServerSid,
    _In_opt_ PSECURITY_DESCRIPTOR ServerSecurityRequirements,
    _Inout_opt_ PPORT_MESSAGE ConnectionMessage,
    _Inout_opt_ PSIZE_T BufferLength,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES OutMessageAttributes,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES InMessageAttributes,
    _In_opt_ PLARGE_INTEGER Timeout,
    _In_ KPROCESSOR_MODE PreviousMode)
{
    NTSTATUS Status;
    PETHREAD Thread = PsGetCurrentThread();
    PALPC_PORT ClientPort = NULL;
    PKALPC_MESSAGE Message = NULL;
    PORT_MESSAGE Header;
    SIZE_T AvailableLength = MAXULONG_PTR;
    ULONG DataLength = 0;
    PALPC_MESSAGE_ATTRIBUTES InAttributes = NULL, OutAttributes = NULL;
    KPROCESSOR_MODE WaitMode;
    BOOLEAN Alertable;
    HANDLE Handle;
    SIZE_T TotalLength;

    Flags &= 0xFFFF0000UL;
    WaitMode = (Flags & ALPC_USER_WAIT_MODE) ? UserMode : PreviousMode;
    Alertable = (Flags & ALPC_WAIT_IS_ALERTABLE) != 0;

    if (AlpcpPortType(ConnectionPort) != ALPC_PORT_TYPE_CONNECTION) return STATUS_INVALID_PORT_HANDLE;

    if (RequiredServerSid)
    {
        Status = AlpcpCheckServerSid(ConnectionPort, RequiredServerSid);
        if (!NT_SUCCESS(Status)) return Status;
    }

    if (ServerSecurityRequirements)
    {
        Status = AlpcpCheckServerSecurityDescriptor(ConnectionPort, ServerSecurityRequirements, PreviousMode);
        if (!NT_SUCCESS(Status)) return Status;
    }

    if (BufferLength)
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                ProbeForWrite(BufferLength, sizeof(SIZE_T), sizeof(SIZE_T));
                AvailableLength = *(volatile SIZE_T*)BufferLength;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            AvailableLength = *BufferLength;
        }
    }

    if (ConnectionMessage)
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                ProbeForWrite(ConnectionMessage, sizeof(PORT_MESSAGE), sizeof(ULONG));
                Header = *(volatile PORT_MESSAGE*)ConnectionMessage;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                _SEH2_YIELD(return _SEH2_GetExceptionCode());
            }
            _SEH2_END;
        }
        else
        {
            Header = *ConnectionMessage;
        }
        DataLength = (USHORT)Header.u1.s1.DataLength;
        if ((DataLength > MAXUSHORT - sizeof(PORT_MESSAGE)) ||
            ((USHORT)Header.u1.s1.TotalLength != sizeof(PORT_MESSAGE) + DataLength))
        {
            return STATUS_INVALID_PARAMETER;
        }
        if (DataLength > ConnectionPort->MaxConnectionInfoLength) return STATUS_BUFFER_TOO_SMALL;
    }

    Status = AlpcpCaptureAttributes(InMessageAttributes, PreviousMode, &InAttributes);
    if (!NT_SUCCESS(Status)) return Status;
    Status = AlpcpCaptureAttributes(OutMessageAttributes, PreviousMode, &OutAttributes);
    if (!NT_SUCCESS(Status)) goto Failure;

    Status = AlpcpCreatePort(&ClientPort, PreviousMode, ClientObjectAttributes, PortAttributes, ALPC_PORT_TYPE_CLIENT, 0, ConnectionPort->MaxConnectionInfoLength, ConnectionPort->MaxMessageLength);
    if (!NT_SUCCESS(Status)) goto Failure;

    if (ClientPort->SecurityQos.ContextTrackingMode == SECURITY_DYNAMIC_TRACKING)
    {
        ClientPort->Flags |= ALPC_PORT_FLAG_DYNAMIC_SECURITY;
    }
    else
    {
        Status = SeCreateClientSecurity(Thread, &ClientPort->SecurityQos, FALSE, &ClientPort->StaticSecurity);
        if (!NT_SUCCESS(Status)) goto Failure;
    }

    Message = AlpcpAllocateMessage(ConnectionPort->MaxMessageLength, ConnectionPort);
    if (!Message)
    {
        Status = STATUS_NO_MEMORY;
        goto Failure;
    }

    Message->PortMessage.u1.s1.DataLength = (CSHORT)DataLength;
    Message->PortMessage.u1.s1.TotalLength = (CSHORT)(sizeof(PORT_MESSAGE) + DataLength);
    Message->PortMessage.u2.s2.Type = LPC_CONNECTION_REQUEST | LPC_CONTINUATION_REQUIRED;
    Message->PortMessage.ClientId = Thread->Cid;
    Message->State = ALPC_MSG_STATE_CONNECTION | ALPC_MSG_STATE_SYNC |
                     ((PreviousMode == KernelMode) ? ALPC_MSG_STATE_KERNEL : 0);
    AlpcpSetMessageSenderPort(Message, ClientPort);
    Message->Connection.SecurityQos = ClientPort->SecurityQos;
    ObReferenceObject(ClientPort);
    Message->Connection.ClientPort = ClientPort;

    if (DataLength)
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                RtlCopyMemory(&Message->PortMessage + 1, ConnectionMessage + 1, DataLength);
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
            RtlCopyMemory(&Message->PortMessage + 1, ConnectionMessage + 1, DataLength);
        }
    }

    Status = AlpcpCaptureSendAttributes(ClientPort, Message, InAttributes, PreviousMode);
    if (!NT_SUCCESS(Status)) goto Failure;

    AlpcpAcquireLock();
    if (ConnectionPort->Flags & (ALPC_PORT_FLAG_NAME_DELETED | ALPC_PORT_FLAG_CLOSED))
    {
        AlpcpReleaseLock();
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
        goto Failure;
    }
    Message->OwnerPort = ConnectionPort;
    Message->PortMessage.MessageId = AlpcpNextMessageId++;
    if (!AlpcpNextMessageId) AlpcpNextMessageId = 1;
    Message->Sequence = ConnectionPort->SequenceNo++;
    Message->WaitingThread = Thread;
    Thread->AlpcMessage = Message;
    Thread->AlpcMessageId = Message->PortMessage.MessageId;
    AlpcpQueueMessage(ConnectionPort, Message);
    AlpcpReleaseLock();

    Status = AlpcpWaitForReply(Message, WaitMode, Alertable, Timeout, NULL);
    if (Status != STATUS_SUCCESS)
    {
        Message = NULL;
        goto Failure;
    }

    if (!(Message->State & ALPC_MSG_STATE_ACCEPTED))
    {
        Status = (ConnectionPort->Flags & ALPC_PORT_FLAG_NAME_DELETED) ? STATUS_OBJECT_NAME_NOT_FOUND
                                                                       : STATUS_PORT_CONNECTION_REFUSED;
        goto Failure;
    }

    TotalLength = (USHORT)Message->PortMessage.u1.s1.TotalLength;
    if (BufferLength && (TotalLength > AvailableLength))
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                *BufferLength = TotalLength;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
        else
        {
            *BufferLength = TotalLength;
        }

        if (NT_SUCCESS(Status)) Status = STATUS_BUFFER_TOO_SMALL;
    }
    else if (ConnectionMessage || BufferLength)
    {
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                if (ConnectionMessage) RtlCopyMemory(ConnectionMessage, &Message->PortMessage, TotalLength);
                if (BufferLength) *BufferLength = TotalLength;
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
        }
        else
        {
            if (ConnectionMessage) RtlCopyMemory(ConnectionMessage, &Message->PortMessage, TotalLength);
            if (BufferLength) *BufferLength = TotalLength;
        }
    }

    if (NT_SUCCESS(Status) && OutAttributes)
    {
        Status = AlpcpExposeReceiveAttributes(ClientPort, Message, OutAttributes, OutMessageAttributes, PreviousMode);
    }

    if (!NT_SUCCESS(Status)) goto Failure;

    Status = ObInsertObject(ClientPort, NULL, PORT_ALL_ACCESS, 0, NULL, &Handle);
    ClientPort = NULL;
    if (!NT_SUCCESS(Status)) goto Failure;

    Status = AlpcpWriteHandle(PortHandle, Handle, PreviousMode);

    AlpcpFreeMessage(Message);
    if (InAttributes) ExFreePoolWithTag(InAttributes, 'AcpA');
    if (OutAttributes) ExFreePoolWithTag(OutAttributes, 'AcpA');
    return Status;

Failure:
    if (Message) AlpcpFreeMessage(Message);
    if (ClientPort) ObDereferenceObject(ClientPort);
    if (InAttributes) ExFreePoolWithTag(InAttributes, 'AcpA');
    if (OutAttributes) ExFreePoolWithTag(OutAttributes, 'AcpA');
    return Status;
}

NTSTATUS
NTAPI
NtAlpcConnectPort(
    _Out_ PHANDLE PortHandle,
    _In_ PUNICODE_STRING PortName,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PALPC_PORT_ATTRIBUTES PortAttributes,
    _In_ ULONG Flags,
    _In_opt_ PSID RequiredServerSid,
    _Inout_updates_bytes_to_opt_(*BufferLength, *BufferLength) PPORT_MESSAGE ConnectionMessage,
    _Inout_opt_ PSIZE_T BufferLength,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES OutMessageAttributes,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES InMessageAttributes,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    ALPC_PORT_ATTRIBUTES Attributes;
    BOOLEAN Present;
    PALPC_PORT ConnectionPort;
    PSID CapturedSid = NULL;
    LARGE_INTEGER CapturedTimeout;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(PortHandle);
            if (Timeout)
            {
                ProbeForReadLargeInteger(Timeout);
                CapturedTimeout = *(volatile LARGE_INTEGER*)Timeout;
                Timeout = &CapturedTimeout;
            }
            if (RequiredServerSid)
            {
                Status = SepCaptureSid(RequiredServerSid, PreviousMode, PagedPool, TRUE, &CapturedSid);
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
        CapturedSid = RequiredServerSid;
    }

    Status = AlpcpCapturePortAttributes(PortAttributes, PreviousMode, &Attributes, &Present);
    if (NT_SUCCESS(Status))
    {
        Status = ObReferenceObjectByName(PortName, 0, NULL, PORT_CONNECT, AlpcPortObjectType, PreviousMode, NULL, (PVOID*)&ConnectionPort);
    }
    if (NT_SUCCESS(Status))
    {
        Status = AlpcpConnectPortInternal(PortHandle, ConnectionPort, ObjectAttributes, Present ? &Attributes : NULL, Flags, CapturedSid, NULL, ConnectionMessage, BufferLength, OutMessageAttributes, InMessageAttributes, Timeout, PreviousMode);
        ObDereferenceObject(ConnectionPort);
    }

    if (CapturedSid && (CapturedSid != RequiredServerSid)) SepReleaseSid(CapturedSid, PreviousMode, TRUE);
    return Status;
}

NTSTATUS
NTAPI
NtAlpcConnectPortEx(
    _Out_ PHANDLE PortHandle,
    _In_ POBJECT_ATTRIBUTES ConnectionPortObjectAttributes,
    _In_opt_ POBJECT_ATTRIBUTES ClientPortObjectAttributes,
    _In_opt_ PALPC_PORT_ATTRIBUTES PortAttributes,
    _In_ ULONG Flags,
    _In_opt_ PSECURITY_DESCRIPTOR ServerSecurityRequirements,
    _Inout_updates_bytes_to_opt_(*BufferLength, *BufferLength) PPORT_MESSAGE ConnectionMessage,
    _Inout_opt_ PSIZE_T BufferLength,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES OutMessageAttributes,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES InMessageAttributes,
    _In_opt_ PLARGE_INTEGER Timeout)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    ALPC_PORT_ATTRIBUTES Attributes;
    BOOLEAN Present;
    PALPC_PORT ConnectionPort;
    LARGE_INTEGER CapturedTimeout;
    PUNICODE_STRING PortName;
    PSECURITY_DESCRIPTOR CapturedSecurityRequirements = NULL;

    PAGED_CODE();

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(PortHandle);
            ProbeForRead(ConnectionPortObjectAttributes, sizeof(*ConnectionPortObjectAttributes), sizeof(ULONG));
            PortName = ((volatile OBJECT_ATTRIBUTES*)ConnectionPortObjectAttributes)->ObjectName;
            if (Timeout)
            {
                ProbeForReadLargeInteger(Timeout);
                CapturedTimeout = *(volatile LARGE_INTEGER*)Timeout;
                Timeout = &CapturedTimeout;
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
        PortName = ConnectionPortObjectAttributes->ObjectName;
    }

    if (!PortName) return STATUS_INVALID_PARAMETER;

    Status = AlpcpCapturePortAttributes(PortAttributes, PreviousMode, &Attributes, &Present);
    if (!NT_SUCCESS(Status)) return Status;

    if (ServerSecurityRequirements)
    {
        Status = SeCaptureSecurityDescriptor(ServerSecurityRequirements, PreviousMode, PagedPool, FALSE, &CapturedSecurityRequirements);
        if (!NT_SUCCESS(Status)) return Status;
    }

    Status = ObReferenceObjectByName(PortName, 0, NULL, PORT_CONNECT, AlpcPortObjectType, PreviousMode, NULL, (PVOID*)&ConnectionPort);
    if (!NT_SUCCESS(Status)) goto Exit;

    Status = AlpcpConnectPortInternal(PortHandle, ConnectionPort, ClientPortObjectAttributes, Present ? &Attributes : NULL, Flags, NULL, CapturedSecurityRequirements, ConnectionMessage, BufferLength, OutMessageAttributes, InMessageAttributes, Timeout, PreviousMode);
    ObDereferenceObject(ConnectionPort);

Exit:
    if (CapturedSecurityRequirements)
    {
        SeReleaseSecurityDescriptor(CapturedSecurityRequirements,
                                    PreviousMode,
                                    FALSE);
    }
    return Status;
}

NTSTATUS
NTAPI
NtAlpcAcceptConnectPort(
    _Out_ PHANDLE PortHandle,
    _In_ HANDLE ConnectionPortHandle,
    _In_ ULONG Flags,
    _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes,
    _In_opt_ PALPC_PORT_ATTRIBUTES PortAttributes,
    _In_opt_ PVOID PortContext,
    _In_reads_bytes_(ConnectionRequest->u1.s1.TotalLength) PPORT_MESSAGE ConnectionRequest,
    _Inout_opt_ PALPC_MESSAGE_ATTRIBUTES ConnectionMessageAttributes,
    _In_ BOOLEAN AcceptConnection)
{
    NTSTATUS Status;
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    ALPC_PORT_ATTRIBUTES Attributes;
    BOOLEAN Present;
    PALPC_PORT ConnectionPort, ServerPort = NULL, ClientPort;
    PALPC_COMMUNICATION_INFO Info;
    PKALPC_MESSAGE Message;
    PORT_MESSAGE Header;
    ALPC_CONNECTION_MESSAGE32 Header32;
    PVOID Buffer = NULL;
    PVOID RequestData;
    ULONG DataLength;
    ULONG RequestHeaderSize;
    ULONG RequestTotalLength;
    PALPC_MESSAGE_ATTRIBUTES SendAttributes = NULL;
    PETHREAD ClientThread;
    LARGE_INTEGER SectionOffset;
    HANDLE Handle;

    PAGED_CODE();

    Flags &= 0xC0000000UL;

    if (PreviousMode != KernelMode)
    {
        _SEH2_TRY
        {
            ProbeForWriteHandle(PortHandle);
            if (Flags == ALPC_MSGFLG_WOW64_CALL)
            {
                ProbeForRead(ConnectionRequest, sizeof(Header32), sizeof(ULONG));
                Header32 = *(volatile ALPC_CONNECTION_MESSAGE32 *)ConnectionRequest;
                RtlZeroMemory(&Header, sizeof(Header));
                Header.u1.s1.DataLength = Header32.DataLength;
                Header.u1.s1.TotalLength = (CSHORT)(sizeof(PORT_MESSAGE) + Header32.DataLength);
                Header.u2.s2.Type = Header32.Type;
                Header.u2.s2.DataInfoOffset = Header32.DataInfoOffset;
                Header.ClientId.UniqueProcess = UlongToHandle(Header32.UniqueProcess);
                Header.ClientId.UniqueThread = UlongToHandle(Header32.UniqueThread);
                Header.MessageId = Header32.MessageId;
                Header.CallbackId = Header32.CallbackId;
                RequestHeaderSize = sizeof(Header32);
                RequestTotalLength = sizeof(Header32) + (USHORT)Header32.DataLength;
            }
            else
            {
                ProbeForRead(ConnectionRequest, sizeof(*ConnectionRequest), sizeof(ULONG));
                Header = *(volatile PORT_MESSAGE *)ConnectionRequest;
                RequestHeaderSize = sizeof(*ConnectionRequest);
                RequestTotalLength = (USHORT)Header.u1.s1.TotalLength;
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
        Header = *ConnectionRequest;
        RequestHeaderSize = sizeof(*ConnectionRequest);
        RequestTotalLength = (USHORT)Header.u1.s1.TotalLength;
    }

    DataLength = (USHORT)Header.u1.s1.DataLength;
    if ((DataLength > MAXUSHORT - RequestHeaderSize) ||
        (RequestTotalLength != RequestHeaderSize + DataLength))
    {
        return STATUS_INVALID_PARAMETER;
    }
    RequestData = (PUCHAR)ConnectionRequest + RequestHeaderSize;

    Status = AlpcpCapturePortAttributes(PortAttributes, PreviousMode, &Attributes, &Present);
    if (!NT_SUCCESS(Status)) return Status;

    Status = AlpcpReferencePortByHandle(ConnectionPortHandle, PORT_CONNECT, PreviousMode, &ConnectionPort);
    if (!NT_SUCCESS(Status)) return Status;

    if (AlpcpPortType(ConnectionPort) != ALPC_PORT_TYPE_CONNECTION)
    {
        ObDereferenceObject(ConnectionPort);
        return STATUS_INVALID_PORT_HANDLE;
    }

    if (DataLength > ConnectionPort->MaxConnectionInfoLength)
    {
        ObDereferenceObject(ConnectionPort);
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (DataLength)
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, DataLength, 'RcpA');
        if (!Buffer)
        {
            ObDereferenceObject(ConnectionPort);
            return STATUS_NO_MEMORY;
        }
        if (PreviousMode != KernelMode)
        {
            _SEH2_TRY
            {
                ProbeForRead(RequestData, DataLength, sizeof(UCHAR));
                RtlCopyMemory(Buffer, RequestData, DataLength);
            }
            _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
            {
                Status = _SEH2_GetExceptionCode();
            }
            _SEH2_END;
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(Buffer, 'RcpA');
                ObDereferenceObject(ConnectionPort);
                return Status;
            }
        }
        else
        {
            RtlCopyMemory(Buffer, RequestData, DataLength);
        }
    }

    Status = AlpcpCaptureAttributes(ConnectionMessageAttributes, PreviousMode, &SendAttributes);
    if (!NT_SUCCESS(Status))
    {
        if (Buffer) ExFreePoolWithTag(Buffer, 'RcpA');
        ObDereferenceObject(ConnectionPort);
        return Status;
    }

    AlpcpAcquireLock();
    Message = AlpcpFindPendingMessage(ConnectionPort, Header.MessageId, &Header.ClientId);
    if (!Message ||
        !(Message->State & ALPC_MSG_STATE_CONNECTION) ||
        !Message->WaitingThread ||
        (Message->State & (ALPC_MSG_STATE_ACCEPTED | ALPC_MSG_STATE_REFUSED | ALPC_MSG_STATE_CANCELED)))
    {
        AlpcpReleaseLock();
        if (Buffer) ExFreePoolWithTag(Buffer, 'RcpA');
        if (SendAttributes) ExFreePoolWithTag(SendAttributes, 'AcpA');
        ObDereferenceObject(ConnectionPort);
        return STATUS_REPLY_MESSAGE_MISMATCH;
    }
    ClientThread = Message->WaitingThread;
    ObReferenceObject(ClientThread);
    AlpcpRemovePending(Message);
    Message->State |= ALPC_MSG_STATE_ACCEPT_IN_PROGRESS;
    AlpcpReleaseLock();

    Message->PortMessage.u1.s1.DataLength = (CSHORT)DataLength;
    Message->PortMessage.u1.s1.TotalLength = (CSHORT)(sizeof(PORT_MESSAGE) + DataLength);
    Message->PortMessage.u2.s2.Type = LPC_CONNECTION_REPLY;
    Message->PortMessage.u2.s2.DataInfoOffset = 0;
    if (DataLength) RtlCopyMemory(&Message->PortMessage + 1, Buffer, DataLength);
    if (Buffer) ExFreePoolWithTag(Buffer, 'RcpA');

    if (!AcceptConnection)
    {
        AlpcpAcquireLock();
        Message->State &= ~ALPC_MSG_STATE_ACCEPT_IN_PROGRESS;
        AlpcpCompleteWithStatus(Message, ALPC_MSG_STATE_REFUSED, STATUS_PORT_CONNECTION_REFUSED);
        AlpcpReleaseLock();
        if (SendAttributes) ExFreePoolWithTag(SendAttributes, 'AcpA');
        ObDereferenceObject(ClientThread);
        ObDereferenceObject(ConnectionPort);
        return STATUS_SUCCESS;
    }

    Status = AlpcpCreatePort(&ServerPort, PreviousMode, ObjectAttributes, Present ? &Attributes : NULL, ALPC_PORT_TYPE_SERVER, 0, ConnectionPort->MaxConnectionInfoLength, ConnectionPort->MaxMessageLength);
    if (!NT_SUCCESS(Status)) goto Refuse;
    ServerPort->PortContext = PortContext;

    AlpcpReleaseMessageAttributes(Message);
    Status = AlpcpCaptureSendAttributes(ServerPort, Message, SendAttributes, PreviousMode);
    if (!NT_SUCCESS(Status)) goto Refuse;

    Info = ExAllocatePoolWithTag(PagedPool, sizeof(*Info), 'IcpA');
    if (!Info)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Refuse;
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
            goto Refuse;
        }
        Message->Connection.ClientView.ViewRemoteBase = ServerPort->ClientSectionBase;
    }

    ObReferenceObject(ServerPort);
    Status = ObInsertObject(ServerPort, NULL, PORT_ALL_ACCESS, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status))
    {
        ObDereferenceObject(ConnectionPort);
        ExFreePoolWithTag(Info, 'IcpA');
        ServerPort = NULL;
        goto Refuse;
    }

    AlpcpAcquireLock();
    ClientPort = Message->Connection.ClientPort;
    ServerPort->CommunicationInfo = Info;
    ClientPort->CommunicationInfo = Info;
    InsertTailList(&ConnectionPort->CommunicationPorts, &Info->CommunicationList);
    ServerPort->Creator = PsGetCurrentThread()->Cid;
    ClientPort->Creator = Message->PortMessage.ClientId;
    Message->State &= ~ALPC_MSG_STATE_ACCEPT_IN_PROGRESS;
    Message->State |= ALPC_MSG_STATE_ACCEPTED;
    AlpcpSignalWaiter(ClientThread);
    AlpcpReleaseLock();

    Status = AlpcpWriteHandle(PortHandle, Handle, PreviousMode);

    ObDereferenceObject(ClientThread);
    ObDereferenceObject(ServerPort);
    if (SendAttributes) ExFreePoolWithTag(SendAttributes, 'AcpA');
    ObDereferenceObject(ConnectionPort);
    return Status;

Refuse:
    AlpcpAcquireLock();
    Message->State &= ~ALPC_MSG_STATE_ACCEPT_IN_PROGRESS;
    AlpcpCompleteWithStatus(Message, ALPC_MSG_STATE_REFUSED, STATUS_PORT_CONNECTION_REFUSED);
    AlpcpReleaseLock();
    if (ServerPort) ObDereferenceObject(ServerPort);
    if (SendAttributes) ExFreePoolWithTag(SendAttributes, 'AcpA');
    ObDereferenceObject(ClientThread);
    ObDereferenceObject(ConnectionPort);
    return Status;
}
