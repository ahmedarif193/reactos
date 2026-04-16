/*
 * Directory name lookup for the ntfslx ReactOS driver.
 *
 * Looks up a single filename in a directory's $INDEX_ROOT and
 * $INDEX_ALLOCATION, returning the MFT reference on match.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

/*
 * Callback context for the lookup enumeration.
 */
typedef struct _NTFSLX_LOOKUP_CONTEXT
{
    const WCHAR *Name;
    ULONG NameLength;
    BOOLEAN IgnoreCase;
    const USHORT *UpcaseTable;
    ULONG UpcaseLength;
    ULONGLONG FoundReference;
    BOOLEAN Found;
} NTFSLX_LOOKUP_CONTEXT, *PNTFSLX_LOOKUP_CONTEXT;

/*
 * Enumeration callback: compare each directory entry name against the target.
 */
static NTSTATUS
NtfslxLookupCallback(
    _In_ const NTFSLX_DIR_ENTRY *Entry,
    _In_opt_ PVOID Context)
{
    PNTFSLX_LOOKUP_CONTEXT Ctx = (PNTFSLX_LOOKUP_CONTEXT)Context;

    if (Entry->FileNameLength != Ctx->NameLength)
        return STATUS_SUCCESS; /* continue enumeration */

    if (NtfslxAreNamesEqual(
            (const USHORT *)Entry->FileName,
            Entry->FileNameLength,
            (const USHORT *)Ctx->Name,
            Ctx->NameLength,
            Ctx->IgnoreCase,
            Ctx->UpcaseTable,
            Ctx->UpcaseLength))
    {
        Ctx->FoundReference = Entry->FileReference;
        Ctx->Found = TRUE;
        /* Return a distinctive status to stop enumeration early */
        return STATUS_NO_MORE_ENTRIES;
    }

    return STATUS_SUCCESS; /* continue */
}

/*
 * NtfslxLookupInodeByName - find a file in a directory by name
 *
 * Reads the directory's MFT record, enumerates its index entries
 * (both resident INDEX_ROOT and non-resident INDEX_ALLOCATION),
 * and returns the MFT reference of the matching entry.
 *
 * Returns STATUS_SUCCESS if found, STATUS_OBJECT_NAME_NOT_FOUND otherwise.
 */
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
    _Out_ PULONGLONG FoundMftReference)
{
    NTSTATUS Status;
    PNTFSLX_MFT_RECORD DirRecord = NULL;
    PNTFSLX_ATTR_RECORD IndexRootAttr = NULL;
    PNTFSLX_ATTR_RECORD IndexAllocAttr = NULL;
    PNTFSLX_RUNLIST_ELEMENT IndexAllocRunlist = NULL;
    ULONG IndexAllocRunlistCount = 0;
    PVOID IndexRootValue;
    ULONG IndexRootLength;
    NTFSLX_LOOKUP_CONTEXT LookupCtx;
    static const WCHAR I30Name[] = { '$', 'I', '3', '0' };

    UNREFERENCED_PARAMETER(MftRunlistCount);

    *FoundMftReference = 0;

    /* Allocate and read directory MFT record */
    DirRecord = ExAllocatePoolWithTag(
        NonPagedPool, VolumeInfo->BytesPerFileRecord, NTFSLX_TAG);
    if (DirRecord == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = NtfslxReadMftRecord(
        StorageDevice, VolumeInfo, MftRunlist,
        DirectoryMftIndex, DirRecord);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /* Verify it's a directory */
    if (!(DirRecord->Flags & NTFSLX_MFT_RECORD_IS_DIRECTORY))
    {
        Status = STATUS_NOT_A_DIRECTORY;
        goto Cleanup;
    }

    /* Find $INDEX_ROOT attribute (named $I30) */
    Status = NtfslxFindAttribute(
        DirRecord, NTFSLX_ATTRIBUTE_INDEX_ROOT,
        I30Name, 4, &IndexRootAttr);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    if (IndexRootAttr->NonResident != 0)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }

    IndexRootValue = (PUCHAR)IndexRootAttr + IndexRootAttr->Data.Resident.ValueOffset;
    IndexRootLength = IndexRootAttr->Data.Resident.ValueLength;

    /* Setup lookup context */
    LookupCtx.Name = Name;
    LookupCtx.NameLength = NameLength;
    LookupCtx.IgnoreCase = IgnoreCase;
    LookupCtx.UpcaseTable = UpcaseTable;
    LookupCtx.UpcaseLength = UpcaseLength;
    LookupCtx.FoundReference = 0;
    LookupCtx.Found = FALSE;

    /* Try to find $INDEX_ALLOCATION for large directories */
    if (NT_SUCCESS(NtfslxFindAttribute(
            DirRecord, NTFSLX_ATTRIBUTE_INDEX_ALLOCATION,
            I30Name, 4, &IndexAllocAttr)))
    {
        if (IndexAllocAttr->NonResident != 0)
        {
            NtfslxBuildIndexAllocationRunlist(
                IndexAllocAttr, &IndexAllocRunlist, &IndexAllocRunlistCount);
        }
    }

    /* Enumerate the full index tree */
    Status = NtfslxEnumerateIndexAllocationTree(
        IndexRootValue,
        IndexRootLength,
        StorageDevice,
        VolumeInfo,
        IndexAllocRunlist,
        IndexAllocRunlistCount,
        NtfslxLookupCallback,
        &LookupCtx);

    /* STATUS_NO_MORE_ENTRIES means we found it and stopped early */
    if (Status == STATUS_NO_MORE_ENTRIES && LookupCtx.Found)
    {
        *FoundMftReference = LookupCtx.FoundReference;
        Status = STATUS_SUCCESS;
    }
    else if (NT_SUCCESS(Status) && !LookupCtx.Found)
    {
        Status = STATUS_OBJECT_NAME_NOT_FOUND;
    }

Cleanup:
    if (IndexAllocRunlist != NULL)
        ExFreePoolWithTag(IndexAllocRunlist, 'aIxN');
    if (DirRecord != NULL)
        ExFreePoolWithTag(DirRecord, NTFSLX_TAG);

    return Status;
}
