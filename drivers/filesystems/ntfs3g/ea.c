/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G extended-attribute support
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

#include <errno.h>

#define NTFS_EA_ALIGNMENT 4

static ULONG
NtfsAlignEaLength(_In_ ULONG Length)
{
    return (Length + NTFS_EA_ALIGNMENT - 1) &
           ~(NTFS_EA_ALIGNMENT - 1);
}

static BOOLEAN
NtfsEaNamesEqual(_In_ PFILE_FULL_EA_INFORMATION Left,
                 _In_ PFILE_FULL_EA_INFORMATION Right)
{
    STRING LeftName;
    STRING RightName;

    if (Left->EaNameLength != Right->EaNameLength)
        return FALSE;
    LeftName.Buffer = Left->EaName;
    LeftName.Length = LeftName.MaximumLength = Left->EaNameLength;
    RightName.Buffer = Right->EaName;
    RightName.Length = RightName.MaximumLength = Right->EaNameLength;
    return RtlEqualString(&LeftName, &RightName, TRUE);
}

/*
 * NTFS stores the final EA's NextEntryOffset as its aligned size, whereas
 * FILE_FULL_EA_INFORMATION returned to callers terminates the list with zero.
 * Accept either representation here so files produced by other NTFS
 * implementations remain readable.
 */
static BOOLEAN
NtfsGetEaEntry(_In_reads_bytes_(BufferLength) PUCHAR Buffer,
               _In_ ULONG BufferLength,
               _In_ ULONG Offset,
               _Out_ PFILE_FULL_EA_INFORMATION *Entry,
               _Out_ PULONG EntryLength,
               _Out_ PULONG NextOffset)
{
    PFILE_FULL_EA_INFORMATION Current;
    ULONG Required;
    ULONG Next;

    if (Offset > BufferLength ||
        BufferLength - Offset <
            FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) + 1)
        return FALSE;

    Current = (PFILE_FULL_EA_INFORMATION)(Buffer + Offset);
    Required = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName);
    if (Current->EaNameLength >
        MAXULONG - Required - 1 - Current->EaValueLength)
        return FALSE;
    Required += Current->EaNameLength + 1 +
                Current->EaValueLength;
    if (Required > BufferLength - Offset ||
        Current->EaName[Current->EaNameLength] != ANSI_NULL)
        return FALSE;

    Next = Current->NextEntryOffset;
    if (!Next) {
        *NextOffset = BufferLength;
    } else {
        if ((Next & (NTFS_EA_ALIGNMENT - 1)) ||
            Next < Required ||
            Next > BufferLength - Offset)
            return FALSE;
        *NextOffset = Offset + Next;
    }
    *Entry = Current;
    *EntryLength = Required;
    return TRUE;
}

static NTSTATUS
NtfsReadEaBuffer(_In_ NTFS3G_ROS_FILE *CoreFile,
                 _Outptr_result_bytebuffer_(*BufferLength) PUCHAR *Buffer,
                 _Out_ PULONG BufferLength)
{
    size_t Required;
    PUCHAR Attributes;
    int Result;

    *Buffer = NULL;
    *BufferLength = 0;
    Result = Ntfs3gRosGetExtendedAttributes(
        CoreFile, NULL, 0, &Required);
    if (Result == -ENODATA)
        return STATUS_NO_EAS_ON_FILE;
    if (Result < 0)
        return Ntfs3gRosStatusFromError(-Result);
    if (!Required || Required > MAXULONG)
        return Required ? STATUS_FILE_CORRUPT_ERROR :
                          STATUS_NO_EAS_ON_FILE;

    Attributes = ExAllocatePoolWithTag(
        PagedPool, (ULONG)Required, TAG_NTFS);
    if (!Attributes)
        return STATUS_INSUFFICIENT_RESOURCES;
    Result = Ntfs3gRosGetExtendedAttributes(
        CoreFile, Attributes, Required, &Required);
    if (Result < 0) {
        ExFreePoolWithTag(Attributes, TAG_NTFS);
        return Result == -ENODATA ?
            STATUS_NO_EAS_ON_FILE :
            Ntfs3gRosStatusFromError(-Result);
    }
    if (!Required || Required > MAXULONG) {
        ExFreePoolWithTag(Attributes, TAG_NTFS);
        return STATUS_FILE_CORRUPT_ERROR;
    }

    *Buffer = Attributes;
    *BufferLength = (ULONG)Required;
    return STATUS_SUCCESS;
}

