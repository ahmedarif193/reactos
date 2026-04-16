/*
 * Extended file write path for ntfslx.
 *
 * Handles:
 *   - Resident write (extends the resident $DATA attribute in place)
 *   - Resident -> non-resident promotion when data grows too big
 *   - Non-resident write (overwrite existing + allocate more clusters on extend)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

/* Maximum size for a resident attribute's value. Conservative threshold:
 * the MFT record is usually 1024 bytes, minus header and other attributes. */
#define NTFSLX_MAX_RESIDENT_DATA_SIZE  700

#define NTFSLX_UPDATE_FN_MAX_PARENTS   4

/* Upper bound on mapping-pairs bytes we will emit for a single $DATA attribute.
 * Each run consumes at most 1 header + 8 length + 8 lcn = 17 bytes, plus 1 byte
 * terminator. 512 comfortably holds ~30 fragmented runs before we'd need an
 * attribute-list promotion (out of scope for this pass). */
#define NTFSLX_MP_ENCODE_MAX           512
#define NTFSLX_NONRESIDENT_ATTR_HEADER_SIZE \
    (FIELD_OFFSET(NTFSLX_ATTR_RECORD, Data.NonResident.CompressedSize) + sizeof(ULONGLONG))

NTSTATUS
NtfslxExtendedWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length,
    _In_ PLARGE_INTEGER ByteOffset,
    _Out_opt_ PULONG BytesWritten);

/*
 * Encode a runlist into NTFS mapping-pairs format (delta-compressed LCNs,
 * LSB-first signed ints). Runlist must be sentinel-terminated (Length == 0).
 * Returns STATUS_BUFFER_TOO_SMALL if the encoded form doesn't fit MaxSize.
 */
static NTSTATUS
NtfslxEncodeMappingPairs(
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _Out_writes_bytes_to_(MaxSize, *OutSize) PUCHAR OutBuffer,
    _In_ ULONG MaxSize,
    _Out_ PULONG OutSize)
{
    ULONG Offset;
    LONGLONG PrevLcn;
    ULONG I;

    *OutSize = 0;
    Offset = 0;
    PrevLcn = 0;

    for (I = 0; Runlist[I].Length != 0; I++)
    {
        LONGLONG Length = Runlist[I].Length;
        LONGLONG Lcn = Runlist[I].Lcn;
        LONGLONG LcnDelta = 0;
        UCHAR LenBytes;
        UCHAR OfsBytes;
        ULONG J;
        UCHAR Header;

        if (Length <= 0)
        {
            return STATUS_INVALID_PARAMETER;
        }

        /* Minimum bytes to encode Length as unsigned (but top bit reserved
         * so it's interpreted as positive). */
        LenBytes = 1;
        {
            ULONGLONG V = (ULONGLONG)Length;
            while (V > 0x7F)
            {
                V >>= 8;
                LenBytes++;
            }
        }

        if (Lcn == NTFSLX_LCN_HOLE)
        {
            OfsBytes = 0;
        }
        else if (Lcn >= 0)
        {
            ULONGLONG AbsV;
            LcnDelta = Lcn - PrevLcn;
            OfsBytes = 1;
            AbsV = (ULONGLONG)((LcnDelta < 0) ? ~LcnDelta : LcnDelta);
            while (AbsV > 0x7F)
            {
                AbsV >>= 8;
                OfsBytes++;
            }
            PrevLcn = Lcn;
        }
        else
        {
            return STATUS_INVALID_PARAMETER;
        }

        if (Offset + 1 + LenBytes + OfsBytes + 1 > MaxSize)
        {
            return STATUS_BUFFER_TOO_SMALL;
        }

        Header = (UCHAR)((OfsBytes << 4) | LenBytes);
        OutBuffer[Offset++] = Header;

        for (J = 0; J < LenBytes; J++)
        {
            OutBuffer[Offset++] = (UCHAR)(((ULONGLONG)Length >> (J * 8)) & 0xFF);
        }
        for (J = 0; J < OfsBytes; J++)
        {
            OutBuffer[Offset++] = (UCHAR)(((ULONGLONG)LcnDelta >> (J * 8)) & 0xFF);
        }
    }

    /* Terminator */
    if (Offset + 1 > MaxSize)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    OutBuffer[Offset++] = 0;

    *OutSize = Offset;
    return STATUS_SUCCESS;
}

/*
 * Write zeros to a run of clusters. Used after extending a non-resident
 * allocation so that newly mapped clusters don't expose stale disk data.
 */
