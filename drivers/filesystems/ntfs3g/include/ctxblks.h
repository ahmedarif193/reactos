/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G file-system driver context blocks
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

/*
 * Names a lookup has already proved absent. Path resolution costs a walk of the
 * MFT and index B-trees even when everything it touches is already in the block
 * cache, and loaders probe the same missing names repeatedly. Entries are only
 * written while FcbListResource is held; any namespace change just bumps
 * NamespaceGeneration, which retires every entry at once.
 */
#define NTFS_NEGATIVE_CACHE_SIZE 64
#define NTFS_NEGATIVE_NAME_MAX   128

typedef struct _NtfsNegativeEntry
{
    ULONGLONG Hash;
    LONG Generation;
    USHORT Length;
    WCHAR Name[NTFS_NEGATIVE_NAME_MAX];
} NtfsNegativeEntry;

/*
 * Paths whose MFT record is already known. Resolving a path walks the MFT and
 * the index B-trees with Unicode collation every time, which measurement showed
 * costs about 25us even when every block it touches is already cached. Entries
 * are retired wholesale by NamespaceGeneration, which is also what keeps an
 * entry from surviving into a reused MFT record.
 */
#define NTFS_PATH_CACHE_SIZE 64

typedef struct _NtfsPathEntry
{
    ULONGLONG Hash;
    ULONGLONG FileId;
    LONG Generation;
    USHORT Length;
    WCHAR Name[NTFS_NEGATIVE_NAME_MAX];
} NtfsPathEntry;


typedef struct _VolumeContextBlock
{
    PNTFS3G_ROS_KM_VOLUME Volume;
    PDEVICE_OBJECT StorageDevice;
    PFILE_OBJECT StreamFileObject;
    struct _FileContextBlock *VolumeFcb;
    PFILE_OBJECT LockFileObject;
    ERESOURCE FcbListResource;
    LIST_ENTRY FcbListHead;
    BOOLEAN FcbListResourceInitialized;
    BOOLEAN ShutdownRegistered;
    BOOLEAN ShutdownStarted;
    BOOLEAN Dismounted;
    volatile LONG NamespaceGeneration;
    NtfsNegativeEntry NegativeCache[NTFS_NEGATIVE_CACHE_SIZE];
    NtfsPathEntry PathCache[NTFS_PATH_CACHE_SIZE];
} VolumeContextBlock, *PVolumeContextBlock;

/* Any change to the namespace retires every remembered absence at once. */
FORCEINLINE VOID
NtfsInvalidateNamespace(_In_ PVolumeContextBlock Volume)
{
    InterlockedIncrement(&Volume->NamespaceGeneration);
}

typedef struct _FileContextBlock
{
    FSRTL_ADVANCED_FCB_HEADER CommonFCBHeader;
    SECTION_OBJECT_POINTERS SectionObjectPointers;
    FILE_LOCK FileLock;
    ERESOURCE MainResource;
    ERESOURCE PagingIoResource;
    FAST_MUTEX HeaderMutex;
    SHARE_ACCESS ShareAccess;
    LIST_ENTRY ListEntry;
    PVolumeContextBlock Volume;
    LONG ReferenceCount;
    ULONG OpenHandleCount;
    BOOLEAN DeletePending;
    BOOLEAN DeleteCompleted;
    BOOLEAN IsVolume;
    NTFS3G_ROS_FILE *File;
    NTFS3G_ROS_FILE_INFORMATION Information;
    UNICODE_STRING FileName;
    /*
     * Set while the size recorded in the MFT record runs ahead of the real one
     * because an extending write grew the allocation in advance. Cleanup puts
     * the exact size back; queries always answer from CommonFCBHeader.
     */
    BOOLEAN SizeGrownAhead;
} FileContextBlock, *PFileContextBlock;

typedef struct _HandleContextBlock
{
    PFILE_OBJECT FileObject;
    ACCESS_MASK DesiredAccess;
    ULONG CreateOptions;
    BOOLEAN ShareAccessSet;
    BOOLEAN CleanupComplete;
    BOOLEAN ExtendedDasdIo;
    UNICODE_STRING DirectoryPattern;
    BOOLEAN DirectoryQueryStarted;
    ULONG EaIndex;
    NTFS3G_ROS_FILE *DirectoryFile;
} HandleContextBlock, *PHandleContextBlock;
