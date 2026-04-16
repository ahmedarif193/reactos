/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     IRP dispatch for staged NTFS port
 */

#include "ntfslx.h"

#include <debug.h>

#ifndef NTFSLX_DISPATCH_TRACE
#define NTFSLX_DISPATCH_TRACE DbgPrint
#endif

/* NTFSLX_FILE_NAME_ATTRIBUTE is now in ntfslx_layout.h */

typedef struct _NTFSLX_PATH_LOOKUP_CONTEXT
{
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    ULONGLONG CurrentIndex;
} NTFSLX_PATH_LOOKUP_CONTEXT, *PNTFSLX_PATH_LOOKUP_CONTEXT;

typedef struct _NTFSLX_COMPONENT_SEARCH_CONTEXT
{
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    PCUNICODE_STRING ComponentName;
    ULONGLONG FoundIndex;
    BOOLEAN Found;
} NTFSLX_COMPONENT_SEARCH_CONTEXT, *PNTFSLX_COMPONENT_SEARCH_CONTEXT;

typedef struct _NTFSLX_QUERY_DIRECTORY_CONTEXT
{
    FILE_INFORMATION_CLASS FileInformationClass;
    PVOID Buffer;
    ULONG BufferLength;
    ULONG BytesWritten;
    ULONG StartIndex;
    PCUNICODE_STRING SearchPattern;
    PCUNICODE_STRING DirectoryPath;
    ULONG CurrentIndex;
    ULONG NextIndex;
    PULONG LastNextEntryOffset;
    ULONG LastEntryOffset;
    BOOLEAN AnyWritten;
    BOOLEAN Overflowed;
    BOOLEAN ReturnSingleEntry;
    BOOLEAN CaseSensitive;
} NTFSLX_QUERY_DIRECTORY_CONTEXT, *PNTFSLX_QUERY_DIRECTORY_CONTEXT;

typedef struct _NTFSLX_QUERY_DIRECTORY_COMMON_HEADER
{
    ULONG NextEntryOffset;
    ULONG FileIndex;
    LARGE_INTEGER CreationTime;
    LARGE_INTEGER LastAccessTime;
    LARGE_INTEGER LastWriteTime;
    LARGE_INTEGER ChangeTime;
    LARGE_INTEGER EndOfFile;
    LARGE_INTEGER AllocationSize;
    ULONG FileAttributes;
    ULONG FileNameLength;
} NTFSLX_QUERY_DIRECTORY_COMMON_HEADER, *PNTFSLX_QUERY_DIRECTORY_COMMON_HEADER;

