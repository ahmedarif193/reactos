/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

/*
 * Unnamed attributes are singletons in a record, with one exception:
 * $FILE_NAME repeats once per hard link and per DOS/WIN32 alias.
 */
static BOOLEAN
NtfsUnnamedAttributeMayRepeat(_In_ AttributeType Type)
{
    return Type == TypeFileName;
}

static ULONG
SignedMappingBytes(_In_ LONGLONG Value)
{
    for (ULONG Bytes = 1;
         Bytes < sizeof(Value);
         Bytes++)
    {
        ULONG SignBit = Bytes * 8 - 1;
        LONGLONG Minimum =
            -((LONGLONG)1 << SignBit);
        LONGLONG Maximum =
            ((LONGLONG)1 << SignBit) - 1;

        if (Value >= Minimum && Value <= Maximum)
            return Bytes;
    }
    return sizeof(Value);
}

/*
 * Run lengths use the same signed variable-length encoding as LCN deltas:
 * a stored top byte with its high bit set decodes as a negative length,
 * which Windows and NTFS-3G reject as corrupt. A positive length must
 * therefore keep one spare high bit in its most significant stored byte.
 */
static ULONG
LengthMappingBytes(_In_ ULONGLONG Value)
{
    return SignedMappingBytes((LONGLONG)Value);
}

static NTSTATUS
GetMappingPairsSize(_In_ PDataRun Runs,
                    _Out_ PULONG MappingBytes,
                    _Out_opt_ PULONGLONG TotalClusters)
{
    const ULONGLONG MaximumPositive =
        (~(ULONGLONG)0) >> 1;
    ULONGLONG Clusters = 0;
    ULONGLONG PreviousLCN = 0;
    ULONG Required = 1; /* Terminator. */

    if (!Runs || !MappingBytes)
        return STATUS_INVALID_PARAMETER;

    for (PDataRun Run = Runs; Run; Run = Run->NextRun)
    {
        ULONGLONG Difference;
        LONGLONG Delta;
        ULONG EntryBytes;

        if (Run->Length == 0 ||
            Run->Length > MaximumPositive ||
            Clusters > ~(ULONGLONG)0 - Run->Length)
        {
            return STATUS_INVALID_PARAMETER;
        }

        if (Run->IsSparse)
        {
            EntryBytes =
                1 + LengthMappingBytes(Run->Length);
        }
        else if (Run->LCN >= PreviousLCN)
        {
            Difference = Run->LCN - PreviousLCN;
            if (Difference > MaximumPositive)
                return STATUS_FILE_TOO_LARGE;
            Delta = (LONGLONG)Difference;
            EntryBytes = 1 +
                LengthMappingBytes(Run->Length) +
                SignedMappingBytes(Delta);
        }
        else
        {
            Difference = PreviousLCN - Run->LCN;
            if (Difference > MaximumPositive)
                return STATUS_FILE_TOO_LARGE;
            Delta = -(LONGLONG)Difference;
            EntryBytes = 1 +
                LengthMappingBytes(Run->Length) +
                SignedMappingBytes(Delta);
        }

        if (Required > MAXULONG - EntryBytes)
            return STATUS_FILE_TOO_LARGE;
        Required += EntryBytes;
        Clusters += Run->Length;
        if (!Run->IsSparse)
            PreviousLCN = Run->LCN;
    }

    *MappingBytes = Required;
    if (TotalClusters)
        *TotalClusters = Clusters;
    return STATUS_SUCCESS;
}

static NTSTATUS
EncodeMappingPairs(_In_ PDataRun Runs,
                   _Out_ PUCHAR Buffer,
                   _In_ ULONG BufferLength,
                   _Out_opt_ PULONG BytesWritten)
{
    ULONGLONG PreviousLCN = 0;
    ULONG Required;
    ULONG Offset = 0;
    NTSTATUS Status;

    Status = GetMappingPairsSize(Runs, &Required, NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Buffer || BufferLength < Required)
        return STATUS_BUFFER_TOO_SMALL;

    for (PDataRun Run = Runs; Run; Run = Run->NextRun)
    {
        LONGLONG Delta = Run->IsSparse
            ? 0
            : (Run->LCN >= PreviousLCN
                ? (LONGLONG)(Run->LCN - PreviousLCN)
                : -(LONGLONG)(PreviousLCN - Run->LCN));
        ULONGLONG EncodedDelta = (ULONGLONG)Delta;
        ULONG LengthBytes =
            LengthMappingBytes(Run->Length);
        ULONG OffsetBytes = Run->IsSparse
            ? 0
            : SignedMappingBytes(Delta);

        Buffer[Offset++] =
            (UCHAR)((OffsetBytes << 4) | LengthBytes);
        for (ULONG Index = 0; Index < LengthBytes; Index++)
        {
            Buffer[Offset++] =
                (UCHAR)(Run->Length >> (Index * 8));
        }
        for (ULONG Index = 0; Index < OffsetBytes; Index++)
        {
            Buffer[Offset++] =
                (UCHAR)(EncodedDelta >> (Index * 8));
        }
        if (!Run->IsSparse)
            PreviousLCN = Run->LCN;
    }
    Buffer[Offset++] = 0;

    if (BytesWritten)
        *BytesWritten = Offset;
    return Offset == Required
        ? STATUS_SUCCESS
        : STATUS_FILE_CORRUPT_ERROR;
}

static NTSTATUS
CompareAttributeNames(_In_ PVolume DiskVolume,
                      _In_ PWSTR Left,
                      _In_ ULONG LeftLength,
                      _In_ PWSTR Right,
                      _In_ ULONG RightLength,
                      _Out_ LONG* Result)
{
    UNICODE_STRING LeftString;
    UNICODE_STRING RightString;
    ULONG CommonLength;
    NTSTATUS Status;

    if (!DiskVolume || !Left || !Right || !Result ||
        LeftLength > MAXUSHORT / sizeof(WCHAR) ||
        RightLength > MAXUSHORT / sizeof(WCHAR))
    {
        return STATUS_INVALID_PARAMETER;
    }

    LeftString.Buffer = Left;
    LeftString.Length =
        (USHORT)(LeftLength * sizeof(WCHAR));
    LeftString.MaximumLength = LeftString.Length;
    RightString.Buffer = Right;
    RightString.Length =
        (USHORT)(RightLength * sizeof(WCHAR));
    RightString.MaximumLength = RightString.Length;

    /*
     * NTFS orders named attributes by the volume's uppercase collation, then
     * by the original UTF-16 code units. The secondary comparison is needed
     * because attribute names are case-sensitive even though "B" and "b"
     * have the same primary collation key.
     */
    Status = DiskVolume->CompareFileNames(&LeftString,
                                          &RightString,
                                          Result);
    if (!NT_SUCCESS(Status) || *Result != 0)
        return Status;

    CommonLength = min(LeftLength, RightLength);
    for (ULONG Index = 0; Index < CommonLength; Index++)
    {
        if (Left[Index] != Right[Index])
        {
            *Result = Left[Index] < Right[Index] ? -1 : 1;
            return STATUS_SUCCESS;
        }
    }

    *Result = LeftLength == RightLength
        ? 0
        : (LeftLength < RightLength ? -1 : 1);
    return STATUS_SUCCESS;
}

static BOOLEAN
IsWritableAttributeListEntryValid(
    _In_ PAttributeListEx Entry,
    _In_ ULONG Remaining)
{
    const ULONG MinimumEntrySize = 0x1a;
    ULONG NameBytes;

    if (!Entry ||
        Remaining < MinimumEntrySize ||
        Entry->RecordLength < MinimumEntrySize ||
        (Entry->RecordLength &
         (sizeof(ULONGLONG) - 1)) != 0 ||
        Entry->RecordLength > Remaining)
    {
        return FALSE;
    }

    NameBytes =
        Entry->NameLength * sizeof(WCHAR);
    return Entry->NameLength == 0
        ? Entry->NameOffset == 0 ||
          (Entry->NameOffset >= MinimumEntrySize &&
           Entry->NameOffset <= Entry->RecordLength)
        : (Entry->NameOffset >= MinimumEntrySize &&
           Entry->NameOffset <= Entry->RecordLength &&
           NameBytes <=
               (ULONG)Entry->RecordLength -
                   (ULONG)Entry->NameOffset);
}

typedef struct _INITIAL_ATTRIBUTE_LIST_ENTRY
{
    PAttribute Attribute;
    PFileRecord Owner;
    PWSTR Name;
    ULONG NameLength;
    ULONGLONG FirstVcn;
    ULONG RecordLength;
} INITIAL_ATTRIBUTE_LIST_ENTRY,
 *PINITIAL_ATTRIBUTE_LIST_ENTRY;

typedef struct _NONRESIDENT_MAPPING_NEW_EXTENT
{
    struct _NONRESIDENT_MAPPING_NEW_EXTENT* Next;
    PFileRecord Record;
    PAttribute Attribute;
    ULONGLONG FirstVcn;
} NONRESIDENT_MAPPING_NEW_EXTENT,
 *PNONRESIDENT_MAPPING_NEW_EXTENT;

typedef struct _NONRESIDENT_MAPPING_OLD_EXTENT
{
    struct _NONRESIDENT_MAPPING_OLD_EXTENT* Next;
    ULONGLONG FileReference;
    ULONGLONG FirstVcn;
    USHORT AttributeId;
} NONRESIDENT_MAPPING_OLD_EXTENT,
 *PNONRESIDENT_MAPPING_OLD_EXTENT;

struct NonResidentMappingUpdate
{
    PFileRecord BaseRecord;
    PFileRecord AttributeOwner;
    PFileRecord SupersededOwner;
    PDataRun AddedListRuns;
    PDataRun SupersededListRuns;
    PUCHAR BaseRecordBackup;
    PUCHAR OwnerRecordBackup;
    PUCHAR OldList;
    PUCHAR NewList;
    PWSTR Name;
    ULONG OldListLength;
    ULONG NewListLength;
    ULONG NameLength;
    AttributeType Type;
    USHORT SupersededAttributeId;
    PNONRESIDENT_MAPPING_NEW_EXTENT NewExtents;
    PNONRESIDENT_MAPPING_OLD_EXTENT OldExtents;
    BOOLEAN OwnerWriteAttempted;
    BOOLEAN ListWriteAttempted;
};

static NTSTATUS
GetMappingPairEntrySize(
    _In_ PDataRun Run,
    _In_ ULONGLONG PreviousLcn,
    _Out_ PULONG EntryBytes)
{
    const ULONGLONG MaximumPositive =
        (~(ULONGLONG)0) >> 1;
    ULONGLONG Difference;
    LONGLONG Delta;

    if (!Run || !EntryBytes || Run->Length == 0 ||
        Run->Length > MaximumPositive)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Run->IsSparse)
    {
        *EntryBytes =
            1 + LengthMappingBytes(Run->Length);
        return STATUS_SUCCESS;
    }

    if (Run->LCN >= PreviousLcn)
    {
        Difference = Run->LCN - PreviousLcn;
        if (Difference > MaximumPositive)
            return STATUS_FILE_TOO_LARGE;
        Delta = (LONGLONG)Difference;
    }
    else
    {
        Difference = PreviousLcn - Run->LCN;
        if (Difference > MaximumPositive)
            return STATUS_FILE_TOO_LARGE;
        Delta = -(LONGLONG)Difference;
    }

    *EntryBytes =
        1 + LengthMappingBytes(Run->Length) +
        SignedMappingBytes(Delta);
    return STATUS_SUCCESS;
}

static NTSTATUS
CloneMappingSegment(
    _Inout_ PDataRun* Cursor,
    _In_ ULONG DataRunsOffset,
    _In_ ULONG MaximumAttributeLength,
    _Out_ PDataRun* Segment,
    _Out_ PULONGLONG SegmentClusters)
{
    PDataRun Head = NULL;
    PDataRun Tail = NULL;
    PDataRun Current;
    ULONGLONG Clusters = 0;
    ULONGLONG PreviousLcn = 0;
    ULONG MappingBytes = 1;
    NTSTATUS Status;

    if (!Cursor || !*Cursor || !Segment ||
        !SegmentClusters ||
        DataRunsOffset >= MaximumAttributeLength)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *Segment = NULL;
    *SegmentClusters = 0;
    Current = *Cursor;

    while (Current)
    {
        PDataRun Copy;
        ULONG EntryBytes;
        ULONG CandidateBytes;
        ULONG CandidateLength;

        Status = GetMappingPairEntrySize(
            Current,
            PreviousLcn,
            &EntryBytes);
        if (!NT_SUCCESS(Status))
            goto Failure;
        if (MappingBytes > MAXULONG - EntryBytes)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Failure;
        }
        CandidateBytes = MappingBytes + EntryBytes;
        if (DataRunsOffset >
            MAXULONG - CandidateBytes)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Failure;
        }
        CandidateLength = ALIGN_UP_BY(
            DataRunsOffset + CandidateBytes,
            sizeof(ULONGLONG));
        if (CandidateLength >
            MaximumAttributeLength)
        {
            break;
        }
        if (Clusters >
            ~(ULONGLONG)0 - Current->Length)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Failure;
        }

        Copy = new(PagedPool, TAG_DATA_RUN)
            DataRun();
        if (!Copy)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Failure;
        }
        Copy->NextRun = NULL;
        Copy->LCN = Current->LCN;
        Copy->Length = Current->Length;
        Copy->IsSparse = Current->IsSparse;
        if (Tail)
            Tail->NextRun = Copy;
        else
            Head = Copy;
        Tail = Copy;

        MappingBytes = CandidateBytes;
        Clusters += Current->Length;
        if (!Current->IsSparse)
            PreviousLcn = Current->LCN;
        Current = Current->NextRun;
    }

    if (!Head)
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Failure;
    }

    *Cursor = Current;
    *Segment = Head;
    *SegmentClusters = Clusters;
    return STATUS_SUCCESS;

Failure:
    FreeDataRun(Head);
    return Status;
}

static NTSTATUS
AppendMappingRunCopy(
    _Inout_ PDataRun* Head,
    _Inout_ PDataRun* Tail,
    _In_ PDataRun Source)
{
    PDataRun Copy;

    if (!Head || !Tail || !Source ||
        Source->IsSparse ||
        Source->Length == 0 ||
        Source->LCN >
            ~(ULONGLONG)0 - Source->Length)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (*Tail &&
        (*Tail)->LCN <=
            ~(ULONGLONG)0 - (*Tail)->Length &&
        (*Tail)->LCN + (*Tail)->Length ==
            Source->LCN &&
        (*Tail)->Length <=
            ~(ULONGLONG)0 - Source->Length)
    {
        (*Tail)->Length += Source->Length;
        return STATUS_SUCCESS;
    }

    Copy = new(PagedPool, TAG_DATA_RUN)
        DataRun();
    if (!Copy)
        return STATUS_INSUFFICIENT_RESOURCES;
    Copy->NextRun = NULL;
    Copy->LCN = Source->LCN;
    Copy->Length = Source->Length;
    Copy->IsSparse = FALSE;
    if (*Tail)
        (*Tail)->NextRun = Copy;
    else
        *Head = Copy;
    *Tail = Copy;
    return STATUS_SUCCESS;
}

