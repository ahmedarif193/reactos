/*
 * PROJECT:     ReactOS NTFS driver (ntfslx)
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Cluster allocation and deallocation via $Bitmap (MFT record 6)
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

/*
 * NtfslxRunlistVcnToLcnLocal - translate VCN to LCN via runlist
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
 * Read the volume $Bitmap data into memory.
 * Caller must free *BitmapData and *BitmapRunlist on success.
 */
static
NTSTATUS
NtfslxReadVolumeBitmap(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _Outptr_ PUCHAR *BitmapData,
    _Out_ PULONG BitmapLength,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *BitmapRunlist,
    _Out_ PULONG BitmapRunlistCount)
{
    PNTFSLX_MFT_RECORD BitmapRecord;
    PNTFSLX_ATTR_RECORD DataAttr;
    ULONG RecordSize;
    NTSTATUS Status;

    *BitmapData = NULL;
    *BitmapLength = 0;
    *BitmapRunlist = NULL;
    *BitmapRunlistCount = 0;

    RecordSize = VolumeInfo->BytesPerFileRecord;

    BitmapRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (BitmapRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(StorageDevice, VolumeInfo, MftRunlist,
                                 NTFSLX_FILE_BITMAP, BitmapRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(BitmapRecord, NTFSLX_TAG);
        return Status;
    }

    Status = NtfslxFindAttribute(BitmapRecord, NTFSLX_ATTRIBUTE_DATA,
                                 NULL, 0, &DataAttr);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(BitmapRecord, NTFSLX_TAG);
        return Status;
    }

    if (DataAttr->NonResident == 0)
    {
        /* Resident $Bitmap $DATA -- unusual but handle it */
        *BitmapLength = DataAttr->Data.Resident.ValueLength;
        *BitmapData = ExAllocatePoolWithTag(NonPagedPool, *BitmapLength, NTFSLX_TAG);
        if (*BitmapData == NULL)
        {
            ExFreePoolWithTag(BitmapRecord, NTFSLX_TAG);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlCopyMemory(*BitmapData,
                      (PUCHAR)DataAttr + DataAttr->Data.Resident.ValueOffset,
                      *BitmapLength);
        ExFreePoolWithTag(BitmapRecord, NTFSLX_TAG);
        return STATUS_SUCCESS;
    }

    /* Non-resident: decompress mapping pairs then read */
    *BitmapLength = (ULONG)DataAttr->Data.NonResident.DataSize;
    Status = NtfslxMappingPairsDecompress(VolumeInfo, DataAttr,
                                          BitmapRunlist, BitmapRunlistCount);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(BitmapRecord, NTFSLX_TAG);
        return Status;
    }

    *BitmapData = ExAllocatePoolWithTag(NonPagedPool, *BitmapLength, NTFSLX_TAG);
    if (*BitmapData == NULL)
    {
        ExFreePoolWithTag(*BitmapRunlist, NTFSLX_TAG);
        *BitmapRunlist = NULL;
        ExFreePoolWithTag(BitmapRecord, NTFSLX_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMappedAttributeData(StorageDevice, VolumeInfo,
                                           *BitmapRunlist, 0, *BitmapLength,
                                           *BitmapData);
    ExFreePoolWithTag(BitmapRecord, NTFSLX_TAG);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(*BitmapData, NTFSLX_TAG);
        *BitmapData = NULL;
        ExFreePoolWithTag(*BitmapRunlist, NTFSLX_TAG);
        *BitmapRunlist = NULL;
        return Status;
    }

    return STATUS_SUCCESS;
}

/*
 * Write the volume $Bitmap back to disk.
 */
static
NTSTATUS
NtfslxWriteVolumeBitmap(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_opt_ PNTFSLX_RUNLIST_ELEMENT BitmapRunlist,
    _In_reads_bytes_(BitmapLength) PUCHAR BitmapData,
    _In_ ULONG BitmapLength,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist)
{
    if (BitmapRunlist != NULL)
    {
        /* Non-resident: write through bitmap's own runlist */
        return NtfslxWriteMappedDataLocal(StorageDevice, VolumeInfo,
                                          BitmapRunlist, 0, BitmapLength,
                                          BitmapData);
    }
    else
    {
        /* Resident: re-read $Bitmap MFT record, update resident data, write back */
        PNTFSLX_MFT_RECORD BitmapRecord;
        PNTFSLX_ATTR_RECORD DataAttr;
        ULONG RecordSize;
        NTSTATUS Status;

        RecordSize = VolumeInfo->BytesPerFileRecord;
        BitmapRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
        if (BitmapRecord == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Status = NtfslxReadMftRecord(StorageDevice, VolumeInfo, MftRunlist,
                                     NTFSLX_FILE_BITMAP, BitmapRecord);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(BitmapRecord, NTFSLX_TAG);
            return Status;
        }

        Status = NtfslxFindAttribute(BitmapRecord, NTFSLX_ATTRIBUTE_DATA,
                                     NULL, 0, &DataAttr);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(BitmapRecord, NTFSLX_TAG);
            return Status;
        }

        if (DataAttr->Data.Resident.ValueLength < BitmapLength)
        {
            ExFreePoolWithTag(BitmapRecord, NTFSLX_TAG);
            return STATUS_DISK_FULL;
        }

        RtlCopyMemory((PUCHAR)DataAttr + DataAttr->Data.Resident.ValueOffset,
                      BitmapData, BitmapLength);

        Status = NtfslxWriteMftRecord(NULL, StorageDevice, VolumeInfo, MftRunlist,
                                      NTFSLX_FILE_BITMAP, BitmapRecord);
        ExFreePoolWithTag(BitmapRecord, NTFSLX_TAG);
        return Status;
    }
}