static
NTSTATUS
NtfslxCompleteRequest(
    _Inout_ PIRP Irp,
    _In_ NTSTATUS Status,
    _In_ ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

static
PVOID
NtfslxGetUserBuffer(
    _In_ PIRP Irp,
    _In_ BOOLEAN PagingIo)
{
    if (Irp->MdlAddress != NULL)
    {
        return MmGetSystemAddressForMdlSafe(Irp->MdlAddress,
                                            PagingIo ? HighPagePriority : NormalPagePriority);
    }

    return Irp->UserBuffer;
}

static
PCUNICODE_STRING
NtfslxTraceUnicodeStringOrEmpty(
    _In_opt_ PCUNICODE_STRING String)
{
    static const UNICODE_STRING Empty = RTL_CONSTANT_STRING(L"");

    if (String != NULL &&
        String->Buffer != NULL &&
        String->Length != 0)
    {
        return String;
    }

    return &Empty;
}

static
PCUNICODE_STRING
NtfslxTraceFileContextPath(
    _In_opt_ PNTFSLX_FILE_CONTEXT FileContext)
{
    if (FileContext != NULL)
    {
        return NtfslxTraceUnicodeStringOrEmpty(&FileContext->FullPath);
    }

    return NtfslxTraceUnicodeStringOrEmpty(NULL);
}

static
NTSTATUS
NtfslxBuildSiblingFullPath(
    _In_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_reads_(LeafNameLength) PCWSTR LeafName,
    _In_ ULONG LeafNameLength,
    _Out_writes_(BufferCount) PWCHAR Buffer,
    _In_ ULONG BufferCount,
    _Out_ PUNICODE_STRING FullPath)
{
    USHORT ParentPrefixBytes;
    ULONG ParentPrefixChars;
    ULONG TotalChars;

    if (FileContext == NULL ||
        Buffer == NULL ||
        BufferCount == 0 ||
        FullPath == NULL ||
        (LeafNameLength != 0 && LeafName == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    ParentPrefixBytes = sizeof(WCHAR);
    if (FileContext->FullPath.Buffer != NULL &&
        FileContext->LeafNameLength <= FileContext->FullPath.Length &&
        FileContext->FullPath.Length >= FileContext->LeafNameLength + sizeof(WCHAR))
    {
        ParentPrefixBytes = FileContext->FullPath.Length - FileContext->LeafNameLength;
        if (ParentPrefixBytes > BufferCount * sizeof(WCHAR))
        {
            return STATUS_BUFFER_OVERFLOW;
        }

        RtlCopyMemory(Buffer, FileContext->FullPath.Buffer, ParentPrefixBytes);
    }
    else
    {
        Buffer[0] = L'\\';
    }

    ParentPrefixChars = ParentPrefixBytes / sizeof(WCHAR);
    TotalChars = ParentPrefixChars + LeafNameLength;
    if (TotalChars >= BufferCount)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    if (LeafNameLength != 0)
    {
        RtlCopyMemory(&Buffer[ParentPrefixChars],
                      LeafName,
                      LeafNameLength * sizeof(WCHAR));
    }
    Buffer[TotalChars] = UNICODE_NULL;

    FullPath->Buffer = Buffer;
    FullPath->Length = (USHORT)(TotalChars * sizeof(WCHAR));
    FullPath->MaximumLength = (USHORT)(BufferCount * sizeof(WCHAR));
    return STATUS_SUCCESS;
}

static
BOOLEAN
NtfslxPathPrefixEqualsInsensitive(
    _In_reads_(PrefixLength) PCWSTR Path,
    _In_ ULONG PathLength,
    _In_reads_(PrefixLength) PCWSTR Prefix,
    _In_ ULONG PrefixLength)
{
    ULONG Index;

    if (Path == NULL || Prefix == NULL || PathLength < PrefixLength)
    {
        return FALSE;
    }

    for (Index = 0; Index < PrefixLength; ++Index)
    {
        if (RtlUpcaseUnicodeChar(Path[Index]) !=
            RtlUpcaseUnicodeChar(Prefix[Index]))
        {
            return FALSE;
        }
    }

    return TRUE;
}

static
NTSTATUS
NtfslxBuildChildFullPath(
    _In_ PCUNICODE_STRING DirectoryPath,
    _In_reads_(RelativePathLength) PCWSTR RelativePath,
    _In_ ULONG RelativePathLength,
    _Out_writes_(BufferCount) PWCHAR Buffer,
    _In_ ULONG BufferCount,
    _Out_ PUNICODE_STRING FullPath)
{
    ULONG DirectoryChars;
    ULONG PrefixChars;
    ULONG TotalChars;

    if (DirectoryPath == NULL ||
        DirectoryPath->Buffer == NULL ||
        DirectoryPath->Length < sizeof(WCHAR) ||
        DirectoryPath->Buffer[0] != L'\\' ||
        RelativePath == NULL ||
        RelativePathLength == 0 ||
        RelativePath[0] == L'\\' ||
        Buffer == NULL ||
        BufferCount == 0 ||
        FullPath == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DirectoryChars = DirectoryPath->Length / sizeof(WCHAR);
    PrefixChars = DirectoryChars;
    TotalChars = DirectoryChars + RelativePathLength;
    if (!(DirectoryChars == 1 && DirectoryPath->Buffer[0] == L'\\'))
    {
        TotalChars += 1;
    }

    if (TotalChars >= BufferCount)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    RtlCopyMemory(Buffer, DirectoryPath->Buffer, DirectoryPath->Length);
    if (!(DirectoryChars == 1 && DirectoryPath->Buffer[0] == L'\\'))
    {
        Buffer[PrefixChars++] = L'\\';
    }

    RtlCopyMemory(&Buffer[PrefixChars], RelativePath, RelativePathLength * sizeof(WCHAR));
    Buffer[TotalChars] = UNICODE_NULL;

    FullPath->Buffer = Buffer;
    FullPath->Length = (USHORT)(TotalChars * sizeof(WCHAR));
    FullPath->MaximumLength = (USHORT)(BufferCount * sizeof(WCHAR));
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxSplitFullPath(
    _In_ PCUNICODE_STRING FullPath,
    _Out_ PUNICODE_STRING ParentPath,
    _Out_ PUNICODE_STRING FinalComponent)
{
    USHORT LastBackslash;
    USHORT Index;

    if (FullPath == NULL ||
        FullPath->Buffer == NULL ||
        FullPath->Length < 2 * sizeof(WCHAR) ||
        FullPath->Buffer[0] != L'\\' ||
        ParentPath == NULL ||
        FinalComponent == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    LastBackslash = 0;
    for (Index = 0; Index < FullPath->Length / sizeof(WCHAR); ++Index)
    {
        if (FullPath->Buffer[Index] == L'\\')
        {
            LastBackslash = Index;
        }
    }

    if (LastBackslash == (FullPath->Length / sizeof(WCHAR)) - 1)
    {
        return STATUS_OBJECT_NAME_INVALID;
    }

    if (LastBackslash == 0)
    {
        ParentPath->Buffer = FullPath->Buffer;
        ParentPath->Length = sizeof(WCHAR);
        ParentPath->MaximumLength = sizeof(WCHAR);

        FinalComponent->Buffer = FullPath->Buffer + 1;
        FinalComponent->Length = FullPath->Length - sizeof(WCHAR);
        FinalComponent->MaximumLength = FinalComponent->Length;
    }
    else
    {
        ParentPath->Buffer = FullPath->Buffer;
        ParentPath->Length = LastBackslash * sizeof(WCHAR);
        ParentPath->MaximumLength = ParentPath->Length;

        FinalComponent->Buffer = FullPath->Buffer + LastBackslash + 1;
        FinalComponent->Length = FullPath->Length - (LastBackslash + 1) * sizeof(WCHAR);
        FinalComponent->MaximumLength = FinalComponent->Length;
    }

    if (FinalComponent->Length == 0)
    {
        return STATUS_OBJECT_NAME_INVALID;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxNormalizeRenameDestinationPath(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ PFILE_RENAME_INFORMATION RenameInfo,
    _In_ KPROCESSOR_MODE RequestorMode,
    _Out_writes_(BufferCount) PWCHAR Buffer,
    _In_ ULONG BufferCount,
    _Out_ PUNICODE_STRING FullPath)
{
    NTSTATUS Status;
    UNICODE_STRING DosName;
    PFILE_OBJECT RootFileObject;
    PNTFSLX_FILE_CONTEXT RootContext;
    PCWSTR Name;
    ULONG NameLength;

    if (DeviceExtension == NULL ||
        FileContext == NULL ||
        RenameInfo == NULL ||
        Buffer == NULL ||
        BufferCount == 0 ||
        FullPath == NULL ||
        RenameInfo->FileNameLength == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(&DosName, sizeof(DosName));
    RootFileObject = NULL;
    RootContext = NULL;
    Name = RenameInfo->FileName;
    NameLength = RenameInfo->FileNameLength / sizeof(WCHAR);

    if (RenameInfo->RootDirectory != NULL)
    {
        if (Name[0] == L'\\')
        {
            return STATUS_OBJECT_NAME_INVALID;
        }

        Status = ObReferenceObjectByHandle(RenameInfo->RootDirectory,
                                           0,
                                           *IoFileObjectType,
                                           RequestorMode,
                                           (PVOID *)&RootFileObject,
                                           NULL);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }

        Status = NtfslxValidateOpenFileObject(RootFileObject, &RootContext);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }

        if (!RootContext->IsDirectory)
        {
            Status = STATUS_NOT_A_DIRECTORY;
            goto Cleanup;
        }

        if (RootContext->DeviceExtension != DeviceExtension)
        {
            Status = STATUS_NOT_SAME_DEVICE;
            goto Cleanup;
        }

        Status = NtfslxBuildChildFullPath(&RootContext->FullPath,
                                          Name,
                                          NameLength,
                                          Buffer,
                                          BufferCount,
                                          FullPath);
        goto Cleanup;
    }

    if (NameLength >= 4 &&
        Name[0] == L'\\' &&
        Name[1] == L'?' &&
        Name[2] == L'?' &&
        Name[3] == L'\\')
    {
        Name += 4;
        NameLength -= 4;
    }

    if (NameLength >= 2 &&
        (((Name[0] >= L'A') && (Name[0] <= L'Z')) ||
         ((Name[0] >= L'a') && (Name[0] <= L'z'))) &&
        Name[1] == L':')
    {
        ULONG DosChars;

        Status = IoVolumeDeviceToDosName(DeviceExtension->StorageDevice,
                                         &DosName);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }

        DosChars = DosName.Length / sizeof(WCHAR);
        if (DosChars == 0 ||
            !NtfslxPathPrefixEqualsInsensitive(Name,
                                               NameLength,
                                               DosName.Buffer,
                                               DosChars))
        {
            Status = STATUS_NOT_SAME_DEVICE;
            goto Cleanup;
        }

        if (NameLength == DosChars || Name[DosChars] != L'\\')
        {
            Status = STATUS_OBJECT_NAME_INVALID;
            goto Cleanup;
        }

        Name += DosChars;
        NameLength -= DosChars;
    }

    if (NameLength == 0)
    {
        Status = STATUS_OBJECT_NAME_INVALID;
        goto Cleanup;
    }

    if (Name[0] == L'\\')
    {
        if (NameLength >= BufferCount)
        {
            Status = STATUS_BUFFER_OVERFLOW;
            goto Cleanup;
        }

        RtlCopyMemory(Buffer, Name, NameLength * sizeof(WCHAR));
        Buffer[NameLength] = UNICODE_NULL;
        FullPath->Buffer = Buffer;
        FullPath->Length = (USHORT)(NameLength * sizeof(WCHAR));
        FullPath->MaximumLength = (USHORT)(BufferCount * sizeof(WCHAR));
        Status = STATUS_SUCCESS;
        goto Cleanup;
    }

    Status = NtfslxBuildSiblingFullPath(FileContext,
                                        Name,
                                        NameLength,
                                        Buffer,
                                        BufferCount,
                                        FullPath);

Cleanup:
    if (RootFileObject != NULL)
    {
        ObDereferenceObject(RootFileObject);
    }

    if (DosName.Buffer != NULL)
    {
        ExFreePool(DosName.Buffer);
    }

    return Status;
}

static
PCSTR
NtfslxDirectoryInfoClassName(
    _In_ FILE_INFORMATION_CLASS FileInformationClass)
{
    switch (FileInformationClass)
    {
        case FileDirectoryInformation:
            return "FileDirectoryInformation";
        case FileFullDirectoryInformation:
            return "FileFullDirectoryInformation";
        case FileIdFullDirectoryInformation:
            return "FileIdFullDirectoryInformation";
        case FileBothDirectoryInformation:
            return "FileBothDirectoryInformation";
        case FileIdBothDirectoryInformation:
            return "FileIdBothDirectoryInformation";
        case FileNamesInformation:
            return "FileNamesInformation";
        default:
            return "UnknownDirectoryInformation";
    }
}

static
PCSTR
NtfslxFileInfoClassName(
    _In_ FILE_INFORMATION_CLASS FileInformationClass)
{
    switch (FileInformationClass)
    {
        case FileBasicInformation:
            return "FileBasicInformation";
        case FileStandardInformation:
            return "FileStandardInformation";
        case FileInternalInformation:
            return "FileInternalInformation";
        case FileEaInformation:
            return "FileEaInformation";
        case FileAccessInformation:
            return "FileAccessInformation";
        case FileNameInformation:
            return "FileNameInformation";
        case FilePositionInformation:
            return "FilePositionInformation";
        case FileModeInformation:
            return "FileModeInformation";
        case FileAlignmentInformation:
            return "FileAlignmentInformation";
        case FileAllInformation:
            return "FileAllInformation";
        case FileNetworkOpenInformation:
            return "FileNetworkOpenInformation";
        case FileAttributeTagInformation:
            return "FileAttributeTagInformation";
        default:
            return "UnknownFileInformation";
    }
}

static
VOID
NtfslxFreeCcb(
    _Inout_opt_ PNTFSLX_CCB Ccb)
{
    if (Ccb == NULL)
    {
        return;
    }

    if (Ccb->SearchPatternBuffer != NULL)
    {
        ExFreePoolWithTag(Ccb->SearchPatternBuffer, NTFSLX_TAG);
    }

    ExFreePoolWithTag(Ccb, NTFSLX_TAG);
}

static
NTSTATUS
NtfslxAllocateCcb(
    _Outptr_ PNTFSLX_CCB *Ccb)
{
    PNTFSLX_CCB LocalCcb;

    *Ccb = NULL;

    LocalCcb = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(*LocalCcb),
                                     NTFSLX_TAG);
    if (LocalCcb == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(LocalCcb, sizeof(*LocalCcb));
    LocalCcb->Signature = NTFSLX_CCB_SIGNATURE;
    *Ccb = LocalCcb;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxPrepareSearchPattern(
    _Inout_ PNTFSLX_CCB Ccb,
    _In_opt_ PUNICODE_STRING SearchPattern)
{
    PWCHAR Buffer;
    USHORT MaximumLength;

    if (Ccb == NULL || Ccb->Signature != NTFSLX_CCB_SIGNATURE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Ccb->SearchInitialized)
    {
        return STATUS_SUCCESS;
    }

    if (SearchPattern != NULL &&
        SearchPattern->Length != 0 &&
        SearchPattern->Buffer != NULL)
    {
        ULONG I;

        MaximumLength = SearchPattern->Length + sizeof(WCHAR);
        Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                       MaximumLength,
                                       NTFSLX_TAG);
        if (Buffer == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /*
         * FsRtlIsNameInExpression requires the EXPRESSION (pattern) to be
         * upcased when called with IgnoreCase = TRUE. Without this, every
         * directory enumeration with a wildcard returns zero matches even
         * for trivial patterns like "*.bin".
         */
        for (I = 0; I < SearchPattern->Length / sizeof(WCHAR); I++)
        {
            Buffer[I] = RtlUpcaseUnicodeChar(SearchPattern->Buffer[I]);
        }
        Buffer[SearchPattern->Length / sizeof(WCHAR)] = UNICODE_NULL;

        Ccb->SearchPattern.Length = SearchPattern->Length;
        Ccb->SearchPattern.MaximumLength = MaximumLength;
    }
    else
    {
        MaximumLength = 2 * sizeof(WCHAR);
        Buffer = ExAllocatePoolWithTag(NonPagedPool,
                                       MaximumLength,
                                       NTFSLX_TAG);
        if (Buffer == NULL)
        {
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Buffer[0] = L'*';
        Buffer[1] = UNICODE_NULL;

        Ccb->SearchPattern.Length = sizeof(WCHAR);
        Ccb->SearchPattern.MaximumLength = MaximumLength;
    }

    Ccb->SearchPattern.Buffer = Buffer;
    Ccb->SearchPatternBuffer = Buffer;
    Ccb->SearchInitialized = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxLookupParentIndexFromRecord(
    _In_ PNTFSLX_MFT_RECORD FileRecord,
    _Out_ PULONGLONG ParentIndex)
{
    PNTFSLX_ATTR_RECORD Attribute;
    PNTFSLX_FILE_NAME_ATTRIBUTE FileNameAttribute;
    NTSTATUS Status;

    if (FileRecord == NULL || ParentIndex == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (FileRecord->MftRecordNumber == NTFSLX_FILE_ROOT)
    {
        *ParentIndex = NTFSLX_FILE_ROOT;
        return STATUS_SUCCESS;
    }

    Status = NtfslxFindAttribute(FileRecord,
                                 NTFSLX_ATTRIBUTE_FILE_NAME,
                                 NULL,
                                 0,
                                 &Attribute);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Attribute->NonResident != 0 ||
        Attribute->Data.Resident.ValueLength < sizeof(*FileNameAttribute) ||
        Attribute->Data.Resident.ValueOffset + sizeof(*FileNameAttribute) > Attribute->Length)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    FileNameAttribute = (PNTFSLX_FILE_NAME_ATTRIBUTE)
        ((PUCHAR)Attribute + Attribute->Data.Resident.ValueOffset);
    *ParentIndex = MREF(FileNameAttribute->ParentDirectory);
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxLookupPathEntryCallback(
    _In_ const NTFSLX_DIR_ENTRY *Entry,
    _In_opt_ PVOID Context)
{
    PNTFSLX_COMPONENT_SEARCH_CONTEXT SearchContext;
    UNICODE_STRING EntryName;

    SearchContext = Context;
    EntryName.Buffer = (PWCHAR)Entry->FileName;
    EntryName.Length = Entry->FileNameLength * sizeof(WCHAR);
    EntryName.MaximumLength = EntryName.Length;

    NTFSLX_DISPATCH_TRACE("ntfslx: dispatch lookup candidate ref=%I64u type=%u attrs=0x%08lx name='%wZ' target='%wZ'\n",
                          MREF(Entry->FileReference),
                          Entry->FileNameType,
                          Entry->FileAttributes,
                          &EntryName,
                          SearchContext->ComponentName);

    if (!FsRtlAreNamesEqual(SearchContext->ComponentName,
                            &EntryName,
                            TRUE,
                            (PCWCH)SearchContext->DeviceExtension->UpcaseTable))
    {
        return STATUS_SUCCESS;
    }

    NTFSLX_DISPATCH_TRACE("ntfslx: dispatch lookup match ref=%I64u name='%wZ'\n",
                          MREF(Entry->FileReference),
                          &EntryName);

    SearchContext->Found = TRUE;
    SearchContext->FoundIndex = MREF(Entry->FileReference);
    return STATUS_NO_MORE_FILES;
}

static
NTSTATUS
NtfslxGetDirectoryIndexRoot(
    _In_ PNTFSLX_MFT_RECORD FileRecord,
    _Outptr_result_bytebuffer_(*IndexRootLength) PVOID *IndexRootBuffer,
    _Out_ PULONG IndexRootLength)
{
    static const WCHAR IndexName[] = L"$I30";
    PNTFSLX_ATTR_RECORD IndexRootAttribute;
    NTSTATUS Status;

    if (IndexRootBuffer == NULL || IndexRootLength == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *IndexRootBuffer = NULL;
    *IndexRootLength = 0;

    Status = NtfslxFindAttribute(FileRecord,
                                 NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                 IndexName,
                                 RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexRootAttribute);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ntfslx: index-root lookup failed for directory MFT %I64u status=0x%08lx\n",
                FileRecord->MftRecordNumber,
                Status);
        return Status;
    }

    if (IndexRootAttribute->NonResident != 0 ||
        IndexRootAttribute->Data.Resident.ValueLength == 0 ||
        IndexRootAttribute->Data.Resident.ValueOffset >= IndexRootAttribute->Length ||
        IndexRootAttribute->Data.Resident.ValueOffset + IndexRootAttribute->Data.Resident.ValueLength >
            IndexRootAttribute->Length)
    {
        DPRINT1("ntfslx: invalid $INDEX_ROOT in directory MFT %I64u AttrLen=%lu NonResident=%u ValueOffset=%u ValueLength=%lu\n",
                FileRecord->MftRecordNumber,
                IndexRootAttribute->Length,
                IndexRootAttribute->NonResident,
                IndexRootAttribute->Data.Resident.ValueOffset,
                IndexRootAttribute->Data.Resident.ValueLength);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    *IndexRootBuffer = (PUCHAR)IndexRootAttribute + IndexRootAttribute->Data.Resident.ValueOffset;
    *IndexRootLength = IndexRootAttribute->Data.Resident.ValueLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxEnumerateLookupDirectory(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_ PNTFSLX_MFT_RECORD FileRecord,
    _In_ PNTFSLX_DIR_ENUM_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    static const WCHAR IndexName[] = L"$I30";
    PNTFSLX_ATTR_RECORD IndexAllocationAttribute;
    PNTFSLX_RUNLIST_ELEMENT IndexAllocationRunlist;
    PVOID IndexRootBuffer;
    ULONG IndexAllocationRunlistCount;
    ULONG IndexRootLength;
    NTSTATUS Status;

    IndexAllocationRunlist = NULL;
    IndexAllocationRunlistCount = 0;

    Status = NtfslxGetDirectoryIndexRoot(FileRecord, &IndexRootBuffer, &IndexRootLength);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ntfslx: failed to get directory index-root for MFT %I64u status=0x%08lx\n",
                FileRecord->MftRecordNumber,
                Status);
        return Status;
    }

    Status = NtfslxFindAttribute(FileRecord,
                                 NTFSLX_ATTRIBUTE_INDEX_ALLOCATION,
                                 IndexName,
                                 RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexAllocationAttribute);
    if (NT_SUCCESS(Status))
    {
        if (IndexAllocationAttribute->NonResident == 0)
        {
            DPRINT1("ntfslx: invalid resident $INDEX_ALLOCATION in directory MFT %I64u AttrLen=%lu\n",
                    FileRecord->MftRecordNumber,
                    IndexAllocationAttribute->Length);
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Status = NtfslxBuildIndexAllocationRunlist(IndexAllocationAttribute,
                                                   &IndexAllocationRunlist,
                                                   &IndexAllocationRunlistCount);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("ntfslx: failed to build $INDEX_ALLOCATION runlist for directory MFT %I64u status=0x%08lx\n",
                    FileRecord->MftRecordNumber,
                    Status);
            return Status;
        }
    }
    else if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        DPRINT1("ntfslx: $INDEX_ALLOCATION lookup failed for directory MFT %I64u status=0x%08lx\n",
                FileRecord->MftRecordNumber,
                Status);
        return Status;
    }

    Status = NtfslxEnumerateIndexAllocationTree(IndexRootBuffer,
                                                IndexRootLength,
                                                DeviceExtension->StorageDevice,
                                                &DeviceExtension->VolumeInfo,
                                                IndexAllocationRunlist,
                                                IndexAllocationRunlistCount,
                                                Callback,
                                                Context);
    if (IndexAllocationRunlist != NULL)
    {
        ExFreePool(IndexAllocationRunlist);
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ntfslx: directory enumeration failed for MFT %I64u status=0x%08lx RootLen=%lu RunCount=%lu\n",
                FileRecord->MftRecordNumber,
                Status,
                IndexRootLength,
                IndexAllocationRunlistCount);
    }

    return Status;
}

static
NTSTATUS
NTAPI
NtfslxLookupPathComponent(
    _In_ PVOID LookupContext,
    _In_opt_ PVOID ParentNode,
    _In_ PUNICODE_STRING ComponentName,
    _In_ BOOLEAN IsFinalComponent,
    _Outptr_ PVOID *ChildNode)
{
    PNTFSLX_PATH_LOOKUP_CONTEXT Context;
    ULONGLONG ParentIndex;
    PNTFSLX_MFT_RECORD FileRecord;
    NTFSLX_COMPONENT_SEARCH_CONTEXT SearchContext;
    NTSTATUS Status;

    Context = LookupContext;
    ParentIndex = (ParentNode != NULL) ? *(PULONGLONG)ParentNode : NTFSLX_FILE_ROOT;

    if (NtfslxPathIsDotComponent(ComponentName))
    {
        Context->CurrentIndex = ParentIndex;
        *ChildNode = &Context->CurrentIndex;
        return STATUS_SUCCESS;
    }

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
        DPRINT1("ntfslx: failed to read parent directory MFT %I64u for component '%wZ' status=0x%08lx\n",
                ParentIndex,
                ComponentName,
                Status);
        ExFreePoolWithTag(FileRecord, NTFSLX_TAG);
        return Status;
    }

    if (NtfslxPathIsDotDotComponent(ComponentName))
    {
        Status = NtfslxLookupParentIndexFromRecord(FileRecord, &Context->CurrentIndex);
        ExFreePoolWithTag(FileRecord, NTFSLX_TAG);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        *ChildNode = &Context->CurrentIndex;
        return STATUS_SUCCESS;
    }

    if ((FileRecord->Flags & NTFSLX_MFT_RECORD_IS_DIRECTORY) == 0)
    {
        ExFreePoolWithTag(FileRecord, NTFSLX_TAG);
        return IsFinalComponent ? STATUS_OBJECT_NAME_NOT_FOUND
                                : STATUS_OBJECT_PATH_NOT_FOUND;
    }

    RtlZeroMemory(&SearchContext, sizeof(SearchContext));
    SearchContext.DeviceExtension = Context->DeviceExtension;
    SearchContext.ComponentName = ComponentName;

    Status = NtfslxEnumerateLookupDirectory(Context->DeviceExtension,
                                            FileRecord,
                                            NtfslxLookupPathEntryCallback,
                                            &SearchContext);
    ExFreePoolWithTag(FileRecord, NTFSLX_TAG);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ntfslx: component lookup failed in directory MFT %I64u for '%wZ' status=0x%08lx\n",
                ParentIndex,
                ComponentName,
                Status);
        return Status;
    }

    if (ParentIndex == NTFSLX_FILE_ROOT)
    {
        DPRINT1("ntfslx: root lookup completed for '%wZ' found=%u index=%I64u\n",
                ComponentName,
                SearchContext.Found,
                SearchContext.FoundIndex);
    }

    if (!SearchContext.Found)
    {
        NTFSLX_DISPATCH_TRACE("ntfslx: dispatch lookup miss parent=%I64u component='%wZ' final=%u\n",
                              ParentIndex,
                              ComponentName,
                              IsFinalComponent);

        if (ParentIndex == NTFSLX_FILE_ROOT)
        {
            DPRINT1("ntfslx: root component '%wZ' is absent from ntfs.img root enumeration, likely image content mismatch rather than case-only lookup failure\n",
                    ComponentName);
        }

        return IsFinalComponent ? STATUS_OBJECT_NAME_NOT_FOUND
                                : STATUS_OBJECT_PATH_NOT_FOUND;
    }

    Context->CurrentIndex = SearchContext.FoundIndex;
    *ChildNode = &Context->CurrentIndex;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxResolvePathToMftIndex(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_ PUNICODE_STRING FileName,
    _Out_ PULONGLONG MftIndex)
{
    NTFSLX_PATH_LOOKUP_CONTEXT Context;
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

static
BOOLEAN
NtfslxAttributeTypeFromOpenSuffix(
    _In_ PCUNICODE_STRING AttributeName,
    _Out_ PULONG AttributeType)
{
    static const UNICODE_STRING DataName = RTL_CONSTANT_STRING(L"$DATA");
    static const UNICODE_STRING IndexAllocationName = RTL_CONSTANT_STRING(L"$INDEX_ALLOCATION");
    static const UNICODE_STRING IndexRootName = RTL_CONSTANT_STRING(L"$INDEX_ROOT");
    static const UNICODE_STRING AttributeListName = RTL_CONSTANT_STRING(L"$ATTRIBUTE_LIST");
    static const UNICODE_STRING FileNameName = RTL_CONSTANT_STRING(L"$FILE_NAME");
    static const UNICODE_STRING VolumeNameName = RTL_CONSTANT_STRING(L"$VOLUME_NAME");
    static const UNICODE_STRING VolumeInformationName = RTL_CONSTANT_STRING(L"$VOLUME_INFORMATION");

    if (RtlEqualUnicodeString(AttributeName, &DataName, TRUE))
    {
        *AttributeType = NTFSLX_ATTRIBUTE_DATA;
        return TRUE;
    }

    if (RtlEqualUnicodeString(AttributeName, &IndexAllocationName, TRUE))
    {
        *AttributeType = NTFSLX_ATTRIBUTE_INDEX_ALLOCATION;
        return TRUE;
    }

    if (RtlEqualUnicodeString(AttributeName, &IndexRootName, TRUE))
    {
        *AttributeType = NTFSLX_ATTRIBUTE_INDEX_ROOT;
        return TRUE;
    }

    if (RtlEqualUnicodeString(AttributeName, &AttributeListName, TRUE))
    {
        *AttributeType = NTFSLX_ATTRIBUTE_ATTRIBUTE_LIST;
        return TRUE;
    }

    if (RtlEqualUnicodeString(AttributeName, &FileNameName, TRUE))
    {
        *AttributeType = NTFSLX_ATTRIBUTE_FILE_NAME;
        return TRUE;
    }

    if (RtlEqualUnicodeString(AttributeName, &VolumeNameName, TRUE))
    {
        *AttributeType = NTFSLX_ATTRIBUTE_VOLUME_NAME;
        return TRUE;
    }

    if (RtlEqualUnicodeString(AttributeName, &VolumeInformationName, TRUE))
    {
        *AttributeType = NTFSLX_ATTRIBUTE_VOLUME_INFORMATION;
        return TRUE;
    }

    return FALSE;
}

static
NTSTATUS
NtfslxParseCreateTarget(
    _In_ PUNICODE_STRING FileName,
    _Out_ PUNICODE_STRING BasePath,
    _Out_opt_ PCWSTR *StreamName,
    _Out_opt_ PULONG StreamNameLength,
    _Out_opt_ PULONG AttributeType,
    _Out_opt_ PBOOLEAN HadTrailingBackslash)
{
    UNICODE_STRING AttributeName;
    USHORT ComponentStart;
    USHORT FirstColon;
    USHORT SecondColon;
    USHORT ParseLength;
    USHORT Index;

    if (FileName == NULL || BasePath == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *BasePath = *FileName;
    if (StreamName != NULL)
    {
        *StreamName = NULL;
    }

    if (StreamNameLength != NULL)
    {
        *StreamNameLength = 0;
    }

    if (AttributeType != NULL)
    {
        *AttributeType = NTFSLX_ATTRIBUTE_DATA;
    }

    if (HadTrailingBackslash != NULL)
    {
        *HadTrailingBackslash = FALSE;
    }

    if (FileName->Length == 0 || FileName->Buffer == NULL)
    {
        return STATUS_SUCCESS;
    }

    /*
     * Reject malformed paths up front. Empty components are still invalid,
     * but a single trailing backslash on a non-root path is normalized away
     * so Explorer-style directory opens like "\subdir\" can reach the
     * directory resolution path instead of failing in the parser.
     */
    {
        USHORT Chars = FileName->Length / sizeof(WCHAR);
        BOOLEAN PrevSlash = FALSE;
        for (Index = 0; Index < Chars; ++Index)
        {
            WCHAR Ch = FileName->Buffer[Index];
            if (Ch == L'\\')
            {
                if (PrevSlash)
                {
                    return STATUS_OBJECT_NAME_INVALID;
                }
                PrevSlash = TRUE;
            }
            else
            {
                PrevSlash = FALSE;
            }
        }
    }

    ParseLength = FileName->Length;
    if (ParseLength > sizeof(WCHAR) &&
        FileName->Buffer[(ParseLength / sizeof(WCHAR)) - 1] == L'\\')
    {
        ParseLength -= sizeof(WCHAR);
        if (HadTrailingBackslash != NULL)
        {
            *HadTrailingBackslash = TRUE;
        }
    }

    BasePath->Length = ParseLength;
    BasePath->MaximumLength = ParseLength;

    ComponentStart = 0;
    for (Index = 0; Index < ParseLength / sizeof(WCHAR); ++Index)
    {
        if (FileName->Buffer[Index] == L'\\')
        {
            ComponentStart = Index + 1;
        }
    }

    FirstColon = 0xFFFF;
    for (Index = ComponentStart; Index < ParseLength / sizeof(WCHAR); ++Index)
    {
        if (FileName->Buffer[Index] == L':')
        {
            FirstColon = Index;
            break;
        }
    }

    if (FirstColon == 0xFFFF)
    {
        return STATUS_SUCCESS;
    }

    if (FirstColon == ComponentStart)
    {
        return STATUS_OBJECT_NAME_INVALID;
    }

    BasePath->Length = FirstColon * sizeof(WCHAR);
    BasePath->MaximumLength = BasePath->Length;

    SecondColon = 0xFFFF;
    for (Index = FirstColon + 1; Index < ParseLength / sizeof(WCHAR); ++Index)
    {
        if (FileName->Buffer[Index] == L':')
        {
            SecondColon = Index;
            break;
        }
    }

    if (SecondColon == 0xFFFF)
    {
        if (StreamName != NULL)
        {
            *StreamName = FileName->Buffer + FirstColon + 1;
        }

        if (StreamNameLength != NULL)
        {
            *StreamNameLength = (ParseLength / sizeof(WCHAR)) - FirstColon - 1;
        }

        return ((ParseLength / sizeof(WCHAR)) == FirstColon + 1)
            ? STATUS_OBJECT_NAME_INVALID
            : STATUS_SUCCESS;
    }

    AttributeName.Buffer = (PWCHAR)(FileName->Buffer + SecondColon + 1);
    AttributeName.Length = ParseLength - ((SecondColon + 1) * sizeof(WCHAR));
    AttributeName.MaximumLength = AttributeName.Length;
    if (AttributeName.Length == 0)
    {
        return STATUS_OBJECT_NAME_INVALID;
    }

    if (StreamName != NULL)
    {
        *StreamName = FileName->Buffer + FirstColon + 1;
    }

    if (StreamNameLength != NULL)
    {
        *StreamNameLength = SecondColon - FirstColon - 1;
    }

    if (AttributeType == NULL)
    {
        return STATUS_SUCCESS;
    }

    if (!NtfslxAttributeTypeFromOpenSuffix(&AttributeName, AttributeType))
    {
        return STATUS_OBJECT_NAME_INVALID;
    }

    return STATUS_SUCCESS;
}


static
NTSTATUS
NtfslxCreate(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    PNTFSLX_FILE_CONTEXT FileContext;
    PNTFSLX_CCB Ccb;
    UNICODE_STRING BasePath;
    PCWSTR StreamName;
    ULONG StreamNameLength;
    ULONG AttributeType;
    ULONGLONG MftIndex;
    ULONGLONG ExistingMftIndex;
    BOOLEAN HadTrailingBackslash;
    BOOLEAN NotifyCreated;
    BOOLEAN OpenTargetDirectory;
    ULONG_PTR CreateResult;
    UNICODE_STRING TargetParentPath;
    UNICODE_STRING TargetLeafName;
    NTSTATUS Status;

    HadTrailingBackslash = FALSE;
    NotifyCreated = FALSE;
    OpenTargetDirectory = FALSE;
    CreateResult = FILE_OPENED;
    RtlZeroMemory(&TargetParentPath, sizeof(TargetParentPath));
    RtlZeroMemory(&TargetLeafName, sizeof(TargetLeafName));

    Stack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    OpenTargetDirectory = BooleanFlagOn(Stack->Flags, SL_OPEN_TARGET_DIRECTORY);

    if (Stack->FileObject == NULL)
    {
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }

    /*
     * Allow a bare open on the control device so clients can send IOCTLs
     * (the self-test IOCTL in particular).
     */
    if (NtfslxIsControlDevice(DeviceExtension))
    {
        if (Stack->FileObject->FileName.Length == 0)
        {
            DbgPrint("ntfslx: Create control-device open fileObj=%p\n", Stack->FileObject);
            return NtfslxCompleteRequest(Irp, STATUS_SUCCESS, FILE_OPENED);
        }
        DbgPrint("ntfslx: Create control-device named open rejected name='%wZ'\n",
                 &Stack->FileObject->FileName);
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }

    if (!NtfslxIsVolumeDevice(DeviceExtension))
    {
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }

    Stack->FileObject->Vpb = DeviceExtension->Vpb;

    if (Stack->FileObject->FileName.Length == 0)
    {
        DbgPrint("ntfslx: Create volume-open fileObj=%p\n", Stack->FileObject);
        return NtfslxCompleteRequest(Irp, STATUS_SUCCESS, FILE_OPENED);
    }

    {
        ULONG Disp = (Stack->Parameters.Create.Options >> 24) & 0xFF;
        ACCESS_MASK Desired = Stack->Parameters.Create.SecurityContext
            ? Stack->Parameters.Create.SecurityContext->DesiredAccess
            : 0;
        DbgPrint("ntfslx: Create fileObj=%p name='%wZ' disposition=%lu desiredAccess=0x%08lx options=0x%08lx\n",
                 Stack->FileObject, &Stack->FileObject->FileName,
                 Disp, Desired, Stack->Parameters.Create.Options);
    }

    Status = NtfslxParseCreateTarget(&Stack->FileObject->FileName,
                                     &BasePath,
                                     &StreamName,
                                     &StreamNameLength,
                                     &AttributeType,
                                     &HadTrailingBackslash);
    if (!NT_SUCCESS(Status))
    {
        return NtfslxCompleteRequest(Irp, Status, 0);
    }

    if (HadTrailingBackslash)
    {
        DbgPrint("ntfslx: Create normalized trailing slash name='%wZ' base='%wZ'\n",
                 &Stack->FileObject->FileName,
                 &BasePath);
    }

    if (OpenTargetDirectory)
    {
        if (BasePath.Length <= sizeof(WCHAR) &&
            BasePath.Buffer[0] == L'\\')
        {
            return NtfslxCompleteRequest(Irp, STATUS_INVALID_PARAMETER, 0);
        }

        Status = NtfslxSplitFullPath(&BasePath,
                                     &TargetParentPath,
                                     &TargetLeafName);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("ntfslx: Create target-dir split failed target='%wZ' status=0x%08lx\n",
                     &BasePath,
                     Status);
            return NtfslxCompleteRequest(Irp, Status, 0);
        }

        Status = NtfslxResolvePathToMftIndex(DeviceExtension,
                                             &TargetParentPath,
                                             &MftIndex);
        if (!NT_SUCCESS(Status))
        {
            DbgPrint("ntfslx: Create target-dir resolve parent failed parent='%wZ' status=0x%08lx\n",
                     &TargetParentPath,
                     Status);
            return NtfslxCompleteRequest(Irp, Status, 0);
        }

        Status = NtfslxResolvePathToMftIndex(DeviceExtension,
                                             &BasePath,
                                             &ExistingMftIndex);
        if (NT_SUCCESS(Status))
        {
            CreateResult = FILE_EXISTS;
        }
        else if (Status == STATUS_OBJECT_NAME_NOT_FOUND ||
                 Status == STATUS_OBJECT_PATH_NOT_FOUND)
        {
            CreateResult = FILE_DOES_NOT_EXIST;
            Status = STATUS_SUCCESS;
        }
        else
        {
            DbgPrint("ntfslx: Create target-dir probe failed target='%wZ' status=0x%08lx\n",
                     &BasePath,
                     Status);
            return NtfslxCompleteRequest(Irp, Status, 0);
        }

        DbgPrint("ntfslx: Create target-dir target='%wZ' parent='%wZ' leaf='%wZ' result=%Iu\n",
                 &BasePath,
                 &TargetParentPath,
                 &TargetLeafName,
                 CreateResult);

        BasePath = TargetParentPath;
        goto OpenByIndex;
    }

    Status = NtfslxResolvePathToMftIndex(DeviceExtension,
                                         &BasePath,
                                         &MftIndex);

    /*
     * Disposition handling when the target exists: FILE_CREATE must fail,
     * FILE_OVERWRITE / FILE_OVERWRITE_IF / FILE_SUPERSEDE should truncate
     * the file back to zero bytes. FILE_OPEN / FILE_OPEN_IF just open it.
     *
     * Special case: if a stream name is present, the disposition applies
     * to the *stream*, not the base file. The base existing is fine; we
     * have to look up whether the named $DATA exists and dispatch on that.
     */
    if (NT_SUCCESS(Status))
    {
        ULONG CreateDisp = (Stack->Parameters.Create.Options >> 24) & 0xFF;

        if (StreamNameLength > 0)
        {
            /*
             * Probe the stream attribute. STATUS_SUCCESS means the named
             * stream exists; STATUS_OBJECT_NAME_NOT_FOUND means it does not.
             */
            PNTFSLX_MFT_RECORD ProbeRecord;
            PNTFSLX_ATTR_RECORD ProbeAttr;
            BOOLEAN StreamExists = FALSE;

            ProbeRecord = ExAllocatePoolWithTag(NonPagedPool,
                              DeviceExtension->VolumeInfo.BytesPerFileRecord,
                              NTFSLX_TAG);
            if (ProbeRecord != NULL)
            {
                NTSTATUS RdSt = NtfslxReadMftRecord(
                    DeviceExtension->StorageDevice,
                    &DeviceExtension->VolumeInfo,
                    DeviceExtension->MftRunlist,
                    MftIndex, ProbeRecord);
                if (NT_SUCCESS(RdSt))
                {
                    NTSTATUS LookupSt = NtfslxFindAttribute(
                        ProbeRecord, AttributeType,
                        StreamName, StreamNameLength,
                        &ProbeAttr);
                    StreamExists = NT_SUCCESS(LookupSt);
                }
                ExFreePoolWithTag(ProbeRecord, NTFSLX_TAG);
            }

            if (StreamExists)
            {
                if (CreateDisp == FILE_CREATE)
                {
                    DbgPrint("ntfslx: Create FILE_CREATE stream exists '%wZ:%.*S'\n",
                             &BasePath, StreamNameLength, StreamName);
                    return NtfslxCompleteRequest(Irp, STATUS_OBJECT_NAME_COLLISION, 0);
                }
                /* For OPEN / OPEN_IF / OVERWRITE_IF: fall through to open. */
            }
            else
            {
                if (CreateDisp == FILE_OPEN)
                {
                    return NtfslxCompleteRequest(Irp, STATUS_OBJECT_NAME_NOT_FOUND, 0);
                }
                if (CreateDisp == FILE_OPEN_IF ||
                    CreateDisp == FILE_CREATE ||
                    CreateDisp == FILE_OVERWRITE_IF ||
                    CreateDisp == FILE_SUPERSEDE)
                {
                    /* Materialize an empty named $DATA, then fall through. */
                    NTSTATUS AddSt = NtfslxAddNamedDataStream(DeviceExtension,
                                                              MftIndex,
                                                              StreamName,
                                                              StreamNameLength);
                    if (!NT_SUCCESS(AddSt))
                    {
                        DbgPrint("ntfslx: Create AddNamedDataStream '%wZ:%.*S' failed 0x%08lx\n",
                                 &BasePath, StreamNameLength, StreamName, AddSt);
                        return NtfslxCompleteRequest(Irp, AddSt, 0);
                    }
                    NotifyCreated = TRUE;
                }
                else if (CreateDisp == FILE_OVERWRITE)
                {
                    return NtfslxCompleteRequest(Irp, STATUS_OBJECT_NAME_NOT_FOUND, 0);
                }
            }
        }
        else if (CreateDisp == FILE_CREATE)
        {
            DbgPrint("ntfslx: Create FILE_CREATE but target exists: '%wZ' mft=%I64u\n",
                     &BasePath, MftIndex);
            return NtfslxCompleteRequest(Irp, STATUS_OBJECT_NAME_COLLISION, 0);
        }
        else if (CreateDisp == FILE_SUPERSEDE ||
                 CreateDisp == FILE_OVERWRITE ||
                 CreateDisp == FILE_OVERWRITE_IF)
        {
            /* Mark for truncation after open. */
            DbgPrint("ntfslx: Create disposition=%lu will truncate existing '%wZ' mft=%I64u\n",
                     CreateDisp, &BasePath, MftIndex);
            /* Fall through to open; truncation happens post-open below. */
        }
    }

    if (!NT_SUCCESS(Status))
    {
        static const UNICODE_STRING SystemVolumeInformationPrefix = RTL_CONSTANT_STRING(L"\\System Volume Information");
        ULONG CreateDisposition;

        CreateDisposition = (Stack->Parameters.Create.Options >> 24) & 0xFF;

        DbgPrint("ntfslx: create failed status=0x%08lx disposition=%lu path='%wZ'\n",
                 Status, CreateDisposition, &BasePath);

        /*
         * Handle create dispositions that create a new file when the path
         * is not found: FILE_CREATE, FILE_OPEN_IF, FILE_OVERWRITE_IF.
         */
        if (Status == STATUS_OBJECT_NAME_NOT_FOUND &&
            (CreateDisposition == FILE_CREATE ||
             CreateDisposition == FILE_OPEN_IF ||
             CreateDisposition == FILE_OVERWRITE_IF))
        {
            UNICODE_STRING ParentPath;
            UNICODE_STRING FinalComponent;
            ULONGLONG ParentMftIndex;
            ULONGLONG NewMftIndex;
            BOOLEAN IsDirectory;
            USHORT LastBackslash;
            USHORT I;

            IsDirectory = FlagOn(Stack->Parameters.Create.Options, FILE_DIRECTORY_FILE) ? TRUE : FALSE;
            if (HadTrailingBackslash && !IsDirectory)
            {
                return NtfslxCompleteRequest(Irp, STATUS_OBJECT_NAME_INVALID, 0);
            }

            /*
             * Split BasePath into parent directory and final component.
             * Find the last backslash to separate them.
             */
            LastBackslash = 0;
            for (I = 0; I < BasePath.Length / sizeof(WCHAR); I++)
            {
                if (BasePath.Buffer[I] == L'\\')
                {
                    LastBackslash = I;
                }
            }

            if (LastBackslash == 0 && BasePath.Length > sizeof(WCHAR) &&
                BasePath.Buffer[0] == L'\\')
            {
                /* File is directly under root */
                ParentPath.Buffer = BasePath.Buffer;
                ParentPath.Length = sizeof(WCHAR);
                ParentPath.MaximumLength = sizeof(WCHAR);

                FinalComponent.Buffer = BasePath.Buffer + 1;
                FinalComponent.Length = BasePath.Length - sizeof(WCHAR);
                FinalComponent.MaximumLength = FinalComponent.Length;
            }
            else
            {
                ParentPath.Buffer = BasePath.Buffer;
                ParentPath.Length = LastBackslash * sizeof(WCHAR);
                ParentPath.MaximumLength = ParentPath.Length;
                if (ParentPath.Length == 0)
                {
                    /* No backslash at all -- malformed path */
                    return NtfslxCompleteRequest(Irp, STATUS_OBJECT_PATH_NOT_FOUND, 0);
                }

                FinalComponent.Buffer = BasePath.Buffer + LastBackslash + 1;
                FinalComponent.Length = BasePath.Length - (LastBackslash + 1) * sizeof(WCHAR);
                FinalComponent.MaximumLength = FinalComponent.Length;
            }

            if (FinalComponent.Length == 0)
            {
                return NtfslxCompleteRequest(Irp, STATUS_OBJECT_NAME_INVALID, 0);
            }

            /* Resolve parent directory */
            Status = NtfslxResolvePathToMftIndex(DeviceExtension,
                                                 &ParentPath,
                                                 &ParentMftIndex);
            if (!NT_SUCCESS(Status))
            {
                return NtfslxCompleteRequest(Irp, Status, 0);
            }

            /* Create the file */
            DbgPrint("ntfslx: creating file '%wZ' under parent MFT %I64u isDir=%u\n",
                     &FinalComponent, ParentMftIndex, IsDirectory);
            Status = NtfslxCreateNewFile(DeviceExtension,
                                         ParentMftIndex,
                                         FinalComponent.Buffer,
                                         FinalComponent.Length / sizeof(WCHAR),
                                         IsDirectory,
                                         &NewMftIndex);
            DbgPrint("ntfslx: NtfslxCreateNewFile returned 0x%08lx newMft=%I64u\n",
                     Status, NewMftIndex);
            if (!NT_SUCCESS(Status))
            {
                return NtfslxCompleteRequest(Irp, Status, 0);
            }

            MftIndex = NewMftIndex;
            NotifyCreated = TRUE;

            /* Fall through to open the newly created file */
            goto OpenByIndex;
        }

        DPRINT1("ntfslx: create resolve failed File='%wZ' Base='%wZ' Status=0x%08lx DirOpt=%u NonDirOpt=%u\n",
                &Stack->FileObject->FileName,
                &BasePath,
                Status,
                FlagOn(Stack->Parameters.Create.Options, FILE_DIRECTORY_FILE) ? 1 : 0,
                FlagOn(Stack->Parameters.Create.Options, FILE_NON_DIRECTORY_FILE) ? 1 : 0);

        if ((Status == STATUS_OBJECT_PATH_NOT_FOUND || Status == STATUS_OBJECT_NAME_NOT_FOUND) &&
            RtlPrefixUnicodeString(&SystemVolumeInformationPrefix, &BasePath, TRUE))
        {
            DPRINT1("ntfslx: open hit missing System Volume Information path, verify image population because external NTFS listing may show the directory was never created\n");
        }

        return NtfslxCompleteRequest(Irp, Status, 0);
    }

OpenByIndex:

    Status = NtfslxOpenFileObjectByIndex(DeviceObject,
                                         Stack->FileObject,
                                         MftIndex,
                                         AttributeType,
                                         StreamName,
                                         StreamNameLength);
    if (!NT_SUCCESS(Status))
    {
        return NtfslxCompleteRequest(Irp, Status, 0);
    }

    Status = NtfslxValidateOpenFileObject(Stack->FileObject, &FileContext);
    if (NT_SUCCESS(Status))
    {
        if (HadTrailingBackslash && !FileContext->IsDirectory)
        {
            Status = STATUS_NOT_A_DIRECTORY;
        }
        else if (OpenTargetDirectory && !FileContext->IsDirectory)
        {
            Status = STATUS_NOT_A_DIRECTORY;
        }
        else if (FlagOn(Stack->Parameters.Create.Options, FILE_DIRECTORY_FILE) &&
                 !FileContext->IsDirectory)
        {
            Status = STATUS_NOT_A_DIRECTORY;
        }
        else if (FlagOn(Stack->Parameters.Create.Options, FILE_NON_DIRECTORY_FILE) &&
                 FileContext->IsDirectory)
        {
            Status = STATUS_FILE_IS_A_DIRECTORY;
        }
    }

    if (!NT_SUCCESS(Status))
    {
        NtfslxCloseFileObject(Stack->FileObject);
        return NtfslxCompleteRequest(Irp, Status, 0);
    }

    Status = NtfslxAllocateCcb(&Ccb);
    if (!NT_SUCCESS(Status))
    {
        NtfslxCloseFileObject(Stack->FileObject);
        return NtfslxCompleteRequest(Irp, Status, 0);
    }

    Stack->FileObject->FsContext2 = Ccb;
    Status = NtfslxValidateOpenFileObjectVpb(Stack->FileObject, DeviceExtension);
    if (!NT_SUCCESS(Status))
    {
        Stack->FileObject->FsContext2 = NULL;
        NtfslxFreeCcb(Ccb);
        NtfslxCloseFileObject(Stack->FileObject);
        return NtfslxCompleteRequest(Irp, Status, 0);
    }

    /*
     * Enforce share-access semantics. The first opener of an MFT slot seeds
     * the share record; subsequent openers are validated against it. A
     * conflicting request returns STATUS_SHARING_VIOLATION; without this we
     * silently let writers stomp readers.
     */
    {
        ACCESS_MASK Desired = Stack->Parameters.Create.SecurityContext
            ? Stack->Parameters.Create.SecurityContext->DesiredAccess
            : 0;
        ULONG ShareMode = Stack->Parameters.Create.ShareAccess;
        PNTFSLX_SHARE_RECORD ShareRec = NULL;

        Status = NtfslxShareAccessAcquire(DeviceExtension,
                                          FileContext->MftIndex,
                                          Desired, ShareMode,
                                          Stack->FileObject,
                                          &ShareRec);
        if (!NT_SUCCESS(Status))
        {
            Stack->FileObject->FsContext2 = NULL;
            NtfslxFreeCcb(Ccb);
            NtfslxCloseFileObject(Stack->FileObject);
            return NtfslxCompleteRequest(Irp, Status, 0);
        }
        FileContext->ShareRecord = ShareRec;
    }

    /*
     * Post-open actions driven by create flags:
     *   - FILE_DELETE_ON_CLOSE: mark CCB so cleanup deletes the file.
     *   - FILE_SUPERSEDE / FILE_OVERWRITE / FILE_OVERWRITE_IF: truncate to 0.
     */
    {
        ULONG CreateDisposition = (Stack->Parameters.Create.Options >> 24) & 0xFF;

        if (FlagOn(Stack->Parameters.Create.Options, FILE_DELETE_ON_CLOSE))
        {
            Status = NtfslxCheckDeleteAllowed(DeviceExtension,
                                              FileContext->MftIndex);
            if (!NT_SUCCESS(Status))
            {
                NtfslxShareAccessRelease(DeviceExtension,
                                         FileContext->ShareRecord,
                                         Stack->FileObject);
                FileContext->ShareRecord = NULL;
                Stack->FileObject->FsContext2 = NULL;
                NtfslxFreeCcb(Ccb);
                NtfslxCloseFileObject(Stack->FileObject);
                return NtfslxCompleteRequest(Irp, Status, 0);
            }

            Ccb->DeletePending = TRUE;
            DbgPrint("ntfslx: Create FILE_DELETE_ON_CLOSE set mft=%I64u\n",
                     FileContext->MftIndex);
        }

        if (!FileContext->IsDirectory &&
            (CreateDisposition == FILE_SUPERSEDE ||
             CreateDisposition == FILE_OVERWRITE ||
             CreateDisposition == FILE_OVERWRITE_IF) &&
            FileContext->DataSize > 0)
        {
            NTSTATUS TruncStatus = NtfslxSetFileSize(FileContext, 0);
            DbgPrint("ntfslx: Create truncate mft=%I64u result=0x%08lx\n",
                     FileContext->MftIndex, TruncStatus);
            /* Non-fatal: open still succeeds even if truncate partially fails. */
        }

        if (CreateDisposition == FILE_CREATE ||
            CreateDisposition == FILE_OPEN_IF ||
            CreateDisposition == FILE_OVERWRITE_IF)
        {
            /* For the "_IF" variants the result reflects whether we created
             * or opened. For now we set CREATED unconditionally in those cases
             * since we don't distinguish the "found existing" branch from
             * a fresh creation here. */
            CreateResult = FILE_OPENED; /* keep simple until needed */
        }

        /*
         * Initialize the cache map on this FileObject so NtCreateSection and
         * subsequent paging I/O from mapped views can be serviced. Failures
         * are non-fatal: the file is still readable/writable via the direct
         * runlist path; we just lose memory mapping support.
         */
        if (!FileContext->IsDirectory)
        {
            NTSTATUS CacheStatus;
            CacheStatus = NtfslxEnsureCacheInitialized(Stack->FileObject);
            if (!NT_SUCCESS(CacheStatus))
            {
                DbgPrint("ntfslx: Create cache init non-fatal failure mft=%I64u status=0x%08lx\n",
                         FileContext->MftIndex, CacheStatus);
            }
        }

        /*
         * Stash the full path in the FCB so FsRtlNotifyFullReportChange
         * can find it later. Non-fatal on allocation failure - the file
         * is still usable, we just lose notifications for it.
         */
        (VOID)NtfslxSetFileContextFullPath(FileContext, &BasePath);

        /*
         * Resolve and cache the parent MFT index from the split path.
         * Cleanup-time delete uses this directly rather than reading it
         * back from the on-disk $FILE_NAME attribute, which is ambiguous
         * across hardlinks and unreliable if the record was ever touched
         * by an older buggy writer. Root-level opens get NTFSLX_FILE_ROOT.
         */
        {
            UNICODE_STRING ParentResolve;
            USHORT LastSlash;
            USHORT Chars;
            USHORT Idx;

            ParentResolve = BasePath;
            Chars = (USHORT)(BasePath.Length / sizeof(WCHAR));
            LastSlash = 0;
            for (Idx = 0; Idx < Chars; ++Idx)
            {
                if (BasePath.Buffer[Idx] == L'\\')
                {
                    LastSlash = Idx;
                }
            }

            if (Chars == 0)
            {
                FileContext->ParentMftIndex = NTFSLX_FILE_ROOT;
            }
            else if (LastSlash == 0)
            {
                /* Single "\\foo" or "foo" — parent is volume root. */
                FileContext->ParentMftIndex = NTFSLX_FILE_ROOT;
            }
            else
            {
                ULONGLONG ParentResolved = NTFSLX_FILE_ROOT;
                ParentResolve.Buffer = BasePath.Buffer;
                ParentResolve.Length = (USHORT)(LastSlash * sizeof(WCHAR));
                ParentResolve.MaximumLength = ParentResolve.Length;
                if (NT_SUCCESS(NtfslxResolvePathToMftIndex(DeviceExtension,
                                                           &ParentResolve,
                                                           &ParentResolved)))
                {
                    FileContext->ParentMftIndex = ParentResolved;
                }
                else
                {
                    FileContext->ParentMftIndex = NTFSLX_FILE_ROOT;
                }
            }
        }

        /*
         * If the create actually produced a new file (as opposed to opening
         * an existing one), tell the directory-change notify package so
         * Explorer sees the new entry without a manual refresh.
         */
        if (NotifyCreated)
        {
            NtfslxNotifyReportChange(DeviceExtension, FileContext,
                                     FILE_NOTIFY_CHANGE_FILE_NAME |
                                         FILE_NOTIFY_CHANGE_CREATION |
                                         FILE_NOTIFY_CHANGE_SIZE |
                                         FILE_NOTIFY_CHANGE_ATTRIBUTES |
                                         FILE_NOTIFY_CHANGE_LAST_WRITE,
                                     FILE_ACTION_ADDED);
        }

        return NtfslxCompleteRequest(Irp, STATUS_SUCCESS, CreateResult);
    }
}

/*
 * Pull the first non-DOS $FILE_NAME's name into a caller-provided buffer.
 * Returns STATUS_OBJECT_NAME_NOT_FOUND if the record has no usable entry.
 */
static NTSTATUS
NtfslxExtractPrimaryFileName(
    _In_ PNTFSLX_MFT_RECORD Record,
    _Out_writes_(256) PWCHAR NameBuffer,
    _Out_ PULONG NameLength)
{
    PUCHAR Base;
    ULONG Offset;

    *NameLength = 0;

    if (Record == NULL)
        return STATUS_INVALID_PARAMETER;

    Base = (PUCHAR)Record;
    Offset = Record->AttributesOffset;
    while (Offset + sizeof(ULONG) * 2 <= Record->BytesInUse)
    {
        PNTFSLX_ATTR_RECORD Attr = (PNTFSLX_ATTR_RECORD)(Base + Offset);
        if (Attr->Type == NTFSLX_ATTRIBUTE_END)
            break;
        if (Attr->Length == 0 || Offset + Attr->Length > Record->BytesInUse)
            break;

        if (Attr->Type == NTFSLX_ATTRIBUTE_FILE_NAME &&
            Attr->NonResident == 0 &&
            Attr->Data.Resident.ValueLength >= sizeof(NTFSLX_FILE_NAME_ATTRIBUTE))
        {
            PNTFSLX_FILE_NAME_ATTRIBUTE Fn = (PNTFSLX_FILE_NAME_ATTRIBUTE)
                ((PUCHAR)Attr + Attr->Data.Resident.ValueOffset);
            if (Fn->FileNameType != NTFSLX_FILE_NAME_DOS &&
                Fn->FileNameLength > 0 && Fn->FileNameLength <= 255)
            {
                RtlCopyMemory(NameBuffer,
                              (PUCHAR)Fn + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE),
                              Fn->FileNameLength * sizeof(WCHAR));
                *NameLength = Fn->FileNameLength;
                return STATUS_SUCCESS;
            }
        }

        Offset += Attr->Length;
    }

    return STATUS_OBJECT_NAME_NOT_FOUND;
}

static
NTSTATUS
NtfslxCleanup(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    PNTFSLX_FILE_CONTEXT FileContext;
    PNTFSLX_CCB Ccb;
    ULONGLONG ParentMftIdx;
    WCHAR NameBuffer[256];
    ULONG NameLength;
    NTSTATUS Status;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;

    if (Stack->FileObject == NULL ||
        DeviceExtension == NULL ||
        DeviceExtension->Signature != NTFSLX_TAG ||
        !NtfslxIsVolumeDevice(DeviceExtension))
    {
        return NtfslxCompleteRequest(Irp, STATUS_SUCCESS, 0);
    }

    FileContext = (PNTFSLX_FILE_CONTEXT)Stack->FileObject->FsContext;
    Ccb = (PNTFSLX_CCB)Stack->FileObject->FsContext2;

    if (FileContext != NULL &&
        FileContext->Signature == NTFSLX_FILE_CONTEXT_SIGNATURE &&
        Ccb != NULL &&
        Ccb->DeletePending &&
        FileContext->FileRecord != NULL)
    {
        /*
         * Prefer the parent MFT index cached at Create time. It was
         * resolved from the split parent path while we still had the
         * caller's UNICODE_STRING, which is both unambiguous (no
         * hardlink aliasing) and independent of any stale on-disk
         * $FILE_NAME state. Fall back to the record-extracted path
         * only if the cache wasn't populated (e.g. non-standard open
         * paths that bypass the Create code in dispatch).
         */
        ParentMftIdx = FileContext->ParentMftIndex;
        Status = STATUS_SUCCESS;
        if (ParentMftIdx == NTFSLX_FILE_ROOT &&
            FileContext->MftIndex != NTFSLX_FILE_ROOT)
        {
            ULONGLONG RecordParent = NTFSLX_FILE_ROOT;
            NTSTATUS LookupStatus =
                NtfslxLookupParentIndexFromRecord(FileContext->FileRecord,
                                                  &RecordParent);
            if (NT_SUCCESS(LookupStatus) && RecordParent != NTFSLX_FILE_ROOT)
            {
                ParentMftIdx = RecordParent;
            }
        }
        Status = NtfslxExtractPrimaryFileName(FileContext->FileRecord,
                                              NameBuffer, &NameLength);
        if (NT_SUCCESS(Status))
        {
            DbgPrint("ntfslx: Cleanup delete mft=%I64u parent=%I64u name='%.*S'\n",
                     FileContext->MftIndex, ParentMftIdx, NameLength, NameBuffer);
            Status = NtfslxDeleteFile(DeviceExtension, ParentMftIdx,
                                      FileContext->MftIndex,
                                      NameBuffer, NameLength);
            DbgPrint("ntfslx: Cleanup delete result=0x%08lx\n", Status);

            if (NT_SUCCESS(Status))
            {
                NtfslxNotifyReportChange(DeviceExtension, FileContext,
                                         FILE_NOTIFY_CHANGE_FILE_NAME,
                                         FILE_ACTION_REMOVED);
            }
        }
        else
        {
            DbgPrint("ntfslx: Cleanup delete: resolve failed 0x%08lx mft=%I64u\n",
                     Status, FileContext->MftIndex);
        }
        Ccb->DeletePending = FALSE;
    }

    /*
     * If this handle made any size-changing writes, fire one consolidated
     * MODIFIED notification now (instead of per-chunk during the writes).
     * Explorer's directory watcher only needs one wake-up per close to
     * refresh the size column. Skip if the file is being deleted; the
     * REMOVED notification covers it.
     */
    if (FileContext != NULL &&
        FileContext->Signature == NTFSLX_FILE_CONTEXT_SIGNATURE &&
        FileContext->PendingDataNotify &&
        !(Ccb != NULL && Ccb->DeletePending))
    {
        NtfslxNotifyReportChange(DeviceExtension, FileContext,
                                 FILE_NOTIFY_CHANGE_SIZE |
                                     FILE_NOTIFY_CHANGE_LAST_WRITE,
                                 FILE_ACTION_MODIFIED);
        FileContext->PendingDataNotify = FALSE;
    }

    /* Drop any pending NOTIFY_CHANGE_DIRECTORY IRPs keyed off this CCB. */
    NtfslxNotifyCleanupCcb(DeviceExtension, Ccb);

    /*
     * Release the per-MFT share-access record. Doing this in cleanup (not
     * close) matches the FILE_OBJECT lifetime: cleanup runs once per handle
     * and the share-access state is supposed to track open handles, not
     * outstanding kernel references.
     */
    if (FileContext != NULL &&
        FileContext->Signature == NTFSLX_FILE_CONTEXT_SIGNATURE &&
        FileContext->ShareRecord != NULL)
    {
        NtfslxShareAccessRelease(DeviceExtension, FileContext->ShareRecord,
                                 Stack->FileObject);
        FileContext->ShareRecord = NULL;
    }

    return NtfslxCompleteRequest(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
NtfslxClose(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    PNTFSLX_CCB Ccb;
    NTSTATUS Status;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;

    if (Stack->FileObject != NULL)
    {
        if (Stack->FileObject->FsContext != NULL &&
            Stack->FileObject->FsContext2 != NULL &&
            DeviceExtension != NULL &&
            DeviceExtension->Signature == NTFSLX_TAG &&
            NtfslxIsVolumeDevice(DeviceExtension))
        {
            Status = NtfslxValidateCloseFileObjectVpb(Stack->FileObject, DeviceExtension);
            if (!NT_SUCCESS(Status))
            {
                return NtfslxCompleteRequest(Irp, Status, 0);
            }
        }

        Ccb = (PNTFSLX_CCB)Stack->FileObject->FsContext2;
        Stack->FileObject->FsContext2 = NULL;
        NtfslxFreeCcb(Ccb);
        NtfslxCloseFileObject(Stack->FileObject);
    }

    return NtfslxCompleteRequest(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
NtfslxRead(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    LARGE_INTEGER ByteOffset;
    ULONG Length;
    ULONG BytesRead;
    PVOID Buffer;
    NTSTATUS Status;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    Length = Stack->Parameters.Read.Length;

    if (!NtfslxIsVolumeDevice(DeviceExtension) || Stack->FileObject == NULL)
    {
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }

    if (Length == 0)
    {
        return NtfslxCompleteRequest(Irp, STATUS_SUCCESS, 0);
    }

    /*
     * Paging I/O arrives from the MM when a page fault on a mapped view
     * needs the backing bytes read in. We service it via the runlist reader
     * exactly like a normal read — no cache interaction, no locks held that
     * would block. For paging I/O the MDL is already mapped by the IO mgr,
     * so NtfslxGetUserBuffer(TRUE) gives us the right kernel VA.
     */
    if (BooleanFlagOn(Irp->Flags, IRP_PAGING_IO))
    {
        Buffer = NtfslxGetUserBuffer(Irp, TRUE);
    }
    else
    {
        Buffer = NtfslxGetUserBuffer(Irp, FALSE);
    }

    if (Buffer == NULL)
    {
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_USER_BUFFER, 0);
    }

    ByteOffset = Stack->Parameters.Read.ByteOffset;
    if (ByteOffset.LowPart == FILE_USE_FILE_POINTER_POSITION &&
        ByteOffset.HighPart == -1)
    {
        ByteOffset = Stack->FileObject->CurrentByteOffset;
    }

    Status = NtfslxReadFileObject(Stack->FileObject,
                                  Buffer,
                                  Length,
                                  &ByteOffset,
                                  BooleanFlagOn(Irp->Flags, IRP_PAGING_IO),
                                  &BytesRead);
    if (NT_SUCCESS(Status) &&
        !BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) &&
        BooleanFlagOn(Stack->FileObject->Flags, FO_SYNCHRONOUS_IO))
    {
        Stack->FileObject->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + BytesRead;
    }

    return NtfslxCompleteRequest(Irp, Status, NT_SUCCESS(Status) ? BytesRead : 0);
}

static
NTSTATUS
NtfslxWrite(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    LARGE_INTEGER ByteOffset;
    ULONG Length;
    ULONG BytesWritten;
    PVOID Buffer;
    NTSTATUS Status;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    Length = Stack->Parameters.Write.Length;

    if (!NtfslxIsVolumeDevice(DeviceExtension) || Stack->FileObject == NULL)
    {
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }

    if (Length == 0)
    {
        return NtfslxCompleteRequest(Irp, STATUS_SUCCESS, 0);
    }

    Buffer = NtfslxGetUserBuffer(Irp, BooleanFlagOn(Irp->Flags, IRP_PAGING_IO));
    if (Buffer == NULL)
    {
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_USER_BUFFER, 0);
    }

    ByteOffset = Stack->Parameters.Write.ByteOffset;
    if (ByteOffset.LowPart == FILE_USE_FILE_POINTER_POSITION &&
        ByteOffset.HighPart == -1)
    {
        ByteOffset = Stack->FileObject->CurrentByteOffset;
    }

    Status = NtfslxWriteFileObject(Stack->FileObject,
                                   Buffer,
                                   Length,
                                   &ByteOffset,
                                   &BytesWritten);
    if (NT_SUCCESS(Status) &&
        !BooleanFlagOn(Irp->Flags, IRP_PAGING_IO) &&
        BooleanFlagOn(Stack->FileObject->Flags, FO_SYNCHRONOUS_IO))
    {
        Stack->FileObject->CurrentByteOffset.QuadPart = ByteOffset.QuadPart + BytesWritten;
    }

    return NtfslxCompleteRequest(Irp, Status, NT_SUCCESS(Status) ? BytesWritten : 0);
}

static
NTSTATUS
NtfslxQueryDirectoryHeaderSize(
    _In_ FILE_INFORMATION_CLASS FileInformationClass)
{
    switch (FileInformationClass)
    {
        case FileDirectoryInformation:
            return FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName);

        case FileFullDirectoryInformation:
            return FIELD_OFFSET(FILE_FULL_DIR_INFORMATION, FileName);

        case FileIdFullDirectoryInformation:
            return FIELD_OFFSET(FILE_ID_FULL_DIR_INFORMATION, FileName);

        case FileBothDirectoryInformation:
            return FIELD_OFFSET(FILE_BOTH_DIR_INFORMATION, FileName);

        case FileIdBothDirectoryInformation:
            return FIELD_OFFSET(FILE_ID_BOTH_DIR_INFORMATION, FileName);

        case FileNamesInformation:
            return FIELD_OFFSET(FILE_NAMES_INFORMATION, FileName);

        default:
            return 0;
    }
}

static
VOID
NtfslxQueryDirectoryFillCommonHeader(
    _Out_ PNTFSLX_QUERY_DIRECTORY_COMMON_HEADER Header,
    _In_ const NTFSLX_DIR_ENTRY *Entry,
    _In_ ULONG FileIndex)
{
    Header->NextEntryOffset = 0;
    Header->FileIndex = FileIndex;
    Header->CreationTime = Entry->CreationTime;
    Header->LastAccessTime = Entry->LastAccessTime;
    Header->LastWriteTime = Entry->LastDataChangeTime;
    Header->ChangeTime = Entry->LastMftChangeTime;
    Header->EndOfFile.QuadPart = (LONGLONG)Entry->DataSize;
    Header->AllocationSize.QuadPart = (LONGLONG)Entry->AllocatedSize;
    Header->FileAttributes = Entry->FileAttributes;
    Header->FileNameLength = (ULONG)Entry->FileNameLength * sizeof(WCHAR);
}

static
NTSTATUS
NtfslxQueryDirectoryWriteEntry(
    _Inout_ PNTFSLX_QUERY_DIRECTORY_CONTEXT QueryContext,
    _In_ const NTFSLX_DIR_ENTRY *Entry)
{
    ULONG HeaderSize;
    ULONG NameBytes;
    ULONG TotalSize;
    ULONG AlignedSize;
    ULONG Remaining;
    ULONG CurrentOffset;
    ULONG WrittenSize;
    PVOID CurrentBuffer;
    PNTFSLX_QUERY_DIRECTORY_COMMON_HEADER Common;
    BOOLEAN TerminalEntry;

    HeaderSize = NtfslxQueryDirectoryHeaderSize(QueryContext->FileInformationClass);
    if (HeaderSize == 0)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    NameBytes = Entry->FileNameLength * sizeof(WCHAR);
    TotalSize = HeaderSize + NameBytes;
    AlignedSize = ROUND_UP(TotalSize, sizeof(ULONGLONG));
    Remaining = QueryContext->BufferLength - QueryContext->BytesWritten;

    if (Remaining < TotalSize)
    {
        if (QueryContext->AnyWritten)
        {
            return STATUS_NO_MORE_FILES;
        }

        QueryContext->Overflowed = TRUE;
        return STATUS_BUFFER_OVERFLOW;
    }

    TerminalEntry = QueryContext->ReturnSingleEntry;
    WrittenSize = AlignedSize;
    if (!TerminalEntry && Remaining < AlignedSize)
    {
        if (QueryContext->AnyWritten)
        {
            return STATUS_NO_MORE_FILES;
        }

        TerminalEntry = TRUE;
        WrittenSize = TotalSize;
    }

    CurrentOffset = QueryContext->BytesWritten;
    CurrentBuffer = (PUCHAR)QueryContext->Buffer + CurrentOffset;
    RtlZeroMemory(CurrentBuffer, TotalSize);

    if (QueryContext->LastNextEntryOffset != NULL)
    {
        *QueryContext->LastNextEntryOffset = CurrentOffset - QueryContext->LastEntryOffset;
    }

    switch (QueryContext->FileInformationClass)
    {
        case FileDirectoryInformation:
            Common = (PNTFSLX_QUERY_DIRECTORY_COMMON_HEADER)CurrentBuffer;
            NtfslxQueryDirectoryFillCommonHeader(Common, Entry, QueryContext->CurrentIndex);
            break;

        case FileFullDirectoryInformation:
            Common = (PNTFSLX_QUERY_DIRECTORY_COMMON_HEADER)CurrentBuffer;
            NtfslxQueryDirectoryFillCommonHeader(Common, Entry, QueryContext->CurrentIndex);
            ((PFILE_FULL_DIR_INFORMATION)CurrentBuffer)->EaSize = 0;
            break;

        case FileIdFullDirectoryInformation:
            Common = (PNTFSLX_QUERY_DIRECTORY_COMMON_HEADER)CurrentBuffer;
            NtfslxQueryDirectoryFillCommonHeader(Common, Entry, QueryContext->CurrentIndex);
            ((PFILE_ID_FULL_DIR_INFORMATION)CurrentBuffer)->EaSize = 0;
            ((PFILE_ID_FULL_DIR_INFORMATION)CurrentBuffer)->FileId.QuadPart = Entry->FileReference;
            break;

        case FileBothDirectoryInformation:
            Common = (PNTFSLX_QUERY_DIRECTORY_COMMON_HEADER)CurrentBuffer;
            NtfslxQueryDirectoryFillCommonHeader(Common, Entry, QueryContext->CurrentIndex);
            ((PFILE_BOTH_DIR_INFORMATION)CurrentBuffer)->EaSize = 0;
            ((PFILE_BOTH_DIR_INFORMATION)CurrentBuffer)->ShortNameLength = 0;
            break;

        case FileIdBothDirectoryInformation:
            Common = (PNTFSLX_QUERY_DIRECTORY_COMMON_HEADER)CurrentBuffer;
            NtfslxQueryDirectoryFillCommonHeader(Common, Entry, QueryContext->CurrentIndex);
            ((PFILE_ID_BOTH_DIR_INFORMATION)CurrentBuffer)->EaSize = 0;
            ((PFILE_ID_BOTH_DIR_INFORMATION)CurrentBuffer)->ShortNameLength = 0;
            ((PFILE_ID_BOTH_DIR_INFORMATION)CurrentBuffer)->FileId.QuadPart = Entry->FileReference;
            break;

        case FileNamesInformation:
            ((PFILE_NAMES_INFORMATION)CurrentBuffer)->NextEntryOffset = 0;
            ((PFILE_NAMES_INFORMATION)CurrentBuffer)->FileIndex = QueryContext->CurrentIndex;
            ((PFILE_NAMES_INFORMATION)CurrentBuffer)->FileNameLength = NameBytes;
            break;

        default:
            return STATUS_INVALID_INFO_CLASS;
    }

    RtlCopyMemory((PUCHAR)CurrentBuffer + HeaderSize,
                  Entry->FileName,
                  NameBytes);

    QueryContext->LastNextEntryOffset = (PULONG)CurrentBuffer;
    QueryContext->LastEntryOffset = CurrentOffset;
    QueryContext->BytesWritten += WrittenSize;
    QueryContext->CurrentIndex++;
    QueryContext->NextIndex = QueryContext->CurrentIndex;
    QueryContext->AnyWritten = TRUE;

    NTFSLX_DISPATCH_TRACE("ntfslx: QueryDirectory emit dir='%wZ' class=%s(%d) index=%lu ref=0x%I64x attrs=0x%08lx eof=%I64u alloc=%I64u name='%.*S' written=%lu terminal=%u\n",
                         (PUNICODE_STRING)NtfslxTraceUnicodeStringOrEmpty(QueryContext->DirectoryPath),
                         NtfslxDirectoryInfoClassName(QueryContext->FileInformationClass),
                         (int)QueryContext->FileInformationClass,
                         QueryContext->CurrentIndex - 1,
                         Entry->FileReference,
                         Entry->FileAttributes,
                         Entry->DataSize,
                         Entry->AllocatedSize,
                         (int)Entry->FileNameLength,
                         Entry->FileName,
                         WrittenSize,
                         TerminalEntry);

    if (TerminalEntry)
    {
        return STATUS_NO_MORE_FILES;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxQueryDirectoryCallback(
    _In_ const NTFSLX_DIR_ENTRY *Entry,
    _In_opt_ PVOID Context)
{
    PNTFSLX_QUERY_DIRECTORY_CONTEXT QueryContext;
    UNICODE_STRING EntryName;

    QueryContext = Context;
    EntryName.Buffer = (PWCHAR)Entry->FileName;
    EntryName.Length = Entry->FileNameLength * sizeof(WCHAR);
    EntryName.MaximumLength = EntryName.Length;

    if (QueryContext->SearchPattern != NULL &&
        QueryContext->SearchPattern->Length > 0)
    {
        BOOLEAN Match;
        Match = FsRtlIsNameInExpression(
                    (PUNICODE_STRING)QueryContext->SearchPattern,
                    &EntryName,
                    (BOOLEAN)!QueryContext->CaseSensitive,
                    NULL);
        DbgPrint("ntfslx: dirmatch pat='%wZ' name='%.*S' caseSens=%u match=%u\n",
                 (PUNICODE_STRING)QueryContext->SearchPattern,
                 (int)(EntryName.Length / sizeof(WCHAR)),
                 EntryName.Buffer,
                 QueryContext->CaseSensitive,
                 Match);
        if (!Match)
        {
            return STATUS_SUCCESS;
        }
    }

    if (QueryContext->CurrentIndex < QueryContext->StartIndex)
    {
        NTFSLX_DISPATCH_TRACE("ntfslx: QueryDirectory skip dir='%wZ' class=%s(%d) index=%lu start=%lu name='%.*S'\n",
                             (PUNICODE_STRING)NtfslxTraceUnicodeStringOrEmpty(QueryContext->DirectoryPath),
                             NtfslxDirectoryInfoClassName(QueryContext->FileInformationClass),
                             (int)QueryContext->FileInformationClass,
                             QueryContext->CurrentIndex,
                             QueryContext->StartIndex,
                             (int)(EntryName.Length / sizeof(WCHAR)),
                             EntryName.Buffer);
        QueryContext->CurrentIndex++;
        QueryContext->NextIndex = QueryContext->CurrentIndex;
        return STATUS_SUCCESS;
    }

    return NtfslxQueryDirectoryWriteEntry(QueryContext, Entry);
}

static
NTSTATUS
NtfslxQueryDirectory(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    PNTFSLX_FILE_CONTEXT FileContext;
    PNTFSLX_CCB Ccb;
    NTFSLX_QUERY_DIRECTORY_CONTEXT QueryContext;
    PVOID Buffer;
    BOOLEAN FirstQuery;
    NTSTATUS Status;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;

    if (!NtfslxIsVolumeDevice(DeviceExtension) || Stack->FileObject == NULL)
    {
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }

    if (Stack->MinorFunction == IRP_MN_NOTIFY_CHANGE_DIRECTORY)
    {
        return NtfslxNotifyChangeDirectory(DeviceObject, Irp);
    }

    if (Stack->MinorFunction != IRP_MN_QUERY_DIRECTORY)
    {
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }

    if (NtfslxQueryDirectoryHeaderSize(Stack->Parameters.QueryDirectory.FileInformationClass) == 0)
    {
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_INFO_CLASS, 0);
    }

    Status = NtfslxValidateOpenFileObject(Stack->FileObject, &FileContext);
    if (!NT_SUCCESS(Status))
    {
        return NtfslxCompleteRequest(Irp, Status, 0);
    }

    if (!FileContext->IsDirectory)
    {
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }

    Ccb = (PNTFSLX_CCB)Stack->FileObject->FsContext2;
    if (Ccb == NULL || Ccb->Signature != NTFSLX_CCB_SIGNATURE)
    {
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }

    Buffer = NtfslxGetUserBuffer(Irp, FALSE);
    if (Buffer == NULL && Stack->Parameters.QueryDirectory.Length != 0)
    {
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_USER_BUFFER, 0);
    }

    FirstQuery = !Ccb->SearchInitialized;
    Status = NtfslxPrepareSearchPattern(Ccb, Stack->Parameters.QueryDirectory.FileName);
    if (!NT_SUCCESS(Status))
    {
        return NtfslxCompleteRequest(Irp, Status, 0);
    }

    if (BooleanFlagOn(Stack->Flags, SL_INDEX_SPECIFIED))
    {
        Ccb->DirectoryIndex = Stack->Parameters.QueryDirectory.FileIndex;
    }
    else if (FirstQuery || BooleanFlagOn(Stack->Flags, SL_RESTART_SCAN))
    {
        Ccb->DirectoryIndex = 0;
    }

    RtlZeroMemory(&QueryContext, sizeof(QueryContext));
    QueryContext.FileInformationClass = Stack->Parameters.QueryDirectory.FileInformationClass;
    QueryContext.Buffer = Buffer;
    QueryContext.BufferLength = Stack->Parameters.QueryDirectory.Length;
    QueryContext.StartIndex = Ccb->DirectoryIndex;
    QueryContext.SearchPattern = &Ccb->SearchPattern;
    QueryContext.DirectoryPath = NtfslxTraceFileContextPath(FileContext);
    QueryContext.NextIndex = Ccb->DirectoryIndex;
    QueryContext.ReturnSingleEntry = BooleanFlagOn(Stack->Flags, SL_RETURN_SINGLE_ENTRY);
    QueryContext.CaseSensitive = BooleanFlagOn(Stack->Flags, SL_CASE_SENSITIVE);

    NTFSLX_DISPATCH_TRACE("ntfslx: QueryDirectory begin dir='%wZ' class=%s(%d) buflen=%lu start=%lu flags=0x%02x first=%u restart=%u return1=%u indexSpecified=%u caseSensitive=%u pattern='%wZ'\n",
                         (PUNICODE_STRING)QueryContext.DirectoryPath,
                         NtfslxDirectoryInfoClassName(QueryContext.FileInformationClass),
                         (int)QueryContext.FileInformationClass,
                         QueryContext.BufferLength,
                         QueryContext.StartIndex,
                         (ULONG)Stack->Flags,
                         FirstQuery,
                         BooleanFlagOn(Stack->Flags, SL_RESTART_SCAN),
                         QueryContext.ReturnSingleEntry,
                         BooleanFlagOn(Stack->Flags, SL_INDEX_SPECIFIED),
                         QueryContext.CaseSensitive,
                         (PUNICODE_STRING)NtfslxTraceUnicodeStringOrEmpty(QueryContext.SearchPattern));

    Status = NtfslxEnumerateLookupDirectory(DeviceExtension,
                                            FileContext->FileRecord,
                                            NtfslxQueryDirectoryCallback,
                                            &QueryContext);
    if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_OVERFLOW || Status == STATUS_NO_MORE_FILES)
    {
        Ccb->DirectoryIndex = QueryContext.NextIndex;
    }

    if (Status == STATUS_NO_MORE_FILES && QueryContext.AnyWritten)
    {
        Status = QueryContext.Overflowed ? STATUS_BUFFER_OVERFLOW : STATUS_SUCCESS;
    }

    if (NT_SUCCESS(Status) && !QueryContext.AnyWritten)
    {
        Status = FirstQuery ? STATUS_NO_SUCH_FILE : STATUS_NO_MORE_FILES;
    }

    NTFSLX_DISPATCH_TRACE("ntfslx: QueryDirectory done dir='%wZ' class=%s(%d) status=0x%08lx bytes=%lu any=%u next=%lu overflow=%u pattern='%wZ'\n",
                         (PUNICODE_STRING)NtfslxTraceUnicodeStringOrEmpty(QueryContext.DirectoryPath),
                         NtfslxDirectoryInfoClassName(QueryContext.FileInformationClass),
                         (int)QueryContext.FileInformationClass,
                         Status,
                         QueryContext.BytesWritten,
                         QueryContext.AnyWritten,
                         QueryContext.NextIndex,
                         QueryContext.Overflowed,
                         (PUNICODE_STRING)NtfslxTraceUnicodeStringOrEmpty(QueryContext.SearchPattern));

    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW)
    {
        return NtfslxCompleteRequest(Irp, Status, 0);
    }

    return NtfslxCompleteRequest(Irp, Status, QueryContext.BytesWritten);
}

/*
 * Resolve the file's cached metadata pointers (StdInfo, primary FnAttr)
 * once so per-class handlers can read real values from the MFT record.
 */
static VOID
NtfslxCollectFileMetadata(
    _In_ PNTFSLX_FILE_CONTEXT FileContext,
    _Out_ PNTFSLX_STANDARD_INFORMATION *OutStdInfo,
    _Out_ PNTFSLX_FILE_NAME_ATTRIBUTE *OutFnAttr,
    _Out_ PULONG OutFileAttributes)
{
    PNTFSLX_ATTR_RECORD AttrRecord;
    PNTFSLX_STANDARD_INFORMATION StdInfo = NULL;
    PNTFSLX_FILE_NAME_ATTRIBUTE FnAttr = NULL;
    ULONG RealAttributes = 0;
    NTSTATUS LocalStatus;

    LocalStatus = NtfslxFindAttribute(FileContext->FileRecord,
                                      NTFSLX_ATTRIBUTE_STANDARD_INFORMATION,
                                      NULL, 0, &AttrRecord);
    if (NT_SUCCESS(LocalStatus) && AttrRecord->NonResident == 0 &&
        AttrRecord->Data.Resident.ValueLength >= sizeof(NTFSLX_STANDARD_INFORMATION))
    {
        StdInfo = (PNTFSLX_STANDARD_INFORMATION)
            ((PUCHAR)AttrRecord + AttrRecord->Data.Resident.ValueOffset);
        RealAttributes = StdInfo->FileAttributes;
    }

    LocalStatus = NtfslxFindAttribute(FileContext->FileRecord,
                                      NTFSLX_ATTRIBUTE_FILE_NAME,
                                      NULL, 0, &AttrRecord);
    if (NT_SUCCESS(LocalStatus) && AttrRecord->NonResident == 0 &&
        AttrRecord->Data.Resident.ValueLength >= sizeof(NTFSLX_FILE_NAME_ATTRIBUTE))
    {
        FnAttr = (PNTFSLX_FILE_NAME_ATTRIBUTE)
            ((PUCHAR)AttrRecord + AttrRecord->Data.Resident.ValueOffset);
        if (RealAttributes == 0)
        {
            RealAttributes = FnAttr->FileAttributes;
        }
    }

    if (FileContext->IsDirectory)
    {
        RealAttributes |= FILE_ATTRIBUTE_DIRECTORY;
        RealAttributes &= ~FILE_ATTRIBUTE_NORMAL;
    }
    else if ((RealAttributes & ~FILE_ATTRIBUTE_NORMAL) == 0)
    {
        RealAttributes = FILE_ATTRIBUTE_NORMAL;
    }

    *OutStdInfo = StdInfo;
    *OutFnAttr = FnAttr;
    *OutFileAttributes = RealAttributes;
}

static VOID
NtfslxFillBasicInformation(
    _In_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_opt_ PNTFSLX_STANDARD_INFORMATION StdInfo,
    _In_opt_ PNTFSLX_FILE_NAME_ATTRIBUTE FnAttr,
    _In_ ULONG RealAttributes,
    _Out_ PFILE_BASIC_INFORMATION Info)
{
    UNREFERENCED_PARAMETER(FileContext);
    RtlZeroMemory(Info, sizeof(*Info));
    if (StdInfo != NULL)
    {
        Info->CreationTime.QuadPart = (LONGLONG)StdInfo->CreationTime;
        Info->LastAccessTime.QuadPart = (LONGLONG)StdInfo->LastAccessTime;
        Info->LastWriteTime.QuadPart = (LONGLONG)StdInfo->LastDataChangeTime;
        Info->ChangeTime.QuadPart = (LONGLONG)StdInfo->LastMftChangeTime;
    }
    else if (FnAttr != NULL)
    {
        Info->CreationTime.QuadPart = (LONGLONG)FnAttr->CreationTime;
        Info->LastAccessTime.QuadPart = (LONGLONG)FnAttr->LastAccessTime;
        Info->LastWriteTime.QuadPart = (LONGLONG)FnAttr->LastDataChangeTime;
        Info->ChangeTime.QuadPart = (LONGLONG)FnAttr->LastMftChangeTime;
    }
    Info->FileAttributes = RealAttributes;
}

static VOID
NtfslxFillStandardInformation(
    _In_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_opt_ PNTFSLX_CCB Ccb,
    _Out_ PFILE_STANDARD_INFORMATION Info)
{
    RtlZeroMemory(Info, sizeof(*Info));
    if (FileContext->IsDirectory)
    {
        Info->AllocationSize.QuadPart = 0;
        Info->EndOfFile.QuadPart = 0;
        Info->Directory = TRUE;
    }
    else
    {
        Info->AllocationSize.QuadPart = (LONGLONG)FileContext->AllocationSize;
        Info->EndOfFile.QuadPart = (LONGLONG)FileContext->DataSize;
        Info->Directory = FALSE;
    }
    Info->NumberOfLinks = FileContext->FileRecord
        ? FileContext->FileRecord->LinkCount
        : 1;
    if (Info->NumberOfLinks == 0)
        Info->NumberOfLinks = 1;
    Info->DeletePending = (Ccb != NULL) ? Ccb->DeletePending : FALSE;
}

static LONGLONG
NtfslxQueryFileId(
    _In_opt_ PNTFSLX_FILE_CONTEXT FileContext)
{
    if (FileContext != NULL && FileContext->FileRecord != NULL)
    {
        return (LONGLONG)
            MK_MREF(FileContext->MftIndex,
                    FileContext->FileRecord->SequenceNumber);
    }

    if (FileContext != NULL)
    {
        return (LONGLONG)FileContext->MftIndex;
    }

    return NTFSLX_FILE_ROOT;
}

static NTSTATUS
NtfslxFillNameInformation(
    _In_opt_ PNTFSLX_FILE_NAME_ATTRIBUTE FnAttr,
    _Out_writes_bytes_(BufferLength) PVOID Buffer,
    _In_ ULONG BufferLength,
    _Out_ PULONG WrittenLength)
{
    PFILE_NAME_INFORMATION Info = Buffer;
    const WCHAR *Name = NULL;
    ULONG NameCharCount = 0;
    ULONG TotalBytes;
    ULONG HeaderBytes;

    HeaderBytes = FIELD_OFFSET(FILE_NAME_INFORMATION, FileName);
    *WrittenLength = 0;

    if (FnAttr != NULL && FnAttr->FileNameLength > 0)
    {
        Name = (const WCHAR *)((PUCHAR)FnAttr + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE));
        NameCharCount = FnAttr->FileNameLength;
    }

    /* Format is "\<name>" — leading backslash plus the file name. */
    TotalBytes = HeaderBytes + (NameCharCount + 1) * sizeof(WCHAR);
    if (BufferLength < HeaderBytes + sizeof(WCHAR))
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    Info->FileNameLength = (NameCharCount + 1) * sizeof(WCHAR);
    Info->FileName[0] = L'\\';
    if (BufferLength < TotalBytes)
    {
        ULONG Avail = (BufferLength - HeaderBytes) / sizeof(WCHAR);
        if (Avail > 1 && Name != NULL)
        {
            ULONG Copy = Avail - 1;
            if (Copy > NameCharCount) Copy = NameCharCount;
            RtlCopyMemory(&Info->FileName[1], Name, Copy * sizeof(WCHAR));
        }
        *WrittenLength = HeaderBytes + Avail * sizeof(WCHAR);
        return STATUS_BUFFER_OVERFLOW;
    }

    if (Name != NULL)
    {
        RtlCopyMemory(&Info->FileName[1], Name, NameCharCount * sizeof(WCHAR));
    }
    *WrittenLength = TotalBytes;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxQueryFileInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    PNTFSLX_FILE_CONTEXT FileContext;
    PNTFSLX_CCB Ccb;
    PNTFSLX_STANDARD_INFORMATION StdInfo = NULL;
    PNTFSLX_FILE_NAME_ATTRIBUTE FnAttr = NULL;
    ULONG RealAttributes = 0;
    PVOID SystemBuffer;
    ULONG BufferLength;
    ULONG ReturnLength = 0;
    NTSTATUS Status;
    FILE_INFORMATION_CLASS InfoClass;
    BOOLEAN ValidContext = FALSE;
    PCUNICODE_STRING TracePath;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    SystemBuffer = Irp->AssociatedIrp.SystemBuffer;
    BufferLength = Stack->Parameters.QueryFile.Length;
    InfoClass = Stack->Parameters.QueryFile.FileInformationClass;

    if (!NtfslxIsVolumeDevice(DeviceExtension) || Stack->FileObject == NULL)
    {
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }

    FileContext = (PNTFSLX_FILE_CONTEXT)Stack->FileObject->FsContext;
    Ccb = (PNTFSLX_CCB)Stack->FileObject->FsContext2;

    if (FileContext == NULL ||
        FileContext->Signature != NTFSLX_FILE_CONTEXT_SIGNATURE ||
        FileContext->FileRecord == NULL)
    {
        /* Volume open or otherwise contextless — provide minimal data. */
        RealAttributes = FILE_ATTRIBUTE_DIRECTORY;
    }
    else
    {
        ValidContext = TRUE;
        NtfslxCollectFileMetadata(FileContext, &StdInfo, &FnAttr, &RealAttributes);
    }

    TracePath = ValidContext ? NtfslxTraceFileContextPath(FileContext)
                             : NtfslxTraceUnicodeStringOrEmpty(NULL);
    DbgPrint("ntfslx: QueryFileInformation begin path='%wZ' class=%s(%d) fileObj=%p buflen=%lu hasCtx=%u mft=%I64u dataSize=%I64u allocSize=%I64u attrs=0x%08lx isDir=%u\n",
             (PUNICODE_STRING)TracePath,
             NtfslxFileInfoClassName(InfoClass),
             (int)InfoClass,
             Stack->FileObject,
             BufferLength,
             ValidContext,
             ValidContext ? FileContext->MftIndex : 0,
             ValidContext ? FileContext->DataSize : 0,
             ValidContext ? FileContext->AllocationSize : 0,
             RealAttributes,
             ValidContext ? FileContext->IsDirectory : TRUE);

    Status = STATUS_SUCCESS;

    switch (InfoClass)
    {
    case FileBasicInformation:
        if (BufferLength >= sizeof(FILE_BASIC_INFORMATION))
        {
            if (FileContext != NULL)
            {
                NtfslxFillBasicInformation(FileContext, StdInfo, FnAttr,
                                           RealAttributes, SystemBuffer);
            }
            else
            {
                PFILE_BASIC_INFORMATION Info = SystemBuffer;
                RtlZeroMemory(Info, sizeof(*Info));
                Info->FileAttributes = RealAttributes;
            }
            ReturnLength = sizeof(FILE_BASIC_INFORMATION);
        }
        else
        {
            Status = STATUS_BUFFER_OVERFLOW;
        }
        break;

    case FileStandardInformation:
        if (BufferLength >= sizeof(FILE_STANDARD_INFORMATION))
        {
            if (FileContext != NULL)
            {
                PFILE_STANDARD_INFORMATION Info = SystemBuffer;
                NtfslxFillStandardInformation(FileContext, Ccb, SystemBuffer);
                DbgPrint("ntfslx: StdInfo returned mft=%I64u eof=%I64u alloc=%I64u dir=%u\n",
                         FileContext->MftIndex,
                         (ULONGLONG)Info->EndOfFile.QuadPart,
                         (ULONGLONG)Info->AllocationSize.QuadPart,
                         Info->Directory);
            }
            else
            {
                PFILE_STANDARD_INFORMATION Info = SystemBuffer;
                RtlZeroMemory(Info, sizeof(*Info));
                Info->Directory = TRUE;
                Info->NumberOfLinks = 1;
            }
            ReturnLength = sizeof(FILE_STANDARD_INFORMATION);
        }
        else
        {
            Status = STATUS_BUFFER_OVERFLOW;
        }
        break;

    case FileInternalInformation:
        if (BufferLength >= sizeof(FILE_INTERNAL_INFORMATION))
        {
            PFILE_INTERNAL_INFORMATION Info = SystemBuffer;
            Info->IndexNumber.QuadPart = NtfslxQueryFileId(FileContext);
            ReturnLength = sizeof(*Info);
        }
        else
        {
            Status = STATUS_BUFFER_OVERFLOW;
        }
        break;

    case FileEaInformation:
        if (BufferLength >= sizeof(FILE_EA_INFORMATION))
        {
            PFILE_EA_INFORMATION Info = SystemBuffer;
            Info->EaSize = 0;
            ReturnLength = sizeof(*Info);
        }
        else
        {
            Status = STATUS_BUFFER_OVERFLOW;
        }
        break;

    case FileAttributeTagInformation:
        if (BufferLength >= sizeof(FILE_ATTRIBUTE_TAG_INFORMATION))
        {
            PFILE_ATTRIBUTE_TAG_INFORMATION Info = SystemBuffer;
            Info->FileAttributes = RealAttributes;
            Info->ReparseTag = 0;
            ReturnLength = sizeof(*Info);
        }
        else
        {
            Status = STATUS_BUFFER_OVERFLOW;
        }
        break;

    case FileNetworkOpenInformation:
        if (BufferLength >= sizeof(FILE_NETWORK_OPEN_INFORMATION))
        {
            PFILE_NETWORK_OPEN_INFORMATION Info = SystemBuffer;
            RtlZeroMemory(Info, sizeof(*Info));
            if (StdInfo != NULL)
            {
                Info->CreationTime.QuadPart = (LONGLONG)StdInfo->CreationTime;
                Info->LastAccessTime.QuadPart = (LONGLONG)StdInfo->LastAccessTime;
                Info->LastWriteTime.QuadPart = (LONGLONG)StdInfo->LastDataChangeTime;
                Info->ChangeTime.QuadPart = (LONGLONG)StdInfo->LastMftChangeTime;
            }
            else if (FnAttr != NULL)
            {
                Info->CreationTime.QuadPart = (LONGLONG)FnAttr->CreationTime;
                Info->LastAccessTime.QuadPart = (LONGLONG)FnAttr->LastAccessTime;
                Info->LastWriteTime.QuadPart = (LONGLONG)FnAttr->LastDataChangeTime;
                Info->ChangeTime.QuadPart = (LONGLONG)FnAttr->LastMftChangeTime;
            }
            if (FileContext != NULL && !FileContext->IsDirectory)
            {
                Info->AllocationSize.QuadPart = (LONGLONG)FileContext->AllocationSize;
                Info->EndOfFile.QuadPart = (LONGLONG)FileContext->DataSize;
            }
            Info->FileAttributes = RealAttributes;
            ReturnLength = sizeof(*Info);
        }
        else
        {
            Status = STATUS_BUFFER_OVERFLOW;
        }
        break;

    case FileNameInformation:
        Status = NtfslxFillNameInformation(FnAttr, SystemBuffer,
                                           BufferLength, &ReturnLength);
        break;

    case FilePositionInformation:
        if (BufferLength >= sizeof(FILE_POSITION_INFORMATION))
        {
            PFILE_POSITION_INFORMATION Info = SystemBuffer;
            Info->CurrentByteOffset = Stack->FileObject->CurrentByteOffset;
            ReturnLength = sizeof(*Info);
        }
        else
        {
            Status = STATUS_BUFFER_OVERFLOW;
        }
        break;

    case FileAccessInformation:
        if (BufferLength >= sizeof(FILE_ACCESS_INFORMATION))
        {
            PFILE_ACCESS_INFORMATION Info = SystemBuffer;
            Info->AccessFlags = 0;
            ReturnLength = sizeof(*Info);
        }
        else
        {
            Status = STATUS_BUFFER_OVERFLOW;
        }
        break;

    case FileModeInformation:
        if (BufferLength >= sizeof(FILE_MODE_INFORMATION))
        {
            PFILE_MODE_INFORMATION Info = SystemBuffer;
            Info->Mode = 0;
            ReturnLength = sizeof(*Info);
        }
        else
        {
            Status = STATUS_BUFFER_OVERFLOW;
        }
        break;

    case FileAlignmentInformation:
        if (BufferLength >= sizeof(FILE_ALIGNMENT_INFORMATION))
        {
            PFILE_ALIGNMENT_INFORMATION Info = SystemBuffer;
            Info->AlignmentRequirement = FILE_BYTE_ALIGNMENT;
            ReturnLength = sizeof(*Info);
        }
        else
        {
            Status = STATUS_BUFFER_OVERFLOW;
        }
        break;

    case FileAllInformation:
        if (BufferLength >= sizeof(FILE_ALL_INFORMATION))
        {
            PFILE_ALL_INFORMATION Info = SystemBuffer;
            ULONG NameLen = 0;

            if (FileContext != NULL)
            {
                NtfslxFillBasicInformation(FileContext, StdInfo, FnAttr,
                                           RealAttributes, &Info->BasicInformation);
                NtfslxFillStandardInformation(FileContext, Ccb,
                                              &Info->StandardInformation);
            }
            else
            {
                RtlZeroMemory(&Info->BasicInformation,
                              sizeof(Info->BasicInformation));
                Info->BasicInformation.FileAttributes = RealAttributes;
                RtlZeroMemory(&Info->StandardInformation,
                              sizeof(Info->StandardInformation));
                Info->StandardInformation.Directory = TRUE;
                Info->StandardInformation.NumberOfLinks = 1;
            }
            Info->InternalInformation.IndexNumber.QuadPart =
                NtfslxQueryFileId(FileContext);
            Info->EaInformation.EaSize = 0;
            Info->AccessInformation.AccessFlags = 0;
            Info->PositionInformation.CurrentByteOffset =
                Stack->FileObject->CurrentByteOffset;
            Info->ModeInformation.Mode = 0;
            Info->AlignmentInformation.AlignmentRequirement = FILE_BYTE_ALIGNMENT;

            /* NameInformation is the tail. Figure out available space after the
             * fixed-size chunk. */
            {
                ULONG FixedSize = FIELD_OFFSET(FILE_ALL_INFORMATION,
                                               NameInformation)
                                  + FIELD_OFFSET(FILE_NAME_INFORMATION, FileName);
                PFILE_NAME_INFORMATION NameInfo = &Info->NameInformation;
                ULONG TailCapacity = (BufferLength > FixedSize)
                    ? (BufferLength - FixedSize)
                    : 0;
                ULONG NameTotalChars = 1; /* leading backslash */
                ULONG NameCopyChars;
                const WCHAR *SourceName = NULL;
                ULONG SourceChars = 0;

                if (FnAttr != NULL && FnAttr->FileNameLength > 0)
                {
                    SourceName = (const WCHAR *)
                        ((PUCHAR)FnAttr + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE));
                    SourceChars = FnAttr->FileNameLength;
                    NameTotalChars += SourceChars;
                }

                NameInfo->FileNameLength = NameTotalChars * sizeof(WCHAR);

                NameCopyChars = TailCapacity / sizeof(WCHAR);
                if (NameCopyChars >= 1)
                {
                    NameInfo->FileName[0] = L'\\';
                    if (NameCopyChars > 1 && SourceName != NULL)
                    {
                        ULONG Copy = NameCopyChars - 1;
                        if (Copy > SourceChars) Copy = SourceChars;
                        RtlCopyMemory(&NameInfo->FileName[1], SourceName,
                                      Copy * sizeof(WCHAR));
                    }
                }

                NameLen = NameCopyChars * sizeof(WCHAR);
                if (NameLen < NameTotalChars * sizeof(WCHAR))
                {
                    Status = STATUS_BUFFER_OVERFLOW;
                }
                ReturnLength = FixedSize + NameLen;
            }
        }
        else
        {
            Status = STATUS_BUFFER_OVERFLOW;
        }
        break;

    default:
        DbgPrint("ntfslx: QueryFileInformation: unsupported class=%d\n",
                 (int)InfoClass);
        Status = STATUS_INVALID_INFO_CLASS;
        break;
    }

    DbgPrint("ntfslx: QueryFileInformation done path='%wZ' class=%s(%d) status=0x%08lx info=%lu fileId=0x%I64x attrs=0x%08lx eof=%I64u alloc=%I64u dir=%u currentOfs=%I64u\n",
             (PUNICODE_STRING)TracePath,
             NtfslxFileInfoClassName(InfoClass),
             (int)InfoClass,
             Status,
             ReturnLength,
             ValidContext ? NtfslxQueryFileId(FileContext) : NTFSLX_FILE_ROOT,
             RealAttributes,
             (ValidContext && !FileContext->IsDirectory) ? FileContext->DataSize : 0,
             (ValidContext && !FileContext->IsDirectory) ? FileContext->AllocationSize : 0,
             ValidContext ? FileContext->IsDirectory : TRUE,
             Stack->FileObject->CurrentByteOffset.QuadPart);

    return NtfslxCompleteRequest(Irp, Status, ReturnLength);
}

/*
 * Write timestamps / attributes from FILE_BASIC_INFORMATION into the file's
 * $STANDARD_INFORMATION and every non-DOS $FILE_NAME attribute.
 */
static NTSTATUS
NtfslxApplyBasicInfo(
    _Inout_ PNTFSLX_FILE_CONTEXT FileContext,
    _In_ PFILE_BASIC_INFORMATION BasicInfo)
{
    PNTFSLX_DEVICE_EXTENSION DevExt;
    PNTFSLX_MFT_RECORD Record;
    PNTFSLX_ATTR_RECORD StdAttr;
    PNTFSLX_STANDARD_INFORMATION StdInfo;
    PUCHAR Base;
    ULONG Offset;
    NTSTATUS Status;

    DevExt = FileContext->DeviceExtension;
    Record = FileContext->FileRecord;

    Status = NtfslxFindAttribute(Record,
                                 NTFSLX_ATTRIBUTE_STANDARD_INFORMATION,
                                 NULL, 0, &StdAttr);
    if (NT_SUCCESS(Status) && StdAttr->NonResident == 0 &&
        StdAttr->Data.Resident.ValueLength >= sizeof(NTFSLX_STANDARD_INFORMATION))
    {
        StdInfo = (PNTFSLX_STANDARD_INFORMATION)
            ((PUCHAR)StdAttr + StdAttr->Data.Resident.ValueOffset);
        if (BasicInfo->CreationTime.QuadPart > 0)
            StdInfo->CreationTime = (ULONGLONG)BasicInfo->CreationTime.QuadPart;
        if (BasicInfo->LastAccessTime.QuadPart > 0)
            StdInfo->LastAccessTime = (ULONGLONG)BasicInfo->LastAccessTime.QuadPart;
        if (BasicInfo->LastWriteTime.QuadPart > 0)
            StdInfo->LastDataChangeTime = (ULONGLONG)BasicInfo->LastWriteTime.QuadPart;
        if (BasicInfo->ChangeTime.QuadPart > 0)
            StdInfo->LastMftChangeTime = (ULONGLONG)BasicInfo->ChangeTime.QuadPart;
        if (BasicInfo->FileAttributes != 0)
            StdInfo->FileAttributes = BasicInfo->FileAttributes;
    }

    /* Also reflect into every $FILE_NAME attribute */
    Base = (PUCHAR)Record;
    Offset = Record->AttributesOffset;
    while (Offset + sizeof(ULONG) * 2 <= Record->BytesInUse)
    {
        PNTFSLX_ATTR_RECORD Attr = (PNTFSLX_ATTR_RECORD)(Base + Offset);
        if (Attr->Type == NTFSLX_ATTRIBUTE_END)
            break;
        if (Attr->Length == 0 || Offset + Attr->Length > Record->BytesInUse)
            break;

        if (Attr->Type == NTFSLX_ATTRIBUTE_FILE_NAME &&
            Attr->NonResident == 0 &&
            Attr->Data.Resident.ValueLength >= sizeof(NTFSLX_FILE_NAME_ATTRIBUTE))
        {
            PNTFSLX_FILE_NAME_ATTRIBUTE Fn = (PNTFSLX_FILE_NAME_ATTRIBUTE)
                ((PUCHAR)Attr + Attr->Data.Resident.ValueOffset);
            if (Fn->FileNameType != NTFSLX_FILE_NAME_DOS)
            {
                if (BasicInfo->CreationTime.QuadPart > 0)
                    Fn->CreationTime = (ULONGLONG)BasicInfo->CreationTime.QuadPart;
                if (BasicInfo->LastAccessTime.QuadPart > 0)
                    Fn->LastAccessTime = (ULONGLONG)BasicInfo->LastAccessTime.QuadPart;
                if (BasicInfo->LastWriteTime.QuadPart > 0)
                    Fn->LastDataChangeTime = (ULONGLONG)BasicInfo->LastWriteTime.QuadPart;
                if (BasicInfo->ChangeTime.QuadPart > 0)
                    Fn->LastMftChangeTime = (ULONGLONG)BasicInfo->ChangeTime.QuadPart;
                if (BasicInfo->FileAttributes != 0)
                    Fn->FileAttributes = BasicInfo->FileAttributes;
            }
        }

        Offset += Attr->Length;
    }

    Status = NtfslxWriteMftRecord(DevExt->StorageDevice,
                                  &DevExt->VolumeInfo,
                                  DevExt->MftRunlist,
                                  FileContext->MftIndex,
                                  Record);
    return Status;
}

static
NTSTATUS
NtfslxSetInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    PNTFSLX_FILE_CONTEXT FileContext;
    PNTFSLX_CCB Ccb;
    PVOID SystemBuffer;
    ULONG Length;
    NTSTATUS Status;
    FILE_INFORMATION_CLASS InfoClass;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    DeviceExtension = DeviceObject->DeviceExtension;
    SystemBuffer = Irp->AssociatedIrp.SystemBuffer;
    Length = Stack->Parameters.SetFile.Length;
    InfoClass = Stack->Parameters.SetFile.FileInformationClass;

    if (!NtfslxIsVolumeDevice(DeviceExtension) || Stack->FileObject == NULL)
    {
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }

    Status = NtfslxValidateOpenFileObject(Stack->FileObject, &FileContext);
    if (!NT_SUCCESS(Status))
    {
        return NtfslxCompleteRequest(Irp, Status, 0);
    }

    Ccb = (PNTFSLX_CCB)Stack->FileObject->FsContext2;

    DbgPrint("ntfslx: SetInformation class=%d mft=%I64u\n",
             (int)InfoClass, FileContext->MftIndex);

    switch (InfoClass)
    {
    case FileBasicInformation:
        if (Length < sizeof(FILE_BASIC_INFORMATION))
        {
            Status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        Status = NtfslxApplyBasicInfo(FileContext, SystemBuffer);
        if (NT_SUCCESS(Status))
        {
            NtfslxNotifyReportChange(DeviceExtension, FileContext,
                                     FILE_NOTIFY_CHANGE_ATTRIBUTES |
                                         FILE_NOTIFY_CHANGE_LAST_WRITE |
                                         FILE_NOTIFY_CHANGE_LAST_ACCESS |
                                         FILE_NOTIFY_CHANGE_CREATION,
                                     FILE_ACTION_MODIFIED);
        }
        break;

    case FileEndOfFileInformation:
        if (Length < sizeof(FILE_END_OF_FILE_INFORMATION))
        {
            Status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        {
            PFILE_END_OF_FILE_INFORMATION EofInfo = SystemBuffer;
            if (EofInfo->EndOfFile.QuadPart < 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            Status = NtfslxSetFileSize(FileContext,
                                       (ULONGLONG)EofInfo->EndOfFile.QuadPart);
            if (NT_SUCCESS(Status))
            {
                NtfslxNotifyReportChange(DeviceExtension, FileContext,
                                         FILE_NOTIFY_CHANGE_SIZE |
                                             FILE_NOTIFY_CHANGE_LAST_WRITE,
                                         FILE_ACTION_MODIFIED);
            }
        }
        break;

    case FileAllocationInformation:
        if (Length < sizeof(FILE_ALLOCATION_INFORMATION))
        {
            Status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        {
            PFILE_ALLOCATION_INFORMATION AllocInfo = SystemBuffer;
            ULONGLONG NewAlloc;
            if (AllocInfo->AllocationSize.QuadPart < 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                break;
            }
            NewAlloc = (ULONGLONG)AllocInfo->AllocationSize.QuadPart;
            if (NewAlloc >= FileContext->AllocationSize)
            {
                /* Accept as a hint; no action. */
                Status = STATUS_SUCCESS;
            }
            else
            {
                Status = NtfslxSetFileSize(FileContext, NewAlloc);
                if (NT_SUCCESS(Status))
                {
                    NtfslxNotifyReportChange(DeviceExtension, FileContext,
                                             FILE_NOTIFY_CHANGE_SIZE |
                                                 FILE_NOTIFY_CHANGE_LAST_WRITE,
                                             FILE_ACTION_MODIFIED);
                }
            }
        }
        break;

    case FileDispositionInformation:
        if (Length < sizeof(FILE_DISPOSITION_INFORMATION))
        {
            Status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        if (Ccb == NULL)
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
        }
        {
            PFILE_DISPOSITION_INFORMATION DispInfo = SystemBuffer;

            if (DispInfo->DeleteFile)
            {
                Status = NtfslxCheckDeleteAllowed(DeviceExtension,
                                                  FileContext->MftIndex);
                if (!NT_SUCCESS(Status))
                {
                    break;
                }
            }

            Ccb->DeletePending = DispInfo->DeleteFile;
            DbgPrint("ntfslx: SetInformation: DeletePending=%u mft=%I64u\n",
                     Ccb->DeletePending, FileContext->MftIndex);
            Status = STATUS_SUCCESS;
        }
        break;

    case FilePositionInformation:
        if (Length < sizeof(FILE_POSITION_INFORMATION))
        {
            Status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        {
            PFILE_POSITION_INFORMATION PosInfo = SystemBuffer;
            Stack->FileObject->CurrentByteOffset = PosInfo->CurrentByteOffset;
            Status = STATUS_SUCCESS;
        }
        break;

    case FileRenameInformation:
        if (Length < FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName))
        {
            Status = STATUS_INFO_LENGTH_MISMATCH;
            break;
        }
        {
            PFILE_RENAME_INFORMATION RenameInfo = SystemBuffer;
            WCHAR OldName[256];
            WCHAR NewFullBuffer[512];
            ULONG OldNameLen;
            ULONGLONG OldParentMft;
            ULONGLONG NewParentMft;
            ULONGLONG ExistingMft;
            ULONGLONG NewParentReference;
            PNTFSLX_ATTR_RECORD FnAttr;
            PNTFSLX_FILE_NAME_ATTRIBUTE FnValue;
            PNTFSLX_MFT_RECORD NewParentRecord;
            UNICODE_STRING NewFullPath;
            UNICODE_STRING NewParentPath;
            UNICODE_STRING NewLeafName;
            LARGE_INTEGER Now;
            ULONG NewValueLength;
            ULONG NewAttrLength;
            ULONG NotifyFilter;

            NewParentRecord = NULL;
            RtlZeroMemory(&NewFullPath, sizeof(NewFullPath));
            RtlZeroMemory(&NewParentPath, sizeof(NewParentPath));
            RtlZeroMemory(&NewLeafName, sizeof(NewLeafName));

            do
            {
                if (Length < FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) +
                             RenameInfo->FileNameLength)
                {
                    Status = STATUS_INFO_LENGTH_MISMATCH;
                    break;
                }

                Status = NtfslxNormalizeRenameDestinationPath(DeviceExtension,
                                                              FileContext,
                                                              RenameInfo,
                                                              Irp->RequestorMode,
                                                              NewFullBuffer,
                                                              RTL_NUMBER_OF(NewFullBuffer),
                                                              &NewFullPath);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint("ntfslx: Rename: normalize target failed 0x%08lx len=%lu root=%p\n",
                             Status,
                             RenameInfo->FileNameLength,
                             RenameInfo->RootDirectory);
                    break;
                }

                Status = NtfslxSplitFullPath(&NewFullPath,
                                             &NewParentPath,
                                             &NewLeafName);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint("ntfslx: Rename: split target '%wZ' failed 0x%08lx\n",
                             &NewFullPath,
                             Status);
                    break;
                }

                OldParentMft = FileContext->ParentMftIndex;
                if (OldParentMft == NTFSLX_FILE_ROOT &&
                    FileContext->MftIndex != NTFSLX_FILE_ROOT)
                {
                    ULONGLONG RecordParent = NTFSLX_FILE_ROOT;
                    NTSTATUS LookupStatus;

                    LookupStatus = NtfslxLookupParentIndexFromRecord(FileContext->FileRecord,
                                                                     &RecordParent);
                    if (NT_SUCCESS(LookupStatus) &&
                        RecordParent != NTFSLX_FILE_ROOT)
                    {
                        OldParentMft = RecordParent;
                    }
                    else if (!NT_SUCCESS(LookupStatus))
                    {
                        DbgPrint("ntfslx: Rename: LookupParentIndex fallback failed 0x%08lx\n",
                                 LookupStatus);
                        Status = LookupStatus;
                        break;
                    }
                }

                Status = NtfslxResolvePathToMftIndex(DeviceExtension,
                                                     &NewParentPath,
                                                     &NewParentMft);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint("ntfslx: Rename: resolve parent '%wZ' failed 0x%08lx\n",
                             &NewParentPath,
                             Status);
                    break;
                }

                if (FileContext->IsDirectory &&
                    FileContext->FullPath.Buffer != NULL &&
                    RtlPrefixUnicodeString(&FileContext->FullPath,
                                           &NewParentPath,
                                           TRUE) &&
                    (NewParentPath.Length == FileContext->FullPath.Length ||
                     NewParentPath.Buffer[FileContext->FullPath.Length / sizeof(WCHAR)] == L'\\'))
                {
                    DbgPrint("ntfslx: Rename: refusing to move directory '%wZ' into '%wZ'\n",
                             &FileContext->FullPath,
                             &NewParentPath);
                    Status = STATUS_ACCESS_DENIED;
                    break;
                }

                NewParentRecord = ExAllocatePoolWithTag(NonPagedPool,
                                                        DeviceExtension->VolumeInfo.BytesPerFileRecord,
                                                        NTFSLX_TAG);
                if (NewParentRecord == NULL)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    break;
                }

                Status = NtfslxReadMftRecord(DeviceExtension->StorageDevice,
                                             &DeviceExtension->VolumeInfo,
                                             DeviceExtension->MftRunlist,
                                             NewParentMft,
                                             NewParentRecord);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint("ntfslx: Rename: read new parent %I64u failed 0x%08lx\n",
                             NewParentMft,
                             Status);
                    break;
                }

                if ((NewParentRecord->Flags & NTFSLX_MFT_RECORD_IS_DIRECTORY) == 0)
                {
                    Status = STATUS_NOT_A_DIRECTORY;
                    break;
                }

                NewParentReference = MK_MREF(NewParentMft,
                                             NewParentRecord->SequenceNumber);

                Status = NtfslxExtractPrimaryFileName(FileContext->FileRecord,
                                                      OldName,
                                                      &OldNameLen);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint("ntfslx: Rename: ExtractPrimaryFileName failed 0x%08lx\n",
                             Status);
                    break;
                }

                DbgPrint("ntfslx: Rename mft=%I64u oldParent=%I64u newParent=%I64u old='%.*S' new='%wZ'\n",
                         FileContext->MftIndex,
                         OldParentMft,
                         NewParentMft,
                         OldNameLen,
                         OldName,
                         &NewFullPath);

                if (FileContext->FullPath.Buffer != NULL &&
                    FileContext->FullPath.Length == NewFullPath.Length &&
                    RtlCompareMemory(FileContext->FullPath.Buffer,
                                     NewFullPath.Buffer,
                                     NewFullPath.Length) == NewFullPath.Length)
                {
                    DbgPrint("ntfslx: Rename: identical full path, no-op\n");
                    Status = STATUS_SUCCESS;
                    break;
                }

                Status = NtfslxResolvePathToMftIndex(DeviceExtension,
                                                     &NewFullPath,
                                                     &ExistingMft);
                if (NT_SUCCESS(Status))
                {
                    if (ExistingMft != FileContext->MftIndex)
                    {
                        if (!RenameInfo->ReplaceIfExists)
                        {
                            DbgPrint("ntfslx: Rename collision: target '%wZ' mft=%I64u and ReplaceIfExists=FALSE\n",
                                     &NewFullPath,
                                     ExistingMft);
                            Status = STATUS_OBJECT_NAME_COLLISION;
                            break;
                        }

                        {
                            PNTFSLX_MFT_RECORD VictimRecord;

                            VictimRecord = ExAllocatePoolWithTag(NonPagedPool,
                                                                 DeviceExtension->VolumeInfo.BytesPerFileRecord,
                                                                 NTFSLX_TAG);
                            if (VictimRecord == NULL)
                            {
                                Status = STATUS_INSUFFICIENT_RESOURCES;
                                break;
                            }

                            Status = NtfslxReadMftRecord(DeviceExtension->StorageDevice,
                                                         &DeviceExtension->VolumeInfo,
                                                         DeviceExtension->MftRunlist,
                                                         ExistingMft,
                                                         VictimRecord);
                            if (NT_SUCCESS(Status))
                            {
                                ULONGLONG VictimParent;

                                Status = NtfslxLookupParentIndexFromRecord(VictimRecord,
                                                                           &VictimParent);
                                if (NT_SUCCESS(Status))
                                {
                                    Status = NtfslxDeleteFile(DeviceExtension,
                                                              VictimParent,
                                                              ExistingMft,
                                                              NewLeafName.Buffer,
                                                              NewLeafName.Length / sizeof(WCHAR));
                                    DbgPrint("ntfslx: Rename: victim delete mft=%I64u result=0x%08lx\n",
                                             ExistingMft,
                                             Status);
                                }
                            }

                            ExFreePoolWithTag(VictimRecord, NTFSLX_TAG);
                            if (!NT_SUCCESS(Status))
                            {
                                break;
                            }
                        }
                    }
                    else
                    {
                        DbgPrint("ntfslx: Rename: target resolves to the same MFT, continuing as case/update rename\n");
                    }
                }
                else if (Status != STATUS_OBJECT_NAME_NOT_FOUND &&
                         Status != STATUS_OBJECT_PATH_NOT_FOUND)
                {
                    DbgPrint("ntfslx: Rename: collision check failed 0x%08lx\n", Status);
                    break;
                }

                Status = STATUS_SUCCESS;

                Status = NtfslxIndexRemoveFileName(DeviceExtension,
                                                   OldParentMft,
                                                   OldName,
                                                   OldNameLen);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint("ntfslx: Rename: IndexRemove old failed 0x%08lx\n", Status);
                    break;
                }

                Status = NtfslxFindAttribute(FileContext->FileRecord,
                                             NTFSLX_ATTRIBUTE_FILE_NAME,
                                             NULL,
                                             0,
                                             &FnAttr);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint("ntfslx: Rename: FindAttribute($FILE_NAME) failed 0x%08lx\n", Status);
                    break;
                }

                NewValueLength = (ULONG)sizeof(NTFSLX_FILE_NAME_ATTRIBUTE) +
                                 NewLeafName.Length;
                NewAttrLength = ROUND_UP(FnAttr->Data.Resident.ValueOffset +
                                         NewValueLength,
                                         8);

                if (NewAttrLength != FnAttr->Length)
                {
                    Status = NtfslxResizeAttributeRecord(FileContext->FileRecord,
                                                         FnAttr,
                                                         NewAttrLength);
                    if (!NT_SUCCESS(Status))
                    {
                        DbgPrint("ntfslx: Rename: ResizeAttributeRecord to %lu failed 0x%08lx\n",
                                 NewAttrLength,
                                 Status);
                        break;
                    }
                }

                FnValue = (PNTFSLX_FILE_NAME_ATTRIBUTE)
                    ((PUCHAR)FnAttr + FnAttr->Data.Resident.ValueOffset);
                FnAttr->Data.Resident.ValueLength = NewValueLength;
                FnValue->ParentDirectory = NewParentReference;
                FnValue->FileNameLength = (UCHAR)(NewLeafName.Length / sizeof(WCHAR));
                FnValue->FileNameType = NTFSLX_FILE_NAME_WIN32_AND_DOS;
                RtlCopyMemory((PUCHAR)FnValue + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE),
                              NewLeafName.Buffer,
                              NewLeafName.Length);

                Status = NtfslxWriteMftRecord(DeviceExtension->StorageDevice,
                                              &DeviceExtension->VolumeInfo,
                                              DeviceExtension->MftRunlist,
                                              FileContext->MftIndex,
                                              FileContext->FileRecord);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint("ntfslx: Rename: WriteMftRecord failed 0x%08lx\n", Status);
                    break;
                }

                KeQuerySystemTime(&Now);
                Status = NtfslxIndexInsertFileName(DeviceExtension,
                                                  NewParentMft,
                                                  NewLeafName.Buffer,
                                                  NewLeafName.Length / sizeof(WCHAR),
                                                  NTFSLX_FILE_NAME_WIN32_AND_DOS,
                                                  MK_MREF(FileContext->MftIndex,
                                                          FileContext->FileRecord->SequenceNumber),
                                                  FileContext->IsDirectory ? FILE_ATTRIBUTE_DIRECTORY
                                                                           : FILE_ATTRIBUTE_NORMAL,
                                                  FileContext->DataSize,
                                                  FileContext->AllocationSize,
                                                  &Now,
                                                  &Now,
                                                  &Now,
                                                  &Now);
                if (!NT_SUCCESS(Status))
                {
                    DbgPrint("ntfslx: Rename: IndexInsert new failed 0x%08lx\n", Status);
                    break;
                }

                FileContext->ParentMftIndex = NewParentMft;
                DbgPrint("ntfslx: Rename: success mft=%I64u new='%wZ'\n",
                         FileContext->MftIndex,
                         &NewFullPath);

                NotifyFilter = FileContext->IsDirectory
                    ? FILE_NOTIFY_CHANGE_DIR_NAME
                    : FILE_NOTIFY_CHANGE_FILE_NAME;
                NtfslxNotifyReportChange(DeviceExtension,
                                         FileContext,
                                         NotifyFilter,
                                         FILE_ACTION_RENAMED_OLD_NAME);
                (VOID)NtfslxSetFileContextFullPath(FileContext,
                                                   &NewFullPath);
                NtfslxNotifyReportChange(DeviceExtension,
                                         FileContext,
                                         NotifyFilter,
                                         FILE_ACTION_RENAMED_NEW_NAME);
            }
            while (0);

            if (NewParentRecord != NULL)
            {
                ExFreePoolWithTag(NewParentRecord, NTFSLX_TAG);
            }
        }
        break;

    default:
        DbgPrint("ntfslx: SetInformation: unsupported class=%d\n",
                 (int)InfoClass);
        Status = STATUS_NOT_IMPLEMENTED;
        break;
    }

    DbgPrint("ntfslx: SetInformation class=%d result=0x%08lx\n",
             (int)InfoClass, Status);

    return NtfslxCompleteRequest(Irp, Status, 0);
}

