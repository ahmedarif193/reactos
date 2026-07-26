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

    /* Parsed file records kept past their last handle, most recent first. */
    FAST_MUTEX RecordCacheMutex;
    LIST_ENTRY RecordCacheList;
    ULONG RecordCacheCount;

    /*
     * One loaded directory tree kept for reuse. Building the in-memory
     * B-tree is nearly the whole cost of listing a directory, and the tree
     * only goes stale when something in the volume changes; the generation
     * number says when.
     */
    FAST_MUTEX DirCacheMutex;
    PNtfsDirectory CachedDir;
    LONG CachedDirGeneration;
    USHORT CachedDirPathLength;
    BOOLEAN CachedDirBusy;
    WCHAR CachedDirPath[128];
    LONG DirGeneration;

    /* Context blocks kept with their resources still initialized. */
    FAST_MUTEX IdleFcbMutex;
    LIST_ENTRY IdleFcbList;
    ULONG IdleFcbCount;
} VolumeContextBlock, *PVolumeContextBlock;

#define NTFS_MAX_MISSING_NAMES 256
#define NTFS_MAX_CACHED_RECORDS 512
#define NTFS_MAX_IDLE_FCBS 64

/* Everything from FileRec onwards describes one open and is reset on reuse;
 * the header, resources and mutex above it stay initialized. */
#define NTFS_FCB_PER_OPEN_OFFSET FIELD_OFFSET(FileContextBlock, FileRec)

typedef struct _NtfsCachedRecord
{
    LIST_ENTRY Link;
    ULONG Hash;
    USHORT Length;
    LONG InUse;
    BOOLEAN Evicted;
    PNtfsFileRecord Record;
    WCHAR Name[1];
} NtfsCachedRecord, *PNtfsCachedRecord;

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

PNtfsCachedRecord
NtfsAcquireCachedRecord(_In_ PVolumeContextBlock VolCB,
                        _In_reads_(Length) PCWSTR Name,
                        _In_ USHORT Length);

PNtfsCachedRecord
NtfsCacheRecord(_In_ PVolumeContextBlock VolCB,
                _In_reads_(Length) PCWSTR Name,
                _In_ USHORT Length,
                _In_ PNtfsFileRecord Record);

VOID
NtfsReleaseCachedRecord(_In_ PVolumeContextBlock VolCB,
                        _In_ PNtfsCachedRecord Entry);

VOID
NtfsEvictCachedRecord(_In_ PVolumeContextBlock VolCB,
                      _In_reads_(Length) PCWSTR Name,
                      _In_ USHORT Length,
                      _In_ BOOLEAN RecordAlreadyFreed);

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

    /* Set through FileDispositionInformation or FILE_DELETE_ON_CLOSE;
     * the name is removed when the handle goes away. */
    BOOLEAN DeletePending;

    /* Decided once at open: whether the first data read still owes a
     * last-access refresh. Checking the record on every read cost more
     * than the read. */
    BOOLEAN LastAccessStampPending;

    /* FileDir is on loan from the volume's directory cache. */
    BOOLEAN FileDirBorrowed;
    /* FileDir streams INDX entries and borrows FileRec for its lifetime. */
    BOOLEAN FileDirDirect;
    /* First QueryDirectory on this handle must start at the beginning. */
    BOOLEAN DirScanStarted;

    /* Non-NULL when FileRec is on loan from the volume's record cache. */
    struct _NtfsCachedRecord* CachedRecord;

    /* Links this block into the volume's idle list while it is not in use. */
    LIST_ENTRY IdleLink;

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
