
#ifndef _NTFSLIB_NEW_H_
#define _NTFSLIB_NEW_H_

#ifdef NTFSLIB_PORTABLE
#include "ntfsenv.h"
#elif !defined(_WINNT_)
#include <ntdef.h>
#endif

#include "ntfsattribdef.h"

#pragma pack(push, 1)
struct BootSector
{
    UCHAR  JumpInstruction[3];     // Offset 0x00, Size 3
    UCHAR  OEM_ID[8];              // Offset 0x03, Size 8
    UINT16 BytesPerSector;         // Offset 0x0B, Size 2
    UINT8  SectorsPerCluster;      // Offset 0x0D, Size 1
    UCHAR  Reserved0[7];           // Offset 0x0E, Size 7
    UINT8  MediaDescriptor;        // Offset 0x15, Size 1
    UCHAR  Reserved1[2];           // Offset 0x16, Size 2
    UINT16 SectorsPerTrack;        // Offset 0x18, Size 2
    UINT16 NumberOfHeads;          // Offset 0x1A, Size 2
    UINT32 HiddenSectors;          // Offset 0x1C, Size 4
    UCHAR  Reserved3[4];           // Offset 0x20, Size 4
    UINT32 Unknown;                // Offset 0x24, Size 4
    UINT64 SectorsInVolume;        // Offset 0x28, Size 8
    UINT64 MFTLCN;                 // Offset 0x30, Size 8
    UINT64 MFTMirrLCN;             // Offset 0x38, Size 8
    INT8   ClustersPerFileRecord;  // Offset 0x40, Size 4
    UCHAR  Reserved4[3];
    INT8   ClustersPerIndexRecord; // Offset 0x44, Size 4
    UCHAR  Reserved5[3];
    UINT64 SerialNumber;           // Offset 0x48, Size 8
    UINT32 Checksum;               // Offset 0x50, Size 4
    UCHAR  BootStrap[426];
    USHORT EndSector;
};
#pragma pack(pop)
typedef struct BootSector BootSector;
typedef struct BootSector* PBootSector;

/* NTFS file record numbers */
enum FileRecordNumbers
{
    _MFT     = 0,
    _MFTMirr = 1,
    _LogFile = 2,
    _Volume  = 3,
    _AttrDef = 4,
    _Root    = 5,
    _Bitmap  = 6,
    _Boot    = 7,
    _BadClus = 8,
    _Secure  = 9,
    _UpCase  = 10,
    _Extend  = 11,
};

/* File records up to and including this number are reserved for
 * NTFS metadata files and are hidden from directory enumeration.
 */
#define NTFS_LAST_RESERVED_FILE_RECORD 26

/* File record flags */
#define FR_IN_USE        0x01
#define FR_IS_DIRECTORY  0x02
#define FR_IS_EXTENSION  0x04
#define FR_SPECIAL_INDEX 0x08

/* NTFS file permission flags */
#define FILE_PERM_READONLY   0x0001
#define FILE_PERM_HIDDEN     0x0002
#define FILE_PERM_SYSTEM     0x0004
#define FILE_PERM_ARCHIVE    0x0020
#define FILE_PERM_DEVICE     0x0040
#define FILE_PERM_NORMAL     0x0080
#define FILE_PERM_TEMP       0x0100
#define FILE_PERM_SPARSE     0x0200
#define FILE_PERM_REPARSE_PT 0x0400
#define FILE_PERM_COMPRESSED 0x0800
#define FILE_PERM_OFFLINE    0x1000
#define FILE_PERM_NOT_INDXED 0x2000
#define FILE_PERM_ENCRYPTED  0x4000

typedef struct
{
    UCHAR  TypeID[4];              // Offset 0x00, Size 4 ('FILE' or 'INDX')
    UINT16 UpdateSequenceOffset;   // Offset 0x04, Size 2
    UINT16 SizeOfUpdateSequence;   // Offset 0x06, Size 2
    UINT64 LogFileSequenceNumber;  // Offset 0x08, Size 8
} NTFSRecordHeader, *PNTFSRecordHeader;

