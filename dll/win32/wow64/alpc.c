/*
 * PROJECT:     ReactOS WoW64 layer
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     ALPC system call argument conversion
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <stdarg.h>
#include "ntstatus.h"
#define WIN32_NO_STATUS
#include "windef.h"
#include "winbase.h"
#include "winnt.h"
#include "winternl.h"
#include "wow64_private.h"

typedef struct
{
    ULONG Flags;
    ULONG SequenceNo;
    ULONG PortContext;
} ALPC_BASIC_INFORMATION32;

typedef union
{
    struct { ULONG ThreadHandle; } In;
    struct
    {
        BOOLEAN ThreadBlocked;
        ULONG ConnectedProcessId;
        UNICODE_STRING32 ConnectionPortName;
    } Out;
} ALPC_SERVER_INFORMATION32;

typedef struct
{
    ULONG CompletionKey;
    ULONG CompletionPort;
} ALPC_PORT_ASSOCIATE_COMPLETION_PORT32;

typedef struct
{
    ULONG Buffer;
    ULONG Size;
} ALPC_PORT_MESSAGE_ZONE_INFORMATION32;

typedef struct
{
    ULONG Buffer;
    ULONG Size;
    ULONG ConcurrencyCount;
    ULONG AttributeFlags;
} ALPC_PORT_COMPLETION_LIST_INFORMATION32;

/* These operations identify an existing message; they do not read its payload. */
static ALPC_PORT_MESSAGE *alpc_message_header_32to64(ALPC_PORT_MESSAGE *out, const ALPC_PORT_MESSAGE32 *in)
{
    if (!in) return NULL;
    memset(out, 0, sizeof(*out));
    out->DataLength = in->DataLength;
    out->TotalLength = in->TotalLength;
    if (in->TotalLength >= sizeof(*in))
        out->TotalLength += sizeof(*out) - sizeof(*in);
    out->Type = in->Type;
    out->DataInfoOffset = in->DataInfoOffset;
    client_id_32to64(&out->ClientId, &in->ClientId);
    out->MessageId = in->MessageId;
    out->ClientViewSize = in->ClientViewSize;
    return out;
}

NTSTATUS WINAPI wow64_NtAlpcCancelMessage(UINT *args)
{
    HANDLE port = get_handle(&args);
    ULONG flags = get_ulong(&args);
    ALPC_CONTEXT_ATTR32 *context32 = get_ptr(&args);
    ALPC_CONTEXT_ATTR context;

    if (context32)
    {
        context.PortContext = ULongToPtr(context32->PortContext);
        context.MessageContext = ULongToPtr(context32->MessageContext);
        context.Sequence = context32->Sequence;
        context.MessageId = context32->MessageId;
        context.CallbackId = context32->CallbackId;
    }
    return NtAlpcCancelMessage(port, flags, context32 ? &context : NULL);
}

NTSTATUS WINAPI wow64_NtAlpcCreatePortSection(UINT *args)
{
    HANDLE port = get_handle(&args);
    ULONG flags = get_ulong(&args);
    HANDLE section = get_handle(&args);
    SIZE_T size = get_ulong(&args);
    ULONG *handle32 = get_ptr(&args);
    ULONG *actual32 = get_ptr(&args);
    HANDLE handle;
    SIZE_T actual;
    NTSTATUS status;

    status = NtAlpcCreatePortSection(port, flags, section, size, handle32 ? &handle : NULL, actual32 ? &actual : NULL);
    if (!status)
    {
        put_handle(handle32, handle);
        put_size(actual32, actual);
    }
    return status;
}

NTSTATUS WINAPI wow64_NtAlpcDeletePortSection(UINT *args)
{
    HANDLE port = get_handle(&args);
    ULONG flags = get_ulong(&args);
    HANDLE section = get_handle(&args);
    return NtAlpcDeletePortSection(port, flags, section);
}

NTSTATUS WINAPI wow64_NtAlpcCreateResourceReserve(UINT *args)
{
    HANDLE port = get_handle(&args);
    ULONG flags = get_ulong(&args);
    SIZE_T size = get_ulong(&args);
    ULONG *resource = get_ptr(&args);

    /* This is a native allocation size, not a marshalled message length. */
    return NtAlpcCreateResourceReserve(port, flags, size, resource);
}

NTSTATUS WINAPI wow64_NtAlpcDeleteResourceReserve(UINT *args)
{
    HANDLE port = get_handle(&args);
    ULONG flags = get_ulong(&args);
    ULONG resource = get_ulong(&args);
    return NtAlpcDeleteResourceReserve(port, flags, resource);
}

