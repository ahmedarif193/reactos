/*
 * PROJECT:     ReactOS NTFS driver (ntfslx)
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     MFT record write, allocate, and free operations
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

/*
 * NtfslxRunlistVcnToLcnLocal - translate VCN to LCN via runlist
 *
 * Duplicated locally to avoid cross-module static linkage issues.
 */
static
LONGLONG
NtfslxRunlistVcnToLcnLocal(
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ LONGLONG Vcn)
{
    ULONG Index;

    if (Runlist == NULL)
    {
        return NTFSLX_LCN_RL_NOT_MAPPED;
    }

    if (Vcn < Runlist[0].Vcn)
    {
        return NTFSLX_LCN_ENOENT;
    }

    for (Index = 0; Runlist[Index].Length != 0; ++Index)
    {
        if (Vcn < Runlist[Index + 1].Vcn)
        {
            if (Runlist[Index].Lcn >= 0)
            {
                return Runlist[Index].Lcn + (Vcn - Runlist[Index].Vcn);
            }

            return Runlist[Index].Lcn;
        }
    }

    return Runlist[Index].Lcn;
}

/*
 * NtfslxWriteMappedDataLocal - write data through a runlist mapping
 */
static
NTSTATUS
NtfslxWriteMappedDataLocal(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ ULONGLONG ByteOffset,
    _In_ ULONG Length,
    _In_reads_bytes_(Length) PUCHAR Buffer)
{
    ULONGLONG Remaining;
    ULONGLONG CurrentOffset;
    LONGLONG Vcn;
    LONGLONG Lcn;
    ULONG ClusterOffset;
    ULONG ChunkLength;
    NTSTATUS Status;

    Remaining = Length;
    CurrentOffset = ByteOffset;

    while (Remaining != 0)
    {
        Vcn = (LONGLONG)(CurrentOffset / VolumeInfo->BytesPerCluster);
        ClusterOffset = (ULONG)(CurrentOffset % VolumeInfo->BytesPerCluster);
        ChunkLength = VolumeInfo->BytesPerCluster - ClusterOffset;
        if ((ULONGLONG)ChunkLength > Remaining)
        {
            ChunkLength = (ULONG)Remaining;
        }

        Lcn = NtfslxRunlistVcnToLcnLocal(Runlist, Vcn);
        if (Lcn < 0)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Status = NtfslxWriteDisk(StorageDevice,
                                 (LONGLONG)((ULONGLONG)Lcn * VolumeInfo->BytesPerCluster + ClusterOffset),
                                 ChunkLength,
                                 VolumeInfo->BytesPerSector,
                                 Buffer,
                                 TRUE);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        Buffer += ChunkLength;
        CurrentOffset += ChunkLength;
        Remaining -= ChunkLength;
    }

    return STATUS_SUCCESS;
}

/*
 * Mirror the first four MFT records ($MFT, $MFTMirr, $LogFile, $Volume)
 * to the $MFTMirr area at VolumeInfo->MftMirrLcn. Called immediately
 * after the primary write of records 0..3 so a torn-write on the live
 * MFT can be recovered by chkdsk reading the mirror. Without this, a
 * crash during a write to records 0..3 bricks the volume — this is
 * TIER 0 for boot-NTFS correctness.
 */
static NTSTATUS
NtfslxWriteMftMirrorRecord(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ ULONGLONG RecordNumber,
    _In_reads_bytes_(RecordSize) PUCHAR RecordBytes,
    _In_ ULONG RecordSize)
{
    ULONGLONG MirrorByteOffset;

    if (VolumeInfo->MftMirrLcn == 0 ||
        VolumeInfo->BytesPerCluster == 0)
    {
        return STATUS_SUCCESS;
    }

    /*
     * The mirror is a contiguous 4096-byte area starting at MftMirrLcn
     * on all NTFS geometries we support (1 KB record * 4 records = 4 KB,
     * which is exactly one cluster on 4 KB clusters and one block on
     * smaller cluster sizes too). Records 0..3 land at consecutive
     * offsets inside it.
     */
    MirrorByteOffset = VolumeInfo->MftMirrLcn * (ULONGLONG)VolumeInfo->BytesPerCluster +
                       RecordNumber * (ULONGLONG)RecordSize;

    return NtfslxWriteDisk(StorageDevice,
                           (LONGLONG)MirrorByteOffset,
                           RecordSize,
                           VolumeInfo->BytesPerSector,
                           RecordBytes,
                           TRUE);
}

NTSTATUS
NtfslxWriteMftRecord(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG RecordNumber,
    _Inout_ PNTFSLX_MFT_RECORD Record)
{
    PNTFSLX_MFT_RECORD RecordCopy;
    ULONG RecordSize;
    ULONGLONG MftOffset;
    NTSTATUS Status;
    NTSTATUS MirrorStatus;

    RecordSize = VolumeInfo->BytesPerFileRecord;

    RecordCopy = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (RecordCopy == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(RecordCopy, Record, RecordSize);

    /* Apply MST fixup before writing */
    Status = NtfslxPreWriteMstFixup(&RecordCopy->Ntfs, RecordSize);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(RecordCopy, NTFSLX_TAG);
        return Status;
    }

    MftOffset = RecordNumber * (ULONGLONG)RecordSize;

    Status = NtfslxWriteMappedDataLocal(StorageDevice,
                                        VolumeInfo,
                                        MftRunlist,
                                        MftOffset,
                                        RecordSize,
                                        (PUCHAR)RecordCopy);

    /*
     * Also write the mirror copy for records 0..3. The mirror is written
     * with the MST-fixed-up bytes so chkdsk's post-read fixup matches.
     * Mirror write failures are logged but non-fatal: the primary write
     * is already persisted, so the volume state is at worst one-crash
     * behind on the four system records — still recoverable.
     */
    if (NT_SUCCESS(Status) && RecordNumber < 4)
    {
        MirrorStatus = NtfslxWriteMftMirrorRecord(StorageDevice,
                                                   VolumeInfo,
                                                   RecordNumber,
                                                   (PUCHAR)RecordCopy,
                                                   RecordSize);
        if (!NT_SUCCESS(MirrorStatus))
        {
            NTFSDBG("ntfslx: WriteMftRecord: mirror write rec=%I64u failed 0x%08lx (non-fatal)\n",
                     RecordNumber, MirrorStatus);
        }
    }

    /* Restore in-memory record after write */
    NtfslxPostWriteMstFixup(&RecordCopy->Ntfs);

    ExFreePoolWithTag(RecordCopy, NTFSLX_TAG);
    return Status;
}

