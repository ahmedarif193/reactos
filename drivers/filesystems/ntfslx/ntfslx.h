#ifndef _NTFSLX_H_
#define _NTFSLX_H_

#include <ntifs.h>
#include <ntdddisk.h>
#include "ntfslx_layout.h"

#define NTFSLX_TAG 'xlTN'
#define NTFSLX_DEVICE_NAME L"\\NtfsLx"
#define NTFSLX_DEFAULT_UPCASE_LENGTH 0x10000

/*
 * In-memory self-test IOCTL (sent to the control device \NtfsLx).
 *
 * The test allocates an internal RAM-backed buffer (no storage device, no
 * drive letter) and exercises the index, attribute, FCB header, and spill
 * code paths. Results are returned in NTFSLX_SELFTEST_RESULT.
 */
#define IOCTL_NTFSLX_SELFTEST \
    CTL_CODE(FILE_DEVICE_DISK_FILE_SYSTEM, 0x800, METHOD_BUFFERED, FILE_ANY_ACCESS)

/*
 * IOCTL_NTFSLX_TIER0_PROOF - reads on-disk state and reports it back so a
 * user-mode (kmtest) caller can assert the TIER 0 corruption rules are
 * being enforced. Issued against a mounted volume device (e.g. "\\??\\D:\\").
 *
 * Fields in NTFSLX_TIER0_PROOF:
 *   - MftMirrorConsistent: TRUE if $MFT records 0..3 byte-match $MFTMirr
 *     contents (with MST fixup removed from both sides).
 *   - MftMirrorRecords:    Number of records that were compared (0..4).
 *   - VolumeDirtyFlag:     0 or 1 — value of NTFSLX_VOLUME_IS_DIRTY in the
 *                          on-disk $Volume $VOLUME_INFORMATION attribute.
 *   - LogFileFirstDword:   First 4 bytes of $LogFile as a raw u32. 0xFFFFFFFF
 *                          means we've successfully wiped it.
 *   - LogFileIs0xFF:       TRUE if the first 512 bytes of $LogFile are all 0xFF.
 *   - LcnZeroIsReserved:   TRUE if bitmap bit 0 is set ($Boot should claim it).
 *   - BootLcn:             LCN of $Boot as reported by the driver (should be 0).
 *   - ReturnStatus:        NTSTATUS of the proof gather (STATUS_SUCCESS if
 *                          the driver could read all the inputs). Non-success
 *                          means the individual fields should not be trusted.
 */
#define IOCTL_NTFSLX_TIER0_PROOF \
    CTL_CODE(FILE_DEVICE_DISK_FILE_SYSTEM, 0x801, METHOD_BUFFERED, FILE_ANY_ACCESS)

#define NTFSLX_SELFTEST_BUFFER_SIZE (32 * 1024 * 1024)

typedef struct _NTFSLX_SELFTEST_RESULT
{
    ULONG TotalChecks;
    ULONG PassedChecks;
    ULONG FailedChecks;
    ULONG FirstFailCode; /* encodes the failure site */
} NTFSLX_SELFTEST_RESULT, *PNTFSLX_SELFTEST_RESULT;

typedef struct _NTFSLX_TIER0_PROOF
{
    ULONG  Version;               /* 1 */
    NTSTATUS ReturnStatus;
    ULONG  MftMirrorConsistent;   /* BOOLEAN-like */
    ULONG  MftMirrorRecords;      /* how many records we successfully compared */
    ULONG  VolumeDirtyFlag;       /* 0 or 1 */
    ULONG  LogFileFirstDword;
    ULONG  LogFileIs0xFF;
    ULONG  LcnZeroIsReserved;
    ULONGLONG BootLcn;
    ULONGLONG MftMirrLcn;
} NTFSLX_TIER0_PROOF, *PNTFSLX_TIER0_PROOF;

#define NTFSLX_RECORD_MAGIC_FILE 0x454C4946UL
#define NTFSLX_RECORD_MAGIC_INDX 0x58444E49UL
#define NTFSLX_RECORD_MAGIC_BAAD 0x44414142UL

#define NTFSLX_MFT_RECORD_IN_USE 0x0001
#define NTFSLX_MFT_RECORD_IS_DIRECTORY 0x0002
#define NTFSLX_ATTRIBUTE_ATTRIBUTE_LIST 0x00000020UL
#define NTFSLX_ATTRIBUTE_FILE_NAME 0x00000030UL
#define NTFSLX_ATTRIBUTE_DATA 0x00000080UL
#define NTFSLX_ATTRIBUTE_INDEX_ROOT 0x00000090UL
#define NTFSLX_ATTRIBUTE_INDEX_ALLOCATION 0x000000A0UL
#define NTFSLX_ATTRIBUTE_VOLUME_NAME 0x00000060UL
#define NTFSLX_ATTRIBUTE_VOLUME_INFORMATION 0x00000070UL
#define NTFSLX_ATTRIBUTE_END 0xFFFFFFFFUL

#define NTFSLX_FILE_MFT 0
#define NTFSLX_FILE_VOLUME 3
#define NTFSLX_FILE_ROOT 5

#define NTFSLX_MFT_REFERENCE_MASK 0x0000FFFFFFFFFFFFULL
#define MREF(Reference) ((Reference) & NTFSLX_MFT_REFERENCE_MASK)

#define NTFSLX_LCN_HOLE ((LONGLONG)-1)
#define NTFSLX_LCN_RL_NOT_MAPPED ((LONGLONG)-3)
#define NTFSLX_LCN_ENOENT ((LONGLONG)-4)
#define NTFSLX_CASE_SENSITIVE 0
#define NTFSLX_IGNORE_CASE 1
#define NTFSLX_FILE_CONTEXT_SIGNATURE 'xCfN'
#define NTFSLX_CCB_SIGNATURE 'cCxN'

#define ROUND_UP(N, S) ((((N) + (S) - 1) / (S)) * (S))
#define ROUND_DOWN(N, S) ((N) - ((N) % (S)))