NTSTATUS WINAPI wow64_NtAlpcCreateSectionView(UINT *args)
{
    HANDLE port = get_handle(&args);
    ULONG flags = get_ulong(&args);
    ALPC_VIEW_ATTR32 *view32 = get_ptr(&args);
    ALPC_VIEW_ATTR view;
    NTSTATUS status;

    if (view32)
    {
        view.Flags = view32->Flags;
        view.SectionHandle = LongToHandle(view32->SectionHandle);
        view.ViewBase = ULongToPtr(view32->ViewBase);
        view.ViewSize = view32->ViewSize;
    }
    status = NtAlpcCreateSectionView(port, flags, view32 ? &view : NULL);
    if (!status)
    {
        if ((ULONG_PTR)view.ViewBase > MAXDWORD || view.ViewSize > MAXDWORD)
        {
            NtAlpcDeleteSectionView(port, 0, view.ViewBase);
            return STATUS_CONFLICTING_ADDRESSES;
        }
        view32->Flags = view.Flags;
        view32->SectionHandle = HandleToULong(view.SectionHandle);
        view32->ViewBase = PtrToUlong(view.ViewBase);
        view32->ViewSize = view.ViewSize;
    }
    return status;
}

NTSTATUS WINAPI wow64_NtAlpcDeleteSectionView(UINT *args)
{
    HANDLE port = get_handle(&args);
    ULONG flags = get_ulong(&args);
    void *base = get_ptr(&args);
    return NtAlpcDeleteSectionView(port, flags, base);
}

NTSTATUS WINAPI wow64_NtAlpcCreateSecurityContext(UINT *args)
{
    HANDLE port = get_handle(&args);
    ULONG flags = get_ulong(&args);
    ALPC_SECURITY_ATTR32 *attr32 = get_ptr(&args);
    ALPC_SECURITY_ATTR attr;
    NTSTATUS status;

    if (attr32)
    {
        attr.Flags = attr32->Flags;
        attr.QoS = ULongToPtr(attr32->QoSPointer);
        attr.ContextHandle = LongToHandle(attr32->ContextHandle);
    }
    status = NtAlpcCreateSecurityContext(port, flags, attr32 ? &attr : NULL);
    if (!status)
    {
        attr32->Flags = attr.Flags;
        attr32->ContextHandle = HandleToULong(attr.ContextHandle);
    }
    return status;
}

NTSTATUS WINAPI wow64_NtAlpcDeleteSecurityContext(UINT *args)
{
    HANDLE port = get_handle(&args);
    ULONG flags = get_ulong(&args);
    HANDLE context = get_handle(&args);
    return NtAlpcDeleteSecurityContext(port, flags, context);
}

NTSTATUS WINAPI wow64_NtAlpcRevokeSecurityContext(UINT *args)
{
    HANDLE port = get_handle(&args);
    ULONG flags = get_ulong(&args);
    HANDLE context = get_handle(&args);
    return NtAlpcRevokeSecurityContext(port, flags, context);
}

NTSTATUS WINAPI wow64_NtAlpcOpenSenderProcess(UINT *args)
{
    ULONG *handle32 = get_ptr(&args);
    HANDLE port = get_handle(&args);
    ALPC_PORT_MESSAGE32 *message32 = get_ptr(&args);
    ULONG flags = get_ulong(&args);
    ACCESS_MASK access = get_ulong(&args);
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr(&args);
    ALPC_PORT_MESSAGE message;
    struct object_attr64 attr;
    HANDLE handle;
    NTSTATUS status;

    status = NtAlpcOpenSenderProcess(handle32 ? &handle : NULL, port, alpc_message_header_32to64(&message, message32), flags, access, objattr_32to64(&attr, attr32));
    if (!status) put_handle(handle32, handle);
    return status;
}

NTSTATUS WINAPI wow64_NtAlpcOpenSenderThread(UINT *args)
{
    ULONG *handle32 = get_ptr(&args);
    HANDLE port = get_handle(&args);
    ALPC_PORT_MESSAGE32 *message32 = get_ptr(&args);
    ULONG flags = get_ulong(&args);
    ACCESS_MASK access = get_ulong(&args);
    OBJECT_ATTRIBUTES32 *attr32 = get_ptr(&args);
    ALPC_PORT_MESSAGE message;
    struct object_attr64 attr;
    HANDLE handle;
    NTSTATUS status;

    status = NtAlpcOpenSenderThread(handle32 ? &handle : NULL, port, alpc_message_header_32to64(&message, message32), flags, access, objattr_32to64(&attr, attr32));
    if (!status) put_handle(handle32, handle);
    return status;
}