static NTSTATUS
NtfslxZeroFillRunRange(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ ULONGLONG StartByte,
    _In_ ULONGLONG Bytes)
{
    ULONG ClusterSize = DevExt->VolumeInfo.BytesPerCluster;
    ULONG SectorSize = DevExt->VolumeInfo.BytesPerSector;
    PUCHAR ZeroBuffer;
    ULONG ZeroBufferSize;
    ULONGLONG Remaining;
    ULONGLONG CurrentByte;
    ULONG I;
    NTSTATUS Status = STATUS_SUCCESS;

    if (Bytes == 0)
    {
        return STATUS_SUCCESS;
    }

    ZeroBufferSize = (ClusterSize > 0x10000) ? ClusterSize : 0x10000;
    ZeroBuffer = ExAllocatePoolWithTag(NonPagedPool, ZeroBufferSize, NTFSLX_TAG);
    if (ZeroBuffer == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(ZeroBuffer, ZeroBufferSize);

    CurrentByte = StartByte;
    Remaining = Bytes;

    for (I = 0; Runlist[I].Length != 0 && Remaining > 0; I++)
    {
        ULONGLONG RunStart;
        ULONGLONG RunEnd;
        ULONGLONG RunOfs;
        ULONGLONG RunBytes;

        if (Runlist[I].Lcn < 0)
        {
            continue;
        }

        RunStart = (ULONGLONG)Runlist[I].Vcn * ClusterSize;
        RunEnd = RunStart + (ULONGLONG)Runlist[I].Length * ClusterSize;
        if (CurrentByte >= RunEnd)
        {
            continue;
        }
        if (CurrentByte < RunStart)
        {
            continue;
        }

        RunOfs = CurrentByte - RunStart;
        RunBytes = RunEnd - CurrentByte;
        if (RunBytes > Remaining)
        {
            RunBytes = Remaining;
        }

        while (RunBytes > 0)
        {
            ULONG Chunk = (RunBytes > ZeroBufferSize) ? ZeroBufferSize : (ULONG)RunBytes;
            LONGLONG DiskOfs = Runlist[I].Lcn * ClusterSize + (LONGLONG)RunOfs;

            Status = NtfslxWriteDisk(DevExt->StorageDevice, DiskOfs, Chunk,
                                     SectorSize, ZeroBuffer, FALSE);
            if (!NT_SUCCESS(Status))
            {
                goto done;
            }

            RunOfs += Chunk;
            RunBytes -= Chunk;
            CurrentByte += Chunk;
            Remaining -= Chunk;
        }
    }

done:
    ExFreePoolWithTag(ZeroBuffer, NTFSLX_TAG);
    return Status;
}

/*
 * Mirror a size change into FcbHeader and tell the cache manager via
 * CcSetFileSizes. Every write/truncate path calls this instead of touching
 * FcbHeader directly, so the cache map stays in sync with on-disk state.
 */
static VOID
NtfslxUpdateFcbAndCacheSizes(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ ULONGLONG NewAllocationSize,
    _In_ ULONGLONG NewFileSize,
    _In_ ULONGLONG NewValidDataLength)
{
    CC_FILE_SIZES Sizes;

    if (NewAllocationSize < NewFileSize)
    {
        NewAllocationSize = NewFileSize;
    }
    if (NewValidDataLength > NewFileSize)
    {
        NewValidDataLength = NewFileSize;
    }

    FileContext->FcbHeader.AllocationSize.QuadPart = (LONGLONG)NewAllocationSize;
    FileContext->FcbHeader.FileSize.QuadPart = (LONGLONG)NewFileSize;
    FileContext->FcbHeader.ValidDataLength.QuadPart = (LONGLONG)NewValidDataLength;

    if (FileContext->CacheContext != NULL)
    {
        Sizes.AllocationSize.QuadPart = (LONGLONG)NewAllocationSize;
        Sizes.FileSize.QuadPart = (LONGLONG)NewFileSize;
        Sizes.ValidDataLength.QuadPart = (LONGLONG)NewValidDataLength;
        (VOID)NtfslxCacheRuntimeSetFileSizes(FileContext->CacheContext, &Sizes);
    }
}

static
ULONG
NtfslxCountRunlistEntries(
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist)
{
    ULONG Count;

    if (Runlist == NULL)
    {
        return 0;
    }

    Count = 0;
    while (Runlist[Count].Length != 0)
    {
        ++Count;
    }

    return Count + 1;
}


static
NTSTATUS
NtfslxEnsureNonResidentRunlistState(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext)
{
    PNTFSLX_ATTR_RECORD DataAttr;
    PNTFSLX_RUNLIST_ELEMENT RebuiltRunlist;
    ULONG ActualCount;
    ULONG RebuiltCount;
    NTSTATUS Status;

    if (FileContext == NULL || FileContext->ResidentData)
    {
        return STATUS_SUCCESS;
    }

    DataAttr = FileContext->DataAttribute;
    if (FileContext->DataRunlist == NULL || DataAttr == NULL || DataAttr->NonResident == 0)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    ActualCount = NtfslxCountRunlistEntries(FileContext->DataRunlist);
    if (ActualCount == 0)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (ActualCount != FileContext->DataRunlistCount)
    {
        DbgPrint("ntfslx: runlist count fixup mft=%I64u old=%lu new=%lu alloc=%I64u data=%I64u\n",
                 FileContext->MftIndex,
                 FileContext->DataRunlistCount,
                 ActualCount,
                 FileContext->AllocationSize,
                 FileContext->DataSize);
        FileContext->DataRunlistCount = ActualCount;
    }

    if (FileContext->AllocationSize == 0 || FileContext->DataRunlistCount > 1)
    {
        return STATUS_SUCCESS;
    }

    RebuiltRunlist = NULL;
    RebuiltCount = 0;
    Status = NtfslxMappingPairsDecompress(&FileContext->DeviceExtension->VolumeInfo,
                                          DataAttr,
                                          &RebuiltRunlist,
                                          &RebuiltCount);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("ntfslx: runlist rebuild failed mft=%I64u status=0x%08lx alloc=%I64u data=%I64u\n",
                 FileContext->MftIndex,
                 Status,
                 FileContext->AllocationSize,
                 FileContext->DataSize);
        return Status;
    }

    ActualCount = NtfslxCountRunlistEntries(RebuiltRunlist);
    if (ActualCount <= 1)
    {
        ExFreePoolWithTag(RebuiltRunlist, NTFSLX_TAG);
        DbgPrint("ntfslx: runlist rebuild still empty mft=%I64u alloc=%I64u data=%I64u\n",
                 FileContext->MftIndex,
                 FileContext->AllocationSize,
                 FileContext->DataSize);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    DbgPrint("ntfslx: runlist rebuilt mft=%I64u old=%lu new=%lu alloc=%I64u data=%I64u\n",
             FileContext->MftIndex,
             FileContext->DataRunlistCount,
             ActualCount,
             FileContext->AllocationSize,
             FileContext->DataSize);
    ExFreePoolWithTag(FileContext->DataRunlist, NTFSLX_TAG);
    FileContext->DataRunlist = RebuiltRunlist;
    FileContext->DataRunlistCount = ActualCount;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxSplitRunlistAtCluster(
    _In_ PNTFSLX_RUNLIST_ELEMENT SourceRunlist,
    _In_ ULONGLONG KeepClusters,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *OutKeepRunlist,
    _Out_ PULONG OutKeepRunlistCount,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *OutTailRunlist,
    _Out_ PULONG OutTailRunlistCount)
{
    ULONG SourceCount;
    PNTFSLX_RUNLIST_ELEMENT KeepRunlist;
    PNTFSLX_RUNLIST_ELEMENT TailRunlist;
    ULONGLONG RemainingKeep;
    ULONG KeepIndex;
    ULONG TailIndex;
    ULONG Index;

    *OutKeepRunlist = NULL;
    *OutKeepRunlistCount = 0;
    *OutTailRunlist = NULL;
    *OutTailRunlistCount = 0;

    if (SourceRunlist == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SourceCount = NtfslxCountRunlistEntries(SourceRunlist);
    if (SourceCount == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeepRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                        SourceCount * sizeof(NTFSLX_RUNLIST_ELEMENT),
                                        NTFSLX_TAG);
    if (KeepRunlist == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    TailRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                        SourceCount * sizeof(NTFSLX_RUNLIST_ELEMENT),
                                        NTFSLX_TAG);
    if (TailRunlist == NULL)
    {
        ExFreePoolWithTag(KeepRunlist, NTFSLX_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(KeepRunlist, SourceCount * sizeof(NTFSLX_RUNLIST_ELEMENT));
    RtlZeroMemory(TailRunlist, SourceCount * sizeof(NTFSLX_RUNLIST_ELEMENT));

    RemainingKeep = KeepClusters;
    KeepIndex = 0;
    TailIndex = 0;

    for (Index = 0; SourceRunlist[Index].Length != 0; ++Index)
    {
        const NTFSLX_RUNLIST_ELEMENT *Run = &SourceRunlist[Index];

        if (RemainingKeep >= (ULONGLONG)Run->Length)
        {
            KeepRunlist[KeepIndex++] = *Run;
            RemainingKeep -= (ULONGLONG)Run->Length;
            continue;
        }

        if (RemainingKeep > 0)
        {
            KeepRunlist[KeepIndex] = *Run;
            KeepRunlist[KeepIndex].Length = (LONGLONG)RemainingKeep;
            ++KeepIndex;

            if (Run->Lcn >= 0)
            {
                TailRunlist[TailIndex].Vcn = Run->Vcn + (LONGLONG)RemainingKeep;
                TailRunlist[TailIndex].Lcn = Run->Lcn + (LONGLONG)RemainingKeep;
                TailRunlist[TailIndex].Length = Run->Length - (LONGLONG)RemainingKeep;
                ++TailIndex;
            }

            RemainingKeep = 0;
            continue;
        }

        if (Run->Lcn >= 0)
        {
            TailRunlist[TailIndex++] = *Run;
        }
    }

    KeepRunlist[KeepIndex].Vcn = (LONGLONG)KeepClusters;
    KeepRunlist[KeepIndex].Lcn = NTFSLX_LCN_ENOENT;
    KeepRunlist[KeepIndex].Length = 0;
    ++KeepIndex;

    TailRunlist[TailIndex].Vcn = 0;
    TailRunlist[TailIndex].Lcn = NTFSLX_LCN_ENOENT;
    TailRunlist[TailIndex].Length = 0;
    ++TailIndex;

    *OutKeepRunlist = KeepRunlist;
    *OutKeepRunlistCount = KeepIndex;
    *OutTailRunlist = TailRunlist;
    *OutTailRunlistCount = TailIndex;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxRewriteNonResidentDataAttribute(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext,
    _Inout_ PNTFSLX_ATTR_RECORD DataAttr,
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ ULONGLONG ClusterCount,
    _In_ ULONGLONG AllocationSize,
    _In_ ULONGLONG DataSize)
{
    UCHAR MpBuffer[NTFSLX_MP_ENCODE_MAX];
    ULONG MpSize;
    ULONG MpOffset;
    ULONG NewAttrLength;
    NTSTATUS Status;

    MpSize = 0;
    RtlZeroMemory(MpBuffer, sizeof(MpBuffer));

    Status = NtfslxEncodeMappingPairs(Runlist, MpBuffer, sizeof(MpBuffer), &MpSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    MpOffset = DataAttr->Data.NonResident.MappingPairsOffset;
    if (MpOffset < NTFSLX_NONRESIDENT_ATTR_HEADER_SIZE)
    {
        MpOffset = NTFSLX_NONRESIDENT_ATTR_HEADER_SIZE;
        DataAttr->Data.NonResident.MappingPairsOffset = (USHORT)MpOffset;
    }
    NewAttrLength = ROUND_UP(MpOffset + MpSize, 8);
    if (NewAttrLength != DataAttr->Length)
    {
        Status = NtfslxResizeAttributeRecord(FileContext->FileRecord, DataAttr, NewAttrLength);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    RtlZeroMemory((PUCHAR)DataAttr + MpOffset, DataAttr->Length - MpOffset);
    RtlCopyMemory((PUCHAR)DataAttr + MpOffset, MpBuffer, MpSize);

    DataAttr->Data.NonResident.LowestVcn = 0;
    DataAttr->Data.NonResident.HighestVcn = (ClusterCount == 0) ? (ULONGLONG)-1 : ClusterCount - 1;
    DataAttr->Data.NonResident.AllocatedSize = AllocationSize;
    DataAttr->Data.NonResident.DataSize = DataSize;
    DataAttr->Data.NonResident.InitializedSize = DataSize;
    DataAttr->Data.NonResident.CompressedSize = 0;


    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxTruncateNonResident(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ ULONGLONG NewSize)
{
    PNTFSLX_DEVICE_EXTENSION DevExt;
    PNTFSLX_MFT_RECORD Record;
    PNTFSLX_ATTR_RECORD DataAttr;
    PNTFSLX_RUNLIST_ELEMENT KeepRunlist;
    PNTFSLX_RUNLIST_ELEMENT TailRunlist;
    PNTFSLX_RUNLIST_ELEMENT OldRunlist;
    ULONG KeepRunlistCount;
    ULONG TailRunlistCount;
    ULONG OldRunlistCount;
    ULONG ClusterSize;
    ULONGLONG CurrentClusters;
    ULONGLONG NewClusters;
    ULONGLONG NewAllocationSize;
    NTSTATUS Status;
    NTSTATUS FreeStatus;

    DevExt = FileContext->DeviceExtension;
    Record = FileContext->FileRecord;
    DataAttr = FileContext->DataAttribute;
    KeepRunlist = NULL;
    TailRunlist = NULL;
    OldRunlist = NULL;
    KeepRunlistCount = 0;
    TailRunlistCount = 0;
    OldRunlistCount = 0;

    if (DataAttr == NULL || DataAttr->NonResident == 0)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    if (FileContext->DataRunlist == NULL)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Status = NtfslxEnsureNonResidentRunlistState(FileContext);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    ClusterSize = DevExt->VolumeInfo.BytesPerCluster;
    CurrentClusters = FileContext->AllocationSize / ClusterSize;
    NewClusters = (NewSize == 0) ? 0 : ((NewSize + ClusterSize - 1) / ClusterSize);
    NewAllocationSize = NewClusters * ClusterSize;

    DbgPrint("ntfslx: TruncateNonRes mft=%I64u oldSize=%I64u newSize=%I64u oldAlloc=%I64u newAlloc=%I64u\n",
             FileContext->MftIndex, FileContext->DataSize, NewSize,
             FileContext->AllocationSize, NewAllocationSize);

    if (NewClusters == CurrentClusters)
    {
        DataAttr->Data.NonResident.DataSize = NewSize;
        DataAttr->Data.NonResident.InitializedSize = NewSize;

        Status = NtfslxWriteMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                      DevExt->MftRunlist, FileContext->MftIndex,
                                      Record);
        if (NT_SUCCESS(Status))
        {
            FileContext->DataSize = NewSize;
            NtfslxUpdateFcbAndCacheSizes(FileContext,
                                         FileContext->AllocationSize,
                                         NewSize,
                                         NewSize);
            NtfslxUpdateFileNameSize(FileContext, NewSize, FileContext->AllocationSize);
            FileContext->PendingDataNotify = TRUE;
        }

        return Status;
    }

    if (NewClusters == 0)
    {
        ASSERT(CurrentClusters != 0);

        KeepRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                            sizeof(NTFSLX_RUNLIST_ELEMENT),
                                            NTFSLX_TAG);
        if (KeepRunlist == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        KeepRunlist[0].Vcn = 0;
        KeepRunlist[0].Lcn = NTFSLX_LCN_ENOENT;
        KeepRunlist[0].Length = 0;
        KeepRunlistCount = 1;

        OldRunlist = FileContext->DataRunlist;
        OldRunlistCount = FileContext->DataRunlistCount;

        Status = NtfslxRewriteNonResidentDataAttribute(FileContext,
                                                       DataAttr,
                                                       KeepRunlist,
                                                       0,
                                                       0,
                                                       0);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }

        Status = NtfslxWriteMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                      DevExt->MftRunlist, FileContext->MftIndex,
                                      Record);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }

        FileContext->DataRunlist = KeepRunlist;
        FileContext->DataRunlistCount = KeepRunlistCount;
        FileContext->AllocationSize = 0;
        FileContext->DataSize = 0;
        KeepRunlist = NULL;

        ASSERT(FileContext->DataRunlistCount == 1);
        ASSERT(FileContext->DataRunlist[0].Length == 0);

        NtfslxUpdateFcbAndCacheSizes(FileContext, 0, 0, 0);
        NtfslxUpdateFileNameSize(FileContext, 0, 0);
        FileContext->PendingDataNotify = TRUE;

        FreeStatus = STATUS_SUCCESS;
        if (OldRunlist != NULL && OldRunlistCount > 1)
        {
            DbgPrint("ntfslx: TruncateNonRes freeing full runlist mft=%I64u runs=%lu oldClusters=%I64u\n",
                     FileContext->MftIndex,
                     OldRunlistCount,
                     CurrentClusters);
            FreeStatus = NtfslxFreeClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                            DevExt->MftRunlist, OldRunlist, OldRunlistCount);
            if (!NT_SUCCESS(FreeStatus))
            {
                DbgPrint("ntfslx: TruncateNonRes zero-size free failed mft=%I64u status=0x%08lx\n",
                         FileContext->MftIndex,
                         FreeStatus);
            }
        }

        if (OldRunlist != NULL)
        {
            ExFreePoolWithTag(OldRunlist, NTFSLX_TAG);
        }

        return NT_SUCCESS(FreeStatus) ? Status : FreeStatus;
    }

    Status = NtfslxSplitRunlistAtCluster(FileContext->DataRunlist,
                                         NewClusters,
                                         &KeepRunlist,
                                         &KeepRunlistCount,
                                         &TailRunlist,
                                         &TailRunlistCount);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = NtfslxRewriteNonResidentDataAttribute(FileContext,
                                                   DataAttr,
                                                   KeepRunlist,
                                                   NewClusters,
                                                   NewAllocationSize,
                                                   NewSize);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    Status = NtfslxWriteMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                  DevExt->MftRunlist, FileContext->MftIndex,
                                  Record);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    OldRunlist = FileContext->DataRunlist;
    FileContext->DataRunlist = KeepRunlist;
    FileContext->DataRunlistCount = KeepRunlistCount;
    FileContext->AllocationSize = NewAllocationSize;
    FileContext->DataSize = NewSize;
    KeepRunlist = NULL;

    if (OldRunlist != NULL)
    {
        ExFreePoolWithTag(OldRunlist, NTFSLX_TAG);
    }

    NtfslxUpdateFcbAndCacheSizes(FileContext, NewAllocationSize, NewSize, NewSize);
    NtfslxUpdateFileNameSize(FileContext, NewSize, NewAllocationSize);
    FileContext->PendingDataNotify = TRUE;

    FreeStatus = STATUS_SUCCESS;
    if (TailRunlist != NULL && TailRunlistCount > 1)
    {
        FreeStatus = NtfslxFreeClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                        DevExt->MftRunlist, TailRunlist, TailRunlistCount);
        if (!NT_SUCCESS(FreeStatus))
        {
            DbgPrint("ntfslx: TruncateNonRes post-commit free failed mft=%I64u status=0x%08lx\n",
                     FileContext->MftIndex, FreeStatus);
        }
    }

    if (TailRunlist != NULL)
    {
        ExFreePoolWithTag(TailRunlist, NTFSLX_TAG);
    }

    return NT_SUCCESS(FreeStatus) ? Status : FreeStatus;

Cleanup:
    if (KeepRunlist != NULL)
    {
        ExFreePoolWithTag(KeepRunlist, NTFSLX_TAG);
    }
    if (TailRunlist != NULL)
    {
        ExFreePoolWithTag(TailRunlist, NTFSLX_TAG);
    }
    return Status;
}


/*
 * After $DATA changes, propagate new DataSize/AllocatedSize and modified
 * timestamps into: (a) the file's own $STANDARD_INFORMATION attribute,
 * (b) every $FILE_NAME attribute in the same MFT record, and (c) each
 * distinct parent directory's $I30 index entry.
 *
 * Best-effort: $I30 failures are traced and ignored since cross-directory
 * or non-resident-index cases cannot be satisfied by this pass.
 */
NTSTATUS
NtfslxUpdateFileNameSize(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ ULONGLONG NewDataSize,
    _In_ ULONGLONG NewAllocatedSize)
{
    PNTFSLX_DEVICE_EXTENSION DevExt;
    PNTFSLX_MFT_RECORD Record;
    PNTFSLX_ATTR_RECORD StdAttr;
    PNTFSLX_STANDARD_INFORMATION StdInfo;
    PUCHAR Base;
    ULONG Offset;
    LARGE_INTEGER Now;
    struct
    {
        ULONGLONG ParentMftIdx;
        WCHAR Name[256];
        ULONG NameLen;
    } Parents[NTFSLX_UPDATE_FN_MAX_PARENTS];
    ULONG ParentCount;
    ULONG I;
    NTSTATUS Status;

    if (FileContext == NULL || FileContext->FileRecord == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (FileContext->IsDirectory)
    {
        return STATUS_SUCCESS;
    }

    DevExt = FileContext->DeviceExtension;
    Record = FileContext->FileRecord;
    KeQuerySystemTime(&Now);

    /* Update $STANDARD_INFORMATION timestamps (file modification). */
    Status = NtfslxFindAttribute(Record,
                                 NTFSLX_ATTRIBUTE_STANDARD_INFORMATION,
                                 NULL, 0, &StdAttr);
    if (NT_SUCCESS(Status) && StdAttr->NonResident == 0 &&
        StdAttr->Data.Resident.ValueLength >= sizeof(NTFSLX_STANDARD_INFORMATION))
    {
        StdInfo = (PNTFSLX_STANDARD_INFORMATION)
            ((PUCHAR)StdAttr + StdAttr->Data.Resident.ValueOffset);
        StdInfo->LastDataChangeTime = (ULONGLONG)Now.QuadPart;
        StdInfo->LastMftChangeTime = (ULONGLONG)Now.QuadPart;
    }

    /* Walk all $FILE_NAME attributes in the record. */
    ParentCount = 0;
    Base = (PUCHAR)Record;
    Offset = Record->AttributesOffset;
    while (Offset + sizeof(ULONG) * 2 <= Record->BytesInUse)
    {
        PNTFSLX_ATTR_RECORD Attr = (PNTFSLX_ATTR_RECORD)(Base + Offset);

        if (Attr->Type == NTFSLX_ATTRIBUTE_END)
            break;
        if (Attr->Length == 0 || Offset + Attr->Length > Record->BytesInUse)
            break;

        if (Attr->Type == NTFSLX_ATTRIBUTE_FILE_NAME &&
            Attr->NonResident == 0 &&
            Attr->Data.Resident.ValueLength >= sizeof(NTFSLX_FILE_NAME_ATTRIBUTE))
        {
            PNTFSLX_FILE_NAME_ATTRIBUTE Fn = (PNTFSLX_FILE_NAME_ATTRIBUTE)
                ((PUCHAR)Attr + Attr->Data.Resident.ValueOffset);

            if (Fn->FileNameType != NTFSLX_FILE_NAME_DOS)
            {
                Fn->DataSize = NewDataSize;
                Fn->AllocatedSize = NewAllocatedSize;
                Fn->LastDataChangeTime = (ULONGLONG)Now.QuadPart;
                Fn->LastMftChangeTime = (ULONGLONG)Now.QuadPart;

                if (ParentCount < NTFSLX_UPDATE_FN_MAX_PARENTS &&
                    Fn->FileNameLength > 0 &&
                    Fn->FileNameLength <= 255)
                {
                    Parents[ParentCount].ParentMftIdx = MREF(Fn->ParentDirectory);
                    Parents[ParentCount].NameLen = Fn->FileNameLength;
                    RtlCopyMemory(Parents[ParentCount].Name,
                                  (PUCHAR)Fn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE),
                                  Fn->FileNameLength * sizeof(WCHAR));
                    ParentCount++;
                }
                else if (ParentCount >= NTFSLX_UPDATE_FN_MAX_PARENTS)
                {
                    DbgPrint("ntfslx: UpdateFileNameSize: more than %lu parents on mft=%I64u, dropping extras\n",
                             (ULONG)NTFSLX_UPDATE_FN_MAX_PARENTS, FileContext->MftIndex);
                }
            }
        }

        Offset += Attr->Length;
    }

    /* Flush the modified MFT record once. */
    Status = NtfslxWriteMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                  DevExt->MftRunlist, FileContext->MftIndex, Record);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("ntfslx: UpdateFileNameSize: WriteMft mft=%I64u failed 0x%08lx\n",
                 FileContext->MftIndex, Status);
        return Status;
    }

    DbgPrint("ntfslx: UpdateFileNameSize mft=%I64u dataSize=%I64u allocSize=%I64u parents=%lu\n",
             FileContext->MftIndex, NewDataSize, NewAllocatedSize, ParentCount);

    /* Propagate into each parent directory's $I30 entry. */
    for (I = 0; I < ParentCount; I++)
    {
        NTSTATUS IxStatus = NtfslxIndexUpdateFileName(
            DevExt,
            Parents[I].ParentMftIdx,
            Parents[I].Name,
            Parents[I].NameLen,
            NewDataSize,
            NewAllocatedSize,
            &Now, &Now, &Now, &Now);
        if (!NT_SUCCESS(IxStatus))
        {
            DbgPrint("ntfslx: IndexUpdateFileName dir=%I64u name='%.*S' status=0x%08lx\n",
                     Parents[I].ParentMftIdx,
                     Parents[I].NameLen, Parents[I].Name,
                     IxStatus);
        }
    }

    return STATUS_SUCCESS;
}

