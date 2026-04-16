/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Resident directory enumeration helpers for staged NTFS port
 *
 * Integration note:
 * - Add prototypes for the exported helper(s) below to ntfslx.h.
 * - Wire IRP_MJ_DIRECTORY_CONTROL in dispatch.c to a query-directory layer
 *   that consumes NTFSLX_DIR_ENTRY records.
 * - Add this file to the ntfslx target once the main thread is ready.
 * - Nonresident $INDEX_ALLOCATION traversal is intentionally deferred.
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

#define NTFSLX_DIR_MAX_NAME_CHARS 255
#define NTFSLX_DIR_INDEX_ROOT_TYPE 0x00000090UL
#define NTFSLX_DIR_INDEX_ENTRY_NODE 0x0001
#define NTFSLX_DIR_INDEX_ENTRY_END 0x0002
#define NTFSLX_DIR_INDEX_SMALL      0x00000000UL
#define NTFSLX_DIR_INDEX_LARGE      0x00000001UL

typedef struct _NTFSLX_DIR_INDEX_HEADER
{
    ULONG EntriesOffset;
    ULONG IndexLength;
    ULONG AllocatedSize;
    UCHAR Flags;
    UCHAR Reserved[3];
} NTFSLX_DIR_INDEX_HEADER, *PNTFSLX_DIR_INDEX_HEADER;

typedef struct _NTFSLX_DIR_INDEX_ROOT
{
    ULONG Type;
    ULONG CollationRule;
    ULONG IndexBlockSize;
    UCHAR ClustersPerIndexBlock;
    UCHAR Reserved[3];
    NTFSLX_DIR_INDEX_HEADER Index;
} NTFSLX_DIR_INDEX_ROOT, *PNTFSLX_DIR_INDEX_ROOT;

typedef struct _NTFSLX_DIR_INDEX_ENTRY_HEADER
{
    union
    {
        struct
        {
            ULONGLONG IndexedFile;
        } Dir;
        struct
        {
            USHORT DataOffset;
            USHORT DataLength;
            ULONG ReservedV;
        } Value;
    } Data;
    USHORT Length;
    USHORT KeyLength;
    USHORT Flags;
    USHORT Reserved;
} NTFSLX_DIR_INDEX_ENTRY_HEADER, *PNTFSLX_DIR_INDEX_ENTRY_HEADER;

typedef struct _NTFSLX_DIR_FILE_NAME_ATTR
{
    ULONGLONG ParentDirectory;
    ULONGLONG CreationTime;
    ULONGLONG LastDataChangeTime;
    ULONGLONG LastMftChangeTime;
    ULONGLONG LastAccessTime;
    ULONGLONG AllocatedSize;
    ULONGLONG DataSize;
    ULONG FileAttributes;
    union
    {
        struct
        {
            USHORT PackedEaSize;
            USHORT Reserved;
        } Ea;
        struct
        {
            ULONG ReparsePointTag;
        } Rp;
    } Type;
    UCHAR FileNameLength;
    UCHAR FileNameType;
    WCHAR FileName[1];
} NTFSLX_DIR_FILE_NAME_ATTR, *PNTFSLX_DIR_FILE_NAME_ATTR;

typedef struct _NTFSLX_DIR_ATTR_RECORD
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
} NTFSLX_DIR_ATTR_RECORD, *PNTFSLX_DIR_ATTR_RECORD;

typedef struct _NTFSLX_DIR_MFT_RECORD
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
} NTFSLX_DIR_MFT_RECORD, *PNTFSLX_DIR_MFT_RECORD;

C_ASSERT(sizeof(NTFSLX_DIR_INDEX_HEADER) == 16);
C_ASSERT(sizeof(NTFSLX_DIR_INDEX_ROOT) == 32);
C_ASSERT(sizeof(NTFSLX_DIR_INDEX_ENTRY_HEADER) == 16);

static
BOOLEAN
NtfslxDirIsDirectoryIndex(
    _In_ const NTFSLX_DIR_INDEX_ROOT *IndexRoot)
{
    return IndexRoot->Type == NTFSLX_DIR_INDEX_ROOT_TYPE;
}

