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
    EX_PUSH_LOCK MetadataGate;
    FAST_MUTEX VolumeStateMutex;
    SHARE_ACCESS VolumeShareAccess;
    LONG OpenHandleCount;
    LONG VolumeHandleCount;
    PFILE_OBJECT VolumeLockOwner;
    BOOLEAN Dismounting;
    BOOLEAN Dismounted;
    FAST_MUTEX StreamListMutex;
    LIST_ENTRY StreamList;
    PNOTIFY_SYNC NotifySync;
    LIST_ENTRY NotifyList;
    ULONG BytesPerSector;

    /* Paths already known not to exist, most recently used first. */
    FAST_MUTEX MissingNameMutex;
    LIST_ENTRY MissingNameList;
    LIST_ENTRY MissingNameHash[256];
    ULONG MissingNameCount;

    /* Parsed file records kept past their last handle, most recent first. */
    FAST_MUTEX RecordCacheMutex;
    LIST_ENTRY RecordCacheList;
    LIST_ENTRY RecordCacheHash[256];
    LIST_ENTRY RecordIdentityHash[256];
    ULONG RecordCacheCount;

    /* One stable parent record for repeated leaf lookups in a directory. */
    PNtfsFileRecord CachedLookupParent;
    LONG CachedLookupParentGeneration;
    USHORT CachedLookupParentPathLength;
    WCHAR CachedLookupParentPath[128];

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
#define NTFS_MISSING_NAME_BUCKETS RTL_NUMBER_OF_FIELD(VolumeContextBlock, MissingNameHash)
#define NTFS_MAX_CACHED_RECORDS 512
#define NTFS_RECORD_CACHE_BUCKETS RTL_NUMBER_OF_FIELD(VolumeContextBlock, RecordCacheHash)
#define NTFS_MAX_IDLE_FCBS 64
#define NTFS_INLINE_FILE_NAME_CHARS 128

/* Everything from FileRec onwards describes one open and is reset on reuse;
 * the header, resources and mutex above it stay initialized. */
#define NTFS_FCB_PER_OPEN_OFFSET FIELD_OFFSET(FileContextBlock, FileRec)

