#ifndef _NTFSLIB_NEW_INTERNAL_H_
#define _NTFSLIB_NEW_INTERNAL_H_

#ifdef NTFSLIB_PORTABLE
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wctype.h>
#include <new>
#else
#include <ntifs.h>

/* Provides DPRINT1 for every library source (no-op in release builds).
 * Must stay unconditional: NTFS_DEBUG is never defined for the library
 * target itself, only by driver builds.
 */
#include <debug.h>
#endif

#ifdef NTFSLIB_PORTABLE
#ifdef NTFSLIB_DEBUG
#define DPRINT1(...) fprintf(stderr, __VA_ARGS__)
#define DbgPrint(...) fprintf(stderr, __VA_ARGS__)
#else
#define DPRINT1(...) ((void)0)
#define DbgPrint(...) ((void)0)
#endif
#define ASSERT(Expression) assert(Expression)
#ifndef DBG
#define DBG 0
#endif

#define RtlCopyMemory(Destination, Source, Length) \
    memcpy((Destination), (Source), (Length))
#define RtlMoveMemory(Destination, Source, Length) \
    memmove((Destination), (Source), (Length))
#define RtlZeroMemory(Destination, Length) \
    memset((Destination), 0, (Length))
#define RtlFillMemory(Destination, Length, Fill) \
    memset((Destination), (Fill), (Length))
#define min(Left, Right) ((Left) < (Right) ? (Left) : (Right))

static inline SIZE_T
RtlCompareMemory(const void* Left, const void* Right, SIZE_T Length)
{
    const UCHAR* LeftBytes = (const UCHAR*)Left;
    const UCHAR* RightBytes = (const UCHAR*)Right;
    SIZE_T Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (LeftBytes[Index] != RightBytes[Index])
            return Index;
    }
    return Length;
}

static inline WCHAR
RtlUpcaseUnicodeChar(WCHAR Character)
{
    return (WCHAR)towupper((wint_t)(UINT16)Character);
}

static inline LONG
RtlCompareUnicodeString(PUNICODE_STRING Left,
                        PUNICODE_STRING Right,
                        BOOLEAN IgnoreCase)
{
    ULONG LeftLength = Left->Length / sizeof(WCHAR);
    ULONG RightLength = Right->Length / sizeof(WCHAR);
    ULONG CommonLength = LeftLength < RightLength ? LeftLength : RightLength;

    for (ULONG Index = 0; Index < CommonLength; Index++)
    {
        WCHAR LeftCharacter = Left->Buffer[Index];
        WCHAR RightCharacter = Right->Buffer[Index];
        if (IgnoreCase)
        {
            LeftCharacter = RtlUpcaseUnicodeChar(LeftCharacter);
            RightCharacter = RtlUpcaseUnicodeChar(RightCharacter);
        }
        if (LeftCharacter != RightCharacter)
            return LeftCharacter < RightCharacter ? -1 : 1;
    }

    return LeftLength == RightLength ? 0 : (LeftLength < RightLength ? -1 : 1);
}

static inline void
RtlInitializeBitMap(PRTL_BITMAP Bitmap, PULONG Buffer, ULONG Size)
{
    Bitmap->Buffer = Buffer;
    Bitmap->SizeOfBitMap = Size;
}

static inline ULONG
RtlNumberOfClearBits(PRTL_BITMAP Bitmap)
{
    ULONG ClearBits = 0;
    UCHAR* Bytes = (UCHAR*)Bitmap->Buffer;

    for (ULONG Bit = 0; Bit < Bitmap->SizeOfBitMap; Bit++)
    {
        if ((Bytes[Bit / 8] & (1u << (Bit % 8))) == 0)
            ClearBits++;
    }
    return ClearBits;
}
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Environment contract: implemented by the linked env glue (km/um/fl). */
void*
NtfsAllocatePoolWithTag(_In_ POOL_TYPE PoolType,
                        _In_ size_t Size,
                        _In_ ULONG Tag);

void
NtfsFreePool(_In_ void* pObject);

BOOLEAN
NtfsIsNameInExpression(_In_     PUNICODE_STRING Expression,
                       _In_     PUNICODE_STRING Name,
                       _In_     BOOLEAN IgnoreCase,
                       _In_opt_ PWCHAR UpcaseTable);

NTSTATUS
NtfsQuerySystemTime(_Out_ PULONGLONG NtfsTime);

unsigned long long
NtfsQueryTicks(void);

/* Default copied into each volume when it is initialized. */
extern BOOLEAN NtfsDefaultShowMetadataFiles;
extern BOOLEAN NtfsDefaultReadOnlyMode;

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
void* __cdecl operator new(size_t Size, POOL_TYPE PoolType);
void* __cdecl operator new(size_t Size, POOL_TYPE PoolType, ULONG Tag);
void* __cdecl operator new[](size_t Size, POOL_TYPE PoolType);
void* __cdecl operator new[](size_t Size, POOL_TYPE PoolType, ULONG Tag);
#endif

// =========================
// In-memory structures
// =========================

// Forward declarations for DataRun struct because it's a linked list.
struct DataRun;
typedef struct DataRun *PDataRun;

struct DataRun
{
    PDataRun  NextRun;
    ULONGLONG LCN;
    ULONGLONG Length; // In clusters
    BOOLEAN   IsSparse;
};

struct NonResidentMappingUpdate;
typedef struct NonResidentMappingUpdate *PNonResidentMappingUpdate;

struct DataRunCacheEntry;
typedef struct DataRunCacheEntry *PDataRunCacheEntry;

struct DataRunCacheEntry
{
    PDataRunCacheEntry Next;
    PAttribute Attribute;
    PDataRun Runs;
};

typedef struct AttrDefCacheEntry
{
    AttributeType Type;
    WCHAR Label[64];
} AttrDefCacheEntry, *PAttrDefCacheEntry;

struct _BTreeNode;
struct _BTreeKey;

typedef struct _BTreeNode BTreeNode, *PBTreeNode;
typedef struct _BTreeKey BTreeKey, *PBTreeKey;

#ifdef __cplusplus
void NtfsDestroyBTreeNode(_In_opt_ PBTreeNode Node);
#endif

NTSTATUS
NtfsApplyFixup(_Inout_ PNTFSRecordHeader Header,
               _In_ ULONG RecordSize,
               _In_ ULONG BytesPerSector);

NTSTATUS
NtfsCommitFixup(_Inout_ PNTFSRecordHeader Header,
                _In_ ULONG RecordSize,
                _In_ ULONG BytesPerSector);

BOOLEAN
NtfsIsDirectoryIndexEntryValid(_In_ PIndexEntry Entry,
                               _In_ ULONG Remaining);

NTSTATUS
NtfsValidateComponentName(
    _In_reads_(NameLength) PWCHAR Name,
    _In_ ULONG NameLength);

/* The one indexed directory index: the $I30 $FILE_NAME index. */
extern const WCHAR NtfsI30Name[];

/* DOS attribute bits a caller may set when creating a file. */
#define NTFS_CREATE_MUTABLE_ATTRIBUTES \
    (FILE_PERM_READONLY | FILE_PERM_HIDDEN | FILE_PERM_SYSTEM | \
     FILE_PERM_ARCHIVE | FILE_PERM_NORMAL | FILE_PERM_TEMP | \
     FILE_PERM_OFFLINE | FILE_PERM_NOT_INDXED)

struct _BTreeNode
{
    ULONGLONG  VCN;
    PBTreeKey  FirstKey;
};

struct _BTreeKey
{
    PBTreeKey   ParentNodeKey; // Used to get entries linearly.
    PBTreeNode  ChildNode;
    PBTreeKey   NextKey;
    PBTreeKey   ShortNameKey;
    PIndexEntry Entry;
    ULONG       Flags;
};

/* Private macros */
#define BytesPerCluster(Volume) (Volume->BytesPerSector * Volume->SectorsPerCluster)

#define FileRef(Key) ((Key)->Entry->Data.Directory.IndexedFile)

#define IsFileRecordInMFTMirr(FileRecordNumber) \
((DiskVolume->SectorsPerCluster * DiskVolume->BytesPerSector) > (FileRecordSize << 2)) ? \
FileRecordNumber < ((DiskVolume->SectorsPerCluster * DiskVolume->BytesPerSector) / FileRecordSize) \
: FileRecordNumber < 4

#define LONGLONG_SIGN_EXTEND(Number, Bytes) \
(((Number) << ((sizeof(LONGLONG) - (Bytes)) * 8)) >> ((sizeof(LONGLONG) - (Bytes)) * 8))

#define BytesPerIndexRecord(DiskVolume) \
((DiskVolume)->ClustersPerIndexRecord < 0 \
 ? (1UL << (-(DiskVolume)->ClustersPerIndexRecord)) \
 : (BytesPerCluster(DiskVolume) * (DiskVolume)->ClustersPerIndexRecord))

