/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Large-directory-aware component lookup helpers for ntfslx
 *
 * Integration note:
 * - This file is intentionally self-contained and only consumes helper
 *   surfaces already present in the staged ntfslx tree.
 * - Resident $INDEX_ROOT enumeration is used first.
 * - Large-directory $INDEX_ALLOCATION traversal is used when the helper
 *   surface is linked in.
 */

#include "ntfslx.h"

#include <debug.h>

#ifndef NTFSLX_TRACE_ALWAYS
#define NTFSLX_TRACE_ALWAYS DbgPrint
#endif

#define NTFSLX_TRACE_LOOKUP(...) NTFSLX_TRACE_ALWAYS(__VA_ARGS__)

static
VOID
NtfslxTraceLookupComponent(
    _In_ PCSTR Prefix,
    _In_ PCUNICODE_STRING ComponentName,
    _In_ ULONGLONG CurrentIndex,
    _In_ BOOLEAN IsFinalComponent)
{
    NTFSLX_TRACE_LOOKUP("ntfslx: %s component='%wZ' index=%I64u final=%u\n",
                        Prefix,
                        ComponentName,
                        CurrentIndex,
                        IsFinalComponent);
}

#define NTFSLX_PATHLOOKUP_INDEX_ROOT_TYPE NTFSLX_ATTRIBUTE_FILE_NAME
#define NTFSLX_PATHLOOKUP_INDEX_ENTRY_NODE 0x0001
#define NTFSLX_PATHLOOKUP_INDEX_ENTRY_END  0x0002
#define NTFSLX_PATHLOOKUP_INDEX_SMALL      0x00000000UL
#define NTFSLX_PATHLOOKUP_INDEX_LARGE      0x00000001UL
#define NTFSLX_PATHLOOKUP_INDEX_ALLOCATION_TAG 'aIxN'

#include <pshpack1.h>
typedef struct _NTFSLX_PATHLOOKUP_INDEX_HEADER
{
    ULONG EntriesOffset;
    ULONG IndexLength;
    ULONG AllocatedSize;
    UCHAR Flags;
    UCHAR Reserved[3];
} NTFSLX_PATHLOOKUP_INDEX_HEADER, *PNTFSLX_PATHLOOKUP_INDEX_HEADER;

typedef struct _NTFSLX_PATHLOOKUP_INDEX_ROOT
{
    ULONG Type;
    ULONG CollationRule;
    ULONG IndexBlockSize;
    UCHAR ClustersPerIndexBlock;
    UCHAR Reserved[3];
    NTFSLX_PATHLOOKUP_INDEX_HEADER Index;
} NTFSLX_PATHLOOKUP_INDEX_ROOT, *PNTFSLX_PATHLOOKUP_INDEX_ROOT;

typedef struct _NTFSLX_PATHLOOKUP_INDEX_ENTRY_HEADER
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
} NTFSLX_PATHLOOKUP_INDEX_ENTRY_HEADER, *PNTFSLX_PATHLOOKUP_INDEX_ENTRY_HEADER;

typedef struct _NTFSLX_PATHLOOKUP_FILE_NAME_ATTRIBUTE
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
} NTFSLX_PATHLOOKUP_FILE_NAME_ATTRIBUTE, *PNTFSLX_PATHLOOKUP_FILE_NAME_ATTRIBUTE;
#include <poppack.h>

C_ASSERT(sizeof(NTFSLX_PATHLOOKUP_INDEX_HEADER) == 16);
C_ASSERT(sizeof(NTFSLX_PATHLOOKUP_INDEX_ROOT) == 32);
C_ASSERT(sizeof(NTFSLX_PATHLOOKUP_INDEX_ENTRY_HEADER) == 16);

typedef struct _NTFSLX_PATHLOOKUP_DIRECTORY_SEARCH_CONTEXT
{
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    PCUNICODE_STRING ComponentName;
    BOOLEAN IgnoreCase;
    BOOLEAN Found;
    UCHAR Reserved[2];
    ULONGLONG FoundIndex;
} NTFSLX_PATHLOOKUP_DIRECTORY_SEARCH_CONTEXT, *PNTFSLX_PATHLOOKUP_DIRECTORY_SEARCH_CONTEXT;

typedef struct _NTFSLX_PATHLOOKUP_PATH_CONTEXT
{
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    ULONGLONG CurrentIndex;
} NTFSLX_PATHLOOKUP_PATH_CONTEXT, *PNTFSLX_PATHLOOKUP_PATH_CONTEXT;

