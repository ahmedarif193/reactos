/*
 * PROJECT:     ReactOS NTFS driver (ntfslx)
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     File creation and deletion orchestration
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

#define NDEBUG
#include <debug.h>

static
NTSTATUS
NtfslxQueryMftReference(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG MftIndex,
    _Out_ PULONGLONG MftReference)
{
    PNTFSLX_MFT_RECORD FileRecord;
    ULONG RecordSize;
    NTSTATUS Status;

    RecordSize = DeviceExtension->VolumeInfo.BytesPerFileRecord;
    FileRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (FileRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(DeviceExtension->StorageDevice,
                                 &DeviceExtension->VolumeInfo,
                                 DeviceExtension->MftRunlist,
                                 MftIndex,
                                 FileRecord);
    if (NT_SUCCESS(Status))
    {
        *MftReference = MK_MREF(MftIndex, FileRecord->SequenceNumber);
    }

    ExFreePoolWithTag(FileRecord, NTFSLX_TAG);
    return Status;
}

/*
 * Build an empty $INDEX_ROOT attribute value for a directory.
 * Caller must free the returned buffer.
 */
static
NTSTATUS
NtfslxBuildEmptyIndexRoot(
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _Outptr_ PUCHAR *IndexRootValue,
    _Out_ PULONG IndexRootLength)
{
    PNTFSLX_INDEX_ROOT Root;
    PNTFSLX_INDEX_ENTRY_HEADER EndEntry;
    ULONG TotalSize;
    ULONG EndEntryOffset;

    *IndexRootValue = NULL;
    *IndexRootLength = 0;

    /* INDEX_ROOT header + INDEX_HEADER + one end entry */
    EndEntryOffset = sizeof(NTFSLX_INDEX_ROOT);
    TotalSize = EndEntryOffset + sizeof(NTFSLX_INDEX_ENTRY_HEADER);

    Root = ExAllocatePoolWithTag(NonPagedPool, TotalSize, NTFSLX_TAG);
    if (Root == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Root, TotalSize);

    /* INDEX_ROOT fields */
    Root->Type = NTFSLX_ATTRIBUTE_FILE_NAME;
    Root->CollationRule = NTFSLX_COLLATION_FILE_NAME;
    Root->IndexBlockSize = VolumeInfo->BytesPerIndexRecord;
    Root->ClustersPerIndexBlock = (UCHAR)(VolumeInfo->BytesPerIndexRecord /
                                          VolumeInfo->BytesPerCluster);
    if (Root->ClustersPerIndexBlock == 0)
    {
        Root->ClustersPerIndexBlock = 1;
    }

    /* INDEX_HEADER */
    Root->Index.EntriesOffset = sizeof(NTFSLX_INDEX_HEADER);
    Root->Index.IndexLength = sizeof(NTFSLX_INDEX_HEADER) +
                              sizeof(NTFSLX_INDEX_ENTRY_HEADER);
    Root->Index.AllocatedSize = Root->Index.IndexLength;
    Root->Index.Flags = NTFSLX_SMALL_INDEX;

    /* End entry (sentinel) */
    EndEntry = (PNTFSLX_INDEX_ENTRY_HEADER)((PUCHAR)&Root->Index +
                                             Root->Index.EntriesOffset);
    EndEntry->Length = sizeof(NTFSLX_INDEX_ENTRY_HEADER);
    EndEntry->KeyLength = 0;
    EndEntry->Flags = NTFSLX_INDEX_ENTRY_END;

    *IndexRootValue = (PUCHAR)Root;
    *IndexRootLength = TotalSize;
    return STATUS_SUCCESS;
}

typedef struct _NTFSLX_DIRECTORY_EMPTINESS_CONTEXT
{
    BOOLEAN HasEntries;
} NTFSLX_DIRECTORY_EMPTINESS_CONTEXT, *PNTFSLX_DIRECTORY_EMPTINESS_CONTEXT;

static
NTSTATUS
NtfslxDetectAnyDirectoryEntryCallback(
    _In_ const NTFSLX_DIR_ENTRY *Entry,
    _In_opt_ PVOID Context)
{
    PNTFSLX_DIRECTORY_EMPTINESS_CONTEXT EmptyContext;

    UNREFERENCED_PARAMETER(Entry);

    EmptyContext = (PNTFSLX_DIRECTORY_EMPTINESS_CONTEXT)Context;
    EmptyContext->HasEntries = TRUE;
    return STATUS_NO_MORE_ENTRIES;
}