#define IsRootFile(Path) \
((Path)[0] == L'\0' || ((Path)[0] == L'\\' && (Path)[1] == L'\0'))

#define MFTDiskOffset (MFTLCN * BytesPerCluster(DiskVolume))
#define MFTMirrDiskOffset (MFTMirrLCN * BytesPerCluster(DiskVolume))
#define FileRecordOffset(FileRecordNumber) (FileRecordNumber * FileRecordSize)

#define GetOffset(LCN) (LCN * BytesPerCluster(DiskVolume))
#define GetRunSize(Run) (Run->Length * BytesPerCluster(DiskVolume))

#define GetFileName(Key) \
((PFileNameEx)((Key)->Entry->IndexStream))

// The VCN of an entry's subnode is stored in the last 8 bytes of the entry.
#define GetSubnodeVCN(Entry) \
((PULONGLONG)((ULONG_PTR)(Entry) + (Entry)->EntryLength - sizeof(ULONGLONG)))

// Calculates start of Index Buffer relative to the index allocation, given the node's VCN
#define GetAllocationOffsetFromVCN(VCN) \
(BytesPerIndexRecord(DiskVolume) < BytesPerCluster(DiskVolume)) ? \
(VCN * (DiskVolume->BytesPerSector)) : \
(VCN * BytesPerCluster(DiskVolume))

#define IsLastEntry(Key) !!((Key)->Entry->Flags & INDEX_ENTRY_END)

#define IsIndexNode(Key) !!((Key)->Entry->Flags & INDEX_ENTRY_NODE)

#define IsEndOfNode(Key) (IsLastEntry(Key) && !IsIndexNode(Key))

#ifdef __cplusplus
static inline PBTreeKey GetFirstKey(PBTreeNode Node)
{
    PBTreeKey Key = Node ? Node->FirstKey : NULL;

    while (Key && Key->ChildNode)
        Key = Key->ChildNode->FirstKey;
    return Key;
}

static inline PBTreeKey GetNextKey(PBTreeKey Key)
{
    if (!Key)
        return NULL;

    if (!(Key->Entry->Flags & INDEX_ENTRY_END))
    {
        Key = Key->NextKey;
        if (Key && Key->ChildNode)
            return GetFirstKey(Key->ChildNode);
        return Key;
    }

    /*
     * An end marker finishes one child node. Its owning parent key is the
     * next in-order entry. End-marker parents represent rightmost children,
     * so keep climbing until a real separator key (or the root) is reached.
     */
    for (Key = Key->ParentNodeKey;
         Key && (Key->Entry->Flags & INDEX_ENTRY_END);
         Key = Key->ParentNodeKey)
    {
    }
    return Key;
}

static inline SIZE_T
NtfsWcsLen(_In_ PCWSTR String)
{
    PCWSTR End = String;
    while (*End)
        End++;
    return End - String;
}

static inline PWSTR
NtfsWcsChr(_In_ PWSTR String, _In_ WCHAR Character)
{
    do
    {
        if (*String == Character)
            return String;
    } while (*String++);
    return NULL;
}

static inline PCWSTR
NtfsWcsChr(_In_ PCWSTR String, _In_ WCHAR Character)
{
    do
    {
        if (*String == Character)
            return String;
    } while (*String++);
    return NULL;
}

static inline UNICODE_STRING
NtfsMakeCountedUnicodeString(_In_ PWSTR Buffer,
                             _In_ USHORT Length)
{
    UNICODE_STRING String;

    String.Buffer = Buffer;
    String.Length = Length;
    String.MaximumLength = Length;
    return String;
}
#endif

#define DIR_KEY_8DOT3          0x00000001
#define BTREE_KEY_BORROWED_ENTRY 0x00000002
#define BTREE_KEY_ARENA_OBJECT 0x00000004

#define DIRECTORY_KEY_BLOCK_CAPACITY 128

typedef struct _DIRECTORY_KEY_BLOCK
{
    struct _DIRECTORY_KEY_BLOCK* Next;
    ULONG Used;
    BTreeKey Keys[DIRECTORY_KEY_BLOCK_CAPACITY];
} DIRECTORY_KEY_BLOCK, *PDIRECTORY_KEY_BLOCK;

#define GetWStrLength(x) ((x) * sizeof(WCHAR))
#define MAX_SHORTNAME_LENGTH 12

// Macro to get data pointer from a resident attribute pointer.
#define GetResidentDataPointer(Attrib) (char*)(((ULONG_PTR)Attrib) + \
                                               (Attrib->Resident.DataOffset))

// Macro to free memory from data run.
#define FreeDataRun(x) while(x) {\
    PDataRun tmp = x->NextRun;\
    delete x;\
    x = tmp;\
}

// Macro to get the file record number from a file reference
#define GetFRNFromFileRef(x) ((x) & 0xFFFFFFFFFFFF)
#define GetSequenceFromFileRef(x) ((USHORT)((x) >> 48))

// Compose a file reference from a record header (inverse of the two above)
#define MakeFileReference(Header) \
    (((ULONGLONG)(Header)->SequenceNumber << 48) | \
     ((ULONGLONG)(Header)->MFTRecordNumber & 0xFFFFFFFFFFFFULL))

#define GetNamePointer(x) (((char*)x) + (x->NameOffset))

#define GetAttributeDataSize(Attribute1) \
((Attribute1)->IsNonResident \
    ? (Attribute1)->NonResident.DataSize \
    : (Attribute1)->Resident.DataLength)

#define NTFS_FILE_NAME_UPDATE_SIZES ((UINT32)0x00000001)
#define NTFS_FILE_NAME_UPDATE_EA_SIZE ((UINT32)0x00000002)
#define NTFS_FILE_NAME_UPDATE_ARCHIVE ((UINT32)0x00000004)
#define NTFS_FILE_NAME_UPDATE_REPARSE_TAG ((UINT32)0x00000008)
#define NTFS_FILE_NAME_UPDATE_STORAGE_FLAGS ((UINT32)0x00000010)
#define NTFS_FILE_NAME_UPDATE_ALL ((UINT32)0x0000001f)

NTSTATUS
NtfsValidateReparseBuffer(
    _In_reads_bytes_(BufferLength) const UCHAR* Buffer,
    _In_ ULONG BufferLength,
    _Out_opt_ PReparsePointEx CommonHeader);

#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS
NtfsReadVolume(_In_    ULONGLONG Offset,
               _In_    ULONG Length,
               _Inout_ PUCHAR Buffer);

NTSTATUS
NtfsWriteVolume(_In_    ULONGLONG Offset,
                _In_    ULONG Length,
                _Inout_ PUCHAR Buffer);

#ifdef __cplusplus
}
#endif

// =========================
// NTFS Memory Tags
// =========================
/* The core passes these into the allocation contract; each environment
 * decides whether to honor them (km pools do, the um heap ignores them).
 */

#define TAG_NTFS 'NTFS'
#define TAG_MFT '$MFT'
#define TAG_FILE_RECORD 'FREC'
#define TAG_DATA_RUN 'DTRN'
#define TAG_LOG_FILE_SERVICE 'LgFS'
#define TAG_BTREE 'BTRE'

// =========================
// NTFS C++ Classes
// =========================

#ifdef __cplusplus

