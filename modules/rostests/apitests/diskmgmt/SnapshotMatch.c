/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Tests for diskmgmt snapshot matching helpers.
 */

#include <apitest.h>
#include <strsafe.h>

#include "snapshot.h"
#include "snapshotmatch.h"

typedef struct _TEST_VOLUME_DISK_EXTENTS_2
{
    VOLUME_DISK_EXTENTS Buffer;
    DISK_EXTENT Extra[1];
} TEST_VOLUME_DISK_EXTENTS_2;

static void
TestExactMatchCopiesMetadata(void)
{
    DM_SNAPSHOT Snapshot;
    DM_DISK Disk;
    DM_REGION Region;
    DM_VOLUME Volume;
    VOLUME_DISK_EXTENTS Extents;

    ZeroMemory(&Snapshot, sizeof(Snapshot));
    ZeroMemory(&Disk, sizeof(Disk));
    ZeroMemory(&Region, sizeof(Region));
    ZeroMemory(&Volume, sizeof(Volume));
    ZeroMemory(&Extents, sizeof(Extents));

    Snapshot.Disks = &Disk;
    Snapshot.DiskCount = 1;
    Snapshot.Volumes = &Volume;
    Snapshot.VolumeCount = 1;

    Disk.DiskNumber = 0;
    Disk.Regions = &Region;
    Disk.RegionCount = 1;

    Region.Type = DmRegionPartition;
    Region.PartitionStyle = PARTITION_STYLE_GPT;
    Region.StartOffset = 1024;
    Region.Length = 4096;

    Volume.Extents = &Extents;
    Volume.ExtentCount = 1;
    Volume.HasDriveLetter = TRUE;
    Volume.DriveLetter = L'D';
    Volume.IsSystem = TRUE;
    StringCchCopyW(Volume.Label, sizeof(Volume.Label) / sizeof(Volume.Label[0]), L"Data");
    StringCchCopyW(Volume.FileSystem, sizeof(Volume.FileSystem) / sizeof(Volume.FileSystem[0]), L"NTFS");

    Extents.NumberOfDiskExtents = 1;
    Extents.Extents[0].DiskNumber = 0;
    Extents.Extents[0].StartingOffset.QuadPart = 1024;
    Extents.Extents[0].ExtentLength.QuadPart = 4096;

    DmSnapshotMatchVolumesToRegions(&Snapshot);

    ok(Volume.Disk == &Disk, "Volume should bind to the matching disk\n");
    ok(Volume.Region == &Region, "Volume should bind to the matching region\n");
    ok(Region.Volume == &Volume, "Region should bind back to the volume\n");
    ok(Region.DriveLetter == L'D', "Expected drive letter D, got 0x%04x\n", Region.DriveLetter);
    ok(lstrcmpW(Region.Label, L"Data") == 0, "Unexpected label copy: %S\n", Region.Label);
    ok(lstrcmpW(Region.FileSystem, L"NTFS") == 0, "Unexpected filesystem copy: %S\n", Region.FileSystem);
    ok(Region.IsSystem == TRUE, "Region should inherit the system flag\n");
    ok(Disk.IsSystem == TRUE, "Disk should inherit the system flag\n");
}

static void
TestLaterExtentCanMatchAndPropagateDynamic(void)
{
    DM_SNAPSHOT Snapshot;
    DM_DISK Disks[2];
    DM_REGION Regions[2];
    DM_VOLUME Volume;
    TEST_VOLUME_DISK_EXTENTS_2 Extents;

    ZeroMemory(&Snapshot, sizeof(Snapshot));
    ZeroMemory(Disks, sizeof(Disks));
    ZeroMemory(Regions, sizeof(Regions));
    ZeroMemory(&Volume, sizeof(Volume));
    ZeroMemory(&Extents, sizeof(Extents));

    Snapshot.Disks = Disks;
    Snapshot.DiskCount = sizeof(Disks) / sizeof(Disks[0]);
    Snapshot.Volumes = &Volume;
    Snapshot.VolumeCount = 1;

    Disks[0].DiskNumber = 0;
    Disks[0].Regions = &Regions[0];
    Disks[0].RegionCount = 1;
    Regions[0].Type = DmRegionPartition;
    Regions[0].StartOffset = 100;
    Regions[0].Length = 200;

    Disks[1].DiskNumber = 5;
    Disks[1].IsDynamic = TRUE;
    Disks[1].Regions = &Regions[1];
    Disks[1].RegionCount = 1;
    Regions[1].Type = DmRegionPartition;
    Regions[1].StartOffset = 8192;
    Regions[1].Length = 16384;

    Volume.Extents = &Extents.Buffer;
    Volume.ExtentCount = 2;

    Extents.Buffer.NumberOfDiskExtents = 2;
    Extents.Buffer.Extents[0].DiskNumber = 9;
    Extents.Buffer.Extents[0].StartingOffset.QuadPart = 1;
    Extents.Buffer.Extents[0].ExtentLength.QuadPart = 2;
    Extents.Extra[0].DiskNumber = 5;
    Extents.Extra[0].StartingOffset.QuadPart = 8192;
    Extents.Extra[0].ExtentLength.QuadPart = 16384;

    DmSnapshotMatchVolumesToRegions(&Snapshot);

    ok(Volume.Disk == &Disks[1], "Volume should match the later extent's disk\n");
    ok(Volume.Region == &Regions[1], "Volume should match the later extent's region\n");
    ok(Volume.IsDynamic == TRUE, "Volume should inherit the dynamic flag from the disk\n");
}

