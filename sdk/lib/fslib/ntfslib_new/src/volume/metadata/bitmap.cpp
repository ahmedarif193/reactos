/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

/* Read $Bitmap in fixed-size chunks so we never need one huge allocation
 * for the whole volume bitmap (32MB for a 1TB volume at 4K clusters).
 */
#define BITMAP_CHUNK_SIZE 0x10000 // 64KB, always a multiple of sizeof(ULONG)

/* One persistent scratch per volume; see Volume::BitmapWorkBuffer. */
static
PUCHAR
AcquireBitmapWorkBuffer(_In_ PVolume DiskVolume)
{
    if (!DiskVolume->BitmapWorkBuffer)
    {
        DiskVolume->BitmapWorkBuffer =
            new(PagedPool, TAG_NTFS) UCHAR[BITMAP_CHUNK_SIZE];
    }
    return DiskVolume->BitmapWorkBuffer;
}


typedef struct _NTFS_BITMAP_UNDO
{
    struct _NTFS_BITMAP_UNDO* Next;
    ULONGLONG Offset;
    ULONG Length;
    PUCHAR Data;
    /* Small undos live inline; short-lived entries live on the caller's
     * stack. Two pool round trips per bit flip cost more than the flip. */
    UCHAR InlineData[32];
    BOOLEAN StackEntry;
} NTFS_BITMAP_UNDO, *PNTFS_BITMAP_UNDO;

static NTSTATUS
GetVolumeBitmap(_In_ PVolume DiskVolume,
                _Out_ PFileRecord* BitmapFile,
                _Out_ PAttribute* BitmapData)
{
    ULONGLONG RequiredBytes;
    NTSTATUS Status;

    *BitmapFile = NULL;
    *BitmapData = NULL;

    if (DiskVolume->CachedBitmapFile)
    {
        *BitmapFile = DiskVolume->CachedBitmapFile;
        *BitmapData = (PAttribute)DiskVolume->CachedBitmapData;
        return STATUS_SUCCESS;
    }

    Status = DiskVolume->MFT->GetFileAttributeFromFileRecordNumber(
        TypeData,
        NULL,
        _Bitmap,
        BitmapFile,
        BitmapData);
    if (!NT_SUCCESS(Status))
        return Status;

    RequiredBytes =
        ((ULONGLONG)DiskVolume->ClustersInVolume + 7) >> 3;
    if (!*BitmapFile ||
        !*BitmapData ||
        !(*BitmapData)->IsNonResident ||
        ((*BitmapData)->Flags &
         (ATTR_COMPRESSION_MASK | ATTR_ENCRYPTED | ATTR_SPARSE)) ||
        (*BitmapData)->NonResident.InitalizedDataSize <
            RequiredBytes ||
        (*BitmapData)->NonResident.DataSize <
            RequiredBytes)
    {
        delete *BitmapFile;
        *BitmapFile = NULL;
        *BitmapData = NULL;
        return STATUS_FILE_CORRUPT_ERROR;
    }

    DiskVolume->CachedBitmapFile = *BitmapFile;
    DiskVolume->CachedBitmapData = (struct _ATTRIBUTE*)*BitmapData;

    return STATUS_SUCCESS;
}

static NTSTATUS
ReadBitmapBytes(_In_ PFileRecord BitmapFile,
                _In_ PAttribute BitmapData,
                _Out_ PUCHAR Buffer,
                _In_ ULONG Length,
                _In_ ULONGLONG Offset)
{
    ULONG Remaining = Length;
    NTSTATUS Status;

    Status = BitmapFile->CopyData(BitmapData,
                                  Buffer,
                                  &Remaining,
                                  Offset);
    if (!NT_SUCCESS(Status))
        return Status;
    return Remaining == 0 ? STATUS_SUCCESS : STATUS_END_OF_FILE;
}

