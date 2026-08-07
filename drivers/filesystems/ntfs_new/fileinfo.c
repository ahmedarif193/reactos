/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source file for the ntfs_new file information
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfspch.h"

 static
 NTSTATUS
 GetFileBasicInformation(_In_ PFileContextBlock FileCB,
                         _Out_ PFILE_BASIC_INFORMATION Buffer,
                         _Inout_ PULONG Length)
 {
    NTSTATUS Status;
    NtfsFileBasicInformation Information;

    if (!FileCB)
        return STATUS_INVALID_PARAMETER;

    if (*Length < sizeof(FILE_BASIC_INFORMATION))
        return STATUS_BUFFER_TOO_SMALL;

    Status = NtfsFileRecordGetBasicInformation(
        FileCB->FileRec,
        &Information);
    if (!NT_SUCCESS(Status))
        return Status;

    Buffer->CreationTime.QuadPart =
        Information.CreationTime;
    Buffer->LastAccessTime.QuadPart =
        Information.LastAccessTime;
    Buffer->LastWriteTime.QuadPart =
        Information.LastWriteTime;
    Buffer->ChangeTime.QuadPart =
        Information.ChangeTime;
    Buffer->FileAttributes =
        Information.FileAttributes;

    *Length -= sizeof(FILE_BASIC_INFORMATION);

    return STATUS_SUCCESS;
 }

 static
 NTSTATUS
 GetFileStandardInformation(_In_ PFileContextBlock FileCB,
                            _Out_ PFILE_STANDARD_INFORMATION Buffer,
                            _Inout_ PULONG Length)
 {
     PNtfsFileRecord File;
     PAttribute DataAttribute;
     size_t FileInfoSize = sizeof(FILE_STANDARD_INFORMATION);

     if (*Length < FileInfoSize)
         return STATUS_BUFFER_TOO_SMALL;

     if (!FileCB)
         return STATUS_NOT_FOUND;

     File = FileCB->FileRec;

     // Information from the stream represented by this file object.
     DataAttribute = NtfsFileRecordGetAttribute(
         File,
         FileCB->RequestedType,
         FileCB->RequestedStream);

     if (DataAttribute)
     {
         if (DataAttribute->IsNonResident)
         {
             Buffer->EndOfFile.QuadPart = DataAttribute->NonResident.DataSize;
             Buffer->AllocationSize.QuadPart =
                 NtfsAttributeGetPhysicalAllocationSize(
                     DataAttribute);
         }

         else
         {
             Buffer->EndOfFile.QuadPart = DataAttribute->Resident.DataLength;
             Buffer->AllocationSize.QuadPart = 0;
         }
     }

     else
     {
         Buffer->EndOfFile.QuadPart = 0;
         Buffer->AllocationSize.QuadPart = 0;
     }

     // Information from file header
     Buffer->Directory = !!(NtfsFileRecordGetHeader(File)->Flags & FR_IS_DIRECTORY);
     Buffer->NumberOfLinks = NtfsFileRecordGetHeader(File)->HardLinkCount;

     // Information from file context block
     Buffer->DeletePending = !!(FileCB->CreateOptions & FILE_DELETE_ON_CLOSE);

     *Length -= FileInfoSize;

     return STATUS_SUCCESS;
 }

 static
 NTSTATUS
 GetFileNameInformation(_In_ PFileContextBlock FileCB,
                        _Out_ PFILE_NAME_INFORMATION Buffer,
                        _Inout_ PULONG Length)
 {
     ULONG HeaderSize;
     ULONG AvailableBytes;
     ULONG BytesToCopy;

     HeaderSize = FIELD_OFFSET(FILE_NAME_INFORMATION, FileName);
     if (*Length < HeaderSize)
         return STATUS_INFO_LENGTH_MISMATCH;

     Buffer->FileNameLength = FileCB->FileName.Length;
     AvailableBytes = *Length - HeaderSize;
     BytesToCopy = min(AvailableBytes, Buffer->FileNameLength);
     if (BytesToCopy)
         RtlCopyMemory(Buffer->FileName, FileCB->FileName.Buffer, BytesToCopy);
     *Length -= HeaderSize + BytesToCopy;

     return BytesToCopy < Buffer->FileNameLength
         ? STATUS_BUFFER_OVERFLOW
         : STATUS_SUCCESS;
 }

static
NTSTATUS
GetFileInternalInformation(_In_ PFileContextBlock FileCB,
                            _Out_ PFILE_INTERNAL_INFORMATION Buffer,
                            _Inout_ PULONG Length)
 {

     /* From Microsoft Learn:
      * The FILE_INTERNAL_INFORMATION structure is used to query for the file
      * system's 8-byte file reference number for a file.
      */

     if (*Length < sizeof(FILE_INTERNAL_INFORMATION))
         return STATUS_BUFFER_TOO_SMALL;

     Buffer->IndexNumber.QuadPart = NtfsFileRecordGetHeader(FileCB->FileRec)->MFTRecordNumber;

     *Length -= sizeof(FILE_INTERNAL_INFORMATION);

     return STATUS_SUCCESS;
 }

static
NTSTATUS
GetFileEaInformation(_In_ PFileContextBlock FileCB,
                     _Out_ PFILE_EA_INFORMATION Buffer,
                     _Inout_ PULONG Length)
{
    EAInformationEx EaInformation;
    ULONG EaLength = 0;
    NTSTATUS Status;

    if (!FileCB || !FileCB->FileRec)
        return STATUS_INVALID_PARAMETER;
    if (*Length < sizeof(*Buffer))
        return STATUS_BUFFER_TOO_SMALL;

    Status = NtfsFileRecordReadExtendedAttributes(FileCB->FileRec,
                                                   NULL,
                                                   &EaLength,
                                                   &EaInformation);
    if (Status == STATUS_NO_EAS_ON_FILE)
    {
        Buffer->EaSize = 0;
    }
    else if (Status == STATUS_BUFFER_TOO_SMALL)
    {
        Buffer->EaSize = EaInformation.UnpackedEASize;
    }
    else
    {
        return Status;
    }

    *Length -= sizeof(*Buffer);
    return STATUS_SUCCESS;
}

