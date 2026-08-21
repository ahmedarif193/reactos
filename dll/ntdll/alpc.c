/*
 * Advanced Local Procedure Call
 *
 * Copyright 2026 Zhiyi Zhang for CodeWeavers
 * Copyright 2026 Ahmed ARIF
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 */

#include <ntdll.h>
#include <ndk/lpcfuncs.h>

#define NDEBUG
#include <debug.h>

#define ALPC_COMPLETION_LIST_EMPTY          0x00FFFFFFULL
#define ALPC_COMPLETION_LIST_INDEX_MASK     0x00FFFFFFULL
#define ALPC_COMPLETION_LIST_TAIL_SHIFT     24
#define ALPC_COMPLETION_LIST_ACTIVE_SHIFT   48
#define ALPC_COMPLETION_LIST_GRANULARITY    64

/*
 * This is the user-mode layout shared with the kernel.  The cache-line
 * spacing is part of the ABI: producer and consumer counters deliberately
 * occupy separate cache lines.
 */
typedef struct _ALPC_COMPLETION_LIST_HEADER
{
    ULONGLONG StartMagic;
    ULONG TotalSize;
    ULONG ListOffset;
    ULONG ListSize;
    ULONG BitmapOffset;
    ULONG BitmapSize;
    ULONG DataOffset;
    ULONG DataSize;
    ULONG AttributeFlags;
    ULONG AttributeSize;
    UCHAR Reserved1[84];
    volatile LONGLONG State;
    ULONG LastMessageId;
    ULONG LastCallbackId;
    UCHAR Reserved2[112];
    volatile LONG PostCount;
    UCHAR Reserved3[124];
    volatile LONG ReturnCount;
    UCHAR Reserved4[124];
    volatile LONG LogSequenceNumber;
    UCHAR Reserved5[124];
    RTL_SRWLOCK UserLock;
#ifndef _WIN64
    ULONG UserLockPadding;
#endif
    ULONGLONG EndMagic;
    UCHAR Reserved6[112];
} ALPC_COMPLETION_LIST_HEADER, *PALPC_COMPLETION_LIST_HEADER;

C_ASSERT(FIELD_OFFSET(ALPC_COMPLETION_LIST_HEADER, State) == 0x80);
C_ASSERT(FIELD_OFFSET(ALPC_COMPLETION_LIST_HEADER, PostCount) == 0x100);
C_ASSERT(FIELD_OFFSET(ALPC_COMPLETION_LIST_HEADER, ReturnCount) == 0x180);
C_ASSERT(FIELD_OFFSET(ALPC_COMPLETION_LIST_HEADER, UserLock) == 0x280);
C_ASSERT(sizeof(ALPC_COMPLETION_LIST_HEADER) == 0x300);

static ULONG_PTR AlpcpCompletionListAlign(ULONG_PTR Value, ULONG Alignment)
{
    return (Value + Alignment - 1) & ~(Alignment - 1);
}

NTSTATUS WINAPI AlpcAdjustCompletionListConcurrencyCount(HANDLE port, ULONG concurrency_count)
{
    return NtAlpcSetInformation(port, AlpcAdjustCompletionListConcurrencyCountInformation, &concurrency_count, sizeof(concurrency_count));
}

void WINAPI AlpcFreeCompletionListMessage(void *completion_list, PORT_MESSAGE *message)
{
    PALPC_COMPLETION_LIST_HEADER header = completion_list;
    ULONG_PTR message_address = (ULONG_PTR)message;
    ULONG_PTR data_address = (ULONG_PTR)header + header->DataOffset;
    ULONG_PTR data_limit = data_address + header->DataSize;
    ULONG_PTR end_address;
    ULONG start_bit, end_bit, bit;
    volatile LONG *bitmap;

    if ((message_address & (ALPC_COMPLETION_LIST_GRANULARITY - 1)) ||
        message_address < data_address || message_address >= data_limit)
        return;

    end_address = message_address + (USHORT)message->u1.s1.TotalLength;
    if (header->AttributeFlags)
    {
        end_address = AlpcpCompletionListAlign(end_address, sizeof(ULONGLONG));
        end_address += header->AttributeSize;
    }
    if (end_address <= message_address || end_address > data_limit)
        return;

    start_bit = (ULONG)((message_address - data_address) / ALPC_COMPLETION_LIST_GRANULARITY);
    end_bit = (ULONG)((end_address - data_address + ALPC_COMPLETION_LIST_GRANULARITY - 1) /
                      ALPC_COMPLETION_LIST_GRANULARITY);
    bitmap = (volatile LONG *)((ULONG_PTR)header + header->BitmapOffset);
    for (bit = start_bit; bit < end_bit; bit++)
        InterlockedAnd(&bitmap[bit / 32], ~(LONG)(1u << (bit % 32)));

    InterlockedIncrement(&header->ReturnCount);
}

void WINAPI AlpcGetCompletionListLastMessageInformation(void *completion_list,
                                                         ULONG *last_message_id,
                                                         ULONG *last_callback_id)
{
    PALPC_COMPLETION_LIST_HEADER header = completion_list;

    *last_message_id = header->LastMessageId;
    *last_callback_id = header->LastCallbackId;
}

ALPC_MESSAGE_ATTRIBUTES *WINAPI AlpcGetCompletionListMessageAttributes(void *completion_list,
                                                                        PORT_MESSAGE *message)
{
    PALPC_COMPLETION_LIST_HEADER header = completion_list;
    ULONG_PTR attributes;

    if (!header->AttributeFlags)
        return NULL;

    attributes = (ULONG_PTR)message + (USHORT)message->u1.s1.TotalLength;
    attributes = AlpcpCompletionListAlign(attributes, sizeof(ULONGLONG));
    return (PALPC_MESSAGE_ATTRIBUTES)attributes;
}