static
NTSTATUS
NtfslxGetMftRecordCapacity(
    _In_ PNTFSLX_MFT_RECORD MftRecord,
    _In_ ULONG RecordSize,
    _Out_ PULONGLONG RecordCount)
{
    PNTFSLX_ATTR_RECORD DataAttr;
    ULONGLONG DataBytes;
    NTSTATUS Status;

    ASSERT(RecordSize != 0);

    *RecordCount = 0;

    Status = NtfslxFindAttribute(MftRecord, NTFSLX_ATTRIBUTE_DATA,
                                 NULL, 0, &DataAttr);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (DataAttr->NonResident == 0)
    {
        DataBytes = DataAttr->Data.Resident.ValueLength;
    }
    else
    {
        DataBytes = DataAttr->Data.NonResident.AllocatedSize;
    }

    *RecordCount = DataBytes / RecordSize;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxEnsureMftBitmapCoverage(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _Inout_ PNTFSLX_MFT_RECORD MftRecord,
    _In_ ULONGLONG RequiredRecords,
    _Out_ PBOOLEAN Modified)
{
    PNTFSLX_ATTR_RECORD BitmapAttr;
    PNTFSLX_RUNLIST_ELEMENT BitmapRunlist = NULL;
    PNTFSLX_RUNLIST_ELEMENT ActiveRunlist = NULL;
    PNTFSLX_RUNLIST_ELEMENT NewRuns = NULL;
    PNTFSLX_RUNLIST_ELEMENT MergedRunlist = NULL;
    PNTFSLX_MFT_RECORD ScanRecord = NULL;
    PUCHAR BitmapData = NULL;
    PUCHAR MappingPairs = NULL;
    ULONGLONG RequiredBytes;
    ULONGLONG OldDataBytes;
    ULONGLONG OldAllocBytes;
    ULONGLONG CurrentClusters;
    ULONGLONG NewTotalClusters;
    ULONGLONG AdditionalClusters;
    ULONGLONG ScanBit;
    ULONG BitmapRunlistCount = 0;
    ULONG NewRunCount = 0;
    ULONG MergedCount = 0;
    ULONG RequiredBitmapLength;
    ULONG RecordSize;
    ULONG ClusterSize;
    ULONG I;
    ULONG NewAttrLength;
    ULONG MpOffset;
    LONG MpLength;
    LONGLONG HintLcn;
    NTSTATUS Status;

    *Modified = FALSE;

    if (RequiredRecords == 0)
    {
        return STATUS_SUCCESS;
    }

    RequiredBytes = (RequiredRecords + 7) / 8;
    if (RequiredBytes > MAXULONG)
    {
        return STATUS_DISK_FULL;
    }
    RequiredBitmapLength = (ULONG)RequiredBytes;
    RecordSize = DevExt->VolumeInfo.BytesPerFileRecord;
    ClusterSize = DevExt->VolumeInfo.BytesPerCluster;

    Status = NtfslxFindAttribute(MftRecord, NTFSLX_ATTRIBUTE_BITMAP,
                                 NULL, 0, &BitmapAttr);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: MftBitmap: FindAttribute($BITMAP) failed 0x%08lx\n",
                 Status);
        return Status;
    }

    if (BitmapAttr->NonResident == 0)
    {
        OldDataBytes = BitmapAttr->Data.Resident.ValueLength;
        if (RequiredBytes <= OldDataBytes)
        {
            return STATUS_SUCCESS;
        }

        BitmapData = ExAllocatePoolWithTag(NonPagedPool,
                                           RequiredBitmapLength,
                                           NTFSLX_TAG);
        if (BitmapData == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(BitmapData, RequiredBitmapLength);
        if (OldDataBytes != 0)
        {
            RtlCopyMemory(BitmapData,
                          (PUCHAR)BitmapAttr + BitmapAttr->Data.Resident.ValueOffset,
                          (ULONG)OldDataBytes);
        }
    }
    else
    {
        OldDataBytes = BitmapAttr->Data.NonResident.DataSize;
        OldAllocBytes = BitmapAttr->Data.NonResident.AllocatedSize;
        if (RequiredBytes <= OldDataBytes)
        {
            return STATUS_SUCCESS;
        }

        Status = NtfslxMappingPairsDecompress(&DevExt->VolumeInfo, BitmapAttr,
                                              &BitmapRunlist, &BitmapRunlistCount);
        if (!NT_SUCCESS(Status))
        {
            NTFSDBG("ntfslx: MftBitmap: decompress failed 0x%08lx\n", Status);
            return Status;
        }
        ActiveRunlist = BitmapRunlist;

        if (RequiredBytes > OldAllocBytes)
        {
            CurrentClusters = (OldAllocBytes + ClusterSize - 1) / ClusterSize;
            NewTotalClusters = (RequiredBytes + ClusterSize - 1) / ClusterSize;
            AdditionalClusters = NewTotalClusters - CurrentClusters;

            HintLcn = 0;
            if (BitmapRunlistCount >= 2)
            {
                PNTFSLX_RUNLIST_ELEMENT TailRun = &BitmapRunlist[BitmapRunlistCount - 2];
                if (TailRun->Lcn >= 0 && TailRun->Length > 0)
                {
                    HintLcn = TailRun->Lcn + TailRun->Length;
                }
            }

            Status = NtfslxAllocateClusters(DevExt->StorageDevice,
                                            &DevExt->VolumeInfo,
                                            DevExt->MftRunlist,
                                            AdditionalClusters,
                                            (ULONGLONG)HintLcn,
                                            &NewRuns,
                                            &NewRunCount);
            if (!NT_SUCCESS(Status))
            {
                NTFSDBG("ntfslx: MftBitmap: AllocateClusters failed 0x%08lx add=%I64u\n",
                         Status, AdditionalClusters);
                goto Cleanup;
            }

            MergedRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                                  (BitmapRunlistCount + NewRunCount + 1) *
                                                      sizeof(NTFSLX_RUNLIST_ELEMENT),
                                                  NTFSLX_TAG);
            if (MergedRunlist == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Cleanup;
            }

            RtlZeroMemory(MergedRunlist,
                          (BitmapRunlistCount + NewRunCount + 1) *
                              sizeof(NTFSLX_RUNLIST_ELEMENT));

            for (I = 0; I + 1 < BitmapRunlistCount; ++I)
            {
                MergedRunlist[MergedCount++] = BitmapRunlist[I];
            }
            for (I = 0; I + 1 < NewRunCount; ++I)
            {
                MergedRunlist[MergedCount] = NewRuns[I];
                MergedRunlist[MergedCount].Vcn += (LONGLONG)CurrentClusters;
                MergedCount++;
            }
            MergedRunlist[MergedCount].Vcn = (LONGLONG)NewTotalClusters;
            MergedRunlist[MergedCount].Lcn = NTFSLX_LCN_ENOENT;
            MergedRunlist[MergedCount].Length = 0;
            ActiveRunlist = MergedRunlist;

            MpLength = NtfslxGetSizeForMappingPairs(0, MergedRunlist, MergedCount,
                                                    0, -1);
            if (MpLength <= 0)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Cleanup;
            }

            MappingPairs = ExAllocatePoolWithTag(NonPagedPool, MpLength, NTFSLX_TAG);
            if (MappingPairs == NULL)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Cleanup;
            }

            RtlZeroMemory(MappingPairs, MpLength);
            Status = NtfslxMappingPairsBuild(0, MappingPairs, MpLength,
                                             MergedRunlist, MergedCount,
                                             0, -1, NULL);
            if (!NT_SUCCESS(Status))
            {
                NTFSDBG("ntfslx: MftBitmap: MappingPairsBuild failed 0x%08lx\n",
                         Status);
                goto Cleanup;
            }

            MpOffset = BitmapAttr->Data.NonResident.MappingPairsOffset;
            NewAttrLength = ROUND_UP(MpOffset + (ULONG)MpLength, 8);
            if (NewAttrLength > BitmapAttr->Length)
            {
                Status = NtfslxResizeAttributeRecord(MftRecord, BitmapAttr,
                                                     NewAttrLength);
                if (!NT_SUCCESS(Status))
                {
                    NTFSDBG("ntfslx: MftBitmap: resize attr %lu->%lu failed 0x%08lx\n",
                             BitmapAttr->Length, NewAttrLength, Status);
                    goto Cleanup;
                }
            }

            RtlZeroMemory((PUCHAR)BitmapAttr + MpOffset,
                          BitmapAttr->Length - MpOffset);
            RtlCopyMemory((PUCHAR)BitmapAttr + MpOffset,
                          MappingPairs, (ULONG)MpLength);
            BitmapAttr->Data.NonResident.HighestVcn = NewTotalClusters - 1;
            BitmapAttr->Data.NonResident.AllocatedSize =
                NewTotalClusters * ClusterSize;
            OldAllocBytes = BitmapAttr->Data.NonResident.AllocatedSize;
        }

        BitmapData = ExAllocatePoolWithTag(NonPagedPool,
                                           RequiredBitmapLength,
                                           NTFSLX_TAG);
        if (BitmapData == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        RtlZeroMemory(BitmapData, RequiredBitmapLength);
        if (OldDataBytes != 0)
        {
            Status = NtfslxReadMappedAttributeData(DevExt->StorageDevice,
                                                   &DevExt->VolumeInfo,
                                                   ActiveRunlist,
                                                   0,
                                                   (ULONG)OldDataBytes,
                                                   BitmapData);
            if (!NT_SUCCESS(Status))
            {
                NTFSDBG("ntfslx: MftBitmap: read old data failed 0x%08lx\n",
                         Status);
                goto Cleanup;
            }
        }
    }

    if (OldDataBytes * 8 < RequiredRecords)
    {
        ScanRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
        if (ScanRecord == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        for (ScanBit = OldDataBytes * 8; ScanBit < RequiredRecords; ++ScanBit)
        {
            BOOLEAN InUse = FALSE;

            Status = NtfslxReadMftRecord(DevExt->StorageDevice,
                                         &DevExt->VolumeInfo,
                                         DevExt->MftRunlist,
                                         ScanBit,
                                         ScanRecord);
            if (NT_SUCCESS(Status) &&
                ScanRecord->Ntfs.Magic == NTFSLX_RECORD_MAGIC_FILE &&
                (ScanRecord->Flags & NTFSLX_MFT_RECORD_IN_USE) != 0)
            {
                InUse = TRUE;
            }

            if (InUse)
            {
                BitmapData[ScanBit >> 3] |= (UCHAR)(1u << (ScanBit & 7));
            }
            else
            {
                BitmapData[ScanBit >> 3] &= (UCHAR)~(1u << (ScanBit & 7));
            }
        }
    }

    NTFSDBG("ntfslx: MftBitmap: grow %I64u -> %I64u bytes for %I64u records\n",
             OldDataBytes, RequiredBytes, RequiredRecords);

    if (BitmapAttr->NonResident == 0)
    {
        Status = NtfslxResizeAttributeRecord(
            MftRecord,
            BitmapAttr,
            BitmapAttr->Data.Resident.ValueOffset + RequiredBitmapLength);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }

        RtlCopyMemory((PUCHAR)BitmapAttr + BitmapAttr->Data.Resident.ValueOffset,
                      BitmapData, RequiredBitmapLength);
        BitmapAttr->Data.Resident.ValueLength = RequiredBitmapLength;
    }
    else
    {
        Status = NtfslxWriteMappedDataLocal(DevExt->StorageDevice,
                                            &DevExt->VolumeInfo,
                                            ActiveRunlist,
                                            0,
                                            RequiredBitmapLength,
                                            BitmapData);
        if (!NT_SUCCESS(Status))
        {
            NTFSDBG("ntfslx: MftBitmap: write grown bitmap failed 0x%08lx\n",
                     Status);
            goto Cleanup;
        }

        BitmapAttr->Data.NonResident.DataSize = RequiredBytes;
        BitmapAttr->Data.NonResident.InitializedSize = RequiredBytes;
        ASSERT(BitmapAttr->Data.NonResident.DataSize <=
               BitmapAttr->Data.NonResident.AllocatedSize);
    }

    *Modified = TRUE;
    Status = STATUS_SUCCESS;