typedef struct _NtfsCachedRecord
{
    LIST_ENTRY Link;
    LIST_ENTRY HashLink;
    LIST_ENTRY IdentityLink;
    ULONGLONG FileReference;
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
    LIST_ENTRY HashLink;
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

typedef struct _NTFS_NATIVE_SCB
{
    UCHAR Unused0[sizeof(PVOID) == 8 ? 0x1e0 : 0x148];
    LIST_ENTRY CcbList;
    UCHAR Unused1[(sizeof(PVOID) == 8 ? 0x280 : 0x1b0) -
                  (sizeof(PVOID) == 8 ? 0x1e0 : 0x148) - sizeof(LIST_ENTRY)];
    LIST_ENTRY ChildLcbList;
    UCHAR Unused2[(sizeof(PVOID) == 8 ? 0x308 : 0x1fc) -
                  (sizeof(PVOID) == 8 ? 0x280 : 0x1b0) - sizeof(LIST_ENTRY)];
} NTFS_NATIVE_SCB, *PNTFS_NATIVE_SCB;

typedef struct _NTFS_NATIVE_LCB
{
    UCHAR Unused0[8];
    LIST_ENTRY ParentEntry;
    PNTFS_NATIVE_SCB ParentScb;
    UCHAR Unused1[(sizeof(PVOID) == 8 ? 0x48 : 0x30) -
                  8 - sizeof(LIST_ENTRY) - sizeof(PVOID)];
    UNICODE_STRING FileName;
    UCHAR Unused2[(sizeof(PVOID) == 8 ? 0x78 : 0x48) -
                  (sizeof(PVOID) == 8 ? 0x48 : 0x30) - sizeof(UNICODE_STRING)];
    LIST_ENTRY CcbList;
    UCHAR Unused3[(sizeof(PVOID) == 8 ? 0xbc : 0x84) -
                  (sizeof(PVOID) == 8 ? 0x78 : 0x48) - sizeof(LIST_ENTRY)];
    ULONG CleanupCount;
    UCHAR Unused4[(sizeof(PVOID) == 8 ? 0xd8 : 0xa0) -
                  (sizeof(PVOID) == 8 ? 0xbc : 0x84) - sizeof(ULONG)];
} NTFS_NATIVE_LCB, *PNTFS_NATIVE_LCB;

typedef struct _NTFS_NATIVE_CCB
{
    UCHAR Unused0[sizeof(PVOID) == 8 ? 0x10 : 0x0c];
    UNICODE_STRING FileName;
    UCHAR Unused1[8];
    LIST_ENTRY StreamEntry;
    LIST_ENTRY LcbEntry;
    PNTFS_NATIVE_LCB Lcb;
    UCHAR Unused2[(sizeof(PVOID) == 8 ? 0x70 : 0x48) -
                  (sizeof(PVOID) == 8 ? 0x48 : 0x2c) - sizeof(PVOID)];
    PFILE_OBJECT FileObject;
    UCHAR Unused3[(sizeof(PVOID) == 8 ? 0x88 : 0x58) -
                  (sizeof(PVOID) == 8 ? 0x70 : 0x48) - sizeof(PVOID)];
} NTFS_NATIVE_CCB, *PNTFS_NATIVE_CCB;

C_ASSERT(FIELD_OFFSET(NTFS_NATIVE_SCB, CcbList) == (sizeof(PVOID) == 8 ? 0x1e0 : 0x148));
C_ASSERT(FIELD_OFFSET(NTFS_NATIVE_SCB, ChildLcbList) == (sizeof(PVOID) == 8 ? 0x280 : 0x1b0));
C_ASSERT(sizeof(NTFS_NATIVE_SCB) == (sizeof(PVOID) == 8 ? 0x308 : 0x1fc));
C_ASSERT(FIELD_OFFSET(NTFS_NATIVE_LCB, ParentEntry) == 8);
C_ASSERT(FIELD_OFFSET(NTFS_NATIVE_LCB, ParentScb) == (sizeof(PVOID) == 8 ? 0x18 : 0x10));
C_ASSERT(FIELD_OFFSET(NTFS_NATIVE_LCB, FileName) == (sizeof(PVOID) == 8 ? 0x48 : 0x30));
C_ASSERT(FIELD_OFFSET(NTFS_NATIVE_LCB, CcbList) == (sizeof(PVOID) == 8 ? 0x78 : 0x48));
C_ASSERT(FIELD_OFFSET(NTFS_NATIVE_LCB, CleanupCount) == (sizeof(PVOID) == 8 ? 0xbc : 0x84));
C_ASSERT(sizeof(NTFS_NATIVE_LCB) == (sizeof(PVOID) == 8 ? 0xd8 : 0xa0));
C_ASSERT(FIELD_OFFSET(NTFS_NATIVE_CCB, FileName) == (sizeof(PVOID) == 8 ? 0x10 : 0x0c));
C_ASSERT(FIELD_OFFSET(NTFS_NATIVE_CCB, StreamEntry) == (sizeof(PVOID) == 8 ? 0x28 : 0x1c));
C_ASSERT(FIELD_OFFSET(NTFS_NATIVE_CCB, LcbEntry) == (sizeof(PVOID) == 8 ? 0x38 : 0x24));
C_ASSERT(FIELD_OFFSET(NTFS_NATIVE_CCB, Lcb) == (sizeof(PVOID) == 8 ? 0x48 : 0x2c));
C_ASSERT(FIELD_OFFSET(NTFS_NATIVE_CCB, FileObject) == (sizeof(PVOID) == 8 ? 0x70 : 0x48));
C_ASSERT(sizeof(NTFS_NATIVE_CCB) == (sizeof(PVOID) == 8 ? 0x88 : 0x58));

typedef struct _SCB
{
    /*
     * FsContext is the shared stream identity and is consumed directly as an
     * FSRTL common header by Mm, Cc and FsRtl.  Per-open state lives in the
     * FileContextBlock published through FsContext2.
     */
    FSRTL_ADVANCED_FCB_HEADER CommonFCBHeader;
    ERESOURCE MainResource;
    ERESOURCE PagingIoResource;
    FAST_MUTEX HeaderMutex;
    LIST_ENTRY ListEntry;
    ULONGLONG FileReference;
    AttributeType RequestedType;
    UNICODE_STRING RequestedStream;
    LONG ReferenceCount;
    FILE_LOCK FileLock;
    SHARE_ACCESS ShareAccess;
    SECTION_OBJECT_POINTERS SectionObjectPointers;
    BOOLEAN SizePending;
    BOOLEAN Deleted;
    BOOLEAN DeletePending;
    LONG UncleanCount;
    NTFS_NATIVE_SCB NativeScb;
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
    WCHAR InlineFileName[NTFS_INLINE_FILE_NAME_CHARS];

    PNtfsFileRecord FileRec;
    BOOLEAN IsVolumeOpen;
    BOOLEAN CleanupComplete;
    BOOLEAN ManageVolumeAccess;
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
    BOOLEAN WriteTimesStamped;
    BOOLEAN ShareAccessSet;

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
    /* NTFS does not store dot entries; expose them before the real index. */
    UCHAR DirDotIndex;
    BOOLEAN DirRealScanStarted;

    /* The search pattern belongs to the handle: it is supplied once and
     * every later query without a name reuses it. */
    UNICODE_STRING DirSearchPattern;

    /* Non-NULL when FileRec is on loan from the volume's record cache. */
    struct _NtfsCachedRecord* CachedRecord;

    /* Links this block into the volume's idle list while it is not in use. */
    LIST_ENTRY IdleLink;

    NTFS_NATIVE_CCB NativeCcb;

} FileContextBlock, *PFileContextBlock;

FORCEINLINE
PFileContextBlock
NtfsGetFileContext(_In_opt_ PFILE_OBJECT FileObject)
{
    if (!FileObject)
        return NULL;

    return (PFileContextBlock)(FileObject->FsContext2 ? FileObject->FsContext2 : FileObject->FsContext);
}

FORCEINLINE
PFSRTL_ADVANCED_FCB_HEADER
NtfsGetCommonFcbHeader(_In_ PFileContextBlock FileCB)
{
    return FileCB->StreamCB ? &FileCB->StreamCB->CommonFCBHeader : &FileCB->CommonFCBHeader;
}

FORCEINLINE
PERESOURCE
NtfsGetMainResource(_In_ PFileContextBlock FileCB)
{
    return FileCB->StreamCB ? &FileCB->StreamCB->MainResource : &FileCB->MainResource;
}

FORCEINLINE
PERESOURCE
NtfsGetPagingIoResource(_In_ PFileContextBlock FileCB)
{
    return FileCB->StreamCB ? &FileCB->StreamCB->PagingIoResource : &FileCB->PagingIoResource;
}

NTSTATUS
NtfsCheckRecordAccess(
    _In_ PNtfsFileRecord File,
    _In_ PSECURITY_SUBJECT_CONTEXT SubjectContext,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ACCESS_MASK PreviouslyGrantedAccess,
    _In_ KPROCESSOR_MODE AccessMode,
    _Out_ PACCESS_MASK GrantedAccess,
    _Out_ PPRIVILEGE_SET* Privileges);

BOOLEAN
NtfsSplitParentName(
    _In_ PUNICODE_STRING Name,
    _Out_ PUSHORT ParentLength,
    _Out_ PWCHAR* LeafName,
    _Out_ PUSHORT LeafLength);

NTSTATUS
NtfsForwardVolumeIo(_In_ PVolumeContextBlock VolCB,
                    _In_ PFileContextBlock FileCB,
                    _Inout_ PIRP Irp,
                    _In_ BOOLEAN Write);

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

NTSTATUS
NtfsReferenceNameParent(
    _In_ PVolumeContextBlock VolCB,
    _In_ PUNICODE_STRING Name,
    _Out_ PStreamContextBlock* ParentStream,
    _Out_ PUNICODE_STRING LeafName);

NTSTATUS
NtfsFindOpenLink(_In_ PVolumeContextBlock VolCB,
                 _In_ PFileContextBlock FileCB,
                 _Out_ PNTFS_NATIVE_LCB* Link);

VOID
NtfsSetLinkDeletePending(_In_ PFileContextBlock FileCB,
                         _In_ BOOLEAN DeletePending);

VOID
NtfsCleanupFailedCreate(_In_ PVolumeContextBlock VolCB,
                        _In_ PFileContextBlock FileCB,
                        _In_ PFILE_OBJECT FileObject);

VOID
NtfsRemoveOpenLink(_In_ PVolumeContextBlock VolCB,
                   _In_ PFileContextBlock FileCB);

NTSTATUS
NtfsCheckDirectoryOpenChildren(_In_ PVolumeContextBlock VolCB,
                               _In_ ULONGLONG DirectoryReference);

NTSTATUS
NtfsRefreshDirectoryRecord(_In_ PVolumeContextBlock VolCB,
                            _In_ PFileContextBlock FileCB);

/* Exported by ntoskrnl, but not declared by the DDK headers. */
NTKERNELAPI VOID FASTCALL
ExfAcquirePushLockExclusive(_Inout_ PEX_PUSH_LOCK PushLock);
NTKERNELAPI VOID FASTCALL
ExfReleasePushLockExclusive(_Inout_ PEX_PUSH_LOCK PushLock);

static inline
VOID
NtfsAcquireMetadata(_In_ PVolumeContextBlock VolCB)
{
    if (!ExIsResourceAcquiredExclusiveLite(&VolCB->MetadataResource))
        ExfAcquirePushLockExclusive(&VolCB->MetadataGate);
    ExAcquireResourceExclusiveLite(&VolCB->MetadataResource, TRUE);
}

static inline
VOID
NtfsReleaseMetadata(_In_ PVolumeContextBlock VolCB)
{
    BOOLEAN Last = ExIsResourceAcquiredSharedLite(&VolCB->MetadataResource) == 1;

    ExReleaseResourceLite(&VolCB->MetadataResource);
    if (Last)
        ExfReleasePushLockExclusive(&VolCB->MetadataGate);
}