static NTSTATUS
NtfsValidateGetEaList(
    _In_reads_bytes_(Length) PFILE_GET_EA_INFORMATION List,
    _In_ ULONG Length)
{
    ULONG Offset = 0;

    if (!List || Length <
        FIELD_OFFSET(FILE_GET_EA_INFORMATION, EaName) + 1)
        return STATUS_EA_LIST_INCONSISTENT;
    for (;;) {
        PFILE_GET_EA_INFORMATION Current;
        ULONG Required;
        ULONG Next;

        if (Offset > Length ||
            Length - Offset <
                FIELD_OFFSET(FILE_GET_EA_INFORMATION, EaName) + 1)
            return STATUS_EA_LIST_INCONSISTENT;
        Current = (PFILE_GET_EA_INFORMATION)((PUCHAR)List + Offset);
        Required = FIELD_OFFSET(FILE_GET_EA_INFORMATION, EaName) +
                   Current->EaNameLength + 1;
        if (Required > Length - Offset ||
            Current->EaName[Current->EaNameLength] != ANSI_NULL)
            return STATUS_EA_LIST_INCONSISTENT;
        Next = Current->NextEntryOffset;
        if (!Next)
            return STATUS_SUCCESS;
        if ((Next & (NTFS_EA_ALIGNMENT - 1)) ||
            Next < Required || Next > Length - Offset)
            return STATUS_EA_LIST_INCONSISTENT;
        Offset += Next;
    }
}

static BOOLEAN
NtfsEaMatchesGetList(
    _In_ PFILE_FULL_EA_INFORMATION Entry,
    _In_reads_bytes_(Length) PFILE_GET_EA_INFORMATION List,
    _In_ ULONG Length)
{
    ULONG Offset = 0;

    UNREFERENCED_PARAMETER(Length);
    for (;;) {
        PFILE_GET_EA_INFORMATION Current;
        STRING EntryName;
        STRING RequestedName;

        Current = (PFILE_GET_EA_INFORMATION)((PUCHAR)List + Offset);
        EntryName.Buffer = Entry->EaName;
        EntryName.Length = EntryName.MaximumLength =
            Entry->EaNameLength;
        RequestedName.Buffer = Current->EaName;
        RequestedName.Length = RequestedName.MaximumLength =
            Current->EaNameLength;
        if (RtlEqualString(&EntryName, &RequestedName, TRUE))
            return TRUE;
        if (!Current->NextEntryOffset)
            return FALSE;
        Offset += Current->NextEntryOffset;
    }
}

static NTSTATUS
NtfsAppendQueryEa(_Out_writes_bytes_(OutputLength) PUCHAR Output,
                  _In_ ULONG OutputLength,
                  _Inout_ PULONG OutputOffset,
                  _Inout_opt_ PFILE_FULL_EA_INFORMATION *Previous,
                  _In_ PFILE_FULL_EA_INFORMATION Source,
                  _In_ ULONG SourceLength)
{
    PFILE_FULL_EA_INFORMATION Destination;
    ULONG Start;

    Start = *Previous ?
        NtfsAlignEaLength(*OutputOffset) : 0;
    if (Start > OutputLength ||
        SourceLength > OutputLength - Start)
        return *OutputOffset ?
            STATUS_BUFFER_OVERFLOW : STATUS_BUFFER_TOO_SMALL;

    if (*Previous)
        (*Previous)->NextEntryOffset =
            Start - ((PUCHAR)*Previous - Output);
    if (Start > *OutputOffset)
        RtlZeroMemory(Output + *OutputOffset,
                      Start - *OutputOffset);
    Destination =
        (PFILE_FULL_EA_INFORMATION)(Output + Start);
    RtlCopyMemory(Destination, Source, SourceLength);
    Destination->NextEntryOffset = 0;
    *Previous = Destination;
    *OutputOffset = Start + SourceLength;
    return STATUS_SUCCESS;
}