typedef struct
{
    NTFSRecordHeader Header;       // Offset 0x00, Size 16
    UINT16 SequenceNumber;         // Offset 0x10, Size 2
    UINT16 HardLinkCount;          // Offset 0x12, Size 2
    UINT16 AttributeOffset;        // Offset 0x14, Size 2
    UINT16 Flags;                  // Offset 0x16, Size 2
    UINT32 ActualSize;             // Offset 0x18, Size 4
    UINT32 AllocatedSize;          // Offset 0x1C, Size 4
    UINT64 BaseFileRecord;         // Offset 0x20, Size 8
    UINT16 NextAttributeID;        // Offset 0x28, Size 2
    UINT16 Padding;
    UINT32 MFTRecordNumber;        // Offset 0x2C, Size 4
} FileRecordHeader, *PFileRecordHeader;

typedef struct
{
    NTFSRecordHeader RecordHeader;
    ULONGLONG VCN;
    IndexNodeHeader IndexHeader;
} IndexBuffer, *PIndexBuffer;

// =========================
// NTFS C API
// =========================

/* Forward declarations */
typedef struct NtfsDirectory NtfsDirectory;
typedef NtfsDirectory* PNtfsDirectory;

typedef struct NtfsFileRecord NtfsFileRecord;
typedef NtfsFileRecord* PNtfsFileRecord;

typedef struct NtfsLogFileService NtfsLogFileService;
typedef NtfsLogFileService* PNtfsLogFileService;

typedef struct NtfsMasterFileTable NtfsMasterFileTable;
typedef NtfsMasterFileTable* PNtfsMasterFileTable;

typedef struct NtfsVolume NtfsVolume;
typedef NtfsVolume* PNtfsVolume;

#define NTFS_MAX_FILE_NAME_LENGTH 255
#define NTFS_MAXIMUM_REPARSE_DATA_BUFFER_SIZE (16 * 1024)
#define NTFS_IO_REPARSE_TAG_MOUNT_POINT ((UINT32)0xA0000003)
#define NTFS_IO_REPARSE_TAG_SYMLINK ((UINT32)0xA000000C)
#define NTFS_IO_REPARSE_TAG_WOF ((UINT32)0x80000017)
#define NTFS_SYMLINK_FLAG_RELATIVE ((UINT32)0x00000001)
#define NTFS_WOF_PROVIDER_FILE ((UINT32)2)
#define NTFS_WOF_COMPRESSION_XPRESS4K ((UINT32)0)
#define NTFS_WOF_COMPRESSION_LZX ((UINT32)1)
#define NTFS_WOF_COMPRESSION_XPRESS8K ((UINT32)2)
#define NTFS_WOF_COMPRESSION_XPRESS16K ((UINT32)3)
#define NTFS_EA_FLAG_NEED_EA ((UINT8)0x80)
#define NTFS_BASIC_INFO_CREATION_TIME ((UINT32)0x00000001)
#define NTFS_BASIC_INFO_LAST_ACCESS_TIME ((UINT32)0x00000002)
#define NTFS_BASIC_INFO_LAST_WRITE_TIME ((UINT32)0x00000004)
#define NTFS_BASIC_INFO_CHANGE_TIME ((UINT32)0x00000008)
#define NTFS_BASIC_INFO_FILE_ATTRIBUTES ((UINT32)0x00000010)
#define NTFS_BASIC_INFO_ALL_FIELDS ((UINT32)0x0000001f)
#define NTFS_AUTOMATIC_TIMESTAMP_FIELDS \
    (NTFS_BASIC_INFO_LAST_ACCESS_TIME | \
     NTFS_BASIC_INFO_LAST_WRITE_TIME | \
     NTFS_BASIC_INFO_CHANGE_TIME)

/*
 * Platform-neutral directory result. Lengths are counts of UTF-16 code
 * units; Name and ShortName are always NUL terminated by the library.
 */
typedef struct NtfsDirectoryEntry
{
    UINT64 FileReference;
    UINT64 CreationTime;
    UINT64 LastAccessTime;
    UINT64 LastWriteTime;
    UINT64 ChangeTime;
    UINT64 EndOfFile;
    UINT64 AllocationSize;
    UINT32 FileAttributes;
    UINT16 EaSize;
    UINT32 ReparseTag;
    UINT16 NameLength;
    UINT8 ShortNameLength;
    WCHAR Name[NTFS_MAX_FILE_NAME_LENGTH + 1];
    WCHAR ShortName[13];
} NtfsDirectoryEntry, *PNtfsDirectoryEntry;