static NTSTATUS
CombineMappingRunLists(
    _In_ PDataRun Existing,
    _In_ PDataRun Added,
    _Out_ PDataRun* Combined)
{
    PDataRun Head = NULL;
    PDataRun Tail = NULL;
    NTSTATUS Status;

    if ((!Existing && !Added) || !Combined)
        return STATUS_INVALID_PARAMETER;
    *Combined = NULL;

    for (PDataRun Run = Existing;
         Run;
         Run = Run->NextRun)
    {
        Status = AppendMappingRunCopy(
            &Head,
            &Tail,
            Run);
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    for (PDataRun Run = Added;
         Run;
         Run = Run->NextRun)
    {
        Status = AppendMappingRunCopy(
            &Head,
            &Tail,
            Run);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    *Combined = Head;
    Head = NULL;
    Status = STATUS_SUCCESS;

Done:
    FreeDataRun(Head);
    return Status;
}

static NTSTATUS
CompareAttributeListKeys(
    _In_ PVolume DiskVolume,
    _In_ ULONG LeftType,
    _In_reads_(LeftNameLength)
        PWSTR LeftName,
    _In_ ULONG LeftNameLength,
    _In_ ULONGLONG LeftFirstVcn,
    _In_ ULONG RightType,
    _In_reads_(RightNameLength)
        PWSTR RightName,
    _In_ ULONG RightNameLength,
    _In_ ULONGLONG RightFirstVcn,
    _Out_ LONG* Result)
{
    NTSTATUS Status;

    if (!DiskVolume || !Result ||
        (!LeftName && LeftNameLength != 0) ||
        (!RightName && RightNameLength != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (LeftType != RightType)
    {
        *Result = LeftType < RightType ? -1 : 1;
        return STATUS_SUCCESS;
    }
    if (LeftNameLength == 0 ||
        RightNameLength == 0)
    {
        if (LeftNameLength != RightNameLength)
        {
            *Result =
                LeftNameLength == 0 ? -1 : 1;
            return STATUS_SUCCESS;
        }
    }
    else
    {
        Status = CompareAttributeNames(
            DiskVolume,
            LeftName,
            LeftNameLength,
            RightName,
            RightNameLength,
            Result);
        if (!NT_SUCCESS(Status) || *Result != 0)
            return Status;
    }

    *Result = LeftFirstVcn == RightFirstVcn
        ? 0
        : (LeftFirstVcn < RightFirstVcn
            ? -1
            : 1);
    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::ValidateAttributeForUpdate(
    _In_ PAttribute TargetAttribute,
    _In_ BOOLEAN ExpectedNonResident,
    _Out_opt_ PULONG DataLength)
{
    PAttribute Attribute;
    ULONG MinimumSize;
    ULONG NameBytes;
    ULONG Offset;
    ULONG Remaining;
    BOOLEAN Found = FALSE;

    if (DataLength)
        *DataLength = 0;
    if (!Data || !Header || !TargetAttribute ||
        RecordBufferSize < sizeof(FileRecordHeader) ||
        Header != reinterpret_cast<PFileRecordHeader>(Data) ||
        Header->AllocatedSize > RecordBufferSize ||
        Header->AllocatedSize < sizeof(FileRecordHeader) ||
        Header->ActualSize > Header->AllocatedSize ||
        Header->ActualSize < sizeof(FileRecordHeader) ||
        Header->AttributeOffset < sizeof(FileRecordHeader) ||
        Header->AttributeOffset >= Header->ActualSize ||
        (Header->AttributeOffset & (sizeof(ULONGLONG) - 1)) != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Offset = Header->AttributeOffset;
    while (Offset < Header->ActualSize)
    {
        Remaining = Header->ActualSize - Offset;
        if (Remaining < sizeof(ULONG))
            return STATUS_FILE_CORRUPT_ERROR;

        Attribute = reinterpret_cast<PAttribute>(Data + Offset);
        if (Attribute->AttributeType == TypeAttributeEndMarker)
        {
            if (!Found)
                return STATUS_INVALID_PARAMETER;
            return STATUS_SUCCESS;
        }

        if (Attribute->IsNonResident > 1)
            return STATUS_FILE_CORRUPT_ERROR;
        MinimumSize = Attribute->IsNonResident
            ? ((Attribute->Flags &
                (ATTR_COMPRESSION_MASK | ATTR_SPARSE))
                ? 0x48
                : 0x40)
            : 0x18;
        if (Remaining < MinimumSize ||
            Attribute->Length < MinimumSize ||
            (Attribute->Length & (sizeof(ULONGLONG) - 1)) != 0 ||
            Attribute->Length > Remaining)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        NameBytes = Attribute->NameLength * sizeof(WCHAR);
        if (NameBytes != 0 &&
            (Attribute->NameOffset < MinimumSize ||
             Attribute->NameOffset > Attribute->Length ||
             NameBytes >
                Attribute->Length - Attribute->NameOffset))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Attribute->IsNonResident)
        {
            if (Attribute->NonResident.DataRunsOffset < MinimumSize ||
                Attribute->NonResident.DataRunsOffset >=
                    Attribute->Length)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
        }
        else if (Attribute->Resident.DataOffset < MinimumSize ||
                 Attribute->Resident.DataOffset >
                    Attribute->Length ||
                 Attribute->Resident.DataLength >
                    Attribute->Length -
                    Attribute->Resident.DataOffset)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Attribute == TargetAttribute)
        {
            if (!!Attribute->IsNonResident !=
                !!ExpectedNonResident)
            {
                return STATUS_INVALID_PARAMETER;
            }
            Found = TRUE;
            if (DataLength && !Attribute->IsNonResident)
                *DataLength = Attribute->Resident.DataLength;
        }

        Offset += Attribute->Length;
    }

    return STATUS_FILE_CORRUPT_ERROR;
}

NTSTATUS
FileRecord::InsertResidentAttribute(
    _In_ AttributeType Type,
    _In_opt_ PWSTR Name,
    _Out_ PAttribute* NewAttribute)
{
    ULONG AttributeCount = 0;
    ULONG DataOffset;
    ULONG InsertionOffset = MAXULONG;
    ULONG NameBytes;
    ULONG NameLength;
    ULONG NewActualSize;
    ULONG NewAttributeLength;
    ULONG Offset;
    ULONG PreviousType = 0;
    SIZE_T NameCharacters;
    USHORT AttributeId;
    BOOLEAN EndMarkerFound = FALSE;
    NTSTATUS Status;

    if (!NewAttribute)
        return STATUS_INVALID_PARAMETER;
    *NewAttribute = NULL;

    if (!Data || !Header || !DiskVolume ||
        Type == TypeAttributeEndMarker)
    {
        return STATUS_INVALID_PARAMETER;
    }

    NameCharacters = Name ? NtfsWcsLen(Name) : 0;
    if (NameCharacters > 0xff)
        return STATUS_NAME_TOO_LONG;
    NameLength = (ULONG)NameCharacters;
    NameBytes = NameLength * sizeof(WCHAR);
    if (NameBytes > MAXULONG - 0x18)
        return STATUS_FILE_TOO_LARGE;
    DataOffset = ALIGN_UP_BY(0x18 + NameBytes,
                             sizeof(ULONGLONG));
    NewAttributeLength = DataOffset;

    if (RecordBufferSize < sizeof(FileRecordHeader) ||
        Header != reinterpret_cast<PFileRecordHeader>(Data) ||
        Header->AllocatedSize > RecordBufferSize ||
        Header->AllocatedSize < sizeof(FileRecordHeader) ||
        Header->ActualSize > Header->AllocatedSize ||
        Header->ActualSize < sizeof(FileRecordHeader) ||
        Header->AttributeOffset < sizeof(FileRecordHeader) ||
        Header->AttributeOffset >= Header->ActualSize ||
        (Header->AttributeOffset &
         (sizeof(ULONGLONG) - 1)) != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Offset = Header->AttributeOffset;
    while (Offset < Header->ActualSize)
    {
        PAttribute Attribute;
        ULONG MinimumSize;
        ULONG Remaining = Header->ActualSize - Offset;

        if (Remaining < sizeof(ULONG))
            return STATUS_FILE_CORRUPT_ERROR;
        Attribute =
            reinterpret_cast<PAttribute>(Data + Offset);
        if (Attribute->AttributeType ==
            TypeAttributeEndMarker)
        {
            EndMarkerFound = TRUE;
            if (InsertionOffset == MAXULONG)
                InsertionOffset = Offset;
            break;
        }

        if (Attribute->IsNonResident > 1 ||
            Attribute->AttributeType < PreviousType)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        PreviousType = Attribute->AttributeType;
        MinimumSize = Attribute->IsNonResident
            ? ((Attribute->Flags &
                (ATTR_COMPRESSION_MASK | ATTR_SPARSE))
                ? 0x48
                : 0x40)
            : 0x18;
        if (Remaining < MinimumSize ||
            Attribute->Length < MinimumSize ||
            (Attribute->Length &
             (sizeof(ULONGLONG) - 1)) != 0 ||
            Attribute->Length > Remaining)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        ULONG ExistingNameBytes =
            Attribute->NameLength * sizeof(WCHAR);
        if (ExistingNameBytes != 0 &&
            (Attribute->NameOffset < MinimumSize ||
             Attribute->NameOffset > Attribute->Length ||
             ExistingNameBytes >
                Attribute->Length - Attribute->NameOffset))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        if (Attribute->IsNonResident)
        {
            if (Attribute->NonResident.DataRunsOffset <
                    MinimumSize ||
                Attribute->NonResident.DataRunsOffset >=
                    Attribute->Length)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
        }
        else if (Attribute->Resident.DataOffset <
                     MinimumSize ||
                 Attribute->Resident.DataOffset >
                     Attribute->Length ||
                 Attribute->Resident.DataLength >
                     Attribute->Length -
                         Attribute->Resident.DataOffset)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Attribute->AttributeType == (ULONG)Type)
        {
            if (NameLength == 0)
            {
                if (Attribute->NameLength == 0 &&
                    !NtfsUnnamedAttributeMayRepeat(Type))
                {
                    return STATUS_INVALID_PARAMETER;
                }
                if (InsertionOffset == MAXULONG)
                    InsertionOffset = Offset;
            }
            else if (Attribute->NameLength != 0)
            {
                LONG Comparison;

                Status = CompareAttributeNames(
                    DiskVolume,
                    Name,
                    NameLength,
                    reinterpret_cast<PWSTR>(
                        reinterpret_cast<PUCHAR>(
                            Attribute) +
                        Attribute->NameOffset),
                    Attribute->NameLength,
                    &Comparison);
                if (!NT_SUCCESS(Status))
                    return Status;
                if (Comparison == 0)
                    return STATUS_INVALID_PARAMETER;
                if (Comparison < 0 &&
                    InsertionOffset == MAXULONG)
                {
                    InsertionOffset = Offset;
                }
            }
        }
        else if (Attribute->AttributeType > (ULONG)Type &&
                 InsertionOffset == MAXULONG)
        {
            InsertionOffset = Offset;
        }

        AttributeCount++;
        Offset += Attribute->Length;
    }

    if (!EndMarkerFound ||
        InsertionOffset == MAXULONG ||
        NewAttributeLength >
            MAXULONG - Header->ActualSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    NewActualSize =
        Header->ActualSize + NewAttributeLength;
    if (NewActualSize > Header->AllocatedSize)
        return STATUS_BUFFER_TOO_SMALL;

    /*
     * NextAttributeID is normally free. If an old writer left it pointing at
     * a live instance, advance at most once per existing attribute; a free
     * 16-bit ID must exist in a record this small.
     */
    AttributeId = Header->NextAttributeID;
    for (ULONG Attempt = 0;
         Attempt <= AttributeCount;
         Attempt++)
    {
        BOOLEAN InUse = FALSE;
        ULONG Scan = Header->AttributeOffset;

        while (Scan < Header->ActualSize)
        {
            PAttribute Attribute =
                reinterpret_cast<PAttribute>(Data + Scan);

            if (Attribute->AttributeType ==
                TypeAttributeEndMarker)
            {
                break;
            }
            if (Attribute->AttributeID == AttributeId)
            {
                InUse = TRUE;
                break;
            }
            Scan += Attribute->Length;
        }
        if (!InUse)
            break;
        AttributeId++;
        if (Attempt == AttributeCount)
            return STATUS_FILE_CORRUPT_ERROR;
    }

    ClearDataRunCache();
    RtlMoveMemory(Data + InsertionOffset +
                      NewAttributeLength,
                  Data + InsertionOffset,
                  Header->ActualSize - InsertionOffset);
    RtlZeroMemory(Data + InsertionOffset,
                  NewAttributeLength);

    PAttribute Attribute =
        reinterpret_cast<PAttribute>(
            Data + InsertionOffset);
    Attribute->AttributeType = Type;
    Attribute->Length = NewAttributeLength;
    Attribute->IsNonResident = FALSE;
    Attribute->NameLength = (UCHAR)NameLength;
    Attribute->NameOffset =
        NameLength != 0 ? 0x18 : 0;
    Attribute->Flags = 0;
    Attribute->AttributeID = AttributeId;
    Attribute->Resident.DataLength = 0;
    Attribute->Resident.DataOffset =
        (USHORT)DataOffset;
    Attribute->Resident.IndexedFlag = 0;
    Attribute->Resident.Padding = 0;
    if (NameLength != 0)
    {
        RtlCopyMemory(
            reinterpret_cast<PUCHAR>(Attribute) +
                Attribute->NameOffset,
            Name,
            NameBytes);
    }

    Header->ActualSize = NewActualSize;
    Header->NextAttributeID = AttributeId + 1;
    *NewAttribute = Attribute;
    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::InsertAttributeListEntry(
    _In_ PAttribute TargetAttribute,
    _In_ PFileRecord AttributeOwner,
    _In_ UINT32 TimestampFields)
{
    PAttribute ListAttribute;
    PUCHAR BaseRecordBackup = NULL;
    PUCHAR NewList = NULL;
    PUCHAR OldList = NULL;
    PWSTR TargetName = NULL;
    ULONGLONG BaseFileReference;
    ULONGLONG OwnerFileReference;
    ULONGLONG TargetFirstVcn;
    ULONG InsertionOffset;
    ULONG NameBytes;
    ULONG NameLength;
    ULONG NewEntryLength;
    ULONG NewListLength;
    ULONG Offset;
    ULONG PreviousOffset = MAXULONG;
    ULONG WrittenLength;
    BOOLEAN ListWriteAttempted = FALSE;
    NTSTATUS Status;

    if (!TargetAttribute || !AttributeOwner ||
        !AttributeOwner->Header ||
        !AttributeOwner->Data ||
        AttributeOwner == this ||
        TimestampFields &
            ~NTFS_AUTOMATIC_TIMESTAMP_FIELDS)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (GetFRNFromFileRef(
            AttributeOwner->
                Header->BaseFileRecord) !=
            Header->MFTRecordNumber ||
        AttributeOwner->Header->SequenceNumber == 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = AttributeOwner->
        ValidateAttributeForUpdate(
            TargetAttribute,
            !!TargetAttribute->IsNonResident,
            NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    NameLength = TargetAttribute->NameLength;
    NameBytes = NameLength * sizeof(WCHAR);
    if (NameLength != 0)
    {
        ULONG MinimumSize =
            TargetAttribute->IsNonResident
            ? ((TargetAttribute->Flags &
                (ATTR_COMPRESSION_MASK |
                 ATTR_SPARSE))
                ? 0x48
                : 0x40)
            : 0x18;

        if (TargetAttribute->NameOffset <
                MinimumSize ||
            TargetAttribute->NameOffset >
                TargetAttribute->Length ||
            NameBytes >
                TargetAttribute->Length -
                    TargetAttribute->NameOffset)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        TargetName =
            reinterpret_cast<PWSTR>(
                reinterpret_cast<PUCHAR>(
                    TargetAttribute) +
                TargetAttribute->NameOffset);
    }
    TargetFirstVcn =
        TargetAttribute->IsNonResident
        ? TargetAttribute->
            NonResident.FirstVCN
        : 0;

    Status = LoadAttributeList();
    if (!NT_SUCCESS(Status) ||
        !AttributeListData ||
        AttributeListLength == 0)
    {
        return NT_SUCCESS(Status)
            ? STATUS_FILE_CORRUPT_ERROR
            : Status;
    }
    ListAttribute = FindAttributeInRecord(
        TypeAttributeList,
        NULL,
        NULL);
    if (!ListAttribute)
        return STATUS_FILE_CORRUPT_ERROR;

    if (NameBytes > MAXULONG - 0x1a ||
        ALIGN_UP_BY(0x1a + NameBytes,
                    sizeof(ULONGLONG)) >
            MAXUSHORT)
    {
        return STATUS_FILE_TOO_LARGE;
    }
    NewEntryLength = ALIGN_UP_BY(
        0x1a + NameBytes,
        sizeof(ULONGLONG));
    if (AttributeListLength >
        MAXULONG - NewEntryLength)
    {
        return STATUS_FILE_TOO_LARGE;
    }
    NewListLength =
        AttributeListLength +
        NewEntryLength;

    InsertionOffset = AttributeListLength;
    Offset = 0;
    while (Offset < AttributeListLength)
    {
        PAttributeListEx Entry =
            reinterpret_cast<PAttributeListEx>(
                AttributeListData + Offset);
        ULONG Remaining =
            AttributeListLength - Offset;
        PWSTR EntryName = Entry->NameLength
            ? reinterpret_cast<PWSTR>(
                AttributeListData + Offset +
                    Entry->NameOffset)
            : NULL;
        LONG Comparison;

        if (!IsWritableAttributeListEntryValid(
                Entry,
                Remaining))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        if (PreviousOffset != MAXULONG)
        {
            PAttributeListEx Previous =
                reinterpret_cast<PAttributeListEx>(
                    AttributeListData +
                        PreviousOffset);
            PWSTR PreviousName =
                Previous->NameLength
                ? reinterpret_cast<PWSTR>(
                    AttributeListData +
                        PreviousOffset +
                        Previous->NameOffset)
                : NULL;

            Status = CompareAttributeListKeys(
                DiskVolume,
                Previous->Type,
                PreviousName,
                Previous->NameLength,
                Previous->FirstVCN,
                Entry->Type,
                EntryName,
                Entry->NameLength,
                Entry->FirstVCN,
                &Comparison);
            if (!NT_SUCCESS(Status) ||
                Comparison > 0)
            {
                return NT_SUCCESS(Status)
                    ? STATUS_FILE_CORRUPT_ERROR
                    : Status;
            }
        }

        Status = CompareAttributeListKeys(
            DiskVolume,
            TargetAttribute->AttributeType,
            TargetName,
            NameLength,
            TargetFirstVcn,
            Entry->Type,
            EntryName,
            Entry->NameLength,
            Entry->FirstVCN,
            &Comparison);
        if (!NT_SUCCESS(Status))
            return Status;
        if (Comparison == 0 &&
            TargetAttribute->AttributeType ==
                TypeData)
        {
            return STATUS_INVALID_PARAMETER;
        }
        if (Comparison < 0 &&
            InsertionOffset ==
                AttributeListLength)
        {
            InsertionOffset = Offset;
        }

        PreviousOffset = Offset;
        Offset += Entry->RecordLength;
    }
    if (Offset != AttributeListLength)
        return STATUS_FILE_CORRUPT_ERROR;

    OldList =
        new(PagedPool, TAG_NTFS)
            UCHAR[AttributeListLength];
    NewList =
        new(PagedPool, TAG_NTFS)
            UCHAR[NewListLength];
    BaseRecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!OldList || !NewList ||
        !BaseRecordBackup)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(OldList,
                  AttributeListData,
                  AttributeListLength);
    RtlCopyMemory(BaseRecordBackup,
                  Data,
                  RecordBufferSize);
    RtlCopyMemory(NewList,
                  OldList,
                  InsertionOffset);
    RtlZeroMemory(NewList + InsertionOffset,
                  NewEntryLength);
    RtlCopyMemory(
        NewList + InsertionOffset +
            NewEntryLength,
        OldList + InsertionOffset,
        AttributeListLength -
            InsertionOffset);

    {
        PAttributeListEx NewEntry =
            reinterpret_cast<PAttributeListEx>(
                NewList + InsertionOffset);

        NewEntry->Type =
            TargetAttribute->AttributeType;
        NewEntry->RecordLength =
            (USHORT)NewEntryLength;
        NewEntry->NameLength =
            (UCHAR)NameLength;
        NewEntry->NameOffset =
            NameLength != 0 ? 0x1a : 0;
        NewEntry->FirstVCN =
            TargetFirstVcn;
        OwnerFileReference =
            ((ULONGLONG)AttributeOwner->
                Header->SequenceNumber << 48) |
            AttributeOwner->
                Header->MFTRecordNumber;
        NewEntry->BaseFileRef =
            OwnerFileReference;
        NewEntry->AttributeId =
            TargetAttribute->AttributeID;
        if (NameBytes != 0)
        {
            RtlCopyMemory(
                NewList + InsertionOffset +
                    NewEntry->NameOffset,
                TargetName,
                NameBytes);
        }
    }

    BaseFileReference =
        ((ULONGLONG)Header->SequenceNumber << 48) |
        Header->MFTRecordNumber;
    if (AttributeOwner->
            Header->BaseFileRecord !=
        BaseFileReference)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    Status = PrepareAutomaticTimestamps(
        TimestampFields,
        NULL);
    if (!NT_SUCCESS(Status))
        goto Restore;

    {
        LARGE_INTEGER ListOffset = {};

        WrittenLength = NewListLength;
        ListWriteAttempted = TRUE;
        Status = WriteFileData(
            TypeAttributeList,
            NULL,
            NewList,
            &WrittenLength,
            &ListOffset);
    }
    if (!NT_SUCCESS(Status) ||
        WrittenLength != NewListLength)
    {
        if (NT_SUCCESS(Status))
            Status = STATUS_END_OF_FILE;
        goto Restore;
    }

    delete[] AttributeListData;
    AttributeListData = NULL;
    AttributeListLength = 0;
    Status = STATUS_SUCCESS;
    goto Done;

Restore:
    RtlCopyMemory(Data,
                  BaseRecordBackup,
                  RecordBufferSize);
    Header =
        reinterpret_cast<PFileRecordHeader>(Data);
    ClearDataRunCache();
    if (ListWriteAttempted)
    {
        LARGE_INTEGER ListOffset = {};
        ULONG RestoreLength =
            AttributeListLength;

        (void)WriteFileData(
            TypeAttributeList,
            NULL,
            OldList,
            &RestoreLength,
            &ListOffset);
        RtlCopyMemory(Data,
                      BaseRecordBackup,
                      RecordBufferSize);
        Header =
            reinterpret_cast<PFileRecordHeader>(
                Data);
        ClearDataRunCache();
    }

Done:
    delete[] BaseRecordBackup;
    delete[] NewList;
    delete[] OldList;
    return Status;
}

NTSTATUS
FileRecord::CreateInitialAttributeList()
{
    PAttribute Candidate = NULL;
    PAttribute Candidates[2] = {};
    PAttribute EaCandidate = NULL;
    PAttribute EaInformationCandidate = NULL;
    PAttribute FallbackCandidate = NULL;
    PAttribute ListAttribute = NULL;
    PAttribute MovedAttributes[2] = {};
    PFileRecord Extension = NULL;
    PINITIAL_ATTRIBUTE_LIST_ENTRY Entries = NULL;
    PUCHAR BaseRecordBackup = NULL;
    PUCHAR CandidateData[2] = {};
    PUCHAR ListData = NULL;
    PWSTR CandidateNames[2] = {};
    ULONGLONG BaseFileReference;
    ULONG AttributeCount = 0;
    ULONG CandidateCount = 0;
    ULONG CandidateDataLengths[2] = {};
    ULONG CandidateNameLengths[2] = {};
    ULONG CandidateRecordLength = 0;
    ULONG EntryCount;
    ULONG EntryIndex;
    ULONG ListLength = 0;
    ULONG MovedCount = 0;
    ULONG Offset;
    ULONG WrittenLength;
    BOOLEAN BaseWriteAttempted = FALSE;
    BOOLEAN BaseRestored = TRUE;
    BOOLEAN EndMarkerFound = FALSE;
    BOOLEAN Published = FALSE;
    NTSTATUS CleanupStatus;
    NTSTATUS Status;

    if (!DiskVolume || DiskVolume->IsReadOnly ||
        !Data || !Header ||
        Header->BaseFileRecord != 0 ||
        Header->SequenceNumber == 0 ||
        FindAttributeInRecord(
            TypeAttributeList,
            NULL,
            NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    BaseRecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!BaseRecordBackup)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(BaseRecordBackup,
                  Data,
                  RecordBufferSize);

    /*
     * Moving one existing resident named stream is sufficient to replace its
     * in-record bytes with the much smaller nonresident $ATTRIBUTE_LIST
     * header. Prefer the largest stream so the list can promote without a
     * second structural move. The native EA attributes form one inseparable
     * pair, but their mutation path is extension-aware and can now relocate
     * both together. A legacy per-file security descriptor is also safe to
     * relocate because it is read-only in the current mutation API.
     */
    Offset = Header->AttributeOffset;
    while (Offset < Header->ActualSize)
    {
        PAttribute Attribute;
        ULONG Remaining =
            Header->ActualSize - Offset;

        if (Remaining < sizeof(ULONG))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        Attribute =
            reinterpret_cast<PAttribute>(
                Data + Offset);
        if (Attribute->AttributeType ==
            TypeAttributeEndMarker)
        {
            EndMarkerFound = TRUE;
            break;
        }
        Status = ValidateAttributeForUpdate(
            Attribute,
            !!Attribute->IsNonResident,
            NULL);
        if (!NT_SUCCESS(Status))
            goto Done;

        if (!Attribute->IsNonResident &&
            Attribute->AttributeType == TypeData &&
            Attribute->NameLength != 0 &&
            Attribute->Flags == 0 &&
            Attribute->Length >
                CandidateRecordLength)
        {
            Candidate = Attribute;
            CandidateRecordLength =
                Attribute->Length;
        }
        else if (!Attribute->IsNonResident &&
                 Attribute->AttributeType ==
                    TypeSecurityDescriptor &&
                 Attribute->NameLength == 0 &&
                 Attribute->Flags == 0 &&
                 (!FallbackCandidate ||
                  Attribute->Length >
                      FallbackCandidate->Length))
        {
            FallbackCandidate = Attribute;
        }
        else if (!Attribute->IsNonResident &&
                 Attribute->AttributeType ==
                    TypeEAInformation &&
                 Attribute->NameLength == 0 &&
                 Attribute->Flags == 0)
        {
            if (EaInformationCandidate)
            {
                Status = STATUS_EA_CORRUPT_ERROR;
                goto Done;
            }
            EaInformationCandidate = Attribute;
        }
        else if (!Attribute->IsNonResident &&
                 Attribute->AttributeType ==
                    TypeEA &&
                 Attribute->NameLength == 0 &&
                 Attribute->Flags == 0)
        {
            if (EaCandidate)
            {
                Status = STATUS_EA_CORRUPT_ERROR;
                goto Done;
            }
            EaCandidate = Attribute;
        }
        Offset += Attribute->Length;
    }
    if (!EndMarkerFound)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }
    if (Candidate)
    {
        Candidates[CandidateCount++] = Candidate;
    }
    else if (EaInformationCandidate &&
             EaCandidate &&
             (!FallbackCandidate ||
              EaInformationCandidate->Length +
                  EaCandidate->Length >=
                  FallbackCandidate->Length))
    {
        Candidates[CandidateCount++] =
            EaInformationCandidate;
        Candidates[CandidateCount++] =
            EaCandidate;
    }
    else if (FallbackCandidate)
    {
        Candidates[CandidateCount++] =
            FallbackCandidate;
    }
    if (CandidateCount == 0 &&
        (Header->ActualSize >
             Header->AllocatedSize ||
         Header->AllocatedSize -
             Header->ActualSize < 0x50))
    {
        Status = STATUS_BUFFER_TOO_SMALL;
        goto Done;
    }

    if (CandidateCount != 0)
    {
        for (ULONG Index = 0;
             Index < CandidateCount;
             Index++)
        {
            CandidateNameLengths[Index] =
                Candidates[Index]->NameLength;
            CandidateDataLengths[Index] =
                Candidates[Index]->
                    Resident.DataLength;
            CandidateNames[Index] =
                new(PagedPool, TAG_NTFS)
                    WCHAR[
                        CandidateNameLengths[Index] +
                        1];
            if (!CandidateNames[Index])
            {
                Status =
                    STATUS_INSUFFICIENT_RESOURCES;
                goto Done;
            }
            RtlCopyMemory(
                CandidateNames[Index],
                reinterpret_cast<PUCHAR>(
                    Candidates[Index]) +
                    Candidates[Index]->NameOffset,
                CandidateNameLengths[Index] *
                    sizeof(WCHAR));
            CandidateNames[Index][
                CandidateNameLengths[Index]] = 0;

            if (CandidateDataLengths[Index] == 0)
                continue;
            CandidateData[Index] =
                new(PagedPool, TAG_NTFS)
                    UCHAR[
                        CandidateDataLengths[
                            Index]];
            if (!CandidateData[Index])
            {
                Status =
                    STATUS_INSUFFICIENT_RESOURCES;
                goto Done;
            }
            RtlCopyMemory(
                CandidateData[Index],
                reinterpret_cast<PUCHAR>(
                    Candidates[Index]) +
                    Candidates[Index]->
                        Resident.DataOffset,
                CandidateDataLengths[Index]);
        }

        BaseFileReference =
            ((ULONGLONG)
                Header->SequenceNumber << 48) |
            Header->MFTRecordNumber;
        Status = DiskVolume->MFT->
            AllocateExtensionFileRecord(
                BaseFileReference,
                &Extension);
        if (!NT_SUCCESS(Status))
            goto Done;

        Status = Extension->
            SetAutomaticTimestampMask(0);
        if (!NT_SUCCESS(Status))
            goto Done;

        for (ULONG Index = 0;
             Index < CandidateCount;
             Index++)
        {
            Status = Extension->
                InsertResidentAttribute(
                    (AttributeType)
                        Candidates[Index]->
                            AttributeType,
                    CandidateNames[Index],
                    &MovedAttributes[Index]);
            if (!NT_SUCCESS(Status))
                goto Done;
            Status = Extension->
                ReplaceResidentData(
                    MovedAttributes[Index],
                    CandidateData[Index],
                    CandidateDataLengths[Index]);
            if (!NT_SUCCESS(Status))
                goto Done;
            MovedCount++;
        }
        Status = DiskVolume->MFT->
            WriteFileRecordToMFT(Extension);
        if (!NT_SUCCESS(Status))
            goto Done;

        /*
         * Remove later records first so pointers to earlier members of an EA
         * pair remain valid while the record tail shifts.
         */
        for (ULONG Index = CandidateCount;
             Index != 0;
             Index--)
        {
            Status = RemoveAttributeRecord(
                Candidates[Index - 1]);
            if (!NT_SUCCESS(Status))
                goto RestoreBase;
        }
    }
    Status = InsertResidentAttribute(
        TypeAttributeList,
        NULL,
        &ListAttribute);
    if (!NT_SUCCESS(Status))
        goto RestoreBase;
    if (!ListAttribute)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto RestoreBase;
    }

    Offset = Header->AttributeOffset;
    EndMarkerFound = FALSE;
    while (Offset < Header->ActualSize)
    {
        PAttribute Attribute;
        ULONG Remaining =
            Header->ActualSize - Offset;

        if (Remaining < sizeof(ULONG))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto RestoreBase;
        }
        Attribute =
            reinterpret_cast<PAttribute>(
                Data + Offset);
        if (Attribute->AttributeType ==
            TypeAttributeEndMarker)
        {
            EndMarkerFound = TRUE;
            break;
        }
        Status = ValidateAttributeForUpdate(
            Attribute,
            !!Attribute->IsNonResident,
            NULL);
        if (!NT_SUCCESS(Status))
            goto RestoreBase;
        if (AttributeCount == MAXULONG)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto RestoreBase;
        }
        AttributeCount++;
        Offset += Attribute->Length;
    }
    if (!EndMarkerFound ||
        AttributeCount == MAXULONG)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto RestoreBase;
    }

    EntryCount =
        AttributeCount +
        MovedCount;
    Entries =
        new(PagedPool, TAG_NTFS)
            INITIAL_ATTRIBUTE_LIST_ENTRY[
                EntryCount]();
    if (!Entries)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto RestoreBase;
    }

    EntryIndex = 0;
    Offset = Header->AttributeOffset;
    while (Offset < Header->ActualSize)
    {
        PAttribute Attribute =
            reinterpret_cast<PAttribute>(
                Data + Offset);
        PINITIAL_ATTRIBUTE_LIST_ENTRY Entry;
        ULONG NameBytes;

        if (Attribute->AttributeType ==
            TypeAttributeEndMarker)
        {
            break;
        }
        Entry = &Entries[EntryIndex++];
        Entry->Attribute = Attribute;
        Entry->Owner = this;
        Entry->NameLength =
            Attribute->NameLength;
        Entry->Name = Entry->NameLength
            ? reinterpret_cast<PWSTR>(
                reinterpret_cast<PUCHAR>(
                    Attribute) +
                Attribute->NameOffset)
            : NULL;
        Entry->FirstVcn =
            Attribute->IsNonResident
            ? Attribute->NonResident.FirstVCN
            : 0;
        NameBytes =
            Entry->NameLength * sizeof(WCHAR);
        if (NameBytes > MAXULONG - 0x1a)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto RestoreBase;
        }
        Entry->RecordLength = ALIGN_UP_BY(
            0x1a + NameBytes,
            sizeof(ULONGLONG));
        if (Entry->RecordLength > MAXUSHORT ||
            ListLength >
                MAXULONG - Entry->RecordLength)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto RestoreBase;
        }
        ListLength += Entry->RecordLength;
        Offset += Attribute->Length;
    }

    for (ULONG Index = 0;
         Index < MovedCount;
         Index++)
    {
        PINITIAL_ATTRIBUTE_LIST_ENTRY Entry =
            &Entries[EntryIndex++];
        ULONG NameBytes =
            MovedAttributes[Index]->NameLength *
            sizeof(WCHAR);

        Entry->Attribute =
            MovedAttributes[Index];
        Entry->Owner = Extension;
        Entry->NameLength =
            MovedAttributes[Index]->NameLength;
        Entry->Name =
            reinterpret_cast<PWSTR>(
                reinterpret_cast<PUCHAR>(
                    MovedAttributes[Index]) +
                MovedAttributes[Index]->NameOffset);
        Entry->FirstVcn =
            MovedAttributes[Index]->
                IsNonResident
            ? MovedAttributes[Index]->
                NonResident.FirstVCN
            : 0;
        if (NameBytes > MAXULONG - 0x1a)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto RestoreBase;
        }
        Entry->RecordLength = ALIGN_UP_BY(
            0x1a + NameBytes,
            sizeof(ULONGLONG));
        if (Entry->RecordLength > MAXUSHORT ||
            ListLength >
                MAXULONG - Entry->RecordLength)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto RestoreBase;
        }
        ListLength += Entry->RecordLength;
    }
    if (EntryIndex != EntryCount ||
        ListLength == 0)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto RestoreBase;
    }

    for (ULONG Index = 1;
         Index < EntryCount;
         Index++)
    {
        INITIAL_ATTRIBUTE_LIST_ENTRY Current =
            Entries[Index];
        ULONG Position = Index;

        while (Position != 0)
        {
            LONG Comparison;

            Status = CompareAttributeListKeys(
                DiskVolume,
                Entries[Position - 1].
                    Attribute->AttributeType,
                Entries[Position - 1].Name,
                Entries[Position - 1].NameLength,
                Entries[Position - 1].FirstVcn,
                Current.Attribute->AttributeType,
                Current.Name,
                Current.NameLength,
                Current.FirstVcn,
                &Comparison);
            if (!NT_SUCCESS(Status))
                goto RestoreBase;
            if (Comparison <= 0)
                break;
            Entries[Position] =
                Entries[Position - 1];
            Position--;
        }
        Entries[Position] = Current;
    }

    ListData =
        new(PagedPool, TAG_NTFS)
            UCHAR[ListLength]();
    if (!ListData)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto RestoreBase;
    }

    Offset = 0;
    for (ULONG Index = 0;
         Index < EntryCount;
         Index++)
    {
        PINITIAL_ATTRIBUTE_LIST_ENTRY Source =
            &Entries[Index];
        PAttributeListEx Target =
            reinterpret_cast<PAttributeListEx>(
                ListData + Offset);
        ULONGLONG OwnerReference =
            ((ULONGLONG)Source->Owner->
                Header->SequenceNumber << 48) |
            Source->Owner->
                Header->MFTRecordNumber;

        Target->Type =
            Source->Attribute->AttributeType;
        Target->RecordLength =
            (USHORT)Source->RecordLength;
        Target->NameLength =
            (UCHAR)Source->NameLength;
        Target->NameOffset =
            Source->NameLength != 0
            ? 0x1a
            : 0;
        Target->FirstVCN =
            Source->FirstVcn;
        Target->BaseFileRef =
            OwnerReference;
        Target->AttributeId =
            Source->Attribute->AttributeID;
        if (Source->NameLength != 0)
        {
            RtlCopyMemory(
                ListData + Offset +
                    Target->NameOffset,
                Source->Name,
                Source->NameLength *
                    sizeof(WCHAR));
        }
        Offset += Source->RecordLength;
    }
    if (Offset != ListLength)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto RestoreBase;
    }

    {
        LARGE_INTEGER ListOffset = {};

        WrittenLength = ListLength;
        BaseWriteAttempted = TRUE;
        Status = WriteFileData(
            TypeAttributeList,
            NULL,
            ListData,
            &WrittenLength,
            &ListOffset);
    }
    if (!NT_SUCCESS(Status) ||
        WrittenLength != ListLength)
    {
        if (NT_SUCCESS(Status))
            Status = STATUS_END_OF_FILE;
        goto RestoreBase;
    }

    delete[] AttributeListData;
    AttributeListData = NULL;
    AttributeListLength = 0;
    Published = TRUE;
    Status = STATUS_SUCCESS;
    goto Done;