static
NTSTATUS
GetFileNetworkOpenInformation(_In_ PFileContextBlock FileCB,
                              _Out_ PFILE_NETWORK_OPEN_INFORMATION Buffer,
                              _Inout_ PULONG Length)
{
    NTSTATUS Status;
    NtfsFileBasicInformation Information;
    PAttribute DataAttribute;
    PNtfsFileRecord File;

    ASSERT(Buffer);
    ASSERT(FileCB);

    File = FileCB->FileRec;

    // Information from the stream represented by this file object.
    DataAttribute = NtfsFileRecordGetAttribute(
        File,
        FileCB->RequestedType,
        FileCB->RequestedStream);

    if (DataAttribute)
    {
        if (DataAttribute->IsNonResident)
        {
            Buffer->EndOfFile.QuadPart = DataAttribute->NonResident.DataSize;
            Buffer->AllocationSize.QuadPart =
                NtfsAttributeGetPhysicalAllocationSize(
                    DataAttribute);
        }

        else
        {
            Buffer->EndOfFile.QuadPart = DataAttribute->Resident.DataLength;
            Buffer->AllocationSize.QuadPart = 0;
        }
    }

    else
    {
        Buffer->EndOfFile.QuadPart = 0;
        Buffer->AllocationSize.QuadPart = 0;
    }

    File = FileCB->FileRec;

    Status = NtfsFileRecordGetBasicInformation(
        File,
        &Information);
    if (!NT_SUCCESS(Status))
        return Status;

    Buffer->CreationTime.QuadPart =
        Information.CreationTime;
    Buffer->LastAccessTime.QuadPart =
        Information.LastAccessTime;
    Buffer->LastWriteTime.QuadPart =
        Information.LastWriteTime;
    Buffer->ChangeTime.QuadPart =
        Information.ChangeTime;
    Buffer->FileAttributes =
        Information.FileAttributes;

    *Length -= sizeof(FILE_NETWORK_OPEN_INFORMATION);

    return STATUS_SUCCESS;
}

static
NTSTATUS
GetFileStreamInformation(
    _In_ PFileContextBlock FileCB,
    _Out_ PFILE_STREAM_INFORMATION Buffer,
    _Inout_ PULONG Length)
{
    static const WCHAR DataSuffix[] = L":$DATA";
    PNtfsDataStreamInformation Streams = NULL;
    PFILE_STREAM_INFORMATION Current;
    PFILE_STREAM_INFORMATION Last = NULL;
    ULONG Available;
    ULONG Capacity = 4;
    ULONG Count;
    ULONG BytesWritten = 0;
    ULONG LastOffset = 0;
    ULONG LastSize = 0;
    ULONG HeaderSize;
    ULONG Index;
    NTSTATUS Status;

    if (!FileCB || !FileCB->FileRec ||
        !Length)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Available = *Length;
    if (Available < sizeof(FILE_STREAM_INFORMATION))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (!Buffer)
        return STATUS_INVALID_USER_BUFFER;

    for (;;)
    {
        if (Capacity >
            MAXULONG / sizeof(*Streams))
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Done;
        }
        Streams = (PNtfsDataStreamInformation)
            ExAllocatePoolWithTag(
                PagedPool,
                Capacity * sizeof(*Streams),
                TAG_NTFS);
        if (!Streams)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Done;
        }

        Count = Capacity;
        Status = NtfsFileRecordQueryDataStreams(
            FileCB->FileRec,
            Streams,
            &Count);
        if (Status != STATUS_BUFFER_TOO_SMALL &&
            Status != STATUS_BUFFER_OVERFLOW)
        {
            break;
        }

        ExFreePoolWithTag(Streams, TAG_NTFS);
        Streams = NULL;
        if (Capacity > MAXULONG / 2)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Done;
        }
        Capacity *= 2;
    }
    if (!NT_SUCCESS(Status))
        goto Done;

    HeaderSize = FIELD_OFFSET(
        FILE_STREAM_INFORMATION,
        StreamName);
    for (Index = 0; Index < Count; Index++)
    {
        ULONG NameBytes;
        ULONG ThisSize;
        ULONG EntrySize;

        if (Streams[Index].DataSize >
                MAXLONGLONG ||
            Streams[Index].AllocationSize >
                MAXLONGLONG)
        {
            Status = STATUS_FILE_TOO_LARGE;
            goto Done;
        }

        NameBytes =
            (Streams[Index].NameLength + 7) *
            sizeof(WCHAR);
        ThisSize = HeaderSize + NameBytes;
        EntrySize = Index + 1 == Count
            ? ThisSize
            : ALIGN_UP_BY(ThisSize,
                          sizeof(ULONGLONG));
        if (BytesWritten > Available ||
            ThisSize >
                Available - BytesWritten)
        {
            if (Last)
            {
                Last->NextEntryOffset = 0;
                BytesWritten =
                    LastOffset + LastSize;
            }
            Status = STATUS_BUFFER_OVERFLOW;
            goto Done;
        }

        Current = (PFILE_STREAM_INFORMATION)
            ((PUCHAR)Buffer + BytesWritten);
        RtlZeroMemory(Current, EntrySize);
        Current->NextEntryOffset =
            Index + 1 == Count ? 0 : EntrySize;
        Current->StreamNameLength = NameBytes;
        Current->StreamSize.QuadPart =
            (LONGLONG)Streams[Index].DataSize;
        Current->StreamAllocationSize.QuadPart =
            (LONGLONG)
                Streams[Index].AllocationSize;
        Current->StreamName[0] = L':';
        if (Streams[Index].NameLength != 0)
        {
            RtlCopyMemory(
                Current->StreamName + 1,
                Streams[Index].Name,
                Streams[Index].NameLength *
                    sizeof(WCHAR));
        }
        RtlCopyMemory(
            Current->StreamName + 1 +
                Streams[Index].NameLength,
            DataSuffix,
            (RTL_NUMBER_OF(DataSuffix) - 1) *
                sizeof(WCHAR));

        Last = Current;
        LastOffset = BytesWritten;
        LastSize = ThisSize;
        BytesWritten += ThisSize;
        if (Index + 1 != Count)
        {
            if (EntrySize >
                Available - LastOffset)
            {
                Current->NextEntryOffset = 0;
                Status = STATUS_BUFFER_OVERFLOW;
                goto Done;
            }
            BytesWritten =
                LastOffset + EntrySize;
        }
    }
    Status = STATUS_SUCCESS;

Done:
    if (Streams)
        ExFreePoolWithTag(Streams, TAG_NTFS);
    *Length = Available - BytesWritten;
    return Status;
}

static
inline
BOOLEAN
ContainsWildcard(PUNICODE_STRING String)
{
    USHORT charCount = (USHORT)(String->Length / sizeof(WCHAR));
    for (USHORT i = 0; i < charCount; i++)
    {
        if (String->Buffer[i] == L'*'    ||
            String->Buffer[i] == L'?'    ||
            String->Buffer[i] == DOS_DOT ||
            String->Buffer[i] == DOS_QM  ||
            String->Buffer[i] == DOS_STAR)
            return TRUE;
    }
    return FALSE;
}

/*
 * A directory search pattern is given once, on the query that starts the
 * scan. Every later query leaves it out and means "the same pattern", so the
 * handle has to remember it; forgetting it turns a single-name search into a
 * full enumeration and hands the caller files it never asked for.
 */
