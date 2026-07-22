/*
 * PROJECT:     ReactOS exFAT filesystem driver
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Driver-wide definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <ntifs.h>
#include <ntdddisk.h>
#include <pseh/pseh2.h>
#include <section_attribs.h>

#include <ff.h>
#include <diskio.h>

#define EXFAT_DEVICE_NAME L"\\ExFat"

#define TAG_EXFAT_GLOBAL 'GfxE'
#define TAG_EXFAT_VCB    'VfxE'
#define TAG_EXFAT_FCB    'FfxE'
#define TAG_EXFAT_CCB    'CfxE'
#define TAG_EXFAT_PATH   'PfxE'
#define TAG_EXFAT_FATFS  'LfxE'
#define TAG_EXFAT_IO     'IfxE'

#define EXFAT_READ_AHEAD_GRANULARITY (64 * 1024)
#define EXFAT_SECTOR_CACHE_SIZE      (128 * 1024)
#define EXFAT_SECTOR_CACHE_EMPTY     ((LBA_t)~0ULL)
#define EXFAT_FATFS_NAME_BUFFER_SIZE \
    (((FF_MAX_LFN + 1) * sizeof(WCHAR)) + (((FF_MAX_LFN + 44U) / 15) * 32))
#define EXFAT_FATFS_ALLOCATION_SIGNATURE 'afxE'

typedef union _EXFAT_FATFS_ALLOCATION_HEADER
{
    struct
    {
        ULONG Signature;
        BOOLEAN FromLookaside;
    } Fields;
    ULONG_PTR Alignment[2];
} EXFAT_FATFS_ALLOCATION_HEADER, *PEXFAT_FATFS_ALLOCATION_HEADER;

#define EXFAT_FCB_SIGNATURE 0x5846
#define EXFAT_CCB_SIGNATURE 0x5843

#define EXFAT_NAME_OFFSET           3
#define EXFAT_NAME_LENGTH           8
#define EXFAT_SECTOR_SHIFT_OFFSET   108
#define EXFAT_BOOT_SIGNATURE_OFFSET 510
#define EXFAT_MIN_SECTOR_SHIFT      9
#define EXFAT_MAX_SECTOR_SHIFT      12

typedef struct _EXFAT_VCB EXFAT_VCB, *PEXFAT_VCB;

typedef struct _EXFAT_FCB
{
    FSRTL_COMMON_FCB_HEADER Header;
    SECTION_OBJECT_POINTERS SectionObjectPointers;
    ERESOURCE MainResource;
    ERESOURCE PagingIoResource;
    FILE_LOCK FileLock;
    SHARE_ACCESS ShareAccess;
    LIST_ENTRY ListEntry;
    PEXFAT_VCB Vcb;
    UNICODE_STRING PathName;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    ULONGLONG IndexNumber;
    ULONG FileAttributes;
    LONG ReferenceCount;
    ULONG OpenHandleCount;
    FIL FatFile;
    PDWORD ClusterMap;
    BOOLEAN FatFileOpen;
    BOOLEAN FatFileWritable;
    BOOLEAN IsDirectory;
    BOOLEAN IsVolume;
    BOOLEAN DeletePending;
} EXFAT_FCB, *PEXFAT_FCB;

typedef struct _EXFAT_CCB
{
    USHORT NodeTypeCode;
    USHORT NodeByteSize;
    PFILE_OBJECT FileObject;
    ACCESS_MASK DesiredAccess;
    BOOLEAN HandleOpen;
    BOOLEAN ShareAccessSet;
    BOOLEAN CleanedUp;
    BOOLEAN DeleteOnClose;
    BOOLEAN IsDirectory;
    ULONG DirectoryIndex;
    UNICODE_STRING SearchPattern;
    union
    {
        FIL File;
        DIR Directory;
    } Handle;
} EXFAT_CCB, *PEXFAT_CCB;

struct _EXFAT_VCB
{
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT StorageDevice;
    PVPB Vpb;
    FATFS FileSystem;
    BYTE DriveNumber;
    ULONG BytesPerSector;
    ULONGLONG SectorCount;
    ULONG SerialNumber;
    BOOLEAN ReadOnly;
    BOOLEAN Mounted;
    BOOLEAN Locked;
    PFILE_OBJECT LockOwner;
    ERESOURCE Resource;
    ERESOURCE FatFsResource;
    LIST_ENTRY FcbListHead;
    LONG OpenHandleCount;
    PEXFAT_FCB VolumeFcb;
    PFILE_OBJECT StreamFileObject;
    PVOID SectorCacheAllocation;
    PVOID SectorCacheBuffer;
    LBA_t* SectorCacheTags;
    ULONG SectorCacheEntries;
    ULONG SectorCacheNext;
};

typedef struct _EXFAT_GLOBAL_DATA
{
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT DeviceObject;
    ERESOURCE VolumeListResource;
    NPAGED_LOOKASIDE_LIST FatFsNameBufferLookaside;
    CACHE_MANAGER_CALLBACKS CacheManagerCallbacks;
    FAST_IO_DISPATCH FastIoDispatch;
    PEXFAT_VCB Volumes[FF_VOLUMES];
} EXFAT_GLOBAL_DATA, *PEXFAT_GLOBAL_DATA;

extern PEXFAT_GLOBAL_DATA ExFatGlobalData;

DRIVER_INITIALIZE DriverEntry;
DRIVER_DISPATCH ExFatFsdDispatch;

NTSTATUS ExFatCreate(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS ExFatClose(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS ExFatCleanup(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS ExFatRead(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS ExFatWrite(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS ExFatQueryInformation(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS ExFatSetInformation(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS ExFatQueryVolumeInformation(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS ExFatSetVolumeInformation(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS ExFatDirectoryControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS ExFatFileSystemControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS ExFatFlushBuffers(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS ExFatLockControl(PDEVICE_OBJECT DeviceObject, PIRP Irp);
NTSTATUS ExFatShutdown(PDEVICE_OBJECT DeviceObject, PIRP Irp);

NTSTATUS ExFatMapResult(FRESULT Result);
PVOID ExFatGetUserBuffer(PIRP Irp, BOOLEAN PagingIo);
NTSTATUS ExFatLockUserBuffer(PIRP Irp, ULONG Length, LOCK_OPERATION Operation);
NTSTATUS ExFatReadWriteDevice(PDEVICE_OBJECT DeviceObject, UCHAR MajorFunction, PVOID Buffer, ULONG Length, PLARGE_INTEGER Offset, BOOLEAN OverrideVerify);
NTSTATUS ExFatFlushStorageDevice(PEXFAT_VCB Vcb);
NTSTATUS ExFatDeviceIoControl(PDEVICE_OBJECT DeviceObject, ULONG ControlCode, PVOID InputBuffer, ULONG InputLength, PVOID OutputBuffer, PULONG OutputLength, BOOLEAN OverrideVerify);
PCHAR ExFatBuildFatPath(PEXFAT_VCB Vcb, PUNICODE_STRING PathName);
NTSTATUS ExFatBuildFullPath(PFILE_OBJECT FileObject, PUNICODE_STRING FullPath);
NTSTATUS ExFatUtf8ToUnicode(PCSTR Source, PUNICODE_STRING Destination);
VOID ExFatFreeUnicodeString(PUNICODE_STRING String);
ULONG ExFatFatAttributesToNt(BYTE Attributes);
BYTE ExFatNtAttributesToFat(ULONG Attributes);
LARGE_INTEGER ExFatFatTimeToSystemTime(WORD Date, WORD Time);
VOID ExFatSystemTimeToFatTime(PLARGE_INTEGER SystemTime, PWORD Date, PWORD Time);
ULONGLONG ExFatRoundUp(ULONGLONG Value, ULONG Alignment);

PEXFAT_FCB ExFatCreateFcb(PEXFAT_VCB Vcb, PUNICODE_STRING PathName, FILINFO* Information, BOOLEAN IsVolume);
PEXFAT_FCB ExFatFindFcb(PEXFAT_VCB Vcb, PUNICODE_STRING PathName);
VOID ExFatReferenceFcb(PEXFAT_FCB Fcb);
VOID ExFatDereferenceFcb(PEXFAT_FCB Fcb);
VOID ExFatUpdateFcbFromInfo(PEXFAT_FCB Fcb, FILINFO* Information);
NTSTATUS ExFatSetFcbPath(PEXFAT_FCB Fcb, PUNICODE_STRING PathName);
ULONGLONG ExFatHashPath(PUNICODE_STRING PathName);

VOID ExFatAcquireFatFs(PEXFAT_VCB Vcb);
VOID ExFatReleaseFatFs(PEXFAT_VCB Vcb);
FRESULT ExFatEnsureFcbFile(PEXFAT_FCB Fcb, BOOLEAN WriteAccess);
FRESULT ExFatSeekFcbFile(PEXFAT_FCB Fcb, FSIZE_t Offset);
FRESULT ExFatZeroFileRange(PEXFAT_FCB Fcb, FSIZE_t Start, FSIZE_t End);
BOOLEAN ExFatFileIsContiguous(PEXFAT_FCB Fcb);
VOID ExFatInvalidateFcbClusterMap(PEXFAT_FCB Fcb);
VOID ExFatInvalidateSectorCache(PEXFAT_VCB Vcb);
VOID ExFatInvalidateSectorCacheRange(PEXFAT_VCB Vcb, LBA_t Sector, UINT Count);

BOOLEAN NTAPI ExFatAcquireForLazyWrite(PVOID Context, BOOLEAN Wait);
VOID NTAPI ExFatReleaseFromLazyWrite(PVOID Context);
BOOLEAN NTAPI ExFatAcquireForReadAhead(PVOID Context, BOOLEAN Wait);
VOID NTAPI ExFatReleaseFromReadAhead(PVOID Context);

BOOLEAN NTAPI ExFatFastIoCheckIfPossible(PFILE_OBJECT FileObject, PLARGE_INTEGER FileOffset, ULONG Length, BOOLEAN Wait, ULONG LockKey, BOOLEAN CheckForReadOperation, PIO_STATUS_BLOCK IoStatus, PDEVICE_OBJECT DeviceObject);
VOID NTAPI ExFatAcquireFileForNtCreateSection(PFILE_OBJECT FileObject);
VOID NTAPI ExFatReleaseFileForNtCreateSection(PFILE_OBJECT FileObject);

#define ExFatIsWriteAccess(Access) \
    BooleanFlagOn((Access), FILE_WRITE_DATA | FILE_APPEND_DATA | FILE_WRITE_EA | FILE_WRITE_ATTRIBUTES | DELETE)