/*
 * Write data by directly modifying the file's MFT record (for resident data).
 * Grows the resident $DATA attribute if needed.
 */
static NTSTATUS
NtfslxWriteResidentExtend(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ ULONGLONG ByteOffset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) const VOID *Buffer)
{
    PNTFSLX_MFT_RECORD Record;
    PNTFSLX_ATTR_RECORD DataAttr;
    ULONG OldValueLength;
    ULONG NewValueLength;
    ULONG OldAttrLength;
    ULONG NewAttrLength;
    ULONG HeaderAndName;
    ULONG ValueOffset;
    PUCHAR ValuePtr;
    PNTFSLX_DEVICE_EXTENSION DevExt;
    NTSTATUS Status;

    DevExt = FileContext->DeviceExtension;
    Record = FileContext->FileRecord;
    DataAttr = FileContext->DataAttribute;

    OldValueLength = DataAttr->Data.Resident.ValueLength;
    OldAttrLength = DataAttr->Length;
    ValueOffset = DataAttr->Data.Resident.ValueOffset;
    HeaderAndName = ValueOffset;  /* header + name + padding */

    /* Determine new value length */
    NewValueLength = OldValueLength;
    if (ByteOffset + Length > NewValueLength)
    {
        NewValueLength = (ULONG)(ByteOffset + Length);
    }

    /* New attribute length, 8-byte aligned */
    NewAttrLength = ROUND_UP(HeaderAndName + NewValueLength, 8);

    DbgPrint("ntfslx: ResidentExtend mft=%I64u ofs=%I64u len=%lu oldVal=%lu newVal=%lu oldAttr=%lu newAttr=%lu bytesInUse=%lu\n",
             FileContext->MftIndex, ByteOffset, Length, OldValueLength, NewValueLength,
             OldAttrLength, NewAttrLength, Record->BytesInUse);

    /* If too big for resident, caller must promote to non-resident */
    if (NewAttrLength > NTFSLX_MAX_RESIDENT_DATA_SIZE ||
        Record->BytesInUse - OldAttrLength + NewAttrLength > Record->BytesAllocated)
    {
        DbgPrint("ntfslx: ResidentExtend: promote needed (newAttr=%lu > %lu OR bytesInUse %lu -> %lu > alloc %lu)\n",
                 NewAttrLength, (ULONG)NTFSLX_MAX_RESIDENT_DATA_SIZE,
                 Record->BytesInUse,
                 Record->BytesInUse - OldAttrLength + NewAttrLength,
                 Record->BytesAllocated);
        return STATUS_BUFFER_TOO_SMALL;  /* signal: need non-resident promotion */
    }

    /* Resize the attribute record if needed */
    if (NewAttrLength != OldAttrLength)
    {
        Status = NtfslxResizeAttributeRecord(Record, DataAttr, NewAttrLength);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    /* Update value length */
    DataAttr->Data.Resident.ValueLength = NewValueLength;

    /* Write the data bytes */
    ValuePtr = (PUCHAR)DataAttr + ValueOffset;
    if (ByteOffset > OldValueLength)
    {
        /* Zero the gap */
        RtlZeroMemory(ValuePtr + OldValueLength, (SIZE_T)(ByteOffset - OldValueLength));
    }
    if (Length > 0 && Buffer != NULL)
    {
        RtlCopyMemory(ValuePtr + ByteOffset, Buffer, Length);
    }

    /* Write the modified MFT record back to disk */
    Status = NtfslxWriteMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                  DevExt->MftRunlist, FileContext->MftIndex,
                                  Record);
    if (NT_SUCCESS(Status))
    {
        FileContext->DataSize = NewValueLength;
        FileContext->AllocationSize = NewValueLength;
        NtfslxUpdateFcbAndCacheSizes(FileContext, NewValueLength, NewValueLength,
                                     NewValueLength);
        NtfslxUpdateFileNameSize(FileContext, NewValueLength, NewValueLength);
        FileContext->PendingDataNotify = TRUE;
    }

    return Status;
}

