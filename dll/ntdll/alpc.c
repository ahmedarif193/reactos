/*
 * Advanced Local Procedure Call
 *
 * Copyright 2026 Zhiyi Zhang for CodeWeavers
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <ntdll.h>

#define NDEBUG
#include <debug.h>

ULONG WINAPI AlpcGetHeaderSize(ULONG attribute_flags)
{
    static const struct
    {
        ULONG attribute;
        ULONG size;
    } attribute_sizes[] =
    {
        /* Attribute with a higher bit is stored before that with a lower bit */
        {ALPC_MESSAGE_SECURITY_ATTRIBUTE, sizeof(ALPC_SECURITY_ATTR)},
        {ALPC_MESSAGE_VIEW_ATTRIBUTE, sizeof(ALPC_VIEW_ATTR)},
        {ALPC_MESSAGE_CONTEXT_ATTRIBUTE, sizeof(ALPC_CONTEXT_ATTR)},
        {ALPC_MESSAGE_HANDLE_ATTRIBUTE, sizeof(ALPC_HANDLE_ATTR)},
        {ALPC_MESSAGE_TOKEN_ATTRIBUTE, sizeof(ALPC_TOKEN_ATTR)},
        {ALPC_MESSAGE_DIRECT_ATTRIBUTE, sizeof(ALPC_DIRECT_ATTR)},
        {ALPC_MESSAGE_WORK_ON_BEHALF_ATTRIBUTE, sizeof(ALPC_WORK_ON_BEHALF_ATTR)},
    };
    unsigned int i;
    ULONG size;

    size = sizeof(ALPC_MESSAGE_ATTRIBUTES);
    for (i = 0; i < RTL_NUMBER_OF(attribute_sizes); i++)
    {
        if (attribute_flags & attribute_sizes[i].attribute)
            size += attribute_sizes[i].size;
    }

    return size;
}

void * WINAPI AlpcGetMessageAttribute(ALPC_MESSAGE_ATTRIBUTES *attributes, ULONG attribute_flag)
{
    if (!attribute_flag)
        return NULL;

    if (attribute_flag & (attribute_flag - 1))
        return NULL;

    if ((attribute_flag & attributes->AllocatedAttributes & ALPC_MESSAGE_ATTRIBUTE_ALL) != attribute_flag)
        return NULL;

    return (unsigned char *)attributes + AlpcGetHeaderSize(attributes->AllocatedAttributes & ~(attribute_flag | (attribute_flag - 1)));
}

NTSTATUS WINAPI AlpcInitializeMessageAttribute(ULONG attribute_flags, ALPC_MESSAGE_ATTRIBUTES *buffer,
                                               SIZE_T buffer_size, SIZE_T *required_buffer_size)
{
    *required_buffer_size = AlpcGetHeaderSize(attribute_flags);

    if (buffer_size < *required_buffer_size)
        return STATUS_BUFFER_TOO_SMALL;

    if (buffer)
    {
        buffer->AllocatedAttributes = attribute_flags;
        buffer->ValidAttributes = 0;
    }

    return STATUS_SUCCESS;
}