Cleanup:
    if (MappingPairs != NULL)
    {
        ExFreePoolWithTag(MappingPairs, NTFSLX_TAG);
    }
    if (BitmapData != NULL)
    {
        ExFreePoolWithTag(BitmapData, NTFSLX_TAG);
    }
    if (ScanRecord != NULL)
    {
        ExFreePoolWithTag(ScanRecord, NTFSLX_TAG);
    }
    if (MergedRunlist != NULL)
    {
        ExFreePoolWithTag(MergedRunlist, NTFSLX_TAG);
    }
    if (NewRuns != NULL)
    {
        ExFreePoolWithTag(NewRuns, NTFSLX_TAG);
    }
    if (BitmapRunlist != NULL)
    {
        ExFreePoolWithTag(BitmapRunlist, NTFSLX_TAG);
    }

    return Status;
}

/*
 * Extend the $MFT's $DATA data stream by AdditionalClusters. Used when
 * NtfslxAllocateMftRecord exhausts the currently-allocated record range
 * (i.e. bitmap cap is past $MFT $DATA AllocatedSize / RecordSize).
 *
 * On success:
 *   - The in-memory MftRecord buffer is mutated: mapping pairs rewritten,
 *     $DATA.AllocatedSize / DataSize / InitializedSize / HighestVcn bumped,
 *     and the caller is expected to NtfslxWriteMftRecord it back.
 *   - DeviceExtension->MftRunlist is replaced with the new merged runlist
 *     (the old one is freed) so subsequent ReadMftRecord / WriteMftRecord
 *     calls can reach the extended range.
 */