/*
 * Convert the file's resident $DATA to non-resident.
 * Allocates clusters for the new size, writes existing + new data there,
 * rewrites the $DATA attribute header with mapping pairs.
 */
static NTSTATUS
NtfslxPromoteToNonResident(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ ULONGLONG NewDataSize,
    _In_ ULONGLONG WriteOffset,
    _In_ ULONG WriteLength,
    _In_reads_bytes_(WriteLength) const VOID *WriteBuffer)
{
    PNTFSLX_DEVICE_EXTENSION DevExt;
    PNTFSLX_MFT_RECORD Record;
    PNTFSLX_ATTR_RECORD DataAttr;
    PNTFSLX_RUNLIST_ELEMENT Runlist = NULL;
    ULONG RunlistCount = 0;
    PUCHAR ClusterBuffer = NULL;
    ULONG ClusterSize;
    ULONGLONG AllocSize;
    ULONGLONG ClusterCount;
    ULONG OldValueLength;
    PUCHAR OldValue;
    ULONG HeaderName;
    ULONG MpOffset;
    ULONG NewAttrSize;
    ULONGLONG ByteOffset;
    ULONG I;
    NTSTATUS Status;

    DevExt = FileContext->DeviceExtension;
    Record = FileContext->FileRecord;
    DataAttr = FileContext->DataAttribute;
    ClusterSize = DevExt->VolumeInfo.BytesPerCluster;

    /* Round allocation up to a whole number of clusters */
    ClusterCount = (NewDataSize + ClusterSize - 1) / ClusterSize;
    if (ClusterCount == 0)
        ClusterCount = 1;
    AllocSize = ClusterCount * ClusterSize;

    DbgPrint("ntfslx: PromoteToNonRes: newSize=%I64u clusters=%I64u\n",
             NewDataSize, ClusterCount);

    /* Save the old resident value */
    OldValueLength = DataAttr->Data.Resident.ValueLength;
    OldValue = ExAllocatePoolWithTag(NonPagedPool,
        OldValueLength ? OldValueLength : 1, NTFSLX_TAG);
    if (OldValue == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    if (OldValueLength > 0)
    {
        RtlCopyMemory(OldValue,
                      (PUCHAR)DataAttr + DataAttr->Data.Resident.ValueOffset,
                      OldValueLength);
    }

    /* Allocate clusters */
    Status = NtfslxAllocateClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                    DevExt->MftRunlist,
                                    (ULONG)ClusterCount, 0,
                                    &Runlist, &RunlistCount);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("ntfslx: AllocateClusters failed 0x%08lx\n", Status);
        ExFreePoolWithTag(OldValue, NTFSLX_TAG);
        return Status;
    }

    /* Prepare cluster-sized buffer, zero-filled */
    ClusterBuffer = ExAllocatePoolWithTag(NonPagedPool, (SIZE_T)AllocSize, NTFSLX_TAG);
    if (ClusterBuffer == NULL)
    {
        NtfslxFreeClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                           DevExt->MftRunlist, Runlist, RunlistCount);
        ExFreePoolWithTag(Runlist, NTFSLX_TAG);
        ExFreePoolWithTag(OldValue, NTFSLX_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(ClusterBuffer, (SIZE_T)AllocSize);

    /* Merge old resident data + new write into the buffer */
    if (OldValueLength > 0)
        RtlCopyMemory(ClusterBuffer, OldValue, OldValueLength);
    if (WriteLength > 0 && WriteOffset < AllocSize)
    {
        ULONG CopyLen = WriteLength;
        if (WriteOffset + CopyLen > AllocSize)
            CopyLen = (ULONG)(AllocSize - WriteOffset);
        RtlCopyMemory(ClusterBuffer + WriteOffset, WriteBuffer, CopyLen);
    }

    /* Write the buffer to the allocated clusters */
    ByteOffset = 0;
    for (I = 0; I < RunlistCount; I++)
    {
        ULONGLONG RunBytes;
        LONGLONG DiskOffset;

        if (Runlist[I].Length <= 0 || Runlist[I].Lcn < 0)
            continue;
        RunBytes = (ULONGLONG)Runlist[I].Length * ClusterSize;
        DiskOffset = Runlist[I].Lcn * ClusterSize;
        if (ByteOffset + RunBytes > AllocSize)
            RunBytes = AllocSize - ByteOffset;
        Status = NtfslxWriteDisk(DevExt->StorageDevice, DiskOffset,
                                 (ULONG)RunBytes, DevExt->VolumeInfo.BytesPerSector,
                                 ClusterBuffer + ByteOffset, FALSE);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("ntfslx: cluster write failed at run %lu 0x%08lx\n", I, Status);
            goto Fail;
        }
        ByteOffset += RunBytes;
        if (ByteOffset >= AllocSize)
            break;
    }

    /* Encode the full allocator runlist (may be multi-run if fragmented). */
    {
        UCHAR MpBuffer[NTFSLX_MP_ENCODE_MAX];
        ULONG MpSize = 0;

        Status = NtfslxEncodeMappingPairs(Runlist, MpBuffer,
                                          sizeof(MpBuffer), &MpSize);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("ntfslx: PromoteToNonRes: encode mapping pairs failed 0x%08lx runCount=%lu\n",
                     Status, RunlistCount);
            goto Fail;
        }

        /* New layout: 16 (generic header) + 48 (non-resident block) + mapping pairs */
        HeaderName = NTFSLX_NONRESIDENT_ATTR_HEADER_SIZE;
        MpOffset = HeaderName;
        NewAttrSize = ROUND_UP(MpOffset + MpSize, 8);

        Status = NtfslxResizeAttributeRecord(Record, DataAttr, NewAttrSize);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("ntfslx: Resize to non-res size=%lu failed 0x%08lx\n",
                     NewAttrSize, Status);
            goto Fail;
        }

        /* Rewrite the attribute header as non-resident */
        DataAttr->NonResident = 1;
        DataAttr->NameLength = 0;
        DataAttr->NameOffset = 0;
        DataAttr->Flags = 0;
        DataAttr->Data.NonResident.LowestVcn = 0;
        DataAttr->Data.NonResident.HighestVcn = ClusterCount - 1;
        DataAttr->Data.NonResident.MappingPairsOffset = (USHORT)MpOffset;
        DataAttr->Data.NonResident.CompressionUnit = 0;
        RtlZeroMemory(DataAttr->Data.NonResident.Reserved,
                      sizeof(DataAttr->Data.NonResident.Reserved));
        DataAttr->Data.NonResident.AllocatedSize = AllocSize;
        DataAttr->Data.NonResident.DataSize = NewDataSize;
        DataAttr->Data.NonResident.InitializedSize = NewDataSize;
        DataAttr->Data.NonResident.CompressedSize = 0;

        /* Copy encoded mapping pairs into the attribute, zero trailing pad. */
        RtlZeroMemory((PUCHAR)DataAttr + MpOffset, NewAttrSize - MpOffset);
        RtlCopyMemory((PUCHAR)DataAttr + MpOffset, MpBuffer, MpSize);
    }

    /* Write the MFT record with new attribute layout */
    Status = NtfslxWriteMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                  DevExt->MftRunlist, FileContext->MftIndex,
                                  Record);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("ntfslx: Write MFT after promote failed 0x%08lx\n", Status);
        goto Fail;
    }

    /* Update FileContext */
    FileContext->ResidentData = FALSE;
    FileContext->DataSize = NewDataSize;
    FileContext->AllocationSize = AllocSize;
    NtfslxUpdateFcbAndCacheSizes(FileContext, AllocSize, NewDataSize, NewDataSize);

    /* Replace old runlist */
    if (FileContext->DataRunlist != NULL)
        ExFreePoolWithTag(FileContext->DataRunlist, NTFSLX_TAG);
    FileContext->DataRunlist = Runlist;
    FileContext->DataRunlistCount = RunlistCount;

    ExFreePoolWithTag(ClusterBuffer, NTFSLX_TAG);
    ExFreePoolWithTag(OldValue, NTFSLX_TAG);

    /* Propagate the new size into $FILE_NAME and parent $I30 */
    NtfslxUpdateFileNameSize(FileContext, NewDataSize, AllocSize);
    FileContext->PendingDataNotify = TRUE;

    return STATUS_SUCCESS;

