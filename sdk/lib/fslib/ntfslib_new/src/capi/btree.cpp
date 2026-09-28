/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     C interface for BTree class and related classes
 * COPYRIGHT:   Copyright 2026 Carl Bialorucki <carl.bialorucki@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

PNtfsDirectory
NtfsDirectoryCreate(
    _In_ PNtfsVolume DiskVolume)
{
    return reinterpret_cast<PNtfsDirectory>(new(NonPagedPool) Directory(reinterpret_cast<PVolume>(DiskVolume)));
}

void
NtfsDirectoryDestroy(
    _In_opt_ PNtfsDirectory Dir)
{
    delete reinterpret_cast<PDirectory>(Dir);
}

NTSTATUS
NtfsDirectoryGetFileBothDirInfo(
    _In_    PNtfsDirectory Dir,
    _In_    BOOLEAN ReturnSingleEntry,
    _In_    BOOLEAN RestartScan,
    _In_    PUNICODE_STRING FileNameFilter,
    _In_    FILE_INFORMATION_CLASS InformationClass,
    _Inout_ PVOID Buffer,
    _Inout_ PULONG BufferLength)
{
    return reinterpret_cast<PDirectory>(Dir)->GetFileBothDirInfo(ReturnSingleEntry,
                                                                 RestartScan,
                                                                 FileNameFilter,
                                                                 InformationClass,
                                                                 Buffer,
                                                                 BufferLength);
}

NTSTATUS
NtfsDirectoryStoreInfo(
    _In_ FILE_INFORMATION_CLASS InformationClass,
    _Inout_ PFILE_ID_BOTH_DIR_INFORMATION Info,
    _In_reads_bytes_(NameLength) PCWSTR Name,
    _In_ ULONG NameLength,
    _Out_writes_bytes_(*BufferLength) PVOID Buffer,
    _Inout_ PULONG BufferLength,
    _Out_ PULONG EntrySize)
{
    PUCHAR Entry = static_cast<PUCHAR>(Buffer);
    ULONG NameOffset;

    switch (InformationClass)
    {
        case FileDirectoryInformation:
            NameOffset = FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName);
            break;
        case FileFullDirectoryInformation:
            NameOffset = FIELD_OFFSET(FILE_FULL_DIR_INFORMATION, FileName);
            break;
        case FileBothDirectoryInformation:
            NameOffset = FIELD_OFFSET(FILE_BOTH_DIR_INFORMATION, FileName);
            break;
        case FileNamesInformation:
            NameOffset = FIELD_OFFSET(FILE_NAMES_INFORMATION, FileName);
            break;
        case FileIdBothDirectoryInformation:
            NameOffset = FIELD_OFFSET(FILE_ID_BOTH_DIR_INFORMATION, FileName);
            break;
        case FileIdFullDirectoryInformation:
            NameOffset = FIELD_OFFSET(FILE_ID_FULL_DIR_INFORMATION, FileName);
            break;
        default:
            return STATUS_INVALID_INFO_CLASS;
    }

    *EntrySize = ALIGN_UP_BY(NameOffset + NameLength, sizeof(ULONGLONG));
    if (*BufferLength < *EntrySize)
        return STATUS_BUFFER_OVERFLOW;

    Info->NextEntryOffset = *EntrySize;
    Info->FileNameLength = NameLength;
    RtlZeroMemory(Entry, *EntrySize);

    if (InformationClass == FileNamesInformation)
    {
        PFILE_NAMES_INFORMATION Names = reinterpret_cast<PFILE_NAMES_INFORMATION>(Entry);

        Names->NextEntryOffset = Info->NextEntryOffset;
        Names->FileIndex = Info->FileIndex;
        Names->FileNameLength = NameLength;
    }
    else
    {
        RtlCopyMemory(Entry, Info, FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName));
        if (InformationClass != FileDirectoryInformation)
            reinterpret_cast<PFILE_FULL_DIR_INFORMATION>(Entry)->EaSize = Info->EaSize;
        if (InformationClass == FileBothDirectoryInformation ||
            InformationClass == FileIdBothDirectoryInformation)
        {
            reinterpret_cast<PFILE_BOTH_DIR_INFORMATION>(Entry)->ShortNameLength = Info->ShortNameLength;
            RtlCopyMemory(reinterpret_cast<PFILE_BOTH_DIR_INFORMATION>(Entry)->ShortName,
                          Info->ShortName,
                          sizeof(Info->ShortName));
        }
        if (InformationClass == FileIdBothDirectoryInformation)
            reinterpret_cast<PFILE_ID_BOTH_DIR_INFORMATION>(Entry)->FileId = Info->FileId;
        if (InformationClass == FileIdFullDirectoryInformation)
            reinterpret_cast<PFILE_ID_FULL_DIR_INFORMATION>(Entry)->FileId = Info->FileId;
    }

    RtlCopyMemory(Entry + NameOffset, Name, NameLength);
    *BufferLength -= *EntrySize;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfsDirectoryLoadDirectory(
    _In_ PNtfsDirectory Dir,
    _In_ PNtfsFileRecord File)
{
    return reinterpret_cast<PDirectory>(Dir)->LoadDirectory(reinterpret_cast<PFileRecord>(File));
}

NTSTATUS
NtfsDirectoryLoadForEnumeration(
    _In_ PNtfsDirectory Dir,
    _In_ PNtfsFileRecord File)
{
    if (!Dir || !File)
        return STATUS_INVALID_PARAMETER;

    return reinterpret_cast<PDirectory>(Dir)->LoadDirectoryForEnumeration(
        reinterpret_cast<PFileRecord>(File));
}

NTSTATUS
NtfsDirectoryReadNext(
    _In_ PNtfsDirectory Dir,
    _In_ BOOLEAN RestartScan,
    _Out_ PNtfsDirectoryEntry Entry)
{
    if (!Dir || !Entry)
        return STATUS_INVALID_PARAMETER;

    return reinterpret_cast<PDirectory>(Dir)->GetNextEntry(RestartScan, Entry);
}

#ifdef __cplusplus
}
#endif
