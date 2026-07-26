/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS volume formatter: geometry, layout and orchestration
 */

#include "formatint.h"

/* Bulk writes are done in chunks of at most this size. */
#define FORMAT_TRANSFER_SIZE (256 * 1024)

NTSTATUS
FormatWriteAt(_In_ PFormatContext Ctx,
              _In_ ULONGLONG Offset,
              _In_ ULONG Length,
              _In_ const void* Buffer)
{
    return Ctx->Params->Write(Ctx->Params->IoContext, Offset, Length, Buffer);
}

NTSTATUS
FormatWriteCluster(_In_ PFormatContext Ctx,
                   _In_ ULONGLONG Lcn,
                   _In_ ULONG Length,
                   _In_ const void* Buffer)
{
    return FormatWriteAt(Ctx, Lcn * Ctx->ClusterSize, Length, Buffer);
}

NTSTATUS
FormatFillClusters(_In_ PFormatContext Ctx,
                   _In_ ULONGLONG Lcn,
                   _In_ ULONGLONG ClusterCount,
                   _In_ UCHAR Value)
{
    ULONGLONG Remaining = ClusterCount * Ctx->ClusterSize;
    ULONGLONG Offset = Lcn * Ctx->ClusterSize;
    NTSTATUS Status;

    if (ClusterCount == 0)
        return STATUS_SUCCESS;

    RtlFillMemory(Ctx->TransferBuffer, Ctx->TransferSize, Value);

    while (Remaining > 0)
    {
        ULONG Chunk = Remaining > Ctx->TransferSize ? Ctx->TransferSize
                                                    : (ULONG)Remaining;

        Status = FormatWriteAt(Ctx, Offset, Chunk, Ctx->TransferBuffer);
        if (!NT_SUCCESS(Status))
            return Status;

        Offset += Chunk;
        Remaining -= Chunk;
    }

    return STATUS_SUCCESS;
}

/*
 * Number of bytes needed to hold Value in a mapping pairs field.
 *
 * Both fields are read back sign-extended from their most significant byte
 * (libntfs does `deltaxcn = (s8)buf[b--]`), so a value whose top bit lands on
 * a byte boundary needs one more byte or it decodes as negative and the whole
 * runlist is rejected.
 */
static UCHAR
FormatSignificantBytes(_In_ ULONGLONG Value)
{
    UCHAR Count = 0;
    ULONGLONG Rest = Value;

    do
    {
        Rest >>= 8;
        Count++;
    } while (Rest != 0);

    if ((Value >> ((Count * 8) - 1)) & 1)
        Count++;

    return Count;
}

/*
 * Encodes a single extent as an NTFS mapping pairs array. A sparse run omits
 * the LCN field entirely, which is what makes the extent a hole.
 */
ULONG
FormatEncodeDataRuns(_Out_ PUCHAR Buffer,
                     _In_ ULONG BufferLength,
                     _In_ ULONGLONG StartLcn,
                     _In_ ULONGLONG ClusterCount,
                     _In_ BOOLEAN Sparse)
{
    UCHAR LengthBytes;
    UCHAR LcnBytes = 0;
    ULONG Offset = 0;
    ULONG Index;

    /* A zero-length attribute has no runs at all, just the terminator. */
    if (ClusterCount == 0)
    {
        if (BufferLength < 1)
            return 0;
        Buffer[0] = 0;
        return 1;
    }

    LengthBytes = FormatSignificantBytes(ClusterCount);

    /* The LCN is a signed delta from the previous run; for the first run the
     * previous LCN is 0, so StartLcn is encoded as-is. */
    if (!Sparse)
        LcnBytes = FormatSignificantBytes(StartLcn);

    if (BufferLength < (ULONG)(1 + LengthBytes + LcnBytes + 1))
        return 0;

    Buffer[Offset++] = (UCHAR)(LengthBytes | (LcnBytes << 4));

    for (Index = 0; Index < LengthBytes; Index++)
        Buffer[Offset++] = (UCHAR)(ClusterCount >> (Index * 8));

    for (Index = 0; Index < LcnBytes; Index++)
        Buffer[Offset++] = (UCHAR)(StartLcn >> (Index * 8));

    Buffer[Offset++] = 0;

    return Offset;
}