RestoreBase:
    RtlCopyMemory(Data,
                  BaseRecordBackup,
                  RecordBufferSize);
    Header =
        reinterpret_cast<PFileRecordHeader>(
            Data);
    ClearDataRunCache();
    ClearExtentCache();
    delete[] AttributeListData;
    AttributeListData = NULL;
    AttributeListLength = 0;
    if (BaseWriteAttempted)
    {
        CleanupStatus = DiskVolume->MFT->
            WriteFileRecordToMFT(this);
        if (!NT_SUCCESS(CleanupStatus))
        {
            BaseRestored = FALSE;
            Status = CleanupStatus;
        }
    }

Done:
    if (Extension &&
        !Published &&
        BaseRestored)
    {
        CleanupStatus = DiskVolume->MFT->
            DeallocateExtensionFileRecord(
                Extension);
        if (NT_SUCCESS(Status) &&
            !NT_SUCCESS(CleanupStatus))
        {
            Status = CleanupStatus;
        }
    }
    delete Extension;
    delete[] Entries;
    delete[] ListData;
    for (ULONG Index = 0;
         Index < RTL_NUMBER_OF(CandidateData);
         Index++)
    {
        delete[] CandidateData[Index];
        delete[] CandidateNames[Index];
    }
    delete[] BaseRecordBackup;
    return Status;
}