#include <pshpack1.h>
typedef struct _NTFSLX_BIOS_PARAMETER_BLOCK
{
    USHORT BytesPerSector;
    UCHAR SectorsPerCluster;
    USHORT ReservedSectors;
    UCHAR Fats;
    USHORT RootEntries;
    USHORT Sectors;
    UCHAR MediaType;
    USHORT SectorsPerFat;
    USHORT SectorsPerTrack;
    USHORT Heads;
    ULONG HiddenSectors;
    ULONG LargeSectors;
} NTFSLX_BIOS_PARAMETER_BLOCK, *PNTFSLX_BIOS_PARAMETER_BLOCK;

typedef struct _NTFSLX_BOOT_SECTOR
{
    UCHAR Jump[3];
    ULONGLONG OemId;
    NTFSLX_BIOS_PARAMETER_BLOCK Bpb;
    UCHAR Unused[4];
    ULONGLONG SectorCount;
    ULONGLONG MftLcn;
    ULONGLONG MftMirrLcn;
    CHAR ClustersPerMftRecord;
    UCHAR Reserved0[3];
    CHAR ClustersPerIndexRecord;
    UCHAR Reserved1[3];
    ULONGLONG SerialNumber;
    ULONG Checksum;
    UCHAR BootStrap[426];
    USHORT EndOfSectorMarker;
} NTFSLX_BOOT_SECTOR, *PNTFSLX_BOOT_SECTOR;

typedef struct _NTFSLX_RECORD_HEADER
{
    ULONG Magic;
    USHORT UsaOffset;
    USHORT UsaCount;
} NTFSLX_RECORD_HEADER, *PNTFSLX_RECORD_HEADER;

typedef struct _NTFSLX_MFT_RECORD
{
    NTFSLX_RECORD_HEADER Ntfs;
    ULONGLONG Lsn;
    USHORT SequenceNumber;
    USHORT LinkCount;
    USHORT AttributesOffset;
    USHORT Flags;
    ULONG BytesInUse;
    ULONG BytesAllocated;
    ULONGLONG BaseMftRecord;
    USHORT NextAttributeInstance;
    USHORT Reserved;
    ULONG MftRecordNumber;
} NTFSLX_MFT_RECORD, *PNTFSLX_MFT_RECORD;

typedef struct _NTFSLX_ATTR_RECORD
{
    ULONG Type;
    ULONG Length;
    UCHAR NonResident;
    UCHAR NameLength;
    USHORT NameOffset;
    USHORT Flags;
    USHORT Instance;
    union
    {
        struct
        {
            ULONG ValueLength;
            USHORT ValueOffset;
            UCHAR Flags;
            CHAR Reserved;
        } Resident;
        struct
        {
            ULONGLONG LowestVcn;
            ULONGLONG HighestVcn;
            USHORT MappingPairsOffset;
            UCHAR CompressionUnit;
            UCHAR Reserved[5];
            ULONGLONG AllocatedSize;
            ULONGLONG DataSize;
            ULONGLONG InitializedSize;
            ULONGLONG CompressedSize;
        } NonResident;
    } Data;
} NTFSLX_ATTR_RECORD, *PNTFSLX_ATTR_RECORD;

typedef struct _NTFSLX_VOLUME_INFORMATION_ATTRIBUTE
{
    ULONGLONG Reserved;
    UCHAR MajorVersion;
    UCHAR MinorVersion;
    USHORT Flags;
} NTFSLX_VOLUME_INFORMATION_ATTRIBUTE, *PNTFSLX_VOLUME_INFORMATION_ATTRIBUTE;

typedef struct _NTFSLX_RUNLIST_ELEMENT
{
    LONGLONG Vcn;
    LONGLONG Lcn;
    LONGLONG Length;
} NTFSLX_RUNLIST_ELEMENT, *PNTFSLX_RUNLIST_ELEMENT;
#include <poppack.h>

C_ASSERT(sizeof(NTFSLX_BOOT_SECTOR) == 512);
C_ASSERT(sizeof(NTFSLX_RECORD_HEADER) == 8);
C_ASSERT(sizeof(NTFSLX_MFT_RECORD) == 48);

typedef struct _NTFSLX_VOLUME_INFO
{
    ULONG BytesPerSector;
    ULONG SectorsPerCluster;
    ULONG BytesPerCluster;
    ULONG BytesPerFileRecord;
    ULONG BytesPerIndexRecord;
    ULONGLONG SectorCount;
    ULONGLONG ClusterCount;
    ULONGLONG MftLcn;
    ULONGLONG MftMirrLcn;
    ULONGLONG SerialNumber;
    ULONGLONG FreeClusters;
    USHORT Flags;
    UCHAR MajorVersion;
    UCHAR MinorVersion;
    USHORT VolumeLabelLength;
    WCHAR VolumeLabel[MAXIMUM_VOLUME_LABEL_LENGTH / sizeof(WCHAR)];
} NTFSLX_VOLUME_INFO, *PNTFSLX_VOLUME_INFO;

typedef struct _NTFSLX_DIR_ENTRY
{
    ULONGLONG FileReference;
    ULONGLONG ParentReference;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastDataChangeTime;
    LARGE_INTEGER LastMftChangeTime;
    LARGE_INTEGER LastAccessTime;
    ULONGLONG AllocatedSize;
    ULONGLONG DataSize;
    ULONG FileAttributes;
    UCHAR FileNameType;
    UCHAR FileNameLength;
    BOOLEAN HasChildNode;
    ULONGLONG ChildVcn;
    ULONG IndexEntryOffset;
    ULONG IndexEntryFlags;
    WCHAR FileName[256];
} NTFSLX_DIR_ENTRY, *PNTFSLX_DIR_ENTRY;

typedef NTSTATUS (*PNTFSLX_DIR_ENUM_CALLBACK)(
    _In_ const NTFSLX_DIR_ENTRY *Entry,
    _In_opt_ PVOID Context);

typedef enum _NTFSLX_DEVICE_KIND
{
    NtfslxDeviceKindControl = 1,
    NtfslxDeviceKindVolume
} NTFSLX_DEVICE_KIND;