static NTSTATUS
NtfsQueryEaLocked(_Inout_ PIRP Irp,
                  _In_ PIO_STACK_LOCATION IrpSp,
                  _Inout_ PHandleContextBlock Handle,
                  _In_ NTFS3G_ROS_FILE *CoreFile,
                  _Out_ PULONG BytesWritten)
{
    PFILE_GET_EA_INFORMATION GetList =
        IrpSp->Parameters.QueryEa.EaList;
    ULONG GetListLength =
        IrpSp->Parameters.QueryEa.EaListLength;
    PUCHAR Output = GetBuffer(Irp);
    ULONG OutputLength = IrpSp->Parameters.QueryEa.Length;
    PUCHAR Attributes = NULL;
    PFILE_FULL_EA_INFORMATION Previous = NULL;
    ULONG AttributeLength = 0;
    ULONG Offset = 0;
    ULONG Index = 0;
    ULONG StartIndex;
    BOOLEAN Matched = FALSE;
    NTSTATUS Status;

    *BytesWritten = 0;
    if (!Output)
        return STATUS_INVALID_USER_BUFFER;
    if (GetList) {
        Status = NtfsValidateGetEaList(
            GetList, GetListLength);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    Status = NtfsReadEaBuffer(
        CoreFile, &Attributes, &AttributeLength);
    if (!NT_SUCCESS(Status))
        return Status;

    if (GetList) {
        StartIndex = 0;
    } else if (IrpSp->Flags & SL_INDEX_SPECIFIED) {
        if (!IrpSp->Parameters.QueryEa.EaIndex) {
            Status = STATUS_NONEXISTENT_EA_ENTRY;
            goto Complete;
        }
        StartIndex = IrpSp->Parameters.QueryEa.EaIndex - 1;
    } else if (IrpSp->Flags & SL_RESTART_SCAN) {
        Handle->EaIndex = 0;
        StartIndex = 0;
    } else {
        StartIndex = Handle->EaIndex;
    }

    while (Offset < AttributeLength) {
        PFILE_FULL_EA_INFORMATION Entry;
        ULONG EntryLength;
        ULONG NextOffset;

        if (!NtfsGetEaEntry(Attributes,
                            AttributeLength,
                            Offset,
                            &Entry,
                            &EntryLength,
                            &NextOffset)) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Complete;
        }
        if (Index >= StartIndex &&
            (!GetList ||
             NtfsEaMatchesGetList(
                 Entry, GetList, GetListLength))) {
            Status = NtfsAppendQueryEa(
                Output,
                OutputLength,
                BytesWritten,
                &Previous,
                Entry,
                EntryLength);
            if (!NT_SUCCESS(Status))
                goto Complete;
            Matched = TRUE;
            if (!GetList &&
                !(IrpSp->Flags & SL_INDEX_SPECIFIED))
                Handle->EaIndex = Index + 1;
            if (IrpSp->Flags & SL_RETURN_SINGLE_ENTRY)
                break;
        }
        Index++;
        if (NextOffset <= Offset ||
            NextOffset >= AttributeLength)
            break;
        Offset = NextOffset;
    }

    if (!Matched) {
        Status = GetList ?
            STATUS_NONEXISTENT_EA_ENTRY : STATUS_NO_MORE_EAS;
    } else {
        Status = STATUS_SUCCESS;
    }

Complete:
    ExFreePoolWithTag(Attributes, TAG_NTFS);
    return Status;
}