static
PUNICODE_STRING
NtfsCaptureDirSearchPattern(_In_ PFileContextBlock FileCB,
                            _In_opt_ PUNICODE_STRING FileNameFilter)
{
    PWCHAR PatternBuffer;

    if (FileNameFilter && FileNameFilter->Length != 0)
    {
        PatternBuffer = (PWCHAR)ExAllocatePoolUninitialized(PagedPool, FileNameFilter->Length, TAG_NTFS);
        if (!PatternBuffer)
            return FileNameFilter;

        RtlCopyMemory(PatternBuffer, FileNameFilter->Buffer, FileNameFilter->Length);
        if (FileCB->DirSearchPattern.Buffer)
            ExFreePoolWithTag(FileCB->DirSearchPattern.Buffer, TAG_NTFS);
        FileCB->DirSearchPattern.Buffer = PatternBuffer;
        FileCB->DirSearchPattern.Length = FileNameFilter->Length;
        FileCB->DirSearchPattern.MaximumLength = FileNameFilter->Length;
    }

    if (!FileCB->DirSearchPattern.Buffer)
        return NULL;

    return &FileCB->DirSearchPattern;
}

static
NTSTATUS
GetFileBothDirectoryInformation(_In_    PFileContextBlock FileCB,
                                _In_    UCHAR IrpFlags,
                                _In_    PUNICODE_STRING FileNameFilter,
                                _Out_   PFILE_BOTH_DIR_INFORMATION Buffer,
                                _Inout_ PULONG Length)
{
    PNtfsDirectory FileDir;
    BOOLEAN ReturnSingleEntry, RestartScan;

    if (!FileCB)
        return STATUS_INVALID_PARAMETER;

    FileDir = FileCB->FileDir;
    RestartScan = !!(IrpFlags & SL_RESTART_SCAN);
    /* A fresh handle starts at the beginning even without the flag; the
     * shared directory tree may hold another handle's cursor. */
    if (!FileCB->DirScanStarted)
    {
        RestartScan = TRUE;
        FileCB->DirScanStarted = TRUE;
    }

    if (!FileDir)
        return STATUS_NOT_FOUND;

    FileNameFilter = NtfsCaptureDirSearchPattern(FileCB, FileNameFilter);

    /* If there's no wild cards and a file name filter
     * is specified, we will only return one entry.
     */
    if (FileNameFilter &&
        !ContainsWildcard(FileNameFilter))
        ReturnSingleEntry = TRUE;
    else
        ReturnSingleEntry = !!(IrpFlags & SL_RETURN_SINGLE_ENTRY);

    return NtfsDirectoryGetFileBothDirInfo(FileDir,
                                           ReturnSingleEntry,
                                           RestartScan,
                                           FileNameFilter,
                                           Buffer,
                                           Length);
 }

static
NTSTATUS
GetFileDirectoryInformation(_In_ PFileContextBlock FileCB,
                            _In_ UCHAR IrpFlags,
                            _In_ PUNICODE_STRING FileNameFilter,
                            _Out_ PFILE_DIRECTORY_INFORMATION Buffer,
                            _Inout_ PULONG Length)
{
    PFILE_BOTH_DIR_INFORMATION Source;
    PFILE_DIRECTORY_INFORMATION Current;
    PFILE_DIRECTORY_INFORMATION Previous = NULL;
    PVOID TemporaryBuffer;
    ULONG Available;
    ULONG Remaining;
    ULONG SourceBytes;
    ULONG SourceOffset = 0;
    ULONG BytesWritten = 0;
    ULONG EntrySize;
    BOOLEAN ReturnSingleEntry;
    BOOLEAN RestartScan;
    NTSTATUS Status;

    if (!FileCB || !FileCB->FileDir)
        return STATUS_NOT_FOUND;

    Available = *Length;
    if (Available < sizeof(FILE_BOTH_DIR_INFORMATION))
        return STATUS_BUFFER_OVERFLOW;

    TemporaryBuffer = ExAllocatePoolZero(PagedPool,
                                         Available,
                                         TAG_NTFS);
    if (!TemporaryBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;

    RestartScan = !!(IrpFlags & SL_RESTART_SCAN);
    /* A fresh handle starts at the beginning even without the flag; the
     * shared directory tree may hold another handle's cursor. */
    if (!FileCB->DirScanStarted)
    {
        RestartScan = TRUE;
        FileCB->DirScanStarted = TRUE;
    }
    FileNameFilter = NtfsCaptureDirSearchPattern(FileCB, FileNameFilter);
    if (FileNameFilter &&
        !ContainsWildcard(FileNameFilter))
    {
        ReturnSingleEntry = TRUE;
    }
    else
    {
        ReturnSingleEntry =
            !!(IrpFlags & SL_RETURN_SINGLE_ENTRY);
    }

    Remaining = Available;
    Status = NtfsDirectoryGetFileBothDirInfo(
        FileCB->FileDir,
        ReturnSingleEntry,
        RestartScan,
        FileNameFilter,
        (PFILE_BOTH_DIR_INFORMATION)TemporaryBuffer,
        &Remaining);
    SourceBytes = Available - Remaining;
    if (!NT_SUCCESS(Status) || SourceBytes == 0)
    {
        ExFreePoolWithTag(TemporaryBuffer, TAG_NTFS);
        return NT_SUCCESS(Status) ? STATUS_BUFFER_OVERFLOW : Status;
    }

    while (SourceOffset < SourceBytes)
    {
        Source = (PFILE_BOTH_DIR_INFORMATION)
            ((PUCHAR)TemporaryBuffer + SourceOffset);
        EntrySize = ALIGN_UP_BY(
            FIELD_OFFSET(FILE_DIRECTORY_INFORMATION, FileName) +
                Source->FileNameLength,
            sizeof(ULONGLONG));
        if (EntrySize > Available - BytesWritten)
        {
            Status = STATUS_BUFFER_OVERFLOW;
            break;
        }

        Current = (PFILE_DIRECTORY_INFORMATION)
            ((PUCHAR)Buffer + BytesWritten);
        RtlZeroMemory(Current, EntrySize);
        Current->FileIndex = Source->FileIndex;
        Current->CreationTime = Source->CreationTime;
        Current->LastAccessTime = Source->LastAccessTime;
        Current->LastWriteTime = Source->LastWriteTime;
        Current->ChangeTime = Source->ChangeTime;
        Current->EndOfFile = Source->EndOfFile;
        Current->AllocationSize = Source->AllocationSize;
        Current->FileAttributes = Source->FileAttributes;
        Current->FileNameLength = Source->FileNameLength;
        RtlCopyMemory(Current->FileName,
                      Source->FileName,
                      Source->FileNameLength);

        if (Previous)
        {
            Previous->NextEntryOffset =
                (ULONG)((PUCHAR)Current -
                        (PUCHAR)Previous);
        }
        Previous = Current;
        BytesWritten += EntrySize;

        if (Source->NextEntryOffset == 0)
            break;
        if (Source->NextEntryOffset > SourceBytes - SourceOffset)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            break;
        }
        SourceOffset += Source->NextEntryOffset;
    }

    if (Previous)
        Previous->NextEntryOffset = 0;
    *Length = Available - BytesWritten;
    ExFreePoolWithTag(TemporaryBuffer, TAG_NTFS);
    return Status;
}