typedef struct _NTFSLX_DEVICE_EXTENSION
{
    ULONG Signature;
    NTFSLX_DEVICE_KIND Kind;
    PDEVICE_OBJECT DeviceObject;
    PDEVICE_OBJECT StorageDevice;
    PVPB Vpb;
    ERESOURCE Resource;
    NTFSLX_VOLUME_INFO VolumeInfo;
    PUSHORT UpcaseTable;
    ULONG UpcaseTableLength;
    PNTFSLX_RUNLIST_ELEMENT MftRunlist;
    ULONG MftRunlistCount;
    /*
     * FsRtl directory-change notification package state. NotifySync is a
     * notify-package opaque handle; NotifyList holds subscribed watchers
     * posted via IRP_MN_NOTIFY_CHANGE_DIRECTORY. Both are initialized on
     * mount and consulted whenever a file is created/modified/deleted.
     */
    PNOTIFY_SYNC NotifySync;
    LIST_ENTRY NotifyList;
    BOOLEAN NotifyInitialized;
    /*
     * Per-MFT share access records. Each record owns a SHARE_ACCESS struct
     * shared by every file object that currently has the same MFT slot
     * open, plus a refcount. Used by NtfslxShareAccessAcquire/Release to
     * enforce share modes across independent opens of the same file.
     */
    LIST_ENTRY ShareList;
    /*
     * Serializes ShareList walks. Must NOT be a spinlock — IoCheckShareAccess
     * and IoSetShareAccess require IRQL <= APC_LEVEL, and we hold the lock
     * across them. We use the FAST_MUTEX which raises to APC_LEVEL only.
     */
    FAST_MUTEX ShareMutex;
    BOOLEAN ShareInitialized;
} NTFSLX_DEVICE_EXTENSION, *PNTFSLX_DEVICE_EXTENSION;

/*
 * Share-access tracking record. Lives on the volume extension's ShareList,
 * one entry per actively-open MFT index. RefCount goes to zero -> unlinked
 * and freed by the last NtfslxShareAccessRelease.
 */
typedef struct _NTFSLX_SHARE_RECORD
{
    LIST_ENTRY Link;
    ULONGLONG MftIndex;
    ULONG RefCount;
    SHARE_ACCESS ShareAccess;
} NTFSLX_SHARE_RECORD, *PNTFSLX_SHARE_RECORD;

typedef struct _NTFSLX_GLOBAL_DATA
{
    PDRIVER_OBJECT DriverObject;
    PDEVICE_OBJECT ControlDeviceObject;
} NTFSLX_GLOBAL_DATA, *PNTFSLX_GLOBAL_DATA;

typedef struct _NTFSLX_FILE_CONTEXT
{
    /*
     * Must be first. The kernel's FsRtl fast-I/O path casts
     * FileObject->FsContext to PFSRTL_COMMON_FCB_HEADER and dereferences
     * Header->Resource at offset 8 before acquiring it. We back it with a
     * real ERESOURCE so the cache manager's lazy writer and the FsRtl
     * acquire helpers have somewhere to lock.
     */
    FSRTL_COMMON_FCB_HEADER FcbHeader;
    /*
     * FileObject->SectionObjectPointer must be non-NULL for NtCreateSection
     * and the cache manager to accept the file. We own a per-FileContext
     * SECTION_OBJECT_POINTERS so every open on this file can see the same
     * instance.
     */
    SECTION_OBJECT_POINTERS SectionObjectPointers;
    ERESOURCE MainResource;
    ERESOURCE PagingIoResource;
    BOOLEAN MainResourceInitialized;
    BOOLEAN PagingIoResourceInitialized;
    ULONG Signature;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    ULONGLONG MftIndex;
    LARGE_INTEGER CurrentByteOffset;
    PNTFSLX_MFT_RECORD FileRecord;
    PNTFSLX_ATTR_RECORD DataAttribute;
    PNTFSLX_RUNLIST_ELEMENT DataRunlist;
    ULONG DataRunlistCount;
    ULONGLONG DataSize;
    ULONGLONG AllocationSize;
    BOOLEAN IsDirectory;
    BOOLEAN ResidentData;
    BOOLEAN SupportsWrite;
    /* Cache-manager integration. NULL until the first successful attach. */
    struct _NTFSLX_CACHE_RUNTIME_CONTEXT *CacheContext;
    /*
     * Full path from volume root (e.g. L"\\subdir\\file.bin"), captured at
     * open time. Used by FsRtlNotifyFullReportChange to drive directory
     * change notifications; LeafNameLength is the byte count of the final
     * component (the portion after the last backslash).
     */
    UNICODE_STRING FullPath;
    USHORT LeafNameLength;
    /*
     * Pointer to the per-MFT share-access record this open contributed to.
     * Set in NtfslxCreate, consumed in NtfslxCleanup. NULL on volume opens
     * and on opens that bypassed the share-access path.
     */
    struct _NTFSLX_SHARE_RECORD *ShareRecord;
    /*
     * Set whenever this open performs a write that grows / shrinks /
     * touches the file. Coalesces per-chunk SIZE/LAST_WRITE notifications
     * into a single FsRtlNotifyFullReportChange fired at cleanup time.
     * Without this, Explorer's directory watcher gets one notification
     * per write call (e.g. one per 4 KB chunk during a CopyFile), which
     * is wasteful and noisy.
     */
    BOOLEAN PendingDataNotify;
    /*
     * Parent MFT index resolved at Create time from the split parent path.
     * Cleanup-time delete uses this instead of re-reading the $FILE_NAME
     * attribute — the latter is ambiguous across hardlinks (each name has
     * its own parent) and is unreliable if the on-disk $FILE_NAME was left
     * stale by an older buggy writer. NTFSLX_FILE_ROOT when the value was
     * never set (volume opens, root opens).
     */
    ULONGLONG ParentMftIndex;
} NTFSLX_FILE_CONTEXT, *PNTFSLX_FILE_CONTEXT;