/*
 * Platform-neutral counterpart of FILE_ALLOCATED_RANGE_BUFFER. Offsets and
 * lengths are bytes. Sparse range queries return logical allocation ranges,
 * not physical disk locations.
 */
typedef struct NtfsAllocatedRange
{
    UINT64 FileOffset;
    UINT64 Length;
} NtfsAllocatedRange, *PNtfsAllocatedRange;

/*
 * Platform-neutral counterpart of one RETRIEVAL_POINTERS_BUFFER extent.
 * NextVcn is the exclusive logical-cluster boundary. Lcn is the physical
 * starting cluster, or -1 for a sparse/unallocated extent.
 */
typedef struct NtfsRetrievalExtent
{
    UINT64 NextVcn;
    LONGLONG Lcn;
} NtfsRetrievalExtent, *PNtfsRetrievalExtent;

/*
 * Platform-neutral projection of NTFS_VOLUME_DATA_BUFFER plus the version
 * fields returned by NTFS_EXTENDED_VOLUME_DATA. Counts remain 64-bit all the
 * way from the boot sector so large volumes are not truncated by a frontend.
 */
typedef struct NtfsVolumeInformation
{
    UINT64 VolumeSerialNumber;
    UINT64 NumberSectors;
    UINT64 TotalClusters;
    UINT64 FreeClusters;
    UINT64 TotalReserved;
    UINT32 BytesPerSector;
    UINT32 BytesPerCluster;
    UINT32 BytesPerFileRecordSegment;
    UINT32 ClustersPerFileRecordSegment;
    UINT64 MftValidDataLength;
    UINT64 MftStartLcn;
    UINT64 Mft2StartLcn;
    UINT64 MftZoneStart;
    UINT64 MftZoneEnd;
    UINT16 MajorVersion;
    UINT16 MinorVersion;
} NtfsVolumeInformation, *PNtfsVolumeInformation;

/*
 * One logical NTFS $DATA stream. Name is the raw attribute name without the
 * leading ':' or trailing ':$DATA' presentation syntax; NameLength is a
 * count of UTF-16 code units and Name is always NUL terminated.
 */
typedef struct NtfsDataStreamInformation
{
    UINT64 DataSize;
    UINT64 AllocationSize;
    UINT16 NameLength;
    WCHAR Name[NTFS_MAX_FILE_NAME_LENGTH + 1];
} NtfsDataStreamInformation, *PNtfsDataStreamInformation;

/*
 * Platform-neutral projection of $STANDARD_INFORMATION. Fields is a mask of
 * NTFS_BASIC_INFO_* values, allowing callers to update an explicit subset
 * without assigning special meanings to otherwise valid 64-bit timestamps.
 */
typedef struct NtfsFileBasicInformation
{
    UINT32 Fields;
    UINT64 CreationTime;
    UINT64 LastAccessTime;
    UINT64 LastWriteTime;
    UINT64 ChangeTime;
    UINT32 FileAttributes;
} NtfsFileBasicInformation, *PNtfsFileBasicInformation;

/*
 * Validated view over a Microsoft symlink or mount-point reparse buffer.
 * Names point at UTF-16LE bytes, lengths are code-unit counts, and the
 * pointers remain valid only while the caller-owned raw buffer remains alive.
 */
typedef struct NtfsReparseNameView
{
    UINT32 ReparseTag;
    UINT32 Flags;
    const UCHAR* SubstituteName;
    ULONG SubstituteNameLength;
    const UCHAR* PrintName;
    ULONG PrintNameLength;
} NtfsReparseNameView, *PNtfsReparseNameView;

/*
 * Validated zero-copy view over one entry in a caller-owned raw NTFS $EA
 * buffer. Names are counted single-byte strings; values are opaque bytes.
 */
typedef struct NtfsExtendedAttributeView
{
    UINT8 Flags;
    UINT8 NameLength;
    UINT16 ValueLength;
    const UCHAR* Name;
    const UCHAR* Value;
} NtfsExtendedAttributeView, *PNtfsExtendedAttributeView;

#define NTFS_EA_UPDATE_SET ((UINT32)0)
#define NTFS_EA_UPDATE_REMOVE ((UINT32)1)