static ULONG
FormatChooseSectorsPerCluster(_In_ ULONGLONG VolumeSize,
                              _In_ ULONG BytesPerSector)
{
    ULONG ClusterSize;

    /* Matches the sizes Windows picks by default. */
    if (VolumeSize <= 512ULL * 1024 * 1024)
        ClusterSize = 512;
    else if (VolumeSize <= 1024ULL * 1024 * 1024)
        ClusterSize = 1024;
    else if (VolumeSize <= 2048ULL * 1024 * 1024)
        ClusterSize = 2048;
    else
        ClusterSize = 4096;

    if (ClusterSize < BytesPerSector)
        ClusterSize = BytesPerSector;

    return ClusterSize / BytesPerSector;
}

static ULONGLONG
FormatChooseLogFileSize(_In_ ULONGLONG VolumeSize)
{
    ULONGLONG Size;

    if (VolumeSize <= 8ULL * 1024 * 1024)
        Size = 256 * 1024;
    else if (VolumeSize <= 512ULL * 1024 * 1024)
        Size = 1024 * 1024;
    else if (VolumeSize <= 1024ULL * 1024 * 1024)
        Size = 2 * 1024ULL * 1024;
    else if (VolumeSize <= 2048ULL * 1024 * 1024)
        Size = 4 * 1024ULL * 1024;
    else
        Size = 64 * 1024ULL * 1024;

    /* Never let the journal dominate a small volume. */
    if (Size > VolumeSize / 16)
        Size = VolumeSize / 16;

    if (Size < 256 * 1024)
        Size = 256 * 1024;

    return ALIGN_UP_BY(Size, NTFS_LOG_PAGE_SIZE);
}

static ULONGLONG
FormatClustersFor(_In_ PFormatContext Ctx,
                  _In_ ULONGLONG Bytes)
{
    return (Bytes + Ctx->ClusterSize - 1) / Ctx->ClusterSize;
}

static ULONGLONG
FormatAllocate(_Inout_ ULONGLONG* NextLcn,
               _In_ ULONGLONG ClusterCount,
               _Out_ ULONGLONG* OutClusters)
{
    ULONGLONG Lcn = *NextLcn;

    *OutClusters = ClusterCount;
    *NextLcn = Lcn + ClusterCount;

    return Lcn;
}