typedef struct _NTFSLX_CCB
{
    ULONG Signature;
    ULONG DirectoryIndex;
    BOOLEAN SearchInitialized;
    BOOLEAN DeletePending;
    UCHAR Reserved[2];
    UNICODE_STRING SearchPattern;
    PWCHAR SearchPatternBuffer;
} NTFSLX_CCB, *PNTFSLX_CCB;

struct _NTFSLX_CACHE_RUNTIME_CONTEXT;
struct _NTFSLX_QUERY_DIRECTORY_REQUEST;

typedef NTSTATUS (NTAPI *PNTFSLX_PATH_COMPONENT_LOOKUP_ROUTINE)(
    _In_ PVOID LookupContext,
    _In_opt_ PVOID ParentNode,
    _In_ PUNICODE_STRING ComponentName,
    _In_ BOOLEAN IsFinalComponent,
    _Outptr_ PVOID *ChildNode);

extern NTFSLX_GLOBAL_DATA NtfslxGlobalData;

static __inline BOOLEAN
NtfslxIsPowerOfTwo(_In_ ULONG Value)
{
    return (Value != 0) && ((Value & (Value - 1)) == 0);
}

static __inline BOOLEAN
NtfslxIsControlDevice(_In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension)
{
    return DeviceExtension->Kind == NtfslxDeviceKindControl;
}

static __inline BOOLEAN
NtfslxIsVolumeDevice(_In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension)
{
    return DeviceExtension->Kind == NtfslxDeviceKindVolume;
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath);

VOID
NtfslxInitializeFunctionPointers(
    _In_ PDRIVER_OBJECT DriverObject);

NTSTATUS
NTAPI
NtfslxFsdDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

NTSTATUS
NtfslxFileSystemControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

NTSTATUS
NtfslxDeviceControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

NTSTATUS
NtfslxQueryVolumeInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

NTSTATUS
NtfslxMountVolume(
    _In_ PDEVICE_OBJECT ControlDevice,
    _In_ PDEVICE_OBJECT StorageDevice,
    _Inout_ PVPB Vpb);

NTSTATUS
NtfslxProbeVolume(
    _In_ PDEVICE_OBJECT StorageDevice,
    _Out_ PNTFSLX_VOLUME_INFO VolumeInfo);

NTSTATUS
NtfslxBuildMftRunlist(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *Runlist,
    _Out_ PULONG RunlistCount);

NTSTATUS
NtfslxReadMftRecord(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_opt_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG RecordNumber,
    _Out_writes_bytes_(VolumeInfo->BytesPerFileRecord) PNTFSLX_MFT_RECORD Record);

NTSTATUS
NtfslxFindAttribute(
    _In_ PNTFSLX_MFT_RECORD FileRecord,
    _In_ ULONG AttributeType,
    _In_reads_opt_(NameLength) PCWSTR Name,
    _In_ ULONG NameLength,
    _Out_ PNTFSLX_ATTR_RECORD *Attribute);

NTSTATUS
NtfslxLoadVolumeMetadata(
    _In_ PDEVICE_OBJECT StorageDevice,
    _Inout_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist);

NTSTATUS
NtfslxSetVolumeDirtyFlag(
    _In_ PDEVICE_OBJECT StorageDevice,
    _Inout_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ BOOLEAN Dirty);

NTSTATUS
NtfslxDeviceIoControl(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ ULONG ControlCode,
    _In_opt_ PVOID InputBuffer,
    _In_ ULONG InputBufferSize,
    _Out_writes_bytes_to_opt_(*OutputBufferSize, *OutputBufferSize) PVOID OutputBuffer,
    _Inout_opt_ PULONG OutputBufferSize,
    _In_ BOOLEAN Override);

NTSTATUS
NtfslxReadDisk(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ LONGLONG StartingOffset,
    _In_ ULONG Length,
    _In_ ULONG SectorSize,
    _Out_writes_bytes_(Length) PUCHAR Buffer,
    _In_ BOOLEAN Override);

ULONG
NtfslxRecordSizeFromClusters(
    _In_ CHAR ClustersPerRecord,
    _In_ ULONG ClusterSize);

PUSHORT
NtfslxGenerateDefaultUpcase(
    VOID);

NTSTATUS
NtfslxPostReadMstFixup(
    _Inout_ PNTFSLX_RECORD_HEADER Record,
    _In_ ULONG Size);

int
NtfslxUcsNCompare(
    _In_reads_(Length) const USHORT *String1,
    _In_reads_(Length) const USHORT *String2,
    _In_ SIZE_T Length);

int
NtfslxUcsNCaseCompare(
    _In_reads_(Length) const USHORT *String1,
    _In_reads_(Length) const USHORT *String2,
    _In_ SIZE_T Length,
    _In_reads_(UpcaseLength) const USHORT *UpcaseTable,
    _In_ ULONG UpcaseLength);

BOOLEAN
NtfslxAreNamesEqual(
    _In_reads_(String1Length) const USHORT *String1,
    _In_ SIZE_T String1Length,
    _In_reads_(String2Length) const USHORT *String2,
    _In_ SIZE_T String2Length,
    _In_ BOOLEAN IgnoreCase,
    _In_reads_(UpcaseLength) const USHORT *UpcaseTable,
    _In_ ULONG UpcaseLength);

LONG
NtfslxCollateNames(
    _In_reads_(Name1Length) const USHORT *Name1,
    _In_ ULONG Name1Length,
    _In_reads_(Name2Length) const USHORT *Name2,
    _In_ ULONG Name2Length,
    _In_ LONG InvalidCharReturn,
    _In_ BOOLEAN IgnoreCase,
    _In_reads_(UpcaseLength) const USHORT *UpcaseTable,
    _In_ ULONG UpcaseLength);

BOOLEAN
NTAPI
NtfslxPathIsAbsolute(
    _In_ PCUNICODE_STRING Path);

BOOLEAN
NTAPI
NtfslxPathIsDotComponent(
    _In_ PUNICODE_STRING Component);

