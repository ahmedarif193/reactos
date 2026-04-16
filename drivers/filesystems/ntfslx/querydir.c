/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Query-directory adapter for staged ntfslx directory enumeration
 *
 * Integration note:
 * - This module is self-contained; wire it into the build and call sites when
 *   the surrounding ntfslx integration is ready.
 */

#include "ntfslx.h"

typedef struct _NTFSLX_QUERY_DIRECTORY_REQUEST
{
    FILE_INFORMATION_CLASS FileInformationClass;
    PVOID Buffer;
    ULONG BufferLength;
    PCUNICODE_STRING SearchPattern;
    ULONG StartIndex;
    BOOLEAN FirstQuery;
    BOOLEAN RestartScan;
    BOOLEAN IndexSpecified;
    BOOLEAN ReturnSingleEntry;
    BOOLEAN CaseSensitive;
} NTFSLX_QUERY_DIRECTORY_REQUEST, *PNTFSLX_QUERY_DIRECTORY_REQUEST;

typedef struct _NTFSLX_QUERY_DIRECTORY_CONTEXT
{
    FILE_INFORMATION_CLASS FileInformationClass;
    PVOID Buffer;
    ULONG BufferLength;
    ULONG BytesWritten;
    ULONG StartIndex;
    ULONG CurrentIndex;
    ULONG NextIndex;
    BOOLEAN FirstQuery;
    BOOLEAN ReturnSingleEntry;
    BOOLEAN CaseSensitive;
    BOOLEAN AnyWritten;
    BOOLEAN Overflowed;
    UNICODE_STRING WildcardPattern;
    PCUNICODE_STRING SearchPattern;
    PULONG LastNextEntryOffset;
} NTFSLX_QUERY_DIRECTORY_CONTEXT, *PNTFSLX_QUERY_DIRECTORY_CONTEXT;

typedef NTSTATUS (NTAPI *PNTFSLX_QUERY_DIRECTORY_ENUMERATOR)(
    _In_ PNTFSLX_DIR_ENUM_CALLBACK Callback,
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PVOID EnumeratorContext);

typedef struct _NTFSLX_QUERY_DIRECTORY_MFT_ENUMERATOR_CONTEXT
{
    const VOID *DirectoryMftRecord;
    ULONG DirectoryRecordLength;
} NTFSLX_QUERY_DIRECTORY_MFT_ENUMERATOR_CONTEXT,
  *PNTFSLX_QUERY_DIRECTORY_MFT_ENUMERATOR_CONTEXT;

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

NTSTATUS
NtfslxEnumerateDirectoryFromMftRecord(
    _In_ const VOID *DirectoryMftRecord,
    _In_ ULONG DirectoryRecordLength,
    _In_ PNTFSLX_DIR_ENUM_CALLBACK Callback,
    _In_opt_ PVOID Context);

static
BOOLEAN
NtfslxQueryDirectoryIsSupportedClass(
    _In_ FILE_INFORMATION_CLASS FileInformationClass)
{
    switch (FileInformationClass)
    {
        case FileDirectoryInformation:
        case FileFullDirectoryInformation:
        case FileIdFullDirectoryInformation:
        case FileBothDirectoryInformation:
        case FileIdBothDirectoryInformation:
        case FileNamesInformation:
            return TRUE;

        default:
            return FALSE;
    }
}