NTSTATUS
FileRecord::ValidateResidentAttributeForUpdate(
    _In_ PAttribute TargetAttribute,
    _Out_opt_ PULONG DataLength)
{
    return ValidateAttributeForUpdate(TargetAttribute,
                                      FALSE,
                                      DataLength);
}

NTSTATUS
FileRecord::ResizeAttributeRecord(
    _In_ PAttribute TargetAttribute,
    _In_ ULONG NewAttributeLength)
{
    PUCHAR OldTail;
    PUCHAR NewTail;
    ULONG OldActualSize;
    ULONG OldAttributeLength;
    ULONG NewActualSize;
    ULONG TailLength;
    ULONG MinimumLength;
    NTSTATUS Status;

    if (!TargetAttribute)
        return STATUS_INVALID_PARAMETER;

    Status = ValidateAttributeForUpdate(
        TargetAttribute,
        !!TargetAttribute->IsNonResident,
        NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    MinimumLength = TargetAttribute->IsNonResident
        ? ((TargetAttribute->Flags &
            (ATTR_COMPRESSION_MASK | ATTR_SPARSE))
            ? 0x48
            : 0x40)
        : 0x18;
    if (NewAttributeLength < MinimumLength ||
        (NewAttributeLength &
         (sizeof(ULONGLONG) - 1)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    OldAttributeLength = TargetAttribute->Length;
    OldActualSize = Header->ActualSize;
    if (NewAttributeLength >
        MAXULONG - (OldActualSize - OldAttributeLength))
    {
        return STATUS_FILE_TOO_LARGE;
    }
    NewActualSize =
        OldActualSize - OldAttributeLength +
        NewAttributeLength;
    if (NewActualSize > Header->AllocatedSize)
        return STATUS_BUFFER_TOO_SMALL;

    OldTail = reinterpret_cast<PUCHAR>(TargetAttribute) +
              OldAttributeLength;
    NewTail = reinterpret_cast<PUCHAR>(TargetAttribute) +
              NewAttributeLength;
    TailLength = (ULONG)((Data + OldActualSize) - OldTail);
    if (NewTail != OldTail)
        RtlMoveMemory(NewTail, OldTail, TailLength);

    if (NewAttributeLength > OldAttributeLength)
    {
        RtlZeroMemory(OldTail,
                      NewAttributeLength -
                          OldAttributeLength);
    }
    else if (NewActualSize < OldActualSize)
    {
        RtlZeroMemory(Data + NewActualSize,
                      OldActualSize - NewActualSize);
    }

    TargetAttribute->Length = NewAttributeLength;
    Header->ActualSize = NewActualSize;
    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::RemoveAttributeRecord(
    _In_ PAttribute TargetAttribute)
{
    PUCHAR AttributeStart;
    PUCHAR Tail;
    ULONG AttributeLength;
    ULONG OldActualSize;
    ULONG TailLength;
    NTSTATUS Status;

    if (!TargetAttribute)
        return STATUS_INVALID_PARAMETER;

    Status = ValidateAttributeForUpdate(
        TargetAttribute,
        !!TargetAttribute->IsNonResident,
        NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if (TargetAttribute->AttributeType ==
        TypeAttributeEndMarker)
    {
        return STATUS_INVALID_PARAMETER;
    }

    AttributeStart =
        reinterpret_cast<PUCHAR>(TargetAttribute);
    AttributeLength = TargetAttribute->Length;
    OldActualSize = Header->ActualSize;
    Tail = AttributeStart + AttributeLength;
    TailLength = (ULONG)((Data + OldActualSize) - Tail);

    ClearDataRunCache();
    RtlMoveMemory(AttributeStart,
                  Tail,
                  TailLength);
    Header->ActualSize =
        OldActualSize - AttributeLength;
    RtlZeroMemory(Data + Header->ActualSize,
                  AttributeLength);
    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::BuildNonResidentMappingSegment(
    _In_ PAttribute TargetAttribute,
    _In_ PDataRun Runs,
    _In_ ULONGLONG FirstVcn,
    _In_ USHORT Flags,
    _In_ USHORT CompressionUnitSize,
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize,
    _In_ ULONGLONG InitializedSize,
    _In_ ULONGLONG CompressedDataSize)
{
    ULONGLONG SegmentClusters;
    ULONG AttributeTypeValue;
    PUCHAR NameCopy = NULL;
    UCHAR NameLength;
    USHORT AttributeId;
    ULONG DataRunsOffset;
    ULONG MappingBytes;
    ULONG NewAttributeLength;
    ULONG MappingCapacity;
    ULONG NameBytes;
    NTSTATUS Status;

    Status = ValidateAttributeForUpdate(TargetAttribute,
                                        !!TargetAttribute->
                                            IsNonResident,
                                        NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if (!Runs ||
        (Flags != 0 &&
         Flags != ATTR_SPARSE &&
         Flags != ATTR_COMPRESSED) ||
        (Flags != 0 &&
         CompressionUnitSize != 4) ||
        (Flags == 0 &&
         CompressionUnitSize != 0) ||
        InitializedSize > DataSize ||
        DataSize > AllocatedSize ||
        CompressedDataSize > AllocatedSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = GetMappingPairsSize(Runs,
                                 &MappingBytes,
                                 &SegmentClusters);
    if (!NT_SUCCESS(Status))
        return Status;
    if (SegmentClusters == 0 ||
        FirstVcn >
            ~(ULONGLONG)0 - (SegmentClusters - 1))
    {
        return STATUS_FILE_TOO_LARGE;
    }

    AttributeTypeValue =
        TargetAttribute->AttributeType;
    AttributeId = TargetAttribute->AttributeID;
    NameLength = TargetAttribute->NameLength;
    NameBytes =
        NameLength * sizeof(WCHAR);
    if (NameBytes != 0)
    {
        NameCopy =
            new(PagedPool, TAG_NTFS) UCHAR[NameBytes];
        if (!NameCopy)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlCopyMemory(
            NameCopy,
            reinterpret_cast<PUCHAR>(TargetAttribute) +
                TargetAttribute->NameOffset,
            NameBytes);
    }

    DataRunsOffset = ALIGN_UP_BY(
        (Flags != 0 ? 0x48 : 0x40) +
            NameBytes,
        sizeof(ULONGLONG));
    if (DataRunsOffset > MAXUSHORT ||
        DataRunsOffset > MAXULONG - MappingBytes ||
        DataRunsOffset + MappingBytes >
            MAXULONG - (sizeof(ULONGLONG) - 1))
    {
        Status = STATUS_FILE_TOO_LARGE;
        goto Done;
    }
    NewAttributeLength = ALIGN_UP_BY(
        DataRunsOffset + MappingBytes,
        sizeof(ULONGLONG));
    Status = ResizeAttributeRecord(TargetAttribute,
                                   NewAttributeLength);
    if (!NT_SUCCESS(Status))
        goto Done;

    /*
     * Preserve only the common type/length fields while rebuilding the
     * complete variant. This works for both an existing nonresident first
     * segment and the empty resident placeholder inserted into a new extent
     * record.
     */
    RtlZeroMemory(
        reinterpret_cast<PUCHAR>(TargetAttribute) + 0x08,
        NewAttributeLength - 0x08);
    TargetAttribute->AttributeType =
        AttributeTypeValue;
    TargetAttribute->Length =
        NewAttributeLength;
    TargetAttribute->IsNonResident = TRUE;
    TargetAttribute->NameLength = NameLength;
    TargetAttribute->NameOffset =
        NameBytes != 0
        ? (Flags != 0 ? 0x48 : 0x40)
        : 0;
    TargetAttribute->Flags = Flags;
    TargetAttribute->AttributeID = AttributeId;
    if (NameBytes != 0)
    {
        RtlCopyMemory(
            reinterpret_cast<PUCHAR>(TargetAttribute) +
                TargetAttribute->NameOffset,
            NameCopy,
            NameBytes);
    }

    MappingCapacity =
        NewAttributeLength - DataRunsOffset;
    Status = EncodeMappingPairs(
        Runs,
        reinterpret_cast<PUCHAR>(TargetAttribute) +
            DataRunsOffset,
        MappingCapacity,
        NULL);
    if (!NT_SUCCESS(Status))
        goto Done;

    TargetAttribute->NonResident.FirstVCN =
        FirstVcn;
    TargetAttribute->NonResident.LastVCN =
        FirstVcn + SegmentClusters - 1;
    TargetAttribute->NonResident.DataRunsOffset =
        (USHORT)DataRunsOffset;
    TargetAttribute->NonResident.CompressionUnitSize =
        CompressionUnitSize;
    TargetAttribute->NonResident.Reserved = 0;
    TargetAttribute->NonResident.AllocatedSize =
        AllocatedSize;
    TargetAttribute->NonResident.DataSize = DataSize;
    TargetAttribute->NonResident.InitalizedDataSize =
        InitializedSize;
    if (Flags != 0)
    {
        TargetAttribute->NonResident.CompressedDataSize =
            CompressedDataSize;
    }
    ClearDataRunCache();
    Status = STATUS_SUCCESS;

Done:
    delete[] NameCopy;
    return Status;
}

static BOOLEAN
MappingListNameMatches(
    _In_ PAttributeListEx Entry,
    _In_reads_(NameLength) PWSTR Name,
    _In_ ULONG NameLength)
{
    ULONG NameBytes;

    if (!Entry ||
        Entry->NameLength != NameLength)
    {
        return FALSE;
    }
    NameBytes = NameLength * sizeof(WCHAR);
    return NameBytes == 0 ||
        RtlCompareMemory(
            reinterpret_cast<PUCHAR>(Entry) +
                Entry->NameOffset,
            Name,
            NameBytes) == NameBytes;
}

static BOOLEAN
MappingExtensionRecordIsEmpty(
    _In_ PFileRecord Record)
{
    PAttribute Attribute;

    if (!Record || !Record->Data ||
        !Record->Header ||
        Record->Header->AttributeOffset >
            Record->Header->ActualSize - sizeof(ULONG))
    {
        return FALSE;
    }
    Attribute = reinterpret_cast<PAttribute>(
        Record->Data +
            Record->Header->AttributeOffset);
    return Attribute->AttributeType ==
        TypeAttributeEndMarker;
}

NTSTATUS
FileRecord::ReplaceNonResidentMappingPairs(
    _Inout_ PAttribute* TargetAttribute,
    _In_ PDataRun Runs,
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize,
    _In_ ULONGLONG InitializedSize,
    _Out_ PNonResidentMappingUpdate* MappingUpdate,
    _Out_opt_ PFileRecord* ResultOwner)
{
    PNonResidentMappingUpdate Update = NULL;
    PNONRESIDENT_MAPPING_NEW_EXTENT NewTail = NULL;
    PNONRESIDENT_MAPPING_OLD_EXTENT OldTail = NULL;
    PAttribute Attribute;
    PAttribute ListAttribute;
    PFileRecord AttributeOwner;
    PDataRun Cursor;
    PDataRun Segment = NULL;
    PDataRun ListRuns = NULL;
    PDataRun CombinedListRuns = NULL;
    PWSTR Name = NULL;
    ULONGLONG BaseFileReference;
    ULONGLONG ClusterSize;
    ULONGLONG FirstVcn = 0;
    ULONGLONG PhysicalClusters = 0;
    ULONGLONG SegmentClusters;
    ULONGLONG TotalClusters;
    ULONGLONG CompressedDataSize = 0;
    ULONGLONG ExistingListClusters;
    ULONGLONG RequiredListClusters;
    ULONGLONG NewListAllocatedSize;
    ULONGLONG PreferredListLcn;
    ULONG DataRunsOffset;
    ULONG FullMappingBytes;
    ULONG FullAttributeLength;
    ULONG MaximumAttributeLength;
    ULONG NameBytes;
    ULONG NameLength;
    ULONG NewEntryLength;
    ULONG NewExtentCount = 0;
    ULONG OldContinuationLength = 0;
    ULONG AddedListClusterCount;
    ULONG MaximumListRuns;
    ULONG Offset;
    ULONG OutputOffset;
    USHORT AttributeId;
    USHORT Flags;
    USHORT CompressionUnitSize;
    BOOLEAN FoundFirstEntry = FALSE;
    BOOLEAN Sparse = FALSE;
    BOOLEAN Compressed = FALSE;
    NTSTATUS Status;

    if (!TargetAttribute || !*TargetAttribute ||
        !MappingUpdate || !Runs)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *MappingUpdate = NULL;
    if (ResultOwner)
        *ResultOwner = NULL;
    Attribute = *TargetAttribute;

    /*
     * Mutation helpers for an extension-owned stream execute on that extent
     * object. Its base object owns the $ATTRIBUTE_LIST transaction, so hand
     * the operation back to the object that loaded this extent.
     */
    if (Header &&
        Header->BaseFileRecord != 0)
    {
        if (!BaseRecordOwner)
            return STATUS_NOT_IMPLEMENTED;
        return BaseRecordOwner->
            ReplaceNonResidentMappingPairs(
                TargetAttribute,
                Runs,
                AllocatedSize,
                DataSize,
                InitializedSize,
                MappingUpdate,
                ResultOwner);
    }

    AttributeOwner = GetAttributeOwner(Attribute);
    if (!AttributeOwner)
        return STATUS_FILE_CORRUPT_ERROR;
    AttributeId = Attribute->AttributeID;
    Status = AttributeOwner->
        ValidateAttributeForUpdate(
        Attribute,
        TRUE,
        NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    ClusterSize = BytesPerCluster(DiskVolume);
    /*
     * LZNT1-compressed streams keep their flag and 16-cluster unit grid
     * through a mapping replacement; their allocation must stay
     * unit-aligned. Other compression formats and combined flags remain
     * unsupported.
     */
    Compressed =
        (Attribute->Flags & ATTR_COMPRESSION_MASK) != 0;
    if ((Compressed &&
         (Attribute->Flags != ATTR_COMPRESSED ||
          Attribute->NonResident.CompressionUnitSize != 4 ||
          ClusterSize == 0 ||
          AllocatedSize % (ClusterSize << 4) != 0)) ||
        (!Compressed &&
         (Attribute->Flags & ~ATTR_SPARSE) != 0) ||
        Attribute->NonResident.FirstVCN != 0 ||
        ClusterSize == 0 ||
        AllocatedSize == 0 ||
        AllocatedSize % ClusterSize != 0 ||
        InitializedSize > DataSize ||
        DataSize > AllocatedSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (PDataRun Run = Runs;
         Run;
         Run = Run->NextRun)
    {
        if (Run->IsSparse)
        {
            Sparse = TRUE;
        }
        else
        {
            if (PhysicalClusters >
                ~(ULONGLONG)0 - Run->Length)
            {
                return STATUS_FILE_TOO_LARGE;
            }
            PhysicalClusters += Run->Length;
        }
    }
    Flags = Compressed
        ? ATTR_COMPRESSED
        : (Sparse ? ATTR_SPARSE : 0);
    CompressionUnitSize =
        Flags != 0 ? 4 : 0;
    if (PhysicalClusters >
        ~(ULONGLONG)0 / ClusterSize)
    {
        return STATUS_FILE_TOO_LARGE;
    }
    CompressedDataSize =
        PhysicalClusters * ClusterSize;

    Status = GetMappingPairsSize(
        Runs,
        &FullMappingBytes,
        &TotalClusters);
    if (!NT_SUCCESS(Status))
        return Status;
    if (TotalClusters !=
        AllocatedSize / ClusterSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    NameLength = Attribute->NameLength;
    NameBytes = NameLength * sizeof(WCHAR);
    if (NameLength != 0)
    {
        Name = reinterpret_cast<PWSTR>(
            reinterpret_cast<PUCHAR>(Attribute) +
                Attribute->NameOffset);
    }
    DataRunsOffset = ALIGN_UP_BY(
        (Flags != 0 ? 0x48 : 0x40) +
            NameBytes,
        sizeof(ULONGLONG));
    if (DataRunsOffset > MAXULONG -
            FullMappingBytes)
    {
        return STATUS_FILE_TOO_LARGE;
    }
    FullAttributeLength = ALIGN_UP_BY(
        DataRunsOffset + FullMappingBytes,
        sizeof(ULONGLONG));
    if (AttributeOwner->Header->ActualSize >
            AttributeOwner->Header->AllocatedSize ||
        Attribute->Length >
            MAXULONG -
                (AttributeOwner->Header->
                     AllocatedSize -
                 AttributeOwner->Header->
                     ActualSize))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    MaximumAttributeLength =
        (Attribute->Length +
         AttributeOwner->Header->AllocatedSize -
         AttributeOwner->Header->ActualSize) &
        ~(sizeof(ULONGLONG) - 1);

    ListAttribute = FindAttributeInRecord(
        TypeAttributeList,
        NULL,
        NULL);
    if (!ListAttribute)
    {
        if (AttributeOwner != this)
            return STATUS_FILE_CORRUPT_ERROR;
        if (FullAttributeLength >
            MaximumAttributeLength)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        Status = BuildNonResidentMappingSegment(
            Attribute,
            Runs,
            0,
            Flags,
            CompressionUnitSize,
            AllocatedSize,
            DataSize,
            InitializedSize,
            Flags != 0
                ? CompressedDataSize
                : 0);
        if (NT_SUCCESS(Status) && ResultOwner)
            *ResultOwner = AttributeOwner;
        return Status;
    }
    if (Attribute->AttributeType ==
        TypeAttributeList)
    {
        if (AttributeOwner != this)
            return STATUS_FILE_CORRUPT_ERROR;
        if (FullAttributeLength >
            MaximumAttributeLength)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }
        Status = BuildNonResidentMappingSegment(
            Attribute,
            Runs,
            0,
            Flags,
            CompressionUnitSize,
            AllocatedSize,
            DataSize,
            InitializedSize,
            Flags != 0
                ? CompressedDataSize
                : 0);
        if (NT_SUCCESS(Status) && ResultOwner)
            *ResultOwner = AttributeOwner;
        return Status;
    }
    if (!Header ||
        Header->BaseFileRecord != 0 ||
        !AttributeOwner->Header ||
        (AttributeOwner != this &&
         AttributeOwner->Header->
             BaseFileRecord !=
             (((ULONGLONG)
                   Header->SequenceNumber << 48) |
              Header->MFTRecordNumber)) ||
        !ListAttribute->IsNonResident ||
        ListAttribute->Flags != 0 ||
        ListAttribute->NonResident.FirstVCN != 0)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    Status = LoadAttributeList();
    if (!NT_SUCCESS(Status) ||
        !AttributeListData ||
        AttributeListLength == 0)
    {
        return NT_SUCCESS(Status)
            ? STATUS_FILE_CORRUPT_ERROR
            : Status;
    }

    Update = new(PagedPool, TAG_NTFS)
        NonResidentMappingUpdate();
    if (!Update)
        return STATUS_INSUFFICIENT_RESOURCES;
    Update->BaseRecord = this;
    Update->AttributeOwner = AttributeOwner;
    Update->BaseRecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (AttributeOwner != this)
    {
        Update->OwnerRecordBackup =
            new(PagedPool, TAG_FILE_RECORD)
                UCHAR[AttributeOwner->
                    RecordBufferSize];
    }
    Update->OldList =
        new(PagedPool, TAG_NTFS)
            UCHAR[AttributeListLength];
    if (NameLength != 0)
    {
        Update->Name =
            new(PagedPool, TAG_NTFS)
                WCHAR[NameLength + 1];
    }
    if (!Update->BaseRecordBackup ||
        (AttributeOwner != this &&
         !Update->OwnerRecordBackup) ||
        !Update->OldList ||
        (NameLength != 0 && !Update->Name))
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }
    RtlCopyMemory(Update->BaseRecordBackup,
                  Data,
                  RecordBufferSize);
    if (Update->OwnerRecordBackup)
    {
        RtlCopyMemory(
            Update->OwnerRecordBackup,
            AttributeOwner->Data,
            AttributeOwner->RecordBufferSize);
    }
    RtlCopyMemory(Update->OldList,
                  AttributeListData,
                  AttributeListLength);
    if (NameLength != 0)
    {
        RtlCopyMemory(Update->Name,
                      Name,
                      NameBytes);
        Update->Name[NameLength] = 0;
    }
    Update->OldListLength = AttributeListLength;
    Update->NameLength = NameLength;
    Update->Type =
        (AttributeType)Attribute->
            AttributeType;

    BaseFileReference =
        ((ULONGLONG)Header->SequenceNumber << 48) |
        Header->MFTRecordNumber;
    {
        ULONGLONG OwnerFileReference =
            ((ULONGLONG)AttributeOwner->
                Header->SequenceNumber << 48) |
            AttributeOwner->Header->
                MFTRecordNumber;

    Offset = 0;
    while (Offset < AttributeListLength)
    {
        PAttributeListEx Entry =
            reinterpret_cast<PAttributeListEx>(
                AttributeListData + Offset);
        ULONG Remaining =
            AttributeListLength - Offset;

        if (!IsWritableAttributeListEntryValid(
                Entry,
                Remaining))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Failure;
        }
        if (Entry->Type ==
                Attribute->AttributeType &&
            MappingListNameMatches(
                Entry,
                Update->Name,
                NameLength))
        {
            if (Entry->FirstVCN == 0)
            {
                if (FoundFirstEntry ||
                    Entry->BaseFileRef !=
                        OwnerFileReference ||
                    Entry->AttributeId !=
                        Attribute->AttributeID)
                {
                    Status =
                        STATUS_FILE_CORRUPT_ERROR;
                    goto Failure;
                }
                FoundFirstEntry = TRUE;
            }
            else
            {
                PNONRESIDENT_MAPPING_OLD_EXTENT Old =
                    new(PagedPool, TAG_NTFS)
                        NONRESIDENT_MAPPING_OLD_EXTENT();
                if (!Old)
                {
                    Status =
                        STATUS_INSUFFICIENT_RESOURCES;
                    goto Failure;
                }
                Old->FileReference =
                    Entry->BaseFileRef;
                Old->FirstVcn =
                    Entry->FirstVCN;
                Old->AttributeId =
                    Entry->AttributeId;
                if (OldTail)
                    OldTail->Next = Old;
                else
                    Update->OldExtents = Old;
                OldTail = Old;
                if (OldContinuationLength >
                    MAXULONG -
                        Entry->RecordLength)
                {
                    Status =
                        STATUS_FILE_TOO_LARGE;
                    goto Failure;
                }
                OldContinuationLength +=
                    Entry->RecordLength;
            }
        }
        Offset += Entry->RecordLength;
    }
    }
    if (!FoundFirstEntry ||
        Offset != AttributeListLength)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Failure;
    }

    if (FullAttributeLength <=
            MaximumAttributeLength &&
        !Update->OldExtents)
    {
        Status = AttributeOwner->
            BuildNonResidentMappingSegment(
            Attribute,
            Runs,
            0,
            Flags,
            CompressionUnitSize,
            AllocatedSize,
            DataSize,
            InitializedSize,
            Flags != 0
                ? CompressedDataSize
                : 0);
        if (NT_SUCCESS(Status))
        {
            if (ResultOwner)
                *ResultOwner = AttributeOwner;
            delete[] Update->Name;
            delete[] Update->OldList;
            delete[] Update->OwnerRecordBackup;
            delete[] Update->BaseRecordBackup;
            delete Update;
        }
        else
        {
            *MappingUpdate = Update;
            AbortNonResidentMappingUpdate(
                MappingUpdate);
        }
        return Status;
    }

    Cursor = Runs;
    Status = CloneMappingSegment(
        &Cursor,
        DataRunsOffset,
        MaximumAttributeLength,
        &Segment,
        &SegmentClusters);
    if (Status == STATUS_BUFFER_TOO_SMALL ||
        (Status == STATUS_INVALID_PARAMETER &&
         DataRunsOffset >= MaximumAttributeLength))
    {
        PFileRecord Extension = NULL;
        PAttribute ExtensionAttribute = NULL;
        ULONG ExtensionMaximumLength;

        /*
         * A sparse-header transition can make even the VCN-zero header larger
         * than the space left in its current record. Stage a replacement
         * owner and switch the first list entry only after that record is
         * durable. The old attribute remains published until commit.
         */
        Status = DiskVolume->MFT->
            AllocateExtensionFileRecord(
                BaseFileReference,
                &Extension);
        if (!NT_SUCCESS(Status))
            goto Failure;
        Status = Extension->
            SetAutomaticTimestampMask(0);
        if (!NT_SUCCESS(Status))
        {
            (void)DiskVolume->MFT->
                DeallocateExtensionFileRecord(
                    Extension);
            delete Extension;
            goto Failure;
        }
        Status = Extension->
            InsertResidentAttribute(
                Update->Type,
                Update->Name,
                &ExtensionAttribute);
        if (!NT_SUCCESS(Status))
        {
            (void)DiskVolume->MFT->
                DeallocateExtensionFileRecord(
                    Extension);
            delete Extension;
            goto Failure;
        }

        Update->SupersededOwner = AttributeOwner;
        Update->SupersededAttributeId = AttributeId;
        Update->AttributeOwner = Extension;
        AttributeOwner = Extension;
        Attribute = ExtensionAttribute;
        AttributeId = ExtensionAttribute->AttributeID;
        Extension->BaseRecordOwner = this;

        ExtensionMaximumLength =
            (ExtensionAttribute->Length +
             Extension->Header->AllocatedSize -
             Extension->Header->ActualSize) &
            ~(sizeof(ULONGLONG) - 1);
        Status = CloneMappingSegment(
            &Cursor,
            DataRunsOffset,
            ExtensionMaximumLength,
            &Segment,
            &SegmentClusters);
    }
    if (!NT_SUCCESS(Status))
        goto Failure;
    Status = AttributeOwner->
        BuildNonResidentMappingSegment(
        Attribute,
        Segment,
        0,
        Flags,
        CompressionUnitSize,
        AllocatedSize,
        DataSize,
        InitializedSize,
        Flags != 0
            ? CompressedDataSize
            : 0);
    FreeDataRun(Segment);
    Segment = NULL;
    if (!NT_SUCCESS(Status))
        goto Failure;
    FirstVcn = SegmentClusters;

    while (Cursor)
    {
        PNONRESIDENT_MAPPING_NEW_EXTENT NewExtent;
        PFileRecord Extension = NULL;
        PAttribute ExtensionAttribute = NULL;
        ULONG ExtensionMaximumLength;

        Status = DiskVolume->MFT->
            AllocateExtensionFileRecord(
                BaseFileReference,
                &Extension);
        if (!NT_SUCCESS(Status))
            goto Failure;
        Status = Extension->
            SetAutomaticTimestampMask(0);
        if (!NT_SUCCESS(Status))
        {
            (void)DiskVolume->MFT->
                DeallocateExtensionFileRecord(
                    Extension);
            delete Extension;
            goto Failure;
        }
        Status = Extension->
            InsertResidentAttribute(
                Update->Type,
                Update->Name,
                &ExtensionAttribute);
        if (!NT_SUCCESS(Status))
        {
            (void)DiskVolume->MFT->
                DeallocateExtensionFileRecord(
                    Extension);
            delete Extension;
            goto Failure;
        }
        ExtensionMaximumLength =
            (ExtensionAttribute->Length +
             Extension->Header->AllocatedSize -
             Extension->Header->ActualSize) &
            ~(sizeof(ULONGLONG) - 1);
        Status = CloneMappingSegment(
            &Cursor,
            DataRunsOffset,
            ExtensionMaximumLength,
            &Segment,
            &SegmentClusters);
        if (!NT_SUCCESS(Status))
        {
            (void)DiskVolume->MFT->
                DeallocateExtensionFileRecord(
                    Extension);
            delete Extension;
            goto Failure;
        }
        Status = Extension->
            BuildNonResidentMappingSegment(
                ExtensionAttribute,
                Segment,
                FirstVcn,
                Flags,
                CompressionUnitSize,
                0,
                0,
                0,
                0);
        FreeDataRun(Segment);
        Segment = NULL;
        if (!NT_SUCCESS(Status))
        {
            (void)DiskVolume->MFT->
                DeallocateExtensionFileRecord(
                    Extension);
            delete Extension;
            goto Failure;
        }
        Status = DiskVolume->MFT->
            WriteFileRecordToMFT(Extension);
        if (!NT_SUCCESS(Status))
        {
            (void)DiskVolume->MFT->
                DeallocateExtensionFileRecord(
                    Extension);
            delete Extension;
            goto Failure;
        }

        NewExtent =
            new(PagedPool, TAG_NTFS)
                NONRESIDENT_MAPPING_NEW_EXTENT();
        if (!NewExtent)
        {
            (void)DiskVolume->MFT->
                DeallocateExtensionFileRecord(
                    Extension);
            delete Extension;
            Status =
                STATUS_INSUFFICIENT_RESOURCES;
            goto Failure;
        }
        NewExtent->Record = Extension;
        NewExtent->Attribute =
            ExtensionAttribute;
        NewExtent->FirstVcn = FirstVcn;
        if (NewTail)
            NewTail->Next = NewExtent;
        else
            Update->NewExtents = NewExtent;
        NewTail = NewExtent;
        NewExtentCount++;

        if (FirstVcn >
            ~(ULONGLONG)0 - SegmentClusters)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Failure;
        }
        FirstVcn += SegmentClusters;
    }
    if (FirstVcn != TotalClusters)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Failure;
    }

    if (NameBytes > MAXULONG - 0x1a)
    {
        Status = STATUS_FILE_TOO_LARGE;
        goto Failure;
    }
    NewEntryLength = ALIGN_UP_BY(
        0x1a + NameBytes,
        sizeof(ULONGLONG));
    if (NewExtentCount >
        (MAXULONG -
         (AttributeListLength -
          OldContinuationLength)) /
            NewEntryLength)
    {
        Status = STATUS_FILE_TOO_LARGE;
        goto Failure;
    }
    Update->NewListLength =
        AttributeListLength -
        OldContinuationLength +
        NewExtentCount * NewEntryLength;
    if (Update->NewListLength == 0)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Failure;
    }
    Update->NewList =
        new(PagedPool, TAG_NTFS)
            UCHAR[Update->NewListLength]();
    if (!Update->NewList)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Failure;
    }

    OutputOffset = 0;
    Offset = 0;
    FoundFirstEntry = FALSE;
    while (Offset < AttributeListLength)
    {
        PAttributeListEx Entry =
            reinterpret_cast<PAttributeListEx>(
                AttributeListData + Offset);
        BOOLEAN Matches =
            Entry->Type ==
                Attribute->AttributeType &&
            MappingListNameMatches(
                Entry,
                Update->Name,
                NameLength);

        if (!Matches ||
            Entry->FirstVCN == 0)
        {
            PAttributeListEx OutputEntry =
                reinterpret_cast<PAttributeListEx>(
                    Update->NewList +
                        OutputOffset);

            RtlCopyMemory(
                OutputEntry,
                Entry,
                Entry->RecordLength);
            if (Matches &&
                Entry->FirstVCN == 0 &&
                Update->SupersededOwner)
            {
                OutputEntry->BaseFileRef =
                    ((ULONGLONG)
                         AttributeOwner->Header->
                             SequenceNumber << 48) |
                    AttributeOwner->Header->
                        MFTRecordNumber;
                OutputEntry->AttributeId =
                    Attribute->AttributeID;
            }
            OutputOffset += Entry->RecordLength;
        }
        if (Matches &&
            Entry->FirstVCN == 0)
        {
            PNONRESIDENT_MAPPING_NEW_EXTENT Current =
                Update->NewExtents;

            if (FoundFirstEntry)
            {
                Status =
                    STATUS_FILE_CORRUPT_ERROR;
                goto Failure;
            }
            FoundFirstEntry = TRUE;
            while (Current)
            {
                PAttributeListEx NewEntry =
                    reinterpret_cast<PAttributeListEx>(
                        Update->NewList +
                            OutputOffset);
                ULONGLONG FileReference =
                    ((ULONGLONG)Current->
                        Record->Header->
                            SequenceNumber << 48) |
                    Current->Record->Header->
                        MFTRecordNumber;

                NewEntry->Type =
                    Attribute->AttributeType;
                NewEntry->RecordLength =
                    (USHORT)NewEntryLength;
                NewEntry->NameLength =
                    (UCHAR)NameLength;
                NewEntry->NameOffset =
                    NameLength ? 0x1a : 0;
                NewEntry->FirstVCN =
                    Current->FirstVcn;
                NewEntry->BaseFileRef =
                    FileReference;
                NewEntry->AttributeId =
                    Current->Attribute->
                        AttributeID;
                if (NameBytes != 0)
                {
                    RtlCopyMemory(
                        reinterpret_cast<PUCHAR>(
                            NewEntry) +
                            NewEntry->NameOffset,
                        Update->Name,
                        NameBytes);
                }
                OutputOffset += NewEntryLength;
                Current = Current->Next;
            }
        }
        Offset += Entry->RecordLength;
    }
    if (!FoundFirstEntry ||
        OutputOffset !=
            Update->NewListLength)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Failure;
    }

    /*
     * A continuation entry may push the list over its current cluster. Keep
     * that allocation in this transaction instead of recursively committing
     * it through WriteFileData(). The list's own mapping pairs must remain
     * wholly in the base record: it is the bootstrap used to locate every
     * other continuation.
     */
    if (Update->NewListLength >
        ListAttribute->NonResident.AllocatedSize)
    {
        if (ListAttribute->NonResident.AllocatedSize == 0 ||
            ListAttribute->NonResident.AllocatedSize %
                ClusterSize != 0 ||
            ListAttribute->NonResident.DataSize !=
                AttributeListLength ||
            ListAttribute->
                NonResident.InitalizedDataSize !=
                AttributeListLength)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Failure;
        }

        ExistingListClusters =
            ListAttribute->NonResident.AllocatedSize /
                ClusterSize;
        if (ListAttribute->NonResident.LastVCN ==
                ~(ULONGLONG)0 ||
            ListAttribute->NonResident.LastVCN + 1 !=
                ExistingListClusters)
        {
            Status = STATUS_NOT_IMPLEMENTED;
            goto Failure;
        }
        RequiredListClusters =
            Update->NewListLength / ClusterSize +
            (Update->NewListLength % ClusterSize != 0);
        if (RequiredListClusters <=
                ExistingListClusters ||
            RequiredListClusters -
                ExistingListClusters > MAXULONG)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Failure;
        }
        AddedListClusterCount =
            (ULONG)(RequiredListClusters -
                    ExistingListClusters);
        NewListAllocatedSize =
            RequiredListClusters * ClusterSize;

        ListRuns = FindNonResidentData(
            ListAttribute);
        if (!ListRuns)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Failure;
        }
        ExistingListClusters = 0;
        PreferredListLcn = 0;
        for (PDataRun Run = ListRuns;
             Run;
             Run = Run->NextRun)
        {
            if (Run->IsSparse ||
                Run->Length == 0 ||
                Run->LCN >
                    DiskVolume->ClustersInVolume ||
                Run->Length >
                    DiskVolume->ClustersInVolume -
                        Run->LCN ||
                ExistingListClusters >
                    ~(ULONGLONG)0 - Run->Length)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Failure;
            }
            ExistingListClusters += Run->Length;
            if (!Run->NextRun &&
                Run->LCN <=
                    ~(ULONGLONG)0 - Run->Length &&
                Run->LCN + Run->Length <
                    DiskVolume->ClustersInVolume)
            {
                PreferredListLcn =
                    Run->LCN + Run->Length;
            }
        }
        if (ExistingListClusters !=
            ListAttribute->NonResident.AllocatedSize /
                ClusterSize)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Failure;
        }

        /*
         * Prefer a complete copy-on-write relocation. A one-run replacement
         * bounds the bootstrap mapping-pair size regardless of how fragmented
         * the old list became. The old clusters stay allocated until commit,
         * so a crash or abort still leaves the published list readable.
         */
        Status = DiskVolume->AllocateClusters(
            PreferredListLcn,
            (ULONG)RequiredListClusters,
            1,
            &Update->AddedListRuns);
        if (NT_SUCCESS(Status))
        {
            Update->SupersededListRuns =
                ListRuns;
            ListRuns = NULL;
            Status = CombineMappingRunLists(
                NULL,
                Update->AddedListRuns,
                &CombinedListRuns);
            if (!NT_SUCCESS(Status))
                goto Failure;
        }
        else if (Status == STATUS_DISK_FULL ||
                 Status == STATUS_BUFFER_TOO_SMALL)
        {
            /*
             * A sufficiently free but heavily fragmented volume can still
             * grow in place while the resulting mappings fit the base record.
             */
            MaximumListRuns = RecordBufferSize / 3;
            if (MaximumListRuns == 0)
                MaximumListRuns = 1;
            Status = DiskVolume->AllocateClusters(
                PreferredListLcn,
                AddedListClusterCount,
                MaximumListRuns,
                &Update->AddedListRuns);
            if (NT_SUCCESS(Status))
            {
                Status = CombineMappingRunLists(
                    ListRuns,
                    Update->AddedListRuns,
                    &CombinedListRuns);
            }
        }
        if (!NT_SUCCESS(Status))
            goto Failure;

        Status = BuildNonResidentMappingSegment(
            ListAttribute,
            CombinedListRuns,
            0,
            0,
            0,
            NewListAllocatedSize,
            AttributeListLength,
            AttributeListLength,
            0);
        if (!NT_SUCCESS(Status))
            goto Failure;

        /*
         * Resizing $ATTRIBUTE_LIST moves every later base-record attribute.
         * Resolve the VCN-zero stream by stable ID before returning it to the
         * caller that will write data and publish this transaction.
         */
        Attribute = AttributeOwner->
            FindAttributeInRecord(
                Update->Type,
                Update->Name,
                &AttributeId);
        if (!Attribute ||
            !Attribute->IsNonResident ||
            Attribute->NonResident.FirstVCN != 0)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Failure;
        }
    }

    FreeDataRun(CombinedListRuns);
    FreeDataRun(ListRuns);
    *TargetAttribute = Attribute;
    *MappingUpdate = Update;
    if (ResultOwner)
        *ResultOwner = AttributeOwner;
    return STATUS_SUCCESS;