static PFILE_FULL_EA_INFORMATION
NtfsFindEaUpdate(
    _In_reads_bytes_(InputLength) PFILE_FULL_EA_INFORMATION Input,
    _In_ ULONG InputLength,
    _In_ PFILE_FULL_EA_INFORMATION Name)
{
    ULONG Offset = 0;

    UNREFERENCED_PARAMETER(InputLength);
    for (;;) {
        PFILE_FULL_EA_INFORMATION Current =
            (PFILE_FULL_EA_INFORMATION)((PUCHAR)Input + Offset);

        if (NtfsEaNamesEqual(Current, Name))
            return Current;
        if (!Current->NextEntryOffset)
            return NULL;
        Offset += Current->NextEntryOffset;
    }
}

static BOOLEAN
NtfsInputEaIsDuplicate(
    _In_reads_bytes_(InputLength) PFILE_FULL_EA_INFORMATION Input,
    _In_ ULONG InputLength,
    _In_ ULONG CandidateOffset)
{
    PFILE_FULL_EA_INFORMATION Candidate =
        (PFILE_FULL_EA_INFORMATION)((PUCHAR)Input + CandidateOffset);
    ULONG Offset = 0;

    UNREFERENCED_PARAMETER(InputLength);
    while (Offset < CandidateOffset) {
        PFILE_FULL_EA_INFORMATION Current =
            (PFILE_FULL_EA_INFORMATION)((PUCHAR)Input + Offset);

        if (NtfsEaNamesEqual(Current, Candidate))
            return TRUE;
        if (!Current->NextEntryOffset)
            break;
        Offset += Current->NextEntryOffset;
    }
    return FALSE;
}