BOOLEAN
NTAPI
NtfslxPathIsDotDotComponent(
    _In_ PUNICODE_STRING Component);

BOOLEAN
NTAPI
NtfslxPathGetNextComponent(
    _In_ PCUNICODE_STRING Path,
    _Inout_ PUSHORT Offset,
    _Out_ PUNICODE_STRING Component,
    _Out_opt_ PBOOLEAN IsLastComponent);

NTSTATUS
NTAPI
NtfslxTraversePath(
    _In_ PCUNICODE_STRING Path,
    _In_ PVOID RootNode,
    _In_ PNTFSLX_PATH_COMPONENT_LOOKUP_ROUTINE LookupComponent,
    _In_ PVOID LookupContext,
    _Out_opt_ PVOID *ResolvedNode,
    _Out_opt_ PUNICODE_STRING FinalComponent,
    _Out_opt_ PULONG ComponentCount);

NTSTATUS
NtfslxEnumerateDirectoryRoot(
    _In_reads_bytes_(RootLength) const VOID *IndexRoot,
    _In_ ULONG RootLength,
    _In_ PNTFSLX_DIR_ENUM_CALLBACK Callback,
    _In_opt_ PVOID Context);

NTSTATUS
NtfslxEnumerateDirectoryFromMftRecord(
    _In_ const VOID *DirectoryMftRecord,
    _In_ ULONG DirectoryRecordLength,
    _In_ PNTFSLX_DIR_ENUM_CALLBACK Callback,
    _In_opt_ PVOID Context);

NTSTATUS
NtfslxBuildIndexAllocationRunlist(
    _In_ PNTFSLX_ATTR_RECORD IndexAllocationAttribute,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *Runlist,
    _Out_ PULONG RunlistCount);

NTSTATUS
NtfslxReadIndexAllocationBlock(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT IndexAllocationRunlist,
    _In_ ULONGLONG Vcn,
    _Out_writes_bytes_(VolumeInfo->BytesPerIndexRecord) PVOID Buffer);

NTSTATUS
NtfslxValidateIndexAllocationBlock(
    _Inout_updates_bytes_(IndexBlockSize) PVOID Buffer,
    _In_ ULONG IndexBlockSize,
    _In_ ULONG SectorSize,
    _In_ ULONGLONG ExpectedVcn);

NTSTATUS
NtfslxEnumerateIndexAllocationTree(
    _In_reads_bytes_(RootLength) const VOID *IndexRootBuffer,
    _In_ ULONG RootLength,
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_opt_ PNTFSLX_RUNLIST_ELEMENT IndexAllocationRunlist,
    _In_ ULONG IndexAllocationRunlistCount,
    _In_ PNTFSLX_DIR_ENUM_CALLBACK Callback,
    _In_opt_ PVOID Context);

NTSTATUS
NtfslxCopyDirectoryEntryToBothInformation(
    _In_ const NTFSLX_DIR_ENTRY *Entry,
    _Inout_updates_bytes_(BufferLength) PFILE_BOTH_DIR_INFORMATION Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG NextOffset);

NTSTATUS
NtfslxQueryDirectoryEnumerateFromMftRecord(
    _In_ const VOID *DirectoryMftRecord,
    _In_ ULONG DirectoryRecordLength,
    _In_ const struct _NTFSLX_QUERY_DIRECTORY_REQUEST *Request,
    _Out_opt_ PULONG BytesWritten,
    _Out_opt_ PULONG NextIndex);

NTSTATUS
NtfslxOpenFileObjectByIndex(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONGLONG MftIndex,
    _In_ ULONG AttributeType,
    _In_opt_ PCWSTR StreamName,
    _In_ ULONG StreamNameLength);

VOID
NtfslxCloseFileObject(
    _Inout_ PFILE_OBJECT FileObject);

/*
 * Copy the supplied path (absolute, relative to the volume root) into
 * FileContext->FullPath and compute LeafNameLength (bytes). Idempotent:
 * rebuilds the stored path on each call. Called from the create path
 * once the FileObject is wired to the context.
 */
NTSTATUS
NtfslxSetFileContextFullPath(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ PCUNICODE_STRING Path);

/*
 * Fire an FsRtl directory-change notification using the FileContext's
 * stored full path. Safe to call with FileContext == NULL or missing path
 * (becomes a no-op); Filter is FILE_NOTIFY_CHANGE_*, Action is FILE_ACTION_*.
 */
VOID
NtfslxNotifyReportChange(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_opt_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ ULONG Filter,
    _In_ ULONG Action);

/*
 * IRP_MN_NOTIFY_CHANGE_DIRECTORY handler. Hands the IRP to FsRtl's notify
 * package; completion happens asynchronously when a matching change fires.
 */