static NTSTATUS
NtfslxExtendMftRunlist(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _Inout_ PNTFSLX_MFT_RECORD MftRecord,
    _In_ ULONG AdditionalClusters)
{
    PNTFSLX_ATTR_RECORD DataAttr;
    PNTFSLX_RUNLIST_ELEMENT OldRunlist = NULL;
    ULONG OldRunCount = 0;
    PNTFSLX_RUNLIST_ELEMENT NewRuns = NULL;
    ULONG NewRunCount = 0;
    PNTFSLX_RUNLIST_ELEMENT Merged = NULL;
    ULONG MergedCount = 0;
    ULONG TotalCap = 0;
    ULONG OutIdx = 0;
    ULONG I;
    LONGLONG LastVcn = 0;
    LONGLONG HintLcn = 0;
    UCHAR MpBuf[256];
    ULONG MpLen = 0;
    ULONG MpOffset;
    ULONG NewAttrLen;
    ULONGLONG NewAllocatedBytes;
    ULONG ClusterSize;
    NTSTATUS Status;

    ClusterSize = DevExt->VolumeInfo.BytesPerCluster;

    Status = NtfslxFindAttribute(MftRecord, NTFSLX_ATTRIBUTE_DATA,
                                 NULL, 0, &DataAttr);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: ExtendMft: FindAttribute($DATA) failed 0x%08lx\n", Status);
        return Status;
    }
    if (DataAttr->NonResident == 0)
    {
        NTFSDBG("ntfslx: ExtendMft: $MFT $DATA is resident?!\n");
        return STATUS_NOT_SUPPORTED;
    }

    Status = NtfslxMappingPairsDecompress(&DevExt->VolumeInfo, DataAttr,
                                          &OldRunlist, &OldRunCount);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: ExtendMft: Decompress failed 0x%08lx\n", Status);
        return Status;
    }

    /* Walk old runlist for the last valid LCN (as allocation hint) and
     * to compute NextVcn. */
    for (I = 0; I < OldRunCount; I++)
    {
        if (OldRunlist[I].Length == 0)
            break;
        LastVcn = OldRunlist[I].Vcn + OldRunlist[I].Length;
        if (OldRunlist[I].Lcn >= 0)
        {
            HintLcn = OldRunlist[I].Lcn + OldRunlist[I].Length;
        }
    }

    NTFSDBG("ntfslx: ExtendMft: old runs=%lu lastVcn=%I64d hintLcn=%I64d addClusters=%lu\n",
             OldRunCount, LastVcn, HintLcn, AdditionalClusters);

    Status = NtfslxAllocateClusters(DevExt->StorageDevice, &DevExt->VolumeInfo,
                                    DevExt->MftRunlist,
                                    (ULONGLONG)AdditionalClusters,
                                    (ULONGLONG)HintLcn,
                                    &NewRuns, &NewRunCount);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: ExtendMft: AllocateClusters failed 0x%08lx\n", Status);
        ExFreePoolWithTag(OldRunlist, NTFSLX_TAG);
        return Status;
    }

    /* Build merged runlist: old valid runs (minus sentinel), then new runs
     * with VCNs rewritten to continue from LastVcn. Allocate one extra slot
     * for the sentinel. */
    TotalCap = OldRunCount + NewRunCount + 2;
    Merged = ExAllocatePoolWithTag(NonPagedPool,
                                    TotalCap * sizeof(NTFSLX_RUNLIST_ELEMENT),
                                    NTFSLX_TAG);
    if (Merged == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    OutIdx = 0;
    for (I = 0; I < OldRunCount; I++)
    {
        if (OldRunlist[I].Length == 0)
            break;
        Merged[OutIdx++] = OldRunlist[I];
    }
    for (I = 0; I < NewRunCount; I++)
    {
        if (NewRuns[I].Length == 0)
            break;
        Merged[OutIdx] = NewRuns[I];
        Merged[OutIdx].Vcn = LastVcn;
        LastVcn += NewRuns[I].Length;
        OutIdx++;
    }
    /* Sentinel terminator */
    Merged[OutIdx].Vcn = LastVcn;
    Merged[OutIdx].Lcn = 0;
    Merged[OutIdx].Length = 0;
    MergedCount = OutIdx;

    /* Encode new mapping pairs. */
    RtlZeroMemory(MpBuf, sizeof(MpBuf));
    Status = NtfslxMappingPairsBuild(
        /* ClusterSizeBits */ 0,
        MpBuf, (LONG)sizeof(MpBuf),
        Merged, MergedCount,
        0, LastVcn, NULL);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: ExtendMft: MappingPairsBuild failed 0x%08lx merged=%lu lastVcn=%I64d\n",
                 Status, MergedCount, LastVcn);
        goto Cleanup;
    }

    /* Compute mapping pairs length (including terminator). */
    MpLen = 1;
    while (MpLen < sizeof(MpBuf) && MpBuf[MpLen - 1] != 0)
        MpLen++;
    NTFSDBG("ntfslx: ExtendMft: new mapping pairs len=%lu runs=%lu lastVcn=%I64d\n",
             MpLen, MergedCount, LastVcn);

    /* Resize $DATA attribute if the new mapping pairs don't fit. */
    MpOffset = DataAttr->Data.NonResident.MappingPairsOffset;
    NewAttrLen = ROUND_UP(MpOffset + MpLen, 8);
    if (NewAttrLen > DataAttr->Length)
    {
        Status = NtfslxResizeAttributeRecord(MftRecord, DataAttr, NewAttrLen);
        if (!NT_SUCCESS(Status))
        {
            NTFSDBG("ntfslx: ExtendMft: ResizeAttributeRecord to %lu failed 0x%08lx\n",
                     NewAttrLen, Status);
            goto Cleanup;
        }
    }

    RtlCopyMemory((PUCHAR)DataAttr + MpOffset, MpBuf, MpLen);

    /* Update $DATA sizes. */
    NewAllocatedBytes = (ULONGLONG)LastVcn * ClusterSize;
    DataAttr->Data.NonResident.AllocatedSize = NewAllocatedBytes;
    DataAttr->Data.NonResident.DataSize = NewAllocatedBytes;
    DataAttr->Data.NonResident.InitializedSize = NewAllocatedBytes;
    DataAttr->Data.NonResident.HighestVcn = LastVcn - 1;

    /* Replace DeviceExtension->MftRunlist with the new merged runlist so
     * subsequent reads/writes can reach the extended range. The caller
     * will NtfslxWriteMftRecord the mutated MftRecord below. */
    {
        PNTFSLX_RUNLIST_ELEMENT OldDevRunlist = DevExt->MftRunlist;
        DevExt->MftRunlist = Merged;
        DevExt->MftRunlistCount = MergedCount;
        Merged = NULL; /* ownership transferred */
        if (OldDevRunlist != NULL)
        {
            ExFreePoolWithTag(OldDevRunlist, NTFSLX_TAG);
        }
    }

    NTFSDBG("ntfslx: ExtendMft: success new $DATA alloc=%I64u records=%I64u\n",
             NewAllocatedBytes, NewAllocatedBytes / DevExt->VolumeInfo.BytesPerFileRecord);
    Status = STATUS_SUCCESS;

