/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS directory index lookup
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

static ULONG
PathElementLength(_In_ PCWSTR FileName)
{
    PCWSTR Separator = NtfsWcsChr(FileName, L'\\');

    if (!Separator)
        Separator = NtfsWcsChr(FileName, L':');

    return Separator ? (ULONG)(Separator - FileName) : NtfsWcsLen(FileName);
}

/*
 * Search one already-validated index node. A directory B+tree stores the VCN
 * for values less than an entry on that entry, while the end entry points at
 * the rightmost child.
 */
static NTSTATUS
FindEntryInNode(
    _In_ PVolume DiskVolume,
    _In_ PUNICODE_STRING FileName,
    _In_ PIndexNodeHeader Header,
    _In_ ULONG HeaderBytes,
    _Out_ PULONGLONG FileReference,
    _Out_ PULONGLONG ChildVCN,
    _Out_ PBOOLEAN Descend)
{
    PIndexEntry Entry;
    ULONG_PTR End;
    LONG CompareResult;
    BOOLEAN FoundEnd = FALSE;

    *FileReference = 0;
    *ChildVCN = 0;
    *Descend = FALSE;

    if (HeaderBytes < sizeof(IndexNodeHeader) ||
        Header->IndexOffset < sizeof(IndexNodeHeader) ||
        Header->IndexOffset > Header->TotalIndexSize ||
        Header->TotalIndexSize > HeaderBytes)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Entry = (PIndexEntry)((PUCHAR)Header + Header->IndexOffset);
    End = (ULONG_PTR)Header + Header->TotalIndexSize;

    while ((ULONG_PTR)Entry < End)
    {
        UNICODE_STRING IndexedNameString;
        NTSTATUS Status;

        if (!NtfsIsDirectoryIndexEntryValid(
                Entry,
                (ULONG)(End - (ULONG_PTR)Entry)))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Entry->Flags & INDEX_ENTRY_END)
        {
            FoundEnd = TRUE;
            if (Entry->Flags & INDEX_ENTRY_NODE)
            {
                *ChildVCN = *GetSubnodeVCN(Entry);
                *Descend = TRUE;
            }
            break;
        }

        PFileNameEx IndexedName = (PFileNameEx)Entry->IndexStream;
        IndexedNameString = NtfsMakeCountedUnicodeString(
            IndexedName->Name,
            IndexedName->NameLength * sizeof(WCHAR));
        Status = DiskVolume->CompareFileNames(
            FileName,
            &IndexedNameString,
            &CompareResult);
        if (!NT_SUCCESS(Status))
            return Status;

        if (CompareResult == 0)
        {
            *FileReference = Entry->Data.Directory.IndexedFile;
            return STATUS_SUCCESS;
        }

        if (CompareResult < 0)
        {
            if (Entry->Flags & INDEX_ENTRY_NODE)
            {
                *ChildVCN = *GetSubnodeVCN(Entry);
                *Descend = TRUE;
                return STATUS_SUCCESS;
            }
            return STATUS_NOT_FOUND;
        }

        Entry = (PIndexEntry)((PUCHAR)Entry + Entry->EntryLength);
    }

    if (!FoundEnd)
        return STATUS_FILE_CORRUPT_ERROR;
    return *Descend ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