static NTSTATUS
WriteBitmapBytes(_In_ PFileRecord BitmapFile,
                 _In_ PUCHAR Buffer,
                 _In_ ULONG Length,
                 _In_ ULONGLONG Offset)
{
    LARGE_INTEGER ByteOffset;
    ULONG Remaining = Length;
    NTSTATUS Status;

    ByteOffset.QuadPart = (LONGLONG)Offset;
    Status = BitmapFile->WriteFileData(TypeData,
                                       NULL,
                                       Buffer,
                                       &Remaining,
                                       &ByteOffset);
    if (!NT_SUCCESS(Status))
        return Status;
    return Remaining == Length ? STATUS_SUCCESS : STATUS_END_OF_FILE;
}

static void
FreeBitmapUndo(_In_opt_ PNTFS_BITMAP_UNDO Undo)
{
    while (Undo)
    {
        PNTFS_BITMAP_UNDO Next = Undo->Next;

        if (Undo->Data != Undo->InlineData)
            delete[] Undo->Data;
        if (!Undo->StackEntry)
            delete Undo;
        Undo = Next;
    }
}

static void
RestoreBitmapUndo(_In_ PFileRecord BitmapFile,
                  _In_opt_ PNTFS_BITMAP_UNDO Undo)
{
    /*
     * Entries are pushed after each read and before its corresponding
     * write. Restoring in list order is therefore reverse write order and
     * correctly handles two selected runs sharing a bitmap byte.
     */
    for (; Undo; Undo = Undo->Next)
    {
        (void)WriteBitmapBytes(BitmapFile,
                               Undo->Data,
                               Undo->Length,
                               Undo->Offset);
    }
}

static NTSTATUS
SetClusterRunBits(_In_ PVolume DiskVolume,
                  _In_ PFileRecord BitmapFile,
                  _In_ PAttribute BitmapData,
                  _In_ PDataRun Runs,
                  _In_ BOOLEAN Allocate,
                  _In_ ULONGLONG ClusterLimit)
{
    PNTFS_BITMAP_UNDO Undo = NULL;
    NTFS_BITMAP_UNDO StackUndo[4];
    ULONG StackUsed = 0;
    PUCHAR Buffer = NULL;
    NTSTATUS Status = STATUS_SUCCESS;

    Buffer = AcquireBitmapWorkBuffer(DiskVolume);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    for (PDataRun Run = Runs; Run; Run = Run->NextRun)
    {
        ULONGLONG Cluster = Run->LCN;
        ULONGLONG RunEnd;

        if (Run->IsSparse ||
            Run->Length == 0 ||
            Run->LCN >= ClusterLimit ||
            Run->Length >
                ClusterLimit - Run->LCN)
        {
            Status = STATUS_INVALID_PARAMETER;
            goto Rollback;
        }
        RunEnd = Run->LCN + Run->Length;

        while (Cluster < RunEnd)
        {
            PNTFS_BITMAP_UNDO Entry;
            ULONGLONG ByteOffset = Cluster >> 3;
            ULONGLONG ChunkByteEnd =
                ALIGN_UP_BY(ByteOffset + 1,
                            (ULONGLONG)BITMAP_CHUNK_SIZE);
            ULONGLONG ChunkClusterEnd =
                min(RunEnd, ChunkByteEnd << 3);
            ULONGLONG LastByte =
                (ChunkClusterEnd - 1) >> 3;
            ULONG ByteLength =
                (ULONG)(LastByte - ByteOffset + 1);

            Status = ReadBitmapBytes(BitmapFile,
                                     BitmapData,
                                     Buffer,
                                     ByteLength,
                                     ByteOffset);
            if (!NT_SUCCESS(Status))
                goto Rollback;

            for (ULONGLONG Current = Cluster;
                 Current < ChunkClusterEnd;
                 Current++)
            {
                ULONG RelativeByte =
                    (ULONG)((Current >> 3) - ByteOffset);
                UCHAR Mask = (UCHAR)(1u << (Current & 7));
                BOOLEAN IsAllocated =
                    !!(Buffer[RelativeByte] & Mask);

                if (IsAllocated == Allocate)
                {
                    Status = Allocate
                        ? STATUS_DISK_FULL
                        : STATUS_FILE_CORRUPT_ERROR;
                    goto Rollback;
                }
            }

            if (StackUsed < RTL_NUMBER_OF(StackUndo))
            {
                Entry = &StackUndo[StackUsed++];
                RtlZeroMemory(Entry, sizeof(*Entry));
                Entry->StackEntry = TRUE;
            }
            else
            {
                Entry = new(PagedPool, TAG_NTFS) NTFS_BITMAP_UNDO();
            }
            if (!Entry)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Rollback;
            }
            Entry->Data = ByteLength <= sizeof(Entry->InlineData)
                ? Entry->InlineData
                : new(PagedPool, TAG_NTFS) UCHAR[ByteLength];
            if (!Entry->Data)
            {
                if (!Entry->StackEntry)
                    delete Entry;
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Rollback;
            }
            RtlCopyMemory(Entry->Data, Buffer, ByteLength);
            Entry->Offset = ByteOffset;
            Entry->Length = ByteLength;
            Entry->Next = Undo;
            Undo = Entry;

            for (ULONGLONG Current = Cluster;
                 Current < ChunkClusterEnd;
                 Current++)
            {
                ULONG RelativeByte =
                    (ULONG)((Current >> 3) - ByteOffset);
                UCHAR Mask = (UCHAR)(1u << (Current & 7));

                if (Allocate)
                    Buffer[RelativeByte] |= Mask;
                else
                    Buffer[RelativeByte] &= (UCHAR)~Mask;
            }

            Status = WriteBitmapBytes(BitmapFile,
                                      Buffer,
                                      ByteLength,
                                      ByteOffset);
            if (!NT_SUCCESS(Status))
                goto Rollback;

            Cluster = ChunkClusterEnd;
        }
    }

    goto Done;