NTSTATUS WINAPI wow64_NtAlpcImpersonateClientContainerOfPort(UINT *args)
{
    HANDLE port = get_handle(&args);
    ALPC_PORT_MESSAGE32 *message32 = get_ptr(&args);
    ULONG flags = get_ulong(&args);
    ALPC_PORT_MESSAGE message;
    return NtAlpcImpersonateClientContainerOfPort(port, alpc_message_header_32to64(&message, message32), flags);
}

NTSTATUS WINAPI wow64_NtAlpcQueryInformationMessage(UINT *args)
{
    HANDLE port = get_handle(&args);
    ALPC_PORT_MESSAGE32 *message32 = get_ptr(&args);
    ALPC_MESSAGE_INFORMATION_CLASS class = get_ulong(&args);
    void *info = get_ptr(&args);
    ULONG size = get_ulong(&args);
    ULONG *ret_size = get_ptr(&args);

    /* The message type tags its header layout, including native headers passed
     * by a 32-bit caller. The query outputs contain no pointer-sized fields. */
    return NtAlpcQueryInformationMessage(port, (ALPC_PORT_MESSAGE *)message32, class, info, size, ret_size);
}

NTSTATUS WINAPI wow64_NtAlpcQueryInformation(UINT *args)
{
    HANDLE port = get_handle(&args);
    ALPC_PORT_INFORMATION_CLASS class = get_ulong(&args);
    void *info = get_ptr(&args);
    ULONG size = get_ulong(&args);
    ULONG *ret_size = get_ptr(&args);
    ULONG native_size, returned = 0;
    NTSTATUS status;

    if (!info) return NtAlpcQueryInformation(port, class, NULL, size, ret_size);
    switch (class)
    {
    case AlpcBasicInformation:
    {
        ALPC_BASIC_INFORMATION basic;
        ALPC_BASIC_INFORMATION32 *basic32 = info;

        native_size = size >= sizeof(*basic32) ? sizeof(basic) : 0;
        status = NtAlpcQueryInformation(port, class, &basic, native_size, &returned);
        if (ret_size && returned) *ret_size = sizeof(*basic32);
        if (!status)
        {
            basic32->Flags = basic.Flags;
            basic32->SequenceNo = basic.SequenceNo;
            basic32->PortContext = PtrToUlong(basic.PortContext);
        }
        return status;
    }
    case AlpcServerInformation:
    {
        ALPC_SERVER_INFORMATION *server;
        ALPC_SERVER_INFORMATION32 *server32 = info;
        ULONG delta = sizeof(*server) - sizeof(*server32);

        if (size < sizeof(ULONG)) return STATUS_INVALID_PARAMETER;
        /* ConnectionPortName has a USHORT length; larger buffers add no capacity. */
        native_size = min(size, 0x10000 + sizeof(*server32)) + delta;
        if (!(server = Wow64AllocateTemp(max(native_size, sizeof(*server))))) return STATUS_NO_MEMORY;
        memset(server, 0, sizeof(*server));
        server->In.ThreadHandle = LongToHandle(server32->In.ThreadHandle);
        status = NtAlpcQueryInformation(port, class, server, native_size, &returned);
        if (ret_size && returned >= delta) *ret_size = returned - delta;
        if (!status)
        {
            memset(server32, 0, sizeof(*server32));
            server32->Out.ThreadBlocked = server->Out.ThreadBlocked;
            server32->Out.ConnectedProcessId = HandleToULong(server->Out.ConnectedProcessId);
            server32->Out.ConnectionPortName.Length = server->Out.ConnectionPortName.Length;
            server32->Out.ConnectionPortName.MaximumLength = server->Out.ConnectionPortName.MaximumLength;
            if (server->Out.ConnectionPortName.Buffer)
            {
                server32->Out.ConnectionPortName.Buffer = PtrToUlong(server32 + 1);
                memcpy(server32 + 1, server->Out.ConnectionPortName.Buffer, server->Out.ConnectionPortName.MaximumLength);
            }
        }
        return status;
    }
    default:
        return NtAlpcQueryInformation(port, class, info, size, ret_size);
    }
}

