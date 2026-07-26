/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Bounded NTFS extended-attribute reading and iteration
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

static_assert(sizeof(EAInformationEx) == 8,
              "NTFS $EA_INFORMATION header must be 8 bytes");
static_assert(sizeof(EAEx) == 8,
              "NTFS $EA entry header must be 8 bytes");

static BOOLEAN
IsEaNameCharacterValid(_In_ UCHAR Character)
{
    if (Character < 0x20 || Character > 0x7e)
        return FALSE;

    switch (Character)
    {
        case '\\':
        case '/':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
        case ',':
        case '+':
        case '=':
        case '[':
        case ']':
        case ';':
            return FALSE;

        default:
            return TRUE;
    }
}

static NTSTATUS
ReadEaEntry(_In_ const UCHAR* Buffer,
            _In_ ULONG BufferLength,
            _In_ ULONG Offset,
            _Out_ PNtfsExtendedAttributeView View,
            _Out_ PULONG NextOffset)
{
    EAEx Header;
    ULONG EntryLength;
    ULONG PayloadLength;
    ULONG UsedLength;
    const UCHAR* Name;

    if (!Buffer || !View || !NextOffset)
        return STATUS_INVALID_PARAMETER;
    if (Offset >= BufferLength ||
        BufferLength - Offset < sizeof(EAEx))
    {
        return STATUS_EA_CORRUPT_ERROR;
    }

    RtlCopyMemory(&Header, Buffer + Offset, sizeof(Header));
    EntryLength = Header.OffsetNextEA;
    if (EntryLength == 0 ||
        (EntryLength & (sizeof(ULONG) - 1)) != 0 ||
        EntryLength > BufferLength - Offset)
    {
        return STATUS_EA_CORRUPT_ERROR;
    }

    if (Header.Flags & ~NTFS_EA_FLAG_NEED_EA)
        return STATUS_INVALID_EA_NAME;
    if (Header.NameLength == 0)
        return STATUS_INVALID_EA_NAME;

    PayloadLength = (ULONG)Header.NameLength + 1 +
                    (ULONG)Header.ValueLength;
    if (PayloadLength > EntryLength - sizeof(EAEx))
        return STATUS_EA_CORRUPT_ERROR;

    UsedLength = sizeof(EAEx) + PayloadLength;
    if (EntryLength - UsedLength >= sizeof(ULONG))
        return STATUS_EA_CORRUPT_ERROR;

    Name = Buffer + Offset + sizeof(EAEx);
    if (Name[Header.NameLength] != '\0')
        return STATUS_INVALID_EA_NAME;
    for (ULONG Index = 0; Index < Header.NameLength; Index++)
    {
        if (!IsEaNameCharacterValid(Name[Index]))
            return STATUS_INVALID_EA_NAME;
    }

    View->Flags = Header.Flags;
    View->NameLength = Header.NameLength;
    View->ValueLength = Header.ValueLength;
    View->Name = Name;
    View->Value = Name + Header.NameLength + 1;
    *NextOffset = Offset + EntryLength;
    return STATUS_SUCCESS;
}

extern "C" NTSTATUS
NtfsGetNextExtendedAttribute(
    _In_ const UCHAR* Buffer,
    _In_ ULONG BufferLength,
    _Inout_ PULONG Offset,
    _Out_ PNtfsExtendedAttributeView View)
{
    ULONG NextOffset;
    NTSTATUS Status;

    if (!Offset || !View || (!Buffer && BufferLength != 0))
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(View, sizeof(*View));
    if (*Offset == BufferLength)
        return STATUS_NO_MORE_EAS;
    if (*Offset > BufferLength)
        return STATUS_EA_CORRUPT_ERROR;

    Status = ReadEaEntry(Buffer,
                         BufferLength,
                         *Offset,
                         View,
                         &NextOffset);
    if (NT_SUCCESS(Status))
        *Offset = NextOffset;
    return Status;
}