static NTSTATUS
NtfsAppendNativeEa(_Out_writes_bytes_(Capacity) PUCHAR Output,
                   _In_ ULONG Capacity,
                   _Inout_ PULONG OutputLength,
                   _In_ PFILE_FULL_EA_INFORMATION Source)
{
    PFILE_FULL_EA_INFORMATION Destination;
    ULONG RecordLength;
    ULONG AlignedLength;

    RecordLength =
        FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) +
        Source->EaNameLength + 1 + Source->EaValueLength;
    AlignedLength = NtfsAlignEaLength(RecordLength);
    if (*OutputLength > Capacity ||
        AlignedLength > Capacity - *OutputLength)
        return STATUS_EA_TOO_LARGE;

    Destination =
        (PFILE_FULL_EA_INFORMATION)(Output + *OutputLength);
    RtlZeroMemory(Destination, AlignedLength);
    Destination->NextEntryOffset = AlignedLength;
    Destination->Flags = Source->Flags;
    Destination->EaNameLength = Source->EaNameLength;
    Destination->EaValueLength = Source->EaValueLength;
    RtlCopyMemory(Destination->EaName,
                  Source->EaName,
                  Source->EaNameLength + 1 +
                      Source->EaValueLength);
    *OutputLength += AlignedLength;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfsSetEaBuffer(_Inout_ PFILE_OBJECT FileObject,
                _In_ NTFS3G_ROS_FILE *CoreFile,
                _In_reads_bytes_(InputLength)
                    PFILE_FULL_EA_INFORMATION Input,
                _In_ ULONG InputLength)
{
    PUCHAR Existing = NULL;
    ULONG ExistingLength = 0;
    PUCHAR Merged = NULL;
    ULONG MergedLength = 0;
    ULONG Capacity;
    ULONG Offset;
    ULONG ErrorOffset;
    NTSTATUS Status;
    int Result;

    Status = IoCheckEaBufferValidity(
        Input, InputLength, &ErrorOffset);
    if (!NT_SUCCESS(Status))
        return Status;

    Offset = 0;
    for (;;) {
        PFILE_FULL_EA_INFORMATION Current =
            (PFILE_FULL_EA_INFORMATION)((PUCHAR)Input + Offset);

        if (NtfsInputEaIsDuplicate(
                Input, InputLength, Offset))
            return STATUS_EA_LIST_INCONSISTENT;
        if (!Current->NextEntryOffset)
            break;
        Offset += Current->NextEntryOffset;
    }

    Status = NtfsReadEaBuffer(
        CoreFile, &Existing, &ExistingLength);
    if (Status == STATUS_NO_EAS_ON_FILE) {
        Status = STATUS_SUCCESS;
        ExistingLength = 0;
    } else if (!NT_SUCCESS(Status)) {
        return Status;
    }

    if (ExistingLength >
        MAXUSHORT - InputLength - NTFS_EA_ALIGNMENT) {
        Status = STATUS_EA_TOO_LARGE;
        goto Complete;
    }
    Capacity = ExistingLength + InputLength +
               NTFS_EA_ALIGNMENT;
    Merged = ExAllocatePoolWithTag(
        PagedPool, Capacity, TAG_NTFS);
    if (!Merged) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Complete;
    }

    Offset = 0;
    while (Offset < ExistingLength) {
        PFILE_FULL_EA_INFORMATION Current;
        PFILE_FULL_EA_INFORMATION Update;
        ULONG CurrentLength;
        ULONG NextOffset;

        if (!NtfsGetEaEntry(Existing,
                            ExistingLength,
                            Offset,
                            &Current,
                            &CurrentLength,
                            &NextOffset)) {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Complete;
        }
        Update = NtfsFindEaUpdate(
            Input, InputLength, Current);
        if (Update) {
            if (Update->EaValueLength) {
                Status = NtfsAppendNativeEa(
                    Merged, Capacity,
                    &MergedLength, Update);
            } else {
                Status = STATUS_SUCCESS;
            }
        } else {
            Status = NtfsAppendNativeEa(
                Merged, Capacity,
                &MergedLength, Current);
        }
        if (!NT_SUCCESS(Status))
            goto Complete;
        if (NextOffset <= Offset ||
            NextOffset >= ExistingLength)
            break;
        Offset = NextOffset;
    }

    Offset = 0;
    for (;;) {
        PFILE_FULL_EA_INFORMATION Current =
            (PFILE_FULL_EA_INFORMATION)((PUCHAR)Input + Offset);
        BOOLEAN Exists = FALSE;
        ULONG ExistingOffset = 0;

        while (ExistingOffset < ExistingLength) {
            PFILE_FULL_EA_INFORMATION ExistingEntry;
            ULONG ExistingEntryLength;
            ULONG NextExistingOffset;

            if (!NtfsGetEaEntry(Existing,
                                ExistingLength,
                                ExistingOffset,
                                &ExistingEntry,
                                &ExistingEntryLength,
                                &NextExistingOffset)) {
                Status = STATUS_FILE_CORRUPT_ERROR;
                goto Complete;
            }
            if (NtfsEaNamesEqual(Current, ExistingEntry)) {
                Exists = TRUE;
                break;
            }
            if (NextExistingOffset <= ExistingOffset ||
                NextExistingOffset >= ExistingLength)
                break;
            ExistingOffset = NextExistingOffset;
        }
        if (!Exists && Current->EaValueLength) {
            Status = NtfsAppendNativeEa(
                Merged, Capacity, &MergedLength, Current);
            if (!NT_SUCCESS(Status))
                goto Complete;
        }
        if (!Current->NextEntryOffset)
            break;
        Offset += Current->NextEntryOffset;
    }

    if (!MergedLength) {
        if (!ExistingLength) {
            Status = STATUS_SUCCESS;
        } else {
            Result = Ntfs3gRosRemoveExtendedAttributes(
                CoreFile);
            Status = Result == -ENODATA ?
                STATUS_SUCCESS :
                (Result < 0 ?
                 Ntfs3gRosStatusFromError(-Result) :
                 STATUS_SUCCESS);
        }
    } else {
        Result = Ntfs3gRosSetExtendedAttributes(
            CoreFile, Merged, MergedLength);
        Status = Result < 0 ?
            Ntfs3gRosStatusFromError(-Result) :
            STATUS_SUCCESS;
    }
    if (NT_SUCCESS(Status))
        FileObject->Flags |= FO_FILE_MODIFIED;

Complete:
    if (Merged)
        ExFreePoolWithTag(Merged, TAG_NTFS);
    if (Existing)
        ExFreePoolWithTag(Existing, TAG_NTFS);
    return Status;
}