typedef class Volume
{
public:
    ULONG  BytesPerSector;
    UINT8  SectorsPerCluster;
    UINT64 SectorsInVolume;
    UINT64 ClustersInVolume;
    INT8   ClustersPerIndexRecord;
    UINT64 SerialNumber;
    USHORT NtfsMajorVersion;
    USHORT NtfsMinorVersion;
    class MasterFileTable* MFT;
    class LogFileService* LFS;
    BOOLEAN IsReadOnly = FALSE;
    BOOLEAN ShowMetadataFiles = FALSE;

    /*
     * Where to begin the free-cluster search. Without it every allocation
     * scans the volume bitmap from cluster zero, so creating or extending N
     * files costs N^2. Only a hint: the search still wraps to the start, and
     * releasing clusters pulls it back so space is never stranded.
     */
    ULONGLONG FreeClusterHint = 0;

    /* Parsed $Bitmap record, built once: its layout never changes and its
     * data is served through the block cache, so re-parsing it for every
     * cluster allocate/release only burned CPU. */
    class FileRecord* CachedBitmapFile = NULL;
    struct _ATTRIBUTE* CachedBitmapData = NULL;

    /* Scratch for volume-bitmap scans and edits; allocating 64 KB of pool
     * per cluster operation cost more than the operation. All users run
     * under the caller's metadata serialization. */
    PUCHAR BitmapWorkBuffer = NULL;

    /* Scratch for index-block reads during lookups; a pool allocation per
     * name resolution was a fifth of the lookup. Descents never nest. */
    PUCHAR IndexWorkBuffer = NULL;
    ULONG IndexWorkBufferSize = 0;

    /* Free list of record-sized buffers. Every parsed record allocated and
     * freed 1 KB of pool; recycling them needs its own lock because records
     * are destroyed outside the metadata serialization. */
    FAST_MUTEX RecordPoolMutex;
    PUCHAR RecordPoolHead = NULL;
    ULONG RecordPoolCount = 0;
    ULONG RecordPoolBufferSize = 0;

    ~Volume();

    /**
     * Gets an attribute type value from the name of the attribute. This
     * performs a lookup against the $AttrDef metadata file on the volume.
     *
     * @return
     * STATUS_SUCCESS if successful.
     */
    NTSTATUS
    GetAttributeTypeFromName(_In_  PWSTR AttributeTypeName,
                             _Out_ AttributeType* Type);

    /**
     * Gets the number of free clusters in the volume. This reads from the
     * $Bitmap metadata file on the volume.
     *
     * @return
     * STATUS_SUCCESS if successful.
     */
    NTSTATUS
    GetFreeClusters(_Out_ PLARGE_INTEGER FreeClusters);

    NTSTATUS
    QueryInformation(
        _Out_ PNtfsVolumeInformation Information);

    NTSTATUS
    ReadBitmap(_In_ ULONGLONG StartingLcn,
               _Out_ PULONGLONG ReturnedStartingLcn,
               _Out_ PULONGLONG BitmapSize,
               _Out_opt_ PUCHAR Bitmap,
               _Inout_ PULONG BitmapLength);

    /*
     * Claims free clusters in $Bitmap and returns their physical runs in
     * logical allocation order. MaxRuns bounds both fragmentation and the
     * mapping-pairs space a caller is prepared to consume.
     */
    NTSTATUS
    AllocateClusters(_In_ ULONGLONG PreferredLCN,
                     _In_ ULONG ClusterCount,
                     _In_ ULONG MaxRuns,
                     _Out_ PDataRun* Runs);

    /*
     * Clears the $Bitmap bits for runs previously returned by
     * AllocateClusters(). The caller retains ownership of the run list.
     */
    NTSTATUS
    ReleaseClusters(_In_ PDataRun Runs);

    /**
     * Converts a null-terminated 16-bit string to uppercase using the
     * code page stored on the volume. This reads from the $UpCase metadata
     * file on the volume.
     *
     * @return
     * STATUS_SUCCESS if successful.
     */
    NTSTATUS
    UpcaseWideString(_Inout_ PWSTR WideString,
                     _In_    ULONG Length);

    /**
     * Compares two counted NTFS file names using this volume's immutable
     * $UpCase table. This is the collation used by the $I30 index.
     */
    NTSTATUS
    CompareFileNames(_In_  PUNICODE_STRING Left,
                     _In_  PUNICODE_STRING Right,
                     _Out_ LONG* Result);

    PWSTR
    GetUpcaseTable()
    {
        return UpcaseTable;
    }

    /**
     * Gets the volume label as a 16-bit string and its length in bytes.
     * This reads from the $Volume metadata file on the volume.
     *
     * @return
     * STATUS_SUCCESS if successful.
     */
    NTSTATUS
    GetVolumeLabel(_Inout_ PWSTR   VolumeLabel,
                   _Inout_ PUSHORT Length);

    /**
     * Sets the volume label from a 16-bit string and its length in bytes.
     * This writes to the $Volume metadata file on the volume.
     *
     * @return
     * STATUS_SUCCESS if successful.
     */
    NTSTATUS
    SetVolumeLabel(_In_ PWSTR VolumeLabel,
                   _In_ ULONG Length);

    /**
     * Copies a specified number of bytes into a buffer from the volume at a
     * given offset. The offset and length do not have to be sector aligned.
     *
     * @param Offset
     * The offset, in bytes, to begin copying data from the volume.
     *
     * @param Length
     * The number of bytes to copy from the volume.
     *
     * @param Buffer
     * The buffer to copy data from the volume into.
     *
     * @return
     * STATUS_SUCCESS if successful.
     */
    NTSTATUS ReadVolume(_In_    ULONGLONG Offset,
                        _In_    ULONG Length,
                        _Inout_ PUCHAR Buffer);

    /**
     * Writes a specified number of bytes to the volume at a given offset. The
     * offset and length do not have to be sector aligned.
     *
     * @param Offset
     * The offset, in bytes, to begin writing data to the volume.
     *
     * @param Length
     * The number of bytes to write to the volume.
     *
     * @param Buffer
     * The buffer containing the data to write to the volume.
     *
     * @return
     * STATUS_SUCCESS if successful.
     */
    NTSTATUS WriteVolume(_In_    ULONGLONG Offset,
                         _In_    ULONG Length,
                         _Inout_ PUCHAR Buffer);

    NTSTATUS
    Initialize(_In_ PUCHAR BootSectorData);

    NTSTATUS
    GetADSPreference(_In_  PUNICODE_STRING FileName,
                     _Out_ AttributeType* RequestedType,
                     _Out_ PWSTR* RequestedStream);

    NTSTATUS
    ReadSecurityDescriptorById(
        _In_ ULONG SecurityId,
        _In_opt_ PUCHAR Buffer,
        _Inout_ PULONG BufferLength);

private:
    PAttrDefCacheEntry AttrDefCache = NULL;
    ULONG AttrDefCacheCount = 0;

    WCHAR VolumeLabelCache[MAXIMUM_VOLUME_LABEL_LENGTH / sizeof(WCHAR)] = {};
    USHORT VolumeLabelCacheLength = 0;
    BOOLEAN VolumeLabelCached = FALSE;

    /* $UpCase table, read from disk on first use by UpcaseWideString(). */
    PWSTR UpcaseTable = NULL;
    ULONG UpcaseTableLength = 0; // In WCHARs

    NTSTATUS
    LoadUpcaseTable();

    NTSTATUS
    LoadAttributeDefinitions();

} *PVolume;

struct FileRecordExtentCacheEntry;
typedef struct FileRecordExtentCacheEntry *PFileRecordExtentCacheEntry;

