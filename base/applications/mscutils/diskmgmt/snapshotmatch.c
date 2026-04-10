/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Volume-to-region matching helpers.
 */

#include "snapshotmatch.h"

#include <strsafe.h>

static VOID
DmSnapshotBindVolumeRegion(
    _Inout_ PDM_DISK Disk,
    _Inout_ PDM_REGION Region,
    _Inout_ PDM_VOLUME Volume)
{
    if (Volume->Disk == NULL)
        Volume->Disk = Disk;
    if (Volume->Region == NULL)
        Volume->Region = Region;
    Volume->IsDynamic = (Volume->IsDynamic || Region->IsDynamic);

    Region->Volume = Volume;
    Region->DriveLetter = Volume->HasDriveLetter ? Volume->DriveLetter : UNICODE_NULL;
    StringCchCopyW(Region->Label,
                   sizeof(Region->Label) / sizeof(Region->Label[0]),
                   Volume->Label);
    StringCchCopyW(Region->FileSystem,
                   sizeof(Region->FileSystem) / sizeof(Region->FileSystem[0]),
                   Volume->FileSystem);
    Region->IsBoot = Volume->IsBoot;
    Region->IsSystem = Volume->IsSystem;

    Disk->IsBoot = Volume->IsBoot ? TRUE : Disk->IsBoot;
    Disk->IsSystem = Volume->IsSystem ? TRUE : Disk->IsSystem;
}

static BOOLEAN
DmSnapshotMatchExtentToRegion(
    _In_ const DM_REGION *Region,
    _In_ const DISK_EXTENT *Extent)
{
    if (Region == NULL || Extent == NULL)
        return FALSE;

    return (Region->Type == DmRegionPartition &&
            Region->StartOffset == Extent->StartingOffset.QuadPart &&
            Region->Length == Extent->ExtentLength.QuadPart);
}

static BOOLEAN
DmSnapshotMatchVolumeFallback(
    _In_ const DM_VOLUME *Volume,
    _In_ const DM_DISK *Disk,
    _In_ const DM_REGION *Region)
{
    if (Volume == NULL || Disk == NULL || Region == NULL)
        return FALSE;

    if (!Volume->HasStorageDeviceNumber)
        return FALSE;

    return (Region->Type == DmRegionPartition &&
            Disk->DiskNumber == Volume->StorageDiskNumber &&
            Region->PartitionNumber == Volume->StoragePartitionNumber);
}

VOID
DmSnapshotMatchVolumesToRegions(
    _Inout_ PDM_SNAPSHOT Snapshot)
{
    ULONG VolumeIndex;
    ULONG DiskIndex;
    ULONG RegionIndex;
    ULONG ExtentIndex;
    PDM_VOLUME Volume;
    PDM_DISK Disk;
    PDM_REGION Region;
    BOOLEAN MatchedAny;
    BOOLEAN MatchedExtent;

    if (Snapshot == NULL)
        return;

    for (VolumeIndex = 0; VolumeIndex < Snapshot->VolumeCount; VolumeIndex++)
    {
        Volume = &Snapshot->Volumes[VolumeIndex];
        if (Volume->Extents == NULL && !Volume->HasStorageDeviceNumber)
            continue;

        MatchedAny = FALSE;
        for (ExtentIndex = 0;
             Volume->Extents != NULL &&
             ExtentIndex < Volume->Extents->NumberOfDiskExtents;
             ExtentIndex++)
        {
            MatchedExtent = FALSE;
            for (DiskIndex = 0; DiskIndex < Snapshot->DiskCount && !MatchedExtent; DiskIndex++)
            {
                Disk = &Snapshot->Disks[DiskIndex];
                if (Disk->DiskNumber != Volume->Extents->Extents[ExtentIndex].DiskNumber)
                    continue;

                for (RegionIndex = 0; RegionIndex < Disk->RegionCount; RegionIndex++)
                {
                    Region = &Disk->Regions[RegionIndex];
                    if (Region->IsHidden)
                        continue;
                    if (DmSnapshotMatchExtentToRegion(Region,
                                                      &Volume->Extents->Extents[ExtentIndex]))
                    {
                        DmSnapshotBindVolumeRegion(Disk, Region, Volume);
                        MatchedAny = TRUE;
                        MatchedExtent = TRUE;
                        break;
                    }
                }
            }
        }

        for (DiskIndex = 0; DiskIndex < Snapshot->DiskCount && !MatchedAny; DiskIndex++)
        {
            Disk = &Snapshot->Disks[DiskIndex];
            for (RegionIndex = 0; RegionIndex < Disk->RegionCount; RegionIndex++)
            {
                Region = &Disk->Regions[RegionIndex];
                if (Region->IsHidden)
                    continue;
                if (DmSnapshotMatchVolumeFallback(Volume, Disk, Region))
                {
                    DmSnapshotBindVolumeRegion(Disk, Region, Volume);
                    MatchedAny = TRUE;
                    break;
                }
            }
        }
    }
}
