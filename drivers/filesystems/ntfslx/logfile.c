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

static
BOOLEAN
NtfslxValidateResidentAttributeValueBounds(
    _In_ PNTFSLX_ATTR_RECORD Attribute)
{
    ULONG ValueOffset;
    ULONG ValueLength;

    if (Attribute == NULL || Attribute->NonResident != 0)
    {
        return FALSE;
    }

    ValueOffset = Attribute->Data.Resident.ValueOffset;
    ValueLength = Attribute->Data.Resident.ValueLength;

    if (ValueOffset < FIELD_OFFSET(NTFSLX_ATTR_RECORD, Data.Resident) ||
        ValueOffset > Attribute->Length ||
        ValueLength > Attribute->Length - ValueOffset)
    {
        return FALSE;
    }

    return TRUE;
}

static
LONGLONG
NtfslxRunlistVcnToLcn(
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

static
NTSTATUS
NtfslxReadLogFileData(
    _In_ BOOLEAN ResidentData,
    _In_reads_bytes_opt_(ResidentDataSize) const PUCHAR ResidentBuffer,
    _In_ ULONGLONG ResidentDataSize,
    _In_opt_ PDEVICE_OBJECT StorageDevice,
    _In_opt_ PNTFSLX_RUNLIST_ELEMENT LogRunlist,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ ULONGLONG Offset,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    ULONGLONG Remaining;
    ULONGLONG CurrentOffset;
    ULONG ClusterOffset;
    ULONG ChunkLength;
    LONGLONG Vcn;
    LONGLONG Lcn;
    NTSTATUS Status;

    if (Length == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (ResidentData)
    {
        if (ResidentBuffer == NULL ||
            Offset > ResidentDataSize ||
            (ULONGLONG)Length > ResidentDataSize - Offset)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        RtlCopyMemory(Buffer, ResidentBuffer + Offset, Length);
        return STATUS_SUCCESS;
    }

    if (StorageDevice == NULL || LogRunlist == NULL || VolumeInfo == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Remaining = Length;
    CurrentOffset = Offset;
    while (Remaining != 0)
    {
        Vcn = (LONGLONG)(CurrentOffset / VolumeInfo->BytesPerCluster);
        ClusterOffset = (ULONG)(CurrentOffset % VolumeInfo->BytesPerCluster);
        ChunkLength = VolumeInfo->BytesPerCluster - ClusterOffset;
        if ((ULONGLONG)ChunkLength > Remaining)
        {
            ChunkLength = (ULONG)Remaining;
        }

        Lcn = NtfslxRunlistVcnToLcn(LogRunlist, Vcn);
        if (Lcn == NTFSLX_LCN_HOLE || Lcn < 0)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Status = NtfslxReadDisk(StorageDevice,
                                (LONGLONG)(Lcn * VolumeInfo->BytesPerCluster + ClusterOffset),
                                ChunkLength,
                                VolumeInfo->BytesPerSector,
                                Buffer,
                                FALSE);
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
 * Validate a single restart page header for consistency.
 */
static BOOLEAN
NtfslxCheckRestartPageHeader(
    _In_ PNTFSLX_RESTART_PAGE_HEADER Rp,
    _In_ ULONG BufferLength,
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

    if (BufferLength < sizeof(NTFSLX_RESTART_PAGE_HEADER))
        return FALSE;

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
        UsaEnd > BufferLength - sizeof(USHORT))
    {
        return FALSE;
    }

SkipUsa:
    /* Restart area offset must be 8-byte aligned and within the buffered page */
    RaOffset = Rp->RestartAreaOffset;
    if ((RaOffset & 7) != 0 ||
        RaOffset > SystemPageSize ||
        RaOffset < sizeof(NTFSLX_RESTART_PAGE_HEADER) ||
        RaOffset > BufferLength ||
        RaOffset + sizeof(NTFSLX_RESTART_AREA) > BufferLength)
    {
        return FALSE;
    }

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
    PUCHAR PageBuffer = NULL;
    ULONG PageSize;
    ULONG PageReadLength;
    LONGLONG LogFileDataSize;
    BOOLEAN ResidentData = FALSE;
    PUCHAR ResidentBuffer = NULL;
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
        if (!NtfslxValidateResidentAttributeValueBounds(DataAttr))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }

        ResidentData = TRUE;
        ResidentBuffer = (PUCHAR)DataAttr + DataAttr->Data.Resident.ValueOffset;
        LogFileDataSize = (LONGLONG)DataAttr->Data.Resident.ValueLength;
        NTFSDBG("ntfslx: CheckLogFile: resident $LogFile size=%I64u\n",
                 LogFileDataSize);
    }
    else
    {
        PNTFSLX_RUNLIST_ELEMENT TempRl = NULL;
        ULONG TempCount = 0;

        LogFileDataSize = (LONGLONG)DataAttr->Data.NonResident.DataSize;

        Status = NtfslxMappingPairsDecompress(VolumeInfo, DataAttr, &TempRl, &TempCount);
        if (!NT_SUCCESS(Status))
            goto Cleanup;

        LogRunlist = TempRl;
    }

    /*
     * Read the first restart-page chunk from offset 0.
     * Resident LogFile values may be smaller than 512 bytes; use what exists.
     */
    PageBuffer = ExAllocatePoolWithTag(NonPagedPool, NTFS_BLOCK_SIZE, NTFSLX_TAG);
    if (PageBuffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    PageReadLength = NTFS_BLOCK_SIZE;
    if (ResidentData && LogFileDataSize < (LONGLONG)PageReadLength)
    {
        PageReadLength = (ULONG)LogFileDataSize;
    }

    if (PageReadLength < sizeof(NTFSLX_RESTART_PAGE_HEADER))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }

    Status = NtfslxReadLogFileData(ResidentData,
                                   ResidentBuffer,
                                   (ULONGLONG)LogFileDataSize,
                                   StorageDevice,
                                   LogRunlist,
                                   VolumeInfo,
                                   0,
                                   PageBuffer,
                                   PageReadLength);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /* Check first restart page header */
    if (NtfslxCheckRestartPageHeader(
            (PNTFSLX_RESTART_PAGE_HEADER)PageBuffer,
            PageReadLength,
            0))
    {
        PNTFSLX_RESTART_PAGE_HEADER Rp = (PNTFSLX_RESTART_PAGE_HEADER)PageBuffer;
        PageSize = Rp->SystemPageSize;
        Page1Valid = TRUE;

        /* Apply MST fixup */
        NtfslxPostReadMstFixup((PNTFSLX_RECORD_HEADER)PageBuffer, PageReadLength);

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
        LONGLONG SecondPageOffset = (LONGLONG)PageSize;

        Status = NtfslxReadLogFileData(ResidentData,
                                       ResidentBuffer,
                                       (ULONGLONG)LogFileDataSize,
                                       StorageDevice,
                                       LogRunlist,
                                       VolumeInfo,
                                       (ULONGLONG)SecondPageOffset,
                                       PageBuffer,
                                       NTFS_BLOCK_SIZE);
        if (NT_SUCCESS(Status) &&
            NtfslxCheckRestartPageHeader((PNTFSLX_RESTART_PAGE_HEADER)PageBuffer,
                                         NTFS_BLOCK_SIZE,
                                         SecondPageOffset))
        {
            Page2Valid = TRUE;
            NtfslxPostReadMstFixup((PNTFSLX_RECORD_HEADER)PageBuffer, NTFS_BLOCK_SIZE);

            if (NtfslxIsRestartAreaClean((PNTFSLX_RESTART_PAGE_HEADER)PageBuffer))
            {
                *IsClean = TRUE;
            }
        }
    }

    if (!Page1Valid && !Page2Valid)
    {
        NTFSDBG("NTFSLX: No valid restart page found in LogFile\n");
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
    PUCHAR ResidentBuffer = NULL;
    ULONG FillBufferSize;
    ULONG I;
    BOOLEAN ResidentData = FALSE;

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
        NTFSDBG("ntfslx: FillLogFile: read $LogFile MFT failed 0x%08lx\n",
                 Status);
        goto Cleanup;
    }

    Status = NtfslxFindAttribute(LogFileRecord, NTFSLX_ATTRIBUTE_DATA,
                                 NULL, 0, &DataAttr);
    if (!NT_SUCCESS(Status))
    {
        NTFSDBG("ntfslx: FillLogFile: $LogFile $DATA missing 0x%08lx\n",
                 Status);
        goto Cleanup;
    }

    if (DataAttr->NonResident == 0)
    {
        if (!NtfslxValidateResidentAttributeValueBounds(DataAttr))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }

        ResidentData = TRUE;
        ResidentBuffer = (PUCHAR)DataAttr + DataAttr->Data.Resident.ValueOffset;
        NTFSDBG("ntfslx: FillLogFile: resident $LogFile size=%lu\n",
                 DataAttr->Data.Resident.ValueLength);
    }
    else
    {
        Status = NtfslxMappingPairsDecompress(VolumeInfo, DataAttr,
                                              &LogRuns, &LogRunCount);
        if (!NT_SUCCESS(Status))
        {
            NTFSDBG("ntfslx: FillLogFile: decompress runs failed 0x%08lx\n",
                     Status);
            goto Cleanup;
        }
    }

    if (ResidentData)
    {
        if (DataAttr->Data.Resident.ValueLength == 0)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }

        RtlFillMemory(ResidentBuffer, DataAttr->Data.Resident.ValueLength, 0xFF);
        Status = NtfslxWriteMftRecord(NULL, StorageDevice, VolumeInfo, MftRunlist,
                                      NTFSLX_FILE_LOGFILE, LogFileRecord);
        if (NT_SUCCESS(Status))
        {
            NTFSDBG("ntfslx: FillLogFile: wiped resident $LogFile to 0xFF (%lu bytes)\n",
                     DataAttr->Data.Resident.ValueLength);
        }
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
                NTFSDBG("ntfslx: FillLogFile: write run %lu offset=0x%I64x chunk=%lu failed 0x%08lx\n",
                         I, ByteOffset, Chunk, Status);
                goto Cleanup;
            }
            ByteOffset += Chunk;
            Remaining -= Chunk;
        }
    }

    NTFSDBG("ntfslx: FillLogFile: wiped $LogFile to 0xFF across %lu runs\n",
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

/* ========================================================================
 * Phase-1 redo-record journal — see docs/ntfs-specs/13_ntfslx_phase1_redo_records.txt
 * ======================================================================== */

/*
 * Translate a byte offset within $LogFile to a disk byte offset using
 * the cached journal runlist. Returns -1 on failure (sparse or out of
 * range).
 */
static
LONGLONG
NtfslxJournalOffsetToDiskOffset(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG LogOffset)
{
    LONGLONG Vcn;
    LONGLONG Lcn;
    ULONG ClusterOffset;
    PNTFSLX_VOLUME_INFO VolumeInfo;

    VolumeInfo = &DevExt->VolumeInfo;

    if (DevExt->JournalLogRunlist == NULL || VolumeInfo->BytesPerCluster == 0)
        return -1;

    Vcn = (LONGLONG)(LogOffset / VolumeInfo->BytesPerCluster);
    ClusterOffset = (ULONG)(LogOffset % VolumeInfo->BytesPerCluster);

    Lcn = NtfslxRunlistVcnToLcn(DevExt->JournalLogRunlist, Vcn);
    if (Lcn < 0)
        return -1;

    return (LONGLONG)((ULONGLONG)Lcn * VolumeInfo->BytesPerCluster + ClusterOffset);
}

/*
 * Write Length bytes from Buffer to LogOffset within $LogFile, walking
 * cluster boundaries as needed. Used both for stamping the synthetic
 * Phase-1 restart page and for appending redo records.
 */
static
NTSTATUS
NtfslxJournalWriteAtOffset(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG LogOffset,
    _In_reads_bytes_(Length) PUCHAR Buffer,
    _In_ ULONG Length)
{
    PNTFSLX_VOLUME_INFO VolumeInfo;
    ULONGLONG Remaining;
    ULONGLONG CurrentOffset;
    ULONG ChunkLength;
    ULONG ClusterOffset;
    LONGLONG DiskOffset;
    NTSTATUS Status;

    VolumeInfo = &DevExt->VolumeInfo;
    Remaining = Length;
    CurrentOffset = LogOffset;

    while (Remaining != 0)
    {
        ClusterOffset = (ULONG)(CurrentOffset % VolumeInfo->BytesPerCluster);
        ChunkLength = VolumeInfo->BytesPerCluster - ClusterOffset;
        if ((ULONGLONG)ChunkLength > Remaining)
            ChunkLength = (ULONG)Remaining;

        DiskOffset = NtfslxJournalOffsetToDiskOffset(DevExt, CurrentOffset);
        if (DiskOffset < 0)
            return STATUS_FILE_CORRUPT_ERROR;

        Status = NtfslxWriteDisk(DevExt->StorageDevice,
                                 DiskOffset,
                                 ChunkLength,
                                 VolumeInfo->BytesPerSector,
                                 Buffer,
                                 TRUE);
        if (!NT_SUCCESS(Status))
            return Status;

        Buffer += ChunkLength;
        CurrentOffset += ChunkLength;
        Remaining -= ChunkLength;
    }

    return STATUS_SUCCESS;
}

/*
 * Stamp a synthetic Microsoft-compatible RSTR restart page (and mirror)
 * at the head of $LogFile. Used both for marking the log "clean" so
 * NtfslxCheckLogFile / Windows NTFS treat the log as having no
 * outstanding transactions, and as the prelude to appending Phase-1
 * redo records past the RSTR pair. Phase-1 records start at offset
 * 2 * SystemPageSize and carry an internal magic ('XLOG' header)
 * that Phase-2 recovery scans for; the wrapping RSTR stays in the
 * Microsoft 'RSTR' format so existing tooling treats the volume as
 * recoverable / clean.
 *
 * Called from JournalInitialize when the existing log was wiped to
 * 0xFF (or otherwise lacks a valid restart page).
 */
static
NTSTATUS
NtfslxJournalStampRestartPage(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt)
{
    UCHAR Page[NTFSLX_PHASE1_RSTR_PAGE_SIZE];
    PNTFSLX_RESTART_PAGE_HEADER Rp;
    PNTFSLX_RESTART_AREA Ra;
    PUSHORT Usa;
    USHORT UsaCount;
    USHORT UsaOffset;
    USHORT RestartAreaOffset;
    ULONG SystemPageSize = NTFSLX_PHASE1_RSTR_PAGE_SIZE;
    ULONG I;
    NTSTATUS Status;

    RtlZeroMemory(Page, sizeof(Page));
    Rp = (PNTFSLX_RESTART_PAGE_HEADER)Page;

    UsaOffset = (USHORT)sizeof(NTFSLX_RESTART_PAGE_HEADER);
    UsaCount = (USHORT)(1 + SystemPageSize / NTFS_BLOCK_SIZE);
    /* Round restart area to 8-byte boundary past the USA. */
    RestartAreaOffset = (USHORT)((UsaOffset + UsaCount * sizeof(USHORT) + 7) & ~7);

    Rp->Magic = NTFSLX_RECORD_MAGIC_RSTR;
    Rp->UsaOffset = UsaOffset;
    Rp->UsaCount = UsaCount;
    Rp->ChkdskLsn = 0;
    Rp->SystemPageSize = SystemPageSize;
    Rp->LogPageSize = SystemPageSize;
    Rp->RestartAreaOffset = RestartAreaOffset;
    Rp->MinorVersion = 1;
    Rp->MajorVersion = 1;

    /*
     * Mark the restart area "clean" by zeroing the LSN and setting
     * ClientInUseList to LOGFILE_NO_CLIENT — this is the legacy
     * pre-XP "no client open" indicator that NtfslxIsRestartAreaClean
     * already accepts. Newer Windows uses the RESTART_VOLUME_IS_CLEAN
     * flag; we set both to be safe.
     */
    Ra = (PNTFSLX_RESTART_AREA)(Page + RestartAreaOffset);
    Ra->CurrentLsn = 0;
    Ra->LogClients = 0;
    Ra->ClientFreeList = NTFSLX_LOGFILE_NO_CLIENT;
    Ra->ClientInUseList = NTFSLX_LOGFILE_NO_CLIENT;
    Ra->Flags = NTFSLX_RESTART_VOLUME_IS_CLEAN;
    Ra->SeqNumberBits = 0;
    Ra->RestartAreaLength = (USHORT)sizeof(NTFSLX_RESTART_AREA);
    Ra->ClientArrayOffset = (USHORT)sizeof(NTFSLX_RESTART_AREA);
    Ra->FileSize = DevExt->JournalLogFileSize;
    Ra->LastLsnDataLength = 0;
    Ra->LogRecordHeaderLength = 0;
    Ra->LogPageDataOffset = 0;
    Ra->RestartLogOpenCount = 0;

    /*
     * Apply update-sequence-array fixup. The USN sits at UsaOffset;
     * each 512-byte chunk in the page has its last 2 bytes saved into
     * the USA and replaced with the USN. Use USN=1 (USN==0 is reserved
     * as "uninitialized" by NTFS).
     */
    Usa = (PUSHORT)(Page + UsaOffset);
    Usa[0] = 1;
    for (I = 0; I < (ULONG)(UsaCount - 1); I++)
    {
        ULONG SectorEnd = (I + 1) * NTFS_BLOCK_SIZE - 2;
        Usa[1 + I] = *(PUSHORT)(Page + SectorEnd);
        *(PUSHORT)(Page + SectorEnd) = Usa[0];
    }

    Status = NtfslxJournalWriteAtOffset(DevExt, 0, Page, sizeof(Page));
    if (!NT_SUCCESS(Status))
        return Status;

    return NtfslxJournalWriteAtOffset(DevExt,
                                       NTFSLX_PHASE1_RSTR_PAGE_SIZE,
                                       Page,
                                       sizeof(Page));
}

NTSTATUS
NtfslxJournalInitialize(
    _Inout_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ BOOLEAN LogWasWiped)
{
    NTSTATUS Status;
    PNTFSLX_MFT_RECORD LogFileRecord = NULL;
    PNTFSLX_ATTR_RECORD DataAttr;
    PNTFSLX_RUNLIST_ELEMENT LogRuns = NULL;
    ULONG LogRunCount = 0;
    ULONGLONG LogFileSize;
    ULONGLONG ReservedHead;

    if (DevExt == NULL || DevExt->StorageDevice == NULL ||
        DevExt->MftRunlist == NULL || DevExt->VolumeInfo.BytesPerCluster == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeInitializeGuardedMutex(&DevExt->JournalLock);
    DevExt->JournalLockInitialized = TRUE;

    LogFileRecord = ExAllocatePoolWithTag(NonPagedPool,
                                          DevExt->VolumeInfo.BytesPerFileRecord,
                                          NTFSLX_TAG);
    if (LogFileRecord == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = NtfslxReadMftRecord(DevExt->StorageDevice,
                                 &DevExt->VolumeInfo,
                                 DevExt->MftRunlist,
                                 NTFSLX_FILE_LOGFILE,
                                 LogFileRecord);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = NtfslxFindAttribute(LogFileRecord,
                                 NTFSLX_ATTRIBUTE_DATA,
                                 NULL, 0, &DataAttr);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (DataAttr->NonResident == 0)
    {
        /*
         * Resident $LogFile is too small to host Phase-1 records (the
         * synthetic restart pair alone is 8 KiB). Disable journaling
         * for this volume and let the rest of the driver run with the
         * "no redo records" path — it's no worse than today.
         */
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    LogFileSize = DataAttr->Data.NonResident.DataSize;
    Status = NtfslxMappingPairsDecompress(&DevExt->VolumeInfo,
                                          DataAttr,
                                          &LogRuns,
                                          &LogRunCount);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    ReservedHead = (ULONGLONG)NTFSLX_PHASE1_RSTR_PAGE_SIZE * 2;
    if (LogFileSize <= ReservedHead)
    {
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    DevExt->JournalLogRunlist = LogRuns;
    DevExt->JournalLogRunCount = LogRunCount;
    DevExt->JournalLogFileSize = LogFileSize;
    DevExt->JournalDataAreaLength = (ULONG)
        ((LogFileSize - ReservedHead) > MAXULONG
            ? MAXULONG
            : (LogFileSize - ReservedHead));
    DevExt->JournalNextOffset = ReservedHead;
    DevExt->JournalNextLsn = 1;
    DevExt->JournalTransactionId = 0;
    LogRuns = NULL;

    if (LogWasWiped)
    {
        /*
         * Stamp the synthetic Phase-1 RSTR pair so subsequent mounts
         * can locate the records and so Phase-2 recovery can detect
         * a Phase-1 log lineage by its 'XLOG' magic.
         */
        Status = NtfslxJournalStampRestartPage(DevExt);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(DevExt->JournalLogRunlist, NTFSLX_TAG);
            DevExt->JournalLogRunlist = NULL;
            DevExt->JournalLogRunCount = 0;
            DevExt->JournalLogFileSize = 0;
            DevExt->JournalDataAreaLength = 0;
            goto Cleanup;
        }
        /*
         * Allocate the batch buffer. 64 KiB caps the in-flight pending
         * window: a crash drops at most this many bytes of records
         * (= the records that were committed in-memory but not yet
         * flushed). This is acceptable for Phase-1 because no recovery
         * driver consumes the records yet — they're durable for Phase 2.
         */
        DevExt->JournalBatchSize = 64 * 1024;
        DevExt->JournalBatchBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                                          DevExt->JournalBatchSize,
                                                          NTFSLX_TAG);
        if (DevExt->JournalBatchBuffer == NULL)
        {
            ExFreePoolWithTag(DevExt->JournalLogRunlist, NTFSLX_TAG);
            DevExt->JournalLogRunlist = NULL;
            DevExt->JournalLogRunCount = 0;
            DevExt->JournalLogFileSize = 0;
            DevExt->JournalDataAreaLength = 0;
            DevExt->JournalBatchSize = 0;
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }
        DevExt->JournalBatchUsed = 0;
        DevExt->JournalEnabled = TRUE;
    }
    else
    {
        /*
         * Existing valid restart page is from a Microsoft NTFS run
         * (or a previous Phase-1 ntfslx mount). We can't safely
         * append our records without overwriting whatever the page
         * is tracking, so disable journaling for this mount. Phase
         * 2 recovery will sort the log out at next chkdsk.
         */
        DevExt->JournalEnabled = FALSE;
    }

Cleanup:
    if (LogRuns != NULL)
        ExFreePoolWithTag(LogRuns, NTFSLX_TAG);
    if (LogFileRecord != NULL)
        ExFreePoolWithTag(LogFileRecord, NTFSLX_TAG);
    return Status;
}

VOID
NtfslxJournalTeardown(
    _Inout_ PNTFSLX_DEVICE_EXTENSION DevExt)
{
    if (DevExt == NULL)
        return;

    /*
     * Drain any pending batch to disk before we free the runlist —
     * otherwise records that were promised durable in-memory would
     * silently disappear on dismount/shutdown.
     */
    NtfslxJournalFlush(DevExt);

    if (DevExt->JournalBatchBuffer != NULL)
    {
        ExFreePoolWithTag(DevExt->JournalBatchBuffer, NTFSLX_TAG);
        DevExt->JournalBatchBuffer = NULL;
    }
    DevExt->JournalBatchSize = 0;
    DevExt->JournalBatchUsed = 0;

    if (DevExt->JournalLogRunlist != NULL)
    {
        ExFreePoolWithTag(DevExt->JournalLogRunlist, NTFSLX_TAG);
        DevExt->JournalLogRunlist = NULL;
    }
    DevExt->JournalLogRunCount = 0;
    DevExt->JournalLogFileSize = 0;
    DevExt->JournalDataAreaLength = 0;
    DevExt->JournalNextOffset = 0;
    DevExt->JournalNextLsn = 0;
    DevExt->JournalEnabled = FALSE;
}

/*
 * Drain JournalBatchBuffer to $LogFile. Caller must hold JournalLock.
 * On IO failure we disable journaling; the in-memory pending bytes
 * are dropped because the on-disk log is now untrusted, matching the
 * Phase-1 "best effort" stance. On success the buffer is reset and
 * JournalNextOffset advances by the flushed length.
 */
static
NTSTATUS
NtfslxJournalFlushLocked(
    _Inout_ PNTFSLX_DEVICE_EXTENSION DevExt)
{
    ULONG Pending;
    NTSTATUS Status;

    if (DevExt->JournalBatchBuffer == NULL || DevExt->JournalBatchUsed == 0)
        return STATUS_SUCCESS;

    Pending = DevExt->JournalBatchUsed;

    Status = NtfslxJournalWriteAtOffset(DevExt,
                                        DevExt->JournalNextOffset,
                                        DevExt->JournalBatchBuffer,
                                        Pending);
    if (!NT_SUCCESS(Status))
    {
        /*
         * Disk-write failure: the on-disk log is no longer in a known
         * state, so disable journaling for the rest of the mount and
         * drop the buffer's bytes. Phase-2 recovery will sort this
         * out via chkdsk-equivalent logic.
         */
        DevExt->JournalEnabled = FALSE;
        DevExt->JournalBatchUsed = 0;
        return Status;
    }

    DevExt->JournalNextOffset += Pending;
    DevExt->JournalBatchUsed = 0;
    return STATUS_SUCCESS;
}

/*
 * Public flush entry point. Acquires JournalLock and drains the batch.
 * Used by FSCTL_FLUSH_BUFFERS, IRP_MJ_SHUTDOWN, FSCTL_DISMOUNT_VOLUME,
 * and NtfslxJournalTeardown.
 */
NTSTATUS
NtfslxJournalFlush(
    _Inout_ PNTFSLX_DEVICE_EXTENSION DevExt)
{
    NTSTATUS Status;

    if (DevExt == NULL || !DevExt->JournalLockInitialized)
        return STATUS_SUCCESS;

    KeAcquireGuardedMutex(&DevExt->JournalLock);
    Status = NtfslxJournalFlushLocked(DevExt);
    KeReleaseGuardedMutex(&DevExt->JournalLock);
    return Status;
}

/*
 * Append a single Phase-1 redo record. Returns STATUS_SUCCESS even when
 * journaling is disabled — callers should always call before applying
 * the metadata change, and "no journal" simply means the record is a
 * no-op. Length must be a multiple of 8 and >= sizeof(NTFSLX_PHASE1_LOG_RECORD).
 *
 * The append copies the record into JournalBatchBuffer under JournalLock;
 * the batch is flushed lazily (when it crosses the high-water mark, or
 * via NtfslxJournalFlush at FSCTL_FLUSH_BUFFERS / shutdown / dismount).
 * This collapses N synchronous IOs into ~one IO per ~32 KiB of records,
 * which is what restores delete p99 below the 50 ms TC.4 threshold.
 */
static
NTSTATUS
NtfslxJournalAppendRecord(
    _Inout_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_reads_bytes_(Length) PUCHAR RecordBytes,
    _In_ ULONG Length)
{
    PNTFSLX_PHASE1_LOG_RECORD Header;
    ULONG Watermark;
    NTSTATUS Status;

    if (DevExt == NULL || RecordBytes == NULL ||
        Length < sizeof(NTFSLX_PHASE1_LOG_RECORD) || (Length & 7) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!DevExt->JournalLockInitialized || !DevExt->JournalEnabled)
    {
        /* Journaling disabled — silently drop. Caller proceeds. */
        return STATUS_SUCCESS;
    }

    KeAcquireGuardedMutex(&DevExt->JournalLock);

    if (!DevExt->JournalEnabled || DevExt->JournalBatchBuffer == NULL)
    {
        KeReleaseGuardedMutex(&DevExt->JournalLock);
        return STATUS_SUCCESS;
    }

    if (DevExt->JournalNextOffset + DevExt->JournalBatchUsed + Length >
        DevExt->JournalLogFileSize)
    {
        /*
         * Append-only Phase-1 log out of room. Disable journaling
         * for the remainder of this mount; callers continue without
         * recording. Phase 2 will introduce circular reuse.
         */
        DevExt->JournalEnabled = FALSE;
        KeReleaseGuardedMutex(&DevExt->JournalLock);
        return STATUS_LOG_FILE_FULL;
    }

    /*
     * If the record won't fit in the remaining buffer, flush first.
     * A record that's larger than the buffer entirely is impossible
     * given the 64 KiB buffer and the USHORT-bounded Length field of
     * the record header (max 0xFFF8 = ~64 KiB), but JournalBatchSize
     * must always be >= the largest single record.
     */
    if (DevExt->JournalBatchUsed + Length > DevExt->JournalBatchSize)
    {
        Status = NtfslxJournalFlushLocked(DevExt);
        if (!NT_SUCCESS(Status))
        {
            KeReleaseGuardedMutex(&DevExt->JournalLock);
            return Status;
        }
    }

    /* Copy the record into the buffer with a final LSN + magic stamped. */
    RtlCopyMemory(DevExt->JournalBatchBuffer + DevExt->JournalBatchUsed,
                  RecordBytes, Length);
    Header = (PNTFSLX_PHASE1_LOG_RECORD)
             (DevExt->JournalBatchBuffer + DevExt->JournalBatchUsed);
    Header->Magic = NTFSLX_PHASE1_RECORD_MAGIC;
    Header->Lsn = DevExt->JournalNextLsn;
    Header->Length = (USHORT)Length;

    DevExt->JournalBatchUsed += Length;
    DevExt->JournalNextLsn += 1;

    /*
     * High watermark: flush when the buffer is half-full so the next
     * burst of appends rarely blocks waiting on a flush. Half is a
     * trade between flush amortization (bigger batches = fewer IOs)
     * and crash-window size (smaller batches = less data lost on
     * crash). 32 KiB is several hundred typical records.
     */
    Watermark = DevExt->JournalBatchSize / 2;
    if (DevExt->JournalBatchUsed >= Watermark)
    {
        Status = NtfslxJournalFlushLocked(DevExt);
        if (!NT_SUCCESS(Status))
        {
            KeReleaseGuardedMutex(&DevExt->JournalLock);
            return Status;
        }
    }

    KeReleaseGuardedMutex(&DevExt->JournalLock);
    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxJournalAppendMftBitmapBit(
    _Inout_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG MftIndex,
    _In_ BOOLEAN Set)
{
    UCHAR Buffer[sizeof(NTFSLX_PHASE1_LOG_RECORD) +
                 sizeof(NTFSLX_PHASE1_MFT_BMP_PAYLOAD)];
    PNTFSLX_PHASE1_LOG_RECORD Header;
    PNTFSLX_PHASE1_MFT_BMP_PAYLOAD Payload;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Header = (PNTFSLX_PHASE1_LOG_RECORD)Buffer;
    Payload = (PNTFSLX_PHASE1_MFT_BMP_PAYLOAD)
        (Buffer + sizeof(NTFSLX_PHASE1_LOG_RECORD));

    Header->RecordType = Set ? NTFSLX_PHASE1_OP_MFT_BMP_SET
                              : NTFSLX_PHASE1_OP_MFT_BMP_CLEAR;
    Header->TransactionId = 0;
    Payload->MftIndex = MftIndex;

    return NtfslxJournalAppendRecord(DevExt, Buffer, (ULONG)sizeof(Buffer));
}

NTSTATUS
NtfslxJournalAppendClusterBitmapRange(
    _Inout_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG StartLcn,
    _In_ ULONGLONG Length,
    _In_ BOOLEAN Set)
{
    UCHAR Buffer[sizeof(NTFSLX_PHASE1_LOG_RECORD) +
                 sizeof(NTFSLX_PHASE1_BMP_RANGE_PAYLOAD)];
    PNTFSLX_PHASE1_LOG_RECORD Header;
    PNTFSLX_PHASE1_BMP_RANGE_PAYLOAD Payload;

    if (Length == 0)
        return STATUS_SUCCESS;

    RtlZeroMemory(Buffer, sizeof(Buffer));
    Header = (PNTFSLX_PHASE1_LOG_RECORD)Buffer;
    Payload = (PNTFSLX_PHASE1_BMP_RANGE_PAYLOAD)
        (Buffer + sizeof(NTFSLX_PHASE1_LOG_RECORD));

    Header->RecordType = Set ? NTFSLX_PHASE1_OP_BMP_SET_RANGE
                              : NTFSLX_PHASE1_OP_BMP_CLEAR_RANGE;
    Header->TransactionId = 0;
    Payload->StartLcn = StartLcn;
    Payload->Length = Length;

    return NtfslxJournalAppendRecord(DevExt, Buffer, (ULONG)sizeof(Buffer));
}

/*
 * Append a Phase-1 IndexBlockUpdate redo record carrying the post-mutation
 * bytes of an INDX block. The full block image is recorded so replay can
 * idempotently rewrite the on-disk block. TransactionId may be non-zero to
 * group records that were intended to replay together (e.g. a split that
 * produced a new block plus a parent-block rewrite).
 *
 * BlockData must point to the *post-mutation* in-memory block bytes BEFORE
 * MST fixup is applied — recovery applies fixup on replay.
 *
 * Length must fit within USHORT (Phase-1 record header limit). For 4 KiB
 * INDX blocks the (header + payload-header + 4096) total is well under that.
 */
NTSTATUS
NtfslxJournalAppendIndexBlockUpdate(
    _Inout_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG ParentMftIndex,
    _In_ ULONGLONG Vcn,
    _In_ ULONG TransactionId,
    _In_reads_bytes_(BlockLength) PVOID BlockData,
    _In_ ULONG BlockLength)
{
    PUCHAR Buffer;
    PNTFSLX_PHASE1_LOG_RECORD Header;
    PNTFSLX_PHASE1_INDEX_BLOCK_PAYLOAD Payload;
    ULONG TotalLength;
    ULONG PaddedBlockLength;
    NTSTATUS Status;

    if (BlockData == NULL || BlockLength == 0)
        return STATUS_INVALID_PARAMETER;

    /* Pad block length to 8-byte alignment for record alignment. */
    PaddedBlockLength = (BlockLength + 7) & ~7u;
    TotalLength = (ULONG)sizeof(NTFSLX_PHASE1_LOG_RECORD) +
                  (ULONG)sizeof(NTFSLX_PHASE1_INDEX_BLOCK_PAYLOAD) +
                  PaddedBlockLength;

    if (TotalLength > 0xFFF8u)
    {
        /*
         * Phase-1 record Length is a USHORT; if a single block somehow
         * exceeds ~64 KiB (no NTFS volume uses index blocks anywhere
         * near this), drop the journal append rather than truncate.
         */
        return STATUS_BUFFER_OVERFLOW;
    }

    /* Fast path: skip work when journaling is off so we don't churn pool. */
    if (DevExt == NULL ||
        !DevExt->JournalLockInitialized || !DevExt->JournalEnabled)
    {
        return STATUS_SUCCESS;
    }

    Buffer = ExAllocatePoolWithTag(NonPagedPool, TotalLength, NTFSLX_TAG);
    if (Buffer == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(Buffer, TotalLength);
    Header = (PNTFSLX_PHASE1_LOG_RECORD)Buffer;
    Payload = (PNTFSLX_PHASE1_INDEX_BLOCK_PAYLOAD)
        (Buffer + sizeof(NTFSLX_PHASE1_LOG_RECORD));

    Header->RecordType = NTFSLX_PHASE1_OP_INDEX_BLOCK_UPDATE;
    Header->TransactionId = TransactionId;
    Payload->ParentMftIndex = ParentMftIndex;
    Payload->Vcn = Vcn;
    Payload->BlockLength = BlockLength;
    Payload->Reserved = 0;

    RtlCopyMemory(Buffer +
                      sizeof(NTFSLX_PHASE1_LOG_RECORD) +
                      sizeof(NTFSLX_PHASE1_INDEX_BLOCK_PAYLOAD),
                  BlockData,
                  BlockLength);

    Status = NtfslxJournalAppendRecord(DevExt, Buffer, TotalLength);
    ExFreePoolWithTag(Buffer, NTFSLX_TAG);
    return Status;
}

/*
 * Allocate a fresh non-zero TransactionId. Used by callers that need to
 * group multiple Phase-1 IndexBlockUpdate records that should replay
 * atomically (e.g. an index split that produces two new blocks plus a
 * parent rewrite). TransactionId 0 is reserved for stand-alone records.
 *
 * Returns 0 if journaling is disabled (caller should pass 0 to
 * NtfslxJournalAppendIndexBlockUpdate, which is the "atomic single record"
 * convention anyway).
 */
ULONG
NtfslxJournalAllocateTransactionId(
    _Inout_ PNTFSLX_DEVICE_EXTENSION DevExt)
{
    ULONG Id;

    if (DevExt == NULL ||
        !DevExt->JournalLockInitialized || !DevExt->JournalEnabled)
    {
        return 0;
    }

    KeAcquireGuardedMutex(&DevExt->JournalLock);
    DevExt->JournalTransactionId += 1;
    if (DevExt->JournalTransactionId == 0)
        DevExt->JournalTransactionId = 1;  /* skip the reserved zero */
    Id = DevExt->JournalTransactionId;
    KeReleaseGuardedMutex(&DevExt->JournalLock);
    return Id;
}
