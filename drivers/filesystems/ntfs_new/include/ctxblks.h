/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include <ntifs.h>

// attributes.h needs to be included before this one.
#include <pshpack1.h>
#include <poppack.h>

#define GetDisposition(x) ((x >> 24) & 0xFF)
#define GetCreateOptions(x) (x & 0xFFFFFF)

typedef struct NtfsVolume NtfsVolume;
typedef NtfsVolume* PNtfsVolume;

typedef struct NtfsFileRecord NtfsFileRecord;
typedef NtfsFileRecord* PNtfsFileRecord;

typedef struct NtfsDirectory NtfsDirectory;
typedef NtfsDirectory* PNtfsDirectory;

typedef struct _VolumeContextBlock
{
    PNtfsVolume DiskVolume;
    PDEVICE_OBJECT StorageDevice;
    PFILE_OBJECT StreamFileObject;
    ERESOURCE MetadataResource;
    FAST_MUTEX StreamListMutex;
    LIST_ENTRY StreamList;
    PNOTIFY_SYNC NotifySync;
    LIST_ENTRY NotifyList;
    ULONG BytesPerSector;

    /* Paths already known not to exist, most recently used first. */
    FAST_MUTEX MissingNameMutex;
    LIST_ENTRY MissingNameList;
    ULONG MissingNameCount;
} VolumeContextBlock, *PVolumeContextBlock;

#define NTFS_MAX_MISSING_NAMES 256

typedef struct _NtfsMissingName
{
    LIST_ENTRY Link;
    ULONG Hash;
    USHORT Length;
    WCHAR Name[1];
} NtfsMissingName, *PNtfsMissingName;

/* Case-insensitive, matching the default NTFS name comparison. */
FORCEINLINE
ULONG
NtfsHashName(_In_reads_(Length) PCWSTR Name, _In_ USHORT Length)
{
    ULONG Hash = 2166136261u;
    USHORT Index;

    for (Index = 0; Index < Length; Index++)
    {
        Hash ^= (ULONG)RtlUpcaseUnicodeChar(Name[Index]);
        Hash *= 16777619u;
    }
    return Hash;
}

BOOLEAN
NtfsIsNameKnownMissing(_In_ PVolumeContextBlock VolCB,
                       _In_reads_(Length) PCWSTR Name,
                       _In_ USHORT Length);

VOID
NtfsRecordNameMissing(_In_ PVolumeContextBlock VolCB,
                      _In_reads_(Length) PCWSTR Name,
                      _In_ USHORT Length);

VOID
NtfsForgetMissingName(_In_ PVolumeContextBlock VolCB,
                      _In_reads_(Length) PCWSTR Name,
                      _In_ USHORT Length);

VOID
NtfsForgetAllMissingNames(_In_ PVolumeContextBlock VolCB);

/*
 * The library keeps the disk it reads through in one process-global slot, so
 * every mount probe overwrites it. Re-point it at this volume before touching
 * the library, otherwise requests are served from the last device probed.
 */
FORCEINLINE
VOID
NtfsBindVolumeDisk(_In_ PVolumeContextBlock VolCB)
{
    if (VolCB && VolCB->StorageDevice && VolCB->BytesPerSector)
        NtfsDiskInitializeKm(VolCB->StorageDevice, VolCB->BytesPerSector);
}

typedef struct _SCB
{
    LIST_ENTRY ListEntry;
    ULONGLONG FileReference;
    AttributeType RequestedType;
    UNICODE_STRING RequestedStream;
    LONG ReferenceCount;
    FILE_LOCK FileLock;
    SECTION_OBJECT_POINTERS SectionObjectPointers;
} StreamContextBlock, *PStreamContextBlock;

typedef struct _FCB
{
    /*
     * FsContext is cast directly to PFSRTL_COMMON_FCB_HEADER by MM and FsRtl.
     * Keep the advanced header first and allocate its full layout.
     */
    FSRTL_ADVANCED_FCB_HEADER CommonFCBHeader;
    ERESOURCE MainResource;
    ERESOURCE PagingIoResource;
    FAST_MUTEX HeaderMutex;

    PNtfsFileRecord FileRec;
    ULONG CreateOptions;
    ACCESS_MASK DesiredAccess;
    ULONG AutomaticTimestampMask;

    // Used for file name information;
    UNICODE_STRING FileName;

    // Used for Alternate Data Streams (ADS)
    AttributeType RequestedType;
    PWSTR RequestedStream;

    // Used for query directory requests
    PNtfsDirectory FileDir;

    // One-based index used to resume IRP_MJ_QUERY_EA enumeration.
    ULONG EaIndex;

    // Consider moving, multiple files can point to the same stream in NTFS.
    PStreamContextBlock StreamCB;

} FileContextBlock, *PFileContextBlock;

PStreamContextBlock
NtfsReferenceStreamContext(
    _In_ PVolumeContextBlock VolCB,
    _In_ PNtfsFileRecord File,
    _In_ AttributeType RequestedType,
    _In_opt_ PWSTR RequestedStream);

VOID
NtfsDereferenceStreamContext(
    _In_ PVolumeContextBlock VolCB,
    _In_ PStreamContextBlock StreamCB);
