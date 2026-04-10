/*
 * PROJECT:     ReactOS Disk Management
 * LICENSE:     GPL - See COPYING in the top level directory
 * FILE:        base/applications/mscutils/diskmgmt/snapshot.h
 * PURPOSE:     Read-only storage snapshot helpers for the Disk Management UI
 */

#ifndef DISKMGMT_SNAPSHOT_H
#define DISKMGMT_SNAPSHOT_H

#include <windef.h>
#include <winbase.h>
#include <winnls.h>
#include <winreg.h>
#include <winioctl.h>
#include <ntsecapi.h>
#include <mountmgr.h>
#include <mountdev.h>
#include <ntddstor.h>
#include <guiddef.h>

#ifndef NT_SUCCESS
#define NT_SUCCESS(Status) (((NTSTATUS)(Status)) >= 0)
#endif

#ifndef STATUS_SUCCESS
#define STATUS_SUCCESS ((NTSTATUS)0x00000000L)
#endif

#ifndef STATUS_UNSUCCESSFUL
#define STATUS_UNSUCCESSFUL ((NTSTATUS)0xC0000001L)
#endif

#ifndef STATUS_INVALID_PARAMETER
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000DL)
#endif

#ifndef STATUS_NO_MEMORY
#define STATUS_NO_MEMORY ((NTSTATUS)0xC0000017L)
#endif

#ifndef STATUS_BUFFER_TOO_SMALL
#define STATUS_BUFFER_TOO_SMALL ((NTSTATUS)0xC0000023L)
#endif

#ifndef DISK_ATTRIBUTE_OFFLINE
#define IOCTL_DISK_GET_DISK_ATTRIBUTES \
  CTL_CODE(IOCTL_DISK_BASE, 0x003c, METHOD_BUFFERED, FILE_ANY_ACCESS)
#define IOCTL_DISK_SET_DISK_ATTRIBUTES \
  CTL_CODE(IOCTL_DISK_BASE, 0x003d, METHOD_BUFFERED, FILE_READ_ACCESS | FILE_WRITE_ACCESS)
#define DISK_ATTRIBUTE_OFFLINE              0x0000000000000001ULL
#define DISK_ATTRIBUTE_READ_ONLY            0x0000000000000002ULL
#define DISK_ATTRIBUTE_HIDDEN               0x0000000000000004ULL
#define DISK_ATTRIBUTE_MAINTENANCE          0x0000000000000008ULL
#define DISK_ATTRIBUTE_SPACES_BYPASS        0x0000000000000010ULL

typedef struct _GET_DISK_ATTRIBUTES
{
    ULONG Version;
    ULONG Reserved1;
    ULONGLONG Attributes;
} GET_DISK_ATTRIBUTES, *PGET_DISK_ATTRIBUTES;

typedef struct _SET_DISK_ATTRIBUTES
{
    ULONG Version;
    BOOLEAN Persist;
    BOOLEAN RelinquishOwnership;
    BOOLEAN Reserved1[2];
    ULONGLONG Attributes;
    ULONGLONG AttributesMask;
    GUID Owner;
} SET_DISK_ATTRIBUTES, *PSET_DISK_ATTRIBUTES;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define DM_MAX_STRING  260
#define DM_MAX_MOUNT_PATHS 8
#define DM_MAX_UNIQUE_ID_BYTES 64

typedef enum _DM_REGION_TYPE
{
    DmRegionFree = 0,
    DmRegionPartition = 1
} DM_REGION_TYPE;

typedef struct _DM_SNAPSHOT DM_SNAPSHOT;
typedef struct _DM_DISK DM_DISK;
typedef struct _DM_REGION DM_REGION;
typedef struct _DM_VOLUME DM_VOLUME;

typedef struct _DM_VOLUME
{
    ULONG VolumeNumber;
    WCHAR VolumeName[MAX_PATH];
    WCHAR DeviceName[MAX_PATH];
    WCHAR MountPoint[MAX_PATH];
    WCHAR FolderMountPoint[MAX_PATH];
    WCHAR FolderMountPoints[DM_MAX_MOUNT_PATHS][MAX_PATH];
    WCHAR Label[MAX_PATH];
    WCHAR FileSystem[MAX_PATH];
    WCHAR DriveLetter;
    BOOLEAN HasDriveLetter;
    ULONG FolderMountPointCount;

    ULARGE_INTEGER Size;
    ULARGE_INTEGER FreeBytes;
    ULARGE_INTEGER TotalAllocationUnits;
    ULARGE_INTEGER AvailableAllocationUnits;
    ULONG SectorsPerAllocationUnit;
    ULONG BytesPerSector;
    ULONG SerialNumber;
    ULONG StorageDiskNumber;
    ULONG StoragePartitionNumber;
    BOOLEAN HasStorageDeviceNumber;
    UCHAR UniqueId[DM_MAX_UNIQUE_ID_BYTES];
    ULONG UniqueIdLength;
    BOOLEAN HasUniqueId;
    BOOLEAN HasDmioUniqueId;

    PVOLUME_DISK_EXTENTS Extents;
    ULONG ExtentCount;
    BOOLEAN IsMultiExtent;

    BOOLEAN IsSystem;
    BOOLEAN IsBoot;
    BOOLEAN IsDynamic;

    DM_DISK *Disk;
    DM_REGION *Region;
} DM_VOLUME, *PDM_VOLUME;