NTSTATUS
NtfslxBuildIndexAllocationRunlist(
    _In_ PNTFSLX_ATTR_RECORD IndexAllocationAttribute,
    _Outptr_ PNTFSLX_RUNLIST_ELEMENT *Runlist,
    _Out_ PULONG RunlistCount);

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

static
BOOLEAN
NtfslxLookupNamesMatch(
    _In_ const NTFSLX_DIR_ENTRY *Entry,
    _In_ PNTFSLX_PATHLOOKUP_DIRECTORY_SEARCH_CONTEXT SearchContext)
{
    ULONG ComponentLength;
    BOOLEAN NamesEqual;

    if (Entry == NULL || SearchContext == NULL || SearchContext->ComponentName == NULL)
    {
        return FALSE;
    }

    ComponentLength = SearchContext->ComponentName->Length / sizeof(WCHAR);
    if (Entry->FileNameLength != ComponentLength)
    {
        return FALSE;
    }

    NamesEqual = NtfslxAreNamesEqual(Entry->FileName,
                                     Entry->FileNameLength,
                                     SearchContext->ComponentName->Buffer,
                                     ComponentLength,
                                     SearchContext->IgnoreCase,
                                     SearchContext->DeviceExtension->UpcaseTable,
                                     SearchContext->DeviceExtension->UpcaseTableLength);
    if (NamesEqual)
    {
        return TRUE;
    }

    if ((Entry->FileNameType == 0x1 || Entry->FileNameType == 0x3) &&
        SearchContext->IgnoreCase)
    {
        NTFSLX_TRACE_LOOKUP("ntfslx: pathlookup missed DOS/win32 name match type=%u name='%.*S' target='%wZ'\n",
                            Entry->FileNameType,
                            Entry->FileNameLength,
                            Entry->FileName,
                            SearchContext->ComponentName);
    }

    return FALSE;
}