typedef class FileRecord
{
public:
    PFileRecordHeader Header;
    PUCHAR Data = NULL;

    // ./filerecord.cpp
    FileRecord(_In_ PVolume DiskVolume,
               _In_ ULONG FileRecordSize);
    FileRecord(_In_ PVolume DiskVolume);
    ~FileRecord();

    // ./find.cpp
    PAttribute GetAttribute(_In_     AttributeType Type,
                            _In_opt_ PWSTR Name);
    NTSTATUS GetAttributeData(_In_     AttributeType Type,
                              _In_opt_ PWSTR Name,
                              _Out_    PUCHAR *Data);
    PDataRun FindNonResidentData(_In_ PAttribute DataAttr);

    // ./copy.cpp
    NTSTATUS CopyData(_In_ AttributeType Type,
                      _In_ PWSTR Name,
                      _In_ PUCHAR Buffer,
                      _Inout_ PULONG Length,
                      _In_ ULONGLONG Offset = 0);
    NTSTATUS CopyData(_In_ PAttribute Attr,
                      _In_ PUCHAR Buffer,
                      _Inout_ PULONG Length,
                      _In_ ULONGLONG Offset = 0);
    NTSTATUS ReadAttributeAlloc(_In_ PAttribute Attr,
                                _Outptr_result_bytebuffer_(*Length) PUCHAR* Buffer,
                                _Out_ PULONG Length);
    NTSTATUS ReadReparsePoint(_In_opt_ PUCHAR Buffer,
                              _Inout_ PULONG BufferLength);
    NTSTATUS SetReparsePoint(
        _In_reads_bytes_(BufferLength)
            const UCHAR* Buffer,
        _In_ ULONG BufferLength);
    NTSTATUS DeleteReparsePoint(
        _In_reads_bytes_(BufferLength)
            const UCHAR* Buffer,
        _In_ ULONG BufferLength);
    NTSTATUS DeleteExternalBacking();
    NTSTATUS ReadExtendedAttributes(
        _In_opt_ PUCHAR Buffer,
        _Inout_ PULONG BufferLength,
        _Out_opt_ PEAInformationEx Information);
    NTSTATUS UpdateExtendedAttributes(
        _In_reads_(UpdateCount)
            const NtfsExtendedAttributeUpdate* Updates,
        _In_ ULONG UpdateCount);
    NTSTATUS
    SetSecurityDescriptor(
        _In_reads_bytes_(BufferLength) const UCHAR* Buffer,
        _In_ ULONG BufferLength);

    NTSTATUS ReadSecurityDescriptor(
        _In_opt_ PUCHAR Buffer,
        _Inout_ PULONG BufferLength);

    NTSTATUS
    GetBasicInformation(
        _Out_ PNtfsFileBasicInformation Information);

    NTSTATUS
    SetBasicInformation(
        _In_ const NtfsFileBasicInformation* Information);

    NTSTATUS
    SetAutomaticTimestampMask(_In_ UINT32 Fields);

    UINT32
    GetAutomaticTimestampMask() const;

    NTSTATUS
    UpdateAutomaticTimestamps(_In_ UINT32 Fields);

    NTSTATUS
    TouchDirectory();

    NTSTATUS
    StampChangeTime();

    // ./write.cpp
    NTSTATUS
    WriteFileData(_In_     AttributeType AttrType,
                  _In_opt_ PWSTR StreamName,
                  _In_     PUCHAR Buffer,
                  _Inout_  PULONG Length,
                  _In_     PLARGE_INTEGER Offset);

    NTSTATUS
    SetFileDataSize(_In_ AttributeType AttrType,
                    _In_opt_ PWSTR StreamName,
                    _In_ ULONGLONG NewSize);

    NTSTATUS
    SetFileAllocationSize(
        _In_ AttributeType AttrType,
        _In_opt_ PWSTR StreamName,
        _In_ ULONGLONG NewAllocationSize);

    NTSTATUS
    SetSparse(_In_ AttributeType AttrType,
              _In_opt_ PWSTR StreamName,
              _In_ BOOLEAN SetSparse);

    NTSTATUS
    SetZeroData(_In_ AttributeType AttrType,
                _In_opt_ PWSTR StreamName,
                _In_ ULONGLONG FileOffset,
                _In_ ULONGLONG BeyondFinalZero);

    NTSTATUS
    QueryAllocatedRanges(
        _In_ AttributeType AttrType,
        _In_opt_ PWSTR StreamName,
        _In_ ULONGLONG FileOffset,
        _In_ ULONGLONG Length,
        _Out_opt_ PNtfsAllocatedRange Ranges,
        _Inout_ PULONG RangeCount);

    NTSTATUS
    QueryRetrievalPointers(
        _In_ AttributeType AttrType,
        _In_opt_ PWSTR StreamName,
        _In_ ULONGLONG StartingVcn,
        _Out_ PULONGLONG ReturnedStartingVcn,
        _Out_opt_ PNtfsRetrievalExtent Extents,
        _Inout_ PULONG ExtentCount);

    NTSTATUS
    QueryDataStreams(
        _Out_opt_ PNtfsDataStreamInformation Streams,
        _Inout_ PULONG StreamCount);

    NTSTATUS
    UpdateResidentData(_In_ PAttribute TargetAttribute,
                       _In_ PUCHAR Buffer,
                       _In_ PULONG Length,
                       _In_ ULONGLONG Offset = 0);

    NTSTATUS
    ReplaceResidentData(_In_ PAttribute TargetAttribute,
                        _In_opt_ const UCHAR* Buffer,
                        _In_ ULONG Length);

    // ./ fixup.cpp
    NTSTATUS
    CommitFixup();

    NTSTATUS
    ApplyFixup();

private:
    friend class Directory;
    friend class MasterFileTable;

    PVolume DiskVolume;
    ULONG RecordBufferSize;
    PDataRunCacheEntry DataRunCache = NULL;
    PFileRecordExtentCacheEntry ExtentCache = NULL;
    FileRecord* BaseRecordOwner = NULL;
    PUCHAR AttributeListData = NULL;
    ULONG AttributeListLength = 0;
    UINT32 AutomaticTimestampMask =
        NTFS_AUTOMATIC_TIMESTAMP_FIELDS;
    UCHAR WofProbeState = 0; /* 0 unknown, 1 absent/non-file, 2 present */
    ULONG WofAlgorithm = 0;
    ULONG WofChunkSize = 0;

    PDataRun
    GetCachedDataRuns(_In_ PAttribute Attribute);

    NTSTATUS
    CopyCompressedData(_In_ PAttribute Attribute,
                       _In_ PUCHAR Buffer,
                       _Inout_ PULONG Length,
                       _In_ ULONGLONG Offset);

    NTSTATUS
    TryCopyWofCompressedData(_In_ PAttribute LogicalAttribute,
                             _In_ PUCHAR Buffer,
                             _Inout_ PULONG Length,
                             _In_ ULONGLONG Offset,
                             _Out_ PBOOLEAN Handled);

    NTSTATUS
    MaterializeWofCompressedData(
        _Out_ PBOOLEAN Materialized);

    void
    InvalidateWofCompression()
    {
        WofProbeState = 0;
        WofAlgorithm = 0;
        WofChunkSize = 0;
    }

    PDataRun
    BuildCompositeDataRuns(_In_ PAttribute Attribute);

    PDataRun
    DecodeDataRuns(_In_ PAttribute Attribute);

    void
    ClearDataRunCache();

    NTSTATUS
    LoadAttributeList();

    PAttribute
    FindAttributeInRecord(_In_ AttributeType Type,
                          _In_opt_ PWSTR Name,
                          _In_opt_ const USHORT *AttributeId);

    PAttribute
    FindAttributeFromList(_In_ AttributeType Type,
                          _In_opt_ PWSTR Name);

    FileRecord*
    GetExtentRecord(_In_ ULONGLONG FileReference);

    FileRecord*
    GetAttributeOwner(_In_ PAttribute Attribute);

    void
    ClearExtentCache();

    void
    ClearExtentCacheExcept(_In_opt_ FileRecord* Record);

    NTSTATUS
    ValidateAttributeForUpdate(
        _In_ PAttribute TargetAttribute,
        _In_ BOOLEAN ExpectedNonResident,
        _Out_opt_ PULONG DataLength);

    NTSTATUS
    ValidateResidentAttributeForUpdate(
        _In_ PAttribute TargetAttribute,
        _Out_opt_ PULONG DataLength);

    NTSTATUS
    GetStandardInformationForUpdate(
        _Out_ PAttribute* Attribute,
        _Out_ PStandardInformationEx* Standard);

    BOOLEAN
    HasAutomaticTimestampUpdate(_In_ UINT32 Fields) const;

    NTSTATUS
    PrepareAutomaticTimestamps(
        _In_ UINT32 Fields,
        _Out_opt_ PBOOLEAN Changed);

    NTSTATUS
    InitializeNewFileRecord(
        _In_ FileRecord* Parent,
        _In_reads_(NameLength) PWSTR Name,
        _In_ ULONG NameLength,
        _In_ BOOLEAN IsDirectory,
        _In_ ULONG FileAttributes,
        _Out_ PFileNameEx* CreatedName);

    NTSTATUS
    ReplaceListedExtendedAttributes(
        _In_opt_ PAttribute InformationAttribute,
        _In_opt_ PAttribute EaAttribute,
        _In_ const EAInformationEx* FinalInformation,
        _In_reads_bytes_(FinalLength)
            const UCHAR* FinalData,
        _In_ ULONG FinalLength);

    NTSTATUS
    ResizeAttributeRecord(_In_ PAttribute TargetAttribute,
                          _In_ ULONG NewAttributeLength);

    NTSTATUS
    ResizeResidentData(_In_ PAttribute TargetAttribute,
                       _In_ ULONG NewDataLength);

    NTSTATUS
    InsertResidentAttribute(
        _In_ AttributeType Type,
        _In_opt_ PWSTR Name,
        _Out_ PAttribute* NewAttribute);

    NTSTATUS
    InsertAttributeListEntry(
        _In_ PAttribute TargetAttribute,
        _In_ FileRecord* AttributeOwner,
        _In_ UINT32 TimestampFields);

    NTSTATUS
    CreateInitialAttributeList();

    NTSTATUS
    EnsureAttributeListForMappingGrowth(
        _In_ AttributeType AttrType,
        _In_opt_ PWSTR StreamName,
        _Inout_ PAttribute* TargetAttribute,
        _Inout_ FileRecord** AttributeOwner,
        _Out_ PBOOLEAN Created);

    NTSTATUS
    RemoveAttributeRecord(_In_ PAttribute TargetAttribute);

    NTSTATUS
    CreateNamedDataStream(
        _In_ PWSTR StreamName,
        _In_ PUCHAR Buffer,
        _Inout_ PULONG Length,
        _Inout_ PLARGE_INTEGER Offset);

    NTSTATUS
    CreateNamedDataStreamInExtension(
        _In_ PWSTR StreamName,
        _In_ PUCHAR Buffer,
        _Inout_ PULONG Length,
        _Inout_ PLARGE_INTEGER Offset);

    NTSTATUS
    ReplaceNonResidentMappingPairs(
        _Inout_ PAttribute* TargetAttribute,
        _In_ PDataRun Runs,
        _In_ ULONGLONG AllocatedSize,
        _In_ ULONGLONG DataSize,
        _In_ ULONGLONG InitializedSize,
        _Out_ PNonResidentMappingUpdate* MappingUpdate,
        _Out_opt_ FileRecord** ResultOwner = NULL);

    NTSTATUS
    CommitNonResidentMappingUpdate(
        _Inout_ PNonResidentMappingUpdate* MappingUpdate);

    void
    AbortNonResidentMappingUpdate(
        _Inout_ PNonResidentMappingUpdate* MappingUpdate);

    NTSTATUS
    BuildNonResidentMappingSegment(
        _In_ PAttribute TargetAttribute,
        _In_ PDataRun Runs,
        _In_ ULONGLONG FirstVcn,
        _In_ USHORT Flags,
        _In_ USHORT CompressionUnitSize,
        _In_ ULONGLONG AllocatedSize,
        _In_ ULONGLONG DataSize,
        _In_ ULONGLONG InitializedSize,
        _In_ ULONGLONG CompressedDataSize);

    NTSTATUS
    ConvertResidentToNonResident(
        _In_ PAttribute TargetAttribute,
        _In_ PDataRun Runs,
        _In_ ULONGLONG AllocatedSize,
        _In_ ULONGLONG DataSize,
        _In_ ULONGLONG InitializedSize);

    NTSTATUS
    ConvertNonResidentToResidentEmpty(
        _In_ PAttribute TargetAttribute);

    NTSTATUS
    PromoteResidentData(_In_ PAttribute TargetAttribute,
                        _In_opt_ PUCHAR Buffer,
                        _In_ ULONG Length,
                        _In_ ULONGLONG Offset,
                        _In_ ULONGLONG NewDataSize,
                        _In_ ULONGLONG RequestedAllocationSize);

    NTSTATUS
    ExtendNonResidentData(_In_ PAttribute TargetAttribute,
                          _In_ PUCHAR Buffer,
                          _In_ ULONG Length,
                          _In_ ULONGLONG Offset);

    NTSTATUS
    ResizeNonResidentData(_In_ PAttribute TargetAttribute,
                          _In_ ULONGLONG NewDataSize);

    NTSTATUS
    ResizeSparseData(_In_ PAttribute TargetAttribute,
                     _In_ ULONGLONG NewDataSize);

    NTSTATUS
    ResizeNonResidentStream(
        _In_ PAttribute TargetAttribute,
        _In_ ULONGLONG NewDataSize,
        _In_ ULONGLONG RequestedAllocationSize);

    NTSTATUS
    UpdateFileNameSizes(_In_ ULONGLONG AllocatedSize,
                        _In_ ULONGLONG DataSize);

    NTSTATUS
    UpdateFileNameEaSize(_In_ USHORT PackedEaSize);

    NTSTATUS
    UpdateFileNameInformation(
        _In_ UINT32 Fields,
        _In_ ULONGLONG AllocatedSize,
        _In_ ULONGLONG DataSize,
        _In_ USHORT PackedEaSize,
        _In_ ULONG ReparseTag,
        _In_ ULONG StorageFlags);

    NTSTATUS
    SynchronizeFileNameSizes(_In_ ULONGLONG AllocatedSize,
                             _In_ ULONGLONG DataSize);

    NTSTATUS
    SynchronizeFileNameEaSize(_In_ USHORT PackedEaSize);

    NTSTATUS
    SynchronizeFileNameInformation(
        _In_ UINT32 Fields,
        _In_ ULONGLONG AllocatedSize,
        _In_ ULONGLONG DataSize,
        _In_ USHORT PackedEaSize,
        _In_ ULONG ReparseTag,
        _In_ ULONG StorageFlags);

    NTSTATUS
    UpdateParentIndexEntry(
        _In_ PFileNameEx TargetName,
        _In_ ULONGLONG FileReference,
        _In_ UINT32 Fields,
        _In_ ULONGLONG AllocatedSize,
        _In_ ULONGLONG DataSize,
        _In_ USHORT PackedEaSize,
        _In_ ULONG ReparseTag,
        _In_ ULONG StorageFlags);

    NTSTATUS
    UpdateReparsePoint(
        _In_opt_ const UCHAR* Buffer,
        _In_ ULONG BufferLength,
        _In_ BOOLEAN Delete);

    // ./write.cpp
    NTSTATUS
    UpdateNonResidentData(_In_ PAttribute TargetAttribute,
                          _In_ PUCHAR Buffer,
                          _In_ PULONG Length,
                          _In_ ULONGLONG Offset = 0);

    NTSTATUS
    WriteSparseData(_In_ PAttribute TargetAttribute,
                    _In_ PUCHAR Buffer,
                    _In_ PULONG Length,
                    _In_ ULONGLONG Offset);

    NTSTATUS
    WriteCompressedData(_In_ PAttribute TargetAttribute,
                        _In_ PUCHAR Buffer,
                        _In_ PULONG Length,
                        _In_ ULONGLONG Offset);
} *PFileRecord;

