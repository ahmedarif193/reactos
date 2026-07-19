/*
 * PROJECT:     ReactOS SDK
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     Simple Peripheral Bus client I/O interface
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#if (NTDDI_VERSION >= NTDDI_WIN8)

#define SPB_TARGET_NAME_PREFIX L"\\SPB\\"

typedef enum SpbIoctl
{
    IOCTL_SPB_LOCK_CONTROLLER = CTL_CODE(FILE_DEVICE_CONTROLLER, 0x600, METHOD_BUFFERED, FILE_ANY_ACCESS),
    IOCTL_SPB_UNLOCK_CONTROLLER = CTL_CODE(FILE_DEVICE_CONTROLLER, 0x601, METHOD_BUFFERED, FILE_ANY_ACCESS),
    IOCTL_SPB_EXECUTE_SEQUENCE = CTL_CODE(FILE_DEVICE_CONTROLLER, 0x602, METHOD_BUFFERED, FILE_ANY_ACCESS),
    IOCTL_SPB_LOCK_CONNECTION = CTL_CODE(FILE_DEVICE_CONTROLLER, 0x603, METHOD_BUFFERED, FILE_ANY_ACCESS),
    IOCTL_SPB_UNLOCK_CONNECTION = CTL_CODE(FILE_DEVICE_CONTROLLER, 0x604, METHOD_BUFFERED, FILE_ANY_ACCESS),
    IOCTL_SPB_FULL_DUPLEX = CTL_CODE(FILE_DEVICE_CONTROLLER, 0x605, METHOD_BUFFERED, FILE_ANY_ACCESS),
    IOCTL_SPB_MULTI_SPI_TRANSFER = CTL_CODE(FILE_DEVICE_CONTROLLER, 0x606, METHOD_BUFFERED, FILE_ANY_ACCESS)
} SpbIoctl, *PSpbIoctl;

typedef enum SPB_TRANSFER_DIRECTION
{
    SpbTransferDirectionNone,
    SpbTransferDirectionFromDevice,
    SpbTransferDirectionToDevice,
    SpbTransferDirectionMax
} SPB_TRANSFER_DIRECTION, *PSPB_TRANSFER_DIRECTION;

typedef enum SPB_TRANSFER_BUFFER_FORMAT
{
    SpbTransferBufferFormatInvalid,
    SpbTransferBufferFormatSimple,
    SpbTransferBufferFormatList,
    SpbTransferBufferFormatSimpleNonPaged,
    SpbTransferBufferFormatMdl,
    SpbTransferBufferFormatMax
} SPB_TRANSFER_BUFFER_FORMAT, *PSPB_TRANSFER_BUFFER_FORMAT;

typedef struct SPB_TRANSFER_BUFFER_LIST_ENTRY
{
    PVOID Buffer;
    ULONG BufferCb;
} SPB_TRANSFER_BUFFER_LIST_ENTRY, *PSPB_TRANSFER_BUFFER_LIST_ENTRY;

#ifndef _NTDDK_
#ifndef PMDL
#define PMDL PVOID
#endif
#endif

typedef struct SPB_TRANSFER_BUFFER
{
    SPB_TRANSFER_BUFFER_FORMAT Format;
    union
    {
        SPB_TRANSFER_BUFFER_LIST_ENTRY Simple;
        struct
        {
            PSPB_TRANSFER_BUFFER_LIST_ENTRY List;
            ULONG ListCe;
        } BufferList;
        PMDL Mdl;
    };
} SPB_TRANSFER_BUFFER, *PSPB_TRANSFER_BUFFER;

typedef struct SPB_TRANSFER_LIST_ENTRY
{
    SPB_TRANSFER_DIRECTION Direction;
    ULONG DelayInUs;
    SPB_TRANSFER_BUFFER Buffer;
} SPB_TRANSFER_LIST_ENTRY, *PSPB_TRANSFER_LIST_ENTRY;

typedef struct SPB_TRANSFER_LIST
{
    ULONG Size;
    ULONG Reserved;
    ULONG TransferCount;
    SPB_TRANSFER_LIST_ENTRY Transfers[1];
} SPB_TRANSFER_LIST, *PSPB_TRANSFER_LIST;

#define SPB_TRANSFER_LIST_AND_ENTRIES(n) struct { \
    SPB_TRANSFER_LIST List; \
    SPB_TRANSFER_LIST_ENTRY ExtraTransfers[(n) - 1]; \
}

FORCEINLINE
SPB_TRANSFER_LIST_ENTRY
SPB_TRANSFER_LIST_ENTRY_INIT_SIMPLE(
    _In_ SPB_TRANSFER_DIRECTION Direction,
    _In_ ULONG DelayInUs,
    _Pre_writable_byte_size_(BufferCb) PVOID Buffer,
    _In_ ULONG BufferCb)
{
    SPB_TRANSFER_LIST_ENTRY Entry;

    Entry.Direction = Direction;
    Entry.DelayInUs = DelayInUs;
    Entry.Buffer.Format = SpbTransferBufferFormatSimple;
    Entry.Buffer.Simple.Buffer = Buffer;
    Entry.Buffer.Simple.BufferCb = BufferCb;
    return Entry;
}

FORCEINLINE
SPB_TRANSFER_LIST_ENTRY
SPB_TRANSFER_LIST_ENTRY_INIT_NON_PAGED(
    _In_ SPB_TRANSFER_DIRECTION Direction,
    _In_ ULONG DelayInUs,
    _Pre_writable_byte_size_(BufferCb) PVOID Buffer,
    _In_ ULONG BufferCb)
{
    SPB_TRANSFER_LIST_ENTRY Entry;

    Entry.Direction = Direction;
    Entry.DelayInUs = DelayInUs;
    Entry.Buffer.Format = SpbTransferBufferFormatSimpleNonPaged;
    Entry.Buffer.Simple.Buffer = Buffer;
    Entry.Buffer.Simple.BufferCb = BufferCb;
    return Entry;
}

FORCEINLINE
SPB_TRANSFER_LIST_ENTRY
SPB_TRANSFER_LIST_ENTRY_INIT_MDL(
    _In_ SPB_TRANSFER_DIRECTION Direction,
    _In_ ULONG DelayInUs,
    _In_ PMDL Mdl)
{
    SPB_TRANSFER_LIST_ENTRY Entry;

    Entry.Direction = Direction;
    Entry.DelayInUs = DelayInUs;
    Entry.Buffer.Format = SpbTransferBufferFormatMdl;
    Entry.Buffer.Mdl = Mdl;
    return Entry;
}

FORCEINLINE
VOID
SPB_TRANSFER_LIST_INIT(
    _Out_ SPB_TRANSFER_LIST *TransferList,
    _In_ ULONG TransferCount)
{
    RtlZeroMemory(TransferList,
                  sizeof(SPB_TRANSFER_LIST) + sizeof(SPB_TRANSFER_LIST_ENTRY) * (TransferCount - 1));
    TransferList->Size = sizeof(SPB_TRANSFER_LIST);
    TransferList->TransferCount = TransferCount;
}

#endif /* NTDDI_VERSION >= NTDDI_WIN8 */
