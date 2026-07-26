/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#define IsOffsetEndOfFile(Offset) \
Offset->HighPart == -1 && Offset->LowPart == FILE_WRITE_TO_END_OF_FILE

#define NTFS_ZERO_CHUNK_SIZE 0x10000

static UINT32
AutomaticTimestampFieldsForAttribute(
    _In_ AttributeType Type,
    _In_ BOOLEAN DataChanged)
{
    if (Type == TypeData)
    {
        return DataChanged
            ? (NTFS_BASIC_INFO_LAST_WRITE_TIME |
               NTFS_BASIC_INFO_CHANGE_TIME)
            : NTFS_BASIC_INFO_CHANGE_TIME;
    }
    if (Type == TypeEA ||
        Type == TypeEAInformation ||
        Type == TypeReparsePoint)
    {
        return NTFS_BASIC_INFO_CHANGE_TIME;
    }
    return 0;
}

static NTSTATUS
AppendRunCopy(_Inout_ PDataRun* Head,
              _Inout_ PDataRun* Tail,
              _In_ PDataRun Source)
{
    PDataRun Copy;

    if (!Source ||
        Source->IsSparse ||
        Source->Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
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

    Copy = new(PagedPool, TAG_DATA_RUN) DataRun();
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
CloneAllocatedRuns(_In_opt_ PDataRun Source,
                   _Out_ PDataRun* Allocated)
{
    PDataRun Head = NULL;
    PDataRun Tail = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Allocated)
        return STATUS_INVALID_PARAMETER;
    *Allocated = NULL;

    for (PDataRun Run = Source; Run; Run = Run->NextRun)
    {
        if (Run->Length == 0)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        if (Run->IsSparse)
            continue;
        if (Run->LCN > ~(ULONGLONG)0 - Run->Length)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        Status = AppendRunCopy(&Head, &Tail, Run);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    *Allocated = Head;
    Head = NULL;

Done:
    FreeDataRun(Head);
    return Status;
}

static NTSTATUS
AppendLogicalRun(_Inout_ PDataRun* Head,
                 _Inout_ PDataRun* Tail,
                 _In_ BOOLEAN Sparse,
                 _In_ ULONGLONG LCN,
                 _In_ ULONGLONG Length)
{
    PDataRun Run;

    if (!Head || !Tail || Length == 0 ||
        (!Sparse &&
         LCN > ~(ULONGLONG)0 - Length))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (*Tail &&
        (*Tail)->IsSparse == Sparse &&
        ((*Tail)->IsSparse ||
         ((*Tail)->LCN <=
              ~(ULONGLONG)0 - (*Tail)->Length &&
          (*Tail)->LCN + (*Tail)->Length == LCN)) &&
        (*Tail)->Length <=
            ~(ULONGLONG)0 - Length)
    {
        (*Tail)->Length += Length;
        return STATUS_SUCCESS;
    }

    Run = new(PagedPool, TAG_DATA_RUN) DataRun();
    if (!Run)
        return STATUS_INSUFFICIENT_RESOURCES;
    Run->NextRun = NULL;
    Run->LCN = Sparse ? 0 : LCN;
    Run->Length = Length;
    Run->IsSparse = Sparse;

    if (*Tail)
        (*Tail)->NextRun = Run;
    else
        *Head = Run;
    *Tail = Run;
    return STATUS_SUCCESS;
}

static NTSTATUS
AppendAllocatedClusters(
    _Inout_ PDataRun* AllocatedRun,
    _Inout_ PULONGLONG AllocatedOffset,
    _In_ ULONGLONG ClusterCount,
    _Inout_ PDataRun* Head,
    _Inout_ PDataRun* Tail)
{
    NTSTATUS Status;

    if (!AllocatedRun || !AllocatedOffset ||
        !Head || !Tail)
    {
        return STATUS_INVALID_PARAMETER;
    }

    while (ClusterCount != 0)
    {
        ULONGLONG Available;
        ULONGLONG Take;

        if (!*AllocatedRun ||
            (*AllocatedRun)->IsSparse ||
            (*AllocatedRun)->Length == 0 ||
            *AllocatedOffset >=
                (*AllocatedRun)->Length)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Available =
            (*AllocatedRun)->Length -
            *AllocatedOffset;
        Take = min(ClusterCount, Available);
        Status = AppendLogicalRun(
            Head,
            Tail,
            FALSE,
            (*AllocatedRun)->LCN +
                *AllocatedOffset,
            Take);
        if (!NT_SUCCESS(Status))
            return Status;

        ClusterCount -= Take;
        *AllocatedOffset += Take;
        if (*AllocatedOffset ==
            (*AllocatedRun)->Length)
        {
            *AllocatedRun =
                (*AllocatedRun)->NextRun;
            *AllocatedOffset = 0;
        }
    }

    return STATUS_SUCCESS;
}

static NTSTATUS
AppendSparseWriteSegment(
    _In_ PDataRun Source,
    _In_ ULONGLONG SegmentStart,
    _In_ ULONGLONG WriteStart,
    _In_ ULONGLONG WriteEnd,
    _Inout_ PDataRun* AllocatedRun,
    _Inout_ PULONGLONG AllocatedOffset,
    _Inout_ PDataRun* Head,
    _Inout_ PDataRun* Tail)
{
    ULONGLONG SegmentEnd;
    ULONGLONG IntersectionStart;
    ULONGLONG IntersectionEnd;
    NTSTATUS Status;

    if (!Source || Source->Length == 0 ||
        SegmentStart >
            ~(ULONGLONG)0 - Source->Length)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    SegmentEnd = SegmentStart + Source->Length;

    if (!Source->IsSparse ||
        SegmentEnd <= WriteStart ||
        SegmentStart >= WriteEnd)
    {
        return AppendLogicalRun(
            Head,
            Tail,
            Source->IsSparse,
            Source->LCN,
            Source->Length);
    }

    IntersectionStart =
        SegmentStart > WriteStart
        ? SegmentStart
        : WriteStart;
    IntersectionEnd =
        SegmentEnd < WriteEnd
        ? SegmentEnd
        : WriteEnd;

    if (IntersectionStart > SegmentStart)
    {
        Status = AppendLogicalRun(
            Head,
            Tail,
            TRUE,
            0,
            IntersectionStart - SegmentStart);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Status = AppendAllocatedClusters(
        AllocatedRun,
        AllocatedOffset,
        IntersectionEnd - IntersectionStart,
        Head,
        Tail);
    if (!NT_SUCCESS(Status))
        return Status;

    if (IntersectionEnd < SegmentEnd)
    {
        Status = AppendLogicalRun(
            Head,
            Tail,
            TRUE,
            0,
            SegmentEnd - IntersectionEnd);
    }
    return Status;
}

/*
 * Copies one logical source segment while replacing the requested VCN
 * intersection with a sparse run. Physical clusters removed from the mapping
 * are copied to ReleasedHead so the bitmap can be updated only after the MFT
 * record is durable.
 */
static NTSTATUS
AppendSparseHoleSegment(
    _In_ PDataRun Source,
    _In_ ULONGLONG SegmentStart,
    _In_ ULONGLONG HoleStart,
    _In_ ULONGLONG HoleEnd,
    _Inout_ PDataRun* Head,
    _Inout_ PDataRun* Tail,
    _Inout_ PDataRun* ReleasedHead,
    _Inout_ PDataRun* ReleasedTail)
{
    DataRun ReleasedPart = {};
    ULONGLONG SegmentEnd;
    ULONGLONG IntersectionStart;
    ULONGLONG IntersectionEnd;
    NTSTATUS Status;

    if (!Source || !Head || !Tail ||
        !ReleasedHead || !ReleasedTail ||
        Source->Length == 0 ||
        SegmentStart >
            ~(ULONGLONG)0 - Source->Length ||
        HoleStart > HoleEnd)
    {
        return STATUS_INVALID_PARAMETER;
    }
    SegmentEnd = SegmentStart + Source->Length;

    if (Source->IsSparse ||
        HoleStart == HoleEnd ||
        SegmentEnd <= HoleStart ||
        SegmentStart >= HoleEnd)
    {
        return AppendLogicalRun(Head,
                                Tail,
                                Source->IsSparse,
                                Source->LCN,
                                Source->Length);
    }

    IntersectionStart =
        SegmentStart > HoleStart
        ? SegmentStart
        : HoleStart;
    IntersectionEnd =
        SegmentEnd < HoleEnd
        ? SegmentEnd
        : HoleEnd;

    if (IntersectionStart > SegmentStart)
    {
        Status = AppendLogicalRun(
            Head,
            Tail,
            FALSE,
            Source->LCN,
            IntersectionStart - SegmentStart);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Status = AppendLogicalRun(
        Head,
        Tail,
        TRUE,
        0,
        IntersectionEnd - IntersectionStart);
    if (!NT_SUCCESS(Status))
        return Status;

    ReleasedPart.LCN =
        Source->LCN +
        (IntersectionStart - SegmentStart);
    ReleasedPart.Length =
        IntersectionEnd - IntersectionStart;
    Status = AppendRunCopy(ReleasedHead,
                           ReleasedTail,
                           &ReleasedPart);
    if (!NT_SUCCESS(Status))
        return Status;

    if (IntersectionEnd < SegmentEnd)
    {
        Status = AppendLogicalRun(
            Head,
            Tail,
            FALSE,
            Source->LCN +
                (IntersectionEnd - SegmentStart),
            SegmentEnd - IntersectionEnd);
    }
    return Status;
}

static NTSTATUS
StoreAllocatedRange(
    _In_opt_ PNtfsAllocatedRange Ranges,
    _In_ ULONG Capacity,
    _Inout_ PULONG Written,
    _In_ ULONGLONG Start,
    _In_ ULONGLONG End)
{
    if (!Written || Start >= End)
        return STATUS_INVALID_PARAMETER;
    if (*Written >= Capacity)
    {
        return Capacity == 0
            ? STATUS_BUFFER_TOO_SMALL
            : STATUS_BUFFER_OVERFLOW;
    }
    if (!Ranges)
        return STATUS_INVALID_PARAMETER;

    Ranges[*Written].FileOffset = Start;
    Ranges[*Written].Length = End - Start;
    (*Written)++;
    return STATUS_SUCCESS;
}

static NTSTATUS
RecordContainsSparseData(
    _In_ PUCHAR RecordData,
    _In_ PFileRecordHeader RecordHeader,
    _In_ ULONG RecordBufferSize,
    _Out_ PBOOLEAN ContainsSparse)
{
    ULONG Offset;

    if (!RecordData || !RecordHeader ||
        !ContainsSparse ||
        RecordHeader->ActualSize >
            RecordHeader->AllocatedSize ||
        RecordHeader->AllocatedSize >
            RecordBufferSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    *ContainsSparse = FALSE;
    Offset = RecordHeader->AttributeOffset;

    while (Offset < RecordHeader->ActualSize)
    {
        PAttribute Attribute;
        ULONG Remaining =
            RecordHeader->ActualSize - Offset;
        ULONG MinimumSize;

        if (Remaining < sizeof(ULONG))
            return STATUS_FILE_CORRUPT_ERROR;
        Attribute =
            reinterpret_cast<PAttribute>(
                RecordData + Offset);
        if (Attribute->AttributeType ==
            TypeAttributeEndMarker)
        {
            return STATUS_SUCCESS;
        }

        MinimumSize = Attribute->IsNonResident
            ? ((Attribute->Flags &
                (ATTR_COMPRESSION_MASK |
                 ATTR_SPARSE))
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

        if (Attribute->AttributeType == TypeData &&
            (Attribute->Flags & ATTR_SPARSE))
        {
            *ContainsSparse = TRUE;
            return STATUS_SUCCESS;
        }
        Offset += Attribute->Length;
    }

    return STATUS_FILE_CORRUPT_ERROR;
}

static NTSTATUS
WriteLogicalRunBytes(_In_ PVolume DiskVolume,
                     _In_ PDataRun Runs,
                     _In_ ULONGLONG Offset,
                     _In_ PUCHAR Buffer,
                     _In_ ULONG Length)
{
    ULONGLONG ClusterSize;
    ULONGLONG BytesInRun;
    ULONG Written = 0;
    NTSTATUS Status;

    if (!DiskVolume || !Runs ||
        (!Buffer && Length != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Length == 0)
        return STATUS_SUCCESS;
    ClusterSize = BytesPerCluster(DiskVolume);
    if (ClusterSize == 0)
        return STATUS_FILE_CORRUPT_ERROR;

    for (PDataRun Run = Runs;
         Run && Written < Length;
         Run = Run->NextRun)
    {
        if (Run->Length == 0 ||
            Run->Length >
                ~(ULONGLONG)0 /
                    ClusterSize)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        BytesInRun =
            Run->Length * ClusterSize;
        if (Offset >= BytesInRun)
        {
            Offset -= BytesInRun;
            continue;
        }
        if (Run->IsSparse)
            return STATUS_FILE_CORRUPT_ERROR;

        ULONG Chunk = (ULONG)min(
            (ULONGLONG)(Length - Written),
            BytesInRun - Offset);
        Status = DiskVolume->WriteVolume(
            GetOffset(Run->LCN) + Offset,
            Chunk,
            Buffer + Written);
        if (!NT_SUCCESS(Status))
            return Status;
        Written += Chunk;
        Offset = 0;
    }

    return Written == Length
        ? STATUS_SUCCESS
        : STATUS_FILE_CORRUPT_ERROR;
}

static NTSTATUS
ZeroLogicalAllocatedBytes(_In_ PVolume DiskVolume,
                          _In_ PDataRun Runs,
                          _In_ ULONGLONG Offset,
                          _In_ ULONGLONG Length)
{
    PUCHAR ZeroBuffer;
    ULONGLONG ClusterSize;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!DiskVolume || !Runs)
        return STATUS_INVALID_PARAMETER;
    if (Length == 0)
        return STATUS_SUCCESS;
    ClusterSize = BytesPerCluster(DiskVolume);
    if (ClusterSize == 0)
        return STATUS_FILE_CORRUPT_ERROR;

    ZeroBuffer =
        new(PagedPool, TAG_NTFS)
            UCHAR[NTFS_ZERO_CHUNK_SIZE];
    if (!ZeroBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(ZeroBuffer,
                  NTFS_ZERO_CHUNK_SIZE);

    for (PDataRun Run = Runs;
         Run && Length != 0;
         Run = Run->NextRun)
    {
        ULONGLONG BytesInRun;
        ULONGLONG RunBytes;

        if (Run->Length == 0 ||
            Run->Length >
                ~(ULONGLONG)0 / ClusterSize)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            break;
        }
        RunBytes = Run->Length * ClusterSize;
        if (Offset >= RunBytes)
        {
            Offset -= RunBytes;
            continue;
        }

        BytesInRun = min(
            Length,
            RunBytes - Offset);
        if (!Run->IsSparse)
        {
            ULONGLONG RunOffset = Offset;
            ULONGLONG Remaining = BytesInRun;

            while (Remaining != 0)
            {
                ULONG Chunk = (ULONG)min(
                    Remaining,
                    (ULONGLONG)
                        NTFS_ZERO_CHUNK_SIZE);
                Status = DiskVolume->WriteVolume(
                    GetOffset(Run->LCN) +
                        RunOffset,
                    Chunk,
                    ZeroBuffer);
                if (!NT_SUCCESS(Status))
                    goto Done;
                RunOffset += Chunk;
                Remaining -= Chunk;
            }
        }

        Length -= BytesInRun;
        Offset = 0;
    }

    if (Length != 0)
        Status = STATUS_FILE_CORRUPT_ERROR;

Done:
    delete[] ZeroBuffer;
    return Status;
}

static NTSTATUS
GetPackedEaSizeForMaterialization(
    _In_ PFileRecord File,
    _Out_ PUSHORT PackedEaSize)
{
    EAInformationEx Information;
    ULONG Length = 0;
    NTSTATUS Status;

    if (!File || !PackedEaSize)
        return STATUS_INVALID_PARAMETER;
    *PackedEaSize = 0;

    Status = File->ReadExtendedAttributes(
        NULL,
        &Length,
        &Information);
    if (Status == STATUS_NO_EAS_ON_FILE)
        return STATUS_SUCCESS;
    if (Status != STATUS_BUFFER_TOO_SMALL)
        return Status;

    *PackedEaSize = Information.PackedEASize;
    return STATUS_SUCCESS;
}

static NTSTATUS
CombineRunLists(_In_ PDataRun Existing,
                _In_ PDataRun Added,
                _Out_ PDataRun* Combined)
{
    PDataRun Head = NULL;
    PDataRun Tail = NULL;
    NTSTATUS Status;

    *Combined = NULL;
    for (PDataRun Run = Existing; Run; Run = Run->NextRun)
    {
        Status = AppendRunCopy(&Head, &Tail, Run);
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    for (PDataRun Run = Added; Run; Run = Run->NextRun)
    {
        Status = AppendRunCopy(&Head, &Tail, Run);
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
SplitRunList(_In_ PDataRun Existing,
             _In_ ULONGLONG RetainedClusters,
             _Out_ PDataRun* Retained,
             _Out_ PDataRun* Released)
{
    PDataRun RetainedHead = NULL;
    PDataRun RetainedTail = NULL;
    PDataRun ReleasedHead = NULL;
    PDataRun ReleasedTail = NULL;
    ULONGLONG Remaining = RetainedClusters;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!Existing || !Retained || !Released)
        return STATUS_INVALID_PARAMETER;
    *Retained = NULL;
    *Released = NULL;

    for (PDataRun Run = Existing; Run; Run = Run->NextRun)
    {
        DataRun Part = {};
        ULONGLONG Keep;

        if (Run->Length == 0 ||
            (!Run->IsSparse &&
             Run->LCN >
                ~(ULONGLONG)0 - Run->Length))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        Keep = Remaining < Run->Length
            ? Remaining
            : Run->Length;
        if (Keep != 0)
        {
            Status = AppendLogicalRun(
                &RetainedHead,
                &RetainedTail,
                Run->IsSparse,
                Run->LCN,
                Keep);
            if (!NT_SUCCESS(Status))
                goto Done;
            Remaining -= Keep;
        }
        if (Keep != Run->Length &&
            !Run->IsSparse)
        {
            Part.LCN = Run->LCN + Keep;
            Part.Length = Run->Length - Keep;
            Status = AppendRunCopy(&ReleasedHead,
                                   &ReleasedTail,
                                   &Part);
            if (!NT_SUCCESS(Status))
                goto Done;
        }
    }

    if (Remaining != 0)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    *Retained = RetainedHead;
    *Released = ReleasedHead;
    RetainedHead = NULL;
    ReleasedHead = NULL;

Done:
    FreeDataRun(RetainedHead);
    FreeDataRun(ReleasedHead);
    return Status;
}

static NTSTATUS
GetRunBytes(_In_ PVolume DiskVolume,
            _In_ PDataRun Runs,
            _Out_ PULONGLONG ByteCount)
{
    ULONGLONG ClusterSize = BytesPerCluster(DiskVolume);
    ULONGLONG Clusters = 0;

    if (!DiskVolume || !Runs || !ByteCount ||
        ClusterSize == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (PDataRun Run = Runs; Run; Run = Run->NextRun)
    {
        if (Run->IsSparse ||
            Run->Length == 0 ||
            Clusters > ~(ULONGLONG)0 - Run->Length)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        Clusters += Run->Length;
    }
    if (Clusters > ~(ULONGLONG)0 / ClusterSize)
        return STATUS_FILE_TOO_LARGE;

    *ByteCount = Clusters * ClusterSize;
    return STATUS_SUCCESS;
}

static NTSTATUS
WriteRunBytes(_In_ PVolume DiskVolume,
              _In_ PDataRun Runs,
              _In_ ULONGLONG Offset,
              _In_ PUCHAR Buffer,
              _In_ ULONG Length)
{
    ULONGLONG TotalBytes;
    ULONGLONG BytesInRun;
    ULONG Written = 0;
    NTSTATUS Status;

    if (Length != 0 && !Buffer)
        return STATUS_INVALID_PARAMETER;
    if (Length == 0)
        return STATUS_SUCCESS;

    Status = GetRunBytes(DiskVolume, Runs, &TotalBytes);
    if (!NT_SUCCESS(Status))
        return Status;
    if (Offset > TotalBytes ||
        Length > TotalBytes - Offset)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    for (PDataRun Run = Runs;
         Run && Written < Length;
         Run = Run->NextRun)
    {
        BytesInRun =
            Run->Length * BytesPerCluster(DiskVolume);
        if (Offset >= BytesInRun)
        {
            Offset -= BytesInRun;
            continue;
        }

        ULONG Chunk = (ULONG)min(
            (ULONGLONG)(Length - Written),
            BytesInRun - Offset);
        Status = DiskVolume->WriteVolume(
            GetOffset(Run->LCN) + Offset,
            Chunk,
            Buffer + Written);
        if (!NT_SUCCESS(Status))
            return Status;
        Written += Chunk;
        Offset = 0;
    }

    return Written == Length
        ? STATUS_SUCCESS
        : STATUS_FILE_CORRUPT_ERROR;
}

static NTSTATUS
ZeroRunBytes(_In_ PVolume DiskVolume,
             _In_ PDataRun Runs,
             _In_ ULONGLONG Offset,
             _In_ ULONGLONG Length)
{
    PUCHAR ZeroBuffer;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Length == 0)
        return STATUS_SUCCESS;

    ZeroBuffer =
        new(PagedPool, TAG_NTFS) UCHAR[NTFS_ZERO_CHUNK_SIZE];
    if (!ZeroBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(ZeroBuffer, NTFS_ZERO_CHUNK_SIZE);

    while (Length != 0)
    {
        ULONG Chunk = (ULONG)min(
            Length,
            (ULONGLONG)NTFS_ZERO_CHUNK_SIZE);

        Status = WriteRunBytes(DiskVolume,
                               Runs,
                               Offset,
                               ZeroBuffer,
                               Chunk);
        if (!NT_SUCCESS(Status))
            break;
        Offset += Chunk;
        Length -= Chunk;
    }

    delete[] ZeroBuffer;
    return Status;
}

static ULONGLONG
PreferredLCNAfterRuns(_In_ PDataRun Runs,
                      _In_ ULONGLONG ClustersInVolume)
{
    PDataRun Tail = Runs;

    while (Tail && Tail->NextRun)
        Tail = Tail->NextRun;
    if (!Tail ||
        Tail->IsSparse ||
        Tail->LCN >
            ~(ULONGLONG)0 - Tail->Length ||
        Tail->LCN + Tail->Length >= ClustersInVolume)
    {
        return 0;
    }
    return Tail->LCN + Tail->Length;
}

NTSTATUS
FileRecord::CreateNamedDataStream(
    _In_ PWSTR StreamName,
    _In_ PUCHAR Buffer,
    _Inout_ PULONG Length,
    _Inout_ PLARGE_INTEGER Offset)
{
    PAttribute TargetAttribute = NULL;
    PUCHAR RecordBackup;
    ULONGLONG EndOffset;
    ULONG RequestedLength;
    ULONG WorkingLength;
    BOOLEAN Inserted = FALSE;
    NTSTATUS Status;

    if (!StreamName || StreamName[0] == 0 ||
        !Buffer || !Length || !Offset ||
        *Length == 0 || !Header)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (Header->BaseFileRecord != 0)
    {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (FindAttributeInRecord(TypeAttributeList,
                              NULL,
                              NULL))
    {
        return CreateNamedDataStreamInExtension(
            StreamName,
            Buffer,
            Length,
            Offset);
    }

    if (IsOffsetEndOfFile(Offset))
        Offset->QuadPart = 0;
    else if (Offset->QuadPart < 0)
        return STATUS_INVALID_PARAMETER;

    RequestedLength = *Length;
    if ((ULONGLONG)Offset->QuadPart >
        ~(ULONGLONG)0 - RequestedLength)
    {
        return STATUS_FILE_TOO_LARGE;
    }
    EndOffset =
        (ULONGLONG)Offset->QuadPart +
        RequestedLength;

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!RecordBackup)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(RecordBackup,
                  Data,
                  RecordBufferSize);

    Status = InsertResidentAttribute(TypeData,
                                     StreamName,
                                     &TargetAttribute);
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        RtlCopyMemory(Data,
                      RecordBackup,
                      RecordBufferSize);
        Header =
            reinterpret_cast<PFileRecordHeader>(
                Data);
        ClearDataRunCache();
        delete[] RecordBackup;
        RecordBackup = NULL;

        Status = CreateInitialAttributeList();
        if (NT_SUCCESS(Status))
        {
            return CreateNamedDataStreamInExtension(
                StreamName,
                Buffer,
                Length,
                Offset);
        }
        return Status;
    }
    if (!NT_SUCCESS(Status))
        goto Done;
    Inserted = TRUE;

    WorkingLength = RequestedLength;
    Status = UpdateResidentData(
        TargetAttribute,
        Buffer,
        &WorkingLength,
        (ULONGLONG)Offset->QuadPart);
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        Status = PromoteResidentData(
            TargetAttribute,
            Buffer,
            RequestedLength,
            (ULONGLONG)Offset->QuadPart,
            EndOffset,
            EndOffset);
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            RtlCopyMemory(Data,
                          RecordBackup,
                          RecordBufferSize);
            Header =
                reinterpret_cast<PFileRecordHeader>(
                    Data);
            ClearDataRunCache();
            delete[] RecordBackup;
            RecordBackup = NULL;
            Inserted = FALSE;

            Status = CreateInitialAttributeList();
            if (NT_SUCCESS(Status))
            {
                return CreateNamedDataStreamInExtension(
                    StreamName,
                    Buffer,
                    Length,
                    Offset);
            }
            return Status;
        }
    }
    else if (NT_SUCCESS(Status))
    {
        Status = PrepareAutomaticTimestamps(
            NTFS_BASIC_INFO_LAST_WRITE_TIME |
            NTFS_BASIC_INFO_CHANGE_TIME,
            NULL);
        if (NT_SUCCESS(Status))
        {
            Status =
                DiskVolume->MFT->WriteFileRecordToMFT(
                    this);
        }
    }

    if (NT_SUCCESS(Status))
        *Length = RequestedLength;

Done:
    if (!NT_SUCCESS(Status) && Inserted)
    {
        RtlCopyMemory(Data,
                      RecordBackup,
                      RecordBufferSize);
        Header =
            reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();
    }
    delete[] RecordBackup;
    return Status;
}

NTSTATUS
FileRecord::CreateNamedDataStreamInExtension(
    _In_ PWSTR StreamName,
    _In_ PUCHAR Buffer,
    _Inout_ PULONG Length,
    _Inout_ PLARGE_INTEGER Offset)
{
    PFileRecord Extension = NULL;
    PAttribute TargetAttribute = NULL;
    PDataRun AllocatedRuns = NULL;
    ULONGLONG BaseFileReference;
    ULONGLONG EndOffset;
    ULONG RequestedLength;
    ULONG WorkingLength;
    NTSTATUS CleanupStatus;
    NTSTATUS Status;

    if (!StreamName || StreamName[0] == 0 ||
        !Buffer || !Length || !Offset ||
        *Length == 0 || !Header ||
        Header->BaseFileRecord != 0 ||
        !FindAttributeInRecord(
            TypeAttributeList,
            NULL,
            NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (IsOffsetEndOfFile(Offset))
        Offset->QuadPart = 0;
    else if (Offset->QuadPart < 0)
        return STATUS_INVALID_PARAMETER;

    RequestedLength = *Length;
    if ((ULONGLONG)Offset->QuadPart >
        ~(ULONGLONG)0 - RequestedLength)
    {
        return STATUS_FILE_TOO_LARGE;
    }
    EndOffset =
        (ULONGLONG)Offset->QuadPart +
        RequestedLength;
    BaseFileReference =
        ((ULONGLONG)Header->SequenceNumber << 48) |
        Header->MFTRecordNumber;

    Status = DiskVolume->MFT->
        AllocateExtensionFileRecord(
            BaseFileReference,
            &Extension);
    if (!NT_SUCCESS(Status))
        return Status;

    /*
     * Extension records have no $STANDARD_INFORMATION of their own. File-wide
     * timestamps are committed with the base record when its attribute-list
     * entry becomes visible.
     */
    Status = Extension->
        SetAutomaticTimestampMask(0);
    if (!NT_SUCCESS(Status))
        goto Rollback;
    Status = Extension->InsertResidentAttribute(
        TypeData,
        StreamName,
        &TargetAttribute);
    if (!NT_SUCCESS(Status))
        goto Rollback;

    WorkingLength = RequestedLength;
    Status = Extension->UpdateResidentData(
        TargetAttribute,
        Buffer,
        &WorkingLength,
        (ULONGLONG)Offset->QuadPart);
    if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        Status = Extension->PromoteResidentData(
            TargetAttribute,
            Buffer,
            RequestedLength,
            (ULONGLONG)Offset->QuadPart,
            EndOffset,
            EndOffset);
        if (!NT_SUCCESS(Status))
            goto Rollback;
        TargetAttribute =
            Extension->FindAttributeInRecord(
                TypeData,
                StreamName,
                NULL);
        if (!TargetAttribute ||
            !TargetAttribute->IsNonResident)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Rollback;
        }
        AllocatedRuns =
            Extension->FindNonResidentData(
                TargetAttribute);
        if (!AllocatedRuns)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Rollback;
        }
    }
    else if (NT_SUCCESS(Status))
    {
        Status = DiskVolume->MFT->
            WriteFileRecordToMFT(Extension);
        if (!NT_SUCCESS(Status))
            goto Rollback;
    }
    else
    {
        goto Rollback;
    }

    Status = InsertAttributeListEntry(
        TargetAttribute,
        Extension,
        NTFS_BASIC_INFO_LAST_WRITE_TIME |
        NTFS_BASIC_INFO_CHANGE_TIME);
    if (!NT_SUCCESS(Status))
        goto Rollback;

    *Length = RequestedLength;
    FreeDataRun(AllocatedRuns);
    delete Extension;
    return STATUS_SUCCESS;

Rollback:
    CleanupStatus =
        DiskVolume->MFT->
            DeallocateExtensionFileRecord(
                Extension);
    if (AllocatedRuns &&
        NT_SUCCESS(CleanupStatus))
    {
        NTSTATUS ReleaseStatus =
            DiskVolume->ReleaseClusters(
                AllocatedRuns);
        if (NT_SUCCESS(CleanupStatus) &&
            !NT_SUCCESS(ReleaseStatus))
        {
            CleanupStatus = ReleaseStatus;
        }
    }
    FreeDataRun(AllocatedRuns);
    delete Extension;
    if (NT_SUCCESS(Status))
        Status = CleanupStatus;
    return Status;
}

NTSTATUS
FileRecord::MaterializeWofCompressedData(
    _Out_ PBOOLEAN Materialized)
{
    static WCHAR WofCompressedDataName[] =
        L"WofCompressedData";
    PAttribute LogicalAttribute;
    PAttribute BackingAttribute;
    PAttribute ReparseAttribute;
    PAttribute StandardAttribute;
    PStandardInformationEx Standard;
    PDataRun DecodedRuns = NULL;
    PDataRun OldLogicalRuns = NULL;
    PDataRun OldBackingRuns = NULL;
    PDataRun OldReparseRuns = NULL;
    PDataRun NewRuns = NULL;
    PDataRun MaterializedRuns = NULL;
    PNonResidentMappingUpdate MappingUpdate = NULL;
    PUCHAR Buffer = NULL;
    PUCHAR ResidentData = NULL;
    PUCHAR RecordBackup = NULL;
    ULONGLONG LogicalSize;
    ULONGLONG ClusterSize;
    ULONGLONG RequiredClusters;
    ULONGLONG AllocatedSize = 0;
    ULONGLONG OldRunBytes;
    ULONGLONG Position;
    ULONG MaxRuns;
    ULONG ProbeLength = 0;
    ULONG Remaining;
    ULONG Chunk;
    ULONG FileNameFields;
    USHORT PackedEaSize;
    UCHAR ProbeByte = 0;
    BOOLEAN Handled = FALSE;
    BOOLEAN Committed = FALSE;
    BOOLEAN LogicalHadSparse = FALSE;
    BOOLEAN ReuseLogicalRuns = FALSE;
    NTSTATUS ReleaseStatus;
    NTSTATUS Status;

    if (!Materialized)
        return STATUS_INVALID_PARAMETER;
    *Materialized = FALSE;
    if (!DiskVolume)
        return STATUS_INVALID_DEVICE_STATE;
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;

    LogicalAttribute = GetAttribute(TypeData, NULL);
    if (!LogicalAttribute)
        return STATUS_FILE_CORRUPT_ERROR;

    Status = TryCopyWofCompressedData(
        LogicalAttribute,
        &ProbeByte,
        &ProbeLength,
        0,
        &Handled);
    if (!NT_SUCCESS(Status) || !Handled)
        return Status;

    if ((Header->Flags & FR_IS_DIRECTORY) ||
        FindAttributeInRecord(TypeAttributeList,
                              NULL,
                              NULL))
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    BackingAttribute = GetAttribute(
        TypeData,
        WofCompressedDataName);
    ReparseAttribute = GetAttribute(
        TypeReparsePoint,
        NULL);
    if (!BackingAttribute ||
        !ReparseAttribute ||
        GetAttributeOwner(LogicalAttribute) != this ||
        GetAttributeOwner(BackingAttribute) != this ||
        GetAttributeOwner(ReparseAttribute) != this)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    if ((LogicalAttribute->Flags &
            (ATTR_COMPRESSION_MASK |
             ATTR_ENCRYPTED)) ||
        (!LogicalAttribute->IsNonResident &&
         LogicalAttribute->Flags != 0) ||
        (BackingAttribute->Flags &
            (ATTR_COMPRESSION_MASK |
             ATTR_ENCRYPTED)) ||
        ReparseAttribute->Flags != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        return Status;
    UNREFERENCED_PARAMETER(StandardAttribute);
    if (!(Standard->FilePermissions &
          FILE_PERM_REPARSE_PT))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = GetPackedEaSizeForMaterialization(
        this,
        &PackedEaSize);
    if (!NT_SUCCESS(Status))
        return Status;

    LogicalSize = GetAttributeDataSize(
        LogicalAttribute);
    if (!LogicalAttribute->IsNonResident &&
        LogicalSize > MAXULONG)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (LogicalAttribute->IsNonResident)
    {
        DecodedRuns = FindNonResidentData(
            LogicalAttribute);
        if (!DecodedRuns)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        for (PDataRun Run = DecodedRuns;
             Run;
             Run = Run->NextRun)
        {
            if (Run->IsSparse)
                LogicalHadSparse = TRUE;
        }
        Status = CloneAllocatedRuns(
            DecodedRuns,
            &OldLogicalRuns);
        FreeDataRun(DecodedRuns);
        DecodedRuns = NULL;
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    if (BackingAttribute->IsNonResident)
    {
        DecodedRuns = FindNonResidentData(
            BackingAttribute);
        if (!DecodedRuns)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        Status = CloneAllocatedRuns(
            DecodedRuns,
            &OldBackingRuns);
        FreeDataRun(DecodedRuns);
        DecodedRuns = NULL;
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    if (ReparseAttribute->IsNonResident)
    {
        DecodedRuns = FindNonResidentData(
            ReparseAttribute);
        if (!DecodedRuns)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        Status = CloneAllocatedRuns(
            DecodedRuns,
            &OldReparseRuns);
        FreeDataRun(DecodedRuns);
        DecodedRuns = NULL;
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    if (LogicalAttribute->IsNonResident &&
        LogicalSize != 0)
    {
        ClusterSize = BytesPerCluster(DiskVolume);
        if (ClusterSize == 0 ||
            LogicalSize >
                ~(ULONGLONG)0 - (ClusterSize - 1))
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Done;
        }
        RequiredClusters =
            (LogicalSize + ClusterSize - 1) /
            ClusterSize;
        if (RequiredClusters == 0 ||
            RequiredClusters > MAXULONG)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Done;
        }

        if (!LogicalHadSparse && OldLogicalRuns)
        {
            Status = GetRunBytes(
                DiskVolume,
                OldLogicalRuns,
                &OldRunBytes);
            if (!NT_SUCCESS(Status) ||
                OldRunBytes !=
                    LogicalAttribute->
                        NonResident.AllocatedSize ||
                OldRunBytes <
                    RequiredClusters * ClusterSize)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
            ReuseLogicalRuns = TRUE;
            MaterializedRuns = OldLogicalRuns;
            AllocatedSize = OldRunBytes;
        }
        else
        {
            MaxRuns = RecordBufferSize / 3;
            if (MaxRuns == 0)
                MaxRuns = 1;
            Status = DiskVolume->AllocateClusters(
                0,
                (ULONG)RequiredClusters,
                MaxRuns,
                &NewRuns);
            if (!NT_SUCCESS(Status))
                goto Done;
            MaterializedRuns = NewRuns;
            AllocatedSize =
                RequiredClusters * ClusterSize;
        }

        Buffer =
            new(PagedPool, TAG_NTFS)
                UCHAR[NTFS_ZERO_CHUNK_SIZE];
        if (!Buffer)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }

        Position = 0;
        while (Position < LogicalSize)
        {
            Chunk = (ULONG)min(
                LogicalSize - Position,
                (ULONGLONG)NTFS_ZERO_CHUNK_SIZE);
            Remaining = Chunk;
            Status = CopyData(
                TypeData,
                NULL,
                Buffer,
                &Remaining,
                Position);
            if (!NT_SUCCESS(Status) ||
                Remaining != 0)
            {
                if (NT_SUCCESS(Status))
                    Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
            Status = WriteRunBytes(
                DiskVolume,
                MaterializedRuns,
                Position,
                Buffer,
                Chunk);
            if (!NT_SUCCESS(Status))
                goto Done;
            Position += Chunk;
        }
    }
    else if (LogicalSize != 0)
    {
        ResidentData =
            new(PagedPool, TAG_NTFS)
                UCHAR[(ULONG)LogicalSize];
        if (!ResidentData)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }
        Remaining = (ULONG)LogicalSize;
        Status = CopyData(
            TypeData,
            NULL,
            ResidentData,
            &Remaining,
            0);
        if (!NT_SUCCESS(Status) ||
            Remaining != 0)
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
    }

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!RecordBackup)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(RecordBackup,
                  Data,
                  RecordBufferSize);

    ReparseAttribute = FindAttributeInRecord(
        TypeReparsePoint,
        NULL,
        NULL);
    if (!ReparseAttribute)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Restore;
    }
    Status = RemoveAttributeRecord(
        ReparseAttribute);
    if (!NT_SUCCESS(Status))
        goto Restore;

    BackingAttribute = FindAttributeInRecord(
        TypeData,
        WofCompressedDataName,
        NULL);
    if (!BackingAttribute)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Restore;
    }
    Status = RemoveAttributeRecord(
        BackingAttribute);
    if (!NT_SUCCESS(Status))
        goto Restore;

    LogicalAttribute = FindAttributeInRecord(
        TypeData,
        NULL,
        NULL);
    if (!LogicalAttribute)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Restore;
    }

    LogicalAttribute->Flags = 0;
    if (LogicalAttribute->IsNonResident)
    {
        LogicalAttribute->NonResident.CompressionUnitSize = 0;
        LogicalAttribute->NonResident.Reserved = 0;
        LogicalAttribute->NonResident.CompressedDataSize = 0;
        Status = LogicalSize == 0
            ? ConvertNonResidentToResidentEmpty(
                LogicalAttribute)
            : ReplaceNonResidentMappingPairs(
                &LogicalAttribute,
                MaterializedRuns,
                AllocatedSize,
                LogicalSize,
                LogicalSize,
                &MappingUpdate);
    }
    else
    {
        Status = ReplaceResidentData(
            LogicalAttribute,
            ResidentData,
            (ULONG)LogicalSize);
    }
    if (!NT_SUCCESS(Status))
        goto Restore;

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        goto Restore;
    UNREFERENCED_PARAMETER(StandardAttribute);
    Standard->FilePermissions &=
        ~(FILE_PERM_REPARSE_PT |
          FILE_PERM_SPARSE |
          FILE_PERM_COMPRESSED);
    Standard->FilePermissions |=
        FILE_PERM_ARCHIVE;

    FileNameFields =
        NTFS_FILE_NAME_UPDATE_SIZES |
        NTFS_FILE_NAME_UPDATE_EA_SIZE |
        NTFS_FILE_NAME_UPDATE_ARCHIVE |
        NTFS_FILE_NAME_UPDATE_REPARSE_TAG |
        NTFS_FILE_NAME_UPDATE_STORAGE_FLAGS;
    Status = SynchronizeFileNameInformation(
        FileNameFields,
        AllocatedSize,
        LogicalSize,
        PackedEaSize,
        0,
        0);
    if (!NT_SUCCESS(Status))
        goto Restore;

    Status = PrepareAutomaticTimestamps(
        NTFS_BASIC_INFO_CHANGE_TIME,
        NULL);
    if (!NT_SUCCESS(Status))
        goto Restore;

    Status = MappingUpdate
        ? CommitNonResidentMappingUpdate(
            &MappingUpdate)
        : DiskVolume->MFT->
            WriteFileRecordToMFT(this);
    if (!NT_SUCCESS(Status))
        goto Restore;

    Committed = TRUE;
    *Materialized = TRUE;
    InvalidateWofCompression();

    ReleaseStatus = STATUS_SUCCESS;
    if (OldLogicalRuns &&
        !ReuseLogicalRuns)
    {
        Status = DiskVolume->ReleaseClusters(
            OldLogicalRuns);
        if (!NT_SUCCESS(Status))
            ReleaseStatus = Status;
    }
    if (OldBackingRuns)
    {
        Status = DiskVolume->ReleaseClusters(
            OldBackingRuns);
        if (!NT_SUCCESS(Status) &&
            NT_SUCCESS(ReleaseStatus))
        {
            ReleaseStatus = Status;
        }
    }
    if (OldReparseRuns)
    {
        Status = DiskVolume->ReleaseClusters(
            OldReparseRuns);
        if (!NT_SUCCESS(Status) &&
            NT_SUCCESS(ReleaseStatus))
        {
            ReleaseStatus = Status;
        }
    }
    Status = ReleaseStatus;
    goto Done;

Restore:
    if (!Committed)
    {
        AbortNonResidentMappingUpdate(
            &MappingUpdate);
        RtlCopyMemory(Data,
                      RecordBackup,
                      RecordBufferSize);
        Header =
            reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();
    }

Done:
    if (!Committed && NewRuns)
        (void)DiskVolume->ReleaseClusters(NewRuns);
    FreeDataRun(DecodedRuns);
    FreeDataRun(OldLogicalRuns);
    FreeDataRun(OldBackingRuns);
    FreeDataRun(OldReparseRuns);
    FreeDataRun(NewRuns);
    delete[] RecordBackup;
    delete[] ResidentData;
    delete[] Buffer;
    return Status;
}

NTSTATUS
FileRecord::DeleteExternalBacking()
{
    BOOLEAN Materialized;
    NTSTATUS Status =
        MaterializeWofCompressedData(
            &Materialized);

    if (NT_SUCCESS(Status) && !Materialized)
        return STATUS_OBJECT_NOT_EXTERNALLY_BACKED;
    return Status;
}

/*
 * Creates the base record's initial $ATTRIBUTE_LIST so a nonresident
 * stream's mapping pairs can spill into extension records, then re-resolves
 * the caller's attribute pointer and owner (both move once the list is
 * published). Created reports whether a list was actually created; a base
 * record with no movable attribute reports success with Created FALSE so
 * the caller keeps its original mapping-growth failure.
 */
NTSTATUS
FileRecord::EnsureAttributeListForMappingGrowth(
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _Inout_ PAttribute* TargetAttribute,
    _Inout_ FileRecord** AttributeOwner,
    _Out_ PBOOLEAN Created)
{
    NTSTATUS Status;

    *Created = FALSE;
    if (*AttributeOwner != this ||
        Header->BaseFileRecord != 0 ||
        Header->MFTRecordNumber <=
            NTFS_LAST_RESERVED_FILE_RECORD ||
        GetAttribute(TypeAttributeList, NULL))
    {
        return STATUS_SUCCESS;
    }

    Status = CreateInitialAttributeList();
    if (Status == STATUS_BUFFER_TOO_SMALL)
        return STATUS_SUCCESS;
    if (!NT_SUCCESS(Status))
        return Status;

    *TargetAttribute = GetAttribute(AttrType, StreamName);
    if (!*TargetAttribute)
        return STATUS_NOT_FOUND;
    *AttributeOwner = GetAttributeOwner(*TargetAttribute);
    if (!*AttributeOwner)
        return STATUS_FILE_CORRUPT_ERROR;
    *Created = TRUE;
    return STATUS_SUCCESS;
}

NTSTATUS
FileRecord::WriteFileData(_In_     AttributeType AttrType,
                          _In_opt_ PWSTR StreamName,
                          _In_     PUCHAR Buffer,
                          _Inout_  PULONG Length,
                          _In_     PLARGE_INTEGER Offset)
{
    NTSTATUS Status;
    PAttribute TargetAttribute;
    PFileRecord AttributeOwner;
    PUCHAR RecordBackup = NULL;
    ULONGLONG EndOffset;
    ULONGLONG OldDataSize;
    ULONGLONG OldInitializedSize;
    BOOLEAN MetadataChanged;
    BOOLEAN TimestampUpdate;
    UINT32 TimestampFields;
    ULONG OldResidentDataLength;
    ULONG RequestedLength;

    if (!Length || !Offset ||
        (!Buffer && *Length != 0))
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!DiskVolume || DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;

    // If we aren't writing anything, don't write anything.
    if (*Length == 0)
        return STATUS_SUCCESS;
    RequestedLength = *Length;

    if (AttrType == TypeData &&
        (!StreamName || StreamName[0] == 0))
    {
        BOOLEAN Materialized;

        Status = MaterializeWofCompressedData(
            &Materialized);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    // Get the target attribute
    TargetAttribute = GetAttribute(AttrType, StreamName);
    if (!TargetAttribute)
    {
        /*
         * A named $DATA attribute can be created in this base record or, when
         * an $ATTRIBUTE_LIST already exists, in a newly allocated extension
         * record. Creating the initial list for an already-full base record
         * remains a separate multi-record transformation.
         */
        if (AttrType == TypeData &&
            StreamName &&
            StreamName[0] != 0)
        {
            return CreateNamedDataStream(StreamName,
                                         Buffer,
                                         Length,
                                         Offset);
        }
        return STATUS_NOT_FOUND;
    }
    AttributeOwner = GetAttributeOwner(TargetAttribute);
    if (!AttributeOwner)
        return STATUS_FILE_CORRUPT_ERROR;
    TimestampFields =
        AutomaticTimestampFieldsForAttribute(
            AttrType,
            TRUE);

    // Update the offset, if needed.
    if (IsOffsetEndOfFile(Offset))
        Offset->QuadPart = GetAttributeDataSize(TargetAttribute);
    else if (Offset->QuadPart < 0)
        return STATUS_INVALID_PARAMETER;
    if ((ULONGLONG)Offset->QuadPart >
        ~(ULONGLONG)0 - RequestedLength)
    {
        return STATUS_FILE_TOO_LARGE;
    }
    EndOffset =
        (ULONGLONG)Offset->QuadPart +
        RequestedLength;

    if (!(TargetAttribute->IsNonResident))
    {
        RecordBackup =
            new(PagedPool, TAG_FILE_RECORD)
                UCHAR[AttributeOwner->RecordBufferSize];
        if (!RecordBackup)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlCopyMemory(RecordBackup,
                      AttributeOwner->Data,
                      AttributeOwner->RecordBufferSize);
        OldResidentDataLength =
            TargetAttribute->Resident.DataLength;

        Status = AttributeOwner->UpdateResidentData(
            TargetAttribute,
            Buffer,
            Length,
            Offset->QuadPart);

        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            RtlCopyMemory(AttributeOwner->Data,
                          RecordBackup,
                          AttributeOwner->RecordBufferSize);
            AttributeOwner->Header =
                reinterpret_cast<PFileRecordHeader>(
                    AttributeOwner->Data);
            delete[] RecordBackup;
            RecordBackup = NULL;

            if ((AttrType != TypeData &&
                 AttrType != TypeEA &&
                 AttrType != TypeBitmap &&
                 AttrType != TypeAttributeList) ||
                (AttributeOwner != this &&
                 (AttrType != TypeData ||
                  TargetAttribute->NameLength == 0)))
            {
                return STATUS_BUFFER_TOO_SMALL;
            }

            Status = AttributeOwner->PromoteResidentData(
                TargetAttribute,
                Buffer,
                RequestedLength,
                (ULONGLONG)Offset->QuadPart,
                EndOffset > OldResidentDataLength
                    ? EndOffset
                    : OldResidentDataLength,
                EndOffset > OldResidentDataLength
                    ? EndOffset
                    : OldResidentDataLength);
            if (NT_SUCCESS(Status))
            {
                *Length = RequestedLength;
                if (AttributeOwner != this &&
                    TimestampFields != 0)
                {
                    Status = UpdateAutomaticTimestamps(
                        TimestampFields);
                }
            }
            return Status;
        }

        else if (!NT_SUCCESS(Status))
        {
            DPRINT1("Unable to write file data!\n");
            goto RestoreResident;
        }

        if (AttrType == TypeData &&
            TargetAttribute->NameLength == 0 &&
            TargetAttribute->Resident.DataLength !=
                OldResidentDataLength)
        {
            Status = AttributeOwner->SynchronizeFileNameSizes(
                0,
                TargetAttribute->Resident.DataLength);
            if (!NT_SUCCESS(Status))
                goto RestoreResident;
        }

        if (TimestampFields != 0 &&
            AttributeOwner == this)
        {
            Status =
                PrepareAutomaticTimestamps(
                    TimestampFields,
                    NULL);
            if (!NT_SUCCESS(Status))
                goto RestoreResident;
        }

        // Write the file record to disk
        Status = DiskVolume->MFT->WriteFileRecordToMFT(
            AttributeOwner);
        if (!NT_SUCCESS(Status))
        {
RestoreResident:
            RtlCopyMemory(AttributeOwner->Data,
                          RecordBackup,
                          AttributeOwner->RecordBufferSize);
            AttributeOwner->Header =
                reinterpret_cast<PFileRecordHeader>(
                    AttributeOwner->Data);
            AttributeOwner->ClearDataRunCache();
        }
        delete[] RecordBackup;
        if (NT_SUCCESS(Status) &&
            AttributeOwner != this &&
            TimestampFields != 0)
        {
            /*
             * An $ATTRIBUTE_LIST extension owns the stream value, but the
             * file-wide $STANDARD_INFORMATION timestamps remain in the base
             * record. Commit the owner first, then maintain the base record
             * through the same automatic-timestamp primitive used by normal
             * writes.
             */
            Status = UpdateAutomaticTimestamps(
                TimestampFields);
        }
    }

    else
    {
        // Attribute data is nonresident.

        /*
         * Sparse streams have virtual allocation and physical allocation
         * sizes with different meanings. Allocate only the sparse clusters
         * intersected by this write and preserve all untouched holes.
         */
        BOOLEAN SparseFile =
            !!(TargetAttribute->Flags & ATTR_SPARSE);

        if (AttrType == TypeData &&
            (TargetAttribute->Flags &
             ATTR_COMPRESSION_MASK) != 0)
        {
            Status = WriteCompressedData(
                TargetAttribute,
                Buffer,
                Length,
                (ULONGLONG)Offset->QuadPart);
            if (NT_SUCCESS(Status))
                *Length = RequestedLength;
            return Status;
        }

        if (!SparseFile &&
            AttrType == TypeData &&
            (TargetAttribute->Flags &
             (ATTR_COMPRESSION_MASK |
              ATTR_ENCRYPTED)) == 0)
        {
            PAttribute StandardAttribute;
            PStandardInformationEx Standard;

            Status = GetStandardInformationForUpdate(
                &StandardAttribute,
                &Standard);
            if (!NT_SUCCESS(Status))
                return Status;
            UNREFERENCED_PARAMETER(StandardAttribute);
            SparseFile =
                !!(Standard->FilePermissions &
                   FILE_PERM_SPARSE);
        }
        if (SparseFile)
        {
            Status = WriteSparseData(
                TargetAttribute,
                Buffer,
                Length,
                (ULONGLONG)Offset->QuadPart);
            if (NT_SUCCESS(Status))
                *Length = RequestedLength;
            return Status;
        }

        /*
         * Nonzero shrinking retains the nonresident form, matching normal
         * NTFS behavior. Explicit truncation to zero removes the empty run
         * list and returns the attribute to resident form.
         */

        if (EndOffset >
            TargetAttribute->NonResident.AllocatedSize)
        {
            /*
             * Mapping-pair growth can outgrow the base record. Once an
             * $ATTRIBUTE_LIST exists the update path relocates crowded
             * segments into extension records, but the list itself needs
             * about 0x50 free bytes here, so it is created proactively
             * while the record still has headroom. A reactive retry backs
             * up a first failure inside the remaining window.
             */
            BOOLEAN ListCreated;

            if (AttributeOwner == this &&
                Header->AllocatedSize >=
                    Header->ActualSize &&
                Header->AllocatedSize -
                    Header->ActualSize < 0x100)
            {
                Status =
                    EnsureAttributeListForMappingGrowth(
                        AttrType,
                        StreamName,
                        &TargetAttribute,
                        &AttributeOwner,
                        &ListCreated);
                if (!NT_SUCCESS(Status))
                    return Status;
            }

            Status = AttributeOwner->
                ExtendNonResidentData(
                TargetAttribute,
                Buffer,
                RequestedLength,
                (ULONGLONG)Offset->QuadPart);
            if (Status == STATUS_BUFFER_TOO_SMALL)
            {
                Status =
                    EnsureAttributeListForMappingGrowth(
                        AttrType,
                        StreamName,
                        &TargetAttribute,
                        &AttributeOwner,
                        &ListCreated);
                if (NT_SUCCESS(Status) && ListCreated)
                {
                    Status = AttributeOwner->
                        ExtendNonResidentData(
                        TargetAttribute,
                        Buffer,
                        RequestedLength,
                        (ULONGLONG)
                            Offset->QuadPart);
                }
                else if (NT_SUCCESS(Status))
                {
                    Status =
                        STATUS_BUFFER_TOO_SMALL;
                }
            }
            if (NT_SUCCESS(Status))
            {
                *Length = RequestedLength;
                if (AttributeOwner != this &&
                    TimestampFields != 0)
                {
                    Status = UpdateAutomaticTimestamps(
                        TimestampFields);
                }
            }
            return Status;
        }

        OldDataSize =
            TargetAttribute->NonResident.DataSize;
        OldInitializedSize =
            TargetAttribute->NonResident.InitalizedDataSize;
        MetadataChanged =
            EndOffset > OldDataSize ||
            EndOffset > OldInitializedSize;
        TimestampUpdate =
            TimestampFields != 0 &&
            AttributeOwner == this &&
            HasAutomaticTimestampUpdate(
                TimestampFields);
        if (MetadataChanged || TimestampUpdate)
        {
            RecordBackup =
                new(PagedPool, TAG_FILE_RECORD)
                    UCHAR[AttributeOwner->RecordBufferSize];
            if (!RecordBackup)
                return STATUS_INSUFFICIENT_RESOURCES;
            RtlCopyMemory(RecordBackup,
                          AttributeOwner->Data,
                          AttributeOwner->RecordBufferSize);
        }

        Status = UpdateNonResidentData(TargetAttribute,
                                       Buffer,
                                       Length,
                                       Offset->QuadPart);

        if (!NT_SUCCESS(Status))
        {
            delete[] RecordBackup;
            return Status;
        }

        if (!MetadataChanged &&
            (TargetAttribute->NonResident.DataSize !=
                 OldDataSize ||
             TargetAttribute->NonResident.InitalizedDataSize !=
                 OldInitializedSize))
        {
            /*
             * UpdateNonResidentData must not change sizes unless the
             * precomputed end offset said metadata would change.
             */
            TargetAttribute->NonResident.DataSize = OldDataSize;
            TargetAttribute->NonResident.InitalizedDataSize =
                OldInitializedSize;
            delete[] RecordBackup;
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (MetadataChanged &&
            AttrType == TypeData &&
            TargetAttribute->NameLength == 0)
        {
            Status =
                AttributeOwner->SynchronizeFileNameSizes(
                    TargetAttribute->NonResident.AllocatedSize,
                    TargetAttribute->NonResident.DataSize);
        }

        if (NT_SUCCESS(Status) && TimestampUpdate)
        {
            Status =
                PrepareAutomaticTimestamps(
                    TimestampFields,
                    NULL);
        }
        if (NT_SUCCESS(Status) &&
            (MetadataChanged || TimestampUpdate))
        {
            Status =
                DiskVolume->MFT->WriteFileRecordToMFT(
                    AttributeOwner);
        }

        if (MetadataChanged || TimestampUpdate)
        {
            if (!NT_SUCCESS(Status))
            {
                RtlCopyMemory(
                    AttributeOwner->Data,
                    RecordBackup,
                    AttributeOwner->RecordBufferSize);
                AttributeOwner->Header =
                    reinterpret_cast<PFileRecordHeader>(
                        AttributeOwner->Data);
                AttributeOwner->ClearDataRunCache();
            }
            delete[] RecordBackup;
            if (!NT_SUCCESS(Status))
                return Status;
        }
        if (AttributeOwner != this &&
            TimestampFields != 0)
        {
            Status = UpdateAutomaticTimestamps(
                TimestampFields);
            if (!NT_SUCCESS(Status))
                return Status;
        }
    }

    *Length = RequestedLength;
    return Status;
}

NTSTATUS
FileRecord::SetFileDataSize(_In_ AttributeType AttrType,
                            _In_opt_ PWSTR StreamName,
                            _In_ ULONGLONG NewSize)
{
    PAttribute StandardAttribute;
    PAttribute TargetAttribute;
    PFileRecord AttributeOwner;
    PStandardInformationEx Standard;
    PUCHAR RecordBackup;
    UINT32 TimestampFields;
    ULONG OldDataLength;
    BOOLEAN HasAttributeList;
    BOOLEAN SparseFile;
    NTSTATUS Status;

    if (!DiskVolume)
        return STATUS_INVALID_DEVICE_STATE;
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;
    if (AttrType != TypeData &&
        AttrType != TypeEA)
        return STATUS_INVALID_PARAMETER;

    if (AttrType == TypeData &&
        (!StreamName || StreamName[0] == 0))
    {
        BOOLEAN Materialized;

        Status = MaterializeWofCompressedData(
            &Materialized);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    TimestampFields =
        AutomaticTimestampFieldsForAttribute(
            AttrType,
            TRUE);

    TargetAttribute = GetAttribute(AttrType, StreamName);
    if (!TargetAttribute)
        return STATUS_NOT_FOUND;
    AttributeOwner = GetAttributeOwner(TargetAttribute);
    if (!AttributeOwner)
        return STATUS_FILE_CORRUPT_ERROR;
    HasAttributeList =
        FindAttributeInRecord(TypeAttributeList,
                              NULL,
                              NULL) != NULL;
    if (TargetAttribute->AttributeType != AttrType ||
        (TargetAttribute->Flags & ~ATTR_SPARSE) != 0 ||
        (TargetAttribute->Flags == ATTR_SPARSE &&
         AttrType != TypeData))
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    if (TargetAttribute->IsNonResident)
    {
        if (AttributeOwner != this &&
             (AttrType != TypeData ||
              TargetAttribute->NameLength == 0))
        {
            return STATUS_NOT_IMPLEMENTED;
        }
        SparseFile =
            TargetAttribute->Flags == ATTR_SPARSE;
        if (!SparseFile && AttrType == TypeData)
        {
            Status = GetStandardInformationForUpdate(
                &StandardAttribute,
                &Standard);
            if (!NT_SUCCESS(Status))
                return Status;
            UNREFERENCED_PARAMETER(StandardAttribute);
            SparseFile =
                !!(Standard->FilePermissions &
                   FILE_PERM_SPARSE);
        }
        if (TargetAttribute->NonResident.DataSize ==
            NewSize)
        {
            return STATUS_SUCCESS;
        }
        if (SparseFile)
        {
            return ResizeSparseData(TargetAttribute,
                                    NewSize);
        }
        Status = AttributeOwner->
            ResizeNonResidentData(TargetAttribute,
                                  NewSize);
        if (NT_SUCCESS(Status) &&
            AttributeOwner != this)
        {
            Status = UpdateAutomaticTimestamps(
                TimestampFields);
        }
        return Status;
    }

    /*
     * A named resident stream may live in an extension record. Its value can
     * be resized or promoted there without changing the attribute-list entry;
     * the list identifies the owning attribute but does not duplicate its
     * resident form or logical size.
     */
    if (HasAttributeList &&
        (AttrType != TypeData ||
         TargetAttribute->NameLength == 0))
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    Status = AttributeOwner->
        ValidateResidentAttributeForUpdate(
        TargetAttribute,
        &OldDataLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (NewSize == OldDataLength)
        return STATUS_SUCCESS;

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[AttributeOwner->RecordBufferSize];
    if (!RecordBackup)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(RecordBackup,
                  AttributeOwner->Data,
                  AttributeOwner->RecordBufferSize);

    Status = NewSize <= MAXULONG
        ? AttributeOwner->ResizeResidentData(
              TargetAttribute,
              (ULONG)NewSize)
        : STATUS_BUFFER_TOO_SMALL;
    if (Status == STATUS_BUFFER_TOO_SMALL &&
        NewSize > OldDataLength)
    {
        RtlCopyMemory(AttributeOwner->Data,
                      RecordBackup,
                      AttributeOwner->RecordBufferSize);
        AttributeOwner->Header =
            reinterpret_cast<PFileRecordHeader>(
                AttributeOwner->Data);
        AttributeOwner->ClearDataRunCache();
        delete[] RecordBackup;
        Status = AttributeOwner->PromoteResidentData(
            TargetAttribute,
            NULL,
            0,
            0,
            NewSize,
            NewSize);
        if (NT_SUCCESS(Status) &&
            AttributeOwner != this)
        {
            Status = UpdateAutomaticTimestamps(
                TimestampFields);
        }
        return Status;
    }
    if (!NT_SUCCESS(Status))
        goto RestoreResident;

    if (AttrType == TypeData &&
        TargetAttribute->NameLength == 0)
    {
        Status = SynchronizeFileNameSizes(
            0,
            NewSize);
        if (!NT_SUCCESS(Status))
            goto RestoreResident;
    }

    if (AttributeOwner == this)
    {
        Status = PrepareAutomaticTimestamps(
            TimestampFields,
            NULL);
        if (!NT_SUCCESS(Status))
            goto RestoreResident;
    }

    Status = DiskVolume->MFT->WriteFileRecordToMFT(
        AttributeOwner);
    if (NT_SUCCESS(Status))
    {
        delete[] RecordBackup;
        if (AttributeOwner != this)
        {
            Status = UpdateAutomaticTimestamps(
                TimestampFields);
        }
        if (!NT_SUCCESS(Status))
            return Status;
        return STATUS_SUCCESS;
    }

RestoreResident:
    RtlCopyMemory(AttributeOwner->Data,
                  RecordBackup,
                  AttributeOwner->RecordBufferSize);
    AttributeOwner->Header =
        reinterpret_cast<PFileRecordHeader>(
            AttributeOwner->Data);
    AttributeOwner->ClearDataRunCache();
    delete[] RecordBackup;
    return Status;
}

NTSTATUS
FileRecord::SetFileAllocationSize(
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ ULONGLONG NewAllocationSize)
{
    PAttribute StandardAttribute;
    PAttribute TargetAttribute;
    PFileRecord AttributeOwner;
    PStandardInformationEx Standard;
    ULONG ResidentDataLength;
    NTSTATUS Status;

    if (!DiskVolume)
        return STATUS_INVALID_DEVICE_STATE;
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;
    if (AttrType != TypeData)
        return STATUS_INVALID_PARAMETER;

    if (!StreamName || StreamName[0] == 0)
    {
        BOOLEAN Materialized;

        Status = MaterializeWofCompressedData(
            &Materialized);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    TargetAttribute = GetAttribute(AttrType, StreamName);
    if (!TargetAttribute)
        return STATUS_NOT_FOUND;
    AttributeOwner = GetAttributeOwner(TargetAttribute);
    if (!AttributeOwner)
        return STATUS_FILE_CORRUPT_ERROR;
    if (AttributeOwner != this &&
         (TargetAttribute->AttributeType !=
              TypeData ||
          TargetAttribute->NameLength == 0))
    {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (TargetAttribute->AttributeType != TypeData ||
        TargetAttribute->Flags != 0)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    if (TargetAttribute->IsNonResident)
    {
        ULONGLONG NewDataSize =
            TargetAttribute->NonResident.DataSize <
                NewAllocationSize
            ? TargetAttribute->NonResident.DataSize
            : NewAllocationSize;

        Status = GetStandardInformationForUpdate(
            &StandardAttribute,
            &Standard);
        if (!NT_SUCCESS(Status))
            return Status;
        UNREFERENCED_PARAMETER(StandardAttribute);
        if (Standard->FilePermissions &
            FILE_PERM_SPARSE)
        {
            /*
             * Physical preallocation inside a sparse stream is a range
             * operation, not a safe interpretation of the scalar NT
             * allocation-size request. Keep it separate from EOF resizing
             * until the allocated-range API is available.
             */
            return STATUS_NOT_IMPLEMENTED;
        }

        ULONGLONG OldDataSize =
            TargetAttribute->NonResident.DataSize;
        ULONGLONG OldAllocatedSize =
            TargetAttribute->NonResident.AllocatedSize;

        Status = AttributeOwner->
            ResizeNonResidentStream(
                TargetAttribute,
                NewDataSize,
                NewAllocationSize);
        if (NT_SUCCESS(Status) &&
            AttributeOwner != this &&
            (NewDataSize != OldDataSize ||
             (TargetAttribute->IsNonResident
                 ? TargetAttribute->
                       NonResident.AllocatedSize
                 : 0) != OldAllocatedSize))
        {
            Status = UpdateAutomaticTimestamps(
                AutomaticTimestampFieldsForAttribute(
                    TypeData,
                    NewDataSize != OldDataSize));
        }
        return Status;
    }

    Status = AttributeOwner->
        ValidateResidentAttributeForUpdate(
            TargetAttribute,
            &ResidentDataLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if (NewAllocationSize < ResidentDataLength)
    {
        return SetFileDataSize(AttrType,
                               StreamName,
                               NewAllocationSize);
    }
    if (NewAllocationSize == 0)
        return STATUS_SUCCESS;

    Status = AttributeOwner->PromoteResidentData(
        TargetAttribute,
        NULL,
        0,
        0,
        ResidentDataLength,
        NewAllocationSize);
    if (NT_SUCCESS(Status) &&
        AttributeOwner != this)
    {
        Status = UpdateAutomaticTimestamps(
            AutomaticTimestampFieldsForAttribute(
                TypeData,
                FALSE));
    }
    return Status;
}

NTSTATUS
FileRecord::SetSparse(_In_ AttributeType AttrType,
                      _In_opt_ PWSTR StreamName,
                      _In_ BOOLEAN SetSparse)
{
    PAttribute StandardAttribute;
    PAttribute TargetAttribute;
    PFileRecord AttributeOwner;
    PStandardInformationEx Standard;
    PDataRun ExistingRuns = NULL;
    PDataRun AllocatedRuns = NULL;
    PDataRun CombinedRuns = NULL;
    PDataRun CombinedTail = NULL;
    PDataRun AllocatedCursor = NULL;
    PNonResidentMappingUpdate MappingUpdate = NULL;
    PUCHAR RecordBackup = NULL;
    ULONGLONG ClusterSize = 0;
    ULONGLONG LogicalClusters = 0;
    ULONGLONG PhysicalClusters = 0;
    ULONGLONG SparseClusters = 0;
    ULONGLONG LogicalCluster = 0;
    ULONGLONG AllocatedOffset = 0;
    ULONG MaxRuns;
    ULONG FileNameFields;
    ULONG StorageFlags;
    BOOLEAN AttributeSparse;
    BOOLEAN ContainsSparse = FALSE;
    BOOLEAN Committed = FALSE;
    BOOLEAN Unnamed;
    NTSTATUS Status;

    if (!DiskVolume || AttrType != TypeData)
        return STATUS_INVALID_PARAMETER;
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;
    if (!Header ||
        (Header->Flags & FR_IS_DIRECTORY) ||
        Header->MFTRecordNumber <=
            NTFS_LAST_RESERVED_FILE_RECORD)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!StreamName || StreamName[0] == 0)
    {
        BOOLEAN Materialized;

        Status = MaterializeWofCompressedData(
            &Materialized);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    TargetAttribute = GetAttribute(AttrType,
                                   StreamName);
    if (!TargetAttribute)
        return STATUS_NOT_FOUND;
    AttributeOwner =
        GetAttributeOwner(TargetAttribute);
    if (!AttributeOwner ||
        TargetAttribute->AttributeType != TypeData ||
        (AttributeOwner != this &&
         TargetAttribute->NameLength == 0) ||
        (TargetAttribute->Flags & ~ATTR_SPARSE) != 0)
    {
        return STATUS_NOT_IMPLEMENTED;
    }
    Unnamed = TargetAttribute->NameLength == 0;
    AttributeSparse =
        !!(TargetAttribute->Flags & ATTR_SPARSE);

    if (TargetAttribute->IsNonResident)
    {
        if (TargetAttribute->NonResident.FirstVCN != 0 ||
            TargetAttribute->
                NonResident.InitalizedDataSize >
                TargetAttribute->NonResident.DataSize ||
            TargetAttribute->NonResident.DataSize >
                TargetAttribute->
                    NonResident.AllocatedSize ||
            (AttributeSparse &&
             (TargetAttribute->
                  NonResident.CompressionUnitSize != 4 ||
              TargetAttribute->Length < 0x48 ||
              TargetAttribute->
                  NonResident.DataRunsOffset < 0x48)) ||
            (!AttributeSparse &&
             TargetAttribute->
                 NonResident.CompressionUnitSize != 0))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
    }
    else
    {
        Status = AttributeOwner->
            ValidateResidentAttributeForUpdate(
            TargetAttribute,
            NULL);
        if (!NT_SUCCESS(Status))
            return Status;
        if (AttributeSparse)
            return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        return Status;
    UNREFERENCED_PARAMETER(StandardAttribute);
    if (AttributeSparse &&
        !(Standard->FilePermissions &
          FILE_PERM_SPARSE))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    /*
     * Marking sparse is deliberately metadata-only: Windows does not punch
     * existing allocation until FSCTL_SET_ZERO_DATA is issued.
     */
    if (SetSparse)
    {
        RecordBackup =
            new(PagedPool, TAG_FILE_RECORD)
                UCHAR[RecordBufferSize];
        if (!RecordBackup)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlCopyMemory(RecordBackup,
                      Data,
                      RecordBufferSize);

        Standard->FilePermissions |=
            FILE_PERM_SPARSE;
        Status = SynchronizeFileNameInformation(
            NTFS_FILE_NAME_UPDATE_STORAGE_FLAGS,
            0,
            0,
            0,
            0,
            Standard->FilePermissions &
                (FILE_PERM_SPARSE |
                 FILE_PERM_COMPRESSED));
        if (!NT_SUCCESS(Status))
            goto Restore;

        Status = PrepareAutomaticTimestamps(
            NTFS_BASIC_INFO_CHANGE_TIME,
            NULL);
        if (!NT_SUCCESS(Status))
            goto Restore;
        Status = DiskVolume->MFT->
            WriteFileRecordToMFT(this);
        if (NT_SUCCESS(Status))
            Committed = TRUE;
        goto Restore;
    }
    if (AttributeOwner != this)
        return STATUS_NOT_IMPLEMENTED;

    if (TargetAttribute->IsNonResident &&
        AttributeSparse)
    {
        ClusterSize = BytesPerCluster(DiskVolume);
        if (ClusterSize == 0 ||
            TargetAttribute->
                NonResident.AllocatedSize == 0 ||
            TargetAttribute->
                NonResident.AllocatedSize %
                ClusterSize != 0)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }
        LogicalClusters =
            TargetAttribute->
                NonResident.AllocatedSize /
            ClusterSize;
        ExistingRuns = FindNonResidentData(
            TargetAttribute);
        if (!ExistingRuns)
            return STATUS_FILE_CORRUPT_ERROR;

        for (PDataRun Run = ExistingRuns;
             Run;
             Run = Run->NextRun)
        {
            if (Run->Length == 0 ||
                LogicalCluster >
                    ~(ULONGLONG)0 - Run->Length)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
            LogicalCluster += Run->Length;
            if (Run->IsSparse)
            {
                if (SparseClusters >
                    ~(ULONGLONG)0 - Run->Length)
                {
                    Status = STATUS_FILE_TOO_LARGE;
                    goto Done;
                }
                SparseClusters += Run->Length;
            }
            else
            {
                if (Run->LCN >
                        ~(ULONGLONG)0 - Run->Length ||
                    PhysicalClusters >
                        ~(ULONGLONG)0 - Run->Length)
                {
                    Status = STATUS_FILE_CORRUPT_ERROR;
                    goto Done;
                }
                PhysicalClusters += Run->Length;
            }
        }
        if (LogicalCluster != LogicalClusters ||
            PhysicalClusters >
                ~(ULONGLONG)0 / ClusterSize ||
            PhysicalClusters * ClusterSize !=
                TargetAttribute->
                    NonResident.CompressedDataSize)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        if (SparseClusters > MAXULONG)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Done;
        }

        if (SparseClusters != 0)
        {
            MaxRuns = RecordBufferSize / 3;
            if (MaxRuns == 0)
                MaxRuns = 1;
            Status = DiskVolume->AllocateClusters(
                0,
                (ULONG)SparseClusters,
                MaxRuns,
                &AllocatedRuns);
            if (!NT_SUCCESS(Status))
                goto Done;
            Status = ZeroRunBytes(
                DiskVolume,
                AllocatedRuns,
                0,
                SparseClusters * ClusterSize);
            if (!NT_SUCCESS(Status))
                goto Done;
        }

        AllocatedCursor = AllocatedRuns;
        LogicalCluster = 0;
        for (PDataRun Run = ExistingRuns;
             Run;
             Run = Run->NextRun)
        {
            Status = AppendSparseWriteSegment(
                Run,
                LogicalCluster,
                0,
                LogicalClusters,
                &AllocatedCursor,
                &AllocatedOffset,
                &CombinedRuns,
                &CombinedTail);
            if (!NT_SUCCESS(Status))
                goto Done;
            LogicalCluster += Run->Length;
        }
        if (AllocatedCursor || AllocatedOffset != 0)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
    }

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!RecordBackup)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(RecordBackup,
                  Data,
                  RecordBufferSize);

    if (TargetAttribute->IsNonResident &&
        AttributeSparse)
    {
        Status = ReplaceNonResidentMappingPairs(
            &TargetAttribute,
            CombinedRuns,
            TargetAttribute->
                NonResident.AllocatedSize,
            TargetAttribute->
                NonResident.DataSize,
            TargetAttribute->
                NonResident.InitalizedDataSize,
            &MappingUpdate);
        if (!NT_SUCCESS(Status))
            goto Restore;
    }

    Status = RecordContainsSparseData(
        Data,
        Header,
        RecordBufferSize,
        &ContainsSparse);
    if (!NT_SUCCESS(Status))
        goto Restore;

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        goto Restore;
    if (ContainsSparse)
        Standard->FilePermissions |=
            FILE_PERM_SPARSE;
    else
        Standard->FilePermissions &=
            ~FILE_PERM_SPARSE;

    FileNameFields =
        NTFS_FILE_NAME_UPDATE_STORAGE_FLAGS;
    if (Unnamed &&
        TargetAttribute->IsNonResident)
    {
        FileNameFields |=
            NTFS_FILE_NAME_UPDATE_SIZES;
    }
    StorageFlags =
        Standard->FilePermissions &
        (FILE_PERM_SPARSE |
         FILE_PERM_COMPRESSED);
    Status = SynchronizeFileNameInformation(
        FileNameFields,
        TargetAttribute->IsNonResident
            ? TargetAttribute->
                NonResident.AllocatedSize
            : 0,
        TargetAttribute->IsNonResident
            ? TargetAttribute->
                NonResident.DataSize
            : TargetAttribute->
                Resident.DataLength,
        0,
        0,
        StorageFlags);
    if (!NT_SUCCESS(Status))
        goto Restore;

    Status = PrepareAutomaticTimestamps(
        NTFS_BASIC_INFO_CHANGE_TIME,
        NULL);
    if (!NT_SUCCESS(Status))
        goto Restore;
    Status = MappingUpdate
        ? CommitNonResidentMappingUpdate(
            &MappingUpdate)
        : DiskVolume->MFT->
            WriteFileRecordToMFT(this);
    if (NT_SUCCESS(Status))
        Committed = TRUE;

Restore:
    if (!Committed && RecordBackup)
    {
        AbortNonResidentMappingUpdate(
            &MappingUpdate);
        RtlCopyMemory(Data,
                      RecordBackup,
                      RecordBufferSize);
        Header =
            reinterpret_cast<PFileRecordHeader>(
                Data);
        ClearDataRunCache();
    }

Done:
    if (!Committed && AllocatedRuns)
        (void)DiskVolume->ReleaseClusters(
            AllocatedRuns);
    FreeDataRun(CombinedRuns);
    FreeDataRun(AllocatedRuns);
    FreeDataRun(ExistingRuns);
    delete[] RecordBackup;
    return Status;
}

NTSTATUS
FileRecord::SetZeroData(
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ ULONGLONG FileOffset,
    _In_ ULONGLONG BeyondFinalZero)
{
    PAttribute StandardAttribute;
    PAttribute TargetAttribute;
    PStandardInformationEx Standard;
    PDataRun ExistingRuns = NULL;
    PDataRun ResultRuns = NULL;
    PDataRun ResultTail = NULL;
    PDataRun ReleasedRuns = NULL;
    PDataRun ReleasedTail = NULL;
    PDataRun EffectiveRuns;
    PNonResidentMappingUpdate MappingUpdate = NULL;
    PUCHAR RecordBackup = NULL;
    ULONGLONG ClusterSize;
    ULONGLONG UnitBytes;
    ULONGLONG LogicalClusters;
    ULONGLONG LogicalCluster = 0;
    ULONGLONG PhysicalClusters = 0;
    ULONGLONG ResultPhysicalClusters = 0;
    ULONGLONG DataSize;
    ULONGLONG InitializedSize;
    ULONGLONG NewInitializedSize;
    ULONGLONG EffectiveEnd;
    ULONGLONG ZeroStart;
    ULONGLONG HoleStartCluster = 0;
    ULONGLONG HoleEndCluster = 0;
    ULONGLONG PhysicalBytes;
    ULONG ResidentLength;
    ULONG FileNameFields;
    ULONG StorageFlags;
    BOOLEAN AttributeSparse;
    BOOLEAN StreamSparse;
    BOOLEAN MappingChanged = FALSE;
    BOOLEAN Committed = FALSE;
    BOOLEAN Unnamed;
    NTSTATUS Status;

    if (!DiskVolume || AttrType != TypeData ||
        FileOffset > BeyondFinalZero)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (DiskVolume->IsReadOnly)
        return STATUS_ACCESS_DENIED;
    if (!Header ||
        (Header->Flags & FR_IS_DIRECTORY) ||
        Header->MFTRecordNumber <=
            NTFS_LAST_RESERVED_FILE_RECORD)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (!StreamName || StreamName[0] == 0)
    {
        BOOLEAN Materialized;

        Status = MaterializeWofCompressedData(
            &Materialized);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    TargetAttribute = GetAttribute(AttrType,
                                   StreamName);
    if (!TargetAttribute)
        return STATUS_NOT_FOUND;
    if (GetAttributeOwner(TargetAttribute) != this ||
        TargetAttribute->AttributeType != TypeData ||
        (TargetAttribute->Flags & ~ATTR_SPARSE) != 0)
    {
        return STATUS_NOT_IMPLEMENTED;
    }
    Unnamed = TargetAttribute->NameLength == 0;
    AttributeSparse =
        !!(TargetAttribute->Flags & ATTR_SPARSE);

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        return Status;
    UNREFERENCED_PARAMETER(StandardAttribute);
    StreamSparse =
        !!(AttributeSparse ||
           (Standard->FilePermissions &
            FILE_PERM_SPARSE));
    if (AttributeSparse &&
        !(Standard->FilePermissions &
          FILE_PERM_SPARSE))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    if (FileOffset == BeyondFinalZero)
        return STATUS_SUCCESS;

    if (!TargetAttribute->IsNonResident)
    {
        Status = ValidateResidentAttributeForUpdate(
            TargetAttribute,
            &ResidentLength);
        if (!NT_SUCCESS(Status))
            return Status;
        if (FileOffset >= ResidentLength)
            return STATUS_SUCCESS;
        EffectiveEnd = BeyondFinalZero <
                ResidentLength
            ? BeyondFinalZero
            : ResidentLength;

        RecordBackup =
            new(PagedPool, TAG_FILE_RECORD)
                UCHAR[RecordBufferSize];
        if (!RecordBackup)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlCopyMemory(RecordBackup,
                      Data,
                      RecordBufferSize);
        RtlZeroMemory(
            GetResidentDataPointer(TargetAttribute) +
                (ULONG)FileOffset,
            (ULONG)(EffectiveEnd - FileOffset));

        Status = PrepareAutomaticTimestamps(
            NTFS_BASIC_INFO_LAST_WRITE_TIME |
            NTFS_BASIC_INFO_CHANGE_TIME,
            NULL);
        if (NT_SUCCESS(Status))
        {
            Status = DiskVolume->MFT->
                WriteFileRecordToMFT(this);
        }
        if (!NT_SUCCESS(Status))
        {
            RtlCopyMemory(Data,
                          RecordBackup,
                          RecordBufferSize);
            Header =
                reinterpret_cast<PFileRecordHeader>(
                    Data);
            ClearDataRunCache();
        }
        delete[] RecordBackup;
        return Status;
    }

    if (TargetAttribute->NonResident.FirstVCN != 0 ||
        TargetAttribute->NonResident.InitalizedDataSize >
            TargetAttribute->NonResident.DataSize ||
        TargetAttribute->NonResident.DataSize >
            TargetAttribute->NonResident.AllocatedSize ||
        (AttributeSparse &&
         (TargetAttribute->
              NonResident.CompressionUnitSize != 4 ||
          TargetAttribute->Length < 0x48 ||
          TargetAttribute->
              NonResident.DataRunsOffset < 0x48)) ||
        (!AttributeSparse &&
         TargetAttribute->
             NonResident.CompressionUnitSize != 0))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    ClusterSize = BytesPerCluster(DiskVolume);
    if (ClusterSize == 0 ||
        ClusterSize >
            ~(ULONGLONG)0 / (1ULL << 4) ||
        TargetAttribute->NonResident.AllocatedSize == 0 ||
        TargetAttribute->NonResident.AllocatedSize %
            ClusterSize != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    UnitBytes = ClusterSize * (1ULL << 4);
    LogicalClusters =
        TargetAttribute->NonResident.AllocatedSize /
        ClusterSize;
    DataSize =
        TargetAttribute->NonResident.DataSize;
    InitializedSize =
        TargetAttribute->
            NonResident.InitalizedDataSize;
    if (FileOffset >= DataSize)
        return STATUS_SUCCESS;
    EffectiveEnd = BeyondFinalZero < DataSize
        ? BeyondFinalZero
        : DataSize;
    NewInitializedSize =
        InitializedSize > EffectiveEnd
        ? InitializedSize
        : EffectiveEnd;
    ZeroStart = FileOffset;
    if (EffectiveEnd > InitializedSize &&
        InitializedSize < ZeroStart)
    {
        ZeroStart = InitializedSize;
    }

    ExistingRuns = FindNonResidentData(
        TargetAttribute);
    if (!ExistingRuns)
        return STATUS_FILE_CORRUPT_ERROR;

    for (PDataRun Run = ExistingRuns;
         Run;
         Run = Run->NextRun)
    {
        if (Run->Length == 0 ||
            LogicalCluster >
                ~(ULONGLONG)0 - Run->Length)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        LogicalCluster += Run->Length;
        if (!Run->IsSparse)
        {
            if (Run->LCN >
                    ~(ULONGLONG)0 - Run->Length ||
                PhysicalClusters >
                    ~(ULONGLONG)0 - Run->Length)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
            PhysicalClusters += Run->Length;
        }
        else if (!AttributeSparse)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
    }
    if (LogicalCluster != LogicalClusters ||
        PhysicalClusters >
            ~(ULONGLONG)0 / ClusterSize ||
        (AttributeSparse &&
         PhysicalClusters * ClusterSize !=
             TargetAttribute->
                 NonResident.CompressedDataSize) ||
        (!AttributeSparse &&
         PhysicalClusters != LogicalClusters))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    /*
     * NTFS sparse streams use a 2^4-cluster sparse unit. Fully covered units
     * become holes; partial leading/trailing units retain allocation and are
     * physically zeroed. A request reaching EOF may release its short final
     * unit because bytes beyond EOF are not part of the stream.
     */
    if (StreamSparse)
    {
        ULONGLONG Remainder =
            FileOffset % UnitBytes;
        ULONGLONG HoleStartBytes =
            FileOffset;

        if (Remainder != 0)
        {
            ULONGLONG Advance =
                UnitBytes - Remainder;
            if (HoleStartBytes >
                ~(ULONGLONG)0 - Advance)
            {
                Status = STATUS_FILE_TOO_LARGE;
                goto Done;
            }
            HoleStartBytes += Advance;
        }
        HoleStartCluster =
            HoleStartBytes / ClusterSize;
        HoleEndCluster =
            BeyondFinalZero >= DataSize
            ? LogicalClusters
            : (BeyondFinalZero / UnitBytes) *
                (1ULL << 4);
        if (HoleStartCluster > LogicalClusters)
            HoleStartCluster = LogicalClusters;
        if (HoleEndCluster > LogicalClusters)
            HoleEndCluster = LogicalClusters;
        if (HoleEndCluster < HoleStartCluster)
            HoleEndCluster = HoleStartCluster;
    }

    if (HoleStartCluster < HoleEndCluster)
    {
        LogicalCluster = 0;
        for (PDataRun Run = ExistingRuns;
             Run;
             Run = Run->NextRun)
        {
            Status = AppendSparseHoleSegment(
                Run,
                LogicalCluster,
                HoleStartCluster,
                HoleEndCluster,
                &ResultRuns,
                &ResultTail,
                &ReleasedRuns,
                &ReleasedTail);
            if (!NT_SUCCESS(Status))
                goto Done;
            LogicalCluster += Run->Length;
        }
        MappingChanged = ReleasedRuns != NULL;
    }

    EffectiveRuns = MappingChanged
        ? ResultRuns
        : ExistingRuns;
    for (PDataRun Run = EffectiveRuns;
         Run;
         Run = Run->NextRun)
    {
        if (!Run->IsSparse)
        {
            if (ResultPhysicalClusters >
                ~(ULONGLONG)0 - Run->Length)
            {
                Status = STATUS_FILE_TOO_LARGE;
                goto Done;
            }
            ResultPhysicalClusters += Run->Length;
        }
    }
    if (ResultPhysicalClusters >
        ~(ULONGLONG)0 / ClusterSize)
    {
        Status = STATUS_FILE_TOO_LARGE;
        goto Done;
    }
    PhysicalBytes =
        ResultPhysicalClusters * ClusterSize;

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!RecordBackup)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(RecordBackup,
                  Data,
                  RecordBufferSize);

    Status = ZeroLogicalAllocatedBytes(
        DiskVolume,
        EffectiveRuns,
        ZeroStart,
        EffectiveEnd - ZeroStart);
    if (!NT_SUCCESS(Status))
        goto Restore;

    if (MappingChanged)
    {
        Status = ReplaceNonResidentMappingPairs(
            &TargetAttribute,
            ResultRuns,
            TargetAttribute->
                NonResident.AllocatedSize,
            DataSize,
            NewInitializedSize,
            &MappingUpdate);
        if (!NT_SUCCESS(Status))
            goto Restore;
    }
    else
    {
        TargetAttribute->
            NonResident.InitalizedDataSize =
            NewInitializedSize;
    }

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        goto Restore;
    StorageFlags =
        Standard->FilePermissions &
        (FILE_PERM_SPARSE |
         FILE_PERM_COMPRESSED);
    FileNameFields =
        NTFS_FILE_NAME_UPDATE_STORAGE_FLAGS;
    if (Unnamed)
    {
        FileNameFields |=
            NTFS_FILE_NAME_UPDATE_SIZES;
    }
    Status = SynchronizeFileNameInformation(
        FileNameFields,
        PhysicalBytes,
        DataSize,
        0,
        0,
        StorageFlags);
    if (!NT_SUCCESS(Status))
        goto Restore;

    Status = PrepareAutomaticTimestamps(
        NTFS_BASIC_INFO_LAST_WRITE_TIME |
        NTFS_BASIC_INFO_CHANGE_TIME,
        NULL);
    if (!NT_SUCCESS(Status))
        goto Restore;
    Status = MappingUpdate
        ? CommitNonResidentMappingUpdate(
            &MappingUpdate)
        : DiskVolume->MFT->
            WriteFileRecordToMFT(this);
    if (!NT_SUCCESS(Status))
        goto Restore;

    Committed = TRUE;
    if (ReleasedRuns)
    {
        Status = DiskVolume->ReleaseClusters(
            ReleasedRuns);
    }
    goto Done;

Restore:
    if (!Committed)
    {
        AbortNonResidentMappingUpdate(
            &MappingUpdate);
        RtlCopyMemory(Data,
                      RecordBackup,
                      RecordBufferSize);
        Header =
            reinterpret_cast<PFileRecordHeader>(
                Data);
        ClearDataRunCache();
    }

Done:
    FreeDataRun(ReleasedRuns);
    FreeDataRun(ResultRuns);
    FreeDataRun(ExistingRuns);
    delete[] RecordBackup;
    return Status;
}

NTSTATUS
FileRecord::QueryAllocatedRanges(
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ ULONGLONG FileOffset,
    _In_ ULONGLONG Length,
    _Out_opt_ PNtfsAllocatedRange Ranges,
    _Inout_ PULONG RangeCount)
{
    PAttribute StandardAttribute;
    PAttribute TargetAttribute;
    PStandardInformationEx Standard;
    PDataRun Runs = NULL;
    ULONGLONG ClusterSize;
    ULONGLONG QueryEnd;
    ULONGLONG LogicalCluster = 0;
    ULONGLONG LogicalClusters;
    ULONGLONG PhysicalClusters = 0;
    ULONGLONG ActiveStart = 0;
    ULONGLONG ActiveEnd = 0;
    ULONG Capacity;
    ULONG Written = 0;
    ULONG ResidentLength;
    BOOLEAN Active = FALSE;
    BOOLEAN StreamSparse;
    NTSTATUS Status;

    if (!RangeCount || !DiskVolume ||
        AttrType != TypeData ||
        FileOffset > ~(ULONGLONG)0 - Length)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Capacity = *RangeCount;
    *RangeCount = 0;
    if (Capacity != 0 && !Ranges)
        return STATUS_INVALID_PARAMETER;
    if (!Header || (Header->Flags & FR_IS_DIRECTORY))
        return STATUS_INVALID_PARAMETER;
    QueryEnd = FileOffset + Length;

    TargetAttribute = GetAttribute(AttrType,
                                   StreamName);
    if (!TargetAttribute)
        return STATUS_NOT_FOUND;
    if (TargetAttribute->AttributeType != TypeData ||
        (TargetAttribute->Flags &
         ~(ATTR_SPARSE)) != 0)
    {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (Length == 0)
        return STATUS_SUCCESS;

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        return Status;
    UNREFERENCED_PARAMETER(StandardAttribute);
    StreamSparse =
        !!((TargetAttribute->Flags & ATTR_SPARSE) ||
           (Standard->FilePermissions &
            FILE_PERM_SPARSE));
    if ((TargetAttribute->Flags & ATTR_SPARSE) &&
        !(Standard->FilePermissions &
          FILE_PERM_SPARSE))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    /*
     * MS-FSA requires a non-sparse stream to echo the complete requested
     * range, even if it extends past EOF.
     */
    if (!StreamSparse)
    {
        Status = StoreAllocatedRange(Ranges,
                                     Capacity,
                                     &Written,
                                     FileOffset,
                                     QueryEnd);
        *RangeCount = Written;
        return Status;
    }

    if (!TargetAttribute->IsNonResident)
    {
        Status = ValidateResidentAttributeForUpdate(
            TargetAttribute,
            &ResidentLength);
        if (!NT_SUCCESS(Status))
            return Status;
        if (FileOffset >= ResidentLength)
            return STATUS_SUCCESS;

        Status = StoreAllocatedRange(
            Ranges,
            Capacity,
            &Written,
            FileOffset,
            QueryEnd < ResidentLength
                ? QueryEnd
                : ResidentLength);
        *RangeCount = Written;
        return Status;
    }

    if (TargetAttribute->NonResident.FirstVCN != 0 ||
        TargetAttribute->NonResident.InitalizedDataSize >
            TargetAttribute->NonResident.DataSize ||
        TargetAttribute->NonResident.DataSize >
            TargetAttribute->NonResident.AllocatedSize ||
        ((TargetAttribute->Flags & ATTR_SPARSE) &&
         (TargetAttribute->
              NonResident.CompressionUnitSize != 4 ||
          TargetAttribute->Length < 0x48 ||
          TargetAttribute->
              NonResident.DataRunsOffset < 0x48)) ||
        (!(TargetAttribute->Flags & ATTR_SPARSE) &&
         TargetAttribute->
             NonResident.CompressionUnitSize != 0))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    ClusterSize = BytesPerCluster(DiskVolume);
    if (ClusterSize == 0 ||
        TargetAttribute->NonResident.AllocatedSize == 0 ||
        TargetAttribute->NonResident.AllocatedSize %
            ClusterSize != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    LogicalClusters =
        TargetAttribute->NonResident.AllocatedSize /
        ClusterSize;
    Runs = FindNonResidentData(TargetAttribute);
    if (!Runs)
        return STATUS_FILE_CORRUPT_ERROR;

    Status = STATUS_SUCCESS;
    for (PDataRun Run = Runs; Run; Run = Run->NextRun)
    {
        ULONGLONG RunEnd;

        if (Run->Length == 0 ||
            LogicalCluster >
                ~(ULONGLONG)0 - Run->Length)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        RunEnd = LogicalCluster + Run->Length;
        if (RunEnd > LogicalClusters)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        if (!Run->IsSparse)
        {
            if (Run->LCN >
                    ~(ULONGLONG)0 - Run->Length ||
                PhysicalClusters >
                    ~(ULONGLONG)0 - Run->Length)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
            PhysicalClusters += Run->Length;
            if (!Active)
            {
                ActiveStart = LogicalCluster;
                Active = TRUE;
            }
            ActiveEnd = RunEnd;
        }
        else if (Active)
        {
            ULONGLONG Start = ActiveStart * ClusterSize;
            ULONGLONG End = ActiveEnd * ClusterSize;

            Start = Start > FileOffset
                ? Start
                : FileOffset;
            End = End < QueryEnd ? End : QueryEnd;
            if (Start < End)
            {
                Status = StoreAllocatedRange(
                    Ranges,
                    Capacity,
                    &Written,
                    Start,
                    End);
                if (!NT_SUCCESS(Status))
                    goto Done;
            }
            Active = FALSE;
        }
        LogicalCluster = RunEnd;
    }

    if (LogicalCluster != LogicalClusters ||
        PhysicalClusters >
            ~(ULONGLONG)0 / ClusterSize ||
        ((TargetAttribute->Flags & ATTR_SPARSE) &&
         PhysicalClusters * ClusterSize !=
             TargetAttribute->
                 NonResident.CompressedDataSize) ||
        (!(TargetAttribute->Flags & ATTR_SPARSE) &&
         PhysicalClusters != LogicalClusters))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    if (Active)
    {
        ULONGLONG Start = ActiveStart * ClusterSize;
        ULONGLONG End = ActiveEnd * ClusterSize;

        Start = Start > FileOffset
            ? Start
            : FileOffset;
        End = End < QueryEnd ? End : QueryEnd;
        if (Start < End)
        {
            Status = StoreAllocatedRange(
                Ranges,
                Capacity,
                &Written,
                Start,
                End);
        }
    }

Done:
    *RangeCount = Written;
    FreeDataRun(Runs);
    return Status;
}

NTSTATUS
FileRecord::PromoteResidentData(
    _In_ PAttribute TargetAttribute,
    _In_opt_ PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ ULONGLONG Offset,
    _In_ ULONGLONG NewDataSize,
    _In_ ULONGLONG RequestedAllocationSize)
{
    PDataRun AllocatedRuns = NULL;
    PUCHAR OldData = NULL;
    PUCHAR RecordBackup = NULL;
    ULONGLONG ClusterSize;
    ULONGLONG AllocatedSize;
    ULONGLONG EndOffset;
    ULONGLONG NewInitializedSize;
    ULONGLONG AllocationTarget;
    ULONGLONG RequiredClusters;
    ULONG ClusterCount;
    ULONG MaxRuns;
    ULONG OldDataLength;
    NTSTATUS Status;

    if (!TargetAttribute ||
        (!Buffer && Length != 0) ||
        Offset > ~(ULONGLONG)0 - Length)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = ValidateResidentAttributeForUpdate(
        TargetAttribute,
        &OldDataLength);
    if (!NT_SUCCESS(Status))
        return Status;
    if ((TargetAttribute->AttributeType != TypeData &&
         TargetAttribute->AttributeType != TypeEA &&
         TargetAttribute->AttributeType !=
            TypeReparsePoint &&
         TargetAttribute->AttributeType !=
            TypeBitmap &&
         TargetAttribute->AttributeType !=
            TypeIndexAllocation &&
         TargetAttribute->AttributeType !=
            TypeAttributeList) ||
        TargetAttribute->Flags != 0)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    EndOffset = Offset + Length;
    if (NewDataSize < OldDataLength ||
        NewDataSize < EndOffset)
    {
        return STATUS_INVALID_PARAMETER;
    }
    NewInitializedSize = Length != 0
        ? (EndOffset > OldDataLength
            ? EndOffset
            : OldDataLength)
        : OldDataLength;
    AllocationTarget =
        RequestedAllocationSize > NewDataSize
        ? RequestedAllocationSize
        : NewDataSize;
    if (AllocationTarget == 0)
        return STATUS_INVALID_PARAMETER;
    ClusterSize = BytesPerCluster(DiskVolume);
    if (ClusterSize == 0)
        return STATUS_FILE_CORRUPT_ERROR;
    if (AllocationTarget >
        ~(ULONGLONG)0 - (ClusterSize - 1))
    {
        return STATUS_FILE_TOO_LARGE;
    }
    RequiredClusters =
        (AllocationTarget + ClusterSize - 1) /
        ClusterSize;
    if (RequiredClusters == 0 ||
        RequiredClusters > MAXULONG)
    {
        return STATUS_FILE_TOO_LARGE;
    }
    ClusterCount = (ULONG)RequiredClusters;
    AllocatedSize =
        (ULONGLONG)ClusterCount * ClusterSize;
    MaxRuns = RecordBufferSize / 3;
    if (MaxRuns == 0)
        MaxRuns = 1;

    if (OldDataLength != 0)
    {
        OldData =
            new(PagedPool, TAG_NTFS)
                UCHAR[OldDataLength];
        if (!OldData)
            return STATUS_INSUFFICIENT_RESOURCES;
        RtlCopyMemory(
            OldData,
            reinterpret_cast<PUCHAR>(TargetAttribute) +
                TargetAttribute->Resident.DataOffset,
            OldDataLength);
    }

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!RecordBackup)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(RecordBackup, Data, RecordBufferSize);

    Status = DiskVolume->AllocateClusters(0,
                                          ClusterCount,
                                          MaxRuns,
                                          &AllocatedRuns);
    if (!NT_SUCCESS(Status))
        goto Done;

    Status = ConvertResidentToNonResident(
        TargetAttribute,
        AllocatedRuns,
        AllocatedSize,
        NewDataSize,
        NewInitializedSize);
    if (!NT_SUCCESS(Status))
        goto Rollback;

    if (OldDataLength != 0)
    {
        Status = WriteRunBytes(DiskVolume,
                               AllocatedRuns,
                               0,
                               OldData,
                               OldDataLength);
        if (!NT_SUCCESS(Status))
            goto Rollback;
    }
    if (Length != 0 &&
        Offset > OldDataLength)
    {
        Status = ZeroRunBytes(
            DiskVolume,
            AllocatedRuns,
            OldDataLength,
            Offset - OldDataLength);
        if (!NT_SUCCESS(Status))
            goto Rollback;
    }
    if (Length != 0)
    {
        Status = WriteRunBytes(DiskVolume,
                               AllocatedRuns,
                               Offset,
                               Buffer,
                               Length);
        if (!NT_SUCCESS(Status))
            goto Rollback;
    }

    if (TargetAttribute->AttributeType == TypeData &&
        TargetAttribute->NameLength == 0)
    {
        Status = SynchronizeFileNameSizes(AllocatedSize,
                                          NewDataSize);
        if (!NT_SUCCESS(Status))
            goto Rollback;
    }

    Status = PrepareAutomaticTimestamps(
        AutomaticTimestampFieldsForAttribute(
            (AttributeType)
                TargetAttribute->AttributeType,
            Length != 0 ||
            NewDataSize != OldDataLength),
        NULL);
    if (!NT_SUCCESS(Status))
        goto Rollback;

    Status = DiskVolume->MFT->WriteFileRecordToMFT(this);
    if (NT_SUCCESS(Status))
        goto Done;

Rollback:
    RtlCopyMemory(Data, RecordBackup, RecordBufferSize);
    Header = reinterpret_cast<PFileRecordHeader>(Data);
    ClearDataRunCache();
    if (AllocatedRuns)
        (void)DiskVolume->ReleaseClusters(AllocatedRuns);

Done:
    FreeDataRun(AllocatedRuns);
    delete[] RecordBackup;
    delete[] OldData;
    return Status;
}

NTSTATUS
FileRecord::ResizeNonResidentData(
    _In_ PAttribute TargetAttribute,
    _In_ ULONGLONG NewDataSize)
{
    return ResizeNonResidentStream(TargetAttribute,
                                   NewDataSize,
                                   NewDataSize);
}

NTSTATUS
FileRecord::ResizeSparseData(
    _In_ PAttribute TargetAttribute,
    _In_ ULONGLONG NewDataSize)
{
    PAttribute StandardAttribute;
    PFileRecord AttributeOwner;
    PFileRecord ReplacementOwner = NULL;
    PStandardInformationEx Standard;
    PDataRun ExistingRuns = NULL;
    PDataRun ExpandedRuns = NULL;
    PDataRun ExpandedTail = NULL;
    PDataRun RetainedRuns = NULL;
    PDataRun ReleasedRuns = NULL;
    PDataRun CollapseRuns = NULL;
    PDataRun CollapseTail = NULL;
    PDataRun ResultRuns = NULL;
    PNonResidentMappingUpdate MappingUpdate = NULL;
    PUCHAR RecordBackup = NULL;
    PUCHAR OwnerRecordBackup = NULL;
    ULONGLONG ClusterSize;
    ULONGLONG LogicalClusters = 0;
    ULONGLONG PhysicalClusters = 0;
    ULONGLONG ResultPhysicalClusters = 0;
    ULONGLONG OldLogicalClusters;
    ULONGLONG RequiredClusters;
    ULONGLONG NewAllocatedSize;
    ULONGLONG NewInitializedSize;
    ULONGLONG PhysicalBytes = 0;
    ULONGLONG OldDataSize;
    UINT32 FileNameFields;
    BOOLEAN AttributeSparse;
    BOOLEAN HasSparseRun = FALSE;
    BOOLEAN Unnamed;
    BOOLEAN Committed = FALSE;
    BOOLEAN OwnerWriteAttempted = FALSE;
    NTSTATUS Status;

    AttributeOwner =
        GetAttributeOwner(TargetAttribute);
    if (!TargetAttribute ||
        !TargetAttribute->IsNonResident ||
        !AttributeOwner ||
        (AttributeOwner != this &&
         (TargetAttribute->AttributeType !=
              TypeData ||
          TargetAttribute->NameLength == 0)))
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    Status = AttributeOwner->
        ValidateAttributeForUpdate(
        TargetAttribute,
        TRUE,
        NULL);
    if (!NT_SUCCESS(Status))
        return Status;

    AttributeSparse =
        !!(TargetAttribute->Flags & ATTR_SPARSE);
    Unnamed =
        TargetAttribute->AttributeType == TypeData &&
        TargetAttribute->NameLength == 0;
    if (TargetAttribute->AttributeType != TypeData ||
        (TargetAttribute->Flags & ~ATTR_SPARSE) != 0 ||
        TargetAttribute->NonResident.FirstVCN != 0 ||
        (AttributeSparse &&
         (TargetAttribute->
              NonResident.CompressionUnitSize != 4 ||
          TargetAttribute->Length < 0x48 ||
          TargetAttribute->
              NonResident.DataRunsOffset < 0x48)) ||
        (!AttributeSparse &&
         TargetAttribute->
             NonResident.CompressionUnitSize != 0) ||
        TargetAttribute->NonResident.InitalizedDataSize >
            TargetAttribute->NonResident.DataSize ||
        TargetAttribute->NonResident.DataSize >
            TargetAttribute->NonResident.AllocatedSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    ClusterSize = BytesPerCluster(DiskVolume);
    if (ClusterSize == 0 ||
        TargetAttribute->NonResident.AllocatedSize == 0 ||
        TargetAttribute->NonResident.AllocatedSize %
            ClusterSize != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    if (NewDataSize >
        ~(ULONGLONG)0 - (ClusterSize - 1))
    {
        return STATUS_FILE_TOO_LARGE;
    }

    Status = GetStandardInformationForUpdate(
        &StandardAttribute,
        &Standard);
    if (!NT_SUCCESS(Status))
        return Status;
    UNREFERENCED_PARAMETER(StandardAttribute);
    if (!(Standard->FilePermissions &
          FILE_PERM_SPARSE))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    ExistingRuns = FindNonResidentData(
        TargetAttribute);
    if (!ExistingRuns)
        return STATUS_FILE_CORRUPT_ERROR;

    OldLogicalClusters =
        TargetAttribute->NonResident.AllocatedSize /
        ClusterSize;
    for (PDataRun Run = ExistingRuns;
         Run;
         Run = Run->NextRun)
    {
        if (Run->Length == 0 ||
            LogicalClusters >
                ~(ULONGLONG)0 - Run->Length)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        LogicalClusters += Run->Length;
        if (Run->IsSparse)
        {
            HasSparseRun = TRUE;
        }
        else
        {
            if (Run->LCN >
                    ~(ULONGLONG)0 - Run->Length ||
                PhysicalClusters >
                    ~(ULONGLONG)0 - Run->Length)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
            PhysicalClusters += Run->Length;
        }
    }
    if (LogicalClusters != OldLogicalClusters ||
        (HasSparseRun && !AttributeSparse) ||
        (!AttributeSparse &&
         PhysicalClusters != OldLogicalClusters) ||
        PhysicalClusters >
            ~(ULONGLONG)0 / ClusterSize ||
        (AttributeSparse &&
         PhysicalClusters * ClusterSize !=
             TargetAttribute->
                 NonResident.CompressedDataSize))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    RequiredClusters = NewDataSize == 0
        ? 0
        : (NewDataSize + ClusterSize - 1) /
            ClusterSize;
    NewAllocatedSize =
        RequiredClusters * ClusterSize;
    OldDataSize =
        TargetAttribute->NonResident.DataSize;
    NewInitializedSize =
        TargetAttribute->
            NonResident.InitalizedDataSize <
                NewDataSize
        ? TargetAttribute->
              NonResident.InitalizedDataSize
        : NewDataSize;

    if (NewDataSize == OldDataSize &&
        RequiredClusters == OldLogicalClusters)
    {
        Status = STATUS_SUCCESS;
        goto Done;
    }

    if (RequiredClusters > OldLogicalClusters)
    {
        for (PDataRun Run = ExistingRuns;
             Run;
             Run = Run->NextRun)
        {
            Status = AppendLogicalRun(
                &ExpandedRuns,
                &ExpandedTail,
                Run->IsSparse,
                Run->LCN,
                Run->Length);
            if (!NT_SUCCESS(Status))
                goto Done;
        }
        Status = AppendLogicalRun(
            &ExpandedRuns,
            &ExpandedTail,
            TRUE,
            0,
            RequiredClusters -
                OldLogicalClusters);
        if (!NT_SUCCESS(Status))
            goto Done;
        ResultRuns = ExpandedRuns;
    }
    else if (RequiredClusters < OldLogicalClusters)
    {
        Status = SplitRunList(
            ExistingRuns,
            RequiredClusters,
            &RetainedRuns,
            &ReleasedRuns);
        if (!NT_SUCCESS(Status))
            goto Done;
        ResultRuns = RetainedRuns;
    }
    else
    {
        ResultRuns = ExistingRuns;
    }

    if (RequiredClusters != 0)
    {
        for (PDataRun Run = ResultRuns;
             Run;
             Run = Run->NextRun)
        {
            if (!Run->IsSparse)
            {
                if (ResultPhysicalClusters >
                    ~(ULONGLONG)0 - Run->Length)
                {
                    Status = STATUS_FILE_TOO_LARGE;
                    goto Done;
                }
                ResultPhysicalClusters +=
                    Run->Length;
            }
        }
        if (ResultPhysicalClusters >
            ~(ULONGLONG)0 / ClusterSize)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Done;
        }
        PhysicalBytes =
            ResultPhysicalClusters * ClusterSize;
    }

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!RecordBackup)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(RecordBackup,
                  Data,
                  RecordBufferSize);
    if (AttributeOwner != this)
    {
        OwnerRecordBackup =
            new(PagedPool, TAG_FILE_RECORD)
                UCHAR[AttributeOwner->
                    RecordBufferSize];
        if (!OwnerRecordBackup)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }
        RtlCopyMemory(
            OwnerRecordBackup,
            AttributeOwner->Data,
            AttributeOwner->RecordBufferSize);
    }

    if (RequiredClusters == 0)
    {
        if (TargetAttribute->
                NonResident.LastVCN + 1 <
            OldLogicalClusters)
        {
            Status = SplitRunList(
                ExistingRuns,
                1,
                &CollapseRuns,
                &CollapseTail);
            if (!NT_SUCCESS(Status))
                goto Restore;
            Status = ReplaceNonResidentMappingPairs(
                &TargetAttribute,
                CollapseRuns,
                ClusterSize,
                0,
                0,
                &MappingUpdate,
                &ReplacementOwner);
            if (!NT_SUCCESS(Status))
                goto Restore;
        }
        Status = (ReplacementOwner
                      ? ReplacementOwner
                      : AttributeOwner)->
            ConvertNonResidentToResidentEmpty(
            TargetAttribute);
        if (!NT_SUCCESS(Status))
            goto Restore;

        if (Unnamed)
        {
            Standard->FilePermissions &=
                ~FILE_PERM_SPARSE;
        }
    }
    else
    {
        Status = ReplaceNonResidentMappingPairs(
            &TargetAttribute,
            ResultRuns,
            NewAllocatedSize,
            NewDataSize,
            NewInitializedSize,
            &MappingUpdate);
        if (!NT_SUCCESS(Status))
            goto Restore;
    }

    if (Unnamed)
    {
        FileNameFields =
            NTFS_FILE_NAME_UPDATE_SIZES;
        if (RequiredClusters == 0)
        {
            FileNameFields |=
                NTFS_FILE_NAME_UPDATE_STORAGE_FLAGS;
        }
        Status = SynchronizeFileNameInformation(
            FileNameFields,
            PhysicalBytes,
            NewDataSize,
            0,
            0,
            0);
        if (!NT_SUCCESS(Status))
            goto Restore;
    }

    Status = PrepareAutomaticTimestamps(
        AutomaticTimestampFieldsForAttribute(
            TypeData,
            NewDataSize != OldDataSize),
        NULL);
    if (!NT_SUCCESS(Status))
        goto Restore;

    if (MappingUpdate)
    {
        Status = CommitNonResidentMappingUpdate(
            &MappingUpdate);
    }
    else
    {
        if (AttributeOwner != this)
        {
            OwnerWriteAttempted = TRUE;
            Status = DiskVolume->MFT->
                WriteFileRecordToMFT(
                    AttributeOwner);
        }
        else
        {
            Status = STATUS_SUCCESS;
        }
        if (NT_SUCCESS(Status))
        {
            Status = DiskVolume->MFT->
                WriteFileRecordToMFT(this);
        }
    }
    if (!NT_SUCCESS(Status))
        goto Restore;

    ClearDataRunCache();
    AttributeOwner->ClearDataRunCache();
    Committed = TRUE;
    if (ReleasedRuns)
    {
        Status = DiskVolume->ReleaseClusters(
            ReleasedRuns);
    }
    goto Done;

Restore:
    if (!Committed)
    {
        AbortNonResidentMappingUpdate(
            &MappingUpdate);
        if (OwnerRecordBackup)
        {
            RtlCopyMemory(
                AttributeOwner->Data,
                OwnerRecordBackup,
                AttributeOwner->RecordBufferSize);
            AttributeOwner->Header =
                reinterpret_cast<PFileRecordHeader>(
                    AttributeOwner->Data);
            AttributeOwner->ClearDataRunCache();
            if (OwnerWriteAttempted)
            {
                (void)DiskVolume->MFT->
                    WriteFileRecordToMFT(
                        AttributeOwner);
            }
        }
        RtlCopyMemory(Data,
                      RecordBackup,
                      RecordBufferSize);
        Header =
            reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();
    }

Done:
    FreeDataRun(CollapseTail);
    FreeDataRun(CollapseRuns);
    FreeDataRun(ReleasedRuns);
    FreeDataRun(RetainedRuns);
    FreeDataRun(ExpandedRuns);
    FreeDataRun(ExistingRuns);
    delete[] OwnerRecordBackup;
    delete[] RecordBackup;
    return Status;
}

NTSTATUS
FileRecord::ResizeNonResidentStream(
    _In_ PAttribute TargetAttribute,
    _In_ ULONGLONG NewDataSize,
    _In_ ULONGLONG RequestedAllocationSize)
{
    PDataRun ExistingRuns;
    PDataRun AddedRuns = NULL;
    PDataRun CombinedRuns = NULL;
    PDataRun RetainedRuns = NULL;
    PDataRun ReleasedRuns = NULL;
    PDataRun CollapseRuns = NULL;
    PDataRun CollapseTail = NULL;
    PNonResidentMappingUpdate MappingUpdate = NULL;
    PFileRecord ReplacementOwner = NULL;
    PUCHAR RecordBackup = NULL;
    ULONGLONG ClusterSize;
    ULONGLONG AllocationTarget;
    ULONGLONG ExistingRunBytes;
    ULONGLONG ExistingClusters;
    ULONGLONG RequiredClusters;
    ULONGLONG NewAllocatedSize;
    ULONGLONG NewInitializedSize;
    ULONGLONG OldDataSize;
    ULONGLONG PreferredLCN;
    ULONG ChangedClusters;
    ULONG MaxRuns;
    NTSTATUS Status;

    Status = ValidateAttributeForUpdate(TargetAttribute,
                                        TRUE,
                                        NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if ((TargetAttribute->AttributeType != TypeData &&
         TargetAttribute->AttributeType != TypeEA) ||
        TargetAttribute->Flags != 0 ||
        TargetAttribute->NonResident.FirstVCN != 0 ||
        TargetAttribute->NonResident.InitalizedDataSize >
            TargetAttribute->NonResident.DataSize ||
        TargetAttribute->NonResident.DataSize >
            TargetAttribute->NonResident.AllocatedSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    ClusterSize = BytesPerCluster(DiskVolume);
    if (ClusterSize == 0 ||
        TargetAttribute->NonResident.AllocatedSize == 0 ||
        TargetAttribute->NonResident.AllocatedSize %
            ClusterSize != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    AllocationTarget =
        RequestedAllocationSize > NewDataSize
        ? RequestedAllocationSize
        : NewDataSize;
    if (AllocationTarget >
        ~(ULONGLONG)0 - (ClusterSize - 1))
    {
        return STATUS_FILE_TOO_LARGE;
    }

    ExistingRuns = GetCachedDataRuns(TargetAttribute);
    if (!ExistingRuns)
        return STATUS_FILE_CORRUPT_ERROR;
    Status = GetRunBytes(DiskVolume,
                         ExistingRuns,
                         &ExistingRunBytes);
    if (!NT_SUCCESS(Status) ||
        ExistingRunBytes !=
            TargetAttribute->NonResident.AllocatedSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    ExistingClusters = ExistingRunBytes / ClusterSize;
    OldDataSize =
        TargetAttribute->NonResident.DataSize;
    RequiredClusters = AllocationTarget == 0
        ? 0
        : (AllocationTarget + ClusterSize - 1) /
            ClusterSize;
    if (RequiredClusters >
            DiskVolume->ClustersInVolume ||
        RequiredClusters > MAXULONG)
    {
        return STATUS_DISK_FULL;
    }
    NewAllocatedSize = RequiredClusters * ClusterSize;
    if (NewDataSize ==
            TargetAttribute->NonResident.DataSize &&
        NewAllocatedSize ==
            TargetAttribute->NonResident.AllocatedSize)
    {
        return STATUS_SUCCESS;
    }
    NewInitializedSize =
        TargetAttribute->NonResident.InitalizedDataSize <
            NewDataSize
        ? TargetAttribute->NonResident.InitalizedDataSize
        : NewDataSize;
    MaxRuns = RecordBufferSize / 3;
    if (MaxRuns == 0)
        MaxRuns = 1;

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!RecordBackup)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(RecordBackup, Data, RecordBufferSize);

    if (RequiredClusters > ExistingClusters)
    {
        if (RequiredClusters - ExistingClusters >
            MAXULONG)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Done;
        }
        ChangedClusters =
            (ULONG)(RequiredClusters -
                    ExistingClusters);
        PreferredLCN = PreferredLCNAfterRuns(
            ExistingRuns,
            DiskVolume->ClustersInVolume);
        Status = DiskVolume->AllocateClusters(
            PreferredLCN,
            ChangedClusters,
            MaxRuns,
            &AddedRuns);
        if (!NT_SUCCESS(Status))
            goto Done;

        Status = CombineRunLists(ExistingRuns,
                                 AddedRuns,
                                 &CombinedRuns);
        if (!NT_SUCCESS(Status))
            goto Rollback;
        Status = ReplaceNonResidentMappingPairs(
            &TargetAttribute,
            CombinedRuns,
            NewAllocatedSize,
            NewDataSize,
            NewInitializedSize,
            &MappingUpdate);
        if (!NT_SUCCESS(Status))
            goto Rollback;
    }
    else if (RequiredClusters < ExistingClusters)
    {
        Status = SplitRunList(ExistingRuns,
                              RequiredClusters,
                              &RetainedRuns,
                              &ReleasedRuns);
        if (!NT_SUCCESS(Status))
            goto Rollback;

        if (RequiredClusters == 0)
        {
            if (TargetAttribute->
                    NonResident.LastVCN + 1 <
                ExistingClusters)
            {
                Status = SplitRunList(
                    ExistingRuns,
                    1,
                    &CollapseRuns,
                    &CollapseTail);
                if (NT_SUCCESS(Status))
                {
                    Status =
                        ReplaceNonResidentMappingPairs(
                            &TargetAttribute,
                            CollapseRuns,
                            ClusterSize,
                            0,
                            0,
                            &MappingUpdate,
                            &ReplacementOwner);
                }
            }
            if (NT_SUCCESS(Status))
                Status =
                    (ReplacementOwner
                         ? ReplacementOwner
                         : this)->
                        ConvertNonResidentToResidentEmpty(
                            TargetAttribute);
        }
        else
        {
            Status = ReplaceNonResidentMappingPairs(
                &TargetAttribute,
                RetainedRuns,
                NewAllocatedSize,
                NewDataSize,
                NewInitializedSize,
                &MappingUpdate);
        }
        if (!NT_SUCCESS(Status))
            goto Rollback;
    }
    else
    {
        TargetAttribute->NonResident.DataSize =
            NewDataSize;
        TargetAttribute->NonResident.InitalizedDataSize =
            NewInitializedSize;
    }

    if (TargetAttribute->AttributeType == TypeData &&
        TargetAttribute->NameLength == 0)
    {
        Status = SynchronizeFileNameSizes(
            NewAllocatedSize,
            NewDataSize);
        if (!NT_SUCCESS(Status))
            goto Rollback;
    }

    Status = PrepareAutomaticTimestamps(
        AutomaticTimestampFieldsForAttribute(
            (AttributeType)
                TargetAttribute->AttributeType,
            NewDataSize != OldDataSize),
        NULL);
    if (!NT_SUCCESS(Status))
        goto Rollback;

    Status = MappingUpdate
        ? CommitNonResidentMappingUpdate(
            &MappingUpdate)
        : DiskVolume->MFT->WriteFileRecordToMFT(
            this);
    if (!NT_SUCCESS(Status))
        goto Rollback;

    if (ReleasedRuns)
        Status = DiskVolume->ReleaseClusters(
            ReleasedRuns);
    goto Done;

Rollback:
    AbortNonResidentMappingUpdate(
        &MappingUpdate);
    RtlCopyMemory(Data, RecordBackup, RecordBufferSize);
    Header = reinterpret_cast<PFileRecordHeader>(Data);
    ClearDataRunCache();
    if (AddedRuns)
        (void)DiskVolume->ReleaseClusters(AddedRuns);

Done:
    FreeDataRun(CollapseTail);
    FreeDataRun(CollapseRuns);
    FreeDataRun(ReleasedRuns);
    FreeDataRun(RetainedRuns);
    FreeDataRun(CombinedRuns);
    FreeDataRun(AddedRuns);
    delete[] RecordBackup;
    return Status;
}

NTSTATUS
FileRecord::ExtendNonResidentData(
    _In_ PAttribute TargetAttribute,
    _In_ PUCHAR Buffer,
    _In_ ULONG Length,
    _In_ ULONGLONG Offset)
{
    PDataRun ExistingRuns;
    PDataRun AddedRuns = NULL;
    PDataRun CombinedRuns = NULL;
    PNonResidentMappingUpdate MappingUpdate = NULL;
    PUCHAR RecordBackup = NULL;
    ULONGLONG ExistingRunBytes;
    ULONGLONG ClusterSize;
    ULONGLONG EndOffset;
    ULONGLONG NewAllocatedSize;
    ULONGLONG NewDataSize;
    ULONGLONG NewInitializedSize;
    ULONGLONG OldInitializedSize;
    ULONGLONG PreferredLCN;
    ULONGLONG RequiredClusters;
    ULONGLONG ExistingClusters;
    ULONG AddedClusterCount;
    ULONG MaxRuns;
    NTSTATUS Status;

    if (!TargetAttribute ||
        !TargetAttribute->IsNonResident ||
        (!Buffer && Length != 0) ||
        Length == 0 ||
        Offset > ~(ULONGLONG)0 - Length)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Status = ValidateAttributeForUpdate(TargetAttribute,
                                        TRUE,
                                        NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    if (TargetAttribute->Flags != 0 ||
        TargetAttribute->NonResident.FirstVCN != 0 ||
        TargetAttribute->NonResident.InitalizedDataSize >
            TargetAttribute->NonResident.DataSize ||
        TargetAttribute->NonResident.DataSize >
            TargetAttribute->NonResident.AllocatedSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    OldInitializedSize =
        TargetAttribute->
            NonResident.InitalizedDataSize;

    ClusterSize = BytesPerCluster(DiskVolume);
    if (ClusterSize == 0 ||
        TargetAttribute->NonResident.AllocatedSize %
            ClusterSize != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }
    EndOffset = Offset + Length;
    if (EndOffset <=
        TargetAttribute->NonResident.AllocatedSize)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ExistingRuns = GetCachedDataRuns(TargetAttribute);
    if (!ExistingRuns)
        return STATUS_FILE_CORRUPT_ERROR;
    Status = GetRunBytes(DiskVolume,
                         ExistingRuns,
                         &ExistingRunBytes);
    if (!NT_SUCCESS(Status) ||
        ExistingRunBytes !=
            TargetAttribute->NonResident.AllocatedSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    ExistingClusters = ExistingRunBytes / ClusterSize;
    RequiredClusters =
        EndOffset / ClusterSize +
        (EndOffset % ClusterSize != 0);
    if (RequiredClusters <= ExistingClusters ||
        RequiredClusters - ExistingClusters > MAXULONG)
    {
        return STATUS_FILE_TOO_LARGE;
    }
    AddedClusterCount =
        (ULONG)(RequiredClusters - ExistingClusters);
    NewAllocatedSize = RequiredClusters * ClusterSize;
    NewDataSize =
        EndOffset >
            TargetAttribute->NonResident.DataSize
        ? EndOffset
        : TargetAttribute->NonResident.DataSize;
    NewInitializedSize =
        EndOffset >
            TargetAttribute->NonResident.InitalizedDataSize
        ? EndOffset
        : TargetAttribute->NonResident.InitalizedDataSize;
    PreferredLCN = PreferredLCNAfterRuns(
        ExistingRuns,
        DiskVolume->ClustersInVolume);
    MaxRuns = RecordBufferSize / 3;
    if (MaxRuns == 0)
        MaxRuns = 1;

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!RecordBackup)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(RecordBackup, Data, RecordBufferSize);

    Status = DiskVolume->AllocateClusters(
        PreferredLCN,
        AddedClusterCount,
        MaxRuns,
        &AddedRuns);
    if (!NT_SUCCESS(Status))
        goto Done;

    Status = CombineRunLists(ExistingRuns,
                             AddedRuns,
                             &CombinedRuns);
    if (!NT_SUCCESS(Status))
        goto Rollback;

    Status = ReplaceNonResidentMappingPairs(
        &TargetAttribute,
        CombinedRuns,
        NewAllocatedSize,
        NewDataSize,
        NewInitializedSize,
        &MappingUpdate);
    if (!NT_SUCCESS(Status))
        goto Rollback;

    if (Offset > OldInitializedSize)
    {
        Status = ZeroRunBytes(
            DiskVolume,
            CombinedRuns,
            OldInitializedSize,
            Offset - OldInitializedSize);
        if (!NT_SUCCESS(Status))
            goto Rollback;
    }

    Status = WriteRunBytes(DiskVolume,
                           CombinedRuns,
                           Offset,
                           Buffer,
                           Length);
    if (!NT_SUCCESS(Status))
        goto Rollback;

    if (TargetAttribute->AttributeType == TypeData &&
        TargetAttribute->NameLength == 0)
    {
        Status = SynchronizeFileNameSizes(NewAllocatedSize,
                                          NewDataSize);
        if (!NT_SUCCESS(Status))
            goto Rollback;
    }

    Status = PrepareAutomaticTimestamps(
        AutomaticTimestampFieldsForAttribute(
            (AttributeType)
                TargetAttribute->AttributeType,
            TRUE),
        NULL);
    if (!NT_SUCCESS(Status))
        goto Rollback;

    Status = MappingUpdate
        ? CommitNonResidentMappingUpdate(
            &MappingUpdate)
        : DiskVolume->MFT->
            WriteFileRecordToMFT(this);
    if (NT_SUCCESS(Status))
        goto Done;

Rollback:
    AbortNonResidentMappingUpdate(
        &MappingUpdate);
    RtlCopyMemory(Data, RecordBackup, RecordBufferSize);
    Header = reinterpret_cast<PFileRecordHeader>(Data);
    ClearDataRunCache();
    if (AddedRuns)
        (void)DiskVolume->ReleaseClusters(AddedRuns);

Done:
    FreeDataRun(CombinedRuns);
    FreeDataRun(AddedRuns);
    delete[] RecordBackup;
    return Status;
}

NTSTATUS
FileRecord::WriteSparseData(
    _In_ PAttribute TargetAttribute,
    _In_ PUCHAR Buffer,
    _In_ PULONG Length,
    _In_ ULONGLONG Offset)
{
    PAttribute StandardAttribute;
    PFileRecord AttributeOwner;
    PStandardInformationEx Standard;
    PDataRun ExistingRuns = NULL;
    PDataRun AllocatedRuns = NULL;
    PDataRun CombinedRuns = NULL;
    PDataRun CombinedTail = NULL;
    PDataRun AllocatedCursor = NULL;
    PNonResidentMappingUpdate MappingUpdate = NULL;
    PUCHAR RecordBackup = NULL;
    PUCHAR OwnerRecordBackup = NULL;
    ULONGLONG ClusterSize;
    ULONGLONG EndOffset;
    ULONGLONG WriteStartCluster;
    ULONGLONG WriteEndCluster;
    ULONGLONG OldLogicalClusters;
    ULONGLONG NewLogicalClusters;
    ULONGLONG PhysicalClusters = 0;
    ULONGLONG SparseClusters = 0;
    ULONGLONG LogicalCluster = 0;
    ULONGLONG AllocatedOffset = 0;
    ULONGLONG NewAllocatedSize;
    ULONGLONG NewDataSize;
    ULONGLONG NewInitializedSize;
    ULONGLONG OldInitializedSize;
    ULONGLONG PhysicalBytes;
    ULONG MaxRuns;
    BOOLEAN AttributeSparse;
    BOOLEAN Committed = FALSE;
    BOOLEAN OwnerWriteAttempted = FALSE;
    NTSTATUS Status = STATUS_SUCCESS;

    if (!TargetAttribute || !Length ||
        (!Buffer && *Length != 0) ||
        *Length == 0 ||
        !TargetAttribute->IsNonResident ||
        Offset > ~(ULONGLONG)0 - *Length)
    {
        return STATUS_INVALID_PARAMETER;
    }
    AttributeOwner =
        GetAttributeOwner(TargetAttribute);
    if (!AttributeOwner ||
        (AttributeOwner != this &&
         (TargetAttribute->AttributeType !=
              TypeData ||
          TargetAttribute->NameLength == 0)))
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    Status = AttributeOwner->
        ValidateAttributeForUpdate(
        TargetAttribute,
        TRUE,
        NULL);
    if (!NT_SUCCESS(Status))
        return Status;
    AttributeSparse =
        !!(TargetAttribute->Flags & ATTR_SPARSE);
    OldInitializedSize =
        TargetAttribute->
            NonResident.InitalizedDataSize;
    if ((TargetAttribute->Flags & ~ATTR_SPARSE) != 0 ||
        TargetAttribute->NonResident.FirstVCN != 0 ||
        (AttributeSparse &&
         (TargetAttribute->
              NonResident.CompressionUnitSize != 4 ||
          TargetAttribute->Length < 0x48 ||
          TargetAttribute->
              NonResident.DataRunsOffset < 0x48)) ||
        (!AttributeSparse &&
         TargetAttribute->
             NonResident.CompressionUnitSize != 0) ||
        TargetAttribute->NonResident.InitalizedDataSize >
            TargetAttribute->NonResident.DataSize ||
        TargetAttribute->NonResident.DataSize >
            TargetAttribute->NonResident.AllocatedSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    ClusterSize = BytesPerCluster(DiskVolume);
    if (ClusterSize == 0 ||
        TargetAttribute->NonResident.AllocatedSize == 0 ||
        TargetAttribute->NonResident.AllocatedSize %
            ClusterSize != 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    EndOffset = Offset + *Length;
    if (EndOffset >
        ~(ULONGLONG)0 - (ClusterSize - 1))
    {
        return STATUS_FILE_TOO_LARGE;
    }
    WriteStartCluster = Offset / ClusterSize;
    WriteEndCluster =
        (EndOffset + ClusterSize - 1) /
        ClusterSize;
    OldLogicalClusters =
        TargetAttribute->NonResident.AllocatedSize /
        ClusterSize;
    NewLogicalClusters =
        WriteEndCluster > OldLogicalClusters
        ? WriteEndCluster
        : OldLogicalClusters;
    if (NewLogicalClusters == 0 ||
        NewLogicalClusters >
            ~(ULONGLONG)0 / ClusterSize)
    {
        return STATUS_FILE_TOO_LARGE;
    }
    NewAllocatedSize =
        NewLogicalClusters * ClusterSize;
    NewDataSize =
        EndOffset >
            TargetAttribute->NonResident.DataSize
        ? EndOffset
        : TargetAttribute->NonResident.DataSize;
    NewInitializedSize =
        EndOffset >
            TargetAttribute->
                NonResident.InitalizedDataSize
        ? EndOffset
        : TargetAttribute->
            NonResident.InitalizedDataSize;

    ExistingRuns = FindNonResidentData(
        TargetAttribute);
    if (!ExistingRuns)
        return STATUS_FILE_CORRUPT_ERROR;

    for (PDataRun Run = ExistingRuns;
         Run;
         Run = Run->NextRun)
    {
        ULONGLONG RunEnd;
        ULONGLONG IntersectionStart;
        ULONGLONG IntersectionEnd;

        if (Run->Length == 0 ||
            LogicalCluster >
                ~(ULONGLONG)0 - Run->Length)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        RunEnd = LogicalCluster + Run->Length;
        if (RunEnd > OldLogicalClusters)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        if (Run->IsSparse)
        {
            if (!AttributeSparse)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
            IntersectionStart =
                LogicalCluster > WriteStartCluster
                ? LogicalCluster
                : WriteStartCluster;
            IntersectionEnd =
                RunEnd < WriteEndCluster
                ? RunEnd
                : WriteEndCluster;
            if (IntersectionEnd >
                IntersectionStart)
            {
                if (SparseClusters >
                    ~(ULONGLONG)0 -
                        (IntersectionEnd -
                         IntersectionStart))
                {
                    Status = STATUS_FILE_TOO_LARGE;
                    goto Done;
                }
                SparseClusters +=
                    IntersectionEnd -
                    IntersectionStart;
            }
        }
        else
        {
            if (PhysicalClusters >
                ~(ULONGLONG)0 - Run->Length)
            {
                Status = STATUS_FILE_TOO_LARGE;
                goto Done;
            }
            PhysicalClusters += Run->Length;
        }
        LogicalCluster = RunEnd;
    }
    if (LogicalCluster != OldLogicalClusters)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    if (WriteEndCluster > OldLogicalClusters)
    {
        ULONGLONG TailStart =
            WriteStartCluster > OldLogicalClusters
            ? WriteStartCluster
            : OldLogicalClusters;

        if (WriteEndCluster > TailStart)
        {
            if (SparseClusters >
                ~(ULONGLONG)0 -
                    (WriteEndCluster - TailStart))
            {
                Status = STATUS_FILE_TOO_LARGE;
                goto Done;
            }
            SparseClusters +=
                WriteEndCluster - TailStart;
        }
    }

    if (PhysicalClusters >
        ~(ULONGLONG)0 / ClusterSize)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }
    if ((AttributeSparse &&
         PhysicalClusters * ClusterSize !=
             TargetAttribute->
                 NonResident.CompressedDataSize) ||
        (!AttributeSparse &&
         PhysicalClusters != OldLogicalClusters))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }
    if (SparseClusters > MAXULONG)
    {
        Status = STATUS_FILE_TOO_LARGE;
        goto Done;
    }
    if (PhysicalClusters >
            ~(ULONGLONG)0 - SparseClusters ||
        PhysicalClusters + SparseClusters >
            ~(ULONGLONG)0 / ClusterSize)
    {
        Status = STATUS_FILE_TOO_LARGE;
        goto Done;
    }
    PhysicalBytes =
        (PhysicalClusters + SparseClusters) *
        ClusterSize;

    if (TargetAttribute->AttributeType == TypeData &&
        TargetAttribute->NameLength == 0)
    {
        Status = GetStandardInformationForUpdate(
            &StandardAttribute,
            &Standard);
        if (!NT_SUCCESS(Status))
            goto Done;
        UNREFERENCED_PARAMETER(StandardAttribute);
        if (!(Standard->FilePermissions &
              FILE_PERM_SPARSE))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
    }

    if (SparseClusters != 0)
    {
        MaxRuns = RecordBufferSize / 3;
        if (MaxRuns == 0)
            MaxRuns = 1;
        Status = DiskVolume->AllocateClusters(
            0,
            (ULONG)SparseClusters,
            MaxRuns,
            &AllocatedRuns);
        if (!NT_SUCCESS(Status))
            goto Done;

        Status = ZeroRunBytes(
            DiskVolume,
            AllocatedRuns,
            0,
            SparseClusters * ClusterSize);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    AllocatedCursor = AllocatedRuns;
    LogicalCluster = 0;
    for (PDataRun Run = ExistingRuns;
         Run;
         Run = Run->NextRun)
    {
        Status = AppendSparseWriteSegment(
            Run,
            LogicalCluster,
            WriteStartCluster,
            WriteEndCluster,
            &AllocatedCursor,
            &AllocatedOffset,
            &CombinedRuns,
            &CombinedTail);
        if (!NT_SUCCESS(Status))
            goto Done;
        LogicalCluster += Run->Length;
    }
    if (NewLogicalClusters > OldLogicalClusters)
    {
        DataRun SparseTail = {};

        SparseTail.IsSparse = TRUE;
        SparseTail.Length =
            NewLogicalClusters -
            OldLogicalClusters;
        Status = AppendSparseWriteSegment(
            &SparseTail,
            OldLogicalClusters,
            WriteStartCluster,
            WriteEndCluster,
            &AllocatedCursor,
            &AllocatedOffset,
            &CombinedRuns,
            &CombinedTail);
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    if (AllocatedCursor ||
        AllocatedOffset != 0)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Done;
    }

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD)
            UCHAR[RecordBufferSize];
    if (!RecordBackup)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(RecordBackup,
                  Data,
                  RecordBufferSize);
    if (AttributeOwner != this)
    {
        OwnerRecordBackup =
            new(PagedPool, TAG_FILE_RECORD)
                UCHAR[AttributeOwner->
                    RecordBufferSize];
        if (!OwnerRecordBackup)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }
        RtlCopyMemory(
            OwnerRecordBackup,
            AttributeOwner->Data,
            AttributeOwner->RecordBufferSize);
    }

    Status = ReplaceNonResidentMappingPairs(
        &TargetAttribute,
        CombinedRuns,
        NewAllocatedSize,
        NewDataSize,
        NewInitializedSize,
        &MappingUpdate);
    if (!NT_SUCCESS(Status))
        goto Restore;

    if (Offset > OldInitializedSize)
    {
        Status = ZeroLogicalAllocatedBytes(
            DiskVolume,
            CombinedRuns,
            OldInitializedSize,
            Offset - OldInitializedSize);
        if (!NT_SUCCESS(Status))
            goto Restore;
    }

    Status = WriteLogicalRunBytes(
        DiskVolume,
        CombinedRuns,
        Offset,
        Buffer,
        *Length);
    if (!NT_SUCCESS(Status))
        goto Restore;

    if (((TargetAttribute->Flags & ATTR_SPARSE) &&
         TargetAttribute->
             NonResident.CompressedDataSize !=
                 PhysicalBytes) ||
        (!(TargetAttribute->Flags & ATTR_SPARSE) &&
         TargetAttribute->
             NonResident.AllocatedSize !=
                 PhysicalBytes))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Restore;
    }

    if (TargetAttribute->AttributeType == TypeData &&
        TargetAttribute->NameLength == 0)
    {
        Status = SynchronizeFileNameSizes(
            PhysicalBytes,
            NewDataSize);
        if (!NT_SUCCESS(Status))
            goto Restore;
    }

    Status = PrepareAutomaticTimestamps(
        AutomaticTimestampFieldsForAttribute(
            (AttributeType)
                TargetAttribute->AttributeType,
            TRUE),
        NULL);
    if (!NT_SUCCESS(Status))
        goto Restore;

    if (MappingUpdate)
    {
        Status = CommitNonResidentMappingUpdate(
            &MappingUpdate);
    }
    else
    {
        if (AttributeOwner != this)
        {
            OwnerWriteAttempted = TRUE;
            Status = DiskVolume->MFT->
                WriteFileRecordToMFT(
                    AttributeOwner);
        }
        else
        {
            Status = STATUS_SUCCESS;
        }
        if (NT_SUCCESS(Status))
        {
            Status = DiskVolume->MFT->
                WriteFileRecordToMFT(this);
        }
    }
    if (!NT_SUCCESS(Status))
        goto Restore;

    ClearDataRunCache();
    AttributeOwner->ClearDataRunCache();
    Committed = TRUE;
    goto Done;

Restore:
    if (!Committed)
    {
        AbortNonResidentMappingUpdate(
            &MappingUpdate);
        if (OwnerRecordBackup)
        {
            RtlCopyMemory(
                AttributeOwner->Data,
                OwnerRecordBackup,
                AttributeOwner->RecordBufferSize);
            AttributeOwner->Header =
                reinterpret_cast<PFileRecordHeader>(
                    AttributeOwner->Data);
            AttributeOwner->ClearDataRunCache();
            if (OwnerWriteAttempted)
            {
                (void)DiskVolume->MFT->
                    WriteFileRecordToMFT(
                        AttributeOwner);
            }
        }
        RtlCopyMemory(Data,
                      RecordBackup,
                      RecordBufferSize);
        Header =
            reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();
    }

Done:
    if (!Committed && AllocatedRuns)
        (void)DiskVolume->ReleaseClusters(
            AllocatedRuns);
    FreeDataRun(CombinedRuns);
    FreeDataRun(AllocatedRuns);
    FreeDataRun(ExistingRuns);
    delete[] OwnerRecordBackup;
    delete[] RecordBackup;
    return Status;
}

/*
 * Write into an LZNT1-compressed stream by rebuilding the complete unit
 * grid: the current plaintext is materialized, patched, and recompressed
 * unit by unit. A unit that saves at least one cluster stores its
 * compressed form followed by a sparse tail; a unit that does not shrink
 * stores all sixteen clusters raw, which is how readers distinguish the
 * two. New clusters and their data become durable before the one-record
 * mapping swap commits, and the old clusters are released only
 * afterwards, so every intermediate state stays readable. The supported
 * shape is deliberately narrow: a base-record attribute flagged exactly
 * ATTR_COMPRESSED with a 16-cluster unit and a bounded stream size;
 * everything else keeps the previous unsupported result.
 */
#define NTFS_COMPRESSED_WRITE_LIMIT (4ULL * 1024 * 1024)

NTSTATUS
FileRecord::WriteCompressedData(_In_ PAttribute TargetAttribute,
                                _In_ PUCHAR Buffer,
                                _In_ PULONG Length,
                                _In_ ULONGLONG Offset)
{
    PDataRun OldPhysicalRuns = NULL;
    PDataRun NewPhysicalRuns = NULL;
    PDataRun NewPhysicalTail = NULL;
    PDataRun CompositeRuns = NULL;
    PDataRun CompositeTail = NULL;
    PDataRun ExistingRuns;
    PNonResidentMappingUpdate MappingUpdate = NULL;
    PUCHAR Plain = NULL;
    PUCHAR Stored = NULL;
    PUCHAR RecordBackup = NULL;
    ULONGLONG ClusterSize;
    ULONGLONG UnitBytes;
    ULONGLONG OldDataSize;
    ULONGLONG NewDataSize;
    ULONGLONG EndOffset;
    ULONGLONG UnitCount;
    ULONGLONG PhysicalBytes = 0;
    ULONGLONG PlainBytes;
    ULONG Remaining;
    BOOLEAN Committed = FALSE;
    NTSTATUS Status;
    NTSTATUS ReleaseStatus;

    if (!TargetAttribute || !Buffer || !Length || *Length == 0)
        return STATUS_INVALID_PARAMETER;
    ClusterSize = BytesPerCluster(DiskVolume);
    UnitBytes = ClusterSize << 4;
    OldDataSize = TargetAttribute->NonResident.DataSize;
    if (Offset > ~(ULONGLONG)0 - *Length)
        return STATUS_FILE_TOO_LARGE;
    EndOffset = Offset + *Length;
    NewDataSize = EndOffset > OldDataSize
        ? EndOffset
        : OldDataSize;

    if (TargetAttribute->AttributeType != TypeData ||
        !TargetAttribute->IsNonResident ||
        TargetAttribute->Flags != ATTR_COMPRESSED ||
        TargetAttribute->NonResident.CompressionUnitSize != 4 ||
        TargetAttribute->NonResident.FirstVCN != 0 ||
        GetAttributeOwner(TargetAttribute) != this ||
        FindAttributeInRecord(TypeAttributeList, NULL, NULL) ||
        ClusterSize == 0 ||
        NewDataSize > NTFS_COMPRESSED_WRITE_LIMIT)
    {
        return STATUS_NOT_IMPLEMENTED;
    }

    ExistingRuns = GetCachedDataRuns(TargetAttribute);
    if (!ExistingRuns)
        return STATUS_FILE_CORRUPT_ERROR;
    Status = CloneAllocatedRuns(ExistingRuns, &OldPhysicalRuns);
    if (!NT_SUCCESS(Status))
        return Status;

    UnitCount = (NewDataSize + UnitBytes - 1) / UnitBytes;
    PlainBytes = UnitCount * UnitBytes;
    Plain = new(PagedPool, TAG_NTFS) UCHAR[(ULONG)PlainBytes];
    Stored = new(PagedPool, TAG_NTFS) UCHAR[(ULONG)UnitBytes];
    if (!Plain || !Stored)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlZeroMemory(Plain, (ULONG)PlainBytes);

    if (OldDataSize != 0)
    {
        Remaining = (ULONG)OldDataSize;
        Status = CopyData(TypeData,
                          NULL,
                          Plain,
                          &Remaining,
                          0);
        if (!NT_SUCCESS(Status) || Remaining != 0)
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
    }
    RtlCopyMemory(Plain + Offset, Buffer, *Length);

    for (ULONGLONG Unit = 0; Unit < UnitCount; Unit++)
    {
        PDataRun UnitRuns = NULL;
        ULONGLONG UnitStart = Unit * UnitBytes;
        ULONG UnitPlain = (ULONG)min(UnitBytes,
                                     NewDataSize - UnitStart);
        ULONG StoredSize = 0;
        ULONG StoredClusters;
        BOOLEAN Raw = FALSE;

        Status = NtfsLznt1Compress(Plain + UnitStart,
                                   UnitPlain,
                                   Stored,
                                   (ULONG)(UnitBytes - ClusterSize),
                                   &StoredSize);
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            Raw = TRUE;
            Status = STATUS_SUCCESS;
        }
        if (!NT_SUCCESS(Status))
            goto Done;

        if (Raw)
        {
            StoredClusters = 16;
            RtlCopyMemory(Stored, Plain + UnitStart, UnitPlain);
            RtlZeroMemory(Stored + UnitPlain,
                          (ULONG)(UnitBytes - UnitPlain));
        }
        else
        {
            StoredClusters = (ULONG)
                ((StoredSize + ClusterSize - 1) / ClusterSize);
            RtlZeroMemory(Stored + StoredSize,
                          (ULONG)(StoredClusters * ClusterSize -
                                  StoredSize));
        }

        Status = DiskVolume->AllocateClusters(
            0,
            StoredClusters,
            8,
            &UnitRuns);
        if (!NT_SUCCESS(Status))
            goto Done;
        Status = WriteRunBytes(DiskVolume,
                               UnitRuns,
                               0,
                               Stored,
                               StoredClusters * ClusterSize);
        if (NT_SUCCESS(Status))
        {
            for (PDataRun Run = UnitRuns;
                 Run && NT_SUCCESS(Status);
                 Run = Run->NextRun)
            {
                Status = AppendLogicalRun(&CompositeRuns,
                                          &CompositeTail,
                                          FALSE,
                                          Run->LCN,
                                          Run->Length);
            }
        }
        if (NT_SUCCESS(Status) && StoredClusters < 16)
        {
            Status = AppendLogicalRun(&CompositeRuns,
                                      &CompositeTail,
                                      TRUE,
                                      0,
                                      16 - StoredClusters);
        }

        /* The unit chain joins the rollback list either way; on failure
         * every cluster allocated so far is released through it.
         */
        if (NewPhysicalTail)
            NewPhysicalTail->NextRun = UnitRuns;
        else
            NewPhysicalRuns = UnitRuns;
        NewPhysicalTail = UnitRuns;
        while (NewPhysicalTail && NewPhysicalTail->NextRun)
            NewPhysicalTail = NewPhysicalTail->NextRun;
        if (!NT_SUCCESS(Status))
            goto Done;
        PhysicalBytes += (ULONGLONG)StoredClusters * ClusterSize;
    }

    RecordBackup =
        new(PagedPool, TAG_FILE_RECORD) UCHAR[RecordBufferSize];
    if (!RecordBackup)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }
    RtlCopyMemory(RecordBackup, Data, RecordBufferSize);

    Status = ReplaceNonResidentMappingPairs(
        &TargetAttribute,
        CompositeRuns,
        UnitCount * UnitBytes,
        NewDataSize,
        NewDataSize,
        &MappingUpdate,
        NULL);
    if (!NT_SUCCESS(Status))
        goto Restore;
    if (TargetAttribute->NonResident.CompressedDataSize !=
        PhysicalBytes)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Restore;
    }

    if (TargetAttribute->NameLength == 0)
    {
        Status = SynchronizeFileNameSizes(PhysicalBytes,
                                          NewDataSize);
        if (!NT_SUCCESS(Status))
            goto Restore;
    }

    Status = PrepareAutomaticTimestamps(
        AutomaticTimestampFieldsForAttribute(TypeData, TRUE),
        NULL);
    if (!NT_SUCCESS(Status))
        goto Restore;

    Status = MappingUpdate
        ? CommitNonResidentMappingUpdate(&MappingUpdate)
        : DiskVolume->MFT->WriteFileRecordToMFT(this);
    if (!NT_SUCCESS(Status))
        goto Restore;

    ClearDataRunCache();
    Committed = TRUE;

    ReleaseStatus = STATUS_SUCCESS;
    if (OldPhysicalRuns)
        ReleaseStatus = DiskVolume->ReleaseClusters(OldPhysicalRuns);
    Status = ReleaseStatus;
    goto Done;

Restore:
    if (!Committed)
    {
        AbortNonResidentMappingUpdate(&MappingUpdate);
        RtlCopyMemory(Data, RecordBackup, RecordBufferSize);
        Header = reinterpret_cast<PFileRecordHeader>(Data);
        ClearDataRunCache();
    }

Done:
    if (!Committed && NewPhysicalRuns)
        (void)DiskVolume->ReleaseClusters(NewPhysicalRuns);
    FreeDataRun(OldPhysicalRuns);
    FreeDataRun(NewPhysicalRuns);
    FreeDataRun(CompositeRuns);
    delete[] RecordBackup;
    delete[] Plain;
    delete[] Stored;
    return Status;
}

NTSTATUS
FileRecord::UpdateNonResidentData(_In_ PAttribute TargetAttribute,
                                  _In_ PUCHAR Buffer,
                                  _In_ PULONG Length,
                                  _In_ ULONGLONG Offset)
{
    NTSTATUS Status;
    ULONGLONG EndOffset;
    ULONGLONG BytesInRun;
    ULONGLONG PreflightOffset;
    ULONG BytesWritten, Chunk, PreflightRemaining;
    PDataRun CurrentRun, Head;

    /*
     * Allocation and mapping-pair growth are preflighted by WriteFileData().
     * The remaining data write is still not journaled until LFS support lands.
     */

    if (!TargetAttribute || !Length ||
        (!Buffer && *Length != 0) ||
        !TargetAttribute->IsNonResident)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (*Length == 0)
        return STATUS_SUCCESS;
    if (TargetAttribute->Flags &
        (ATTR_COMPRESSION_MASK | ATTR_ENCRYPTED | ATTR_SPARSE))
    {
        return STATUS_NOT_IMPLEMENTED;
    }
    if (TargetAttribute->NonResident.FirstVCN != 0 ||
        TargetAttribute->NonResident.InitalizedDataSize >
            TargetAttribute->NonResident.DataSize ||
        TargetAttribute->NonResident.DataSize >
            TargetAttribute->NonResident.AllocatedSize ||
        Offset > ~(ULONGLONG)0 - *Length)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    EndOffset = Offset + *Length;
    if (EndOffset > TargetAttribute->NonResident.AllocatedSize)
    {
        DPRINT1("Write request exceeds the preflighted allocation!\n");
        return STATUS_INVALID_PARAMETER;
    }
    /* Runs belong to the per-record cache; they stay valid until cluster
     * (re)allocation lands, which must invalidate the cache when it does.
     */
    Head = GetCachedDataRuns(TargetAttribute);
    if (!Head)
        return STATUS_FILE_CORRUPT_ERROR;

    if (Offset >
        TargetAttribute->NonResident.InitalizedDataSize)
    {
        Status = ZeroRunBytes(
            DiskVolume,
            Head,
            TargetAttribute->NonResident.InitalizedDataSize,
            Offset -
                TargetAttribute->NonResident.InitalizedDataSize);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    /*
     * Validate the complete target range before issuing the first write.
     * This prevents a later sparse or missing run from turning a request
     * into a silent partial update.
     */
    CurrentRun = Head;
    PreflightOffset = Offset;
    PreflightRemaining = *Length;
    while (CurrentRun && PreflightRemaining != 0)
    {
        BytesInRun = GetRunSize(CurrentRun);
        if (PreflightOffset >= BytesInRun)
        {
            PreflightOffset -= BytesInRun;
        }
        else
        {
            ULONGLONG Available = BytesInRun - PreflightOffset;

            if (CurrentRun->IsSparse)
                return STATUS_NOT_IMPLEMENTED;
            Chunk = Available < PreflightRemaining
                ? (ULONG)Available
                : PreflightRemaining;
            PreflightRemaining -= Chunk;
            PreflightOffset = 0;
        }
        CurrentRun = CurrentRun->NextRun;
    }
    if (PreflightRemaining != 0)
        return STATUS_FILE_CORRUPT_ERROR;

    CurrentRun = Head;
    BytesWritten = 0;

    while (CurrentRun)
    {
        BytesInRun = GetRunSize(CurrentRun);

        if (Offset >= BytesInRun)
        {
            // Skip over this entire data run
            Offset -= BytesInRun;
        }

        else
        {
            if (CurrentRun->IsSparse)
                return STATUS_NOT_IMPLEMENTED;

            // Get data
            Chunk = (ULONG)min(
                (ULONGLONG)(*Length - BytesWritten),
                BytesInRun - Offset);
            Status = DiskVolume->WriteVolume(GetOffset(CurrentRun->LCN) + Offset,
                                             Chunk,
                                             Buffer + BytesWritten);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("Failed to write data contents!\n");
                return Status;
            }

            // Adjust bytes written
            BytesWritten += Chunk;

            // Are we done writing?
            if (BytesWritten == *Length)
                break;

            // Clear offset
            Offset = 0;
        }

        // Set up next data run
        CurrentRun = CurrentRun->NextRun;
    }

    // Check to make sure we wrote what was requested
    if (BytesWritten != *Length)
    {
        DPRINT1("Failed to write file data!\n");
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (EndOffset > TargetAttribute->NonResident.DataSize)
        TargetAttribute->NonResident.DataSize = EndOffset;
    if (EndOffset >
        TargetAttribute->NonResident.InitalizedDataSize)
    {
        TargetAttribute->NonResident.InitalizedDataSize = EndOffset;
    }

    return STATUS_SUCCESS;

}