Fail:
    NtfslxFreeClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                       DevExt->MftRunlist, Runlist, RunlistCount);
    ExFreePoolWithTag(Runlist, NTFSLX_TAG);
    if (ClusterBuffer != NULL)
        ExFreePoolWithTag(ClusterBuffer, NTFSLX_TAG);
    ExFreePoolWithTag(OldValue, NTFSLX_TAG);
    return Status;
}

/*
 * Extend a non-resident file's allocation by (NewAllocSize - current) bytes.
 * Allocates additional clusters, merges them into the runlist (coalescing
 * with the tail run when contiguous), rewrites the $DATA mapping pairs,
 * resizes the attribute record if needed, updates HighestVcn / AllocatedSize,
 * zero-fills the newly mapped region so no stale disk data is exposed, then
 * flushes the MFT record. DataSize / InitializedSize are untouched — the
 * caller is expected to bump those after the user write lands.
 */
static NTSTATUS
NtfslxExtendNonResident(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ ULONGLONG NewAllocSize)
{
    PNTFSLX_DEVICE_EXTENSION DevExt;
    PNTFSLX_MFT_RECORD Record;
    PNTFSLX_ATTR_RECORD DataAttr;
    PNTFSLX_RUNLIST_ELEMENT NewRunlist = NULL;
    ULONG NewRunlistCount = 0;
    PNTFSLX_RUNLIST_ELEMENT MergedRunlist = NULL;
    ULONG MergedCount;
    ULONG ClusterSize;
    ULONGLONG CurrentClusters;
    ULONGLONG NewTotalClusters;
    ULONGLONG AdditionalClusters;
    ULONGLONG ActualNewAllocSize;
    ULONGLONG ExtendStartByte;
    UCHAR MpBuffer[NTFSLX_MP_ENCODE_MAX];
    ULONG MpSize = 0;
    ULONG MpOffset;
    ULONG OldAttrLength;
    ULONG NewAttrLength;
    ULONG OldRuns;
    ULONG NewRuns;
    ULONG I;
    LONGLONG HintLcn;
    BOOLEAN CanMerge;
    NTSTATUS Status;

    DevExt = FileContext->DeviceExtension;
    Record = FileContext->FileRecord;
    DataAttr = FileContext->DataAttribute;
    ClusterSize = DevExt->VolumeInfo.BytesPerCluster;

    if (DataAttr == NULL || DataAttr->NonResident == 0)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }
    if (FileContext->DataRunlist == NULL || FileContext->DataRunlistCount == 0)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Status = NtfslxEnsureNonResidentRunlistState(FileContext);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    CurrentClusters = FileContext->AllocationSize / ClusterSize;
    NewTotalClusters = (NewAllocSize + ClusterSize - 1) / ClusterSize;
    if (NewTotalClusters <= CurrentClusters)
    {
        return STATUS_SUCCESS;
    }
    AdditionalClusters = NewTotalClusters - CurrentClusters;
    ActualNewAllocSize = NewTotalClusters * ClusterSize;
    ExtendStartByte = CurrentClusters * ClusterSize;

    DbgPrint("ntfslx: ExtendNonRes mft=%I64u curClusters=%I64u addClusters=%I64u newAlloc=%I64u\n",
             FileContext->MftIndex, CurrentClusters, AdditionalClusters, ActualNewAllocSize);

    /* Hint the allocator to try placing the extension contiguous with the
     * existing tail run, so the common sequential-write case stays one run. */
    HintLcn = 0;
    if (FileContext->DataRunlistCount >= 2)
    {
        ULONG TailIdx = FileContext->DataRunlistCount - 2; /* entry before sentinel */
        PNTFSLX_RUNLIST_ELEMENT TailRun = &FileContext->DataRunlist[TailIdx];
        if (TailRun->Lcn >= 0 && TailRun->Length > 0)
        {
            HintLcn = TailRun->Lcn + TailRun->Length;
        }
    }

    Status = NtfslxAllocateClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                    DevExt->MftRunlist,
                                    AdditionalClusters, (ULONGLONG)HintLcn,
                                    &NewRunlist, &NewRunlistCount);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("ntfslx: ExtendNonRes: AllocateClusters failed 0x%08lx\n", Status);
        return Status;
    }

    OldRuns = FileContext->DataRunlistCount - 1;  /* excluding sentinel */
    NewRuns = NewRunlistCount - 1;

    CanMerge = FALSE;
    if (OldRuns > 0 && NewRuns > 0)
    {
        PNTFSLX_RUNLIST_ELEMENT LastOld = &FileContext->DataRunlist[OldRuns - 1];
        PNTFSLX_RUNLIST_ELEMENT FirstNew = &NewRunlist[0];
        if (LastOld->Lcn >= 0 && FirstNew->Lcn >= 0 &&
            LastOld->Lcn + LastOld->Length == FirstNew->Lcn)
        {
            CanMerge = TRUE;
        }
    }

    {
        ULONG MergedCapacity = CanMerge ? (OldRuns + NewRuns)
                                        : (OldRuns + NewRuns + 1);
        MergedRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                              MergedCapacity * sizeof(NTFSLX_RUNLIST_ELEMENT),
                                              NTFSLX_TAG);
        if (MergedRunlist == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Fail;
        }
        RtlZeroMemory(MergedRunlist, MergedCapacity * sizeof(NTFSLX_RUNLIST_ELEMENT));
    }

    /* Copy old real runs (skip old sentinel). */
    RtlCopyMemory(MergedRunlist,
                  FileContext->DataRunlist,
                  OldRuns * sizeof(NTFSLX_RUNLIST_ELEMENT));
    MergedCount = OldRuns;

    /* Append new runs, shifted into the file's VCN space. */
    for (I = 0; I < NewRuns; I++)
    {
        if (CanMerge && I == 0)
        {
            MergedRunlist[MergedCount - 1].Length += NewRunlist[I].Length;
            continue;
        }
        MergedRunlist[MergedCount].Vcn = NewRunlist[I].Vcn + (LONGLONG)CurrentClusters;
        MergedRunlist[MergedCount].Lcn = NewRunlist[I].Lcn;
        MergedRunlist[MergedCount].Length = NewRunlist[I].Length;
        MergedCount++;
    }

    /* Sentinel */
    MergedRunlist[MergedCount].Vcn = (LONGLONG)NewTotalClusters;
    MergedRunlist[MergedCount].Lcn = NTFSLX_LCN_ENOENT;
    MergedRunlist[MergedCount].Length = 0;
    MergedCount++;

    /* Encode the merged runlist into mapping pairs. */
    Status = NtfslxEncodeMappingPairs(MergedRunlist, MpBuffer, sizeof(MpBuffer),
                                      &MpSize);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("ntfslx: ExtendNonRes: encode mapping pairs failed 0x%08lx runs=%lu\n",
                 Status, MergedCount);
        goto Fail;
    }

    MpOffset = DataAttr->Data.NonResident.MappingPairsOffset;
    if (MpOffset == 0)
    {
        MpOffset = NTFSLX_NONRESIDENT_ATTR_HEADER_SIZE;
    }
    OldAttrLength = DataAttr->Length;
    NewAttrLength = ROUND_UP(MpOffset + MpSize, 8);

    if (NewAttrLength != OldAttrLength)
    {
        Status = NtfslxResizeAttributeRecord(Record, DataAttr, NewAttrLength);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("ntfslx: ExtendNonRes: resize attr %lu->%lu failed 0x%08lx\n",
                     OldAttrLength, NewAttrLength, Status);
            goto Fail;
        }
    }

    /* Patch mapping pairs and non-resident header fields. */
    RtlZeroMemory((PUCHAR)DataAttr + MpOffset, NewAttrLength - MpOffset);
    RtlCopyMemory((PUCHAR)DataAttr + MpOffset, MpBuffer, MpSize);
    DataAttr->Data.NonResident.HighestVcn = NewTotalClusters - 1;
    DataAttr->Data.NonResident.AllocatedSize = ActualNewAllocSize;

    /*
     * Zero-fill the newly mapped clusters BEFORE the MFT record commit.
     * The ordering matters for crash safety: if we committed the MFT first
     * and then crashed before zeroing, the file would reference clusters
     * that still hold stale data from whoever had them before. Zero-fill
     * uses the in-memory merged runlist and the bitmap-committed LCNs, so
     * it doesn't need the MFT record on disk first.
     */
    Status = NtfslxZeroFillRunRange(DevExt, MergedRunlist, ExtendStartByte,
                                    ActualNewAllocSize - ExtendStartByte);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("ntfslx: ExtendNonRes: zero fill failed 0x%08lx\n", Status);
        goto Fail;
    }

    Status = NtfslxWriteMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                  DevExt->MftRunlist, FileContext->MftIndex,
                                  Record);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("ntfslx: ExtendNonRes: WriteMft failed 0x%08lx\n", Status);
        goto Fail;
    }

    /* Swap in the merged runlist and update the in-memory allocation. */
    if (FileContext->DataRunlist != NULL)
    {
        ExFreePoolWithTag(FileContext->DataRunlist, NTFSLX_TAG);
    }
    FileContext->DataRunlist = MergedRunlist;
    FileContext->DataRunlistCount = MergedCount;
    FileContext->AllocationSize = ActualNewAllocSize;
    MergedRunlist = NULL;

    NtfslxUpdateFcbAndCacheSizes(FileContext, ActualNewAllocSize,
                                 FileContext->DataSize,
                                 FileContext->DataSize);

    /* Free the raw allocator output (its contents are now embedded in the merged
     * runlist, not shared, so we free the array structure only). */
    ExFreePoolWithTag(NewRunlist, NTFSLX_TAG);
    NewRunlist = NULL;

    DbgPrint("ntfslx: ExtendNonRes mft=%I64u ok newAlloc=%I64u runs=%lu\n",
             FileContext->MftIndex, ActualNewAllocSize, MergedCount);

    return STATUS_SUCCESS;