VOID
NtfsRefreshFileSizes(_In_ PFileContextBlock FileCB,
                     _In_opt_ PFILE_OBJECT FileObject)
{
    PAttribute DataAttribute;

    if (!FileCB || !FileCB->FileRec)
        return;

    DataAttribute = NtfsFileRecordGetAttribute(
        FileCB->FileRec,
        FileCB->RequestedType,
        FileCB->RequestedStream);
    if (DataAttribute)
    {
        if (DataAttribute->IsNonResident)
        {
            FileCB->CommonFCBHeader.AllocationSize.QuadPart =
                NtfsAttributeGetPhysicalAllocationSize(
                    DataAttribute);
            FileCB->CommonFCBHeader.FileSize.QuadPart =
                DataAttribute->NonResident.DataSize;
            FileCB->CommonFCBHeader.ValidDataLength.QuadPart =
                DataAttribute->NonResident.InitalizedDataSize;
        }
        else
        {
            FileCB->CommonFCBHeader.AllocationSize.QuadPart =
                DataAttribute->Resident.DataLength;
            FileCB->CommonFCBHeader.FileSize.QuadPart =
                DataAttribute->Resident.DataLength;
            FileCB->CommonFCBHeader.ValidDataLength.QuadPart =
                DataAttribute->Resident.DataLength;
        }
    }
    else
    {
        FileCB->CommonFCBHeader.AllocationSize.QuadPart = 0;
        FileCB->CommonFCBHeader.FileSize.QuadPart = 0;
        FileCB->CommonFCBHeader.ValidDataLength.QuadPart = 0;
    }

    if (FileObject && CcIsFileCached(FileObject))
    {
        CcSetFileSizes(
            FileObject,
            (PCC_FILE_SIZES)&
                FileCB->CommonFCBHeader.AllocationSize);
    }
    /* Cc dereferences its map assuming allocation covers the data. */
    if (FileCB->CommonFCBHeader.AllocationSize.QuadPart <
        FileCB->CommonFCBHeader.FileSize.QuadPart)
    {
        FileCB->CommonFCBHeader.AllocationSize.QuadPart =
            FileCB->CommonFCBHeader.FileSize.QuadPart;
    }

}

/*
 * This driver writes data straight into the volume, never through the cache
 * manager, so pages Mm already holds for a stream keep the old contents after
 * any size change or overwrite. Every mutation that bypasses Cc drops the
 * affected range here so a later mapped or cached reader faults in the new
 * bytes. Purging synchronizes against paging I/O; callers hold MainResource
 * at most, so acquiring PagingIoResource here preserves lock order.
 */
VOID
NtfsPurgeStreamCache(_In_ PFileContextBlock FileCB,
                     _In_ PFILE_OBJECT FileObject,
                     _In_opt_ PLARGE_INTEGER Offset,
                     _In_ ULONG Length)
{
    if (!FileObject->SectionObjectPointer)
        return;

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&FileCB->PagingIoResource, TRUE);
    CcPurgeCacheSection(FileObject->SectionObjectPointer, Offset, Length, FALSE);
    ExReleaseResourceLite(&FileCB->PagingIoResource);
    KeLeaveCriticalRegion();
}