static void
TestNoMatchLeavesPointersClear(void)
{
    DM_SNAPSHOT Snapshot;
    DM_DISK Disk;
    DM_REGION Region;
    DM_VOLUME Volume;
    VOLUME_DISK_EXTENTS Extents;

    ZeroMemory(&Snapshot, sizeof(Snapshot));
    ZeroMemory(&Disk, sizeof(Disk));
    ZeroMemory(&Region, sizeof(Region));
    ZeroMemory(&Volume, sizeof(Volume));
    ZeroMemory(&Extents, sizeof(Extents));

    Snapshot.Disks = &Disk;
    Snapshot.DiskCount = 1;
    Snapshot.Volumes = &Volume;
    Snapshot.VolumeCount = 1;

    Disk.DiskNumber = 2;
    Disk.Regions = &Region;
    Disk.RegionCount = 1;

    Region.Type = DmRegionPartition;
    Region.StartOffset = 2048;
    Region.Length = 4096;

    Volume.Extents = &Extents;
    Volume.ExtentCount = 1;
    Extents.NumberOfDiskExtents = 1;
    Extents.Extents[0].DiskNumber = 3;
    Extents.Extents[0].StartingOffset.QuadPart = 2048;
    Extents.Extents[0].ExtentLength.QuadPart = 4096;

    DmSnapshotMatchVolumesToRegions(&Snapshot);

    ok(Volume.Disk == NULL, "Volume should stay unmatched\n");
    ok(Volume.Region == NULL, "Volume region should stay clear\n");
    ok(Region.Volume == NULL, "Region should stay unmatched\n");
}

static void
TestStorageDeviceNumberFallbackMatchesPartition(void)
{
    DM_SNAPSHOT Snapshot;
    DM_DISK Disk;
    DM_REGION Regions[2];
    DM_VOLUME Volume;

    ZeroMemory(&Snapshot, sizeof(Snapshot));
    ZeroMemory(&Disk, sizeof(Disk));
    ZeroMemory(Regions, sizeof(Regions));
    ZeroMemory(&Volume, sizeof(Volume));

    Snapshot.Disks = &Disk;
    Snapshot.DiskCount = 1;
    Snapshot.Volumes = &Volume;
    Snapshot.VolumeCount = 1;

    Disk.DiskNumber = 4;
    Disk.Regions = Regions;
    Disk.RegionCount = ARRAYSIZE(Regions);

    Regions[0].Type = DmRegionPartition;
    Regions[0].PartitionNumber = 1;
    Regions[0].StartOffset = 1024;
    Regions[0].Length = 2048;

    Regions[1].Type = DmRegionPartition;
    Regions[1].PartitionNumber = 7;
    Regions[1].StartOffset = 4096;
    Regions[1].Length = 8192;

    Volume.HasStorageDeviceNumber = TRUE;
    Volume.StorageDiskNumber = 4;
    Volume.StoragePartitionNumber = 7;

    DmSnapshotMatchVolumesToRegions(&Snapshot);

    ok(Volume.Disk == &Disk, "Volume should bind to the fallback disk\n");
    ok(Volume.Region == &Regions[1], "Volume should bind to the fallback partition number\n");
    ok(Regions[1].Volume == &Volume, "Fallback region should bind back to the volume\n");
}

START_TEST(SnapshotMatch)
{
    TestExactMatchCopiesMetadata();
    TestLaterExtentCanMatchAndPropagateDynamic();
    TestNoMatchLeavesPointersClear();
    TestStorageDeviceNumberFallbackMatchesPartition();
}