Fail:
    if (NewRunlist != NULL)
    {
        NtfslxFreeClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                           DevExt->MftRunlist, NewRunlist, NewRunlistCount);
        ExFreePoolWithTag(NewRunlist, NTFSLX_TAG);
    }
    if (MergedRunlist != NULL)
    {
        ExFreePoolWithTag(MergedRunlist, NTFSLX_TAG);
    }
    return Status;
}

/*
 * Write to a non-resident file. Extends the allocation (via
 * NtfslxExtendNonResident) if the write goes past EOF, then streams
 * the user data into the mapped clusters.
 */
static NTSTATUS
NtfslxWriteNonResidentOverwrite(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ ULONGLONG ByteOffset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) const VOID *Buffer)
{
    PNTFSLX_DEVICE_EXTENSION DevExt;
    ULONG ClusterSize;
    ULONG SectorSize;
    ULONGLONG Remaining;
    ULONGLONG CurrentOffset;
    const UCHAR *Src;
    ULONG I;
    NTSTATUS Status;

    NTSTATUS NormalizeStatus;

    DevExt = FileContext->DeviceExtension;
    ClusterSize = DevExt->VolumeInfo.BytesPerCluster;
    SectorSize = DevExt->VolumeInfo.BytesPerSector;

    NormalizeStatus = NtfslxEnsureNonResidentRunlistState(FileContext);
    if (!NT_SUCCESS(NormalizeStatus))
    {
        return NormalizeStatus;
    }

    if (ByteOffset + Length > FileContext->AllocationSize)
    {
        NTSTATUS ExtendStatus = NtfslxExtendNonResident(FileContext,
                                                        ByteOffset + Length);
        if (!NT_SUCCESS(ExtendStatus))
        {
            return ExtendStatus;
        }
    }

    CurrentOffset = ByteOffset;
    Remaining = Length;
    Src = (const UCHAR *)Buffer;

    for (I = 0; I < FileContext->DataRunlistCount && Remaining > 0; I++)
    {
        NTFSLX_RUNLIST_ELEMENT *Run = &FileContext->DataRunlist[I];
        ULONGLONG RunStart;
        ULONGLONG RunEnd;
        ULONGLONG WriteOfs;
        ULONGLONG WriteLen;
        LONGLONG DiskOfs;

        if (Run->Length <= 0 || Run->Lcn < 0)
            continue;

        RunStart = (ULONGLONG)Run->Vcn * ClusterSize;
        RunEnd = RunStart + (ULONGLONG)Run->Length * ClusterSize;

        if (CurrentOffset >= RunEnd)
            continue;
        if (CurrentOffset < RunStart)
            continue;  /* shouldn't happen with sorted runlist */

        WriteOfs = CurrentOffset - RunStart;
        WriteLen = (RunEnd - CurrentOffset);
        if (WriteLen > Remaining)
            WriteLen = Remaining;

        DiskOfs = Run->Lcn * ClusterSize + (LONGLONG)WriteOfs;
        Status = NtfslxWriteDisk(DevExt->StorageDevice, DiskOfs,
                                 (ULONG)WriteLen, SectorSize,
                                 (PUCHAR)Src, FALSE);
        if (!NT_SUCCESS(Status))
            return Status;

        Src += WriteLen;
        CurrentOffset += WriteLen;
        Remaining -= WriteLen;
    }

    if (Remaining > 0)
        return STATUS_UNEXPECTED_IO_ERROR;

    /* Update DataSize if the write extended it */
    if (ByteOffset + Length > FileContext->DataSize)
    {
        PNTFSLX_MFT_RECORD Record = FileContext->FileRecord;
        FileContext->DataSize = ByteOffset + Length;
        FileContext->DataAttribute->Data.NonResident.DataSize = FileContext->DataSize;
        FileContext->DataAttribute->Data.NonResident.InitializedSize = FileContext->DataSize;
        NtfslxUpdateFcbAndCacheSizes(FileContext,
                                     FileContext->AllocationSize,
                                     FileContext->DataSize,
                                     FileContext->DataSize);
        NtfslxWriteMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                             DevExt->MftRunlist, FileContext->MftIndex, Record);
        NtfslxUpdateFileNameSize(FileContext,
                                 FileContext->DataSize,
                                 FileContext->DataAttribute->Data.NonResident.AllocatedSize);
        FileContext->PendingDataNotify = TRUE;
    }

    return STATUS_SUCCESS;
}