static
ULONGLONG
NtfslxRunlistMappedClusterCount(
    _In_reads_(RunlistCount) PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ ULONG RunlistCount)
{
    ULONGLONG ClusterCount;
    ULONG Index;

    ClusterCount = 0;
    for (Index = 0; Index < RunlistCount; ++Index)
    {
        if (Runlist[Index].Length == 0)
        {
            break;
        }

        if (Runlist[Index].Lcn >= 0)
        {
            ClusterCount += (ULONGLONG)Runlist[Index].Length;
        }
    }

    return ClusterCount;
}

static
VOID
NtfslxAdjustFreeClusterCount(
    _Inout_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ LONGLONG Delta,
    _In_z_ PCSTR Reason)
{
    ULONGLONG Before;
    ULONGLONG After;
    ULONGLONG Magnitude;

    Before = VolumeInfo->FreeClusters;
    After = Before;

    if (Delta < 0)
    {
        Magnitude = (ULONGLONG)(-Delta);
        if (Magnitude >= Before)
        {
            After = 0;
        }
        else
        {
            After = Before - Magnitude;
        }
    }
    else if (Delta > 0)
    {
        Magnitude = (ULONGLONG)Delta;
        if (Magnitude >= VolumeInfo->ClusterCount - Before)
        {
            After = VolumeInfo->ClusterCount;
        }
        else
        {
            After = Before + Magnitude;
        }
    }

    VolumeInfo->FreeClusters = After;
    NTFSDBG("ntfslx: volume space %s delta=%I64d before=%I64u after=%I64u total=%I64u\n",
             Reason, Delta, Before, After, VolumeInfo->ClusterCount);
}


/*
 * Internal lock-naive cluster allocator. Callers must hold
 * DevExt->ClusterAllocLock (or be on the volume mount path before any
 * other thread can race). Performs the bitmap scan, bit-flip, and
 * durable bitmap write. The bitmap write IS the on-disk reservation,
 * so it has to be inside the alloc-lock. Once this returns, callers
 * can drop the lock and write data to the reserved clusters without
 * risk of another allocator picking the same range.
 */