Rollback:
    RestoreBitmapUndo(BitmapFile, Undo);

Done:
    FreeBitmapUndo(Undo);
    /* Buffer is the volume's shared scratch. */
    return Status;
}

static NTSTATUS
FindContiguousFreeClusters(
    _In_ PVolume DiskVolume,
    _In_ PFileRecord BitmapFile,
    _In_ PAttribute BitmapData,
    _In_ ULONGLONG StartCluster,
    _In_ ULONGLONG EndCluster,
    _In_ ULONG ClusterCount,
    _Out_ PULONGLONG FoundLCN)
{
    PUCHAR Buffer;
    ULONGLONG Cluster = StartCluster;
    ULONGLONG StreakStart = 0;
    ULONG StreakLength = 0;
    NTSTATUS Status = STATUS_NOT_FOUND;

    Buffer = AcquireBitmapWorkBuffer(DiskVolume);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    while (Cluster < EndCluster)
    {
        ULONGLONG ByteOffset = Cluster >> 3;
        ULONGLONG ChunkByteOffset =
            ByteOffset -
            (ByteOffset % BITMAP_CHUNK_SIZE);
        ULONGLONG ChunkByteEnd =
            ChunkByteOffset + BITMAP_CHUNK_SIZE;
        ULONGLONG ChunkClusterEnd =
            min(EndCluster, ChunkByteEnd << 3);
        ULONG ByteLength = (ULONG)(
            ((ChunkClusterEnd + 7) >> 3) -
            ChunkByteOffset);

        Status = ReadBitmapBytes(BitmapFile,
                                 BitmapData,
                                 Buffer,
                                 ByteLength,
                                 ChunkByteOffset);
        if (!NT_SUCCESS(Status))
            goto Done;

        while (Cluster < ChunkClusterEnd)
        {
            ULONG RelativeByte =
                (ULONG)((Cluster >> 3) - ChunkByteOffset);
            UCHAR Mask = (UCHAR)(1u << (Cluster & 7));

            if (!(Buffer[RelativeByte] & Mask))
            {
                if (StreakLength == 0)
                    StreakStart = Cluster;
                StreakLength++;
                if (StreakLength == ClusterCount)
                {
                    *FoundLCN = StreakStart;
                    Status = STATUS_SUCCESS;
                    goto Done;
                }
            }
            else
            {
                StreakLength = 0;
            }
            Cluster++;
        }
    }

    Status = STATUS_NOT_FOUND;

Done:
    /* Buffer is the volume's shared scratch. */
    return Status;
}