static
NTSTATUS
NtfsSetRenameInformation(_In_ PVolumeContextBlock VolCB,
                         _In_ PFileContextBlock FileCB,
                         _In_ PFILE_OBJECT FileObject,
                         _In_ PIO_STACK_LOCATION IrpSp,
                         _In_ PFILE_RENAME_INFORMATION RenameInfo,
                         _In_ ULONG BufferLength)
{
    PFILE_OBJECT TargetFileObject;
    PFileContextBlock TargetFileCB;
    PNtfsFileRecord RefreshedRecord = NULL;
    PNtfsFileRecord ExistingRecord = NULL;
    PNtfsFileRecord StaleRecord;
    UNICODE_STRING ParentName;
    UNICODE_STRING LeafName;
    UNICODE_STRING NewName;
    PWCHAR NameBuffer;
    PWCHAR OldNameBuffer;
    ULONGLONG ExistingRecordNumber;
    ULONG RemainingNameLength = 0;
    USHORT PrefixLength;
    NTSTATUS Status;
    BOOLEAN RootParent;
    BOOLEAN ReplaceIfExists;
    BOOLEAN ExistingIsDirectory;

    if (BufferLength < FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) || RenameInfo->FileNameLength == 0 || (RenameInfo->FileNameLength & (sizeof(WCHAR) - 1)) != 0 || BufferLength - FIELD_OFFSET(FILE_RENAME_INFORMATION, FileName) < RenameInfo->FileNameLength)
        return STATUS_INVALID_PARAMETER;
    if (!(FileCB->DesiredAccess & DELETE))
        return STATUS_ACCESS_DENIED;
    if (FileCB->DeletePending)
        return STATUS_DELETE_PENDING;
    if (FileCB->IsVolumeOpen || FileCB->FileName.Length <= sizeof(WCHAR) || FileCB->RequestedStream)
        return STATUS_INVALID_PARAMETER;
    ReplaceIfExists = IrpSp->Parameters.SetFile.ReplaceIfExists || RenameInfo->ReplaceIfExists;

    TargetFileObject = IrpSp->Parameters.SetFile.FileObject;
    if (TargetFileObject)
    {
        TargetFileCB = (PFileContextBlock)TargetFileObject->FsContext;
        if (!TargetFileCB || !TargetFileCB->FileRec || !(NtfsFileRecordGetHeader(TargetFileCB->FileRec)->Flags & FR_IS_DIRECTORY))
            return STATUS_INVALID_PARAMETER;
        ParentName = TargetFileCB->FileName;
        LeafName = TargetFileObject->FileName;
    }
    else
    {
        PWCHAR OldLeafName;
        USHORT OldLeafLength;
        USHORT ParentLength;

        if (!NtfsSplitParentName(&FileCB->FileName, &ParentLength, &OldLeafName, &OldLeafLength))
            return STATUS_OBJECT_PATH_INVALID;
        ParentName.Buffer = FileCB->FileName.Buffer;
        ParentName.Length = ParentLength * sizeof(WCHAR);
        ParentName.MaximumLength = ParentName.Length;
        LeafName.Buffer = RenameInfo->FileName;
        LeafName.Length = (USHORT)RenameInfo->FileNameLength;
        LeafName.MaximumLength = LeafName.Length;
    }

    if (LeafName.Length == 0 || ParentName.Length == 0)
        return STATUS_OBJECT_NAME_INVALID;
    for (PrefixLength = 0; PrefixLength < LeafName.Length / sizeof(WCHAR); PrefixLength++)
    {
        if (LeafName.Buffer[PrefixLength] == L'\\' || LeafName.Buffer[PrefixLength] == L'/')
            return STATUS_OBJECT_NAME_INVALID;
    }

    RootParent = ParentName.Length == sizeof(WCHAR) && ParentName.Buffer[0] == L'\\';
    if ((ULONG)ParentName.Length + (RootParent ? 0 : sizeof(WCHAR)) + LeafName.Length > MAXUSHORT)
        return STATUS_NAME_TOO_LONG;
    NewName.Length = ParentName.Length + (RootParent ? 0 : sizeof(WCHAR)) + LeafName.Length;
    NewName.MaximumLength = NewName.Length;
    NameBuffer = ExAllocatePoolWithTag(PagedPool, NewName.Length, TAG_NTFS);
    if (!NameBuffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    NewName.Buffer = NameBuffer;
    PrefixLength = RootParent ? 0 : ParentName.Length;
    if (PrefixLength)
        RtlCopyMemory(NewName.Buffer, ParentName.Buffer, PrefixLength);
    NewName.Buffer[PrefixLength / sizeof(WCHAR)] = L'\\';
    RtlCopyMemory((PUCHAR)NewName.Buffer + PrefixLength + sizeof(WCHAR), LeafName.Buffer, LeafName.Length);

    /*
     * A destination that already exists is only overwritten when the caller
     * asked for it, and never when it is a directory. Renaming a file onto
     * its own name is a no-op, so it must not reach the delete below.
     */
    Status = NtfsMasterFileTableGetFileRecordFromQueryEx(NtfsVolumeGetMft(VolCB->DiskVolume), NewName.Buffer, NewName.Length / sizeof(WCHAR), TRUE, &RemainingNameLength, &ExistingRecord);
    if (NT_SUCCESS(Status) && RemainingNameLength == 0 && ExistingRecord)
    {
        ExistingRecordNumber = NtfsFileRecordGetHeader(ExistingRecord)->MFTRecordNumber;
        ExistingIsDirectory = !!(NtfsFileRecordGetHeader(ExistingRecord)->Flags & FR_IS_DIRECTORY);
        NtfsFileRecordDestroy(ExistingRecord);
        ExistingRecord = NULL;

        if (ExistingRecordNumber == NtfsFileRecordGetHeader(FileCB->FileRec)->MFTRecordNumber)
        {
            ExFreePoolWithTag(NameBuffer, TAG_NTFS);
            return STATUS_SUCCESS;
        }
        if (!ReplaceIfExists || ExistingIsDirectory)
        {
            ExFreePoolWithTag(NameBuffer, TAG_NTFS);
            return STATUS_OBJECT_NAME_COLLISION;
        }

        Status = NtfsMasterFileTableDeleteFile(NtfsVolumeGetMft(VolCB->DiskVolume), NewName.Buffer, NewName.Length / sizeof(WCHAR), FALSE);
        NtfsEvictCachedRecord(VolCB, NewName.Buffer, (USHORT)(NewName.Length / sizeof(WCHAR)), NT_SUCCESS(Status));
        InterlockedIncrement(&VolCB->DirGeneration);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(NameBuffer, TAG_NTFS);
            return Status;
        }
    }
    else if (ExistingRecord)
    {
        NtfsFileRecordDestroy(ExistingRecord);
        ExistingRecord = NULL;
    }

    RemainingNameLength = 0;
    Status = NtfsMasterFileTableRenameFile(NtfsVolumeGetMft(VolCB->DiskVolume), FileCB->FileName.Buffer, FileCB->FileName.Length / sizeof(WCHAR), NewName.Buffer, NewName.Length / sizeof(WCHAR));
    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(NameBuffer, TAG_NTFS);
        return Status;
    }

    NtfsEvictCachedRecord(VolCB, FileCB->FileName.Buffer, FileCB->FileName.Length / sizeof(WCHAR), FALSE);
    NtfsRecordNameMissing(VolCB, FileCB->FileName.Buffer, FileCB->FileName.Length / sizeof(WCHAR));
    NtfsForgetMissingName(VolCB, NewName.Buffer, NewName.Length / sizeof(WCHAR));
    InterlockedIncrement(&VolCB->DirGeneration);

    Status = NtfsMasterFileTableGetFileRecordFromQueryEx(NtfsVolumeGetMft(VolCB->DiskVolume), NewName.Buffer, NewName.Length / sizeof(WCHAR), TRUE, &RemainingNameLength, &RefreshedRecord);
    if (NT_SUCCESS(Status) && RemainingNameLength == 0)
    {
        StaleRecord = FileCB->FileRec;
        FileCB->FileRec = RefreshedRecord;
        /*
         * The old name's cache entry has just been evicted, but other handles
         * may still hold it, so the record behind it belongs to them until
         * they let go. This handle keeps the freshly parsed one instead.
         */
        if (FileCB->CachedRecord)
        {
            NtfsReleaseCachedRecord(VolCB, FileCB->CachedRecord);
            FileCB->CachedRecord = NULL;
        }
        else
        {
            NtfsFileRecordDestroy(StaleRecord);
        }
        NtfsRefreshFileSizes(FileCB, FileObject);
    }
    else
    {
        if (RefreshedRecord)
            NtfsFileRecordDestroy(RefreshedRecord);
        DPRINT1("NtfsSetRenameInformation: renamed file but failed to refresh its record (0x%08lx, remaining %lu)\n", Status, RemainingNameLength);
    }

    OldNameBuffer = FileCB->FileName.Buffer;
    if (NewName.Length <= sizeof(FileCB->InlineFileName))
    {
        RtlCopyMemory(FileCB->InlineFileName, NewName.Buffer, NewName.Length);
        FileCB->FileName.Buffer = FileCB->InlineFileName;
        ExFreePoolWithTag(NameBuffer, TAG_NTFS);
    }
    else
    {
        FileCB->FileName.Buffer = NameBuffer;
    }
    FileCB->FileName.Length = NewName.Length;
    FileCB->FileName.MaximumLength = NewName.Length;
    if (OldNameBuffer != FileCB->InlineFileName)
        ExFreePool(OldNameBuffer);
    FileObject->Flags |= FO_FILE_MODIFIED;
    return STATUS_SUCCESS;
}

/* GLOBALS *****************************************************************/

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, NtfsFsdQueryInformation)
#pragma alloc_text(PAGE, NtfsFsdSetInformation)
#pragma alloc_text(PAGE, NtfsFsdDirectoryControl)
#endif

/* FUNCTIONS ****************************************************************/