static
NTSTATUS
NTAPI
NtfslxLookupDirectoryEnumerateCallback(
    _In_ const NTFSLX_DIR_ENTRY *Entry,
    _In_opt_ PVOID Context)
{
    PNTFSLX_PATHLOOKUP_DIRECTORY_SEARCH_CONTEXT SearchContext;

    SearchContext = Context;
    if (SearchContext == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    NTFSLX_TRACE_LOOKUP("ntfslx: lookup candidate type=%u attrs=0x%08lx ref=%I64u name='%.*S' target='%wZ'\n",
                        Entry->FileNameType,
                        Entry->FileAttributes,
                        Entry->FileReference,
                        Entry->FileNameLength,
                        Entry->FileName,
                        SearchContext->ComponentName);

    if (!NtfslxLookupNamesMatch(Entry, SearchContext))
    {
        return STATUS_SUCCESS;
    }

    NTFSLX_TRACE_LOOKUP("ntfslx: lookup match type=%u attrs=0x%08lx ref=%I64u name='%.*S'\n",
                        Entry->FileNameType,
                        Entry->FileAttributes,
                        Entry->FileReference,
                        Entry->FileNameLength,
                        Entry->FileName);

    SearchContext->Found = TRUE;
    SearchContext->FoundIndex = Entry->FileReference;
    return STATUS_NO_MORE_FILES;
}

static
NTSTATUS
NtfslxLookupParentIndexFromRecord(
    _In_reads_bytes_(DirectoryRecordLength) const VOID *DirectoryMftRecord,
    _In_ ULONG DirectoryRecordLength,
    _Out_ PULONGLONG ParentIndex)
{
    PNTFSLX_MFT_RECORD FileRecord;
    PNTFSLX_ATTR_RECORD Attribute;
    const NTFSLX_PATHLOOKUP_FILE_NAME_ATTRIBUTE *FileNameAttribute;
    NTSTATUS Status;

    if (DirectoryMftRecord == NULL || ParentIndex == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DirectoryRecordLength < sizeof(NTFSLX_MFT_RECORD))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    FileRecord = (PNTFSLX_MFT_RECORD)DirectoryMftRecord;
    if (FileRecord->Ntfs.Magic != NTFSLX_RECORD_MAGIC_FILE)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = NtfslxFindAttribute(FileRecord,
                                 NTFSLX_ATTRIBUTE_FILE_NAME,
                                 NULL,
                                 0,
                                 &Attribute);
    if (!NT_SUCCESS(Status))
    {
        return (Status == STATUS_OBJECT_NAME_NOT_FOUND)
            ? STATUS_FILE_CORRUPT_ERROR
            : Status;
    }

    if (Attribute->NonResident != 0 ||
        Attribute->Data.Resident.ValueOffset + FIELD_OFFSET(NTFSLX_PATHLOOKUP_FILE_NAME_ATTRIBUTE, FileName) > Attribute->Length)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    FileNameAttribute = (const NTFSLX_PATHLOOKUP_FILE_NAME_ATTRIBUTE *)
        ((const UCHAR *)Attribute + Attribute->Data.Resident.ValueOffset);
    if (Attribute->Data.Resident.ValueOffset +
        FIELD_OFFSET(NTFSLX_PATHLOOKUP_FILE_NAME_ATTRIBUTE, FileName) +
        ((ULONG)FileNameAttribute->FileNameLength * sizeof(WCHAR)) > Attribute->Length)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    *ParentIndex = MREF(FileNameAttribute->ParentDirectory);
    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxLookupDirectoryComponent(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_reads_bytes_(DirectoryRecordLength) const VOID *DirectoryMftRecord,
    _In_ ULONG DirectoryRecordLength,
    _In_ ULONGLONG CurrentIndex,
    _In_ PCUNICODE_STRING ComponentName,
    _In_ BOOLEAN IsFinalComponent,
    _Out_ PULONGLONG ChildIndex)
{
    const NTFSLX_PATHLOOKUP_INDEX_ROOT *IndexRoot;
    PNTFSLX_ATTR_RECORD IndexRootAttribute;
    PNTFSLX_ATTR_RECORD IndexAllocationAttribute;
    NTFSLX_PATHLOOKUP_DIRECTORY_SEARCH_CONTEXT SearchContext;
    PNTFSLX_RUNLIST_ELEMENT IndexAllocationRunlist;
    ULONG IndexAllocationRunlistCount;
    ULONG IndexRootLength;
    NTSTATUS Status;
    BOOLEAN LargeIndex;

    if (ChildIndex != NULL)
    {
        *ChildIndex = CurrentIndex;
    }

    if (DeviceExtension == NULL ||
        DirectoryMftRecord == NULL ||
        ComponentName == NULL ||
        ChildIndex == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (ComponentName->Length == 0 ||
        (ComponentName->Length & (sizeof(WCHAR) - 1)) != 0 ||
        ComponentName->Buffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    NtfslxTraceLookupComponent("lookup begin", ComponentName, CurrentIndex, IsFinalComponent);

    if (NtfslxPathIsDotComponent(ComponentName))
    {
        *ChildIndex = CurrentIndex;
        return STATUS_SUCCESS;
    }

    if (NtfslxPathIsDotDotComponent(ComponentName))
    {
        Status = NtfslxLookupParentIndexFromRecord(DirectoryMftRecord,
                                                   DirectoryRecordLength,
                                                   ChildIndex);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        return STATUS_SUCCESS;
    }

    if (DirectoryRecordLength < sizeof(NTFSLX_MFT_RECORD))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    if (((const PNTFSLX_MFT_RECORD)DirectoryMftRecord)->Ntfs.Magic != NTFSLX_RECORD_MAGIC_FILE)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if ((((const PNTFSLX_MFT_RECORD)DirectoryMftRecord)->Flags &
         NTFSLX_MFT_RECORD_IS_DIRECTORY) == 0)
    {
        return IsFinalComponent ? STATUS_OBJECT_NAME_NOT_FOUND
                                : STATUS_OBJECT_PATH_NOT_FOUND;
    }

    if (!IsFinalComponent &&
        ComponentName->Length == sizeof(L"System Volume Information") - sizeof(WCHAR) &&
        RtlCompareMemory(ComponentName->Buffer,
                         L"System Volume Information",
                         sizeof(L"System Volume Information") - sizeof(WCHAR)) ==
            (sizeof(L"System Volume Information") - sizeof(WCHAR)))
    {
        return STATUS_REPARSE_POINT_NOT_RESOLVED;
    }

    Status = NtfslxFindAttribute((PNTFSLX_MFT_RECORD)DirectoryMftRecord,
                                 NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                 L"$I30",
                                 4,
                                 &IndexRootAttribute);
    if (!NT_SUCCESS(Status))
    {
        NTFSLX_TRACE_LOOKUP("ntfslx: lookup missing $I30 root component='%wZ' index=%I64u status=0x%08lx\n",
                            ComponentName,
                            CurrentIndex,
                            Status);
        return (Status == STATUS_OBJECT_NAME_NOT_FOUND)
            ? STATUS_FILE_CORRUPT_ERROR
            : Status;
    }

    if (IndexRootAttribute->NonResident != 0 ||
        IndexRootAttribute->Data.Resident.ValueLength < sizeof(NTFSLX_PATHLOOKUP_INDEX_ROOT) ||
        IndexRootAttribute->Data.Resident.ValueOffset > IndexRootAttribute->Length ||
        IndexRootAttribute->Data.Resident.ValueLength > IndexRootAttribute->Length - IndexRootAttribute->Data.Resident.ValueOffset ||
        IndexRootAttribute->Data.Resident.ValueOffset + sizeof(NTFSLX_PATHLOOKUP_INDEX_ROOT) > IndexRootAttribute->Length)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IndexRoot = (const NTFSLX_PATHLOOKUP_INDEX_ROOT *)
        ((const UCHAR *)IndexRootAttribute + IndexRootAttribute->Data.Resident.ValueOffset);
    IndexRootLength = IndexRootAttribute->Data.Resident.ValueLength;
    LargeIndex = BooleanFlagOn(IndexRoot->Index.Flags, NTFSLX_PATHLOOKUP_INDEX_LARGE);

    RtlZeroMemory(&SearchContext, sizeof(SearchContext));
    SearchContext.DeviceExtension = DeviceExtension;
    SearchContext.ComponentName = ComponentName;
    SearchContext.IgnoreCase = (DeviceExtension->UpcaseTable != NULL);

    Status = NtfslxEnumerateDirectoryRoot(IndexRoot,
                                          IndexRootLength,
                                          NtfslxLookupDirectoryEnumerateCallback,
                                          &SearchContext);
    if (!NT_SUCCESS(Status))
    {
        NTFSLX_TRACE_LOOKUP("ntfslx: resident root enumeration failed component='%wZ' index=%I64u status=0x%08lx\n",
                            ComponentName,
                            CurrentIndex,
                            Status);
        return Status;
    }

    if (!SearchContext.Found)
    {
        NTFSLX_TRACE_LOOKUP("ntfslx: root lookup miss for '%wZ' LargeIndex=%u CurrentIndex=%I64u\n",
                            ComponentName,
                            LargeIndex,
                            CurrentIndex);
    }

    if (SearchContext.Found)
    {
        *ChildIndex = SearchContext.FoundIndex;
        return STATUS_SUCCESS;
    }

    if (!LargeIndex)
    {
        NTFSLX_TRACE_LOOKUP("ntfslx: no large index for component='%wZ' index=%I64u returning=0x%08lx\n",
                            ComponentName,
                            CurrentIndex,
                            IsFinalComponent ? STATUS_OBJECT_NAME_NOT_FOUND : STATUS_OBJECT_PATH_NOT_FOUND);
        return IsFinalComponent ? STATUS_OBJECT_NAME_NOT_FOUND
                                : STATUS_OBJECT_PATH_NOT_FOUND;
    }

    IndexAllocationRunlist = NULL;
    IndexAllocationRunlistCount = 0;
    Status = NtfslxFindAttribute((PNTFSLX_MFT_RECORD)DirectoryMftRecord,
                                 NTFSLX_ATTRIBUTE_INDEX_ALLOCATION,
                                 L"$I30",
                                 4,
                                 &IndexAllocationAttribute);
    if (!NT_SUCCESS(Status))
    {
        NTFSLX_TRACE_LOOKUP("ntfslx: missing INDEX_ALLOCATION component='%wZ' index=%I64u status=0x%08lx\n",
                            ComponentName,
                            CurrentIndex,
                            Status);
        return STATUS_NOT_IMPLEMENTED;
    }

    Status = NtfslxBuildIndexAllocationRunlist(IndexAllocationAttribute,
                                               &IndexAllocationRunlist,
                                               &IndexAllocationRunlistCount);
    if (!NT_SUCCESS(Status))
    {
        if (IndexAllocationRunlist != NULL)
        {
            ExFreePoolWithTag(IndexAllocationRunlist, NTFSLX_PATHLOOKUP_INDEX_ALLOCATION_TAG);
        }

        NTFSLX_TRACE_LOOKUP("ntfslx: build INDEX_ALLOCATION runlist failed component='%wZ' index=%I64u status=0x%08lx\n",
                            ComponentName,
                            CurrentIndex,
                            Status);
        return Status;
    }

    NTFSLX_TRACE_LOOKUP("ntfslx: walking INDEX_ALLOCATION for '%wZ' CurrentIndex=%I64u RunCount=%lu\n",
                        ComponentName,
                        CurrentIndex,
                        IndexAllocationRunlistCount);

    Status = NtfslxEnumerateIndexAllocationTree(IndexRoot,
                                                IndexRootLength,
                                                DeviceExtension->StorageDevice,
                                                &DeviceExtension->VolumeInfo,
                                                IndexAllocationRunlist,
                                                IndexAllocationRunlistCount,
                                                NtfslxLookupDirectoryEnumerateCallback,
                                                &SearchContext);
    if (IndexAllocationRunlist != NULL)
    {
        ExFreePoolWithTag(IndexAllocationRunlist, NTFSLX_TAG);
    }
    if (!NT_SUCCESS(Status))
    {
        NTFSLX_TRACE_LOOKUP("ntfslx: INDEX_ALLOCATION walk failed component='%wZ' index=%I64u status=0x%08lx\n",
                            ComponentName,
                            CurrentIndex,
                            Status);
        return Status;
    }

    if (SearchContext.Found)
    {
        *ChildIndex = SearchContext.FoundIndex;
        return STATUS_SUCCESS;
    }

    NTFSLX_TRACE_LOOKUP("ntfslx: lookup exhausted component='%wZ' index=%I64u returning=0x%08lx\n",
                        ComponentName,
                        CurrentIndex,
                        IsFinalComponent ? STATUS_OBJECT_NAME_NOT_FOUND : STATUS_OBJECT_PATH_NOT_FOUND);
    return IsFinalComponent ? STATUS_OBJECT_NAME_NOT_FOUND
                            : STATUS_OBJECT_PATH_NOT_FOUND;
}

NTSTATUS
NtfslxLookupPathComponent(
    _In_ PVOID LookupContext,
    _In_opt_ PVOID ParentNode,
    _In_ PUNICODE_STRING ComponentName,
    _In_ BOOLEAN IsFinalComponent,
    _Outptr_ PVOID *ChildNode)
{
    PNTFSLX_PATHLOOKUP_PATH_CONTEXT Context;
    PNTFSLX_MFT_RECORD FileRecord;
    ULONGLONG ParentIndex;
    NTSTATUS Status;

    if (LookupContext == NULL || ComponentName == NULL || ChildNode == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Context = LookupContext;
    if (Context->DeviceExtension == NULL ||
        Context->DeviceExtension->StorageDevice == NULL ||
        Context->DeviceExtension->VolumeInfo.BytesPerFileRecord == 0)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    ParentIndex = (ParentNode != NULL) ? *(PULONGLONG)ParentNode : NTFSLX_FILE_ROOT;

    NTFSLX_TRACE_LOOKUP("ntfslx: lookup path component begin name='%wZ' parent=%I64u final=%u\n",
                        ComponentName,
                        ParentIndex,
                        IsFinalComponent);

    FileRecord = ExAllocatePoolWithTag(NonPagedPool,
                                       Context->DeviceExtension->VolumeInfo.BytesPerFileRecord,
                                       NTFSLX_TAG);
    if (FileRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(Context->DeviceExtension->StorageDevice,
                                 &Context->DeviceExtension->VolumeInfo,
                                 Context->DeviceExtension->MftRunlist,
                                 ParentIndex,
                                 FileRecord);
    if (!NT_SUCCESS(Status))
    {
        NTFSLX_TRACE_LOOKUP("ntfslx: read MFT record failed parent=%I64u name='%wZ' status=0x%08lx\n",
                            ParentIndex,
                            ComponentName,
                            Status);
        ExFreePoolWithTag(FileRecord, NTFSLX_TAG);
        return Status;
    }

    NTFSLX_TRACE_LOOKUP("ntfslx: read MFT record ok parent=%I64u flags=0x%04x bytes=%u\n",
                        ParentIndex,
                        FileRecord->Flags,
                        FileRecord->BytesInUse);

    Status = NtfslxLookupDirectoryComponent(Context->DeviceExtension,
                                            FileRecord,
                                            Context->DeviceExtension->VolumeInfo.BytesPerFileRecord,
                                            ParentIndex,
                                            ComponentName,
                                            IsFinalComponent,
                                            &Context->CurrentIndex);
    ExFreePoolWithTag(FileRecord, NTFSLX_TAG);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    *ChildNode = &Context->CurrentIndex;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxResolvePathToMftIndex(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_ PUNICODE_STRING FileName,
    _Out_ PULONGLONG MftIndex)
{
    NTFSLX_PATHLOOKUP_PATH_CONTEXT Context;
    NTSTATUS Status;

    if (DeviceExtension == NULL || FileName == NULL || MftIndex == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&Context, sizeof(Context));
    Context.DeviceExtension = DeviceExtension;
    Context.CurrentIndex = NTFSLX_FILE_ROOT;

    Status = NtfslxTraversePath(FileName,
                                &Context.CurrentIndex,
                                NtfslxLookupPathComponent,
                                &Context,
                                NULL,
                                NULL,
                                NULL);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    *MftIndex = Context.CurrentIndex;
    return STATUS_SUCCESS;
}
