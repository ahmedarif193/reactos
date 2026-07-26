/*
 * PROJECT:     ReactOS NTFS library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     C interface for MFT class
 * COPYRIGHT:   Copyright 2026 Carl Bialorucki <carl.bialorucki@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

#ifdef __cplusplus
extern "C" {
#endif

NTSTATUS
NtfsMasterFileTableGetFileRecordFromQuery(
    _In_ PNtfsMasterFileTable Mft,
    _In_ PWCHAR Query,
    _Out_ PNtfsFileRecord* File)
{
    return reinterpret_cast<PMasterFileTable>(Mft)->GetFileRecordFromQuery(Query, reinterpret_cast<PFileRecord*>(File));
}

NTSTATUS
NtfsMasterFileTableGetFileRecordFromQueryEx(
    _In_ PNtfsMasterFileTable Mft,
    _In_reads_(QueryLength) PWCHAR Query,
    _In_ ULONG QueryLength,
    _In_ BOOLEAN OpenFinalReparsePoint,
    _Out_ PULONG RemainingNameLength,
    _Out_ PNtfsFileRecord* File)
{
    PWCHAR TerminatedQuery;
    NTSTATUS Status;

    if (!Mft || !Query ||
        !RemainingNameLength || !File)
        return STATUS_INVALID_PARAMETER;
    *RemainingNameLength = 0;
    *File = NULL;
    if (QueryLength >
        MAXUSHORT / sizeof(WCHAR))
    {
        return STATUS_NAME_TOO_LONG;
    }

    TerminatedQuery =
        new(PagedPool, TAG_NTFS)
            WCHAR[(SIZE_T)QueryLength + 1];
    if (!TerminatedQuery)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(TerminatedQuery,
                  Query,
                  QueryLength * sizeof(WCHAR));
    TerminatedQuery[QueryLength] = 0;

    Status = reinterpret_cast<PMasterFileTable>(Mft)->
        GetFileRecordFromQuery(
            TerminatedQuery,
            reinterpret_cast<PFileRecord*>(File),
            TRUE,
            OpenFinalReparsePoint,
            RemainingNameLength);
    delete[] TerminatedQuery;
    return Status;
}

NTSTATUS
NtfsMasterFileTableReadFileRecord(
    _In_ PNtfsMasterFileTable Mft,
    _In_ ULONGLONG RequestedFileReference,
    _Out_ PULONGLONG ReturnedFileReference,
    _Out_opt_ PUCHAR Buffer,
    _Inout_ PULONG BufferLength)
{
    if (!Mft)
        return STATUS_INVALID_PARAMETER;
    return reinterpret_cast<PMasterFileTable>(Mft)->
        ReadFileRecord(RequestedFileReference,
                       ReturnedFileReference,
                       Buffer,
                       BufferLength);
}

static NTSTATUS
NtfsCopyTerminatedQuery(
    _In_reads_(QueryLength) PWCHAR Query,
    _In_ ULONG QueryLength,
    _Outptr_ PWCHAR* TerminatedQuery)
{
    *TerminatedQuery = NULL;
    if (!Query)
        return STATUS_INVALID_PARAMETER;
    if (QueryLength > MAXUSHORT / sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;

    *TerminatedQuery =
        new(PagedPool, TAG_NTFS)
            WCHAR[(SIZE_T)QueryLength + 1];
    if (!*TerminatedQuery)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlCopyMemory(*TerminatedQuery,
                  Query,
                  QueryLength * sizeof(WCHAR));
    (*TerminatedQuery)[QueryLength] = 0;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfsMasterFileTableCreateFile(
    _In_ PNtfsMasterFileTable Mft,
    _In_reads_(QueryLength) PWCHAR Query,
    _In_ ULONG QueryLength,
    _In_ BOOLEAN IsDirectory,
    _In_ ULONG FileAttributes,
    _Out_ PNtfsFileRecord* File)
{
    PWCHAR TerminatedQuery;
    NTSTATUS Status;

    if (!Mft || !File)
        return STATUS_INVALID_PARAMETER;
    *File = NULL;
    Status = NtfsCopyTerminatedQuery(
        Query,
        QueryLength,
        &TerminatedQuery);
    if (!NT_SUCCESS(Status))
        return Status;

    Status =
        reinterpret_cast<PMasterFileTable>(Mft)->
            CreateFile(
                TerminatedQuery,
                IsDirectory,
                FileAttributes,
                reinterpret_cast<PFileRecord*>(File));
    delete[] TerminatedQuery;
    return Status;
}

NTSTATUS
NtfsMasterFileTableDeleteFile(
    _In_ PNtfsMasterFileTable Mft,
    _In_reads_(QueryLength) PWCHAR Query,
    _In_ ULONG QueryLength,
    _In_ BOOLEAN RemoveDirectory)
{
    PWCHAR TerminatedQuery;
    NTSTATUS Status;

    if (!Mft)
        return STATUS_INVALID_PARAMETER;
    Status = NtfsCopyTerminatedQuery(
        Query,
        QueryLength,
        &TerminatedQuery);
    if (!NT_SUCCESS(Status))
        return Status;

    Status =
        reinterpret_cast<PMasterFileTable>(Mft)->
            DeleteFile(
                TerminatedQuery,
                RemoveDirectory);
    delete[] TerminatedQuery;
    return Status;
}

NTSTATUS
NtfsMasterFileTableRenameFile(
    _In_ PNtfsMasterFileTable Mft,
    _In_reads_(OldQueryLength) PWCHAR OldQuery,
    _In_ ULONG OldQueryLength,
    _In_reads_(NewQueryLength) PWCHAR NewQuery,
    _In_ ULONG NewQueryLength)
{
    PWCHAR TerminatedOld = NULL;
    PWCHAR TerminatedNew = NULL;
    NTSTATUS Status;

    if (!Mft)
        return STATUS_INVALID_PARAMETER;
    Status = NtfsCopyTerminatedQuery(
        OldQuery,
        OldQueryLength,
        &TerminatedOld);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsCopyTerminatedQuery(
            NewQuery,
            NewQueryLength,
            &TerminatedNew);
    }
    if (NT_SUCCESS(Status))
    {
        Status =
            reinterpret_cast<PMasterFileTable>(
                Mft)->
                RenameFile(
                    TerminatedOld,
                    TerminatedNew);
    }
    delete[] TerminatedNew;
    delete[] TerminatedOld;
    return Status;
}

NTSTATUS
NtfsMasterFileTableCreateHardLink(
    _In_ PNtfsMasterFileTable Mft,
    _In_reads_(ExistingQueryLength)
        PWCHAR ExistingQuery,
    _In_ ULONG ExistingQueryLength,
    _In_reads_(NewQueryLength) PWCHAR NewQuery,
    _In_ ULONG NewQueryLength)
{
    PWCHAR TerminatedExisting = NULL;
    PWCHAR TerminatedNew = NULL;
    NTSTATUS Status;

    if (!Mft)
        return STATUS_INVALID_PARAMETER;
    Status = NtfsCopyTerminatedQuery(
        ExistingQuery,
        ExistingQueryLength,
        &TerminatedExisting);
    if (NT_SUCCESS(Status))
    {
        Status = NtfsCopyTerminatedQuery(
            NewQuery,
            NewQueryLength,
            &TerminatedNew);
    }
    if (NT_SUCCESS(Status))
    {
        Status =
            reinterpret_cast<PMasterFileTable>(
                Mft)->
                CreateHardLink(
                    TerminatedExisting,
                    TerminatedNew);
    }
    delete[] TerminatedNew;
    delete[] TerminatedExisting;
    return Status;
}

#ifdef __cplusplus
}
#endif