static NTSTATUS
AppendFreeClustersFromRange(
    _In_ PVolume DiskVolume,
    _In_ PFileRecord BitmapFile,
    _In_ PAttribute BitmapData,
    _In_ ULONGLONG StartCluster,
    _In_ ULONGLONG EndCluster,
    _Inout_ PULONG ClustersNeeded,
    _In_ ULONG MaxRuns,
    _Inout_ PULONG RunCount,
    _Inout_ PDataRun* Head,
    _Inout_ PDataRun* Tail)
{
    PUCHAR Buffer;
    ULONGLONG Cluster = StartCluster;
    NTSTATUS Status = STATUS_SUCCESS;

    Buffer = AcquireBitmapWorkBuffer(DiskVolume);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    while (Cluster < EndCluster && *ClustersNeeded != 0)
    {
        ULONGLONG ByteOffset = Cluster >> 3;
        ULONGLONG ChunkByteOffset =
            ByteOffset -
            (ByteOffset % BITMAP_CHUNK_SIZE);
        ULONGLONG ChunkByteEnd =
            ChunkByteOffset + BITMAP_CHUNK_SIZE;
        ULONGLONG ChunkClusterEnd =
            min(EndCluster, ChunkByteEnd << 3);
        ULONG ByteLength = (ULONG)(
            ((ChunkClusterEnd + 7) >> 3) -
            ChunkByteOffset);

        Status = ReadBitmapBytes(BitmapFile,
                                 BitmapData,
                                 Buffer,
                                 ByteLength,
                                 ChunkByteOffset);
        if (!NT_SUCCESS(Status))
            goto Done;

        while (Cluster < ChunkClusterEnd &&
               *ClustersNeeded != 0)
        {
            ULONG RelativeByte =
                (ULONG)((Cluster >> 3) - ChunkByteOffset);
            UCHAR Mask = (UCHAR)(1u << (Cluster & 7));

            if (!(Buffer[RelativeByte] & Mask))
            {
                if (*Tail &&
                    (*Tail)->LCN + (*Tail)->Length == Cluster)
                {
                    (*Tail)->Length++;
                }
                else
                {
                    PDataRun Run;

                    if (*RunCount == MaxRuns)
                    {
                        Status = STATUS_BUFFER_TOO_SMALL;
                        goto Done;
                    }
                    Run = new(PagedPool, TAG_DATA_RUN) DataRun();
                    if (!Run)
                    {
                        Status = STATUS_INSUFFICIENT_RESOURCES;
                        goto Done;
                    }
                    Run->NextRun = NULL;
                    Run->LCN = Cluster;
                    Run->Length = 1;
                    Run->IsSparse = FALSE;
                    if (*Tail)
                        (*Tail)->NextRun = Run;
                    else
                        *Head = Run;
                    *Tail = Run;
                    (*RunCount)++;
                }
                (*ClustersNeeded)--;
            }
            Cluster++;
        }
    }

Done:
    /* Buffer is the volume's shared scratch. */
    return Status;
}