static
NTSTATUS
NtfslxShutdownStorageDevice(
    _In_opt_ PDEVICE_OBJECT StorageDevice)
{
    PIRP Irp;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    NTSTATUS Status;

    if (StorageDevice == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_SHUTDOWN,
                                       StorageDevice,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatus);
    if (Irp == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = IoCallDriver(StorageDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    return Status;
}

static
NTSTATUS
NtfslxFlushBuffers(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    PFILE_OBJECT FileObject;
    PNTFSLX_FILE_CONTEXT FileContext;
    NTSTATUS Status;

    Stack = IoGetCurrentIrpStackLocation(Irp);
    FileObject = Stack != NULL ? Stack->FileObject : NULL;
    DeviceExtension = DeviceObject != NULL ? DeviceObject->DeviceExtension : NULL;

    if (DeviceExtension == NULL ||
        DeviceExtension->Signature != NTFSLX_TAG ||
        !NtfslxIsVolumeDevice(DeviceExtension) ||
        FileObject == NULL)
    {
        return NtfslxCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    }

    if (FileObject->FileName.Length == 0)
    {
        DbgPrint("ntfslx: FlushBuffers volume-open fileObj=%p\n", FileObject);
        return NtfslxCompleteRequest(Irp, STATUS_SUCCESS, 0);
    }

    Status = NtfslxValidateOpenFileObject(FileObject, &FileContext);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("ntfslx: FlushBuffers validate failed fileObj=%p status=0x%08lx\n",
                 FileObject,
                 Status);
        return NtfslxCompleteRequest(Irp, Status, 0);
    }

    DbgPrint("ntfslx: FlushBuffers mft=%I64u path='%wZ' data=%I64u alloc=%I64u resident=%u cache=%p\n",
             FileContext->MftIndex,
             NtfslxTraceFileContextPath(FileContext),
             FileContext->DataSize,
             FileContext->AllocationSize,
             FileContext->ResidentData,
             FileContext->CacheContext);

    /*
     * ntfslx writes through to disk synchronously today, so there is no dirty
     * file-system cache state to push here. Report success so user-mode flush
     * callers stop treating the operation as unsupported.
     */
    return NtfslxCompleteRequest(Irp, STATUS_SUCCESS, 0);
}

static
NTSTATUS
NtfslxShutdown(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PNTFSLX_DEVICE_EXTENSION DeviceExtension;
    NTSTATUS Status;
    NTSTATUS StorageStatus;

    DeviceExtension = DeviceObject != NULL ? DeviceObject->DeviceExtension : NULL;
    if (DeviceExtension == NULL ||
        DeviceExtension->Signature != NTFSLX_TAG ||
        !NtfslxIsVolumeDevice(DeviceExtension))
    {
        return NtfslxCompleteRequest(Irp, STATUS_SUCCESS, 0);
    }

    DbgPrint("ntfslx: shutdown begin volume=%p storage=%p flags=0x%04x\n",
             DeviceObject,
             DeviceExtension->StorageDevice,
             DeviceExtension->VolumeInfo.Flags);

    Status = NtfslxSetVolumeDirtyFlag(DeviceExtension->StorageDevice,
                                      &DeviceExtension->VolumeInfo,
                                      DeviceExtension->MftRunlist,
                                      FALSE);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("ntfslx: shutdown: clear dirty flag failed 0x%08lx\n",
                 Status);
    }
    else
    {
        DbgPrint("ntfslx: shutdown: volume marked clean flags=0x%04x\n",
                 DeviceExtension->VolumeInfo.Flags);
    }

    StorageStatus = NtfslxShutdownStorageDevice(DeviceExtension->StorageDevice);
    if (!NT_SUCCESS(StorageStatus))
    {
        DbgPrint("ntfslx: shutdown: storage shutdown failed 0x%08lx\n",
                 StorageStatus);
        if (NT_SUCCESS(Status))
        {
            Status = StorageStatus;
        }
    }

    return NtfslxCompleteRequest(Irp, Status, 0);
}