static
ULONG
NtfslxQueryDirectoryGetHeaderSize(
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
VOID
NtfslxQueryDirectoryFillClassSpecificTail(
    _In_ FILE_INFORMATION_CLASS FileInformationClass,
    _In_ PVOID Buffer,
    _In_ const NTFSLX_DIR_ENTRY *Entry)
{
    switch (FileInformationClass)
    {
        case FileIdFullDirectoryInformation:
            ((PFILE_ID_FULL_DIR_INFORMATION)Buffer)->FileId.QuadPart = Entry->FileReference;
            break;

        case FileIdBothDirectoryInformation:
            ((PFILE_ID_BOTH_DIR_INFORMATION)Buffer)->FileId.QuadPart = Entry->FileReference;
            break;

        default:
            break;
    }
}

static
BOOLEAN
NtfslxQueryDirectoryMatchName(
    _In_ PNTFSLX_QUERY_DIRECTORY_CONTEXT Context,
    _In_ const NTFSLX_DIR_ENTRY *Entry)
{
    UNICODE_STRING EntryName;
    BOOLEAN Match;

    if (Context->SearchPattern == NULL)
    {
        return TRUE;
    }

    EntryName.Buffer = (PWCHAR)Entry->FileName;
    EntryName.Length = Entry->FileNameLength * sizeof(WCHAR);
    EntryName.MaximumLength = EntryName.Length;

    __try
    {
        Match = FsRtlIsNameInExpression((PUNICODE_STRING)Context->SearchPattern,
                                        &EntryName,
                                        (BOOLEAN)!Context->CaseSensitive,
                                        NULL);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        Match = FALSE;
    }

    NTFSDBG("ntfslx: dirmatch pat='%wZ' name='%.*S' caseSens=%u match=%u\n",
             (PUNICODE_STRING)Context->SearchPattern,
             (int)(EntryName.Length / sizeof(WCHAR)),
             EntryName.Buffer,
             Context->CaseSensitive, Match);

    return Match;
}

static
NTSTATUS
NTAPI
NtfslxQueryDirectoryEnumerateFromMftRecordSource(
    _In_ PNTFSLX_DIR_ENUM_CALLBACK Callback,
    _In_opt_ PVOID CallbackContext,
    _In_opt_ PVOID EnumeratorContext)
{
    PNTFSLX_QUERY_DIRECTORY_MFT_ENUMERATOR_CONTEXT SourceContext;

    if (Callback == NULL || EnumeratorContext == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    SourceContext = EnumeratorContext;
    return NtfslxEnumerateDirectoryFromMftRecord(SourceContext->DirectoryMftRecord,
                                                 SourceContext->DirectoryRecordLength,
                                                 Callback,
                                                 CallbackContext);
}

NTSTATUS
NtfslxQueryDirectoryEnumerateWithEnumerator(
    _In_ PNTFSLX_QUERY_DIRECTORY_ENUMERATOR Enumerator,
    _In_opt_ PVOID EnumeratorContext,
    _In_ const NTFSLX_QUERY_DIRECTORY_REQUEST *Request,
    _Out_opt_ PULONG BytesWritten,
    _Out_opt_ PULONG NextIndex);

static
NTSTATUS
NtfslxQueryDirectoryPrepareContext(
    _Out_ PNTFSLX_QUERY_DIRECTORY_CONTEXT Context,
    _In_ const NTFSLX_QUERY_DIRECTORY_REQUEST *Request)
{
    if (Context == NULL || Request == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!NtfslxQueryDirectoryIsSupportedClass(Request->FileInformationClass))
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    if (Request->Buffer == NULL && Request->BufferLength != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Request->SearchPattern != NULL &&
        Request->SearchPattern->Length != 0 &&
        Request->SearchPattern->Buffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Context, sizeof(*Context));
    Context->FileInformationClass = Request->FileInformationClass;
    Context->Buffer = Request->Buffer;
    Context->BufferLength = Request->BufferLength;
    Context->FirstQuery = Request->FirstQuery;
    Context->ReturnSingleEntry = Request->ReturnSingleEntry;
    Context->CaseSensitive = Request->CaseSensitive;

    RtlInitUnicodeString(&Context->WildcardPattern, L"*");
    if (Request->SearchPattern != NULL && Request->SearchPattern->Length != 0)
    {
        Context->SearchPattern = Request->SearchPattern;
    }
    else
    {
        Context->SearchPattern = &Context->WildcardPattern;
    }

    if (Request->IndexSpecified)
    {
        Context->StartIndex = Request->StartIndex;
    }
    else if (Request->RestartScan || Request->FirstQuery)
    {
        Context->StartIndex = 0;
    }
    else
    {
        Context->StartIndex = Request->StartIndex;
    }

    Context->NextIndex = Context->StartIndex;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxQueryDirectoryWriteCommonLayout(
    _Inout_ PNTFSLX_QUERY_DIRECTORY_CONTEXT Context,
    _In_ const NTFSLX_DIR_ENTRY *Entry)
{
    ULONG HeaderSize;
    ULONG NameBytes;
    ULONG TotalSize;
    ULONG AlignedSize;
    ULONG Remaining;
    ULONG CopyBytes;
    PVOID CurrentBuffer;
    PNTFSLX_QUERY_DIRECTORY_COMMON_HEADER Common;

    HeaderSize = NtfslxQueryDirectoryGetHeaderSize(Context->FileInformationClass);
    if (HeaderSize == 0)
    {
        return STATUS_INVALID_INFO_CLASS;
    }

    NameBytes = Entry->FileNameLength * sizeof(WCHAR);
    TotalSize = HeaderSize + NameBytes;
    AlignedSize = ROUND_UP(TotalSize, sizeof(ULONGLONG));
    Remaining = Context->BufferLength - Context->BytesWritten;
    CurrentBuffer = (PUCHAR)Context->Buffer + Context->BytesWritten;

    if (Remaining < HeaderSize)
    {
        if (Context->AnyWritten)
        {
            return STATUS_NO_MORE_FILES;
        }

        Context->Overflowed = TRUE;
        return STATUS_BUFFER_OVERFLOW;
    }

    if (Remaining < TotalSize)
    {
        if (Context->AnyWritten)
        {
            return STATUS_NO_MORE_FILES;
        }

        CopyBytes = Remaining - HeaderSize;
        RtlZeroMemory(CurrentBuffer, HeaderSize + CopyBytes);
        Common = (PNTFSLX_QUERY_DIRECTORY_COMMON_HEADER)CurrentBuffer;
        NtfslxQueryDirectoryFillCommonHeader(Common, Entry, Context->CurrentIndex);

        NtfslxQueryDirectoryFillClassSpecificTail(Context->FileInformationClass,
                                                  CurrentBuffer,
                                                  Entry);

        if (CopyBytes != 0)
        {
            RtlCopyMemory((PUCHAR)CurrentBuffer + HeaderSize,
                          Entry->FileName,
                          CopyBytes);
        }

        Common->NextEntryOffset = 0;
        Context->LastNextEntryOffset = &Common->NextEntryOffset;
        Context->AnyWritten = TRUE;
        Context->Overflowed = TRUE;
        Context->BytesWritten += HeaderSize + CopyBytes;
        Context->NextIndex = Context->CurrentIndex + 1;
        Context->CurrentIndex++;
        return STATUS_BUFFER_OVERFLOW;
    }

    RtlZeroMemory(CurrentBuffer, TotalSize);
    Common = (PNTFSLX_QUERY_DIRECTORY_COMMON_HEADER)CurrentBuffer;
    NtfslxQueryDirectoryFillCommonHeader(Common, Entry, Context->CurrentIndex);

    NtfslxQueryDirectoryFillClassSpecificTail(Context->FileInformationClass,
                                              CurrentBuffer,
                                              Entry);

    RtlCopyMemory((PUCHAR)CurrentBuffer + HeaderSize,
                  Entry->FileName,
                  NameBytes);

    Context->LastNextEntryOffset = &Common->NextEntryOffset;
    Context->AnyWritten = TRUE;
    Context->NextIndex = Context->CurrentIndex + 1;
    Context->CurrentIndex++;

    if (Context->ReturnSingleEntry || Remaining < AlignedSize)
    {
        Common->NextEntryOffset = 0;
        Context->BytesWritten += TotalSize;
        return STATUS_NO_MORE_FILES;
    }

    Common->NextEntryOffset = AlignedSize;
    Context->BytesWritten += AlignedSize;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NTAPI
NtfslxQueryDirectoryVisitEntry(
    _In_ const NTFSLX_DIR_ENTRY *Entry,
    _In_opt_ PVOID Context)
{
    PNTFSLX_QUERY_DIRECTORY_CONTEXT QueryContext;

    QueryContext = Context;
    if (QueryContext == NULL || Entry == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (!NtfslxQueryDirectoryMatchName(QueryContext, Entry))
    {
        return STATUS_SUCCESS;
    }

    if (QueryContext->CurrentIndex < QueryContext->StartIndex)
    {
        QueryContext->CurrentIndex++;
        QueryContext->NextIndex = QueryContext->CurrentIndex;
        return STATUS_SUCCESS;
    }

    return NtfslxQueryDirectoryWriteCommonLayout(QueryContext, Entry);
}

static
NTSTATUS
NtfslxQueryDirectoryFinalize(
    _Inout_ PNTFSLX_QUERY_DIRECTORY_CONTEXT Context,
    _Out_opt_ PULONG BytesWritten,
    _Out_opt_ PULONG NextIndex)
{
    if (Context == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Context->AnyWritten && Context->LastNextEntryOffset != NULL)
    {
        *Context->LastNextEntryOffset = 0;
    }

    if (BytesWritten != NULL)
    {
        *BytesWritten = Context->BytesWritten;
    }

    if (NextIndex != NULL)
    {
        *NextIndex = Context->NextIndex;
    }

    if (Context->Overflowed)
    {
        return STATUS_BUFFER_OVERFLOW;
    }

    if (Context->AnyWritten)
    {
        return STATUS_SUCCESS;
    }

    return Context->FirstQuery ? STATUS_NO_SUCH_FILE : STATUS_NO_MORE_FILES;
}

NTSTATUS
NtfslxQueryDirectoryEnumerateWithEnumerator(
    _In_ PNTFSLX_QUERY_DIRECTORY_ENUMERATOR Enumerator,
    _In_opt_ PVOID EnumeratorContext,
    _In_ const NTFSLX_QUERY_DIRECTORY_REQUEST *Request,
    _Out_opt_ PULONG BytesWritten,
    _Out_opt_ PULONG NextIndex)
{
    NTFSLX_QUERY_DIRECTORY_CONTEXT Context;
    NTSTATUS Status;

    if (Enumerator == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtfslxQueryDirectoryPrepareContext(&Context, Request);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = Enumerator(NtfslxQueryDirectoryVisitEntry, &Context, EnumeratorContext);
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW)
    {
        return Status;
    }

    return NtfslxQueryDirectoryFinalize(&Context, BytesWritten, NextIndex);
}

NTSTATUS
NtfslxQueryDirectoryEnumerateFromMftRecord(
    _In_ const VOID *DirectoryMftRecord,
    _In_ ULONG DirectoryRecordLength,
    _In_ const NTFSLX_QUERY_DIRECTORY_REQUEST *Request,
    _Out_opt_ PULONG BytesWritten,
    _Out_opt_ PULONG NextIndex)
{
    NTFSLX_QUERY_DIRECTORY_MFT_ENUMERATOR_CONTEXT SourceContext;

    SourceContext.DirectoryMftRecord = DirectoryMftRecord;
    SourceContext.DirectoryRecordLength = DirectoryRecordLength;
    return NtfslxQueryDirectoryEnumerateWithEnumerator(
        NtfslxQueryDirectoryEnumerateFromMftRecordSource,
        &SourceContext,
        Request,
        BytesWritten,
        NextIndex);
}

NTSTATUS
NtfslxQueryDirectoryEnumerateFromDirectoryEntry(
    _In_ const NTFSLX_DIR_ENTRY *Entry,
    _In_ const NTFSLX_QUERY_DIRECTORY_REQUEST *Request,
    _Out_opt_ PULONG BytesWritten,
    _Out_opt_ PULONG NextIndex)
{
    NTFSLX_QUERY_DIRECTORY_CONTEXT Context;
    NTSTATUS Status;

    Status = NtfslxQueryDirectoryPrepareContext(&Context, Request);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Entry == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtfslxQueryDirectoryVisitEntry(Entry, &Context);
    if (!NT_SUCCESS(Status) && Status != STATUS_BUFFER_OVERFLOW && Status != STATUS_NO_MORE_FILES)
    {
        return Status;
    }

    return NtfslxQueryDirectoryFinalize(&Context, BytesWritten, NextIndex);
}