NTSTATUS
Volume::GetFreeClusters(_Out_ PLARGE_INTEGER FreeClusters)
{
    NTSTATUS Status;
    PFileRecord BitmapFile;
    PAttribute BitmapData;
    PUCHAR BitmapBuffer;
    RTL_BITMAP Bitmap;
    ULONGLONG BitmapOffset;
    ULONGLONG ClustersRemaining;
    ULONG ClustersInChunk, BytesToRead;

    // Note: $Bitmap is *always* non-resident on Windows.

    // Get file record for $Bitmap and $DATA attribute.
    Status = GetVolumeBitmap(this, &BitmapFile, &BitmapData);
    if (!NT_SUCCESS(Status))
        return Status;

    // Initialize bitmap chunk buffer
    BitmapBuffer = new(NonPagedPool) UCHAR[BITMAP_CHUNK_SIZE];
    if (!BitmapBuffer)
    {
        /* Owned by the volume's bitmap-record cache. */
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    FreeClusters->QuadPart = 0;
    BitmapOffset = 0;
    ClustersRemaining = ClustersInVolume;

    /* Count the clear (free) bits chunk by chunk. Only ClustersInVolume
     * bits are valid; the padding bits at the end of $Bitmap are not.
     */
    while (ClustersRemaining != 0)
    {
        ClustersInChunk = (ULONG)min(
            ClustersRemaining,
            (ULONGLONG)BITMAP_CHUNK_SIZE * 8);
        BytesToRead = ALIGN_UP_BY((ClustersInChunk + 7) / 8, sizeof(ULONG));

        Status = BitmapFile->CopyData(BitmapData,
                                      BitmapBuffer,
                                      &BytesToRead,
                                      BitmapOffset);
        if (!NT_SUCCESS(Status) || BytesToRead != 0)
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_END_OF_FILE;
            goto Done;
        }

        RtlInitializeBitMap(&Bitmap, (PULONG)BitmapBuffer, ClustersInChunk);
        FreeClusters->QuadPart += RtlNumberOfClearBits(&Bitmap);

        BitmapOffset += ClustersInChunk / 8;
        ClustersRemaining -= ClustersInChunk;
    }

    Status = STATUS_SUCCESS;

Done:
    // We're done! Time to cleanup.
    delete[] BitmapBuffer;
    /* Owned by the volume's bitmap-record cache. */
    return Status;
}