/*
 * One case-insensitive native NTFS EA update. SET accepts an empty value;
 * REMOVE ignores Flags, ValueLength, and Value. Updates are applied in array
 * order (so repeated names have native last-wins behavior), and the complete
 * batch is merged and size-checked before any on-disk mutation is attempted.
 */
typedef struct NtfsExtendedAttributeUpdate
{
    UINT32 Operation;
    UINT8 Flags;
    UINT8 NameLength;
    UINT16 ValueLength;
    const UCHAR* Name;
    const UCHAR* Value;
} NtfsExtendedAttributeUpdate, *PNtfsExtendedAttributeUpdate;

#ifdef __cplusplus
extern "C" {
#endif

/* Directory functions */
PNtfsDirectory
NtfsDirectoryCreate(
    _In_ PNtfsVolume DiskVolume);

void
NtfsDirectoryDestroy(
    _In_opt_ PNtfsDirectory Dir);

NTSTATUS
NtfsDirectoryLoadDirectory(
    _In_ PNtfsDirectory Dir,
    _In_ PNtfsFileRecord File);

NTSTATUS
NtfsDirectoryReadNext(
    _In_ PNtfsDirectory Dir,
    _In_ BOOLEAN RestartScan,
    _Out_ PNtfsDirectoryEntry Entry);

/* FileRecord functions */
PNtfsFileRecord
NtfsFileRecordCreate(
    _In_ PNtfsVolume DiskVolume,
    _In_ ULONG FileRecordSize);

void
NtfsFileRecordDestroy(
    _In_opt_ NtfsFileRecord *FileRecord);

PFileRecordHeader
NtfsFileRecordGetHeader(
    _In_ NtfsFileRecord *FileRecord);

PAttribute
NtfsFileRecordGetAttribute(
    _In_ NtfsFileRecord *FileRecord,
    _In_ AttributeType Type,
    _In_opt_ PWSTR Name);

/*
 * Returns physical bytes reserved for an attribute. Sparse/compressed
 * nonresident attributes use CompressedDataSize; ordinary nonresident
 * attributes use AllocatedSize; resident attributes reserve no data clusters.
 */
UINT64
NtfsAttributeGetPhysicalAllocationSize(
    _In_opt_ PAttribute Attribute);

NTSTATUS
NtfsFileRecordGetAttributeData(
    _In_     PNtfsFileRecord Fr,
    _In_     AttributeType Type,
    _In_opt_ PWSTR Name,
    _Out_    PUCHAR *Data);

NTSTATUS
NtfsFileRecordCopyData(
    _In_ NtfsFileRecord *FileRecord,
    _In_ AttributeType Type,
    _In_opt_ PWSTR Name,
    _In_ PUCHAR Buffer,
    _Inout_ PULONG Length,
    _In_ ULONGLONG Offset);

NTSTATUS
NtfsFileRecordGetBasicInformation(
    _In_ NtfsFileRecord *FileRecord,
    _Out_ PNtfsFileBasicInformation Information);

NTSTATUS
NtfsFileRecordSetBasicInformation(
    _In_ NtfsFileRecord *FileRecord,
    _In_ const NtfsFileBasicInformation *Information);

NTSTATUS
NtfsFileRecordSetAutomaticTimestampMask(
    _In_ NtfsFileRecord *FileRecord,
    _In_ UINT32 Fields);

UINT32
NtfsFileRecordGetAutomaticTimestampMask(
    _In_ NtfsFileRecord *FileRecord);

NTSTATUS
NtfsFileRecordUpdateAutomaticTimestamps(
    _In_ NtfsFileRecord *FileRecord,
    _In_ UINT32 Fields);

/*
 * Returns the complete on-disk $REPARSE_POINT value. BufferLength is a byte
 * capacity on input and the required/returned byte count on output. A NULL
 * or undersized buffer returns STATUS_BUFFER_TOO_SMALL.
 */
NTSTATUS
NtfsFileRecordReadReparsePoint(
    _In_ NtfsFileRecord *FileRecord,
    _In_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength);

/*
 * Creates or replaces the complete native $REPARSE_POINT value. The supplied
 * tag must match an existing point; third-party tags also require a matching
 * GUID. Directory-emptiness, symlink-data, EA-conflict, and 16 KiB limits are
 * enforced before mutation.
 */
NTSTATUS
NtfsFileRecordSetReparsePoint(
    _In_ NtfsFileRecord *FileRecord,
    _In_reads_bytes_(BufferLength) const UCHAR* Buffer,
    _In_ ULONG BufferLength);

/*
 * Removes a native reparse point. Buffer is the FSCTL deletion envelope:
 * an 8-byte Microsoft header or 24-byte third-party header with zero data
 * length. Its tag and optional GUID must match the stored value.
 */
NTSTATUS
NtfsFileRecordDeleteReparsePoint(
    _In_ NtfsFileRecord *FileRecord,
    _In_reads_bytes_(BufferLength) const UCHAR* Buffer,
    _In_ ULONG BufferLength);

/*
 * Restores a WOF FILE-provider stream to ordinary unnamed $DATA, removes its
 * external-backing metadata, and releases the old backing allocation.
 * Returns STATUS_OBJECT_NOT_EXTERNALLY_BACKED for an ordinary file.
 */
NTSTATUS
NtfsFileRecordDeleteExternalBacking(
    _In_ NtfsFileRecord *FileRecord);

/*
 * Validates and projects IO_REPARSE_TAG_SYMLINK or
 * IO_REPARSE_TAG_MOUNT_POINT data without allocating or copying strings.
 */
NTSTATUS
NtfsParseNameSurrogateReparsePoint(
    _In_ const UCHAR* Buffer,
    _In_ ULONG BufferLength,
    _Out_ PNtfsReparseNameView View);

/*
 * Returns the complete, validated on-disk $EA value and its matching
 * $EA_INFORMATION record. BufferLength is a byte capacity on input and the
 * required/returned raw byte count on output.
 */
NTSTATUS
NtfsFileRecordReadExtendedAttributes(
    _In_ NtfsFileRecord *FileRecord,
    _In_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength,
    _Out_opt_ PEAInformationEx Information);

NTSTATUS
NtfsFileRecordUpdateExtendedAttributes(
    _In_ NtfsFileRecord *FileRecord,
    _In_reads_(UpdateCount)
        const NtfsExtendedAttributeUpdate *Updates,
    _In_ ULONG UpdateCount);

/*
 * Returns a validated self-relative security descriptor. NTFS 1.x per-file
 * attributes and NTFS 3.x $Secure/$SII/$SDS storage use the same API.
 * BufferLength is a byte capacity on input and the required/returned byte
 * count on output.
 */
NTSTATUS
NtfsFileRecordReadSecurityDescriptor(
    _In_ NtfsFileRecord *FileRecord,
    _In_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength);

/*
 * Replaces the file's security with a validated self-relative descriptor
 * stored in the legacy per-file $SECURITY_DESCRIPTOR attribute and
 * clears any $Secure SecurityId reference so the local value is
 * authoritative for every reader.
 */
NTSTATUS
NtfsFileRecordSetSecurityDescriptor(
    _In_ NtfsFileRecord *FileRecord,
    _In_reads_bytes_(BufferLength) const UCHAR* Buffer,
    _In_ ULONG BufferLength);

/*
 * Projects the entry at *Offset from a validated/raw $EA sequence and advances
 * *Offset. The first call uses offset zero; a call at the end returns
 * STATUS_NO_MORE_EAS.
 */
NTSTATUS
NtfsGetNextExtendedAttribute(
    _In_ const UCHAR* Buffer,
    _In_ ULONG BufferLength,
    _Inout_ PULONG Offset,
    _Out_ PNtfsExtendedAttributeView View);

/*
 * LZNT1 codec for ordinary NTFS compression. Compress fails with
 * STATUS_BUFFER_TOO_SMALL when the stream cannot fit OutputCapacity, so
 * unit writers can bound it by the byte saving they require and fall
 * back to raw storage.
 */
NTSTATUS
NtfsLznt1Compress(
    _In_ PUCHAR Input,
    _In_ ULONG InputSize,
    _Out_ PUCHAR Output,
    _In_ ULONG OutputCapacity,
    _Out_ PULONG FinalSize);

NTSTATUS
NtfsLznt1Decompress(
    _Out_ PUCHAR Output,
    _In_ ULONG OutputSize,
    _In_ PUCHAR Input,
    _In_ ULONG InputSize,
    _Out_ PULONG FinalSize);

NTSTATUS
NtfsFileRecordWriteFileData(
    _In_ NtfsFileRecord *FileRecord,
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ PUCHAR Buffer,
    _Inout_ PULONG Length,
    _In_ PLARGE_INTEGER Offset);

/*
 * Changes the logical size of an existing ordinary $DATA stream. Growth
 * reads as zero beyond the prior initialized size; shrink releases complete
 * tail clusters and converts a zero-length nonresident stream to resident.
 */
NTSTATUS
NtfsFileRecordSetFileDataSize(
    _In_ NtfsFileRecord *FileRecord,
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ ULONGLONG NewSize);

NTSTATUS NtfsFileRecordSetFileValidDataLength(_In_ NtfsFileRecord *FileRecord, _In_ AttributeType AttrType, _In_opt_ PWSTR StreamName, _In_ ULONGLONG NewValidDataLength);

/*
 * Changes allocation independently of EOF. Requests below EOF truncate the
 * stream first; larger requests reserve rounded whole clusters without
 * advancing data or initialized size.
 */
NTSTATUS
NtfsFileRecordSetFileAllocationSize(
    _In_ NtfsFileRecord *FileRecord,
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ ULONGLONG NewAllocationSize);

/*
 * Implements the stream semantics of FSCTL_SET_SPARSE. Marking a stream
 * sparse does not discard allocation. Clearing sparse fully allocates every
 * hole before removing the sparse state.
 */
NTSTATUS
NtfsFileRecordSetSparse(
    _In_ NtfsFileRecord *FileRecord,
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ BOOLEAN SetSparse);

/*
 * Implements FSCTL_SET_ZERO_DATA semantics without extending EOF. A sparse
 * stream may release complete NTFS sparse units wholly covered by the range.
 */
NTSTATUS
NtfsFileRecordSetZeroData(
    _In_ NtfsFileRecord *FileRecord,
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ ULONGLONG FileOffset,
    _In_ ULONGLONG BeyondFinalZero);

/*
 * Implements FSCTL_QUERY_ALLOCATED_RANGES semantics. RangeCount is an
 * element capacity on input and the number of elements written on output.
 * STATUS_BUFFER_TOO_SMALL means no element fit; STATUS_BUFFER_OVERFLOW means
 * the returned prefix is valid but more ranges remain.
 */
NTSTATUS
NtfsFileRecordQueryAllocatedRanges(
    _In_ NtfsFileRecord *FileRecord,
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ ULONGLONG FileOffset,
    _In_ ULONGLONG Length,
    _Out_opt_ PNtfsAllocatedRange Ranges,
    _Inout_ PULONG RangeCount);

/*
 * Implements FSCTL_GET_RETRIEVAL_POINTERS stream semantics. ExtentCount is
 * an element capacity on input and the number written on output.
 * ReturnedStartingVcn is the beginning of the extent containing StartingVcn.
 */
NTSTATUS
NtfsFileRecordQueryRetrievalPointers(
    _In_ NtfsFileRecord *FileRecord,
    _In_ AttributeType AttrType,
    _In_opt_ PWSTR StreamName,
    _In_ ULONGLONG StartingVcn,
    _Out_ PULONGLONG ReturnedStartingVcn,
    _Out_opt_ PNtfsRetrievalExtent Extents,
    _Inout_ PULONG ExtentCount);

/*
 * Enumerates logical $DATA streams, including streams whose first extent is
 * stored in an $ATTRIBUTE_LIST extension record. StreamCount is an element
 * capacity on input and the number written on output.
 */
NTSTATUS
NtfsFileRecordQueryDataStreams(
    _In_ NtfsFileRecord *FileRecord,
    _Out_opt_ PNtfsDataStreamInformation Streams,
    _Inout_ PULONG StreamCount);

/* LFS functions */
ULONG
NtfsLogFileServiceGetClientMajorVersion(
    _In_ PNtfsLogFileService LFS);

ULONG
NtfsLogFileServiceGetClientMinorVersion(
    _In_ PNtfsLogFileService LFS);

/* MFT functions */
NTSTATUS
NtfsMasterFileTableGetFileRecordFromQuery(
    _In_ PNtfsMasterFileTable Mft,
    _In_ PWCHAR Query,
    _Out_ PNtfsFileRecord* File);

/*
 * Resolves a counted UTF-16 path while stopping on an intermediate reparse
 * point, or on the final component unless OpenFinalReparsePoint is set.
 * QueryLength is measured in UTF-16 code units. STATUS_REPARSE returns the
 * owning file record and the byte length of the unparsed path suffix.
 */
/*
 * Resolves one name inside a directory the caller already has open. Avoids
 * re-walking the path from the root for every open in the same directory.
 */
NTSTATUS
NtfsMasterFileTableGetFileRecordInDirectory(
    _In_ PNtfsMasterFileTable Mft,
    _In_ PNtfsFileRecord ParentDirectory,
    _In_reads_(NameLength) PWCHAR Name,
    _In_ ULONG NameLength,
    _Out_ PNtfsFileRecord* File);

NTSTATUS
NtfsMasterFileTableGetFileRecordFromQueryEx(
    _In_ PNtfsMasterFileTable Mft,
    _In_reads_(QueryLength) PWCHAR Query,
    _In_ ULONG QueryLength,
    _In_ BOOLEAN OpenFinalReparsePoint,
    _Out_ PULONG RemainingNameLength,
    _Out_ PNtfsFileRecord* File);

/*
 * Implements FSCTL_GET_NTFS_FILE_RECORD enumeration semantics. The high
 * 16-bit sequence component of RequestedFileReference is ignored and the
 * first in-use ordinal at or below the requested ordinal is returned.
 * BufferLength is a byte capacity on input and the exact record size on
 * success or STATUS_BUFFER_TOO_SMALL.
 */
NTSTATUS
NtfsMasterFileTableReadFileRecord(
    _In_ PNtfsMasterFileTable Mft,
    _In_ ULONGLONG RequestedFileReference,
    _Out_ PULONGLONG ReturnedFileReference,
    _Out_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength);

/*
 * Creates one regular file or empty directory at a counted UTF-16 path.
 * The parent must already exist. FileAttributes contains ordinary DOS
 * attributes; the directory bit is supplied separately through IsDirectory.
 * On success File receives the new, caller-owned file-record handle.
 */
NTSTATUS
NtfsMasterFileTableCreateFile(
    _In_ PNtfsMasterFileTable Mft,
    _In_reads_(QueryLength) PWCHAR Query,
    _In_ ULONG QueryLength,
    _In_ BOOLEAN IsDirectory,
    _In_ ULONG FileAttributes,
    _Out_ PNtfsFileRecord* File);

/*
 * Creates one entry in a directory whose record the caller already holds.
 * Name is one counted path component. NameKnownMissing may be TRUE only when
 * the caller has just checked the same parent while serializing namespace
 * changes. The parent record is updated in place and remains caller-owned.
 */
NTSTATUS
NtfsMasterFileTableCreateFileInDirectory(
    _In_ PNtfsMasterFileTable Mft,
    _In_ PNtfsFileRecord Parent,
    _In_reads_(NameLength) PWCHAR Name,
    _In_ ULONG NameLength,
    _In_ BOOLEAN IsDirectory,
    _In_ ULONG FileAttributes,
    _In_ BOOLEAN NameKnownMissing,
    _Out_ PNtfsFileRecord* File);

/*
 * Removes one name at a counted UTF-16 path. RemoveDirectory selects
 * Win32 RemoveDirectory semantics: it deletes only empty directories,
 * while FALSE deletes only ordinary files. Dropping the last name
 * releases the record set and every owned cluster.
 */
NTSTATUS
NtfsMasterFileTableDeleteFile(
    _In_ PNtfsMasterFileTable Mft,
    _In_reads_(QueryLength) PWCHAR Query,
    _In_ ULONG QueryLength,
    _In_ BOOLEAN RemoveDirectory);

/*
 * Renames or moves one file or directory between counted UTF-16 paths.
 * The destination parent must exist; the destination name must be free
 * except for a pure case change of the same file.
 */
NTSTATUS
NtfsMasterFileTableRenameFile(
    _In_ PNtfsMasterFileTable Mft,
    _In_reads_(OldQueryLength) PWCHAR OldQuery,
    _In_ ULONG OldQueryLength,
    _In_reads_(NewQueryLength) PWCHAR NewQuery,
    _In_ ULONG NewQueryLength);

/*
 * Publishes an additional hard link to an existing ordinary file at a
 * new counted UTF-16 path and raises its link count.
 */
NTSTATUS
NtfsMasterFileTableCreateHardLink(
    _In_ PNtfsMasterFileTable Mft,
    _In_reads_(ExistingQueryLength)
        PWCHAR ExistingQuery,
    _In_ ULONG ExistingQueryLength,
    _In_reads_(NewQueryLength) PWCHAR NewQuery,
    _In_ ULONG NewQueryLength);

/* Probe functions */
NTSTATUS
NtfsProbePartition(
    _In_ ULONG BytesPerSector,
    _In_ PUCHAR BootSectorData);

/* Reads the boot sector through the environment's disk routines
 * (initialize them first) and mounts the volume if it is NTFS.
 */
NTSTATUS
NtfsProbePartitionAndOpenVolume(
    _In_ ULONG BytesPerSector,
    _Out_ PNtfsVolume* VolumeOut);

/* Library options */
void
NtfsSetShowMetadataFiles(
    _In_ BOOLEAN Show);

/*
 * Opens subsequent volumes without journal recovery or write support.
 * Intended for diagnostic readers such as the Linux FUSE frontend.
 */
void
NtfsSetReadOnlyMode(
    _In_ BOOLEAN ReadOnly);

/* Volume functions */
NTSTATUS
NtfsVolumeGetADSPreference(
    _In_ PNtfsVolume DiskVolume,
    _In_ PUNICODE_STRING FileName,
    _Out_ AttributeType* RequestedType,
    _Out_ PWSTR* RequestedStream);

NTSTATUS
NtfsVolumeReadSecurityDescriptorById(
    _In_ PNtfsVolume DiskVolume,
    _In_ ULONG SecurityId,
    _In_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength);

void
NtfsVolumeDestroy(
    _In_opt_ PNtfsVolume DiskVolume);

ULONG
NtfsVolumeGetBytesPerSector(
    _In_ PNtfsVolume DiskVolume);

UINT64
NtfsVolumeGetClustersInVolume(
    _In_ PNtfsVolume DiskVolume);

NTSTATUS
NtfsVolumeGetFreeClusters(
    _In_ PNtfsVolume DiskVolume,
    _Out_ PLARGE_INTEGER FreeClusters);

/*
 * Returns the geometry and core metadata needed by
 * FSCTL_GET_NTFS_VOLUME_DATA.
 */
NTSTATUS
NtfsVolumeQueryInformation(
    _In_ PNtfsVolume DiskVolume,
    _Out_ PNtfsVolumeInformation Information);

/*
 * Reads the allocation bits backing FSCTL_GET_VOLUME_BITMAP. StartingLcn is
 * rounded down to a byte boundary. BitmapLength is a byte capacity on input
 * and the number written on output; BitmapSize is a count of valid clusters.
 */
NTSTATUS
NtfsVolumeReadBitmap(
    _In_ PNtfsVolume DiskVolume,
    _In_ ULONGLONG StartingLcn,
    _Out_ PULONGLONG ReturnedStartingLcn,
    _Out_ PULONGLONG BitmapSize,
    _Out_opt_ PUCHAR Bitmap,
    _Inout_ PULONG BitmapLength);

PNtfsLogFileService
NtfsVolumeGetLFS(
    _In_ PNtfsVolume DiskVolume);

PNtfsMasterFileTable
NtfsVolumeGetMft(
    _In_ PNtfsVolume DiskVolume);

USHORT
NtfsVolumeGetMajorVersion(
    _In_ PNtfsVolume DiskVolume);

USHORT
NtfsVolumeGetMinorVersion(
    _In_ PNtfsVolume DiskVolume);

UINT8
NtfsVolumeGetSectorsPerCluster(
    _In_ PNtfsVolume DiskVolume);

UINT64
NtfsVolumeGetSerialNumber(
    _In_ PNtfsVolume DiskVolume);

NTSTATUS
NtfsVolumeGetVolumeLabel(
    _In_ PNtfsVolume DiskVolume,
    _Out_ PWSTR VolumeLabel,
    _Out_ PUSHORT Length);

BOOLEAN
NtfsVolumeIsReadOnly(
    _In_ PNtfsVolume DiskVolume);

NTSTATUS
NtfsVolumeSetVolumeLabel(
    _In_ PNtfsVolume DiskVolume,
    _In_ PWSTR VolumeLabel,
    _In_ ULONG Length);

#ifdef __cplusplus
}
#endif

#endif /* _NTFSLIB_NEW_H_ */