NTSTATUS
NtfslxNotifyChangeDirectory(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

/*
 * Cancel any pending IRP_MN_NOTIFY_CHANGE_DIRECTORY IRPs associated with
 * the supplied CCB. Called from cleanup so stale watch IRPs get completed.
 */
VOID
NtfslxNotifyCleanupCcb(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_opt_ PNTFSLX_CCB Ccb);

/* ========================================================================
 * Share-access enforcement (share.c)
 * ======================================================================== */

VOID
NtfslxShareInitialize(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt);

NTSTATUS
NtfslxShareAccessAcquire(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG MftIndex,
    _In_ ACCESS_MASK DesiredAccess,
    _In_ ULONG ShareAccess,
    _In_ PFILE_OBJECT FileObject,
    _Outptr_ struct _NTFSLX_SHARE_RECORD **OutRecord);

VOID
NtfslxShareAccessRelease(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_opt_ struct _NTFSLX_SHARE_RECORD *Record,
    _In_opt_ PFILE_OBJECT FileObject);

/*
 * Add a named, empty resident $DATA attribute (alternate data stream) to
 * the file at MftIndex. Reads the existing MFT record, inserts a new
 * attribute with the supplied stream name and zero bytes of value, and
 * writes the record back. Returns STATUS_OBJECT_NAME_COLLISION if a
 * stream with that name already exists.
 */
NTSTATUS
NtfslxAddNamedDataStream(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG MftIndex,
    _In_reads_(NameLength) PCWSTR Name,
    _In_ ULONG NameLength);

NTSTATUS
NtfslxReadFileObject(
    _In_ PFILE_OBJECT FileObject,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _In_ ULONG Length,
    _In_ PLARGE_INTEGER ByteOffset,
    _In_ BOOLEAN PagingIo,
    _Out_opt_ PULONG BytesRead);

NTSTATUS
NtfslxRefreshFileObjectMetadata(
    _In_ PFILE_OBJECT FileObject);

NTSTATUS
NtfslxWriteFileObject(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length,
    _In_ PLARGE_INTEGER ByteOffset,
    _Out_opt_ PULONG BytesWritten);

NTSTATUS
NtfslxValidateOpenFileObject(
    _In_ PFILE_OBJECT FileObject,
    _Outptr_ PNTFSLX_FILE_CONTEXT *FileContext);

NTSTATUS
NtfslxEnsureCacheInitialized(
    _In_ PFILE_OBJECT FileObject);

NTSTATUS
NtfslxWriteFileObjectConservative(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length,
    _In_ PLARGE_INTEGER ByteOffset,
    _Out_opt_ PULONG BytesWritten);

NTSTATUS
NtfslxExtendedWrite(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(Length) const VOID *Buffer,
    _In_ ULONG Length,
    _In_ PLARGE_INTEGER ByteOffset,
    _Out_opt_ PULONG BytesWritten);

NTSTATUS
NtfslxUpdateFileNameSize(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ ULONGLONG NewDataSize,
    _In_ ULONGLONG NewAllocatedSize);

NTSTATUS
NtfslxSetFileSize(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ ULONGLONG NewSize);

/* ========================================================================
 * Self-test (selftest.c) - RAM-only internal checks, no storage device
 * ======================================================================== */

NTSTATUS
NtfslxSelfTestRunAll(
    _Out_ PNTFSLX_SELFTEST_RESULT Result);

/* Accessors for internal MFT-record helpers so selftest.c can call them. */
NTSTATUS
NtfslxInsertAttributeRecord(
    _Inout_ PNTFSLX_MFT_RECORD MftRecord,
    _In_ ULONG RecordSize,
    _In_ ULONG Type,
    _In_reads_opt_(NameLength) PCWSTR Name,
    _In_ ULONG NameLength,
    _In_reads_bytes_opt_(ValueLength) const VOID *Value,
    _In_ ULONG ValueLength,
    _Outptr_opt_ PNTFSLX_ATTR_RECORD *OutAttribute);

NTSTATUS
NtfslxResizeAttributeRecord(
    _Inout_ PNTFSLX_MFT_RECORD MftRecord,
    _Inout_ PNTFSLX_ATTR_RECORD Attribute,
    _In_ ULONG NewSize);

NTSTATUS
NtfslxRemoveAttributeRecord(
    _Inout_ PNTFSLX_MFT_RECORD MftRecord,
    _In_ PNTFSLX_ATTR_RECORD Attribute);

NTSTATUS
NTAPI
NtfslxValidateMountVpb(
    _In_ PVPB Vpb,
    _In_ PDEVICE_OBJECT RealDevice,
    _In_ PDEVICE_OBJECT FileSystemDeviceObject);

NTSTATUS
NTAPI
NtfslxValidateOpenFileObjectVpb(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PNTFSLX_DEVICE_EXTENSION DeviceExtension);

NTSTATUS
NTAPI
NtfslxValidateCloseFileObjectVpb(
    _In_ PFILE_OBJECT FileObject,
    _In_opt_ PNTFSLX_DEVICE_EXTENSION DeviceExtension);

NTSTATUS
NTAPI
NtfslxValidateDismountVpb(
    _In_ PVPB Vpb,
    _In_opt_ PDEVICE_OBJECT RealDevice);

NTSTATUS
NTAPI
NtfslxCacheRuntimeCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointers,
    _In_ PCC_FILE_SIZES FileSizes,
    _In_ BOOLEAN ReadOnly,
    _In_ ULONG ReadAheadGranularity,
    _Outptr_ struct _NTFSLX_CACHE_RUNTIME_CONTEXT **CacheContext);

NTSTATUS
NTAPI
NtfslxCacheRuntimeAttach(
    _In_ PFILE_OBJECT FileObject,
    _In_ PCC_FILE_SIZES FileSizes,
    _In_ BOOLEAN ReadOnly,
    _In_ ULONG ReadAheadGranularity,
    _Outptr_ struct _NTFSLX_CACHE_RUNTIME_CONTEXT **CacheContext);

NTSTATUS
NTAPI
NtfslxCacheRuntimeCopyRead(
    _In_ struct _NTFSLX_CACHE_RUNTIME_CONTEXT *CacheContext,
    _In_ PLARGE_INTEGER ByteOffset,
    _In_ ULONG Length,
    _In_ BOOLEAN Wait,
    _Out_writes_bytes_(Length) PVOID Buffer,
    _Inout_ PIO_STATUS_BLOCK IoStatus);

NTSTATUS
NTAPI
NtfslxCacheRuntimeSetFileSizes(
    _In_ struct _NTFSLX_CACHE_RUNTIME_CONTEXT *CacheContext,
    _In_ PCC_FILE_SIZES NewFileSizes);

NTSTATUS
NTAPI
NtfslxCacheRuntimeValidate(
    _In_ struct _NTFSLX_CACHE_RUNTIME_CONTEXT *CacheContext);

NTSTATUS
NTAPI
NtfslxCacheRuntimeDestroy(
    _Inout_opt_ struct _NTFSLX_CACHE_RUNTIME_CONTEXT *CacheContext);

/* ========================================================================
 * MST fixup (mst.c) - pre-write and post-write fixup
 * ======================================================================== */

NTSTATUS
NtfslxPreWriteMstFixup(
    _Inout_ PNTFSLX_RECORD_HEADER Record,
    _In_ ULONG Size);