NTSTATUS WINAPI wow64_NtAlpcSetInformation(UINT *args)
{
    HANDLE port = get_handle(&args);
    ALPC_PORT_INFORMATION_CLASS class = get_ulong(&args);
    void *info = get_ptr(&args);
    ULONG size = get_ulong(&args);
    ALPC_PORT_ATTRIBUTES port_attr;
    ALPC_PORT_ASSOCIATE_COMPLETION_PORT associate;
    ALPC_PORT_MESSAGE_ZONE_INFORMATION zone;
    ALPC_PORT_COMPLETION_LIST_INFORMATION completion;

    if (!info) return NtAlpcSetInformation(port, class, NULL, size);
    switch (class)
    {
    case AlpcPortInformation:
        if (size < sizeof(ALPC_PORT_ATTRIBUTES32)) return STATUS_INFO_LENGTH_MISMATCH;
        alpc_port_attributes_32to64(&port_attr, info);
        info = &port_attr;
        size = sizeof(port_attr);
        break;
    case AlpcAssociateCompletionPortInformation:
    {
        ALPC_PORT_ASSOCIATE_COMPLETION_PORT32 *in = info;
        if (size < sizeof(*in)) return STATUS_INFO_LENGTH_MISMATCH;
        associate.CompletionKey = ULongToPtr(in->CompletionKey);
        associate.CompletionPort = LongToHandle(in->CompletionPort);
        info = &associate;
        size = sizeof(associate);
        break;
    }
    case AlpcMessageZoneInformation:
    {
        ALPC_PORT_MESSAGE_ZONE_INFORMATION32 *in = info;
        if (size < sizeof(*in)) return STATUS_INFO_LENGTH_MISMATCH;
        zone.Buffer = ULongToPtr(in->Buffer);
        zone.Size = in->Size;
        info = &zone;
        size = sizeof(zone);
        break;
    }
    case AlpcRegisterCompletionListInformation:
    {
        ALPC_PORT_COMPLETION_LIST_INFORMATION32 *in = info;
        if (size < sizeof(*in)) return STATUS_INFO_LENGTH_MISMATCH;
        completion.Buffer = ULongToPtr(in->Buffer);
        completion.Size = in->Size;
        completion.ConcurrencyCount = in->ConcurrencyCount;
        completion.AttributeFlags = in->AttributeFlags;
        info = &completion;
        size = sizeof(completion);
        break;
    }
    default:
        break;
    }
    return NtAlpcSetInformation(port, class, info, size);
}

NTSTATUS WINAPI wow64_NtAlpcConnectPortEx(UINT *args)
{
    ULONG *handle32 = get_ptr(&args);
    OBJECT_ATTRIBUTES32 *connection32 = get_ptr(&args);
    OBJECT_ATTRIBUTES32 *client32 = get_ptr(&args);
    ALPC_PORT_ATTRIBUTES32 *port_attr32 = get_ptr(&args);
    ULONG flags = get_ulong(&args);
    SECURITY_DESCRIPTOR *sd32 = get_ptr(&args);
    ALPC_PORT_MESSAGE32 *message32 = get_ptr(&args);
    ULONG *size32 = get_ptr(&args);
    ALPC_MESSAGE_ATTRIBUTES32 *send32 = get_ptr(&args);
    ALPC_MESSAGE_ATTRIBUTES32 *recv32 = get_ptr(&args);
    LARGE_INTEGER *timeout = get_ptr(&args);
    struct object_attr64 connection, client;
    ALPC_PORT_ATTRIBUTES port_attr;
    SECURITY_DESCRIPTOR sd;
    ALPC_PORT_MESSAGE *message;
    ALPC_MESSAGE_ATTRIBUTES *send, *recv;
    HANDLE handle;
    SIZE_T delta = sizeof(ALPC_PORT_MESSAGE) - sizeof(ALPC_PORT_MESSAGE32);
    SIZE_T size = size32 ? (SIZE_T)*size32 + delta : 65535;
    SIZE_T allocation = size;
    NTSTATUS status;

    if (message32)
    {
        if (message32->DataLength > 65535 - sizeof(ALPC_PORT_MESSAGE)) return STATUS_PORT_MESSAGE_TOO_LONG;
        allocation = max(allocation, sizeof(ALPC_PORT_MESSAGE) + message32->DataLength);
    }
    alpc_port_message_32to64(&message, allocation, message32, TRUE);
    alpc_port_message_attributes_32to64(&send, send32, TRUE);
    alpc_port_message_attributes_32to64(&recv, recv32, FALSE);
    if ((message32 && !message) || (send32 && !send) || (recv32 && !recv)) return STATUS_NO_MEMORY;

    status = NtAlpcConnectPortEx(handle32 ? &handle : NULL, objattr_32to64(&connection, connection32), objattr_32to64(&client, client32), alpc_port_attributes_32to64(&port_attr, port_attr32), flags, secdesc_32to64(&sd, sd32), message, size32 ? &size : NULL, send, recv, timeout);
    if (!status)
    {
        put_handle(handle32, handle);
        alpc_port_message_64to32(message32, message);
        alpc_port_message_attributes_64to32(send32, send);
        alpc_port_message_attributes_64to32(recv32, recv);
    }
    if (size32 && (!status || status == STATUS_BUFFER_TOO_SMALL))
        *size32 = size >= delta ? size - delta : size;
    return status;
}