PORT_MESSAGE *WINAPI AlpcGetMessageFromCompletionList(void *completion_list,
                                                       ALPC_MESSAGE_ATTRIBUTES **attributes)
{
    PALPC_COMPLETION_LIST_HEADER header = completion_list;
    volatile LONGLONG *state_pointer = &header->State;
    ULONGLONG state, new_state;
    ULONG head, tail, capacity, offset;
    PORT_MESSAGE *message = NULL;
    ULONG *list;

    RtlAcquireSRWLockExclusive(&header->UserLock);
    capacity = header->ListSize / sizeof(*list);
    list = (ULONG *)((ULONG_PTR)header + header->ListOffset);

    for (;;)
    {
        state = (ULONGLONG)*state_pointer;
        head = (ULONG)(state & ALPC_COMPLETION_LIST_INDEX_MASK);
        tail = (ULONG)((state >> ALPC_COMPLETION_LIST_TAIL_SHIFT) &
                       ALPC_COMPLETION_LIST_INDEX_MASK);
        if (head == ALPC_COMPLETION_LIST_EMPTY || !capacity ||
            head >= capacity || tail >= capacity)
            break;

        offset = list[head];
        if (head == tail)
        {
            new_state = (state & 0xFFFF000000000000ULL) |
                        (ALPC_COMPLETION_LIST_EMPTY << ALPC_COMPLETION_LIST_TAIL_SHIFT) |
                        ALPC_COMPLETION_LIST_EMPTY;
        }
        else
        {
            new_state = (state & ~ALPC_COMPLETION_LIST_INDEX_MASK) |
                        ((head + 1) % capacity);
        }

        if ((ULONGLONG)InterlockedCompareExchange64(state_pointer, (LONGLONG)new_state, (LONGLONG)state) == state)
        {
            if (offset < header->DataSize)
                message = (PORT_MESSAGE *)((ULONG_PTR)header + header->DataOffset + offset);
            break;
        }
    }

    if (message && attributes)
        *attributes = AlpcGetCompletionListMessageAttributes(header, message);
    RtlReleaseSRWLockExclusive(&header->UserLock);
    return message;
}

ULONG WINAPI AlpcGetOutstandingCompletionListMessageCount(void *completion_list)
{
    PALPC_COMPLETION_LIST_HEADER header = completion_list;
    return (ULONG)(header->PostCount - header->ReturnCount);
}

NTSTATUS WINAPI AlpcRegisterCompletionList(HANDLE port,
                                            void *completion_list,
                                            ULONG size,
                                            ULONG concurrency_count,
                                            ULONG attribute_flags)
{
    ALPC_PORT_COMPLETION_LIST_INFORMATION information;
    NTSTATUS status;

    information.Buffer = completion_list;
    information.Size = size;
    information.ConcurrencyCount = concurrency_count;
    information.AttributeFlags = attribute_flags;
    status = NtAlpcSetInformation(port, AlpcRegisterCompletionListInformation, &information, sizeof(information));
    if (NT_SUCCESS(status))
        ((PALPC_COMPLETION_LIST_HEADER)completion_list)->UserLock.Ptr = NULL;
    return status;
}

BOOLEAN WINAPI AlpcRegisterCompletionListWorkerThread(void *completion_list)
{
    PALPC_COMPLETION_LIST_HEADER header = completion_list;
    volatile LONGLONG *state_pointer = &header->State;
    ULONGLONG state, new_state;
    ULONG active;

    for (;;)
    {
        state = (ULONGLONG)*state_pointer;
        active = (ULONG)(state >> ALPC_COMPLETION_LIST_ACTIVE_SHIFT);
        if (active == 0xFFFF)
            return FALSE;
        new_state = state + (1ULL << ALPC_COMPLETION_LIST_ACTIVE_SHIFT);
        if ((ULONGLONG)InterlockedCompareExchange64(state_pointer, (LONGLONG)new_state, (LONGLONG)state) == state)
            return TRUE;
    }
}

NTSTATUS WINAPI AlpcRundownCompletionList(HANDLE port)
{
    return NtAlpcSetInformation(port, AlpcCompletionListRundownInformation, NULL, 0);
}

NTSTATUS WINAPI AlpcUnregisterCompletionList(HANDLE port)
{
    return NtAlpcSetInformation(port, AlpcUnregisterCompletionListInformation, NULL, 0);
}

BOOLEAN WINAPI AlpcUnregisterCompletionListWorkerThread(void *completion_list)
{
    PALPC_COMPLETION_LIST_HEADER header = completion_list;
    volatile LONGLONG *state_pointer = &header->State;
    ULONGLONG state, new_state;
    ULONG active;

    for (;;)
    {
        state = (ULONGLONG)*state_pointer;
        active = (ULONG)(state >> ALPC_COMPLETION_LIST_ACTIVE_SHIFT);
        if (!active || (state & ALPC_COMPLETION_LIST_INDEX_MASK) != ALPC_COMPLETION_LIST_EMPTY)
            return FALSE;
        new_state = state - (1ULL << ALPC_COMPLETION_LIST_ACTIVE_SHIFT);
        if ((ULONGLONG)InterlockedCompareExchange64(state_pointer, (LONGLONG)new_state, (LONGLONG)state) == state)
            return TRUE;
    }
}

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

    if ((attribute_flag & attributes->AllocatedAttributes) != attribute_flag)
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

ULONG WINAPI AlpcMaxAllowedMessageLength(void)
{
    return ALPC_MAX_ALLOWED_MESSAGE_LENGTH;
}
