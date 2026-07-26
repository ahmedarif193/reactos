/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Validated logical $DATA stream enumeration
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

static BOOLEAN
IsStreamListEntryValid(_In_ PAttributeListEx Entry,
                       _In_ ULONG Remaining)
{
    const ULONG MinimumEntrySize = 0x1a;
    ULONG NameBytes;

    if (Remaining < MinimumEntrySize ||
        Entry->RecordLength < MinimumEntrySize ||
        (Entry->RecordLength & 7) != 0 ||
        Entry->RecordLength > Remaining)
    {
        return FALSE;
    }

    NameBytes = Entry->NameLength * sizeof(WCHAR);
    return Entry->NameLength == 0 ||
           (Entry->NameOffset >= MinimumEntrySize &&
            Entry->NameOffset <= Entry->RecordLength &&
            NameBytes <=
                (ULONG)Entry->RecordLength -
                    (ULONG)Entry->NameOffset);
}

static BOOLEAN
IsStreamAttributeNameValid(_In_ PAttribute Attribute)
{
    ULONG HeaderSize;
    ULONG NameBytes;

    if (!Attribute)
        return FALSE;
    if (Attribute->NameLength == 0)
        return TRUE;

    HeaderSize = Attribute->IsNonResident
        ? ((Attribute->Flags &
            (ATTR_COMPRESSION_MASK | ATTR_SPARSE))
            ? 0x48
            : 0x40)
        : 0x18;
    NameBytes =
        Attribute->NameLength * sizeof(WCHAR);
    return Attribute->NameOffset >= HeaderSize &&
           Attribute->NameOffset <= Attribute->Length &&
           NameBytes <=
               Attribute->Length -
                   Attribute->NameOffset;
}

static BOOLEAN
StreamAttributeMatchesListEntry(
    _In_ PAttribute Attribute,
    _In_ PAttributeListEx Entry)
{
    ULONG NameBytes;

    if (!IsStreamAttributeNameValid(Attribute) ||
        Attribute->NameLength != Entry->NameLength)
    {
        return FALSE;
    }

    NameBytes =
        Attribute->NameLength * sizeof(WCHAR);
    return NameBytes == 0 ||
           RtlCompareMemory(
               GetNamePointer(Attribute),
               reinterpret_cast<PUCHAR>(Entry) +
                   Entry->NameOffset,
               NameBytes) == NameBytes;
}