static
NTSTATUS
NtfslxAllocateClustersLocked(
    _In_opt_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG Count,
    _In_ ULONGLONG HintLcn,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *OutRunlist,
    _Out_ PULONG OutRunlistCount)
{
    PUCHAR BitmapData;
    PNTFSLX_RUNLIST_ELEMENT BitmapRunlist;
    PNTFSLX_RUNLIST_ELEMENT ResultRunlist;
    ULONG BitmapLength;
    ULONG BitmapRunlistCount;
    ULONGLONG TotalBits;
    ULONGLONG SearchStart;
    ULONGLONG RunStart;
    ULONGLONG RunLength;
    ULONGLONG Remaining;
    ULONG RunCount;
    ULONG RunCapacity;
    NTSTATUS Status;

    *OutRunlist = NULL;
    *OutRunlistCount = 0;

    if (Count == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtfslxReadVolumeBitmap(StorageDevice, VolumeInfo, MftRunlist,
                                    &BitmapData, &BitmapLength,
                                    &BitmapRunlist, &BitmapRunlistCount);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    TotalBits = (ULONGLONG)BitmapLength * 8;
    if (TotalBits > VolumeInfo->ClusterCount)
    {
        TotalBits = VolumeInfo->ClusterCount;
    }

    /* Allocate result runlist (start with capacity for a few runs + sentinel) */
    RunCapacity = 8;
    ResultRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                          RunCapacity * sizeof(NTFSLX_RUNLIST_ELEMENT),
                                          NTFSLX_TAG);
    if (ResultRunlist == NULL)
    {
        ExFreePoolWithTag(BitmapData, NTFSLX_TAG);
        if (BitmapRunlist != NULL)
            ExFreePoolWithTag(BitmapRunlist, NTFSLX_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RunCount = 0;
    Remaining = Count;

    /* Start searching near hint, then wrap around */
    if (HintLcn >= TotalBits || HintLcn == 0)
    {
        SearchStart = NTFSLX_FILE_FIRST_USER;
    }
    else
    {
        SearchStart = HintLcn;
    }

    /* Simple first-fit: scan for contiguous free clusters */
    RunStart = SearchStart;
    while (Remaining > 0 && RunStart < TotalBits)
    {
        /* Skip used clusters */
        while (RunStart < TotalBits &&
               NtfslxBitmapTestBit(BitmapData, BitmapLength, RunStart))
        {
            RunStart++;
        }

        if (RunStart >= TotalBits)
        {
            break;
        }

        /* Count contiguous free clusters */
        RunLength = 0;
        while (RunStart + RunLength < TotalBits &&
               RunLength < Remaining &&
               !NtfslxBitmapTestBit(BitmapData, BitmapLength, RunStart + RunLength))
        {
            RunLength++;
        }

        if (RunLength > 0)
        {
            /* Grow result array if needed */
            if (RunCount + 2 > RunCapacity)
            {
                PNTFSLX_RUNLIST_ELEMENT NewRunlist;
                ULONG NewCapacity;

                NewCapacity = RunCapacity * 2;
                NewRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                                   NewCapacity * sizeof(NTFSLX_RUNLIST_ELEMENT),
                                                   NTFSLX_TAG);
                if (NewRunlist == NULL)
                {
                    ExFreePoolWithTag(ResultRunlist, NTFSLX_TAG);
                    ExFreePoolWithTag(BitmapData, NTFSLX_TAG);
                    if (BitmapRunlist != NULL)
                        ExFreePoolWithTag(BitmapRunlist, NTFSLX_TAG);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                RtlCopyMemory(NewRunlist, ResultRunlist,
                              RunCount * sizeof(NTFSLX_RUNLIST_ELEMENT));
                ExFreePoolWithTag(ResultRunlist, NTFSLX_TAG);
                ResultRunlist = NewRunlist;
                RunCapacity = NewCapacity;
            }

            /* Mark bits as allocated in bitmap */
            NtfslxBitmapSetBitsInRun(BitmapData, BitmapLength, RunStart, RunLength, 1);

            ResultRunlist[RunCount].Vcn = (LONGLONG)(Count - Remaining);
            ResultRunlist[RunCount].Lcn = (LONGLONG)RunStart;
            ResultRunlist[RunCount].Length = (LONGLONG)RunLength;
            RunCount++;
            Remaining -= RunLength;
            RunStart += RunLength;
        }
    }

    /* If we started after the beginning, wrap around and scan [FIRST_USER, SearchStart) */
    if (Remaining > 0 && SearchStart > NTFSLX_FILE_FIRST_USER)
    {
        RunStart = NTFSLX_FILE_FIRST_USER;
        while (Remaining > 0 && RunStart < SearchStart)
        {
            while (RunStart < SearchStart &&
                   NtfslxBitmapTestBit(BitmapData, BitmapLength, RunStart))
            {
                RunStart++;
            }

            if (RunStart >= SearchStart)
            {
                break;
            }

            RunLength = 0;
            while (RunStart + RunLength < SearchStart &&
                   RunLength < Remaining &&
                   !NtfslxBitmapTestBit(BitmapData, BitmapLength, RunStart + RunLength))
            {
                RunLength++;
            }

            if (RunLength > 0)
            {
                if (RunCount + 2 > RunCapacity)
                {
                    PNTFSLX_RUNLIST_ELEMENT NewRunlist;
                    ULONG NewCapacity;

                    NewCapacity = RunCapacity * 2;
                    NewRunlist = ExAllocatePoolWithTag(NonPagedPool,
                                                       NewCapacity * sizeof(NTFSLX_RUNLIST_ELEMENT),
                                                       NTFSLX_TAG);
                    if (NewRunlist == NULL)
                    {
                        ExFreePoolWithTag(ResultRunlist, NTFSLX_TAG);
                        ExFreePoolWithTag(BitmapData, NTFSLX_TAG);
                        if (BitmapRunlist != NULL)
                            ExFreePoolWithTag(BitmapRunlist, NTFSLX_TAG);
                        return STATUS_INSUFFICIENT_RESOURCES;
                    }

                    RtlCopyMemory(NewRunlist, ResultRunlist,
                                  RunCount * sizeof(NTFSLX_RUNLIST_ELEMENT));
                    ExFreePoolWithTag(ResultRunlist, NTFSLX_TAG);
                    ResultRunlist = NewRunlist;
                    RunCapacity = NewCapacity;
                }

                NtfslxBitmapSetBitsInRun(BitmapData, BitmapLength, RunStart, RunLength, 1);

                ResultRunlist[RunCount].Vcn = (LONGLONG)(Count - Remaining);
                ResultRunlist[RunCount].Lcn = (LONGLONG)RunStart;
                ResultRunlist[RunCount].Length = (LONGLONG)RunLength;
                RunCount++;
                Remaining -= RunLength;
                RunStart += RunLength;
            }
        }
    }

    if (Remaining > 0)
    {
        /* Not enough free clusters: undo allocations in bitmap */
        ULONG I;
        for (I = 0; I < RunCount; I++)
        {
            NtfslxBitmapSetBitsInRun(BitmapData, BitmapLength,
                                     (ULONGLONG)ResultRunlist[I].Lcn,
                                     (ULONGLONG)ResultRunlist[I].Length, 0);
        }

        ExFreePoolWithTag(ResultRunlist, NTFSLX_TAG);
        ExFreePoolWithTag(BitmapData, NTFSLX_TAG);
        if (BitmapRunlist != NULL)
            ExFreePoolWithTag(BitmapRunlist, NTFSLX_TAG);
        return STATUS_DISK_FULL;
    }

    /*
     * Journal each allocated range as a BMP_SET_RANGE redo record before
     * the bitmap write hits disk, so a crash between the journal append
     * and the bitmap I/O can be replayed on next mount. Phase-1 journaling
     * is best-effort — failure to journal is non-fatal because Phase-1
     * has no recovery driver yet, but the records stay durable for Phase 2.
     * See docs/ntfs-specs/13_ntfslx_phase1_redo_records.txt.
     */
    if (DevExt != NULL)
    {
        ULONG J;
        for (J = 0; J < RunCount; J++)
        {
            (VOID)NtfslxJournalAppendClusterBitmapRange(
                DevExt,
                (ULONGLONG)ResultRunlist[J].Lcn,
                (ULONGLONG)ResultRunlist[J].Length,
                TRUE);
        }
    }

    /* Write modified bitmap back to disk */
    Status = NtfslxWriteVolumeBitmap(StorageDevice, VolumeInfo,
                                     BitmapRunlist, BitmapData,
                                     BitmapLength, MftRunlist);
    ExFreePoolWithTag(BitmapData, NTFSLX_TAG);
    if (BitmapRunlist != NULL)
        ExFreePoolWithTag(BitmapRunlist, NTFSLX_TAG);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(ResultRunlist, NTFSLX_TAG);
        return Status;
    }

    /* Add sentinel entry */
    ResultRunlist[RunCount].Vcn = (LONGLONG)Count;
    ResultRunlist[RunCount].Lcn = NTFSLX_LCN_ENOENT;
    ResultRunlist[RunCount].Length = 0;
    RunCount++;

    NtfslxAdjustFreeClusterCount(VolumeInfo,
                                 -(LONGLONG)NtfslxRunlistMappedClusterCount(ResultRunlist, RunCount),
                                 "allocate");

    *OutRunlist = ResultRunlist;
    *OutRunlistCount = RunCount;
    return STATUS_SUCCESS;
}

/*
 * Internal lock-naive cluster freer. Callers must hold
 * DevExt->ClusterAllocLock. Symmetric with NtfslxAllocateClustersLocked:
 * the bitmap write IS the durable record of the free, so it must be
 * inside the alloc-lock.
 */
static
NTSTATUS
NtfslxFreeClustersLocked(
    _In_opt_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ ULONG RunlistCount)
{
    PUCHAR BitmapData;
    PNTFSLX_RUNLIST_ELEMENT BitmapRunlist;
    ULONG BitmapLength;
    ULONG BitmapRunlistCount;
    ULONG I;
    NTSTATUS Status;

    if (RunlistCount == 0 || Runlist == NULL)
    {
        return STATUS_SUCCESS;
    }

    Status = NtfslxReadVolumeBitmap(StorageDevice, VolumeInfo, MftRunlist,
                                    &BitmapData, &BitmapLength,
                                    &BitmapRunlist, &BitmapRunlistCount);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Clear bits for each run */
    for (I = 0; I < RunlistCount; I++)
    {
        if (Runlist[I].Length == 0)
        {
            break; /* sentinel */
        }

        if (Runlist[I].Lcn >= 0)
        {
            NtfslxBitmapSetBitsInRun(BitmapData, BitmapLength,
                                     (ULONGLONG)Runlist[I].Lcn,
                                     (ULONGLONG)Runlist[I].Length, 0);
        }
    }

    /*
     * Journal each freed range as a BMP_CLEAR_RANGE redo record before the
     * bitmap write hits disk. Same Phase-1 best-effort discipline as the
     * allocator above — see docs/ntfs-specs/13_ntfslx_phase1_redo_records.txt.
     */
    if (DevExt != NULL)
    {
        for (I = 0; I < RunlistCount; I++)
        {
            if (Runlist[I].Length == 0)
                break;

            if (Runlist[I].Lcn >= 0)
            {
                (VOID)NtfslxJournalAppendClusterBitmapRange(
                    DevExt,
                    (ULONGLONG)Runlist[I].Lcn,
                    (ULONGLONG)Runlist[I].Length,
                    FALSE);
            }
        }
    }

    /* Write back */
    Status = NtfslxWriteVolumeBitmap(StorageDevice, VolumeInfo,
                                     BitmapRunlist, BitmapData,
                                     BitmapLength, MftRunlist);
    ExFreePoolWithTag(BitmapData, NTFSLX_TAG);
    if (BitmapRunlist != NULL)
        ExFreePoolWithTag(BitmapRunlist, NTFSLX_TAG);

    if (NT_SUCCESS(Status))
    {
        NtfslxAdjustFreeClusterCount(VolumeInfo,
                                     (LONGLONG)NtfslxRunlistMappedClusterCount(Runlist, RunlistCount),
                                     "free");
    }

    return Status;
}

/*
 * Public DevExt-aware wrappers. They acquire ClusterAllocLock around
 * the lock-naive helpers, so callers don't have to know about the lock.
 *
 * Lock-window discipline: the lock is held only across the bitmap RMW
 * cycle (read bitmap, scan, set bits, write bitmap back). The caller
 * is expected to write the actual cluster contents (data pages,
 * cleared MFT records, etc.) AFTER this returns and the lock is
 * dropped — see drivers/filesystems/ntfslx/fcb.c for the full
 * lock-order doc.
 *
 * The pre-T1.5.2 code held no lock at all here, which let two parallel
 * allocators read the same bitmap, both pick the same free range, and
 * both think they had it. The lock is the fix.
 */
NTSTATUS
NtfslxAllocateClusters(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG Count,
    _In_ ULONGLONG HintLcn,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *OutRunlist,
    _Out_ PULONG OutRunlistCount)
{
    PNTFSLX_DEVICE_EXTENSION DevExt = NULL;
    NTSTATUS Status;

    /*
     * Look up the volume DevExt via the storage device's Vpb. The
     * Vpb->DeviceObject is the VolumeDevice we created in
     * NtfslxMountVolume; its DeviceExtension is the volume's
     * NTFSLX_DEVICE_EXTENSION which owns ClusterAllocLock. If the Vpb
     * is missing (test path, pre-mount), fall through to the lock-
     * naive helper — there's no shared state to protect yet.
     */
    if (StorageDevice != NULL &&
        StorageDevice->Vpb != NULL &&
        StorageDevice->Vpb->DeviceObject != NULL)
    {
        DevExt = StorageDevice->Vpb->DeviceObject->DeviceExtension;
    }

    if (DevExt != NULL && DevExt->ClusterAllocLockInitialized)
    {
        KeAcquireGuardedMutex(&DevExt->ClusterAllocLock);
    }

    Status = NtfslxAllocateClustersLocked(DevExt, StorageDevice, VolumeInfo, MftRunlist,
                                          Count, HintLcn,
                                          OutRunlist, OutRunlistCount);

    if (DevExt != NULL && DevExt->ClusterAllocLockInitialized)
    {
        KeReleaseGuardedMutex(&DevExt->ClusterAllocLock);
    }

    return Status;
}

NTSTATUS
NtfslxFreeClusters(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ ULONG RunlistCount)
{
    PNTFSLX_DEVICE_EXTENSION DevExt = NULL;
    NTSTATUS Status;

    if (StorageDevice != NULL &&
        StorageDevice->Vpb != NULL &&
        StorageDevice->Vpb->DeviceObject != NULL)
    {
        DevExt = StorageDevice->Vpb->DeviceObject->DeviceExtension;
    }

    if (DevExt != NULL && DevExt->ClusterAllocLockInitialized)
    {
        KeAcquireGuardedMutex(&DevExt->ClusterAllocLock);
    }

    Status = NtfslxFreeClustersLocked(DevExt, StorageDevice, VolumeInfo, MftRunlist,
                                      Runlist, RunlistCount);

    if (DevExt != NULL && DevExt->ClusterAllocLockInitialized)
    {
        KeReleaseGuardedMutex(&DevExt->ClusterAllocLock);
    }

    return Status;
}