NTSTATUS
Directory::FindNextFile(_In_  PFileRecord File,
                        _In_  PWCHAR FileName,
                        _Out_ PULONGLONG FileReference)
{
    const ULONGLONG MaximumValue = ~(ULONGLONG)0;
    NTSTATUS Status;
    ULONG ElementLength;
    UNICODE_STRING FileNameString;
    PAttribute IndexRootAttribute;
    PAttribute IndexAllocationAttribute = NULL;
    PAttribute BitmapAttribute = NULL;
    PIndexRootEx IndexRoot;
    PUCHAR IndexBufferData = NULL;
    ULONG IndexRecordSize;
    ULONGLONG BitmapLength;
    ULONGLONG ChildVCN;
    ULONGLONG VisitedVCNs[64];
    ULONG VisitedCount = 0;
    BOOLEAN Descend;

    *FileReference = 0;

    if (!File || !(File->Header->Flags & FR_IS_DIRECTORY))
        return STATUS_NOT_FOUND;

    ElementLength = PathElementLength(FileName);
    if (ElementLength == 0)
        return STATUS_OBJECT_NAME_INVALID;
    if (ElementLength > MAXUSHORT / sizeof(WCHAR))
        return STATUS_NAME_TOO_LONG;

    FileNameString = NtfsMakeCountedUnicodeString(
        FileName,
        (USHORT)(ElementLength * sizeof(WCHAR)));

    IndexRootAttribute = File->GetAttribute(
        TypeIndexRoot,
        const_cast<PWSTR>(L"$I30"));
    if (!IndexRootAttribute ||
        IndexRootAttribute->IsNonResident ||
        IndexRootAttribute->Resident.DataLength <
            FIELD_OFFSET(IndexRootEx, Header) + sizeof(IndexNodeHeader))
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    IndexRoot = (PIndexRootEx)GetResidentDataPointer(IndexRootAttribute);
    IndexRecordSize = BytesPerIndexRecord(DiskVolume);
    if (IndexRecordSize == 0 ||
        IndexRoot->AttributeType != TypeFileName ||
        IndexRoot->CollationRule != ATTRDEF_COLLATION_FILENAME ||
        IndexRoot->BytesPerIndexRec != IndexRecordSize)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Status = FindEntryInNode(
        DiskVolume,
        &FileNameString,
        &IndexRoot->Header,
        IndexRootAttribute->Resident.DataLength -
            FIELD_OFFSET(IndexRootEx, Header),
        FileReference,
        &ChildVCN,
        &Descend);
    if (!NT_SUCCESS(Status) || !Descend)
        return Status;

    IndexAllocationAttribute = File->GetAttribute(
        TypeIndexAllocation,
        const_cast<PWSTR>(L"$I30"));
    BitmapAttribute = File->GetAttribute(
        TypeBitmap,
        const_cast<PWSTR>(L"$I30"));
    if (!IndexAllocationAttribute ||
        !IndexAllocationAttribute->IsNonResident ||
        !BitmapAttribute)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    BitmapLength = GetAttributeDataSize(BitmapAttribute);
    if (BitmapLength == 0)
        return STATUS_FILE_CORRUPT_ERROR;

    IndexBufferData = new(PagedPool, TAG_NTFS) UCHAR[IndexRecordSize];
    if (!IndexBufferData)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    while (Descend)
    {
        PIndexBuffer NodeBuffer;
        ULONGLONG AllocationUnit;
        ULONGLONG AllocationOffset;
        ULONGLONG IndexRecordNumber;
        ULONGLONG BitmapByte;
        ULONG BitmapBytesRemaining = sizeof(UCHAR);
        UCHAR BitmapMask;
        UCHAR BitmapValue;
        ULONG BytesRemaining = IndexRecordSize;

        if (VisitedCount == RTL_NUMBER_OF(VisitedVCNs))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }
        for (ULONG Index = 0; Index < VisitedCount; Index++)
        {
            if (VisitedVCNs[Index] == ChildVCN)
            {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Done;
            }
        }
        VisitedVCNs[VisitedCount++] = ChildVCN;

        AllocationUnit = IndexRecordSize < BytesPerCluster(DiskVolume)
            ? DiskVolume->BytesPerSector
            : BytesPerCluster(DiskVolume);
        if (AllocationUnit == 0 ||
            ChildVCN > MaximumValue / AllocationUnit)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        AllocationOffset = ChildVCN * AllocationUnit;
        if (AllocationOffset % IndexRecordSize != 0)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        IndexRecordNumber = AllocationOffset / IndexRecordSize;
        if ((IndexRecordNumber >> 3) >= BitmapLength)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        BitmapByte = IndexRecordNumber >> 3;
        BitmapMask = (UCHAR)(1 << (IndexRecordNumber & 7));
        Status = File->CopyData(BitmapAttribute,
                                &BitmapValue,
                                &BitmapBytesRemaining,
                                BitmapByte);
        if (!NT_SUCCESS(Status) ||
            BitmapBytesRemaining != 0 ||
            !(BitmapValue & BitmapMask))
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        Status = File->CopyData(IndexAllocationAttribute,
                                IndexBufferData,
                                &BytesRemaining,
                                AllocationOffset);
        if (!NT_SUCCESS(Status) || BytesRemaining != 0)
        {
            if (NT_SUCCESS(Status))
                Status = STATUS_END_OF_FILE;
            goto Done;
        }

        NodeBuffer = (PIndexBuffer)IndexBufferData;
        Status = NtfsApplyFixup(&NodeBuffer->RecordHeader,
                                IndexRecordSize,
                                DiskVolume->BytesPerSector);
        if (!NT_SUCCESS(Status) ||
            RtlCompareMemory(NodeBuffer->RecordHeader.TypeID,
                             "INDX",
                             4) != 4 ||
            NodeBuffer->VCN != ChildVCN)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Done;
        }

        Descend = FALSE;
        Status = FindEntryInNode(
            DiskVolume,
            &FileNameString,
            &NodeBuffer->IndexHeader,
            IndexRecordSize - FIELD_OFFSET(IndexBuffer, IndexHeader),
            FileReference,
            &ChildVCN,
            &Descend);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

Done:
    delete[] IndexBufferData;
    return Status;
}