_Function_class_(IRP_MJ_QUERY_INFORMATION)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdQueryInformation(_In_    PDEVICE_OBJECT VolumeDeviceObject,
                        _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);

    /* Overview:
     * Determine if file information request is appropriate.
     * If it is, fulfill it. If it isn't, return STATUS_INVALID_DEVICE_REQUEST.
     *
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/irp-mj-query-information
     */

    PIO_STACK_LOCATION IoStack;
    FILE_INFORMATION_CLASS FileInfoRequest;
    PFileContextBlock FileCB;
    NTSTATUS Status;
    PVOID SystemBuffer;
    PFILE_OBJECT FileObject;
    ULONG BufferLength;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    FileInfoRequest = IoStack->Parameters.QueryFile.FileInformationClass;
    FileObject = IoStack->FileObject;
    FileCB = (PFileContextBlock)FileObject->FsContext;
    if (!FileCB)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Done;
    }

    if (FileCB->StreamCB)
    {
        FileObject->SectionObjectPointer =
            &FileCB->StreamCB->SectionObjectPointers;
    }
    SystemBuffer = GetBuffer(Irp);
    BufferLength = IoStack->Parameters.QueryFile.Length;

    switch (FileInfoRequest)
    {
        case FileBasicInformation:
            Status = GetFileBasicInformation(FileCB,
                                             (PFILE_BASIC_INFORMATION)SystemBuffer,
                                             &BufferLength);
            break;
        case FileStandardInformation:
            Status = GetFileStandardInformation(FileCB,
                                                (PFILE_STANDARD_INFORMATION)SystemBuffer,
                                                &BufferLength);
            break;
        case FileInternalInformation:
            Status = GetFileInternalInformation(FileCB,
                                                (PFILE_INTERNAL_INFORMATION)SystemBuffer,
                                                &BufferLength);
            break;
        case FileEaInformation:
            Status = GetFileEaInformation(FileCB,
                                          (PFILE_EA_INFORMATION)SystemBuffer,
                                          &BufferLength);
            break;
        case FileNameInformation:
            Status = GetFileNameInformation(FileCB,
                                            (PFILE_NAME_INFORMATION)SystemBuffer,
                                            &BufferLength);
            break;
        case FileNetworkOpenInformation:
            Status = GetFileNetworkOpenInformation(FileCB,
                                                   (PFILE_NETWORK_OPEN_INFORMATION)SystemBuffer,
                                                   &BufferLength);
            break;
        case FileStreamInformation:
            Status = GetFileStreamInformation(
                FileCB,
                (PFILE_STREAM_INFORMATION)SystemBuffer,
                &BufferLength);
            break;
        case FilePositionInformation:
        {
            PFILE_POSITION_INFORMATION Position = (PFILE_POSITION_INFORMATION)SystemBuffer;

            if (BufferLength < sizeof(FILE_POSITION_INFORMATION))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Position->CurrentByteOffset = FileObject->CurrentByteOffset;
            BufferLength -= sizeof(FILE_POSITION_INFORMATION);
            Status = STATUS_SUCCESS;
            break;
        }
        case FileAttributeTagInformation:
        {
            PFILE_ATTRIBUTE_TAG_INFORMATION Tag = (PFILE_ATTRIBUTE_TAG_INFORMATION)SystemBuffer;
            FILE_BASIC_INFORMATION Basic;
            ULONG BasicLength = sizeof(Basic);

            if (BufferLength < sizeof(FILE_ATTRIBUTE_TAG_INFORMATION))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            Status = GetFileBasicInformation(FileCB, &Basic, &BasicLength);
            if (!NT_SUCCESS(Status))
                break;

            Tag->FileAttributes = Basic.FileAttributes ? Basic.FileAttributes
                                                       : FILE_ATTRIBUTE_NORMAL;
            /* Reparse points are not surfaced yet, so the tag is always zero. */
            Tag->ReparseTag = 0;
            BufferLength -= sizeof(FILE_ATTRIBUTE_TAG_INFORMATION);
            break;
        }
        case FileAllInformation:
        {
            PFILE_ALL_INFORMATION All = (PFILE_ALL_INFORMATION)SystemBuffer;
            ULONG FixedLength = FIELD_OFFSET(FILE_ALL_INFORMATION, NameInformation);
            ULONG PartLength;
            ULONG NameLength;

            /* The name is variable length and comes last, so the fixed part
             * must fit in full before any of it is filled in. */
            if (BufferLength < FixedLength)
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }

            RtlZeroMemory(All, FixedLength);

            PartLength = sizeof(FILE_BASIC_INFORMATION);
            Status = GetFileBasicInformation(FileCB, &All->BasicInformation, &PartLength);
            if (!NT_SUCCESS(Status))
                break;

            PartLength = sizeof(FILE_STANDARD_INFORMATION);
            Status = GetFileStandardInformation(FileCB, &All->StandardInformation, &PartLength);
            if (!NT_SUCCESS(Status))
                break;

            PartLength = sizeof(FILE_INTERNAL_INFORMATION);
            Status = GetFileInternalInformation(FileCB, &All->InternalInformation, &PartLength);
            if (!NT_SUCCESS(Status))
                break;

            PartLength = sizeof(FILE_EA_INFORMATION);
            Status = GetFileEaInformation(FileCB, &All->EaInformation, &PartLength);
            if (!NT_SUCCESS(Status))
                break;

            All->AccessInformation.AccessFlags = FileCB->DesiredAccess;
            All->PositionInformation.CurrentByteOffset = FileObject->CurrentByteOffset;
            All->ModeInformation.Mode = 0;
            All->AlignmentInformation.AlignmentRequirement = FILE_BYTE_ALIGNMENT;

            NameLength = BufferLength - FixedLength;
            Status = GetFileNameInformation(FileCB, &All->NameInformation, &NameLength);
            if (NT_SUCCESS(Status))
            {
                BufferLength = NameLength;
                break;
            }

            /* Not enough room for the name: report the fixed part only. */
            BufferLength -= FixedLength;
            Status = STATUS_BUFFER_OVERFLOW;
            break;
        }
        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

Done:
    Irp->IoStatus.Status = Status;
    if (NT_SUCCESS(Status) ||
        Status == STATUS_BUFFER_OVERFLOW)
    {
        Irp->IoStatus.Information =
            IoStack->Parameters.QueryFile.Length -
            BufferLength;

#ifdef __REACTOS__
        // HACK!!! Driver should not have to edit UserIosb.
        Irp->UserIosb->Status = Irp->IoStatus.Status;
        Irp->UserIosb->Information = Irp->IoStatus.Information;
#endif
    }
    else
    {
        Irp->IoStatus.Information = 0;
    }

    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}

static
NTSTATUS
NtfsCheckDirectoryEmpty(_In_ PVolumeContextBlock VolCB,
                        _In_ PFileContextBlock FileCB)
{
    NtfsDirectoryEntry Entry;
    PNtfsDirectory Directory;
    NTSTATUS Status;

    if (!(NtfsFileRecordGetHeader(FileCB->FileRec)->Flags & FR_IS_DIRECTORY))
        return STATUS_SUCCESS;

    Directory = NtfsDirectoryCreate(VolCB->DiskVolume);
    if (!Directory)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = NtfsDirectoryLoadDirectory(Directory, FileCB->FileRec);
    if (NT_SUCCESS(Status))
        Status = NtfsDirectoryReadNext(Directory, TRUE, &Entry);
    NtfsDirectoryDestroy(Directory);

    if (NT_SUCCESS(Status))
        return STATUS_DIRECTORY_NOT_EMPTY;
    if (Status == STATUS_NO_MORE_FILES)
        return STATUS_SUCCESS;
    return Status;
}