struct FileRecordExtentCacheEntry
{
    PFileRecordExtentCacheEntry Next;
    ULONGLONG FileReference;
    PFileRecord Record;
};

class BTree
{
public:
    NTSTATUS ResetCurrentKey();
protected:
    ~BTree();
    PBTreeNode RootNode;
    PBTreeKey CurrentKey;
};

typedef class Directory : BTree
{
public:
    // ./directory.cpp
    Directory(_In_ PVolume DiskVolume);
    ~Directory();
    NTSTATUS
    LoadDirectory(_In_ PFileRecord File);

    // ./find.cpp
    NTSTATUS
    FindNextFile(_In_  PFileRecord File,
                 _In_  PWCHAR FileName,
                 _Out_ PULONGLONG FileRecordNumber);

    // ./get.cpp
    NTSTATUS
    GetFileBothDirInfo(_In_    BOOLEAN ReturnSingleEntry,
                       _In_    BOOLEAN RestartScan,
                       _In_    PUNICODE_STRING FileNameFilter,
                       _Inout_ PFILE_BOTH_DIR_INFORMATION Buffer,
                       _Inout_ PULONG BufferLength);

    NTSTATUS
    GetNextEntry(_In_ BOOLEAN RestartScan,
                 _Out_ PNtfsDirectoryEntry Entry);

    // ./editdir.cpp
    NTSTATUS
    AddFileToDirectory(
        _In_ PFileRecord DirectoryFile,
        _In_ ULONGLONG FileReference,
        _In_ PFileNameEx FileToAdd);

    NTSTATUS
    RemoveFileFromDirectory(
        _In_ PFileRecord DirectoryFile,
        _In_ ULONGLONG FileReference,
        _In_ PUNICODE_STRING Name);

private:
    PVolume DiskVolume;
    PUCHAR IndexAllocationData = NULL;
    ULONG IndexAllocationLength = 0;
    PDIRECTORY_KEY_BLOCK KeyBlocks = NULL;

    // ./editdir.cpp
    static NTSTATUS
    ReplaceIndexRootValue(
        _In_ PVolume DiskVolume,
        _In_ PFileRecord DirectoryFile,
        _In_reads_bytes_(ValueLength) PUCHAR Value,
        _In_ ULONG ValueLength);

    static NTSTATUS
    PushDownRoot(
        _In_ PVolume DiskVolume,
        _In_ PFileRecord DirectoryFile,
        _In_ const IndexRootEx* OldRoot,
        _In_reads_bytes_(ListBytes) PUCHAR List,
        _In_ ULONG ListBytes,
        _In_ BOOLEAN ListInternal,
        _In_ ULONG RecordSize,
        _In_ ULONGLONG AllocationUnit);

    static NTSTATUS
    SplitAndPromote(
        _In_ PVolume DiskVolume,
        _In_ PFileRecord DirectoryFile,
        _In_ ULONG RecordSize,
        _In_ ULONGLONG AllocationUnit,
        _In_reads_(PathDepth) const ULONGLONG* PathVcns,
        _In_ ULONG PathDepth,
        _Inout_updates_bytes_(RecordSize) PUCHAR NodeImage,
        _In_ ULONG LeafInsertionOffset,
        _In_ PIndexEntry FirstPending,
        _In_ ULONG FirstPendingLength);

    // ./directory.cpp
    PBTreeKey
    AllocateKey();
    void
    FreeKeyBlocks();
    NTSTATUS
    CreateNode(_Inout_ PUCHAR BitmapData,
               _In_ ULONG BitmapLength,
               _Inout_ PBTreeKey ParentNodeKey);
    NTSTATUS
    CreateRootNode(_In_  PFileRecord File,
                   _Out_ PBTreeNode *NewRootNode);
    BOOLEAN
    DoesFileNameMatch(PUNICODE_STRING NameFilter,
                      PBTreeKey Key,
                      BOOLEAN IgnoreCase = TRUE);
    PBTreeKey
    GetShortNameKey(_In_ PBTreeKey Key);

    // ./get.cpp
    BOOLEAN
    IsEligibleForFileDir(PBTreeKey Key,
                         PUNICODE_STRING FileNameFilter);

} *PDirectory;

