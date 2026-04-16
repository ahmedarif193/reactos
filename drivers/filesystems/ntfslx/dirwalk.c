/*
 * PROJECT:     ReactOS NTFS Linux-Port Skeleton
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Unified directory-walk wrapper for ntfslx
 *
 * Integration note:
 * - Add prototypes for the public helpers below to ntfslx.h.
 * - Add this file to the ntfslx target when the caller side is ready.
 * - The helpers in this file intentionally reuse the existing validated
 *   resident-root and $INDEX_ALLOCATION tree walkers.
 */

#include "ntfslx.h"

#include <pshpack1.h>
typedef struct _NTFSLX_DIRWALK_INDEX_HEADER
{
    ULONG EntriesOffset;
    ULONG IndexLength;
    ULONG AllocatedSize;
    UCHAR Flags;
    UCHAR Reserved[3];
} NTFSLX_DIRWALK_INDEX_HEADER, *PNTFSLX_DIRWALK_INDEX_HEADER;

typedef struct _NTFSLX_DIRWALK_INDEX_ROOT
{
    ULONG Type;
    ULONG CollationRule;
    ULONG IndexBlockSize;
    UCHAR ClustersPerIndexBlock;
    UCHAR Reserved[3];
    NTFSLX_DIRWALK_INDEX_HEADER Header;
} NTFSLX_DIRWALK_INDEX_ROOT, *PNTFSLX_DIRWALK_INDEX_ROOT;
#include <poppack.h>

C_ASSERT(sizeof(NTFSLX_DIRWALK_INDEX_HEADER) == 16);
C_ASSERT(sizeof(NTFSLX_DIRWALK_INDEX_ROOT) == 32);

static
NTSTATUS
NtfslxWalkDirectoryRecordInternal(
    _In_ const VOID *DirectoryMftRecord,
    _In_ ULONG DirectoryRecordLength,
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_DIR_ENUM_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    const PNTFSLX_MFT_RECORD FileRecord;
    PNTFSLX_ATTR_RECORD IndexRootAttribute;
    PNTFSLX_ATTR_RECORD IndexAllocationAttribute;
    PNTFSLX_RUNLIST_ELEMENT IndexAllocationRunlist = NULL;
    ULONG IndexAllocationRunlistCount;
    const NTFSLX_DIRWALK_INDEX_ROOT *IndexRoot;
    NTSTATUS Status;

    if (DirectoryMftRecord == NULL ||
        StorageDevice == NULL ||
        VolumeInfo == NULL ||
        Callback == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DirectoryRecordLength < sizeof(NTFSLX_MFT_RECORD) ||
        VolumeInfo->BytesPerFileRecord == 0)
    {
        return STATUS_BUFFER_TOO_SMALL;
    }

    FileRecord = (const PNTFSLX_MFT_RECORD)DirectoryMftRecord;
    if (FileRecord->Ntfs.Magic != NTFSLX_RECORD_MAGIC_FILE ||
        FileRecord->BytesInUse == 0 ||
        FileRecord->BytesInUse > DirectoryRecordLength ||
        FileRecord->BytesAllocated > DirectoryRecordLength ||
        FileRecord->AttributesOffset >= FileRecord->BytesInUse)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = NtfslxFindAttribute((PNTFSLX_MFT_RECORD)FileRecord,
                                 NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                 L"$I30",
                                 4,
                                 &IndexRootAttribute);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (IndexRootAttribute->NonResident != 0 ||
        IndexRootAttribute->Data.Resident.ValueOffset > IndexRootAttribute->Length ||
        IndexRootAttribute->Data.Resident.ValueLength < sizeof(NTFSLX_DIRWALK_INDEX_ROOT) ||
        IndexRootAttribute->Data.Resident.ValueOffset + IndexRootAttribute->Data.Resident.ValueLength >
            IndexRootAttribute->Length)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IndexRoot = (const NTFSLX_DIRWALK_INDEX_ROOT *)
        ((const UCHAR *)IndexRootAttribute + IndexRootAttribute->Data.Resident.ValueOffset);
    IndexAllocationRunlistCount = 0;

    if (IndexRoot->Type != NTFSLX_ATTRIBUTE_INDEX_ROOT)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    if (IndexRoot->Header.Flags != 0)
    {
        Status = NtfslxFindAttribute((PNTFSLX_MFT_RECORD)FileRecord,
                                     NTFSLX_ATTRIBUTE_INDEX_ALLOCATION,
                                     L"$I30",
                                     4,
                                     &IndexAllocationAttribute);
        if (!NT_SUCCESS(Status))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (IndexAllocationAttribute->NonResident == 0)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Status = NtfslxBuildIndexAllocationRunlist(IndexAllocationAttribute,
                                                   &IndexAllocationRunlist,
                                                   &IndexAllocationRunlistCount);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }

    Status = NtfslxEnumerateIndexAllocationTree(IndexRoot,
                                                IndexRootAttribute->Data.Resident.ValueLength,
                                                StorageDevice,
                                                VolumeInfo,
                                                IndexAllocationRunlist,
                                                IndexAllocationRunlistCount,
                                                Callback,
                                                Context);
    if (IndexAllocationRunlist != NULL)
    {
        ExFreePoolWithTag(IndexAllocationRunlist, 'aIxN');
    }

    return Status;
}

NTSTATUS
NTAPI
NtfslxWalkDirectoryEntriesFromRecord(
    _In_reads_bytes_(DirectoryRecordLength) const VOID *DirectoryMftRecord,
    _In_ ULONG DirectoryRecordLength,
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_DIR_ENUM_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    return NtfslxWalkDirectoryRecordInternal(DirectoryMftRecord,
                                             DirectoryRecordLength,
                                             StorageDevice,
                                             VolumeInfo,
                                             Callback,
                                             Context);
}

NTSTATUS
NTAPI
NtfslxWalkDirectoryEntries(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_opt_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG DirectoryMftIndex,
    _In_ PNTFSLX_DIR_ENUM_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    PNTFSLX_MFT_RECORD DirectoryRecord;
    NTSTATUS Status;

    if (StorageDevice == NULL ||
        VolumeInfo == NULL ||
        Callback == NULL ||
        VolumeInfo->BytesPerFileRecord == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DirectoryRecord = ExAllocatePoolWithTag(NonPagedPool,
                                            VolumeInfo->BytesPerFileRecord,
                                            NTFSLX_TAG);
    if (DirectoryRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(StorageDevice,
                                 VolumeInfo,
                                 MftRunlist,
                                 DirectoryMftIndex,
                                 DirectoryRecord);
    if (NT_SUCCESS(Status))
    {
        Status = NtfslxWalkDirectoryRecordInternal(DirectoryRecord,
                                                   VolumeInfo->BytesPerFileRecord,
                                                   StorageDevice,
                                                   VolumeInfo,
                                                   Callback,
                                                   Context);
    }

    ExFreePoolWithTag(DirectoryRecord, NTFSLX_TAG);
    return Status;
}