_Function_class_(IRP_MJ_SET_INFORMATION)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdSetInformation(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                      _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    PIO_STACK_LOCATION IrpSp;
    PFILE_OBJECT FileObject;
    PFileContextBlock FileCB;
    PVolumeContextBlock VolCB;
    PVOID SystemBuffer;
    LARGE_INTEGER RequestedSize;
    NtfsFileBasicInformation BasicInformation;
    PFILE_BASIC_INFORMATION RequestedBasic;
    ULONG NewTimestampMask;
    ULONG OldTimestampMask;
    ULONG BufferLength;
    NTSTATUS Status;
    BOOLEAN AllocationRequest = FALSE;
    BOOLEAN ResourceAcquired = FALSE;
    BOOLEAN MetadataResourceAcquired = FALSE;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    FileObject = IrpSp->FileObject;
    FileCB = FileObject
        ? (PFileContextBlock)FileObject->FsContext
        : NULL;
    VolCB = VolumeDeviceObject
        ? (PVolumeContextBlock)
            VolumeDeviceObject->DeviceExtension
        : NULL;
    SystemBuffer = GetBuffer(Irp);
    BufferLength =
        IrpSp->Parameters.SetFile.Length;

    if (!FileObject || !FileCB || !FileCB->FileRec ||
        !VolCB || !VolCB->DiskVolume)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }
    if (NtfsVolumeIsReadOnly(VolCB->DiskVolume))
    {
        Status = STATUS_MEDIA_WRITE_PROTECTED;
        goto Complete;
    }
    if (!SystemBuffer)
    {
        Status = STATUS_INVALID_USER_BUFFER;
        goto Complete;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(
        &FileCB->MainResource,
        TRUE);
    ResourceAcquired = TRUE;
    ExAcquireResourceExclusiveLite(
        &VolCB->MetadataResource,
        TRUE);
    MetadataResourceAcquired = TRUE;

    switch (IrpSp->Parameters.SetFile.
                FileInformationClass)
    {
        case FileBasicInformation:
            if (BufferLength <
                sizeof(FILE_BASIC_INFORMATION))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                goto Complete;
            }
            if (!(FileCB->DesiredAccess &
                  FILE_WRITE_ATTRIBUTES))
            {
                Status = STATUS_ACCESS_DENIED;
                goto Complete;
            }

            RequestedBasic =
                (PFILE_BASIC_INFORMATION)SystemBuffer;
            RtlZeroMemory(&BasicInformation,
                          sizeof(BasicInformation));
            OldTimestampMask =
                FileCB->AutomaticTimestampMask;
            NewTimestampMask = OldTimestampMask;

#define SET_BASIC_TIME(Member, Mask, AutomaticMask) \
    do { \
        if (RequestedBasic->Member.QuadPart > 0) \
        { \
            BasicInformation.Fields |= (Mask); \
            BasicInformation.Member = \
                (ULONGLONG)RequestedBasic->Member.QuadPart; \
            NewTimestampMask &= ~(AutomaticMask); \
        } \
        else if (RequestedBasic->Member.QuadPart == -1) \
        { \
            NewTimestampMask &= ~(AutomaticMask); \
        } \
        else if (RequestedBasic->Member.QuadPart == -2) \
        { \
            NewTimestampMask |= (AutomaticMask); \
        } \
        else if (RequestedBasic->Member.QuadPart != 0) \
        { \
            Status = STATUS_INVALID_PARAMETER; \
            goto Complete; \
        } \
    } while (0)

            SET_BASIC_TIME(
                CreationTime,
                NTFS_BASIC_INFO_CREATION_TIME,
                0);
            SET_BASIC_TIME(
                LastAccessTime,
                NTFS_BASIC_INFO_LAST_ACCESS_TIME,
                NTFS_BASIC_INFO_LAST_ACCESS_TIME);
            SET_BASIC_TIME(
                LastWriteTime,
                NTFS_BASIC_INFO_LAST_WRITE_TIME,
                NTFS_BASIC_INFO_LAST_WRITE_TIME);
            SET_BASIC_TIME(
                ChangeTime,
                NTFS_BASIC_INFO_CHANGE_TIME,
                NTFS_BASIC_INFO_CHANGE_TIME);
#undef SET_BASIC_TIME

            if (RequestedBasic->FileAttributes != 0)
            {
                BasicInformation.Fields |=
                    NTFS_BASIC_INFO_FILE_ATTRIBUTES;
                BasicInformation.FileAttributes =
                    RequestedBasic->FileAttributes;
            }

            Status =
                NtfsFileRecordSetAutomaticTimestampMask(
                    FileCB->FileRec,
                    NewTimestampMask);
            if (!NT_SUCCESS(Status))
                goto Complete;

            Status =
                NtfsFileRecordSetBasicInformation(
                    FileCB->FileRec,
                    &BasicInformation);
            if (NT_SUCCESS(Status))
            {
                FileCB->AutomaticTimestampMask =
                    NewTimestampMask;
            }
            else
            {
                (void)
                    NtfsFileRecordSetAutomaticTimestampMask(
                        FileCB->FileRec,
                        OldTimestampMask);
            }
            if (NT_SUCCESS(Status) &&
                BasicInformation.Fields != 0)
            {
                FileObject->Flags |= FO_FILE_MODIFIED;
            }
            goto Complete;

        case FileDispositionInformation:
        {
            PFILE_DISPOSITION_INFORMATION Disposition =
                (PFILE_DISPOSITION_INFORMATION)SystemBuffer;

            if (BufferLength < sizeof(FILE_DISPOSITION_INFORMATION))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                goto Complete;
            }
            if (!(FileCB->DesiredAccess & DELETE))
            {
                Status = STATUS_ACCESS_DENIED;
                goto Complete;
            }
            if (NtfsVolumeIsReadOnly(VolCB->DiskVolume))
            {
                Status = STATUS_MEDIA_WRITE_PROTECTED;
                goto Complete;
            }

            /* A read-only file is not deletable until the attribute goes. */
            if (Disposition->DeleteFile)
            {
                NtfsFileBasicInformation DispositionBasic;

                Status = NtfsFileRecordGetBasicInformation(FileCB->FileRec, &DispositionBasic);
                if (!NT_SUCCESS(Status))
                    goto Complete;
                if (DispositionBasic.FileAttributes & FILE_ATTRIBUTE_READONLY)
                {
                    Status = STATUS_CANNOT_DELETE;
                    goto Complete;
                }

                Status = NtfsCheckDirectoryEmpty(VolCB, FileCB);
                if (!NT_SUCCESS(Status))
                    goto Complete;
            }

            /* The name is only removed once the last handle is gone. */
            FileCB->DeletePending = !!Disposition->DeleteFile;
            FileObject->DeletePending = FileCB->DeletePending;
            Status = STATUS_SUCCESS;
            goto Complete;
        }

        case FileRenameInformation:
            Status = NtfsSetRenameInformation(VolCB, FileCB, FileObject, IrpSp, (PFILE_RENAME_INFORMATION)SystemBuffer, BufferLength);
            goto Complete;

        case FileEndOfFileInformation:
            if (BufferLength <
                sizeof(FILE_END_OF_FILE_INFORMATION))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                goto Complete;
            }
            if (IrpSp->Parameters.SetFile.AdvanceOnly)
            {
                Status = STATUS_NOT_IMPLEMENTED;
                goto Complete;
            }
            RequestedSize =
                ((PFILE_END_OF_FILE_INFORMATION)
                    SystemBuffer)->EndOfFile;
            break;

        case FileValidDataLengthInformation:
            if (BufferLength < sizeof(FILE_VALID_DATA_LENGTH_INFORMATION))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                goto Complete;
            }
            if (!(FileCB->DesiredAccess & FILE_WRITE_DATA))
            {
                Status = STATUS_ACCESS_DENIED;
                goto Complete;
            }

            RequestedSize = ((PFILE_VALID_DATA_LENGTH_INFORMATION)SystemBuffer)->ValidDataLength;
            if (RequestedSize.QuadPart < 0)
            {
                Status = STATUS_INVALID_PARAMETER;
                goto Complete;
            }
            Status = NtfsFileRecordSetFileValidDataLength(FileCB->FileRec, FileCB->RequestedType, FileCB->RequestedStream, (ULONGLONG)RequestedSize.QuadPart);
            if (NT_SUCCESS(Status))
            {
                NtfsRefreshFileSizes(FileCB, FileObject);
                FileObject->Flags |= FO_FILE_MODIFIED;
            }
            goto Complete;

        case FileAllocationInformation:
            if (BufferLength <
                sizeof(FILE_ALLOCATION_INFORMATION))
            {
                Status = STATUS_INFO_LENGTH_MISMATCH;
                goto Complete;
            }
            RequestedSize =
                ((PFILE_ALLOCATION_INFORMATION)
                    SystemBuffer)->AllocationSize;
            AllocationRequest = TRUE;
            break;

        default:
            Status = STATUS_NOT_IMPLEMENTED;
            goto Complete;
    }

    if (!(FileCB->DesiredAccess & FILE_WRITE_DATA))
    {
        Status = STATUS_ACCESS_DENIED;
        goto Complete;
    }
    if (NtfsFileRecordGetHeader(FileCB->FileRec)->
            Flags & FR_IS_DIRECTORY)
    {
        Status = STATUS_INVALID_DEVICE_REQUEST;
        goto Complete;
    }
    if (RequestedSize.QuadPart < 0)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Complete;
    }
    /* A stream with no section pointer has nothing mapped over it, so there is
     * nobody for a shrink to invalidate. */
    if (RequestedSize.QuadPart <
            FileCB->CommonFCBHeader.FileSize.QuadPart &&
        FileObject->SectionObjectPointer != NULL &&
        !MmCanFileBeTruncated(
            FileObject->SectionObjectPointer,
            &RequestedSize))
    {
        Status = STATUS_USER_MAPPED_FILE;
        goto Complete;
    }

    Status = AllocationRequest
        ? NtfsFileRecordSetFileAllocationSize(
            FileCB->FileRec,
            FileCB->RequestedType,
            FileCB->RequestedStream,
            (ULONGLONG)RequestedSize.QuadPart)
        : NtfsFileRecordSetFileDataSize(
            FileCB->FileRec,
            FileCB->RequestedType,
            FileCB->RequestedStream,
            (ULONGLONG)RequestedSize.QuadPart);
    if (NT_SUCCESS(Status))
    {
        NtfsRefreshFileSizes(FileCB,
                             FileObject);
        NtfsPurgeStreamCache(FileCB, FileObject, NULL, 0);
        FileObject->Flags |=
            FO_FILE_MODIFIED |
            FO_FILE_SIZE_CHANGED;
    }