typedef struct _DM_REGION
{
    DM_REGION_TYPE Type;
    PARTITION_STYLE PartitionStyle;
    ULONGLONG StartOffset;
    ULONGLONG Length;
    ULONG DiskNumber;
    ULONG PartitionNumber;
    ULONG PartitionIndex;
    BOOLEAN IsLogical;
    BOOLEAN IsContainer;
    BOOLEAN IsBoot;
    BOOLEAN IsSystem;
    BOOLEAN IsDynamic;
    BOOLEAN IsHidden;
    WCHAR DriveLetter;
    WCHAR Label[MAX_PATH];
    WCHAR FileSystem[MAX_PATH];

    union
    {
        struct
        {
            BOOLEAN BootIndicator;
            UCHAR PartitionType;
        } Mbr;

        struct
        {
            GUID PartitionType;
            GUID PartitionId;
            DWORD64 Attributes;
        } Gpt;
    } Data;

    DM_VOLUME *Volume;
} DM_REGION, *PDM_REGION;

typedef struct _DM_DISK
{
    ULONG DiskNumber;
    PARTITION_STYLE PartitionStyle;
    ULONG MbrSignature;
    GUID GptDiskId;
    STORAGE_BUS_TYPE BusType;
    ULONGLONG Size;
    ULONGLONG BytesPerSector;
    ULONGLONG SectorsPerTrack;
    ULONGLONG TracksPerCylinder;
    ULONGLONG Cylinders;
    ULONG SectorAlignment;
    ULONGLONG UsableStartOffset;
    ULONGLONG UsableEndOffset;

    WCHAR DeviceName[MAX_PATH];
    WCHAR Description[MAX_PATH];
    WCHAR Location[MAX_PATH];

    BOOLEAN IsBoot;
    BOOLEAN IsSystem;
    BOOLEAN IsRemovable;
    BOOLEAN IsOffline;
    BOOLEAN IsReadOnly;
    BOOLEAN IsDynamic;
    BOOLEAN HasExtendedPartition;
    ULONGLONG ExtendedPartitionOffset;
    ULONGLONG ExtendedPartitionLength;

    PDM_REGION Regions;
    ULONG RegionCount;
    ULONG PartitionCount;
} DM_DISK, *PDM_DISK;

typedef struct _DM_SNAPSHOT
{
    PDM_DISK Disks;
    ULONG DiskCount;
    PDM_VOLUME Volumes;
    ULONG VolumeCount;
    ULONG Generation;
} DM_SNAPSHOT, *PDM_SNAPSHOT;

VOID
DmSnapshotInitialize(
    _Out_ PDM_SNAPSHOT Snapshot);

VOID
DmSnapshotClear(
    _Inout_ PDM_SNAPSHOT Snapshot);

NTSTATUS
DmSnapshotRefresh(
    _Inout_ PDM_SNAPSHOT Snapshot);

NTSTATUS
DmSnapshotCreate(
    _Out_ PDM_SNAPSHOT *Snapshot);

VOID
DmSnapshotDestroy(
    _In_opt_ PDM_SNAPSHOT Snapshot);

PDM_DISK
DmSnapshotFindDiskByNumber(
    _In_ PDM_SNAPSHOT Snapshot,
    _In_ ULONG DiskNumber);

PDM_VOLUME
DmSnapshotFindVolumeByName(
    _In_ PDM_SNAPSHOT Snapshot,
    _In_ PCWSTR VolumeName);

typedef DM_SNAPSHOT DISKMGMT_SNAPSHOT;
typedef DM_DISK DISKMGMT_DISK;
typedef DM_REGION DISKMGMT_REGION;
typedef DM_VOLUME DISKMGMT_VOLUME;
typedef PDM_SNAPSHOT PDISKMGMT_SNAPSHOT;
typedef PDM_DISK PDISKMGMT_DISK;
typedef PDM_REGION PDISKMGMT_REGION;
typedef PDM_VOLUME PDISKMGMT_VOLUME;
typedef DM_SNAPSHOT DmSnapshot;
typedef DM_DISK DmDisk;
typedef DM_REGION DmRegion;
typedef DM_VOLUME DmVolume;

#ifdef __cplusplus
}
#endif

#endif /* DISKMGMT_SNAPSHOT_H */