static
NTSTATUS
NtfslxIsDirectoryRecordEmpty(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_ PNTFSLX_MFT_RECORD FileRecord,
    _Out_ PBOOLEAN IsEmpty)
{
    static const WCHAR IndexName[] = L"$I30";
    PNTFSLX_ATTR_RECORD IndexRootAttr;
    PNTFSLX_ATTR_RECORD IndexAllocAttr;
    PNTFSLX_RUNLIST_ELEMENT IndexAllocRunlist = NULL;
    ULONG IndexAllocRunlistCount = 0;
    PVOID IndexRootValue;
    ULONG IndexRootLength;
    NTFSLX_DIRECTORY_EMPTINESS_CONTEXT EmptyContext;
    NTSTATUS Status;

    *IsEmpty = FALSE;

    Status = NtfslxFindAttribute(FileRecord,
                                 NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                 IndexName,
                                 RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexRootAttr);
    if (!NT_SUCCESS(Status) || IndexRootAttr->NonResident != 0)
    {
        return NT_SUCCESS(Status) ? STATUS_FILE_CORRUPT_ERROR : Status;
    }

    IndexRootValue = (PUCHAR)IndexRootAttr + IndexRootAttr->Data.Resident.ValueOffset;
    IndexRootLength = IndexRootAttr->Data.Resident.ValueLength;

    Status = NtfslxFindAttribute(FileRecord,
                                 NTFSLX_ATTRIBUTE_INDEX_ALLOCATION,
                                 IndexName,
                                 RTL_NUMBER_OF(IndexName) - 1,
                                 &IndexAllocAttr);
    if (NT_SUCCESS(Status))
    {
        if (IndexAllocAttr->NonResident == 0)
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Status = NtfslxBuildIndexAllocationRunlist(IndexAllocAttr,
                                                   &IndexAllocRunlist,
                                                   &IndexAllocRunlistCount);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }
    else if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        return Status;
    }

    RtlZeroMemory(&EmptyContext, sizeof(EmptyContext));
    Status = NtfslxEnumerateIndexAllocationTree(IndexRootValue,
                                                IndexRootLength,
                                                DeviceExtension->StorageDevice,
                                                &DeviceExtension->VolumeInfo,
                                                IndexAllocRunlist,
                                                IndexAllocRunlistCount,
                                                NtfslxDetectAnyDirectoryEntryCallback,
                                                &EmptyContext);

    if (IndexAllocRunlist != NULL)
    {
        ExFreePoolWithTag(IndexAllocRunlist, 'aIxN');
    }

    if (Status == STATUS_NO_MORE_ENTRIES)
    {
        Status = STATUS_SUCCESS;
    }
    if (NT_SUCCESS(Status))
    {
        *IsEmpty = EmptyContext.HasEntries ? FALSE : TRUE;
    }

    return Status;
}

