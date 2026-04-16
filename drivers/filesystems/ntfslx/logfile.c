/*
 * NTFS LogFile ($LogFile) validation for the ntfslx ReactOS driver.
 *
 * Reads the first two restart pages from the LogFile (MFT record 2)
 * and checks whether the volume was cleanly dismounted.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

#define NTFS_BLOCK_SIZE 512

/*
 * Validate a single restart page header for consistency.
 */
static BOOLEAN
NtfslxCheckRestartPageHeader(
    _In_ PNTFSLX_RESTART_PAGE_HEADER Rp,
    _In_ LONGLONG Position)
{
    ULONG SystemPageSize;
    ULONG LogPageSize;
    USHORT RaOffset;
    USHORT UsaOffset;
    USHORT UsaCount;
    USHORT UsaEnd;

    /* Must be RSTR or CHKD magic */
    if (Rp->Magic != NTFSLX_RECORD_MAGIC_RSTR &&
        Rp->Magic != NTFSLX_RECORD_MAGIC_CHKD)
    {
        return FALSE;
    }

    SystemPageSize = Rp->SystemPageSize;
    LogPageSize = Rp->LogPageSize;

    /* Both must be >= 512 and powers of 2 */
    if (SystemPageSize < NTFS_BLOCK_SIZE ||
        LogPageSize < NTFS_BLOCK_SIZE ||
        !NtfslxIsPowerOfTwo(SystemPageSize) ||
        !NtfslxIsPowerOfTwo(LogPageSize))
    {
        return FALSE;
    }

    /* Position must be 0 (first page) or SystemPageSize (second page) */
    if (Position != 0 && Position != (LONGLONG)SystemPageSize)
        return FALSE;

    /* Version check: only 1.1 supported */
    if (Rp->MajorVersion != 1 || Rp->MinorVersion != 1)
        return FALSE;

    /* Validate USA if present (not CHKD with zero usa_count) */
    if (Rp->Magic == NTFSLX_RECORD_MAGIC_CHKD && Rp->UsaCount == 0)
        goto SkipUsa;

    UsaCount = 1 + (USHORT)(SystemPageSize / NTFS_BLOCK_SIZE);
    if (UsaCount != Rp->UsaCount)
        return FALSE;

    UsaOffset = Rp->UsaOffset;
    UsaEnd = UsaOffset + UsaCount * sizeof(USHORT);
    if (UsaOffset < sizeof(NTFSLX_RESTART_PAGE_HEADER) ||
        UsaEnd > NTFS_BLOCK_SIZE - sizeof(USHORT))
    {
        return FALSE;
    }

SkipUsa:
    /* Restart area offset must be 8-byte aligned and within the page */
    RaOffset = Rp->RestartAreaOffset;
    if ((RaOffset & 7) != 0 || RaOffset > SystemPageSize)
        return FALSE;

    return TRUE;
}

/*
 * Check whether the restart area indicates a clean volume.
 */
static BOOLEAN
NtfslxIsRestartAreaClean(
    _In_ PNTFSLX_RESTART_PAGE_HEADER Rp)
{
    PNTFSLX_RESTART_AREA Ra;
    USHORT RaOffset;

    RaOffset = Rp->RestartAreaOffset;
    Ra = (PNTFSLX_RESTART_AREA)((PUCHAR)Rp + RaOffset);

    /*
     * Clean volume indicators:
     * 1. ClientInUseList == LOGFILE_NO_CLIENT (logfile closed, pre-XP), OR
     * 2. RESTART_VOLUME_IS_CLEAN flag set (XP+)
     */
    if (Ra->ClientInUseList == NTFSLX_LOGFILE_NO_CLIENT)
        return TRUE;

    if (Ra->Flags & NTFSLX_RESTART_VOLUME_IS_CLEAN)
        return TRUE;

    return FALSE;
}