Cleanup:
    if (OldRunlist != NULL)
        ExFreePoolWithTag(OldRunlist, NTFSLX_TAG);
    if (NewRuns != NULL)
        ExFreePoolWithTag(NewRuns, NTFSLX_TAG);
    if (Merged != NULL)
        ExFreePoolWithTag(Merged, NTFSLX_TAG);
    return Status;
}

NTSTATUS
NtfslxAllocateMftRecord(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONG MftRunlistCount,
    _Out_ PULONGLONG OutRecordNumber)
{
    PNTFSLX_MFT_RECORD MftRecord;
    PNTFSLX_ATTR_RECORD BitmapAttr;
    PNTFSLX_RUNLIST_ELEMENT BitmapRunlist;
    PUCHAR BitmapData;
    ULONG BitmapRunlistCount;
    ULONG BitmapLength;
    ULONGLONG TotalBits;
    ULONGLONG Bit;
    ULONGLONG RecordNumber;
    ULONG RecordSize;
    PNTFSLX_MFT_RECORD NewRecord;
    USHORT UsaCount;
    PUSHORT UsaPos;
    NTSTATUS Status;

    *OutRecordNumber = 0;
    RecordSize = VolumeInfo->BytesPerFileRecord;

    /* Read the $MFT record to get its $BITMAP attribute */
    MftRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (MftRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(StorageDevice, VolumeInfo, MftRunlist,
                                 NTFSLX_FILE_MFT, MftRecord);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: AllocMft: ReadMftRecord($MFT) failed 0x%08lx\n", Status);
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return Status;
    }

    Status = NtfslxFindAttribute(MftRecord, NTFSLX_ATTRIBUTE_BITMAP, NULL, 0, &BitmapAttr);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: AllocMft: FindAttribute($BITMAP) failed 0x%08lx MftBytesInUse=%lu\n",
                 Status, MftRecord->BytesInUse);
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return Status;
    }

    NTFSDBG("ntfslx: AllocMft: $BITMAP found NonResident=%u Length=%lu ValueLength=%lu\n",
             BitmapAttr->NonResident, BitmapAttr->Length,
             BitmapAttr->NonResident == 0 ? BitmapAttr->Data.Resident.ValueLength : 0);

    /*
     * Also read the $MFT's $DATA attribute so we can cap record numbers by
     * the real MFT data-stream size. Without this the bitmap may advertise
     * more bits than there is on-disk space for, and we end up asking the
     * mapped writer to write past the end of the MFT runlist.
     */
    {
        PNTFSLX_ATTR_RECORD MftDataAttr;
        Status = NtfslxFindAttribute(MftRecord, NTFSLX_ATTRIBUTE_DATA,
                                     NULL, 0, &MftDataAttr);
        if (NT_SUCCESS(Status))
        {
            ULONGLONG MftDataSize;
            if (MftDataAttr->NonResident == 0)
            {
                MftDataSize = MftDataAttr->Data.Resident.ValueLength;
            }
            else
            {
                MftDataSize = MftDataAttr->Data.NonResident.AllocatedSize;
            }
            NTFSDBG("ntfslx: AllocMft: $MFT $DATA alloc=%I64u records=%I64u\n",
                     MftDataSize, MftDataSize / RecordSize);
            *OutRecordNumber = MftDataSize / RecordSize;  /* remember cap */
        }
        else
        {
            NTFSDBG("ntfslx: AllocMft: FindAttribute($DATA) failed 0x%08lx\n", Status);
            *OutRecordNumber = (ULONGLONG)-1;
        }
    }

    /* Read bitmap data */
    if (BitmapAttr->NonResident == 0)
    {
        /* Resident bitmap */
        BitmapLength = BitmapAttr->Data.Resident.ValueLength;
        BitmapData = ExAllocatePoolWithTag(NonPagedPool, BitmapLength, NTFSLX_TAG);
        if (BitmapData == NULL)
        {
            ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlCopyMemory(BitmapData,
                      (PUCHAR)BitmapAttr + BitmapAttr->Data.Resident.ValueOffset,
                      BitmapLength);
        BitmapRunlist = NULL;
        BitmapRunlistCount = 0;
    }
    else
    {
        /* Non-resident bitmap */
        BitmapLength = (ULONG)BitmapAttr->Data.NonResident.DataSize;
        NTFSDBG("ntfslx: AllocMft: non-resident bitmap DataSize=%I64u AllocSize=%I64u\n",
                 BitmapAttr->Data.NonResident.DataSize,
                 BitmapAttr->Data.NonResident.AllocatedSize);
        Status = NtfslxMappingPairsDecompress(VolumeInfo, BitmapAttr,
                                              &BitmapRunlist, &BitmapRunlistCount);
        if (!NT_SUCCESS(Status))
        {
            NTFSDBG("ntfslx: AllocMft: MappingPairsDecompress failed 0x%08lx\n", Status);
            ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
            return Status;
        }
        NTFSDBG("ntfslx: AllocMft: bitmap runlist count=%lu\n", BitmapRunlistCount);

        BitmapData = ExAllocatePoolWithTag(NonPagedPool, BitmapLength, NTFSLX_TAG);
        if (BitmapData == NULL)
        {
            ExFreePoolWithTag(BitmapRunlist, NTFSLX_TAG);
            ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Status = NtfslxReadMappedAttributeData(StorageDevice, VolumeInfo,
                                               BitmapRunlist, 0, BitmapLength,
                                               BitmapData);
        if (!NT_SUCCESS(Status))
        {
            NTFSDBG("ntfslx: AllocMft: ReadMappedAttributeData($BITMAP) failed 0x%08lx\n", Status);
            ExFreePoolWithTag(BitmapData, NTFSLX_TAG);
            ExFreePoolWithTag(BitmapRunlist, NTFSLX_TAG);
            ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
            return Status;
        }
    }

    NTFSDBG("ntfslx: AllocMft: scan BitmapLength=%lu TotalBits=%I64u\n",
             BitmapLength, (ULONGLONG)BitmapLength * 8);

    /*
     * Find first free bit, starting after system records. Cap the search by
     * whichever is smaller: the bitmap size, or the $MFT $DATA record count
     * captured above. Trying to allocate a record number past the $MFT
     * $DATA allocation would leave us writing into a hole in the MFT
     * runlist.
     */
    TotalBits = (ULONGLONG)BitmapLength * 8;
    {
        ULONGLONG MftRecordCap = *OutRecordNumber; /* captured above */
        if (MftRecordCap != (ULONGLONG)-1 && MftRecordCap < TotalBits)
        {
            NTFSDBG("ntfslx: AllocMft: capping TotalBits %I64u -> %I64u by $MFT $DATA\n",
                     TotalBits, MftRecordCap);
            TotalBits = MftRecordCap;
        }
    }
    *OutRecordNumber = 0;

    RecordNumber = (ULONGLONG)-1;
    for (Bit = NTFSLX_FILE_FIRST_USER; Bit < TotalBits; Bit++)
    {
        if (!NtfslxBitmapTestBit(BitmapData, BitmapLength, Bit))
        {
            RecordNumber = Bit;
            break;
        }
    }

    if (RecordNumber == (ULONGLONG)-1)
    {
        NTFSDBG("ntfslx: AllocMft: no free records in [%u..%I64u) -- DISK_FULL\n",
                 NTFSLX_FILE_FIRST_USER, TotalBits);
        if (BitmapRunlist != NULL)
            ExFreePoolWithTag(BitmapRunlist, NTFSLX_TAG);
        ExFreePoolWithTag(BitmapData, NTFSLX_TAG);
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return STATUS_DISK_FULL;
    }

    /* Set the bit in the bitmap */
    Status = NtfslxBitmapSetBitsInRun(BitmapData, BitmapLength, RecordNumber, 1, 1);
    if (!NT_SUCCESS(Status))
    {
        if (BitmapRunlist != NULL)
            ExFreePoolWithTag(BitmapRunlist, NTFSLX_TAG);
        ExFreePoolWithTag(BitmapData, NTFSLX_TAG);
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return Status;
    }

    /* Write bitmap back */
    if (BitmapAttr->NonResident == 0)
    {
        /* Write back via the resident attribute in the MFT record */
        RtlCopyMemory((PUCHAR)BitmapAttr + BitmapAttr->Data.Resident.ValueOffset,
                      BitmapData, BitmapLength);
        Status = NtfslxWriteMftRecord(StorageDevice, VolumeInfo, MftRunlist,
                                      NTFSLX_FILE_MFT, MftRecord);
    }
    else
    {
        /* Write non-resident bitmap data back to disk */
        Status = NtfslxWriteMappedDataLocal(StorageDevice, VolumeInfo,
                                            BitmapRunlist, 0, BitmapLength,
                                            BitmapData);
    }

    if (BitmapRunlist != NULL)
        ExFreePoolWithTag(BitmapRunlist, NTFSLX_TAG);
    ExFreePoolWithTag(BitmapData, NTFSLX_TAG);
    ExFreePoolWithTag(MftRecord, NTFSLX_TAG);

    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Initialize the new MFT record */
    NewRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (NewRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * Preserve and bump the on-disk sequence number from the slot we just
     * claimed. NTFS uses (sequence_number << 48) | mft_record_number as the
     * 64-bit FileId; if we always reset to 1 on allocation, deleting and
     * recreating immediately produces an identical FileId, which lets a
     * stale FILE_OPEN_BY_FILE_ID alias the new file. NtfslxFreeMftRecord
     * already bumps the sequence on free; here we just read it back. For
     * a freshly extended $MFT (slot was never used), the on-disk record
     * is zero / garbage and we fall back to 1.
     */
    {
        PNTFSLX_MFT_RECORD ExistingRecord;
        USHORT PreservedSeq = 0;

        ExistingRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
        if (ExistingRecord != NULL)
        {
            NTSTATUS ReadStatus = NtfslxReadMftRecord(StorageDevice, VolumeInfo,
                                                      MftRunlist, RecordNumber,
                                                      ExistingRecord);
            if (NT_SUCCESS(ReadStatus) &&
                ExistingRecord->Ntfs.Magic == NTFSLX_RECORD_MAGIC_FILE)
            {
                PreservedSeq = ExistingRecord->SequenceNumber;
            }
            ExFreePoolWithTag(ExistingRecord, NTFSLX_TAG);
        }

        RtlZeroMemory(NewRecord, RecordSize);
        NewRecord->Ntfs.Magic = NTFSLX_RECORD_MAGIC_FILE;
        NewRecord->Ntfs.UsaOffset = FIELD_OFFSET(NTFSLX_MFT_RECORD, Reserved) +
                                     sizeof(NewRecord->Reserved);

        UsaCount = (USHORT)(RecordSize / 512 + 1);
        NewRecord->Ntfs.UsaCount = UsaCount;

        /* Use preserved+1 to guarantee monotonic. 0 wraps to 1. */
        NewRecord->SequenceNumber = (USHORT)(PreservedSeq + 1);
        if (NewRecord->SequenceNumber == 0)
        {
            NewRecord->SequenceNumber = 1;
        }
    }
    NewRecord->LinkCount = 0;
    NewRecord->AttributesOffset = (USHORT)(NewRecord->Ntfs.UsaOffset +
                                           UsaCount * sizeof(USHORT));
    /* Align attributes offset to 8 bytes */
    NewRecord->AttributesOffset = (USHORT)ROUND_UP(NewRecord->AttributesOffset, 8);
    NewRecord->Flags = NTFSLX_MFT_RECORD_IN_USE;
    NewRecord->BytesAllocated = RecordSize;
    NewRecord->BaseMftRecord = 0;
    NewRecord->NextAttributeInstance = 0;
    NewRecord->MftRecordNumber = (ULONG)RecordNumber;

    /* Place an attribute end marker */
    *(PULONG)((PUCHAR)NewRecord + NewRecord->AttributesOffset) = NTFSLX_ATTRIBUTE_END;
    NewRecord->BytesInUse = NewRecord->AttributesOffset + sizeof(ULONG) * 2;

    /* Initialize the USA: USN = 1, all entries = 0 */
    UsaPos = (PUSHORT)((PUCHAR)NewRecord + NewRecord->Ntfs.UsaOffset);
    *UsaPos = 1;

    Status = NtfslxWriteMftRecord(StorageDevice, VolumeInfo, MftRunlist,
                                  RecordNumber, NewRecord);
    ExFreePoolWithTag(NewRecord, NTFSLX_TAG);

    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: AllocMft: WriteMftRecord(rec=%I64u) failed 0x%08lx\n",
                 RecordNumber, Status);
        return Status;
    }

    NTFSDBG("ntfslx: AllocMft: allocated record=%I64u\n", RecordNumber);
    *OutRecordNumber = RecordNumber;
    return STATUS_SUCCESS;
}