NTSTATUS
NTAPI
NtfslxFsdDispatch(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    NTSTATUS Status;

    FsRtlEnterFileSystem();

    Stack = IoGetCurrentIrpStackLocation(Irp);
    switch (Stack->MajorFunction)
    {
        case IRP_MJ_CREATE:
            Status = NtfslxCreate(DeviceObject, Irp);
            break;

        case IRP_MJ_CLOSE:
            Status = NtfslxClose(DeviceObject, Irp);
            break;

        case IRP_MJ_CLEANUP:
            Status = NtfslxCleanup(DeviceObject, Irp);
            break;

        case IRP_MJ_READ:
            Status = NtfslxRead(DeviceObject, Irp);
            break;

        case IRP_MJ_WRITE:
            Status = NtfslxWrite(DeviceObject, Irp);
            break;

        case IRP_MJ_FLUSH_BUFFERS:
            Status = NtfslxFlushBuffers(DeviceObject, Irp);
            break;

        case IRP_MJ_DIRECTORY_CONTROL:
            Status = NtfslxQueryDirectory(DeviceObject, Irp);
            break;

        case IRP_MJ_FILE_SYSTEM_CONTROL:
            Status = NtfslxFileSystemControl(DeviceObject, Irp);
            break;

        case IRP_MJ_DEVICE_CONTROL:
            Status = NtfslxDeviceControl(DeviceObject, Irp);
            break;

        case IRP_MJ_QUERY_VOLUME_INFORMATION:
            Status = NtfslxQueryVolumeInformation(DeviceObject, Irp);
            break;

        case IRP_MJ_QUERY_INFORMATION:
            Status = NtfslxQueryFileInformation(DeviceObject, Irp);
            break;

        case IRP_MJ_SET_INFORMATION:
            Status = NtfslxSetInformation(DeviceObject, Irp);
            break;

        case IRP_MJ_SHUTDOWN:
            Status = NtfslxShutdown(DeviceObject, Irp);
            break;

        default:
            Status = NtfslxCompleteRequest(Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
            break;
    }

    FsRtlExitFileSystem();
    return Status;
}
