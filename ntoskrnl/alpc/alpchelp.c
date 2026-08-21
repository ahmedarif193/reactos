/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Exported Advanced Local Procedure Call attribute helpers
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

#include <ntoskrnl.h>

ULONG
NTAPI
AlpcMaxAllowedMessageLength(VOID)
{
    return ALPC_MAX_ALLOWED_MESSAGE_LENGTH;
}

ULONG
NTAPI
AlpcGetHeaderSize(
    _In_ ULONG AttributeFlags)
{
    ULONG Size = sizeof(ALPC_MESSAGE_ATTRIBUTES);

    if (AttributeFlags & ALPC_MESSAGE_SECURITY_ATTRIBUTE)
        Size += sizeof(ALPC_SECURITY_ATTR);
    if (AttributeFlags & ALPC_MESSAGE_VIEW_ATTRIBUTE)
        Size += sizeof(ALPC_VIEW_ATTR);
    if (AttributeFlags & ALPC_MESSAGE_CONTEXT_ATTRIBUTE)
        Size += sizeof(ALPC_CONTEXT_ATTR);
    if (AttributeFlags & ALPC_MESSAGE_HANDLE_ATTRIBUTE)
        Size += sizeof(ALPC_HANDLE_ATTR);
    if (AttributeFlags & ALPC_MESSAGE_TOKEN_ATTRIBUTE)
        Size += sizeof(ALPC_TOKEN_ATTR);
    if (AttributeFlags & ALPC_MESSAGE_DIRECT_ATTRIBUTE)
        Size += sizeof(ALPC_DIRECT_ATTR);
    if (AttributeFlags & ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE)
        Size += sizeof(ALPC_WORK_ON_BEHALF_ATTR);

    return Size;
}

PVOID
NTAPI
AlpcGetMessageAttribute(
    _In_ PALPC_MESSAGE_ATTRIBUTES Attributes,
    _In_ ULONG AttributeFlag)
{
    ULONG HigherAttributes;

    if (!(Attributes->AllocatedAttributes & AttributeFlag) ||
        !AttributeFlag ||
        (AttributeFlag & (AttributeFlag - 1)))
    {
        return NULL;
    }

    HigherAttributes = Attributes->AllocatedAttributes & (-AttributeFlag << 1);
    return (PUCHAR)Attributes + AlpcGetHeaderSize(HigherAttributes);
}

NTSTATUS
NTAPI
AlpcInitializeMessageAttribute(
    _In_ ULONG AttributeFlags,
    _Out_writes_bytes_opt_(BufferSize) PALPC_MESSAGE_ATTRIBUTES Buffer,
    _In_ SIZE_T BufferSize,
    _Out_ PSIZE_T RequiredBufferSize)
{
    SIZE_T RequiredSize;

    RequiredSize = AlpcGetHeaderSize(AttributeFlags);
    *RequiredBufferSize = RequiredSize;

    if (BufferSize < RequiredSize)
        return STATUS_BUFFER_TOO_SMALL;

    if (Buffer)
    {
        Buffer->AllocatedAttributes = AttributeFlags;
        Buffer->ValidAttributes = 0;
    }

    return STATUS_SUCCESS;
}