/*
 * Set a file's end-of-file (EOF). Extend via resident or promoted non-resident
 * path, truncate in place.
 */
NTSTATUS
NtfslxSetFileSize(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ ULONGLONG NewSize)
{
    PNTFSLX_DEVICE_EXTENSION DevExt;
    PNTFSLX_MFT_RECORD Record;
    PNTFSLX_ATTR_RECORD DataAttr;
    NTSTATUS Status;

    if (FileContext == NULL || FileContext->IsDirectory)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (FileContext->DataAttribute == NULL)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    DevExt = FileContext->DeviceExtension;
    Record = FileContext->FileRecord;
    DataAttr = FileContext->DataAttribute;

    DbgPrint("ntfslx: SetFileSize mft=%I64u newSize=%I64u curSize=%I64u resident=%u\n",
             FileContext->MftIndex, NewSize, FileContext->DataSize,
             FileContext->ResidentData);

    if (NewSize == FileContext->DataSize)
    {
        return STATUS_SUCCESS;
    }

    if (NewSize > FileContext->DataSize)
    {
        /* Extend */
        if (FileContext->ResidentData)
        {
            Status = NtfslxWriteResidentExtend(FileContext, NewSize, 0, NULL);
            if (Status == STATUS_BUFFER_TOO_SMALL)
            {
                Status = NtfslxPromoteToNonResident(FileContext, NewSize,
                                                    0, 0, NULL);
            }
            return Status;
        }
        else
        {
            if (NewSize > FileContext->AllocationSize)
            {
                Status = NtfslxExtendNonResident(FileContext, NewSize);
                if (!NT_SUCCESS(Status))
                {
                    return Status;
                }
                /* DataAttr pointer is stable across extend, but its contents
                 * (including resident/non-resident layout) were rewritten -
                 * re-load it so local DataAttr alias is current. */
                DataAttr = FileContext->DataAttribute;
            }
            /* InitializedSize / DataSize grow within allocation */
            FileContext->DataSize = NewSize;
            DataAttr->Data.NonResident.DataSize = NewSize;
            DataAttr->Data.NonResident.InitializedSize = NewSize;
            NtfslxUpdateFcbAndCacheSizes(FileContext,
                                         FileContext->AllocationSize,
                                         NewSize, NewSize);
            Status = NtfslxWriteMftRecord(DevExt->StorageDevice,
                                          &DevExt->VolumeInfo,
                                          DevExt->MftRunlist,
                                          FileContext->MftIndex,
                                          Record);
            if (NT_SUCCESS(Status))
            {
                NtfslxUpdateFileNameSize(FileContext, NewSize,
                                         FileContext->AllocationSize);
            }
            return Status;
        }
    }

    /* Truncate */
    if (FileContext->ResidentData)
    {
        ULONG HeaderAndName = DataAttr->Data.Resident.ValueOffset;
        ULONG NewAttrLength = ROUND_UP(HeaderAndName + (ULONG)NewSize, 8);

        Status = NtfslxResizeAttributeRecord(Record, DataAttr, NewAttrLength);
        if (!NT_SUCCESS(Status))
            return Status;

        DataAttr->Data.Resident.ValueLength = (ULONG)NewSize;
        Status = NtfslxWriteMftRecord(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                      DevExt->MftRunlist, FileContext->MftIndex,
                                      Record);
        if (NT_SUCCESS(Status))
        {
            FileContext->DataSize = NewSize;
            FileContext->AllocationSize = NewSize;
            NtfslxUpdateFcbAndCacheSizes(FileContext, NewSize, NewSize, NewSize);
            NtfslxUpdateFileNameSize(FileContext, NewSize, NewSize);
            FileContext->PendingDataNotify = TRUE;
        }
        return Status;
    }
    else
    {
        return NtfslxTruncateNonResident(FileContext, NewSize);
    }
}