typedef class MasterFileTable
{
public:
    ULONG FileRecordSize;

    /*
     * Where to begin looking for a free record. Scanning the whole bitmap from
     * the start on every allocation makes creating N files cost N^2; the hint
     * keeps a run of creates linear. It is only a hint: allocation still falls
     * back to a scan from the beginning, and freeing a record moves it back so
     * nothing is stranded.
     */
    ULONGLONG FreeRecordHint = 0;

    /* Scratch for the allocation bitmap scan; allocating and freeing 64 KB
     * of pool per created file cost more than the allocation itself. */
    PUCHAR AllocationScanBuffer = NULL;

    // ./ mft.cpp
    MasterFileTable(_In_ PVolume TargetVolume,
                    _In_ UINT64 MFTLCN,
                    _In_ UINT64 MFTMirrLCN,
                    _In_ INT8   ClustersPerFileRecord);
    ~MasterFileTable();

    NTSTATUS
    WriteFileRecordToMFT(_In_ PFileRecord File);

    NTSTATUS
    IsFileRecordNumberInUse(_In_  ULONG FileRecordNumber,
                            _Out_ PBOOLEAN InUse);

    NTSTATUS
    AllocateExtensionFileRecord(
        _In_ ULONGLONG BaseFileReference,
        _Out_ PFileRecord* File);

    NTSTATUS
    DeallocateExtensionFileRecord(
        _Inout_ PFileRecord File);

    // ./get.cpp
    NTSTATUS
    GetFileRecord(_In_   ULONG FileRecordNumber,
                  _Out_  PFileRecord* File);

    /* Resolves one component inside a directory whose record the caller
     * already holds, so a repeat open in a known directory does not walk the
     * path from the root again. */
    NTSTATUS
    GetFileRecordInDirectory(_In_  PFileRecord ParentDirectory,
                             _In_  PWCHAR Name,
                             _Out_ PFileRecord* File);

    NTSTATUS
    GetFileRecordFromMFTMirr(_In_  ULONG FileRecordNumber,
                             _Out_ PFileRecord* File);

    NTSTATUS
    GetFileRecordFromQuery(_In_  PWCHAR Query,
                           _Out_ PFileRecord* File,
                           _In_ BOOLEAN ReturnReparse = FALSE,
                           _In_ BOOLEAN OpenFinalReparsePoint = TRUE,
                           _Out_opt_ PULONG RemainingNameLength = NULL);

    NTSTATUS
    ReadFileRecord(
        _In_ ULONGLONG RequestedFileReference,
        _Out_ PULONGLONG ReturnedFileReference,
        _Out_opt_ PUCHAR Buffer,
        _Inout_ PULONG BufferLength);

    NTSTATUS
    QueryVolumeInformation(
        _Out_ PNtfsVolumeInformation Information);

    NTSTATUS
    CreateFile(
        _Inout_ PWCHAR Query,
        _In_ BOOLEAN IsDirectory,
        _In_ ULONG FileAttributes,
        _Out_ PFileRecord* File);

    // ./namespace.cpp
    NTSTATUS
    DeleteFile(
        _Inout_ PWCHAR Query,
        _In_ BOOLEAN RemoveDirectory);

    NTSTATUS
    RenameFile(
        _Inout_ PWCHAR OldQuery,
        _Inout_ PWCHAR NewQuery);

    NTSTATUS
    CreateHardLink(
        _Inout_ PWCHAR ExistingQuery,
        _Inout_ PWCHAR NewQuery);

    NTSTATUS
    GetFileAttributeFromFileRecordNumber(_In_  AttributeType Type,
                                         _In_  PWSTR Name,
                                         _In_  ULONG FileRecordNumber,
                                         _Out_ PFileRecord* TargetFile,
                                         _Out_ PAttribute* TargetAttribute);

private:
    // ./namespace.cpp
    NTSTATUS
    LookupParentAndChild(
        _Inout_ PWCHAR Query,
        _Out_ PFileRecord* Parent,
        _Out_ PFileRecord* Child,
        _Out_ PULONGLONG ChildReference,
        _Out_ PWCHAR* Name,
        _Out_ PULONG NameLength);

    NTSTATUS
    SplitAndResolveParent(
        _Inout_ PWCHAR Query,
        _In_ BOOLEAN ValidateName,
        _Out_ PFileRecord* Parent,
        _Out_ PWCHAR* Name,
        _Out_ PULONG NameLength);

    NTSTATUS
    EnumerateFileNames(
        _In_ PFileRecord File,
        _Inout_ PULONG Offset,
        _Out_ PAttribute* Attribute,
        _Out_ PFileNameEx* FileName);

    NTSTATUS
    FindFileNamePair(
        _In_ PFileRecord File,
        _In_ ULONGLONG ParentReference,
        _In_ PUNICODE_STRING Name,
        _Out_ PAttribute* NameAttribute,
        _Out_ PFileNameEx* NameValue,
        _Out_ PAttribute* AliasAttribute,
        _Out_ PFileNameEx* AliasValue);

    NTSTATUS
    RemoveFileNameFromRecord(
        _In_ PFileRecord File,
        _In_ ULONGLONG ParentReference,
        _In_ PUNICODE_STRING Name);

    NTSTATUS
    InsertFileNameLink(
        _In_ PFileRecord File,
        _In_ const FileNameEx* Source,
        _In_ ULONGLONG ParentReference,
        _In_reads_(NameLength) PWCHAR Name,
        _In_ ULONG NameLength,
        _Out_ PAttribute* NewAttribute,
        _Out_ PFileNameEx* NewValue);

    NTSTATUS
    CollectReleasableRuns(
        _In_ PFileRecord File,
        _Out_ PDataRun* Runs);

    NTSTATUS
    CollectDistinctExtentReferences(
        _In_ PFileRecord File,
        _Out_ PULONGLONG ExtentReferences,
        _In_ ULONG Capacity,
        _Out_ PULONG ExtentCount);

    NTSTATUS
    AllocateFileRecord(
        _In_ ULONGLONG BaseFileReference,
        _In_ BOOLEAN IsDirectory,
        _Out_ PFileRecord* File);

    NTSTATUS
    AllocateBaseFileRecord(
        _In_ BOOLEAN IsDirectory,
        _Out_ PFileRecord* File);

    NTSTATUS
    DeallocateFileRecord(
        _Inout_ PFileRecord File);

    NTSTATUS
    DeallocateBaseFileRecord(
        _Inout_ PFileRecord File);

    PVolume DiskVolume;
    UINT64 MFTLCN;
    UINT64 MFTMirrLCN;
    PFileRecord MFTFile = NULL;
    PFileRecord MFTMirrFile = NULL;

    /* $DATA attributes of the cached $MFT/$MFTMirr records, so every
     * GetFileRecord() doesn't re-walk the attribute list. The decoded run
     * lists live in each record's own data-run cache.
     */
    PAttribute MFTDataAttr = NULL;
    PAttribute MFTMirrDataAttr = NULL;
} *PMasterFileTable;
#endif // __cplusplus

 /* *** Formerly: lfs/logfile.h *** */
 enum NtfsLogOperation
 {
     NoOp                                 = 0x00,
     CompensationLogRecord                = 0x01,
     InitializeFileRecordSegment          = 0x02,
     DeallocateFileRecordSegment          = 0x03,
     WriteEndOfFileRecordSegment          = 0x04,
     CreateAttribute                      = 0x05,
     DeleteAttribute                      = 0x06,
     UpdateResidentAttributeValue         = 0x07,
     UpdateNonResidentAttributeValue      = 0x08,
     UpdateMappingPairs                   = 0x09,
     DeleteDirtyClusters                  = 0x0A,
     SetNewAttributeSizes                 = 0x0B,
     AddIndexEntryToRoot                  = 0x0C,
     DeleteIndexEntryFromRoot             = 0x0D,
     AddIndexEntryToAllocationBuffer      = 0x0E,
     DeleteIndexEntryFromAllocationBuffer = 0x0F,
     WriteEndOfIndexBuffer                = 0x10,
     SetIndexEntryVcnInRoot               = 0x11,
     SetIndexEntryVcnInAllocationBuffer   = 0x12,
     UpdateFileNameInRoot                 = 0x13,
     UpdateFileNameInAllocationBuffer     = 0x14,
     SetBitsInNonResidentBitMap           = 0x15,
     ClearBitsInNonResidentBitMap         = 0x16,
     HotFix                               = 0x17,
     EndTopLevelAction                    = 0x18,
     PrepareTransaction                   = 0x19,
     CommitTransaction                    = 0x1A,
     ForgetTransaction                    = 0x1B,
     OpenNonResidentAttribute             = 0x1C,
     OpenAttributeTableDump               = 0x1D,
     AttributeNamesDump                   = 0x1E,
     DirtyPageTableDump                   = 0x1F,
     TransactionTableDump                 = 0x20,
     UpdateRecordDataInRoot               = 0x21,
     UpdateRecordDataInAllocationBuffer   = 0x22
 };

 typedef struct NtfsLogRecord
 {
     UINT16 RedoOperation;              // Offset 0x00, Size 2
     UINT16 UndoOperation;              // Offset 0x02, Size 2
     UINT16 RedoOffset;                 // Offset 0x04, Size 2
     UINT16 RedoLength;                 // Offset 0x06, Size 2
     UINT16 UndoOffset;                 // Offset 0x08, Size 2
     UINT16 UndoLength;                 // Offset 0x0A, Size 2
     UINT16 TargetAttributeOffset;      // Offset 0x0C, Size 2
     UINT16 LCNsToFollow;               // Offset 0x0E, Size 2
     UINT16 RecordOffset;               // Offset 0x10, Size 2
     UINT16 AttributeOffset;            // Offset 0x12, Size 2
     UINT16 ClusterBlockOffset;         // Offset 0x14, Size 2
     UINT16 Reserved;                   // Offset 0x16, Size 2
     UINT64 TargetVCN;                  // Offset 0x18, Size 8
     UCHAR  LCNsForPage;                // Offset 0x20, Size Variable
 } *PNtfsLogRecord;

 typedef struct LfsClientRecord
 {
     UINT64 OldestLsn;                  // Offset 0x00, Size 8
     UINT64 ClientRestartLsn;           // Offset 0x08, Size 8
     UINT16 PrevClient;                 // Offset 0x10, Size 2
     UINT16 NextClient;                 // Offset 0x12, Size 2
     UINT16 SeqNumber;                  // Offset 0x14, Size 2
     UCHAR  Padding[6];                 // Offset 0x16, Size 6
     UINT32 ClientNameLength;           // Offset 0x1C, Size 4
     UCHAR  ClientName[128];            // Offset 0x20, Size 128
 } *PLfsClientRecord;

 typedef struct LfsRecordPage
 {
     WCHAR  Signature[4];               // Offset 0x00, Size 8
     UINT64 LastLsnOrFileOffset;        // Offset 0x08, Size 8
     UINT32 Flags;                      // Offset 0x10, Size 4
     UINT16 PageCount;                  // Offset 0x14, Size 2
     UINT16 PagePosition;               // Offset 0x16, Size 2
     UINT16 NextRecordOffset;           // Offset 0x18, Size 2
     UCHAR  Padding[6];                 // Offset 0x1A, Size 6
     UINT64 LastEndLsn;                 // Offset 0x20, Size 8
     USHORT UpdateSequenceArray;        // Offset 0x28, Size Variable
 } *PLfsRecordPage;

 typedef struct LfsRecord
 {
     UINT64 ThisLsn;                    // Offset 0x00, Size 8
     UINT64 ClientPreviousLsn;          // Offset 0x08, Size 8
     UINT64 ClientUndoNextLsn;          // Offset 0x10, Size 8
     UINT32 ClientDataLength;           // Offset 0x18, Size 4
     UINT16 ClientSeqNumber;            // Offset 0x1C, Size 2
     UINT16 ClientIndex;                // Offset 0x1E, Size 2
     UINT32 RecordType;                 // Offset 0x20, Size 4
     UINT32 TransactionId;              // Offset 0x24, Size 4
     UINT16 Flags;                      // Offset 0x28, Size 2
     UCHAR Padding[6];                  // Offset 0x2A, Size 6
     UCHAR Data;                        // Offset 0x30, Size (ClientDataLength)
 } *PLfsRecord;

 typedef struct LfsRestartPage
 {
     UCHAR  Signature[8];               // Offset 0x00, Size 8
     UINT64 ChkDskLsn;                  // Offset 0x08, Size 8
     UINT32 SystemPageSize;             // Offset 0x10, Size 4
     UINT32 LogPageSize;                // Offset 0x14, Size 4
     UINT16 RestartOffset;              // Offset 0x18, Size 2
     UINT16 MinorVersion;               // Offset 0x1A, Size 2
     UINT16 MajorVersion;               // Offset 0x1C, Size 2
     USHORT UpdateSequenceArray;        // Offset 0x1E, Size Variable
 } *PLfsRestartPage;

 typedef struct LfsRestartArea
 {
     UINT64 CurrentLsn;                 // Offset 0x00, Size 8
     UINT16 LogClients;                 // Offset 0x08, Size 2
     UINT16 ClientFreeList;             // Offset 0x0A, Size 2
     UINT16 ClientInUseList;            // Offset 0x0C, Size 2
     UINT16 Flags;                      // Offset 0x0E, Size 2
     UINT32 SeqNumberBits;              // Offset 0x10, Size 4
     UINT16 RestartAreaLength;          // Offset 0x14, Size 2
     UINT16 ClientArrayOffset;          // Offset 0x16, Size 2
     UINT64 FileSize;                   // Offset 0x18, Size 8
     UINT32 LastLsnDataLength;          // Offset 0x20, Size 4
     UINT16 RecordHeaderLength;         // Offset 0x24, Size 2
     UINT16 LogPageDataOffset;          // Offset 0x26, Size 2
     UINT32 RevisionNumber;             // Offset 0x28, Size 4
 } *PLfsRestartArea;

 /* Note: Client Version is stored in MajorVersion and MinorVersion
  * for the various versions of RestartArea/
  *
  * TODO: How do we determine which one to use?
  */

 // Used for disks formatted on Windows NT 4.0 (Size 64)
 typedef struct RestartArea
 {
     UINT32 MajorVersion;               // Offset 0x00, Size 4
     UINT32 MinorVersion;               // Offset 0x04, Size 4
     UINT64 StartOfCheckpointLsn;       // Offset 0x08, Size 8
     UINT64 OpenAttributeTableLsn;      // Offset 0x10, Size 8
     UINT64 AttributeNamesLsn;          // Offset 0x18, Size 8
     UINT64 DirtyPageTableLsn;          // Offset 0x20, Size 8
     UINT64 TransactionTableLsn;        // Offset 0x28, Size 8
     UINT32 OpenAttributeTableLength;   // Offset 0x30, Size 4
     UINT32 AttributeNamesLength;       // Offset 0x34, Size 4
     UINT32 DirtyPageTableLength;       // Offset 0x38, Size 4
     UINT32 TransactionTableLength;     // Offset 0x3C, Size 4
 } *PRestartArea;

 // Used for disks formatted on Windows 2000 - Windows Server 2003 (Size 104)
 typedef struct RestartArea2
 {
     UINT32 MajorVersion;               // Offset 0x00, Size 4
     UINT32 MinorVersion;               // Offset 0x04, Size 4
     UINT64 StartOfCheckpointLsn;       // Offset 0x08, Size 8
     UINT64 OpenAttributeTableLsn;      // Offset 0x10, Size 8
     UINT64 AttributeNamesLsn;          // Offset 0x18, Size 8
     UINT64 DirtyPageTableLsn;          // Offset 0x20, Size 8
     UINT64 TransactionTableLsn;        // Offset 0x28, Size 8
     UINT32 OpenAttributeTableLength;   // Offset 0x30, Size 4
     UINT32 AttributeNamesLength;       // Offset 0x34, Size 4
     UINT32 DirtyPageTableLength;       // Offset 0x38, Size 4
     UINT32 TransactionTableLength;     // Offset 0x3C, Size 4
     UINT64 Unknown1;                   // Offset 0x40, Size 8
     UINT64 PreviousRestartRecordLsn;   // Offset 0x48, Size 8
     UINT32 BytesPerCluster;            // Offset 0x50, Size 4
     UCHAR  Padding[4];                 // Offset 0x54, Size 4
     UINT64 UsnJournal;                 // Offset 0x58, Size 8
     UINT64 Unknown2;                   // Offset 0x60, Size 8
 } *PRestartArea2;

 // Used for disks formatted on Windows Vista+ (Size 112)
 typedef struct RestartArea3
 {
     UINT32 MajorVersion;               // Offset 0x00, Size 4
     UINT32 MinorVersion;               // Offset 0x04, Size 4
     UINT64 StartOfCheckpointLsn;       // Offset 0x08, Size 8
     UINT64 OpenAttributeTableLsn;      // Offset 0x10, Size 8
     UINT64 AttributeNamesLsn;          // Offset 0x18, Size 8
     UINT64 DirtyPageTableLsn;          // Offset 0x20, Size 8
     UINT64 TransactionTableLsn;        // Offset 0x28, Size 8
     UINT32 OpenAttributeTableLength;   // Offset 0x30, Size 4
     UINT32 AttributeNamesLength;       // Offset 0x34, Size 4
     UINT32 DirtyPageTableLength;       // Offset 0x38, Size 4
     UINT32 TransactionTableLength;     // Offset 0x3C, Size 4
     UINT64 Unknown1;                   // Offset 0x40, Size 8
     UINT64 PreviousRestartRecordLsn;   // Offset 0x48, Size 8
     UINT32 BytesPerCluster;            // Offset 0x50, Size 4
     UCHAR  Padding[4];                 // Offset 0x54, Size 4
     UINT64 UsnJournal;                 // Offset 0x58, Size 8
     UINT64 Unknown2;                   // Offset 0x60, Size 8
     UINT64 UnknownLsn;                 // Offset 0x68, Size 8
 } *PRestartArea3;

 /* Note: When an NTFS disk is formatted from a PC
  * with 32-bit Windows, the client version is set to 0.0.
  * 64-bit Windows will set the client version to 1.0.
  */

 // Observed on NTFS 1.2, Client Version 0.0
 typedef struct OpenAttributeEntry
 {
     UINT32  AllocatedOrNextFree;       // Offset 0x00, Size 4
     UINT32  PointerToAttributeName;    // Offset 0x04, Size 4
     UINT64  FileReference;             // Offset 0x08, Size 8
     UINT64  LsnOfOpenRecord;           // Offset 0x10, Size 8
     BOOLEAN DirtyPagesSeen;            // Offset 0x18, Size 1
     BOOLEAN AttributeNamePresent;      // Offset 0x19, Size 1
     UCHAR   Padding[2];                // Offset 0x1A, Size 2
     UINT32  AttributeTypeCode;         // Offset 0x1C, Size 4
     UINT64  AttributeName;             // Offset 0x20, Size 8
     UINT32  BytesPerIndexBuffer;       // Offset 0x28, Size 4
 } *POpenAttributeEntry;

 // Observed on NTFS 3.0+, Client Version 0.0
 typedef struct OpenAttributeEntry2
 {
     UINT32 AllocatedOrNextFree;        // Offset 0x00, Size 4
     UINT32 AttributeOffset;            // Offset 0x04, Size 4
     UINT64 FileReference;              // Offset 0x08, Size 8
     UINT64 LsnOfOpenRecord;            // Offset 0x10, Size 8
     UCHAR  Padding[4];                 // Offset 0x18, Size 4
     UINT32 AttributeTypeCode;          // Offset 0x1C, Size 4
     UINT64 PointerToAttributeName;     // Offset 0x20, Size 8
     UINT32 BytesPerIndexBuffer;        // Offset 0x28, Size 4
 } *POpenAttributeEntry2;

 // Observed on NTFS 3.0+, Client Version 1.0
 typedef struct OpenAttributeEntry3
 {
     UINT32  AllocatedOrNextFree;       // Offset 0x00, Size 4
     UINT32  BytesPerIndexBuffer;       // Offset 0x04, Size 4
     UINT32  AttributeTypeCode;         // Offset 0x08, Size 4
     BOOLEAN DirtyPagesSeen;            // Offset 0x0C, Size 1
     UCHAR   Padding[3];                // Offset 0x0D, Size 3
     UINT64  FileReference;             // Offset 0x10, Size 8
     UINT64  LsnOfOpenRecord;           // Offset 0x18, Size 8
     UINT64  PointerToAttributeName;    // Offset 0x20, Size 8
 } *POpenAttributeEntry3;

 // Used for Client Version 0.0
 typedef struct DirtyPageEntry
 {
     UINT32 AllocatedOrNextFree;        // Offset 0x00, Size 4
     UINT32 TargetAttributeOffset;      // Offset 0x04, Size 4
     UINT32 LengthOfTransfer;           // Offset 0x08, Size 4
     UINT32 LCNsToFollow;               // Offset 0x0C, Size 4
     UINT32 Reserved;                   // Offset 0x10, Size 4
     UINT64 VCN;                        // Offset 0x14, Size 8
     UINT64 OldestLsn;                  // Offset 0x1C, Size 8
     UINT64 LCNsForPage;                // Offset 0x24, Size (8 * LCNsToFollow)
 } *PDirtyPageEntry;

 // Used for Client Version 1.0
 typedef struct DirtyPageEntry2
 {
     UINT32 AllocatedOrNextFree;        // Offset 0x00, Size 4
     UINT32 TargetAttributeOffset;      // Offset 0x04, Size 4
     UINT32 LengthOfTransfer;           // Offset 0x08, Size 4
     UINT32 LCNsToFollow;               // Offset 0x0C, Size 4
     UINT64 VCN;                        // Offset 0x10, Size 8
     UINT64 OldestLsn;                  // Offset 0x18, Size 8
     UINT64 LCNsForPage;                // Offset 0x20, Size (8 * LCNsToFollow)
 } *PDirtyPageEntry2;

 typedef struct RestartTable
 {
     UINT16 EntrySize;                  // Offset 0x00, Size 2
     UINT16 NumberEntries;              // Offset 0x02, Size 2
     UINT16 NumberAllocated;            // Offset 0x04, Size 2
     UCHAR  Padding[6];                 // Offset 0x06, Size 6
     UINT32 FreeGoal;                   // Offset 0x0C, Size 4
     UINT32 FirstFree;                  // Offset 0x10, Size 4
     UINT32 LastFree;                   // Offset 0x14, Size 4
 } *PRestartTable;

 typedef struct TransactionEntry
 {
     UINT32 AllocatedOrNextFree;        // Offset 0x00, Size 4
     UINT32 TransactionState;           // Offset 0x04, Size 4
     UINT64 FirstLsn;                   // Offset 0x08, Size 8
     UINT64 PreviousLsn;                // Offset 0x10, Size 8
     UINT64 UndoNextLsn;                // Offset 0x18, Size 8
     UINT32 UndoRecords;                // Offset 0x20, Size 4
     UINT32 UndoBytes;                  // Offset 0x24, Size 4
 } *PTransactionEntry;

 typedef struct AttributeNameEntry
 {
     UINT16 OpenAttributeOffset;        // Offset 0x00, Size 2
     UINT16 NameLength;                 // Offset 0x02, Size 2
     WCHAR  Name;                       // Offset 0x04, Size Variable
 } *PAttributeNameEntry;