Complete:
    if (MetadataResourceAcquired)
    {
        ExReleaseResourceLite(
            &VolCB->MetadataResource);
    }
    if (ResourceAcquired)
    {
        ExReleaseResourceLite(
            &FileCB->MainResource);
        KeLeaveCriticalRegion();
    }
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}

_Function_class_(IRP_MJ_DIRECTORY_CONTROL)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdDirectoryControl(_In_ PDEVICE_OBJECT VolumeDeviceObject,
                        _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-directory-control
     */

    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    FILE_INFORMATION_CLASS FileInformationRequest;
    PVolumeContextBlock VolCB;
    PFileContextBlock FileCB;
    PVOID SystemBuffer;
    ULONG BufferLength;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    FileCB = (PFileContextBlock)(IrpSp->FileObject->FsContext);
    VolCB = (PVolumeContextBlock)(VolumeDeviceObject->DeviceExtension);
    SystemBuffer = GetBuffer(Irp);
    BufferLength = IrpSp->Parameters.QueryDirectory.Length;

    if (!SystemBuffer)
    {
        Status = STATUS_INVALID_USER_BUFFER;
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_DISK_INCREMENT);
        return Status;
    }

    if (IrpSp->MinorFunction == IRP_MN_QUERY_DIRECTORY)
    {
        FileInformationRequest = IrpSp->Parameters.QueryDirectory.FileInformationClass;

        switch(FileInformationRequest)
        {
            case FileBothDirectoryInformation:
                Status = GetFileBothDirectoryInformation(FileCB,
                                                         IrpSp->Flags,
                                                         IrpSp->Parameters.QueryDirectory.FileName,
                                                         (PFILE_BOTH_DIR_INFORMATION)SystemBuffer,
                                                         &BufferLength);
                break;
            case FileDirectoryInformation:
                Status = GetFileDirectoryInformation(
                    FileCB,
                    IrpSp->Flags,
                    IrpSp->Parameters.QueryDirectory.FileName,
                    (PFILE_DIRECTORY_INFORMATION)SystemBuffer,
                                                         &BufferLength);
                break;
            case FileFullDirectoryInformation:
            case FileIdBothDirectoryInformation:
            case FileIdFullDirectoryInformation:
            case FileNamesInformation:
            case FileObjectIdInformation:
            case FileReparsePointInformation:
            default:
                Status = STATUS_INVALID_INFO_CLASS;
                break;
        }
    }

    else
    {
        if (IrpSp->MinorFunction == IRP_MN_NOTIFY_CHANGE_DIRECTORY)
        {
            if (!FileCB || !FileCB->FileDir || !VolCB->NotifySync)
            {
                Status = STATUS_INVALID_PARAMETER;
            }
            else
            {
                KeEnterCriticalRegion();
                FsRtlNotifyFullChangeDirectory(
                    VolCB->NotifySync,
                    &VolCB->NotifyList,
                    FileCB,
                    (PSTRING)&FileCB->FileName,
                    BooleanFlagOn(IrpSp->Flags, SL_WATCH_TREE),
                    FALSE,
                    IrpSp->Parameters.NotifyDirectory.CompletionFilter,
                    Irp,
                    NULL,
                    NULL);
                KeLeaveCriticalRegion();
                return STATUS_PENDING;
            }
        }

        else
        {
            Status = STATUS_INVALID_DEVICE_REQUEST;
        }
    }

    // Set to number of bytes written
    if (NT_SUCCESS(Status))
    {
        Irp->IoStatus.Information = IrpSp->Parameters.QueryDirectory.Length - BufferLength;
    }
    else
    {
        Irp->IoStatus.Information = 0;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}