/*
 * Same as NtfslxAllocateMftRecord, but if the $MFT is out of records we
 * first try to extend the $MFT data stream by a small chunk of clusters
 * and retry the allocation. Uses the DeviceExtension so it can update the
 * cached MftRunlist after a successful extension.
 */
NTSTATUS
NtfslxAllocateMftRecordEx(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _Out_ PULONGLONG OutRecordNumber)
{
    NTSTATUS Status;
    BOOLEAN MetadataDirty;
    ULONGLONG RecordCap;
    ULONG ExtraClusters = 4; /* ~16 more records per extension pass */
    PNTFSLX_MFT_RECORD MftRecord;
    ULONG RecordSize = DevExt->VolumeInfo.BytesPerFileRecord;

    Status = NtfslxAllocateMftRecord(DevExt->StorageDevice,
                                     &DevExt->VolumeInfo,
                                     DevExt->MftRunlist,
                                     DevExt->MftRunlistCount,
                                     OutRecordNumber);
    if (Status != STATUS_DISK_FULL)
    {
        return Status;
    }

    MftRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (MftRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(DevExt->StorageDevice,
                                 &DevExt->VolumeInfo,
                                 DevExt->MftRunlist,
                                 NTFSLX_FILE_MFT, MftRecord);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: AllocateMftRecordEx: ReadMftRecord($MFT) failed 0x%08lx\n",
                 Status);
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return Status;
    }

    Status = NtfslxGetMftRecordCapacity(MftRecord, RecordSize, &RecordCap);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: AllocateMftRecordEx: GetMftRecordCapacity failed 0x%08lx\n",
                 Status);
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return Status;
    }

    Status = NtfslxEnsureMftBitmapCoverage(DevExt, MftRecord, RecordCap, &MetadataDirty);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: AllocateMftRecordEx: bitmap coverage sync failed 0x%08lx\n",
                 Status);
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return Status;
    }

    if (MetadataDirty)
    {
        Status = NtfslxWriteMftRecord(DevExt->StorageDevice,
                                      &DevExt->VolumeInfo,
                                      DevExt->MftRunlist,
                                      NTFSLX_FILE_MFT, MftRecord);
        if (!NT_SUCCESS(Status))
        {
            NTFSDBG("ntfslx: AllocateMftRecordEx: WriteMftRecord(bitmap sync) failed 0x%08lx\n",
                     Status);
            ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
            return Status;
        }

        Status = NtfslxAllocateMftRecord(DevExt->StorageDevice,
                                         &DevExt->VolumeInfo,
                                         DevExt->MftRunlist,
                                         DevExt->MftRunlistCount,
                                         OutRecordNumber);
        NTFSDBG("ntfslx: AllocateMftRecordEx: retry after bitmap sync result=0x%08lx rec=%I64u\n",
                 Status, NT_SUCCESS(Status) ? *OutRecordNumber : 0);
        if (Status != STATUS_DISK_FULL)
        {
            ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
            return Status;
        }

        Status = NtfslxReadMftRecord(DevExt->StorageDevice,
                                     &DevExt->VolumeInfo,
                                     DevExt->MftRunlist,
                                     NTFSLX_FILE_MFT, MftRecord);
        if (!NT_SUCCESS(Status))
        {
            NTFSDBG("ntfslx: AllocateMftRecordEx: re-read $MFT failed 0x%08lx\n",
                     Status);
            ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
            return Status;
        }
    }

    NTFSDBG("ntfslx: AllocateMftRecordEx: $MFT still full, extending by %lu clusters\n",
             ExtraClusters);

    Status = NtfslxExtendMftRunlist(DevExt, MftRecord, ExtraClusters);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: AllocateMftRecordEx: ExtendMftRunlist failed 0x%08lx\n",
                 Status);
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return Status;
    }

    Status = NtfslxGetMftRecordCapacity(MftRecord, RecordSize, &RecordCap);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: AllocateMftRecordEx: GetMftRecordCapacity(post-extend) failed 0x%08lx\n",
                 Status);
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return Status;
    }

    Status = NtfslxEnsureMftBitmapCoverage(DevExt, MftRecord, RecordCap, &MetadataDirty);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: AllocateMftRecordEx: bitmap grow after extend failed 0x%08lx\n",
                 Status);
        ExFreePoolWithTag(MftRecord, NTFSLX_TAG);
        return Status;
    }

    Status = NtfslxWriteMftRecord(DevExt->StorageDevice,
                                  &DevExt->VolumeInfo,
                                  DevExt->MftRunlist,
                                  NTFSLX_FILE_MFT, MftRecord);
    ExFreePoolWithTag(MftRecord, NTFSLX_TAG);

    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: AllocateMftRecordEx: WriteMftRecord($MFT) failed 0x%08lx\n",
                 Status);
        return Status;
    }

    /* Retry the allocation against the extended $MFT. */
    Status = NtfslxAllocateMftRecord(DevExt->StorageDevice,
                                     &DevExt->VolumeInfo,
                                     DevExt->MftRunlist,
                                     DevExt->MftRunlistCount,
                                     OutRecordNumber);
    NTFSDBG("ntfslx: AllocateMftRecordEx: retry after extend result=0x%08lx rec=%I64u\n",
             Status, NT_SUCCESS(Status) ? *OutRecordNumber : 0);
    return Status;
}