/*
 * Main extended write entry point.
 */
NTSTATUS
NtfslxExtendedWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length,
    _In_ PLARGE_INTEGER ByteOffset,
    _Out_opt_ PULONG BytesWritten)
{
    PNTFSLX_FILE_CONTEXT FileContext;
    ULONGLONG EffectiveOffset;
    NTSTATUS Status;

    if (BytesWritten != NULL)
        *BytesWritten = 0;

    if (FileObject == NULL || ByteOffset == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Length > 0 && Buffer == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Length == 0)
        return STATUS_SUCCESS;

    FileContext = (PNTFSLX_FILE_CONTEXT)FileObject->FsContext;
    if (FileContext == NULL ||
        FileContext->Signature != NTFSLX_FILE_CONTEXT_SIGNATURE ||
        FileContext->IsDirectory)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (ByteOffset->QuadPart < 0)
        return STATUS_INVALID_PARAMETER;
    EffectiveOffset = (ULONGLONG)ByteOffset->QuadPart;

    DbgPrint("ntfslx: ExtendedWrite mft=%I64u ofs=%I64u len=%lu resident=%u dataSize=%I64u\n",
             FileContext->MftIndex, EffectiveOffset, Length,
             FileContext->ResidentData, FileContext->DataSize);

    if (FileContext->ResidentData)
    {
        Status = NtfslxWriteResidentExtend(FileContext, EffectiveOffset, Length, Buffer);
        if (Status == STATUS_BUFFER_TOO_SMALL)
        {
            /* Too big for resident -- promote to non-resident */
            ULONGLONG NewSize = EffectiveOffset + Length;
            if (FileContext->DataSize > NewSize)
                NewSize = FileContext->DataSize;
            Status = NtfslxPromoteToNonResident(FileContext, NewSize,
                                                EffectiveOffset, Length, Buffer);
        }
    }
    else
    {
        Status = NtfslxWriteNonResidentOverwrite(FileContext, EffectiveOffset,
                                                 Length, Buffer);
    }

    if (NT_SUCCESS(Status) && BytesWritten != NULL)
        *BytesWritten = Length;

    DbgPrint("ntfslx: ExtendedWrite result=0x%08lx\n", Status);
    return Status;
}