typedef struct BitmapRange
{
    UINT32 BitmapOffset;               // Offset 0x00, Size 4
    UINT32 NumberOfBits;               // Offset 0x04, Size 4
} *PBitmapRange;

/* *** Formerly: lfs/usnjrnl.h *** */

/* Notes:
 *  - UsnJrnl entries are aligned to 8-byte boundaries.
 */
 typedef struct UsnJrnlEntry
 {
     UINT32 EntrySize;           // Offset 0x00, Size 4
     UINT16 MajorVersion;        // Offset 0x04, Size 2
     UINT16 MinorVersion;        // Offset 0x06, Size 2
     UINT64 MFTReference;        // Offset 0x08, Size 8
     UINT64 ParentMFTReference;  // Offset 0x10, Size 8
     UINT64 EntryOffset;         // Offset 0x18, Size 8
     UINT64 Timestamp;           // Offset 0x20, Size 8
     UINT32 Reason;              // Offset 0x28, Size 4
     UINT32 SourceInfo;          // Offset 0x2C, Size 4
     UINT32 SecurityID;          // Offset 0x30, Size 4
     UINT32 FileAttributes;      // Offset 0x34, Size 4
     UINT16 FileNameSize;        // Offset 0x38, Size 2
     UINT16 FileNameOffset;      // Offset 0x3A, Size 2
     UCHAR  FileName;            // Offset 0x3C, Size Variable
 } *PUsnJrnlEntry;

 typedef struct UsnJrnlMaxData
 {
     UINT64 MaxSize;             // Offset 0x00, Size 8
     UINT64 AllocationDelta;     // Offset 0x08, Size 8
     UINT64 USN_ID;              // Offset 0x10, Size 8
     UINT64 LowestValidUSN;      // Offset 0x18, Size 8
 } *PUsnJrnlMaxData;