static NTSTATUS
FormatComputeLayout(_In_ PFormatContext Ctx)
{
    const NtfsFormatParameters* Params = Ctx->Params;
    ULONGLONG VolumeSize;
    ULONGLONG NextLcn;

    Ctx->BytesPerSector = Params->BytesPerSector;
    if (Ctx->BytesPerSector < 512 ||
        (Ctx->BytesPerSector & (Ctx->BytesPerSector - 1)) != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* NTFS reserves the final sector of the partition for the backup boot
     * sector, so it is not part of the addressable cluster space. */
    if (Params->TotalSectors < 2)
        return STATUS_INVALID_PARAMETER;

    Ctx->UsableSectors = Params->TotalSectors - 1;
    VolumeSize = Ctx->UsableSectors * Ctx->BytesPerSector;

    if (VolumeSize < NTFS_FORMAT_MINIMUM_VOLUME_SIZE)
        return STATUS_INVALID_PARAMETER;

    Ctx->SectorsPerCluster = Params->SectorsPerCluster;
    if (Ctx->SectorsPerCluster == 0)
    {
        Ctx->SectorsPerCluster =
            FormatChooseSectorsPerCluster(VolumeSize, Ctx->BytesPerSector);
    }

    if (Ctx->SectorsPerCluster == 0 ||
        (Ctx->SectorsPerCluster & (Ctx->SectorsPerCluster - 1)) != 0 ||
        Ctx->SectorsPerCluster > 128)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Ctx->ClusterSize = Ctx->SectorsPerCluster * Ctx->BytesPerSector;
    Ctx->TotalClusters = Ctx->UsableSectors / Ctx->SectorsPerCluster;

    Ctx->MftRecordSize = Params->MftRecordSize
                             ? Params->MftRecordSize
                             : NTFS_FORMAT_DEFAULT_MFT_RECORD_SIZE;
    Ctx->IndexRecordSize = Params->IndexRecordSize
                               ? Params->IndexRecordSize
                               : NTFS_FORMAT_DEFAULT_INDEX_RECORD_SIZE;

    if (Ctx->MftRecordSize < Ctx->BytesPerSector ||
        (Ctx->MftRecordSize & (Ctx->MftRecordSize - 1)) != 0 ||
        Ctx->MftRecordSize > 65536)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Ctx->IndexRecordSize < Ctx->BytesPerSector ||
        (Ctx->IndexRecordSize & (Ctx->IndexRecordSize - 1)) != 0 ||
        Ctx->IndexRecordSize > 65536)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Ctx->MftRecordCount = NTFS_FORMAT_INITIAL_RECORDS;
    Ctx->MftDataSize = (ULONGLONG)Ctx->MftRecordCount * Ctx->MftRecordSize;

    /* One bit per MFT record, rounded to the 8 byte granularity NTFS uses
     * for bitmap attribute sizes. */
    Ctx->MftBitmapDataSize = ALIGN_UP_BY((Ctx->MftRecordCount + 7) / 8, 8);

    /* One bit per cluster, likewise rounded to 8 bytes. */
    Ctx->BitmapDataSize = ALIGN_UP_BY((Ctx->TotalClusters + 7) / 8, 8);

    Ctx->LogFileSize = FormatChooseLogFileSize(VolumeSize);

    Ctx->CurrentTime = Params->CurrentTime;

    Ctx->SerialNumber = Params->SerialNumber;
    if (Ctx->SerialNumber == 0)
    {
        /* Fold the timestamp so that two volumes formatted in the same tick
         * still differ in the low bits that tools display. */
        Ctx->SerialNumber = Ctx->CurrentTime ^
                            (Ctx->CurrentTime << 31) ^
                            Ctx->TotalClusters;
    }

    /*
     * Head region: $Boot must start at cluster 0 because it maps the boot
     * area, everything else simply follows in allocation order.
     */
    NextLcn = 0;
    Ctx->BootLcn = FormatAllocate(&NextLcn,
                                  FormatClustersFor(Ctx, NTFS_BOOT_AREA_SIZE),
                                  &Ctx->BootClusters);
    Ctx->MftLcn = FormatAllocate(&NextLcn,
                                 FormatClustersFor(Ctx, Ctx->MftDataSize),
                                 &Ctx->MftClusters);
    Ctx->MftBitmapLcn = FormatAllocate(&NextLcn,
                                       FormatClustersFor(Ctx, Ctx->MftBitmapDataSize),
                                       &Ctx->MftBitmapClusters);
    Ctx->LogFileLcn = FormatAllocate(&NextLcn,
                                     FormatClustersFor(Ctx, Ctx->LogFileSize),
                                     &Ctx->LogFileClusters);
    Ctx->BitmapLcn = FormatAllocate(&NextLcn,
                                    FormatClustersFor(Ctx, Ctx->BitmapDataSize),
                                    &Ctx->BitmapClusters);
    Ctx->UpCaseLcn = FormatAllocate(&NextLcn,
                                    FormatClustersFor(Ctx, NTFS_UPCASE_SIZE),
                                    &Ctx->UpCaseClusters);
    Ctx->AttrDefLcn = FormatAllocate(&NextLcn,
                                     FormatClustersFor(Ctx, NTFS_ATTRDEF_SIZE),
                                     &Ctx->AttrDefClusters);
    /* One index block holds the root directory's system-file entries. */
    Ctx->RootIndexLcn = FormatAllocate(&NextLcn,
                                       FormatClustersFor(Ctx, Ctx->IndexRecordSize),
                                       &Ctx->RootIndexClusters);
    Ctx->HeadEndLcn = NextLcn;

    /*
     * Put the mirror near the middle of the volume so that it survives damage
     * to the head. On a volume too small for that it just follows the head.
     */
    Ctx->MftMirrClusters =
        FormatClustersFor(Ctx, 4ULL * Ctx->MftRecordSize);
    Ctx->MftMirrLcn = Ctx->TotalClusters / 2;
    if (Ctx->MftMirrLcn < Ctx->HeadEndLcn)
        Ctx->MftMirrLcn = Ctx->HeadEndLcn;

    if (Ctx->MftMirrLcn + Ctx->MftMirrClusters > Ctx->TotalClusters)
        return STATUS_INVALID_PARAMETER;

    return STATUS_SUCCESS;
}