Failure:
    FreeDataRun(CombinedListRuns);
    FreeDataRun(ListRuns);
    FreeDataRun(Segment);
    if (Update)
    {
        *MappingUpdate = Update;
        AbortNonResidentMappingUpdate(
            MappingUpdate);
    }
    return Status;
}

NTSTATUS
FileRecord::CommitNonResidentMappingUpdate(
    _Inout_ PNonResidentMappingUpdate* MappingUpdate)
{
    PNonResidentMappingUpdate Update;
    PFileRecord BaseRecord;
    PFileRecord AttributeOwner;
    PFileRecord CacheRecordToKeep;
    PAttribute ListAttribute;
    LARGE_INTEGER ListOffset = {};
    ULONG WrittenLength;
    BOOLEAN SupersededRemoved = FALSE;
    NTSTATUS Status;

    if (!MappingUpdate || !*MappingUpdate)
        return STATUS_INVALID_PARAMETER;
    Update = *MappingUpdate;
    BaseRecord = Update->BaseRecord;
    AttributeOwner = Update->AttributeOwner;
    if (!BaseRecord || !AttributeOwner ||
        (this != BaseRecord &&
         this != AttributeOwner &&
         this != Update->SupersededOwner))
    {
        return STATUS_INVALID_PARAMETER;
    }

    ListAttribute = BaseRecord->
        FindAttributeInRecord(
        TypeAttributeList,
        NULL,
        NULL);
    if (!ListAttribute ||
        !ListAttribute->IsNonResident ||
        ListAttribute->Flags != 0 ||
        ListAttribute->NonResident.FirstVCN != 0 ||
        ListAttribute->NonResident.AllocatedSize <
            Update->NewListLength ||
        ListAttribute->NonResident.AllocatedSize <
            Update->OldListLength)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    /*
     * Publish the rewritten VCN-zero owner before the list can expose its new
     * owner or continuation records. The old list remains authoritative until
     * the replacement record is durable.
     */
    if (AttributeOwner != BaseRecord)
    {
        Update->OwnerWriteAttempted = TRUE;
        Status = BaseRecord->DiskVolume->MFT->
            WriteFileRecordToMFT(
                AttributeOwner);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    WrittenLength = Update->NewListLength;
    Update->ListWriteAttempted = TRUE;
    Status = BaseRecord->WriteFileData(
        TypeAttributeList,
        NULL,
        Update->NewList,
        &WrittenLength,
        &ListOffset);
    if (!NT_SUCCESS(Status) ||
        WrittenLength != Update->NewListLength)
    {
        return NT_SUCCESS(Status)
            ? STATUS_END_OF_FILE
            : Status;
    }
    ListAttribute = BaseRecord->
        FindAttributeInRecord(
        TypeAttributeList,
        NULL,
        NULL);
    if (!ListAttribute ||
        !ListAttribute->IsNonResident ||
        ListAttribute->NonResident.AllocatedSize <
            Update->NewListLength)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    ListAttribute->NonResident.DataSize =
        Update->NewListLength;
    ListAttribute->NonResident.InitalizedDataSize =
        Update->NewListLength;
    Status = BaseRecord->DiskVolume->MFT->
        WriteFileRecordToMFT(BaseRecord);
    if (!NT_SUCCESS(Status))
        return Status;

    delete[] BaseRecord->AttributeListData;
    BaseRecord->AttributeListData = NULL;
    BaseRecord->AttributeListLength = 0;

    /*
     * A relocated first extent is no longer referenced by the durable list.
     * Retire only that one attribute; its old owner can contain unrelated
     * streams. Failure here leaves an unreachable duplicate but cannot
     * invalidate the now-authoritative layout.
     */
    if (Update->SupersededOwner)
    {
        PAttribute OldAttribute =
            Update->SupersededOwner->
                FindAttributeInRecord(
                    Update->Type,
                    Update->Name,
                    &Update->
                        SupersededAttributeId);

        if (OldAttribute &&
            OldAttribute->IsNonResident &&
            OldAttribute->NonResident.FirstVCN == 0 &&
            NT_SUCCESS(
                Update->SupersededOwner->
                    RemoveAttributeRecord(
                        OldAttribute)) &&
            NT_SUCCESS(
                BaseRecord->DiskVolume->MFT->
                    WriteFileRecordToMFT(
                        Update->
                            SupersededOwner)))
        {
            SupersededRemoved = TRUE;
        }
    }

    /*
     * The new list is durable, so old mapping-only records are now
     * unreachable. Remove their attribute records after publication. A
     * cleanup failure is a bounded metadata leak, not a reason to roll back
     * the now-authoritative stream layout without LFS.
     */
    for (PNONRESIDENT_MAPPING_OLD_EXTENT Old =
             Update->OldExtents;
         Old;
         Old = Old->Next)
    {
        PFileRecord Record =
            BaseRecord->
                GetExtentRecord(
                    Old->FileReference);
        PAttribute OldAttribute;
        NTSTATUS CleanupStatus;

        if (!Record)
            continue;
        OldAttribute =
            Record->FindAttributeInRecord(
                Update->Type,
                Update->Name,
                &Old->AttributeId);
        if (!OldAttribute ||
            !OldAttribute->IsNonResident ||
            OldAttribute->NonResident.FirstVCN !=
                Old->FirstVcn)
        {
            continue;
        }
        CleanupStatus =
            Record->RemoveAttributeRecord(
                OldAttribute);
        if (NT_SUCCESS(CleanupStatus))
        {
            (void)BaseRecord->DiskVolume->MFT->
                WriteFileRecordToMFT(Record);
        }
    }
    for (PNONRESIDENT_MAPPING_OLD_EXTENT Old =
             Update->OldExtents;
         Old;
         Old = Old->Next)
    {
        BOOLEAN Seen = FALSE;
        PFileRecord Record;

        for (PNONRESIDENT_MAPPING_OLD_EXTENT Prior =
                 Update->OldExtents;
             Prior != Old;
             Prior = Prior->Next)
        {
            if (Prior->FileReference ==
                Old->FileReference)
            {
                Seen = TRUE;
                break;
            }
        }
        if (Seen)
            continue;
        Record = BaseRecord->
            GetExtentRecord(
                Old->FileReference);
        if (Record == Update->SupersededOwner)
            continue;
        if (MappingExtensionRecordIsEmpty(
                Record))
        {
            (void)BaseRecord->DiskVolume->MFT->
                DeallocateExtensionFileRecord(
                    Record);
        }
    }
    if (SupersededRemoved &&
        Update->SupersededOwner != BaseRecord &&
        MappingExtensionRecordIsEmpty(
            Update->SupersededOwner))
    {
        (void)BaseRecord->DiskVolume->MFT->
            DeallocateExtensionFileRecord(
                Update->SupersededOwner);
    }

    BaseRecord->ClearDataRunCache();
    AttributeOwner->ClearDataRunCache();
    CacheRecordToKeep =
        Update->SupersededOwner &&
        Update->SupersededOwner != BaseRecord
        ? Update->SupersededOwner
        : (AttributeOwner != BaseRecord &&
           !Update->SupersededOwner
           ? AttributeOwner
           : NULL);
    BaseRecord->ClearExtentCacheExcept(
        CacheRecordToKeep);

    while (Update->NewExtents)
    {
        PNONRESIDENT_MAPPING_NEW_EXTENT Next =
            Update->NewExtents->Next;
        delete Update->NewExtents->Record;
        delete Update->NewExtents;
        Update->NewExtents = Next;
    }
    while (Update->OldExtents)
    {
        PNONRESIDENT_MAPPING_OLD_EXTENT Next =
            Update->OldExtents->Next;
        delete Update->OldExtents;
        Update->OldExtents = Next;
    }
    if (Update->SupersededOwner &&
        AttributeOwner != this)
    {
        delete AttributeOwner;
    }
    if (Update->SupersededListRuns)
    {
        (void)BaseRecord->DiskVolume->
            ReleaseClusters(
                Update->SupersededListRuns);
    }
    FreeDataRun(Update->SupersededListRuns);
    FreeDataRun(Update->AddedListRuns);
    delete[] Update->Name;
    delete[] Update->NewList;
    delete[] Update->OldList;
    delete[] Update->OwnerRecordBackup;
    delete[] Update->BaseRecordBackup;
    delete Update;
    *MappingUpdate = NULL;
    return STATUS_SUCCESS;
}

void
FileRecord::AbortNonResidentMappingUpdate(
    _Inout_ PNonResidentMappingUpdate* MappingUpdate)
{
    PNonResidentMappingUpdate Update;
    PFileRecord BaseRecord;
    PFileRecord AttributeOwner;
    PFileRecord BackupOwner;
    BOOLEAN MetadataRestored = TRUE;

    if (!MappingUpdate || !*MappingUpdate)
        return;
    Update = *MappingUpdate;
    BaseRecord = Update->BaseRecord;
    AttributeOwner = Update->AttributeOwner;
    BackupOwner = Update->SupersededOwner
        ? Update->SupersededOwner
        : AttributeOwner;

    if (Update->OwnerRecordBackup &&
        BackupOwner)
    {
        RtlCopyMemory(
            BackupOwner->Data,
            Update->OwnerRecordBackup,
            BackupOwner->RecordBufferSize);
        BackupOwner->Header =
            reinterpret_cast<PFileRecordHeader>(
                BackupOwner->Data);
        BackupOwner->ClearDataRunCache();

        if (!Update->SupersededOwner &&
            Update->OwnerWriteAttempted &&
            (!BaseRecord ||
             !NT_SUCCESS(
                 BaseRecord->DiskVolume->MFT->
                     WriteFileRecordToMFT(
                         BackupOwner))))
        {
            MetadataRestored = FALSE;
        }
    }

    if (Update->BaseRecordBackup &&
        BaseRecord)
    {
        RtlCopyMemory(BaseRecord->Data,
                      Update->BaseRecordBackup,
                      BaseRecord->RecordBufferSize);
        BaseRecord->Header =
            reinterpret_cast<PFileRecordHeader>(
                BaseRecord->Data);
        BaseRecord->ClearDataRunCache();
        delete[] BaseRecord->AttributeListData;
        BaseRecord->AttributeListData = NULL;
        BaseRecord->AttributeListLength = 0;
    }

    if (Update->ListWriteAttempted &&
        BaseRecord &&
        Update->OldList &&
        Update->OldListLength != 0)
    {
        LARGE_INTEGER ListOffset = {};
        ULONG WrittenLength =
            Update->OldListLength;
        NTSTATUS RestoreStatus =
            BaseRecord->WriteFileData(
                TypeAttributeList,
                NULL,
                Update->OldList,
                &WrittenLength,
                &ListOffset);

        if (NT_SUCCESS(RestoreStatus) &&
            WrittenLength ==
                Update->OldListLength)
        {
            PAttribute ListAttribute =
                BaseRecord->
                    FindAttributeInRecord(
                    TypeAttributeList,
                    NULL,
                    NULL);
            if (ListAttribute &&
                ListAttribute->IsNonResident)
            {
                ListAttribute->
                    NonResident.DataSize =
                        Update->
                            OldListLength;
                ListAttribute->
                    NonResident.
                        InitalizedDataSize =
                            Update->
                                OldListLength;
                RestoreStatus =
                    BaseRecord->DiskVolume->MFT->
                        WriteFileRecordToMFT(
                            BaseRecord);
            }
            else
            {
                RestoreStatus =
                    STATUS_FILE_CORRUPT_ERROR;
            }
        }
        if (!NT_SUCCESS(RestoreStatus) ||
            WrittenLength !=
                Update->OldListLength)
        {
            MetadataRestored = FALSE;
        }
    }

    while (Update->NewExtents)
    {
        PNONRESIDENT_MAPPING_NEW_EXTENT Next =
            Update->NewExtents->Next;

        if (MetadataRestored)
        {
            (void)BaseRecord->DiskVolume->MFT->
                DeallocateExtensionFileRecord(
                    Update->NewExtents->
                        Record);
        }
        delete Update->NewExtents->Record;
        delete Update->NewExtents;
        Update->NewExtents = Next;
    }
    if (Update->SupersededOwner &&
        AttributeOwner)
    {
        if (MetadataRestored &&
            BaseRecord)
        {
            (void)BaseRecord->DiskVolume->MFT->
                DeallocateExtensionFileRecord(
                    AttributeOwner);
        }
        if (AttributeOwner != this)
            delete AttributeOwner;
    }
    while (Update->OldExtents)
    {
        PNONRESIDENT_MAPPING_OLD_EXTENT Next =
            Update->OldExtents->Next;
        delete Update->OldExtents;
        Update->OldExtents = Next;
    }
    if (MetadataRestored &&
        Update->AddedListRuns &&
        BaseRecord)
    {
        (void)BaseRecord->DiskVolume->
            ReleaseClusters(
                Update->AddedListRuns);
    }
    FreeDataRun(Update->AddedListRuns);
    FreeDataRun(Update->SupersededListRuns);
    delete[] Update->Name;
    delete[] Update->NewList;
    delete[] Update->OldList;
    delete[] Update->OwnerRecordBackup;
    delete[] Update->BaseRecordBackup;
    delete Update;
    *MappingUpdate = NULL;
}

NTSTATUS
FileRecord::ConvertResidentToNonResident(
    _In_ PAttribute TargetAttribute,
    _In_ PDataRun Runs,
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize,
    _In_ ULONGLONG InitializedSize)
{
    ULONGLONG ClusterSize = BytesPerCluster(DiskVolume);
    ULONGLONG TotalClusters;
    PUCHAR NameCopy = NULL;
    ULONG NameBytes;
    ULONG MappingBytes;
    ULONG MappingCapacity;
    ULONG NewAttributeLength;
    ULONG DataRunsOffset;
    NTSTATUS Status;

    Status = ValidateResidentAttributeForUpdate(
        TargetAttribute,
        NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if (TargetAttribute->Flags != 0 ||
        ClusterSize == 0 ||
        AllocatedSize == 0 ||
        AllocatedSize % ClusterSize != 0 ||
        InitializedSize > DataSize ||
        DataSize > AllocatedSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    NameBytes =
        TargetAttribute->NameLength * sizeof(WCHAR);
    if (NameBytes != 0)
    {
        NameCopy =
            new(PagedPool, TAG_NTFS) UCHAR[NameBytes];
        if (!NameCopy)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlCopyMemory(NameCopy,
                      reinterpret_cast<PUCHAR>(
                          TargetAttribute) +
                          TargetAttribute->NameOffset,
                      NameBytes);
    }

    Status = GetMappingPairsSize(Runs,
                                 &MappingBytes,
                                 &TotalClusters);
    if (!NT_SUCCESS(Status))
        goto Done;
    if (TotalClusters != AllocatedSize / ClusterSize)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    DataRunsOffset = ALIGN_UP_BY(
        0x40 + NameBytes,
        sizeof(ULONGLONG));
    if (DataRunsOffset > MAXUSHORT ||
        DataRunsOffset > MAXULONG - MappingBytes)
    {
        Status = STATUS_FILE_TOO_LARGE;
        goto Done;
    }
    NewAttributeLength = ALIGN_UP_BY(
        DataRunsOffset + MappingBytes,
        sizeof(ULONGLONG));

    Status = ResizeAttributeRecord(TargetAttribute,
                                   NewAttributeLength);
    if (!NT_SUCCESS(Status))
        goto Done;

    /*
     * Keep the common 16-byte attribute header and rebuild the variant,
     * name, mapping pairs, and padding from scratch.
     */
    RtlZeroMemory(
        reinterpret_cast<PUCHAR>(TargetAttribute) + 0x10,
        NewAttributeLength - 0x10);
    TargetAttribute->IsNonResident = TRUE;
    TargetAttribute->NameOffset =
        NameBytes != 0 ? 0x40 : 0;
    if (NameBytes != 0)
    {
        RtlCopyMemory(
            reinterpret_cast<PUCHAR>(TargetAttribute) +
                TargetAttribute->NameOffset,
            NameCopy,
            NameBytes);
    }

    TargetAttribute->NonResident.FirstVCN = 0;
    TargetAttribute->NonResident.LastVCN =
        TotalClusters - 1;
    TargetAttribute->NonResident.DataRunsOffset =
        (USHORT)DataRunsOffset;
    TargetAttribute->NonResident.CompressionUnitSize = 0;
    TargetAttribute->NonResident.Reserved = 0;
    TargetAttribute->NonResident.AllocatedSize =
        AllocatedSize;
    TargetAttribute->NonResident.DataSize = DataSize;
    TargetAttribute->NonResident.InitalizedDataSize =
        InitializedSize;

    MappingCapacity =
        NewAttributeLength - DataRunsOffset;
    Status = EncodeMappingPairs(
        Runs,
        reinterpret_cast<PUCHAR>(TargetAttribute) +
            DataRunsOffset,
        MappingCapacity,
        NULL);
    if (NT_SUCCESS(Status))
        ClearDataRunCache();

Done:
    delete[] NameCopy;
    return Status;
}

NTSTATUS
FileRecord::ConvertNonResidentToResidentEmpty(
    _In_ PAttribute TargetAttribute)
{
    PUCHAR NameCopy = NULL;
    ULONG NameBytes;
    ULONG DataOffset;
    ULONG NewAttributeLength;
    ULONG OldAttributeLength;
    NTSTATUS Status;

    Status = ValidateAttributeForUpdate(TargetAttribute,
                                        TRUE,
                                        NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if ((TargetAttribute->Flags & ~ATTR_SPARSE) != 0 ||
        TargetAttribute->NonResident.FirstVCN != 0)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    NameBytes =
        TargetAttribute->NameLength * sizeof(WCHAR);
    if (NameBytes != 0)
    {
        NameCopy =
            new(PagedPool, TAG_NTFS) UCHAR[NameBytes];
        if (!NameCopy)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlCopyMemory(
            NameCopy,
            reinterpret_cast<PUCHAR>(TargetAttribute) +
                TargetAttribute->NameOffset,
            NameBytes);
    }

    DataOffset = ALIGN_UP_BY(
        0x18 + NameBytes,
        sizeof(ULONGLONG));
    if (DataOffset > MAXUSHORT)
    {
        Status = STATUS_FILE_TOO_LARGE;
        goto Done;
    }
    NewAttributeLength = DataOffset;

    /*
     * Build a valid resident header while the old attribute still has enough
     * space, then let the generic resident resizer compact the record.
     */
    OldAttributeLength = TargetAttribute->Length;
    RtlZeroMemory(
        reinterpret_cast<PUCHAR>(TargetAttribute) + 0x10,
        OldAttributeLength - 0x10);
    TargetAttribute->Flags = 0;
    TargetAttribute->IsNonResident = FALSE;
    TargetAttribute->NameOffset =
        NameBytes != 0 ? 0x18 : 0;
    TargetAttribute->Resident.DataLength = 0;
    TargetAttribute->Resident.DataOffset =
        (USHORT)DataOffset;
    TargetAttribute->Resident.IndexedFlag = 0;
    TargetAttribute->Resident.Padding = 0;
    if (NameBytes != 0)
    {
        RtlCopyMemory(
            reinterpret_cast<PUCHAR>(TargetAttribute) +
                TargetAttribute->NameOffset,
            NameCopy,
            NameBytes);
    }

    Status = ResizeAttributeRecord(TargetAttribute,
                                   NewAttributeLength);
    if (!NT_SUCCESS(Status))
        goto Done;
    ClearDataRunCache();

Done:
    delete[] NameCopy;
    return Status;
}

NTSTATUS
FileRecord::UpdateFileNameSizes(
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize)
{
    return UpdateFileNameInformation(
        NTFS_FILE_NAME_UPDATE_SIZES,
        AllocatedSize,
        DataSize,
        0,
        0,
        0);
}

NTSTATUS
FileRecord::UpdateFileNameEaSize(
    _In_ USHORT PackedEaSize)
{
    return UpdateFileNameInformation(
        NTFS_FILE_NAME_UPDATE_EA_SIZE,
        0,
        0,
        PackedEaSize,
        0,
        0);
}

NTSTATUS
FileRecord::UpdateFileNameInformation(
    _In_ UINT32 Fields,
    _In_ ULONGLONG AllocatedSize,
    _In_ ULONGLONG DataSize,
    _In_ USHORT PackedEaSize,
    _In_ ULONG ReparseTag,
    _In_ ULONG StorageFlags)
{
    ULONG Offset;

    if (Fields == 0 ||
        (Fields & ~NTFS_FILE_NAME_UPDATE_ALL) != 0 ||
        (StorageFlags &
         ~(FILE_PERM_SPARSE |
           FILE_PERM_COMPRESSED)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!Data || !Header ||
        Header->ActualSize > Header->AllocatedSize ||
        Header->AllocatedSize > RecordBufferSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Offset = Header->AttributeOffset;
    while (Offset < Header->ActualSize)
    {
        PAttribute Attribute;
        ULONG Remaining = Header->ActualSize - Offset;
        ULONG MinimumSize;

        if (Remaining < sizeof(ULONG))
            return STATUS_FILE_CORRUPT_ERROR;
        Attribute =
            reinterpret_cast<PAttribute>(Data + Offset);
        if (Attribute->AttributeType ==
            TypeAttributeEndMarker)
        {
            return STATUS_SUCCESS;
        }

        MinimumSize = Attribute->IsNonResident
            ? ((Attribute->Flags &
                (ATTR_COMPRESSION_MASK | ATTR_SPARSE))
                ? 0x48
                : 0x40)
            : 0x18;
        if (Attribute->Length < MinimumSize ||
            (Attribute->Length &
             (sizeof(ULONGLONG) - 1)) != 0 ||
            Attribute->Length > Remaining)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Attribute->AttributeType == TypeFileName)
        {
            PFileNameEx FileName;
            ULONG NameBytes;

            if (Attribute->IsNonResident ||
                Attribute->Resident.DataOffset < 0x18 ||
                Attribute->Resident.DataOffset >
                    Attribute->Length ||
                Attribute->Resident.DataLength <
                    FIELD_OFFSET(FileNameEx, Name) ||
                Attribute->Resident.DataLength >
                    Attribute->Length -
                        Attribute->Resident.DataOffset)
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }

            FileName = reinterpret_cast<PFileNameEx>(
                GetResidentDataPointer(Attribute));
            NameBytes = FileName->NameLength *
                        sizeof(WCHAR);
            if (NameBytes >
                Attribute->Resident.DataLength -
                    FIELD_OFFSET(FileNameEx, Name))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }
            if (Fields & NTFS_FILE_NAME_UPDATE_SIZES)
            {
                FileName->AllocatedSize = AllocatedSize;
                FileName->DataSize = DataSize;
            }
            if (Fields &
                NTFS_FILE_NAME_UPDATE_REPARSE_TAG)
            {
                if (ReparseTag != 0)
                {
                    FileName->Flags |=
                        FILE_PERM_REPARSE_PT;
                    FileName->Extended.ReparseTag =
                        ReparseTag;
                }
                else
                {
                    FileName->Flags &=
                        ~FILE_PERM_REPARSE_PT;
                    FileName->Extended.EAInfo.PackedEASize =
                        PackedEaSize;
                    FileName->Extended.EAInfo.Padding = 0;
                }
            }
            if ((Fields & NTFS_FILE_NAME_UPDATE_EA_SIZE) &&
                !(FileName->Flags &
                  FILE_PERM_REPARSE_PT))
            {
                FileName->Extended.EAInfo.PackedEASize =
                    PackedEaSize;
                FileName->Extended.EAInfo.Padding = 0;
            }
            if (Fields & NTFS_FILE_NAME_UPDATE_ARCHIVE)
                FileName->Flags |= FILE_PERM_ARCHIVE;
            if (Fields &
                NTFS_FILE_NAME_UPDATE_STORAGE_FLAGS)
            {
                FileName->Flags =
                    (FileName->Flags &
                     ~(FILE_PERM_SPARSE |
                       FILE_PERM_COMPRESSED)) |
                    StorageFlags;
            }
        }

        Offset += Attribute->Length;
    }

    return STATUS_FILE_CORRUPT_ERROR;
}

NTSTATUS
FileRecord::ResizeResidentData(_In_ PAttribute TargetAttribute,
                               _In_ ULONG NewDataLength)
{
    PUCHAR DataStart;
    PUCHAR OldTail;
    PUCHAR NewTail;
    ULONG OldActualSize;
    ULONG OldAttributeLength;
    ULONG OldDataLength;
    ULONG NewAttributeLength;
    ULONG NewActualSize;
    ULONG TailLength;
    NTSTATUS Status;

    Status = ValidateResidentAttributeForUpdate(TargetAttribute,
                                                &OldDataLength);
    if (!NT_SUCCESS(Status))
        return Status;

    if (TargetAttribute->Resident.DataOffset >
            MAXULONG - NewDataLength ||
        TargetAttribute->Resident.DataOffset + NewDataLength >
            MAXULONG - (sizeof(ULONGLONG) - 1))
    {
        return STATUS_FILE_TOO_LARGE;
    }

    NewAttributeLength = ALIGN_UP_BY(
        TargetAttribute->Resident.DataOffset + NewDataLength,
        sizeof(ULONGLONG));
    OldAttributeLength = TargetAttribute->Length;
    OldActualSize = Header->ActualSize;
    NewActualSize =
        OldActualSize - OldAttributeLength + NewAttributeLength;
    if (NewActualSize > Header->AllocatedSize)
        return STATUS_BUFFER_TOO_SMALL;

    OldTail = reinterpret_cast<PUCHAR>(TargetAttribute) +
              OldAttributeLength;
    NewTail = reinterpret_cast<PUCHAR>(TargetAttribute) +
              NewAttributeLength;
    TailLength = (ULONG)((Data + OldActualSize) - OldTail);
    if (NewTail != OldTail)
        RtlMoveMemory(NewTail, OldTail, TailLength);

    TargetAttribute->Length = NewAttributeLength;
    TargetAttribute->Resident.DataLength = NewDataLength;
    Header->ActualSize = NewActualSize;

    DataStart = reinterpret_cast<PUCHAR>(TargetAttribute) +
                TargetAttribute->Resident.DataOffset;
    if (NewDataLength > OldDataLength)
    {
        RtlZeroMemory(DataStart + OldDataLength,
                      NewDataLength - OldDataLength);
    }
    if (NewAttributeLength >
        TargetAttribute->Resident.DataOffset + NewDataLength)
    {
        RtlZeroMemory(
            DataStart + NewDataLength,
            NewAttributeLength -
                TargetAttribute->Resident.DataOffset -
                NewDataLength);
    }
    if (NewActualSize < OldActualSize)
    {
        RtlZeroMemory(Data + NewActualSize,
                      OldActualSize - NewActualSize);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::UpdateResidentData(_In_ PAttribute TargetAttribute,
                               _In_ PUCHAR Buffer,
                               _In_ PULONG Length,
                               _In_ ULONGLONG Offset)
{
    PUCHAR DataStart;
    ULONG NewDataLength;
    ULONG OldDataLength;
    ULONG RequestedLength;
    NTSTATUS Status;

    if (!Length)
        return STATUS_INVALID_PARAMETER;
    RequestedLength = *Length;
    if (RequestedLength != 0 && !Buffer)
        return STATUS_INVALID_PARAMETER;

    Status = ValidateResidentAttributeForUpdate(TargetAttribute,
                                                &OldDataLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Offset > MAXULONG ||
        RequestedLength > MAXULONG - (ULONG)Offset)
    {
        return STATUS_FILE_TOO_LARGE;
    }
    if (RequestedLength == 0)
        return STATUS_SUCCESS;

    NewDataLength = (ULONG)Offset + RequestedLength;
    if (NewDataLength < OldDataLength)
        NewDataLength = OldDataLength;
    if (NewDataLength != OldDataLength)
    {
        Status = ResizeResidentData(TargetAttribute, NewDataLength);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    DataStart = reinterpret_cast<PUCHAR>(TargetAttribute) +
                TargetAttribute->Resident.DataOffset;
    RtlMoveMemory(DataStart + (ULONG)Offset,
                  Buffer,
                  RequestedLength);

    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::ReplaceResidentData(_In_ PAttribute TargetAttribute,
                                _In_opt_ const UCHAR* Buffer,
                                _In_ ULONG Length)
{
    NTSTATUS Status;

    if (Length != 0 && !Buffer)
        return STATUS_INVALID_PARAMETER;

    Status = ResizeResidentData(TargetAttribute, Length);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Length != 0)
    {
        RtlMoveMemory(
            reinterpret_cast<PUCHAR>(TargetAttribute) +
                TargetAttribute->Resident.DataOffset,
            Buffer,
            Length);
    }

    return STATUS_SUCCESS;
}