/* *** Formerly: lfs/lfs.h *** */

#ifdef __cplusplus

typedef class LogFileService
{
public:
    ULONG ClientMajorVersion;
    ULONG ClientMinorVersion;

    LogFileService(_In_ PVolume TargetVolume);
    ~LogFileService();
    NTSTATUS InitializeLFS();
    NTSTATUS ShutdownLFS();

    /* Roadmap: LogTransaction()/CommitTransaction() for journaling and a
     * periodic WriteCheckpointRecord() will be added with the LFS write path.
     */
private:
    PVolume DiskVolume;
    PFileRecord LogFile;
    PUCHAR LogFileData;

    PLfsRestartPage RestartPage1;
    PLfsRestartPage RestartPage2;

    // Call when creating LFS Object
    NTSTATUS PerformFileSystemRecovery();

    BOOLEAN IsSupportedClientVersion()
    {
        // Supported versions currently include: 0.0, 1.0, 1.1
        if (ClientMajorVersion == 1)
        {
            return ClientMinorVersion == 0
                   || ClientMinorVersion == 1;
        }

        else if (ClientMajorVersion == 0)
        {
            return ClientMinorVersion == 0;
        }

        return FALSE;
    }
} *PLogFileService;

#endif // __cplusplus

#endif /* _NTFSLIB_NEW_INTERNAL_H_ */