VOID
NtfslxPostWriteMstFixup(
    _Inout_ PNTFSLX_RECORD_HEADER Record);

/* ========================================================================
 * Collation (collate.c) - generic collation dispatch
 * ======================================================================== */

LONG
NtfslxCollate(
    _In_ ULONG CollationRule,
    _In_reads_bytes_(Data1Length) const VOID *Data1,
    _In_ ULONG Data1Length,
    _In_reads_bytes_(Data2Length) const VOID *Data2,
    _In_ ULONG Data2Length,
    _In_reads_(UpcaseLength) const USHORT *UpcaseTable,
    _In_ ULONG UpcaseLength);

/* ========================================================================
 * LogFile (logfile.c) - journal validation
 * ======================================================================== */

NTSTATUS
NtfslxCheckLogFile(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_opt_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _Out_ PBOOLEAN IsClean);

NTSTATUS
NtfslxFillLogFile(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist);

/* ========================================================================
 * Bitmap (bitmap.c) - on-disk bitmap operations
 * ======================================================================== */

NTSTATUS
NtfslxBitmapSetBitsInRun(
    _Inout_updates_bytes_(BitmapLength) PUCHAR Bitmap,
    _In_ ULONG BitmapLength,
    _In_ ULONGLONG StartBit,
    _In_ ULONGLONG Count,
    _In_ UCHAR Value);

BOOLEAN
NtfslxBitmapTestBit(
    _In_reads_bytes_(BitmapLength) const UCHAR *Bitmap,
    _In_ ULONG BitmapLength,
    _In_ ULONGLONG Bit);

ULONGLONG
NtfslxBitmapCountFreeBits(
    _In_reads_bytes_(BitmapLength) const UCHAR *Bitmap,
    _In_ ULONG BitmapLength);

/* ========================================================================
 * LZNT1 compression (compress.c) - decompression
 * ======================================================================== */

NTSTATUS
NtfslxDecompressLznt1(
    _In_reads_bytes_(CompressedLength) const VOID *CompressedBuffer,
    _In_ ULONG CompressedLength,
    _Out_writes_bytes_(UncompressedLength) PVOID UncompressedBuffer,
    _In_ ULONG UncompressedLength,
    _Out_ PULONG FinalUncompressedSize);

/* ========================================================================
 * Runlist operations (runlist.c) - merge, truncate, build
 * ======================================================================== */

NTSTATUS
NtfslxRunlistsMerge(
    _In_reads_(DestCount) PNTFSLX_RUNLIST_ELEMENT DestRunlist,
    _In_ ULONG DestCount,
    _In_reads_(SrcCount) const NTFSLX_RUNLIST_ELEMENT *SrcRunlist,
    _In_ ULONG SrcCount,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *MergedRunlist,
    _Out_ PULONG MergedCount);

NTSTATUS
NtfslxRunlistTruncate(
    _Inout_ PNTFSLX_RUNLIST_ELEMENT *Runlist,
    _Inout_ PULONG RunlistCount,
    _In_ LONGLONG NewLength);

LONG
NtfslxGetSizeForMappingPairs(
    _In_ ULONG ClusterSizeBits,
    _In_reads_(RunlistCount) const NTFSLX_RUNLIST_ELEMENT *Runlist,
    _In_ ULONG RunlistCount,
    _In_ LONGLONG FirstVcn,
    _In_ LONGLONG LastVcn);

NTSTATUS
NtfslxMappingPairsBuild(
    _In_ ULONG ClusterSizeBits,
    _Out_writes_bytes_(DstLength) PUCHAR Dst,
    _In_ LONG DstLength,
    _In_reads_(RunlistCount) const NTFSLX_RUNLIST_ELEMENT *Runlist,
    _In_ ULONG RunlistCount,
    _In_ LONGLONG FirstVcn,
    _In_ LONGLONG LastVcn,
    _Out_opt_ PLONGLONG StopVcn);

BOOLEAN
NtfslxRunlistIsSparse(
    _In_reads_(RunlistCount) const NTFSLX_RUNLIST_ELEMENT *Runlist,
    _In_ ULONG RunlistCount);

/* ========================================================================
 * Directory lookup (dirlookup.c) - name-based inode lookup
 * ======================================================================== */

NTSTATUS
NtfslxLookupInodeByName(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONG MftRunlistCount,
    _In_ ULONGLONG DirectoryMftIndex,
    _In_reads_(NameLength) const WCHAR *Name,
    _In_ ULONG NameLength,
    _In_ BOOLEAN IgnoreCase,
    _In_reads_(UpcaseLength) const USHORT *UpcaseTable,
    _In_ ULONG UpcaseLength,
    _Out_ PULONGLONG FoundMftReference);

/* ========================================================================
 * Security (security.c) - descriptor parsing helpers
 * ======================================================================== */

NTSTATUS
NtfslxReadSecurityDescriptor(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG MftIndex,
    _Outptr_ PSECURITY_DESCRIPTOR *SecurityDescriptor,
    _Out_ PULONG SecurityDescriptorLength);

/* ========================================================================
 * Reparse (reparse.c) - reparse point reading
 * ======================================================================== */

NTSTATUS
NtfslxReadReparsePoint(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG MftIndex,
    _Out_ PULONG ReparseTag,
    _Outptr_opt_ PVOID *ReparseData,
    _Out_opt_ PUSHORT ReparseDataLength);

NTSTATUS
NtfslxSetReparsePoint(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength);

NTSTATUS
NtfslxGetReparsePoint(
    _In_ PFILE_OBJECT FileObject,
    _Out_writes_bytes_to_(OutputLength, *ReturnLength) PVOID OutputBuffer,
    _In_ ULONG OutputLength,
    _Out_ PULONG ReturnLength);

NTSTATUS
NtfslxDeleteReparsePoint(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength);

/* ========================================================================
 * Extended attributes (ea.c) - EA reading
 * ======================================================================== */

NTSTATUS
NtfslxReadEaInformation(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG MftIndex,
    _Out_ PNTFSLX_EA_INFORMATION EaInfo);