NTSTATUS
NtfslxFreeMftRecord(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG RecordNumber)
{
    PNTFSLX_MFT_RECORD Record;
    PNTFSLX_MFT_RECORD MftBaseRecord;
    PNTFSLX_ATTR_RECORD BitmapAttr;
    PNTFSLX_RUNLIST_ELEMENT BitmapRunlist;
    PUCHAR BitmapData;
    ULONG BitmapRunlistCount;
    ULONG BitmapLength;
    ULONG RecordSize;
    NTSTATUS Status;

    RecordSize = VolumeInfo->BytesPerFileRecord;

    /* Read the target MFT record */
    Record = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (Record == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(StorageDevice, VolumeInfo, MftRunlist,
                                 RecordNumber, Record);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Record, NTFSLX_TAG);
        return Status;
    }

    /* Clear IN_USE flag and increment sequence number */
    Record->Flags &= ~NTFSLX_MFT_RECORD_IN_USE;
    Record->SequenceNumber++;
    if (Record->SequenceNumber == 0)
    {
        Record->SequenceNumber = 1;
    }

    Status = NtfslxWriteMftRecord(StorageDevice, VolumeInfo, MftRunlist,
                                  RecordNumber, Record);
    ExFreePoolWithTag(Record, NTFSLX_TAG);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Now clear the bit in $MFT/$BITMAP */
    MftBaseRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (MftBaseRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(StorageDevice, VolumeInfo, MftRunlist,
                                 NTFSLX_FILE_MFT, MftBaseRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(MftBaseRecord, NTFSLX_TAG);
        return Status;
    }

    Status = NtfslxFindAttribute(MftBaseRecord, NTFSLX_ATTRIBUTE_BITMAP,
                                 NULL, 0, &BitmapAttr);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(MftBaseRecord, NTFSLX_TAG);
        return Status;
    }

    if (BitmapAttr->NonResident == 0)
    {
        BitmapLength = BitmapAttr->Data.Resident.ValueLength;
        BitmapData = ExAllocatePoolWithTag(NonPagedPool, BitmapLength, NTFSLX_TAG);
        if (BitmapData == NULL)
        {
            ExFreePoolWithTag(MftBaseRecord, NTFSLX_TAG);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlCopyMemory(BitmapData,
                      (PUCHAR)BitmapAttr + BitmapAttr->Data.Resident.ValueOffset,
                      BitmapLength);
        BitmapRunlist = NULL;
        BitmapRunlistCount = 0;
    }
    else
    {
        BitmapLength = (ULONG)BitmapAttr->Data.NonResident.DataSize;
        Status = NtfslxMappingPairsDecompress(VolumeInfo, BitmapAttr,
                                              &BitmapRunlist, &BitmapRunlistCount);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(MftBaseRecord, NTFSLX_TAG);
            return Status;
        }

        BitmapData = ExAllocatePoolWithTag(NonPagedPool, BitmapLength, NTFSLX_TAG);
        if (BitmapData == NULL)
        {
            ExFreePoolWithTag(BitmapRunlist, NTFSLX_TAG);
            ExFreePoolWithTag(MftBaseRecord, NTFSLX_TAG);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Status = NtfslxReadMappedAttributeData(StorageDevice, VolumeInfo,
                                               BitmapRunlist, 0, BitmapLength,
                                               BitmapData);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(BitmapData, NTFSLX_TAG);
            ExFreePoolWithTag(BitmapRunlist, NTFSLX_TAG);
            ExFreePoolWithTag(MftBaseRecord, NTFSLX_TAG);
            return Status;
        }
    }

    /* Clear the bit */
    NtfslxBitmapSetBitsInRun(BitmapData, BitmapLength, RecordNumber, 1, 0);

    /* Write bitmap back */
    if (BitmapAttr->NonResident == 0)
    {
        RtlCopyMemory((PUCHAR)BitmapAttr + BitmapAttr->Data.Resident.ValueOffset,
                      BitmapData, BitmapLength);
        Status = NtfslxWriteMftRecord(StorageDevice, VolumeInfo, MftRunlist,
                                      NTFSLX_FILE_MFT, MftBaseRecord);
    }
    else
    {
        Status = NtfslxWriteMappedDataLocal(StorageDevice, VolumeInfo,
                                            BitmapRunlist, 0, BitmapLength,
                                            BitmapData);
    }

    if (BitmapRunlist != NULL)
        ExFreePoolWithTag(BitmapRunlist, NTFSLX_TAG);
    ExFreePoolWithTag(BitmapData, NTFSLX_TAG);
    ExFreePoolWithTag(MftBaseRecord, NTFSLX_TAG);

    return Status;
}
