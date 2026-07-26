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
    _Inout_ PFILE_BOTH_DIR_INFORMATION Buffer,
    _Inout_ PULONG BufferLength)
{
    return reinterpret_cast<PDirectory>(Dir)->GetFileBothDirInfo(ReturnSingleEntry,
                                                                 RestartScan,
                                                                 FileNameFilter,
                                                                 Buffer,
                                                                 BufferLength);
}

NTSTATUS
NtfsDirectoryLoadDirectory(
    _In_ PNtfsDirectory Dir,
    _In_ PNtfsFileRecord File)
{
    return reinterpret_cast<PDirectory>(Dir)->LoadDirectory(reinterpret_cast<PFileRecord>(File));
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