NTSTATUS
NtfslxReadEaData(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG MftIndex,
    _Outptr_ PVOID *EaBuffer,
    _Out_ PULONG EaBufferLength);

NTSTATUS
NtfslxQueryEa(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

NTSTATUS
NtfslxSetEa(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

/* ========================================================================
 * Metadata helpers (metadata.c) - mapping pairs and mapped read
 * ======================================================================== */

NTSTATUS
NtfslxMappingPairsDecompress(
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_ATTR_RECORD Attribute,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *Runlist,
    _Out_ PULONG RunlistCount);

NTSTATUS
NtfslxReadMappedAttributeData(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ ULONGLONG Offset,
    _In_ ULONG Length,
    _Out_writes_bytes_(Length) PUCHAR Buffer);

/* ========================================================================
 * Disk write (diskwrite.c) - synchronous disk write
 * ======================================================================== */

NTSTATUS
NtfslxWriteDisk(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ LONGLONG StartingOffset,
    _In_ ULONG Length,
    _In_ ULONG SectorSize,
    _In_reads_bytes_(Length) PUCHAR Buffer,
    _In_ BOOLEAN Override);

/* ========================================================================
 * MFT write (mftwrite.c) - MFT record write, allocate, free
 * ======================================================================== */

NTSTATUS
NtfslxWriteMftRecord(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG RecordNumber,
    _Inout_ PNTFSLX_MFT_RECORD Record);

NTSTATUS
NtfslxAllocateMftRecord(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONG MftRunlistCount,
    _Out_ PULONGLONG OutRecordNumber);

/*
 * Extended allocate that may grow the $MFT data stream on demand. DevExt
 * is used to update DeviceExtension->MftRunlist in place when the $MFT is
 * extended so subsequent reads see the new clusters.
 */
NTSTATUS
NtfslxAllocateMftRecordEx(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _Out_ PULONGLONG OutRecordNumber);

NTSTATUS
NtfslxFreeMftRecord(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG RecordNumber);

/* ========================================================================
 * Cluster allocation (clusteralloc.c) - allocate/free volume clusters
 * ======================================================================== */

NTSTATUS
NtfslxAllocateClusters(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG Count,
    _In_ ULONGLONG HintLcn,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *OutRunlist,
    _Out_ PULONG OutRunlistCount);

NTSTATUS
NtfslxFreeClusters(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ PNTFSLX_RUNLIST_ELEMENT Runlist,
    _In_ ULONG RunlistCount);

/* ========================================================================
 * Attribute modification (attribmod.c) - in-record attribute manipulation
 * ======================================================================== */

NTSTATUS
NtfslxResizeAttributeRecord(
    _Inout_ PNTFSLX_MFT_RECORD MftRecord,
    _Inout_ PNTFSLX_ATTR_RECORD Attribute,
    _In_ ULONG NewSize);

NTSTATUS
NtfslxInsertAttributeRecord(
    _Inout_ PNTFSLX_MFT_RECORD MftRecord,
    _In_ ULONG RecordSize,
    _In_ ULONG Type,
    _In_reads_opt_(NameLength) PCWSTR Name,
    _In_ ULONG NameLength,
    _In_reads_bytes_opt_(ValueLength) const VOID *Value,
    _In_ ULONG ValueLength,
    _Outptr_opt_ PNTFSLX_ATTR_RECORD *OutAttribute);

NTSTATUS
NtfslxRemoveAttributeRecord(
    _Inout_ PNTFSLX_MFT_RECORD MftRecord,
    _In_ PNTFSLX_ATTR_RECORD Attribute);

/* ========================================================================
 * Index modification (indexmod.c) - directory B-tree insert/remove
 * ======================================================================== */

NTSTATUS
NtfslxIndexInsertFileName(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG DirectoryMftIndex,
    _In_reads_(FileNameLength) const WCHAR *FileName,
    _In_ ULONG FileNameLength,
    _In_ UCHAR FileNameType,
    _In_ ULONGLONG FileMftReference,
    _In_ ULONG FileAttributes,
    _In_ ULONGLONG FileDataSize,
    _In_ ULONGLONG FileAllocSize,
    _In_ PLARGE_INTEGER CreationTime,
    _In_ PLARGE_INTEGER LastDataChangeTime,
    _In_ PLARGE_INTEGER LastMftChangeTime,
    _In_ PLARGE_INTEGER LastAccessTime);

NTSTATUS
NtfslxIndexRemoveFileName(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG DirectoryMftIndex,
    _In_reads_(FileNameLength) const WCHAR *FileName,
    _In_ ULONG FileNameLength);

NTSTATUS
NtfslxIndexUpdateFileName(
    _In_ PNTFSLX_DEVICE_EXTENSION DevExt,
    _In_ ULONGLONG DirectoryMftIndex,
    _In_reads_(FileNameLength) const WCHAR *FileName,
    _In_ ULONG FileNameLength,
    _In_ ULONGLONG NewDataSize,
    _In_ ULONGLONG NewAllocatedSize,
    _In_ PLARGE_INTEGER CreationTime,
    _In_ PLARGE_INTEGER LastDataChangeTime,
    _In_ PLARGE_INTEGER LastMftChangeTime,
    _In_ PLARGE_INTEGER LastAccessTime);

/* ========================================================================
 * File creation (filecreate.c) - create/delete orchestration
 * ======================================================================== */

NTSTATUS
NtfslxCreateNewFile(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG ParentMftIndex,
    _In_reads_(FileNameLength) const WCHAR *FileName,
    _In_ ULONG FileNameLength,
    _In_ BOOLEAN IsDirectory,
    _Out_ PULONGLONG OutMftIndex);

NTSTATUS
NtfslxCheckDeleteAllowed(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG MftIndex);

NTSTATUS
NtfslxDeleteFile(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG ParentMftIndex,
    _In_ ULONGLONG MftIndex,
    _In_reads_(FileNameLength) const WCHAR *FileName,
    _In_ ULONG FileNameLength);

#endif /* _NTFSLX_H_ */