static BOOLEAN
FormatReportProgress(_In_ PFormatContext Ctx,
                     _In_ ULONG Percent)
{
    if (!Ctx->Params->Progress)
        return TRUE;

    return Ctx->Params->Progress(Ctx->Params->IoContext, Percent);
}

/*
 * Zeroes every cluster that is not part of the metadata written above. Only
 * done for a non-quick format; the volume is already valid without it.
 */
static NTSTATUS
FormatZeroFreeSpace(_In_ PFormatContext Ctx)
{
    ULONGLONG Lcn = Ctx->HeadEndLcn;
    NTSTATUS Status;

    while (Lcn < Ctx->TotalClusters)
    {
        ULONGLONG RunEnd = Ctx->TotalClusters;
        ULONGLONG Percent;

        if (Lcn >= Ctx->MftMirrLcn &&
            Lcn < Ctx->MftMirrLcn + Ctx->MftMirrClusters)
        {
            Lcn = Ctx->MftMirrLcn + Ctx->MftMirrClusters;
            continue;
        }

        if (Ctx->MftMirrLcn > Lcn && Ctx->MftMirrLcn < RunEnd)
            RunEnd = Ctx->MftMirrLcn;

        Status = FormatFillClusters(Ctx, Lcn, RunEnd - Lcn, 0);
        if (!NT_SUCCESS(Status))
            return Status;

        Lcn = RunEnd;

        Percent = 50 + (Lcn * 50) / Ctx->TotalClusters;
        if (!FormatReportProgress(Ctx, (ULONG)Percent))
            return STATUS_CANCELLED;
    }

    return STATUS_SUCCESS;
}

EXTERN_C
NTSTATUS
NtfsVolumeFormat(_In_ const NtfsFormatParameters* Parameters)
{
    FormatContext Ctx;
    NTSTATUS Status;

    if (!Parameters || !Parameters->Write || !Parameters->Allocate ||
        !Parameters->Free)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&Ctx, sizeof(Ctx));
    Ctx.Params = Parameters;

    Status = FormatComputeLayout(&Ctx);
    if (!NT_SUCCESS(Status))
        return Status;

    Ctx.TransferSize = FORMAT_TRANSFER_SIZE;
    if (Ctx.TransferSize < Ctx.ClusterSize)
        Ctx.TransferSize = Ctx.ClusterSize;

    Ctx.RecordBuffer = (PUCHAR)Parameters->Allocate(Parameters->IoContext,
                                                    Ctx.MftRecordSize);
    Ctx.TransferBuffer = (PUCHAR)Parameters->Allocate(Parameters->IoContext,
                                                      Ctx.TransferSize);
    if (!Ctx.RecordBuffer || !Ctx.TransferBuffer)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    if (!FormatReportProgress(&Ctx, 0))
    {
        Status = STATUS_CANCELLED;
        goto Cleanup;
    }

    Status = FormatWriteMetadata(&Ctx);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = FormatWriteBootSectors(&Ctx);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (!FormatReportProgress(&Ctx, 50))
    {
        Status = STATUS_CANCELLED;
        goto Cleanup;
    }

    if (!Parameters->QuickFormat)
    {
        Status = FormatZeroFreeSpace(&Ctx);
        if (!NT_SUCCESS(Status))
            goto Cleanup;
    }

    FormatReportProgress(&Ctx, 100);
    Status = STATUS_SUCCESS;

Cleanup:
    if (Ctx.RecordBuffer)
        Parameters->Free(Parameters->IoContext, Ctx.RecordBuffer);
    if (Ctx.TransferBuffer)
        Parameters->Free(Parameters->IoContext, Ctx.TransferBuffer);

    return Status;
}