static NTSTATUS
ValidateExtendedAttributes(_In_ const UCHAR* Buffer,
                           _In_ ULONG BufferLength,
                           _In_ const EAInformationEx* Information)
{
    NtfsExtendedAttributeView View;
    ULONG Offset = 0;
    ULONG PackedSize = 0;
    ULONG PackedEntrySize;
    ULONG NeedEaCount = 0;
    ULONG EntryCount = 0;
    NTSTATUS Status;

    if (!Buffer || BufferLength == 0 || !Information ||
        Information->UnpackedEASize != BufferLength)
    {
        return STATUS_EA_CORRUPT_ERROR;
    }

    while (Offset < BufferLength)
    {
        Status = NtfsGetNextExtendedAttribute(Buffer,
                                               BufferLength,
                                               &Offset,
                                               &View);
        if (!NT_SUCCESS(Status))
            return Status;

        PackedEntrySize =
            5UL + View.NameLength + View.ValueLength;
        if (PackedEntrySize > MAXUSHORT ||
            PackedSize > MAXUSHORT - PackedEntrySize)
        {
            return STATUS_EA_CORRUPT_ERROR;
        }
        PackedSize += PackedEntrySize;

        if (View.Flags & NTFS_EA_FLAG_NEED_EA)
        {
            if (NeedEaCount == MAXUSHORT)
                return STATUS_EA_CORRUPT_ERROR;
            NeedEaCount++;
        }
        EntryCount++;
    }

    if (EntryCount == 0 ||
        Offset != BufferLength ||
        Information->PackedEASize != PackedSize ||
        Information->NumEAWithNEED_EA != NeedEaCount)
    {
        return STATUS_EA_CORRUPT_ERROR;
    }

    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::ReadExtendedAttributes(
    _In_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength,
    _Out_opt_ PEAInformationEx Information)
{
    PAttribute EaAttribute;
    PAttribute InformationAttribute;
    EAInformationEx EaInformation;
    PUCHAR ReadBuffer;
    PUCHAR TemporaryBuffer = NULL;
    ULONGLONG AttributeLength;
    ULONG BytesRemaining;
    ULONG Capacity;
    NTSTATUS Status;

    if (!BufferLength)
        return STATUS_INVALID_PARAMETER;
    if (Information)
        RtlZeroMemory(Information, sizeof(*Information));

    InformationAttribute = GetAttribute(TypeEAInformation, NULL);
    EaAttribute = GetAttribute(TypeEA, NULL);
    if (!InformationAttribute && !EaAttribute)
    {
        *BufferLength = 0;
        return STATUS_NO_EAS_ON_FILE;
    }
    if (!InformationAttribute || !EaAttribute ||
        InformationAttribute->IsNonResident ||
        GetAttributeDataSize(InformationAttribute) !=
            sizeof(EAInformationEx))
    {
        *BufferLength = 0;
        return STATUS_EA_CORRUPT_ERROR;
    }

    BytesRemaining = sizeof(EaInformation);
    Status = CopyData(InformationAttribute,
                      reinterpret_cast<PUCHAR>(&EaInformation),
                      &BytesRemaining,
                      0);
    if (!NT_SUCCESS(Status))
        return Status;
    if (BytesRemaining != 0)
        return STATUS_EA_CORRUPT_ERROR;

    AttributeLength = GetAttributeDataSize(EaAttribute);
    if (AttributeLength == 0 ||
        AttributeLength > MAXULONG ||
        EaInformation.UnpackedEASize != AttributeLength)
    {
        *BufferLength = 0;
        return STATUS_EA_CORRUPT_ERROR;
    }

    Capacity = *BufferLength;
    *BufferLength = (ULONG)AttributeLength;
    if (Buffer && Capacity >= AttributeLength)
    {
        ReadBuffer = Buffer;
    }
    else
    {
        TemporaryBuffer =
            new(NonPagedPool) UCHAR[(ULONG)AttributeLength];
        if (!TemporaryBuffer)
            return STATUS_INSUFFICIENT_RESOURCES;
        ReadBuffer = TemporaryBuffer;
    }

    BytesRemaining = (ULONG)AttributeLength;
    Status = CopyData(EaAttribute,
                      ReadBuffer,
                      &BytesRemaining,
                      0);
    if (NT_SUCCESS(Status) && BytesRemaining != 0)
        Status = STATUS_EA_CORRUPT_ERROR;
    if (NT_SUCCESS(Status))
    {
        Status = ValidateExtendedAttributes(ReadBuffer,
                                            (ULONG)AttributeLength,
                                            &EaInformation);
    }

    delete[] TemporaryBuffer;
    if (!NT_SUCCESS(Status))
        return Status;

    if (Information)
        *Information = EaInformation;
    if (!Buffer || Capacity < AttributeLength)
        return STATUS_BUFFER_TOO_SMALL;

    return STATUS_SUCCESS;
}
