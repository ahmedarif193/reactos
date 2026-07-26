/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS volume formatter: boot sector and boot area
 */

#include "formatint.h"

#define NTFS_BOOT_SIGNATURE 0xAA55
#define NTFS_DEFAULT_MEDIA_DESCRIPTOR 0xF8

static INT8
FormatEncodeRecordSize(_In_ ULONG Size,
                       _In_ ULONG ClusterSize)
{
    INT8 Log;

    if (Size >= ClusterSize)
        return (INT8)(Size / ClusterSize);

    for (Log = 0; Log < 32; Log++)
    {
        if ((1UL << Log) == Size)
            return (INT8)(-Log);
    }

    return 0;
}

/*
 * Builds the BPB. The bootstrap area is intentionally left zeroed: on ReactOS
 * the boot code comes from loader\ntfs.bin and is installed by
 * InstallNtfsBootCode(), which copies the BPB written here into it.
 */
static void
FormatBuildBootSector(_In_ PFormatContext Ctx,
                      _Out_ PBootSector Boot)
{
    RtlZeroMemory(Boot, sizeof(*Boot));

    /* Skip over the BPB, matching what real NTFS boot code does. */
    Boot->JumpInstruction[0] = 0xEB;
    Boot->JumpInstruction[1] = 0x52;
    Boot->JumpInstruction[2] = 0x90;

    Boot->OEM_ID[0] = 'N';
    Boot->OEM_ID[1] = 'T';
    Boot->OEM_ID[2] = 'F';
    Boot->OEM_ID[3] = 'S';
    Boot->OEM_ID[4] = ' ';
    Boot->OEM_ID[5] = ' ';
    Boot->OEM_ID[6] = ' ';
    Boot->OEM_ID[7] = ' ';

    Boot->BytesPerSector = (UINT16)Ctx->BytesPerSector;
    Boot->SectorsPerCluster = (UINT8)Ctx->SectorsPerCluster;

    Boot->MediaDescriptor = Ctx->Params->MediaDescriptor
                                ? Ctx->Params->MediaDescriptor
                                : NTFS_DEFAULT_MEDIA_DESCRIPTOR;
    Boot->SectorsPerTrack = Ctx->Params->SectorsPerTrack;
    Boot->NumberOfHeads = Ctx->Params->NumberOfHeads;
    Boot->HiddenSectors = Ctx->Params->HiddenSectors;

    /* Windows writes 0x00800080 here; it is not interpreted. */
    Boot->Unknown = 0x00800080;

    Boot->SectorsInVolume = Ctx->UsableSectors;
    Boot->MFTLCN = Ctx->MftLcn;
    Boot->MFTMirrLCN = Ctx->MftMirrLcn;

    Boot->ClustersPerFileRecord =
        FormatEncodeRecordSize(Ctx->MftRecordSize, Ctx->ClusterSize);
    Boot->ClustersPerIndexRecord =
        FormatEncodeRecordSize(Ctx->IndexRecordSize, Ctx->ClusterSize);

    Boot->SerialNumber = Ctx->SerialNumber;

    /* Windows leaves the checksum zero and does not verify it. */
    Boot->Checksum = 0;

    Boot->EndSector = NTFS_BOOT_SIGNATURE;
}

NTSTATUS
FormatWriteBootSectors(_In_ PFormatContext Ctx)
{
    PUCHAR BootArea = Ctx->TransferBuffer;
    ULONG BootAreaSize = (ULONG)(Ctx->BootClusters * Ctx->ClusterSize);
    NTSTATUS Status;

    if (BootAreaSize < NTFS_BOOT_AREA_SIZE)
        BootAreaSize = NTFS_BOOT_AREA_SIZE;

    if (BootAreaSize > Ctx->TransferSize)
        return STATUS_INSUFFICIENT_RESOURCES;

    /*
     * Sector 0 holds the BPB, the rest of the 8 KB boot area is reserved for
     * boot code and stays zeroed here.
     */
    RtlZeroMemory(BootArea, BootAreaSize);
    FormatBuildBootSector(Ctx, (PBootSector)BootArea);

    Status = FormatWriteAt(Ctx, 0, BootAreaSize, BootArea);
    if (!NT_SUCCESS(Status))
        return Status;

    /*
     * The backup lives in the last sector of the partition, which is the one
     * sector deliberately left outside the cluster space.
     */
    return FormatWriteAt(Ctx,
                         Ctx->UsableSectors * Ctx->BytesPerSector,
                         Ctx->BytesPerSector,
                         BootArea);
}