NTSTATUS
NTAPI
NtfsFsdQueryEa(_In_ PDEVICE_OBJECT DeviceObject,
               _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp =
        IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFileContextBlock File;
    PHandleContextBlock Handle;
    NTFS3G_ROS_FILE *CoreFile;
    ULONG BytesWritten = 0;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (!FileObject ||
        !(File = FileObject->FsContext) ||
        !(Handle = FileObject->FsContext2))
        return NtfsCompleteRequest(
            Irp, STATUS_INVALID_PARAMETER, 0);
    if (Handle->CleanupComplete)
        return NtfsCompleteRequest(
            Irp, STATUS_FILE_CLOSED, 0);
    if (File->IsVolume)
        return NtfsCompleteRequest(
            Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (Irp->RequestorMode != KernelMode &&
        !(Handle->DesiredAccess & FILE_READ_EA))
        return NtfsCompleteRequest(
            Irp, STATUS_ACCESS_DENIED, 0);
    CoreFile = File->File ?
        File->File : Handle->DirectoryFile;
    if (!CoreFile)
        return NtfsCompleteRequest(
            Irp, STATUS_INVALID_PARAMETER, 0);

    KeEnterCriticalRegion();
    ExAcquireResourceSharedLite(&File->MainResource, TRUE);
    Status = File->Volume->ShutdownStarted ?
        STATUS_SYSTEM_SHUTDOWN :
        NtfsQueryEaLocked(Irp,
                          IrpSp,
                          Handle,
                          CoreFile,
                          &BytesWritten);
    ExReleaseResourceLite(&File->MainResource);
    KeLeaveCriticalRegion();
    return NtfsCompleteRequest(
        Irp, Status,
        NT_SUCCESS(Status) ||
        Status == STATUS_BUFFER_OVERFLOW ?
            BytesWritten : 0);
}

NTSTATUS
NTAPI
NtfsFsdSetEa(_In_ PDEVICE_OBJECT DeviceObject,
             _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp =
        IoGetCurrentIrpStackLocation(Irp);
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PFileContextBlock File;
    PHandleContextBlock Handle;
    PFILE_FULL_EA_INFORMATION Input = GetBuffer(Irp);
    NTFS3G_ROS_FILE *CoreFile;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);
    if (!FileObject ||
        !(File = FileObject->FsContext) ||
        !(Handle = FileObject->FsContext2))
        return NtfsCompleteRequest(
            Irp, STATUS_INVALID_PARAMETER, 0);
    if (Handle->CleanupComplete)
        return NtfsCompleteRequest(
            Irp, STATUS_FILE_CLOSED, 0);
    if (File->IsVolume)
        return NtfsCompleteRequest(
            Irp, STATUS_INVALID_DEVICE_REQUEST, 0);
    if (Irp->RequestorMode != KernelMode &&
        !(Handle->DesiredAccess & FILE_WRITE_EA))
        return NtfsCompleteRequest(
            Irp, STATUS_ACCESS_DENIED, 0);
    if (!Input || !IrpSp->Parameters.SetEa.Length)
        return NtfsCompleteRequest(
            Irp, STATUS_INVALID_USER_BUFFER, 0);
    CoreFile = File->File ?
        File->File : Handle->DirectoryFile;
    if (!CoreFile)
        return NtfsCompleteRequest(
            Irp, STATUS_INVALID_PARAMETER, 0);

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&File->MainResource, TRUE);
    if (File->Volume->ShutdownStarted) {
        Status = STATUS_SYSTEM_SHUTDOWN;
    } else if (Ntfs3gRosIsReadOnly(
                   File->Volume->Volume)) {
        Status = STATUS_MEDIA_WRITE_PROTECTED;
    } else {
        Status = NtfsSetEaBuffer(
            FileObject,
            CoreFile,
            Input,
            IrpSp->Parameters.SetEa.Length);
    }
    ExReleaseResourceLite(&File->MainResource);
    KeLeaveCriticalRegion();
    return NtfsCompleteRequest(Irp, Status, 0);
}