/*
 * NtfslxCheckLogFile - check whether the volume's LogFile is clean
 *
 * Reads the LogFile (MFT record 2) and examines both restart pages.
 * Sets *IsClean to TRUE if the volume was cleanly dismounted.
 *
 * Returns STATUS_SUCCESS if the check completed (regardless of clean/dirty),
 * or an error status if the LogFile could not be read.
 */
NTSTATUS
NtfslxCheckLogFile(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_opt_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _Out_ PBOOLEAN IsClean)
{
    NTSTATUS Status;
    PNTFSLX_MFT_RECORD LogFileRecord = NULL;
    PNTFSLX_ATTR_RECORD DataAttr = NULL;
    PNTFSLX_RUNLIST_ELEMENT LogRunlist = NULL;
    ULONG LogRunlistCount = 0;
    PUCHAR PageBuffer = NULL;
    ULONG PageSize;
    LONGLONG LogFileDataSize;
    LONGLONG DiskOffset;
    BOOLEAN Page1Valid = FALSE;
    BOOLEAN Page2Valid = FALSE;

    *IsClean = FALSE;

    /* Allocate buffer for LogFile MFT record */
    LogFileRecord = ExAllocatePoolWithTag(
        NonPagedPool, VolumeInfo->BytesPerFileRecord, NTFSLX_TAG);
    if (LogFileRecord == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    /* Read MFT record 2 (LogFile) */
    Status = NtfslxReadMftRecord(
        StorageDevice, VolumeInfo, MftRunlist,
        NTFSLX_FILE_LOGFILE, LogFileRecord);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /* Find $DATA attribute */
    Status = NtfslxFindAttribute(LogFileRecord,
        NTFSLX_ATTRIBUTE_DATA, NULL, 0, &DataAttr);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (DataAttr->NonResident == 0)
    {
        /* Resident LogFile is unexpected but technically valid if small */
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    LogFileDataSize = (LONGLONG)DataAttr->Data.NonResident.DataSize;

    /* Build runlist for LogFile data */
    {
        PNTFSLX_RUNLIST_ELEMENT TempRl = NULL;
        ULONG TempCount = 0;

        Status = NtfslxMappingPairsDecompress(VolumeInfo, DataAttr, &TempRl, &TempCount);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        LogRunlist = TempRl;
        LogRunlistCount = TempCount;
    }

    /*
     * Read the first NTFS_BLOCK_SIZE bytes from offset 0.
     * That's enough to check the restart page header.
     * We'll read a full system page once we know the size.
     */
    PageBuffer = ExAllocatePoolWithTag(NonPagedPool, NTFS_BLOCK_SIZE, NTFSLX_TAG);
    if (PageBuffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    /* Read first sector of LogFile */
    if (LogRunlist == NULL || LogRunlistCount == 0)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }

    DiskOffset = LogRunlist[0].Lcn * VolumeInfo->BytesPerCluster;
    Status = NtfslxReadDisk(StorageDevice, DiskOffset, NTFS_BLOCK_SIZE,
                            VolumeInfo->BytesPerSector, PageBuffer, FALSE);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /* Check first restart page header */
    if (NtfslxCheckRestartPageHeader(
            (PNTFSLX_RESTART_PAGE_HEADER)PageBuffer, 0))
    {
        PNTFSLX_RESTART_PAGE_HEADER Rp = (PNTFSLX_RESTART_PAGE_HEADER)PageBuffer;
        PageSize = Rp->SystemPageSize;
        Page1Valid = TRUE;

        /* Apply MST fixup */
        NtfslxPostReadMstFixup((PNTFSLX_RECORD_HEADER)PageBuffer, NTFS_BLOCK_SIZE);

        if (NtfslxIsRestartAreaClean(Rp))
        {
            *IsClean = TRUE;
            Status = STATUS_SUCCESS;
            goto Cleanup;
        }
    }
    else
    {
        PageSize = NTFSLX_DEFAULT_LOG_PAGE_SIZE;
    }

    /* Try the second restart page at offset = SystemPageSize */
    if (LogFileDataSize >= (LONGLONG)(PageSize + NTFS_BLOCK_SIZE))
    {
        LONGLONG SecondPageLcn;
        LONGLONG SecondPageVcn;
        LONGLONG SecondPageOffset = (LONGLONG)PageSize;
        ULONG I;

        SecondPageVcn = SecondPageOffset / VolumeInfo->BytesPerCluster;

        /* Find the LCN for this VCN */
        SecondPageLcn = -1;
        for (I = 0; I < LogRunlistCount; I++)
        {
            if (SecondPageVcn >= LogRunlist[I].Vcn &&
                SecondPageVcn < LogRunlist[I].Vcn + LogRunlist[I].Length)
            {
                SecondPageLcn = LogRunlist[I].Lcn +
                    (SecondPageVcn - LogRunlist[I].Vcn);
                break;
            }
        }

        if (SecondPageLcn >= 0)
        {
            LONGLONG Offset2 = SecondPageLcn * VolumeInfo->BytesPerCluster +
                (SecondPageOffset % VolumeInfo->BytesPerCluster);

            Status = NtfslxReadDisk(StorageDevice, Offset2, NTFS_BLOCK_SIZE,
                                    VolumeInfo->BytesPerSector, PageBuffer, FALSE);
            if (NT_SUCCESS(Status))
            {
                if (NtfslxCheckRestartPageHeader(
                        (PNTFSLX_RESTART_PAGE_HEADER)PageBuffer,
                        SecondPageOffset))
                {
                    Page2Valid = TRUE;
                    NtfslxPostReadMstFixup(
                        (PNTFSLX_RECORD_HEADER)PageBuffer, NTFS_BLOCK_SIZE);

                    if (NtfslxIsRestartAreaClean(
                            (PNTFSLX_RESTART_PAGE_HEADER)PageBuffer))
                    {
                        *IsClean = TRUE;
                    }
                }
            }
        }
    }

    if (!Page1Valid && !Page2Valid)
    {
        DbgPrint("NTFSLX: No valid restart page found in LogFile\n");
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }

    Status = STATUS_SUCCESS;

Cleanup:
    if (PageBuffer != NULL)
        ExFreePoolWithTag(PageBuffer, NTFSLX_TAG);
    if (LogRunlist != NULL)
        ExFreePoolWithTag(LogRunlist, NTFSLX_TAG);
    if (LogFileRecord != NULL)
        ExFreePoolWithTag(LogFileRecord, NTFSLX_TAG);

    return Status;
}

/*
 * NtfslxFillLogFile - overwrite the entire $LogFile with 0xFF.
 *
 * Windows NTFS replays $LogFile on every writable mount. Because this
 * driver has no CLFS journal of its own, any metadata mutation we make
 * leaves garbage log state on disk that Windows would later try to
 * replay — which would then fight with our changes and potentially
 * roll back real work. The safe thing for a bring-up driver is to
 * fill the entire logfile with 0xFF (the NTFS convention for
 * "uninitialized"); Windows then sees no transactions to replay and
 * simply rebuilds the restart pages on first write.
 *
 * Called once at mount time, after the dirty flag has been set and
 * before any real metadata mutation. Returns STATUS_SUCCESS on success,
 * any NTSTATUS otherwise; callers log failures but usually treat them
 * as non-fatal — the worst case is that Windows will rerun chkdsk.
 */
NTSTATUS
NtfslxFillLogFile(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist)
{
    NTSTATUS Status;
    PNTFSLX_MFT_RECORD LogFileRecord = NULL;
    PNTFSLX_ATTR_RECORD DataAttr;
    PNTFSLX_RUNLIST_ELEMENT LogRuns = NULL;
    ULONG LogRunCount = 0;
    PUCHAR FillBuffer = NULL;
    ULONG FillBufferSize;
    ULONG I;

    if (StorageDevice == NULL || VolumeInfo == NULL)
        return STATUS_INVALID_PARAMETER;

    LogFileRecord = ExAllocatePoolWithTag(NonPagedPool,
                                          VolumeInfo->BytesPerFileRecord,
                                          NTFSLX_TAG);
    if (LogFileRecord == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = NtfslxReadMftRecord(StorageDevice, VolumeInfo, MftRunlist,
                                 NTFSLX_FILE_LOGFILE, LogFileRecord);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("ntfslx: FillLogFile: read $LogFile MFT failed 0x%08lx\n",
                 Status);
        goto Cleanup;
    }

    Status = NtfslxFindAttribute(LogFileRecord, NTFSLX_ATTRIBUTE_DATA,
                                 NULL, 0, &DataAttr);
    if (!NT_SUCCESS(Status) || DataAttr->NonResident == 0)
    {
        DbgPrint("ntfslx: FillLogFile: $LogFile $DATA missing or resident 0x%08lx\n",
                 Status);
        Status = NT_SUCCESS(Status) ? STATUS_NOT_SUPPORTED : Status;
        goto Cleanup;
    }

    Status = NtfslxMappingPairsDecompress(VolumeInfo, DataAttr,
                                          &LogRuns, &LogRunCount);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("ntfslx: FillLogFile: decompress runs failed 0x%08lx\n",
                 Status);
        goto Cleanup;
    }

    /*
     * 64 KB per write is plenty: most loops fit in one iteration per run,
     * and we keep the allocation small to stay inside non-paged quota
     * during boot-time mount.
     */
    FillBufferSize = 65536;
    if (FillBufferSize > VolumeInfo->BytesPerCluster * 64)
        FillBufferSize = VolumeInfo->BytesPerCluster * 64;
    if (FillBufferSize == 0)
        FillBufferSize = VolumeInfo->BytesPerCluster;

    FillBuffer = ExAllocatePoolWithTag(NonPagedPool, FillBufferSize, NTFSLX_TAG);
    if (FillBuffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }
    RtlFillMemory(FillBuffer, FillBufferSize, 0xFF);

    for (I = 0; I < LogRunCount; I++)
    {
        ULONGLONG RunBytes;
        ULONGLONG RunLcn;
        ULONGLONG ByteOffset;
        ULONGLONG Remaining;

        if (LogRuns[I].Length == 0)
            break;
        if (LogRuns[I].Lcn < 0)
            continue; /* sparse hole in $LogFile? skip defensively. */

        RunBytes = (ULONGLONG)LogRuns[I].Length *
                   (ULONGLONG)VolumeInfo->BytesPerCluster;
        RunLcn = (ULONGLONG)LogRuns[I].Lcn;
        ByteOffset = RunLcn * (ULONGLONG)VolumeInfo->BytesPerCluster;
        Remaining = RunBytes;

        while (Remaining != 0)
        {
            ULONG Chunk = (Remaining > FillBufferSize)
                            ? FillBufferSize
                            : (ULONG)Remaining;

            Status = NtfslxWriteDisk(StorageDevice,
                                     (LONGLONG)ByteOffset,
                                     Chunk,
                                     VolumeInfo->BytesPerSector,
                                     FillBuffer,
                                     TRUE);
            if (!NT_SUCCESS(Status))
            {
                DbgPrint("ntfslx: FillLogFile: write run %lu offset=0x%I64x chunk=%lu failed 0x%08lx\n",
                         I, ByteOffset, Chunk, Status);
                goto Cleanup;
            }
            ByteOffset += Chunk;
            Remaining -= Chunk;
        }
    }

    DbgPrint("ntfslx: FillLogFile: wiped $LogFile to 0xFF across %lu runs\n",
             LogRunCount);
    Status = STATUS_SUCCESS;

Cleanup:
    if (FillBuffer != NULL)
        ExFreePoolWithTag(FillBuffer, NTFSLX_TAG);
    if (LogRuns != NULL)
        ExFreePoolWithTag(LogRuns, NTFSLX_TAG);
    if (LogFileRecord != NULL)
        ExFreePoolWithTag(LogFileRecord, NTFSLX_TAG);

    return Status;
}
