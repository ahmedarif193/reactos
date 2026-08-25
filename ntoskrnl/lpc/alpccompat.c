/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Modern LPC compatibility exports over the classic LPC kernel
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <ntoskrnl.h>

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
    union
    {
        ULONGLONG Alignment;
        UCHAR Buffer[PORT_MAXIMUM_MESSAGE_LENGTH];
    } Reply;
    NTSTATUS Status;
    PPORT_MESSAGE ReplyMessage = (PPORT_MESSAGE)Reply.Buffer;
    SIZE_T AvailableLength = MAXULONG_PTR;
    SIZE_T RequiredLength;

    PAGED_CODE();

    if (!(Flags & ALPC_MSGFLG_SYNC_REQUEST) || (Flags & ALPC_MSGFLG_REPLY_MESSAGE)) return STATUS_INVALID_PARAMETER_2;
    if (!SendMessage) return STATUS_INVALID_PARAMETER_3;
    if (!ReceiveMessage) return STATUS_LPC_RECEIVE_BUFFER_EXPECTED;
    if (Timeout) return STATUS_NOT_SUPPORTED;
    if (BufferLength) AvailableLength = *BufferLength;

    Status = LpcRequestWaitReplyPort(PortObject, SendMessage, ReplyMessage);
    if (!NT_SUCCESS(Status)) return Status;

    RequiredLength = (USHORT)ReplyMessage->u1.s1.TotalLength;
    if (BufferLength) *BufferLength = RequiredLength;
    if (AvailableLength < RequiredLength) return STATUS_BUFFER_TOO_SMALL;

    RtlCopyMemory(ReceiveMessage, ReplyMessage, RequiredLength);
    return STATUS_SUCCESS;
}