NTSTATUS
Volume::ReadBitmap(_In_ ULONGLONG StartingLcn,
                   _Out_ PULONGLONG ReturnedStartingLcn,
                   _Out_ PULONGLONG BitmapSize,
                   _Out_opt_ PUCHAR Bitmap,
                   _Inout_ PULONG BitmapLength)
{
    PFileRecord BitmapFile = NULL;
    PAttribute BitmapData = NULL;
    ULONGLONG TotalBytes;
    ULONG Capacity;
    ULONG BytesToRead;
    NTSTATUS Status;

    if (!ReturnedStartingLcn ||
        !BitmapSize ||
        !BitmapLength ||
        (!Bitmap && *BitmapLength != 0) ||
        StartingLcn > ClustersInVolume)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Capacity = *BitmapLength;
    *BitmapLength = 0;
    StartingLcn &= ~(ULONGLONG)7;
    *ReturnedStartingLcn = StartingLcn;
    *BitmapSize =
        (ULONGLONG)ClustersInVolume - StartingLcn;
    TotalBytes = (*BitmapSize + 7) >> 3;
    BytesToRead = (ULONG)min(
        TotalBytes,
        (ULONGLONG)Capacity);

    Status = GetVolumeBitmap(this,
                             &BitmapFile,
                             &BitmapData);
    if (!NT_SUCCESS(Status))
        return Status;

    if (BytesToRead != 0)
    {
        Status = ReadBitmapBytes(BitmapFile,
                                 BitmapData,
                                 Bitmap,
                                 BytesToRead,
                                 StartingLcn >> 3);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    *BitmapLength = BytesToRead;
    Status = BytesToRead == TotalBytes
        ? STATUS_SUCCESS
        : BytesToRead == 0
            ? STATUS_BUFFER_TOO_SMALL
            : STATUS_BUFFER_OVERFLOW;

Done:
    /* Owned by the volume's bitmap-record cache. */
    return Status;
}

NTSTATUS
Volume::AllocateClusters(_In_ ULONGLONG PreferredLCN,
                         _In_ ULONG ClusterCount,
                         _In_ ULONG MaxRuns,
                         _Out_ PDataRun* Runs)
{
    PFileRecord BitmapFile = NULL;
    PAttribute BitmapData = NULL;
    PDataRun Head = NULL;
    PDataRun Tail = NULL;
    ULONGLONG ContiguousLCN;
    ULONG ClustersNeeded;
    ULONG RunCount = 0;
    NTSTATUS Status;

    if (!Runs || ClusterCount == 0 || MaxRuns == 0)
        return STATUS_INVALID_PARAMETER;
    *Runs = NULL;
    if (IsReadOnly)
        return STATUS_ACCESS_DENIED;

    Status = GetVolumeBitmap(this, &BitmapFile, &BitmapData);
    if (!NT_SUCCESS(Status))
        return Status;

    if (PreferredLCN >= ClustersInVolume)
        PreferredLCN = 0;

    /* Callers with no placement preference start where the last allocation
     * ended; the wrap pass below still covers everything before it. */
    if (PreferredLCN == 0 &&
        FreeClusterHint != 0 &&
        FreeClusterHint < ClustersInVolume)
    {
        PreferredLCN = FreeClusterHint;
    }

    /*
     * Prefer one extent. Besides reducing I/O, this keeps mapping pairs
     * small enough to remain in the owning MFT record in the common case.
     */
    Status = FindContiguousFreeClusters(this,
                                        BitmapFile,
                                        BitmapData,
                                        PreferredLCN,
                                        ClustersInVolume,
                                        ClusterCount,
                                        &ContiguousLCN);
    if (Status == STATUS_NOT_FOUND && PreferredLCN != 0)
    {
        Status = FindContiguousFreeClusters(this,
                                            BitmapFile,
                                            BitmapData,
                                            0,
                                            PreferredLCN,
                                            ClusterCount,
                                            &ContiguousLCN);
    }
    if (NT_SUCCESS(Status))
    {
        Head = new(PagedPool, TAG_DATA_RUN) DataRun();
        if (!Head)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }
        Head->NextRun = NULL;
        Head->LCN = ContiguousLCN;
        Head->Length = ClusterCount;
        Head->IsSparse = FALSE;
    }
    else if (Status == STATUS_NOT_FOUND)
    {
        /*
         * No single extent fits. Gather ordered free extents, bounded by
         * the caller's mapping-pairs capacity.
         */
        ClustersNeeded = ClusterCount;
        Status = AppendFreeClustersFromRange(this,
                                             BitmapFile,
                                             BitmapData,
                                             PreferredLCN,
                                             ClustersInVolume,
                                             &ClustersNeeded,
                                             MaxRuns,
                                             &RunCount,
                                             &Head,
                                             &Tail);
        if (NT_SUCCESS(Status) &&
            ClustersNeeded != 0 &&
            PreferredLCN != 0)
        {
            Status = AppendFreeClustersFromRange(this,
                                                 BitmapFile,
                                                 BitmapData,
                                                 0,
                                                 PreferredLCN,
                                                 &ClustersNeeded,
                                                 MaxRuns,
                                                 &RunCount,
                                                 &Head,
                                                 &Tail);
        }
        if (NT_SUCCESS(Status) && ClustersNeeded != 0)
            Status = STATUS_DISK_FULL;
        if (!NT_SUCCESS(Status))
            goto Done;
    }
    else
    {
        goto Done;
    }

    Status = SetClusterRunBits(this,
                               BitmapFile,
                               BitmapData,
                               Head,
                               TRUE,
                               ClustersInVolume);
    if (NT_SUCCESS(Status))
    {
        PDataRun Last = Head;

        while (Last->NextRun)
            Last = Last->NextRun;
        FreeClusterHint = Last->LCN + Last->Length;

        *Runs = Head;
        Head = NULL;
    }

Done:
    FreeDataRun(Head);
    /* Owned by the volume's bitmap-record cache. */
    return Status;
}

NTSTATUS
Volume::ReleaseClusters(_In_ PDataRun Runs)
{
    PFileRecord BitmapFile = NULL;
    PAttribute BitmapData = NULL;
    NTSTATUS Status;

    if (!Runs)
        return STATUS_INVALID_PARAMETER;
    if (IsReadOnly)
        return STATUS_ACCESS_DENIED;

    Status = GetVolumeBitmap(this, &BitmapFile, &BitmapData);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = SetClusterRunBits(this,
                               BitmapFile,
                               BitmapData,
                               Runs,
                               FALSE,
                               ClustersInVolume);
    if (NT_SUCCESS(Status))
    {
        PDataRun Freed = Runs;

        /* Freed space becomes the next best place to look. */
        for (; Freed; Freed = Freed->NextRun)
        {
            if (!Freed->IsSparse &&
                Freed->LCN < FreeClusterHint)
            {
                FreeClusterHint = Freed->LCN;
            }
        }
    }
    /* Owned by the volume's bitmap-record cache. */
    return Status;
}