static
BOOLEAN
NtfslxDirEntryIsValid(
    _In_ const NTFSLX_DIR_INDEX_ENTRY_HEADER *Entry,
    _In_ ULONG Remaining,
    _Out_ PULONG KeyLength)
{
    if (Remaining < sizeof(*Entry) || Entry->Length < sizeof(*Entry) || Entry->Length > Remaining)
    {
        return FALSE;
    }

    if (Entry->Flags & NTFSLX_DIR_INDEX_ENTRY_END)
    {
        *KeyLength = 0;
        return TRUE;
    }

    if (Entry->KeyLength < FIELD_OFFSET(NTFSLX_DIR_FILE_NAME_ATTR, FileName) - sizeof(WCHAR))
    {
        return FALSE;
    }

    *KeyLength = Entry->KeyLength;
    return TRUE;
}

static
NTSTATUS
NtfslxDirCopyFileName(
    _In_ const NTFSLX_DIR_FILE_NAME_ATTR *FileNameAttr,
    _Out_ PNTFSLX_DIR_ENTRY Entry)
{
    ULONG NameLength;

    NameLength = FileNameAttr->FileNameLength;
    if (NameLength > NTFSLX_DIR_MAX_NAME_CHARS)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Entry->ParentReference = FileNameAttr->ParentDirectory;
    Entry->CreationTime.QuadPart = (LONGLONG)FileNameAttr->CreationTime;
    Entry->LastDataChangeTime.QuadPart = (LONGLONG)FileNameAttr->LastDataChangeTime;
    Entry->LastMftChangeTime.QuadPart = (LONGLONG)FileNameAttr->LastMftChangeTime;
    Entry->LastAccessTime.QuadPart = (LONGLONG)FileNameAttr->LastAccessTime;
    Entry->AllocatedSize = FileNameAttr->AllocatedSize;
    Entry->DataSize = FileNameAttr->DataSize;
    Entry->FileAttributes = FileNameAttr->FileAttributes;
    Entry->FileNameType = FileNameAttr->FileNameType;
    Entry->FileNameLength = (UCHAR)NameLength;

    RtlZeroMemory(Entry->FileName, sizeof(Entry->FileName));
    if (NameLength != 0)
    {
        RtlCopyMemory(Entry->FileName, FileNameAttr->FileName, NameLength * sizeof(WCHAR));
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxDirExtractEntry(
    _In_ const NTFSLX_DIR_INDEX_ENTRY_HEADER *IndexEntry,
    _In_ ULONG Remaining,
    _Out_ PNTFSLX_DIR_ENTRY Entry)
{
    const NTFSLX_DIR_FILE_NAME_ATTR *FileNameAttr;
    NTSTATUS Status;

    if (IndexEntry->Flags & NTFSLX_DIR_INDEX_ENTRY_END)
    {
        return STATUS_NO_MORE_FILES;
    }

    if (IndexEntry->KeyLength > Remaining - sizeof(*IndexEntry))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    FileNameAttr = (const NTFSLX_DIR_FILE_NAME_ATTR *)
        ((const UCHAR *)IndexEntry + sizeof(*IndexEntry));
    if (IndexEntry->KeyLength < FIELD_OFFSET(NTFSLX_DIR_FILE_NAME_ATTR, FileName))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    RtlZeroMemory(Entry, sizeof(*Entry));
    Entry->FileReference = IndexEntry->Data.Dir.IndexedFile;
    Entry->IndexEntryOffset = 0;
    Entry->IndexEntryFlags = IndexEntry->Flags;
    Entry->HasChildNode = BooleanFlagOn(IndexEntry->Flags, NTFSLX_DIR_INDEX_ENTRY_NODE);
    if (Entry->HasChildNode)
    {
        Entry->ChildVcn = *(const ULONGLONG *)
            ((const UCHAR *)IndexEntry + IndexEntry->Length - sizeof(ULONGLONG));
    }

    Status = NtfslxDirCopyFileName(FileNameAttr, Entry);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxDirWalkResidentIndexRoot(
    _In_reads_bytes_(RootLength) const NTFSLX_DIR_INDEX_ROOT *IndexRoot,
    _In_ ULONG RootLength,
    _In_ PNTFSLX_DIR_ENUM_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    ULONG Offset;
    ULONG UsedLength;
    ULONG KeyLength;
    PNTFSLX_DIR_INDEX_ENTRY_HEADER IndexEntry;
    NTFSLX_DIR_ENTRY Entry;
    NTSTATUS Status;

    if (RootLength < sizeof(*IndexRoot) || !NtfslxDirIsDirectoryIndex(IndexRoot))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (IndexRoot->Index.EntriesOffset < sizeof(*IndexRoot) ||
        IndexRoot->Index.EntriesOffset > RootLength ||
        IndexRoot->Index.IndexLength > RootLength ||
        IndexRoot->Index.EntriesOffset > IndexRoot->Index.IndexLength)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (IndexRoot->Index.Flags == NTFSLX_DIR_INDEX_LARGE)
    {
        /*
         * The root is still resident, but the tree extends into
         * $INDEX_ALLOCATION. Enumerating only the resident path is a safe
         * partial implementation until subnodes are wired in.
         */
        NTFSDBG("ntfslx: resident directory enumeration sees LARGE index; subnodes deferred\n");
    }

    Offset = IndexRoot->Index.EntriesOffset;
    UsedLength = IndexRoot->Index.IndexLength;
    while (Offset < UsedLength)
    {
        IndexEntry = (PNTFSLX_DIR_INDEX_ENTRY_HEADER)
            ((const UCHAR *)IndexRoot + Offset);
        if (!NtfslxDirEntryIsValid(IndexEntry, UsedLength - Offset, &KeyLength))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (IndexEntry->Flags & NTFSLX_DIR_INDEX_ENTRY_END)
        {
            break;
        }

        Status = NtfslxDirExtractEntry(IndexEntry, UsedLength - Offset, &Entry);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        Entry.IndexEntryOffset = Offset;

        Status = Callback(&Entry, Context);
        if (Status == STATUS_NO_MORE_FILES)
        {
            return STATUS_SUCCESS;
        }

        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        if (IndexEntry->Length == 0)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Offset += IndexEntry->Length;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxDirFindAttribute(
    _In_ const NTFSLX_DIR_MFT_RECORD *FileRecord,
    _In_ ULONG AttributeType,
    _In_reads_opt_(NameLength) PCWSTR Name,
    _In_ ULONG NameLength,
    _Out_ PNTFSLX_DIR_ATTR_RECORD *Attribute)
{
    ULONG Offset;
    PNTFSLX_DIR_ATTR_RECORD CurrentAttribute;

    Offset = FileRecord->AttributesOffset;
    while (Offset + sizeof(ULONG) * 2 <= FileRecord->BytesInUse)
    {
        CurrentAttribute = (PNTFSLX_DIR_ATTR_RECORD)
            ((const UCHAR *)FileRecord + Offset);
        if (CurrentAttribute->Type == NTFSLX_ATTRIBUTE_END)
        {
            break;
        }

        if (CurrentAttribute->Length == 0 ||
            Offset + CurrentAttribute->Length > FileRecord->BytesInUse)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (CurrentAttribute->Type == AttributeType &&
            CurrentAttribute->NameLength == NameLength)
        {
            if (NameLength == 0)
            {
                *Attribute = CurrentAttribute;
                return STATUS_SUCCESS;
            }

            if (CurrentAttribute->NameOffset + NameLength * sizeof(WCHAR) <= CurrentAttribute->Length &&
                RtlCompareMemory((const UCHAR *)CurrentAttribute + CurrentAttribute->NameOffset,
                                 Name,
                                 NameLength * sizeof(WCHAR)) == NameLength * sizeof(WCHAR))
            {
                *Attribute = CurrentAttribute;
                return STATUS_SUCCESS;
            }
        }

        Offset += CurrentAttribute->Length;
    }

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

/*
 * Public helper for future IRP_MJ_DIRECTORY_CONTROL integration.
 * The main thread can map the callback output into FILE_*_DIR_INFORMATION.
 */
NTSTATUS
NtfslxEnumerateDirectoryRoot(
    _In_reads_bytes_(RootLength) const VOID *IndexRoot,
    _In_ ULONG RootLength,
    _In_ PNTFSLX_DIR_ENUM_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    return NtfslxDirWalkResidentIndexRoot((const NTFSLX_DIR_INDEX_ROOT *)IndexRoot,
                                          RootLength,
                                          Callback,
                                          Context);
}

NTSTATUS
NtfslxEnumerateDirectoryFromMftRecord(
    _In_ const VOID *DirectoryMftRecord,
    _In_ ULONG DirectoryRecordLength,
    _In_ PNTFSLX_DIR_ENUM_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    const NTFSLX_DIR_MFT_RECORD *FileRecord;
    PNTFSLX_DIR_ATTR_RECORD IndexRootAttribute;
    const NTFSLX_DIR_INDEX_ROOT *IndexRoot;
    NTSTATUS Status;

    if (DirectoryMftRecord == NULL || Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DirectoryRecordLength < sizeof(NTFSLX_DIR_MFT_RECORD))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    FileRecord = (const NTFSLX_DIR_MFT_RECORD *)DirectoryMftRecord;
    if (FileRecord->Ntfs.Magic != NTFSLX_RECORD_MAGIC_FILE)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = NtfslxDirFindAttribute(FileRecord,
                                    NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                    L"$I30",
                                    4,
                                    &IndexRootAttribute);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (IndexRootAttribute->NonResident != 0)
    {
        /*
         * The resident root is mandatory. Large directories may still need
         * subnodes from $INDEX_ALLOCATION, which is intentionally deferred.
         */
        return STATUS_NOT_IMPLEMENTED;
    }

    if (IndexRootAttribute->Data.Resident.ValueLength < sizeof(NTFSLX_DIR_INDEX_ROOT) ||
        IndexRootAttribute->Data.Resident.ValueOffset + sizeof(NTFSLX_DIR_INDEX_ROOT) > IndexRootAttribute->Length)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IndexRoot = (const NTFSLX_DIR_INDEX_ROOT *)
        ((const UCHAR *)IndexRootAttribute + IndexRootAttribute->Data.Resident.ValueOffset);
    return NtfslxDirWalkResidentIndexRoot(IndexRoot,
                                          IndexRootAttribute->Data.Resident.ValueLength,
                                          Callback,
                                          Context);
}

NTSTATUS
NtfslxCopyDirectoryEntryToBothInformation(
    _In_ const NTFSLX_DIR_ENTRY *Entry,
    _Inout_updates_bytes_(BufferLength) PFILE_BOTH_DIR_INFORMATION Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG NextOffset)
{
    ULONG RequiredLength;
    ULONG NameBytes;

    if (Entry == NULL || Buffer == NULL || NextOffset == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    NameBytes = Entry->FileNameLength * sizeof(WCHAR);
    RequiredLength = FIELD_OFFSET(FILE_BOTH_DIR_INFORMATION, FileName) + NameBytes;
    if (BufferLength < RequiredLength)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    RtlZeroMemory(Buffer, RequiredLength);
    Buffer->FileIndex = 0;
    Buffer->CreationTime = Entry->CreationTime;
    Buffer->LastAccessTime = Entry->LastAccessTime;
    Buffer->LastWriteTime = Entry->LastDataChangeTime;
    Buffer->ChangeTime = Entry->LastMftChangeTime;
    Buffer->EndOfFile.QuadPart = (LONGLONG)Entry->DataSize;
    Buffer->AllocationSize.QuadPart = (LONGLONG)Entry->AllocatedSize;
    Buffer->FileAttributes = Entry->FileAttributes;
    Buffer->FileNameLength = NameBytes;
    Buffer->EaSize = 0;
    Buffer->ShortNameLength = 0;
    RtlCopyMemory(Buffer->FileName, Entry->FileName, NameBytes);

    *NextOffset = ROUND_UP(RequiredLength, sizeof(ULONGLONG));
    return STATUS_SUCCESS;
}

/*
 * NTFS directory enumeration is intentionally limited to the resident
 * $INDEX_ROOT path here. The main thread can decide whether to feed these
 * helpers into FILE_*_DIR_INFORMATION or a custom query-directory adapter.
 */