NTSTATUS
NtfslxCheckDeleteAllowed(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG MftIndex)
{
    PNTFSLX_MFT_RECORD FileRecord;
    ULONG RecordSize;
    BOOLEAN IsEmpty;
    NTSTATUS Status;

    RecordSize = DeviceExtension->VolumeInfo.BytesPerFileRecord;
    FileRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (FileRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(DeviceExtension->StorageDevice,
                                 &DeviceExtension->VolumeInfo,
                                 DeviceExtension->MftRunlist,
                                 MftIndex,
                                 FileRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(FileRecord, NTFSLX_TAG);
        return Status;
    }

    if (!(FileRecord->Flags & NTFSLX_MFT_RECORD_IS_DIRECTORY))
    {
        ExFreePoolWithTag(FileRecord, NTFSLX_TAG);
        return STATUS_SUCCESS;
    }

    Status = NtfslxIsDirectoryRecordEmpty(DeviceExtension, FileRecord, &IsEmpty);
    ExFreePoolWithTag(FileRecord, NTFSLX_TAG);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    return IsEmpty ? STATUS_SUCCESS : STATUS_DIRECTORY_NOT_EMPTY;
}

NTSTATUS
NtfslxCreateNewFile(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG ParentMftIndex,
    _In_reads_(FileNameLength) const WCHAR *FileName,
    _In_ ULONG FileNameLength,
    _In_ BOOLEAN IsDirectory,
    _Out_ PULONGLONG OutMftIndex)
{
    static const WCHAR I30Name[] = L"$I30";
    NTFSLX_STANDARD_INFORMATION StdInfo;
    NTFSLX_FILE_NAME_ATTRIBUTE FnAttr;
    PUCHAR FnAttrValue;
    ULONG FnAttrValueLength;
    ULONG NameBytes;
    PNTFSLX_MFT_RECORD NewRecord;
    PUCHAR IndexRootValue;
    ULONG IndexRootLength;
    ULONGLONG RecordNumber;
    ULONGLONG ParentMftReference;
    ULONG RecordSize;
    LARGE_INTEGER CurrentTime;
    NTSTATUS Status;

    *OutMftIndex = 0;
    RecordSize = DeviceExtension->VolumeInfo.BytesPerFileRecord;
    NameBytes = FileNameLength * sizeof(WCHAR);
    FnAttrValueLength = sizeof(NTFSLX_FILE_NAME_ATTRIBUTE) + NameBytes;

    /* Get current system time */
    KeQuerySystemTime(&CurrentTime);

    Status = NtfslxQueryMftReference(DeviceExtension,
                                     ParentMftIndex,
                                     &ParentMftReference);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    DbgPrint("ntfslx: CreateNewFile: name='%.*S' parent=%I64u isDir=%u\n",
             FileNameLength, FileName, ParentMftIndex, IsDirectory);

    /* Step 1: Allocate a new MFT record. Use the Ex variant so the $MFT
     * gets extended on demand when the initial allocation is exhausted. */
    Status = NtfslxAllocateMftRecordEx(DeviceExtension, &RecordNumber);
    DbgPrint("ntfslx: CreateNewFile: AllocateMftRecord returned 0x%08lx rec=%I64u\n",
             Status, RecordNumber);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Read back the freshly initialized record */
    NewRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (NewRecord == NULL)
    {
        NtfslxFreeMftRecord(DeviceExtension->StorageDevice,
                            &DeviceExtension->VolumeInfo,
                            DeviceExtension->MftRunlist,
                            RecordNumber);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(DeviceExtension->StorageDevice,
                                 &DeviceExtension->VolumeInfo,
                                 DeviceExtension->MftRunlist,
                                 RecordNumber, NewRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(NewRecord, NTFSLX_TAG);
        NtfslxFreeMftRecord(DeviceExtension->StorageDevice,
                            &DeviceExtension->VolumeInfo,
                            DeviceExtension->MftRunlist,
                            RecordNumber);
        return Status;
    }

    /* Set directory flag if needed */
    if (IsDirectory)
    {
        NewRecord->Flags |= NTFSLX_MFT_RECORD_IS_DIRECTORY;
    }

    NewRecord->LinkCount = 1;

    /* Step 2: Insert $STANDARD_INFORMATION attribute */
    RtlZeroMemory(&StdInfo, sizeof(StdInfo));
    StdInfo.CreationTime = (ULONGLONG)CurrentTime.QuadPart;
    StdInfo.LastDataChangeTime = (ULONGLONG)CurrentTime.QuadPart;
    StdInfo.LastMftChangeTime = (ULONGLONG)CurrentTime.QuadPart;
    StdInfo.LastAccessTime = (ULONGLONG)CurrentTime.QuadPart;
    StdInfo.FileAttributes = IsDirectory ? FILE_ATTRIBUTE_DIRECTORY : FILE_ATTRIBUTE_NORMAL;

    DbgPrint("ntfslx: CreateNewFile step2: inserting $STANDARD_INFORMATION size=%u recSize=%lu bytesInUse=%lu\n",
             (unsigned)sizeof(StdInfo), RecordSize, NewRecord->BytesInUse);
    Status = NtfslxInsertAttributeRecord(NewRecord, RecordSize,
                                         NTFSLX_ATTRIBUTE_STANDARD_INFORMATION,
                                         NULL, 0,
                                         &StdInfo, sizeof(StdInfo),
                                         NULL);
    DbgPrint("ntfslx: CreateNewFile step2: result=0x%08lx bytesInUse=%lu\n", Status, NewRecord->BytesInUse);
    if (!NT_SUCCESS(Status))
    {
        goto RollbackMft;
    }

    /* Step 3: Insert $FILE_NAME attribute */
    FnAttrValue = ExAllocatePoolWithTag(NonPagedPool, FnAttrValueLength, NTFSLX_TAG);
    if (FnAttrValue == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto RollbackMft;
    }

    RtlZeroMemory(FnAttrValue, FnAttrValueLength);
    RtlCopyMemory(FnAttrValue, &FnAttr, 0); /* just ensure it's zeroed */

    {
        PNTFSLX_FILE_NAME_ATTRIBUTE FnPtr = (PNTFSLX_FILE_NAME_ATTRIBUTE)FnAttrValue;
        FnPtr->ParentDirectory = ParentMftReference;
        FnPtr->CreationTime = (ULONGLONG)CurrentTime.QuadPart;
        FnPtr->LastDataChangeTime = (ULONGLONG)CurrentTime.QuadPart;
        FnPtr->LastMftChangeTime = (ULONGLONG)CurrentTime.QuadPart;
        FnPtr->LastAccessTime = (ULONGLONG)CurrentTime.QuadPart;
        FnPtr->AllocatedSize = 0;
        FnPtr->DataSize = 0;
        FnPtr->FileAttributes = IsDirectory ? FILE_ATTRIBUTE_DIRECTORY
                                             : FILE_ATTRIBUTE_NORMAL;
        FnPtr->FileNameLength = (UCHAR)FileNameLength;
        FnPtr->FileNameType = NTFSLX_FILE_NAME_WIN32_AND_DOS;

        RtlCopyMemory(FnAttrValue + sizeof(NTFSLX_FILE_NAME_ATTRIBUTE),
                      FileName, NameBytes);
    }

    DbgPrint("ntfslx: CreateNewFile step3: inserting $FILE_NAME fnLen=%lu bytesInUse=%lu\n",
             FnAttrValueLength, NewRecord->BytesInUse);
    Status = NtfslxInsertAttributeRecord(NewRecord, RecordSize,
                                         NTFSLX_ATTRIBUTE_FILE_NAME,
                                         NULL, 0,
                                         FnAttrValue, FnAttrValueLength,
                                         NULL);
    DbgPrint("ntfslx: CreateNewFile step3: result=0x%08lx bytesInUse=%lu\n", Status, NewRecord->BytesInUse);
    ExFreePoolWithTag(FnAttrValue, NTFSLX_TAG);
    if (!NT_SUCCESS(Status))
    {
        goto RollbackMft;
    }

    /* Step 4/5: Insert $DATA or $INDEX_ROOT */
    if (!IsDirectory)
    {
        /* Empty resident $DATA attribute (zero-length) */
        Status = NtfslxInsertAttributeRecord(NewRecord, RecordSize,
                                             NTFSLX_ATTRIBUTE_DATA,
                                             NULL, 0,
                                             NULL, 0,
                                             NULL);
        if (!NT_SUCCESS(Status))
        {
            goto RollbackMft;
        }
    }
    else
    {
        /* Empty $INDEX_ROOT for $I30 */
        Status = NtfslxBuildEmptyIndexRoot(&DeviceExtension->VolumeInfo,
                                           &IndexRootValue, &IndexRootLength);
        if (!NT_SUCCESS(Status))
        {
            goto RollbackMft;
        }

        Status = NtfslxInsertAttributeRecord(NewRecord, RecordSize,
                                             NTFSLX_ATTRIBUTE_INDEX_ROOT,
                                             I30Name,
                                             RTL_NUMBER_OF(I30Name) - 1,
                                             IndexRootValue, IndexRootLength,
                                             NULL);
        ExFreePoolWithTag(IndexRootValue, NTFSLX_TAG);
        if (!NT_SUCCESS(Status))
        {
            goto RollbackMft;
        }
    }

    DbgPrint("ntfslx: CreateNewFile step4/5 done, bytesInUse=%lu\n", NewRecord->BytesInUse);

    /* Step 6: Write MFT record to disk */
    Status = NtfslxWriteMftRecord(DeviceExtension->StorageDevice,
                                  &DeviceExtension->VolumeInfo,
                                  DeviceExtension->MftRunlist,
                                  RecordNumber, NewRecord);
    DbgPrint("ntfslx: CreateNewFile step6: WriteMftRecord=0x%08lx\n", Status);
    if (!NT_SUCCESS(Status))
    {
        goto RollbackMft;
    }

    /* Step 7: Insert filename into parent directory's index */
    {
        LARGE_INTEGER Times;
        Times = CurrentTime;

        Status = NtfslxIndexInsertFileName(DeviceExtension,
                                           ParentMftIndex,
                                           FileName, FileNameLength,
                                           NTFSLX_FILE_NAME_WIN32_AND_DOS,
                                           MK_MREF(RecordNumber,
                                                   NewRecord->SequenceNumber),
                                           IsDirectory ? FILE_ATTRIBUTE_DIRECTORY
                                                        : FILE_ATTRIBUTE_NORMAL,
                                           0, 0,
                                           &Times, &Times, &Times, &Times);
    }

    if (!NT_SUCCESS(Status))
    {
        goto RollbackMft;
    }

    ExFreePoolWithTag(NewRecord, NTFSLX_TAG);
    *OutMftIndex = RecordNumber;
    return STATUS_SUCCESS;

RollbackMft:
    ExFreePoolWithTag(NewRecord, NTFSLX_TAG);
    NtfslxFreeMftRecord(DeviceExtension->StorageDevice,
                        &DeviceExtension->VolumeInfo,
                        DeviceExtension->MftRunlist,
                        RecordNumber);
    return Status;
}

NTSTATUS
NtfslxDeleteFile(
    _In_ PNTFSLX_DEVICE_EXTENSION DeviceExtension,
    _In_ ULONGLONG ParentMftIndex,
    _In_ ULONGLONG MftIndex,
    _In_reads_(FileNameLength) const WCHAR *FileName,
    _In_ ULONG FileNameLength)
{
    PNTFSLX_MFT_RECORD FileRecord;
    PNTFSLX_ATTR_RECORD DataAttr;
    PNTFSLX_RUNLIST_ELEMENT DataRunlist;
    ULONG DataRunlistCount;
    ULONG RecordSize;
    NTSTATUS Status;

    RecordSize = DeviceExtension->VolumeInfo.BytesPerFileRecord;

    /* Step 1: Read the file's MFT record */
    FileRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (FileRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Status = NtfslxReadMftRecord(DeviceExtension->StorageDevice,
                                 &DeviceExtension->VolumeInfo,
                                 DeviceExtension->MftRunlist,
                                 MftIndex, FileRecord);
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(FileRecord, NTFSLX_TAG);
        return Status;
    }

    /* Step 2: If directory, verify it's actually empty. */
    if (FileRecord->Flags & NTFSLX_MFT_RECORD_IS_DIRECTORY)
    {
        BOOLEAN IsEmpty = FALSE;

        Status = NtfslxIsDirectoryRecordEmpty(DeviceExtension, FileRecord, &IsEmpty);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(FileRecord, NTFSLX_TAG);
            return Status;
        }
        if (!IsEmpty)
        {
            ExFreePoolWithTag(FileRecord, NTFSLX_TAG);
            return STATUS_DIRECTORY_NOT_EMPTY;
        }
    }

    /* Step 3: Free clusters allocated to $DATA if non-resident */
    Status = NtfslxFindAttribute(FileRecord, NTFSLX_ATTRIBUTE_DATA,
                                 NULL, 0, &DataAttr);
    if (NT_SUCCESS(Status) && DataAttr->NonResident != 0)
    {
        DataRunlist = NULL;
        DataRunlistCount = 0;

        Status = NtfslxMappingPairsDecompress(&DeviceExtension->VolumeInfo,
                                              DataAttr,
                                              &DataRunlist, &DataRunlistCount);
        if (NT_SUCCESS(Status) && DataRunlist != NULL)
        {
            NtfslxFreeClusters(DeviceExtension->StorageDevice,
                               &DeviceExtension->VolumeInfo,
                               DeviceExtension->MftRunlist,
                               DataRunlist, DataRunlistCount);
            ExFreePoolWithTag(DataRunlist, NTFSLX_TAG);
        }
    }

    ExFreePoolWithTag(FileRecord, NTFSLX_TAG);

    /* Step 4: Remove filename from parent's index */
    Status = NtfslxIndexRemoveFileName(DeviceExtension,
                                       ParentMftIndex,
                                       FileName, FileNameLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    /* Step 5: Free the MFT record */
    Status = NtfslxFreeMftRecord(DeviceExtension->StorageDevice,
                                 &DeviceExtension->VolumeInfo,
                                 DeviceExtension->MftRunlist,
                                 MftIndex);
    return Status;
}