static NTSTATUS
AppendDataStream(
    _In_ PAttribute Attribute,
    _In_ ULONGLONG ClusterSize,
    _Out_opt_ PNtfsDataStreamInformation Streams,
    _In_ ULONG Capacity,
    _Inout_ PULONG Written)
{
    ULONGLONG AllocationSize;
    ULONGLONG DataSize;
    ULONG NameBytes;

    if (!Attribute ||
        Attribute->AttributeType != TypeData ||
        !IsStreamAttributeNameValid(Attribute))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    NameBytes =
        Attribute->NameLength * sizeof(WCHAR);
    for (ULONG Index = 0; Index < *Written; Index++)
    {
        if (Streams[Index].NameLength ==
                Attribute->NameLength &&
            (NameBytes == 0 ||
             RtlCompareMemory(
                 Streams[Index].Name,
                 GetNamePointer(Attribute),
                 NameBytes) == NameBytes))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
    }

    if (Attribute->IsNonResident)
    {
        if (Attribute->NonResident.FirstVCN != 0 ||
            ClusterSize == 0 ||
            Attribute->NonResident.InitalizedDataSize >
                Attribute->NonResident.DataSize ||
            Attribute->NonResident.DataSize >
                Attribute->NonResident.AllocatedSize ||
            Attribute->NonResident.AllocatedSize %
                ClusterSize != 0)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        DataSize =
            Attribute->NonResident.DataSize;
        AllocationSize =
            (Attribute->Flags &
             (ATTR_COMPRESSION_MASK | ATTR_SPARSE))
            ? Attribute->
                NonResident.CompressedDataSize
            : Attribute->
                NonResident.AllocatedSize;
        if (AllocationSize >
                Attribute->NonResident.AllocatedSize ||
            AllocationSize % ClusterSize != 0)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
    }
    else
    {
        if (Attribute->Flags != 0 ||
            Attribute->Resident.DataOffset < 0x18 ||
            Attribute->Resident.DataOffset >
                Attribute->Length ||
            Attribute->Resident.DataLength >
                Attribute->Length -
                    Attribute->Resident.DataOffset)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        DataSize =
            Attribute->Resident.DataLength;
        AllocationSize = 0;
    }

    if (*Written == Capacity)
    {
        return *Written == 0
            ? STATUS_BUFFER_TOO_SMALL
            : STATUS_BUFFER_OVERFLOW;
    }

    RtlZeroMemory(&Streams[*Written],
                  sizeof(Streams[*Written]));
    Streams[*Written].DataSize = DataSize;
    Streams[*Written].AllocationSize =
        AllocationSize;
    Streams[*Written].NameLength =
        Attribute->NameLength;
    if (NameBytes != 0)
    {
        RtlCopyMemory(
            Streams[*Written].Name,
            GetNamePointer(Attribute),
            NameBytes);
    }
    (*Written)++;
    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::QueryDataStreams(
    _Out_opt_ PNtfsDataStreamInformation Streams,
    _Inout_ PULONG StreamCount)
{
    PAttribute AttributeList;
    ULONGLONG ClusterSize;
    ULONG Capacity;
    ULONG Written = 0;
    NTSTATUS Status;

    if (!StreamCount ||
        (!Streams && *StreamCount != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }
    Capacity = *StreamCount;
    *StreamCount = 0;
    if (!Data || !Header ||
        Header->AttributeOffset <
            sizeof(FileRecordHeader) ||
        Header->AllocatedSize >
            RecordBufferSize ||
        Header->ActualSize > Header->AllocatedSize ||
        Header->AttributeOffset >= Header->ActualSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    ClusterSize = BytesPerCluster(DiskVolume);
    AttributeList = FindAttributeInRecord(
        TypeAttributeList,
        NULL,
        NULL);
    if (AttributeList)
    {
        ULONG Offset = 0;

        Status = LoadAttributeList();
        if (!NT_SUCCESS(Status) ||
            !AttributeListData ||
            AttributeListLength == 0)
        {
            return NT_SUCCESS(Status)
                ? STATUS_FILE_CORRUPT_ERROR
                : Status;
        }

        while (Offset < AttributeListLength)
        {
            PAttributeListEx Entry =
                reinterpret_cast<PAttributeListEx>(
                    AttributeListData + Offset);
            ULONG Remaining =
                AttributeListLength - Offset;

            if (!IsStreamListEntryValid(
                    Entry,
                    Remaining))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }

            if (Entry->Type == TypeData &&
                Entry->FirstVCN == 0)
            {
                PFileRecord TargetRecord;
                PAttribute StreamAttribute;
                ULONGLONG FileRecordNumber =
                    GetFRNFromFileRef(
                        Entry->BaseFileRef);
                USHORT SequenceNumber =
                    GetSequenceFromFileRef(
                        Entry->BaseFileRef);

                if (FileRecordNumber ==
                    Header->MFTRecordNumber)
                {
                    if (SequenceNumber != 0 &&
                        SequenceNumber !=
                            Header->SequenceNumber)
                    {
                        return STATUS_FILE_CORRUPT_ERROR;
                    }
                    TargetRecord = this;
                }
                else
                {
                    TargetRecord =
                        GetExtentRecord(
                            Entry->BaseFileRef);
                    if (!TargetRecord)
                        return STATUS_FILE_CORRUPT_ERROR;
                }

                StreamAttribute =
                    TargetRecord->
                        FindAttributeInRecord(
                            TypeData,
                            NULL,
                            &Entry->AttributeId);
                if (!StreamAttribute ||
                    !StreamAttributeMatchesListEntry(
                        StreamAttribute,
                        Entry))
                {
                    return STATUS_FILE_CORRUPT_ERROR;
                }

                Status = AppendDataStream(
                    StreamAttribute,
                    ClusterSize,
                    Streams,
                    Capacity,
                    &Written);
                if (!NT_SUCCESS(Status))
                {
                    *StreamCount = Written;
                    return Status;
                }
            }
            Offset += Entry->RecordLength;
        }
    }
    else
    {
        ULONG Offset = Header->AttributeOffset;
        BOOLEAN FoundEnd = FALSE;

        while (Offset < Header->ActualSize)
        {
            PAttribute Current;
            ULONG MinimumSize;
            ULONG Remaining =
                Header->ActualSize - Offset;

            if (Remaining < sizeof(ULONG))
                return STATUS_FILE_CORRUPT_ERROR;
            Current =
                reinterpret_cast<PAttribute>(
                    Data + Offset);
            if (Current->AttributeType ==
                TypeAttributeEndMarker)
            {
                FoundEnd = TRUE;
                break;
            }
            if (Remaining < 0x10)
                return STATUS_FILE_CORRUPT_ERROR;

            MinimumSize = Current->IsNonResident
                ? ((Current->Flags &
                    (ATTR_COMPRESSION_MASK |
                     ATTR_SPARSE))
                    ? 0x48
                    : 0x40)
                : 0x18;
            if (Current->Length < MinimumSize ||
                (Current->Length & 7) != 0 ||
                Current->Length > Remaining)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }

            if (Current->AttributeType == TypeData)
            {
                Status = AppendDataStream(
                    Current,
                    ClusterSize,
                    Streams,
                    Capacity,
                    &Written);
                if (!NT_SUCCESS(Status))
                {
                    *StreamCount = Written;
                    return Status;
                }
            }
            Offset += Current->Length;
        }
        if (!FoundEnd)
            return STATUS_FILE_CORRUPT_ERROR;
    }

    *StreamCount = Written;
    return STATUS_SUCCESS;
}
