/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Source file for the ntfs_new ea
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfspch.h"

#define NDEBUG

/* GLOBALS *****************************************************************/

#ifdef ALLOC_PRAGMA
#pragma alloc_text(PAGE, NtfsFsdQueryEa)
#pragma alloc_text(PAGE, NtfsFsdSetEa)
#endif

/* FUNCTIONS ****************************************************************/

static
BOOLEAN
NtfsIsValidEaNameCharacter(_In_ UCHAR Character)
{
    if (Character < 0x20 || Character > 0x7e)
        return FALSE;

    switch (Character)
    {
        case '\\':
        case '/':
        case ':':
        case '*':
        case '?':
        case '"':
        case '<':
        case '>':
        case '|':
        case ',':
        case '+':
        case '=':
        case '[':
        case ']':
        case ';':
            return FALSE;

        default:
            return TRUE;
    }
}

static
UCHAR
NtfsUpperEaCharacter(_In_ UCHAR Character)
{
    if (Character >= 'a' && Character <= 'z')
        return Character - ('a' - 'A');
    return Character;
}

static
BOOLEAN
NtfsEaNamesEqual(_In_reads_bytes_(LeftLength) const UCHAR* Left,
                 _In_ UCHAR LeftLength,
                 _In_reads_bytes_(RightLength) const UCHAR* Right,
                 _In_ UCHAR RightLength)
{
    ULONG Index;

    if (LeftLength != RightLength)
        return FALSE;
    for (Index = 0; Index < LeftLength; Index++)
    {
        if (NtfsUpperEaCharacter(Left[Index]) !=
            NtfsUpperEaCharacter(Right[Index]))
        {
            return FALSE;
        }
    }
    return TRUE;
}

static
NTSTATUS
NtfsValidateGetEaList(_In_ PVOID EaList,
                      _In_ ULONG EaListLength)
{
    PFILE_GET_EA_INFORMATION Entry;
    ULONG EntryLength;
    ULONG NextEntryOffset;
    ULONG Offset = 0;
    ULONG Remaining;
    ULONG Index;

    if (!EaList || EaListLength == 0)
        return STATUS_EA_LIST_INCONSISTENT;

    for (;;)
    {
        Remaining = EaListLength - Offset;
        if (Remaining < FIELD_OFFSET(FILE_GET_EA_INFORMATION, EaName))
            return STATUS_EA_LIST_INCONSISTENT;

        Entry = (PFILE_GET_EA_INFORMATION)((PUCHAR)EaList + Offset);
        if (Entry->EaNameLength == 0)
            return STATUS_INVALID_EA_NAME;

        EntryLength = FIELD_OFFSET(FILE_GET_EA_INFORMATION, EaName) +
                      Entry->EaNameLength;
        if (EntryLength > Remaining)
            return STATUS_EA_LIST_INCONSISTENT;
        for (Index = 0; Index < Entry->EaNameLength; Index++)
        {
            if (!NtfsIsValidEaNameCharacter(
                    (UCHAR)Entry->EaName[Index]))
            {
                return STATUS_INVALID_EA_NAME;
            }
        }

        NextEntryOffset = Entry->NextEntryOffset;
        if (NextEntryOffset == 0)
            return STATUS_SUCCESS;
        if ((NextEntryOffset & (sizeof(ULONG) - 1)) != 0 ||
            NextEntryOffset < EntryLength ||
            NextEntryOffset > Remaining)
        {
            return STATUS_EA_LIST_INCONSISTENT;
        }
        Offset += NextEntryOffset;
    }
}

static
NTSTATUS
NtfsFindEaByName(_In_reads_bytes_opt_(RawLength) const UCHAR* RawData,
                 _In_ ULONG RawLength,
                 _In_reads_bytes_(NameLength) const UCHAR* Name,
                 _In_ UCHAR NameLength,
                 _Out_ PBOOLEAN Found,
                 _Out_ PNtfsExtendedAttributeView View)
{
    ULONG RawOffset = 0;
    NTSTATUS Status;

    *Found = FALSE;
    RtlZeroMemory(View, sizeof(*View));
    while (RawOffset < RawLength)
    {
        Status = NtfsGetNextExtendedAttribute(RawData,
                                              RawLength,
                                              &RawOffset,
                                              View);
        if (!NT_SUCCESS(Status))
            return Status;
        if (NtfsEaNamesEqual(View->Name,
                             View->NameLength,
                             Name,
                             NameLength))
        {
            *Found = TRUE;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_SUCCESS;
}

static
BOOLEAN
NtfsEaRequestIsDuplicate(
    _In_ PFILE_GET_EA_INFORMATION CurrentEntry,
    _In_reads_bytes_(EaListLength) PVOID EaList,
    _In_ ULONG EaListLength)
{
    PFILE_GET_EA_INFORMATION Entry;
    ULONG Offset = 0;

    while (Offset < EaListLength)
    {
        Entry = (PFILE_GET_EA_INFORMATION)((PUCHAR)EaList + Offset);
        if (Entry == CurrentEntry)
            return FALSE;
        if (NtfsEaNamesEqual((const UCHAR*)Entry->EaName,
                             Entry->EaNameLength,
                             (const UCHAR*)CurrentEntry->EaName,
                             CurrentEntry->EaNameLength))
        {
            return TRUE;
        }
        if (Entry->NextEntryOffset == 0 ||
            Entry->NextEntryOffset > EaListLength - Offset)
        {
            return FALSE;
        }
        Offset += Entry->NextEntryOffset;
    }

    return FALSE;
}

static
NTSTATUS
NtfsAppendEa(_Out_writes_bytes_(OutputLength) PUCHAR Output,
             _In_ ULONG OutputLength,
             _In_ const NtfsExtendedAttributeView* View,
             _Inout_ PULONG BytesWritten,
             _Inout_ PFILE_FULL_EA_INFORMATION* LastEntry)
{
    PFILE_FULL_EA_INFORMATION CurrentEntry;
    ULONG AlignedOffset;
    ULONG EntryLength;

    EntryLength = FIELD_OFFSET(FILE_FULL_EA_INFORMATION, EaName) +
                  View->NameLength + 1 + View->ValueLength;
    AlignedOffset = ALIGN_UP_BY(*BytesWritten, sizeof(ULONG));
    if (AlignedOffset > OutputLength ||
        EntryLength > OutputLength - AlignedOffset)
    {
        return *BytesWritten == 0
            ? STATUS_BUFFER_TOO_SMALL
            : STATUS_BUFFER_OVERFLOW;
    }

    if (AlignedOffset > *BytesWritten)
    {
        RtlZeroMemory(Output + *BytesWritten,
                      AlignedOffset - *BytesWritten);
    }

    CurrentEntry =
        (PFILE_FULL_EA_INFORMATION)(Output + AlignedOffset);
    if (*LastEntry)
    {
        (*LastEntry)->NextEntryOffset =
            (ULONG)((PUCHAR)CurrentEntry - (PUCHAR)*LastEntry);
    }

    CurrentEntry->NextEntryOffset = 0;
    CurrentEntry->Flags = View->Flags;
    CurrentEntry->EaNameLength = View->NameLength;
    CurrentEntry->EaValueLength = View->ValueLength;
    RtlCopyMemory(CurrentEntry->EaName,
                  View->Name,
                  View->NameLength);
    CurrentEntry->EaName[View->NameLength] = ANSI_NULL;
    if (View->ValueLength != 0)
    {
        RtlCopyMemory(CurrentEntry->EaName + View->NameLength + 1,
                      View->Value,
                      View->ValueLength);
    }

    *LastEntry = CurrentEntry;
    *BytesWritten = AlignedOffset + EntryLength;
    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfsQueryEa(_In_ PFileContextBlock FileCB,
            _In_ PIO_STACK_LOCATION IrpSp,
            _Out_writes_bytes_(IrpSp->Parameters.QueryEa.Length) PUCHAR Output,
            _Out_ PULONG BytesWritten)
{
    PFILE_FULL_EA_INFORMATION LastEntry = NULL;
    NtfsExtendedAttributeView View;
    PUCHAR RawData = NULL;
    ULONG RawLength = 0;
    ULONG RawOffset = 0;
    ULONG CurrentIndex = 1;
    ULONG StartIndex;
    ULONG ReturnedCount = 0;
    PFILE_GET_EA_INFORMATION RequestedEntry;
    ULONG RequestedOffset;
    BOOLEAN Found;
    BOOLEAN ExplicitIndex;
    BOOLEAN HasEaList;
    NTSTATUS Status;

    *BytesWritten = 0;
    HasEaList = IrpSp->Parameters.QueryEa.EaList != NULL &&
                IrpSp->Parameters.QueryEa.EaListLength != 0;
    ExplicitIndex = !HasEaList &&
                    !!(IrpSp->Flags & SL_INDEX_SPECIFIED);

    if (HasEaList)
    {
        Status = NtfsValidateGetEaList(
            IrpSp->Parameters.QueryEa.EaList,
            IrpSp->Parameters.QueryEa.EaListLength);
        if (!NT_SUCCESS(Status))
            return Status;
    }
    else if (IrpSp->Parameters.QueryEa.EaList != NULL ||
             IrpSp->Parameters.QueryEa.EaListLength != 0)
    {
        return STATUS_EA_LIST_INCONSISTENT;
    }

    if (ExplicitIndex)
    {
        StartIndex = IrpSp->Parameters.QueryEa.EaIndex;
        if (StartIndex == 0)
            return STATUS_NONEXISTENT_EA_ENTRY;
    }
    else if (!HasEaList)
    {
        if (IrpSp->Flags & SL_RESTART_SCAN)
            FileCB->EaIndex = 1;
        if (FileCB->EaIndex == 0)
            FileCB->EaIndex = 1;
        StartIndex = FileCB->EaIndex;
    }
    else
    {
        StartIndex = 1;
    }

    Status = NtfsFileRecordReadExtendedAttributes(FileCB->FileRec,
                                                   NULL,
                                                   &RawLength,
                                                   NULL);
    if (Status == STATUS_NO_EAS_ON_FILE && HasEaList)
    {
        Status = STATUS_SUCCESS;
        RawLength = 0;
    }
    else if (Status == STATUS_NO_EAS_ON_FILE && ExplicitIndex)
    {
        return STATUS_NONEXISTENT_EA_ENTRY;
    }
    if (Status != STATUS_BUFFER_TOO_SMALL)
    {
        if (!NT_SUCCESS(Status))
            return Status;
    }

    if (RawLength != 0)
    {
        RawData = ExAllocatePoolWithTag(PagedPool,
                                        RawLength,
                                        TAG_NTFS);
        if (!RawData)
            return STATUS_INSUFFICIENT_RESOURCES;

        Status = NtfsFileRecordReadExtendedAttributes(FileCB->FileRec,
                                                       RawData,
                                                       &RawLength,
                                                       NULL);
        if (!NT_SUCCESS(Status))
            goto Done;
    }

    if (HasEaList)
    {
        RequestedOffset = 0;
        for (;;)
        {
            RequestedEntry =
                (PFILE_GET_EA_INFORMATION)(
                    (PUCHAR)IrpSp->Parameters.QueryEa.EaList +
                    RequestedOffset);
            if (!NtfsEaRequestIsDuplicate(
                    RequestedEntry,
                    IrpSp->Parameters.QueryEa.EaList,
                    IrpSp->Parameters.QueryEa.EaListLength))
            {
                Status = NtfsFindEaByName(
                    RawData,
                    RawLength,
                    (const UCHAR*)RequestedEntry->EaName,
                    RequestedEntry->EaNameLength,
                    &Found,
                    &View);
                if (!NT_SUCCESS(Status))
                    goto Done;
                if (!Found)
                {
                    View.Flags = 0;
                    View.NameLength = RequestedEntry->EaNameLength;
                    View.ValueLength = 0;
                    View.Name = (const UCHAR*)RequestedEntry->EaName;
                    View.Value = NULL;
                }

                Status = NtfsAppendEa(
                    Output,
                    IrpSp->Parameters.QueryEa.Length,
                    &View,
                    BytesWritten,
                    &LastEntry);
                if (!NT_SUCCESS(Status))
                    goto Done;
                ReturnedCount++;
                if (IrpSp->Flags & SL_RETURN_SINGLE_ENTRY)
                    break;
            }

            if (RequestedEntry->NextEntryOffset == 0)
                break;
            RequestedOffset += RequestedEntry->NextEntryOffset;
        }

        Status = STATUS_SUCCESS;
        goto Done;
    }

    for (;;)
    {
        Status = NtfsGetNextExtendedAttribute(RawData,
                                              RawLength,
                                              &RawOffset,
                                              &View);
        if (Status == STATUS_NO_MORE_EAS)
            break;
        if (!NT_SUCCESS(Status))
            goto Done;

        if (CurrentIndex < StartIndex)
        {
            CurrentIndex++;
            continue;
        }

        Status = NtfsAppendEa(Output,
                              IrpSp->Parameters.QueryEa.Length,
                              &View,
                              BytesWritten,
                              &LastEntry);
        if (!NT_SUCCESS(Status))
            goto Done;

        ReturnedCount++;
        if (!ExplicitIndex)
            FileCB->EaIndex = CurrentIndex + 1;
        if (IrpSp->Flags & SL_RETURN_SINGLE_ENTRY)
            break;

        CurrentIndex++;
    }

    if (ReturnedCount != 0)
    {
        Status = STATUS_SUCCESS;
    }
    else
    {
        Status = ExplicitIndex
            ? STATUS_NONEXISTENT_EA_ENTRY
            : STATUS_NO_MORE_EAS;
    }

Done:
    if (RawData)
        ExFreePoolWithTag(RawData, TAG_NTFS);
    return Status;
}

_Function_class_(IRP_MJ_QUERY_EA)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdQueryEa(_In_ PDEVICE_OBJECT VolumeDeviceObject,
              _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * Reads extended attributes of files.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-query-ea
     */

    PIO_STACK_LOCATION IrpSp;
    PFileContextBlock FileCB;
    PUCHAR Output;
    ULONG BytesWritten = 0;
    NTSTATUS Status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(VolumeDeviceObject);

    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    Irp->IoStatus.Information = 0;
    if (!IrpSp->FileObject ||
        !IrpSp->FileObject->FsContext)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Done;
    }

    FileCB = (PFileContextBlock)IrpSp->FileObject->FsContext;
    if (!FileCB->FileRec)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Done;
    }
    if (Irp->RequestorMode == UserMode &&
        !(FileCB->DesiredAccess & FILE_READ_EA))
    {
        Status = STATUS_ACCESS_DENIED;
        goto Done;
    }

    Output = (PUCHAR)GetBuffer(Irp);
    if (IrpSp->Parameters.QueryEa.Length != 0 && !Output)
    {
        Status = STATUS_INVALID_USER_BUFFER;
        goto Done;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(&FileCB->MainResource, TRUE);
    Status = NtfsQueryEa(FileCB,
                         IrpSp,
                         Output,
                         &BytesWritten);
    ExReleaseResourceLite(&FileCB->MainResource);
    KeLeaveCriticalRegion();

Done:
    Irp->IoStatus.Status = Status;
    if (NT_SUCCESS(Status) || Status == STATUS_BUFFER_OVERFLOW)
        Irp->IoStatus.Information = BytesWritten;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}

_Function_class_(IRP_MJ_SET_EA)
_Function_class_(DRIVER_DISPATCH)
NTSTATUS
NTAPI
NtfsFsdSetEa(_In_ PDEVICE_OBJECT VolumeDeviceObject,
            _Inout_ PIRP Irp)
{
    if (VolumeDeviceObject != NtfsDiskFileSystemDeviceObject)
        NtfsBindVolumeDisk((PVolumeContextBlock)VolumeDeviceObject->DeviceExtension);
    /* Overview:
     * Sets extended attributes of files.
     * See: https://learn.microsoft.com/en-us/windows-hardware/drivers/ifs/irp-mj-set-ea
     */

    PFILE_FULL_EA_INFORMATION Entry;
    PFileContextBlock FileCB;
    PFILE_OBJECT FileObject;
    PIO_STACK_LOCATION IrpSp;
    PVolumeContextBlock VolCB;
    NtfsExtendedAttributeUpdate* Updates = NULL;
    PUCHAR Input;
    ULONG EntryCount = 0;
    ULONG EntryIndex;
    ULONG ErrorOffset = 0;
    ULONG InputLength;
    ULONG NameIndex;
    ULONG Offset;
    BOOLEAN ResourceAcquired = FALSE;
    NTSTATUS Status;

    PAGED_CODE();

    Irp->IoStatus.Information = 0;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    FileObject = IrpSp->FileObject;
    FileCB = FileObject
        ? (PFileContextBlock)FileObject->FsContext
        : NULL;
    VolCB = VolumeDeviceObject
        ? (PVolumeContextBlock)
            VolumeDeviceObject->DeviceExtension
        : NULL;
    if (!FileObject || !FileCB || !FileCB->FileRec ||
        !VolCB || !VolCB->DiskVolume)
    {
        Status = STATUS_INVALID_PARAMETER;
        goto Done;
    }
    if (Irp->RequestorMode == UserMode &&
        !(FileCB->DesiredAccess & FILE_WRITE_EA))
    {
        Status = STATUS_ACCESS_DENIED;
        goto Done;
    }
    if (NtfsVolumeIsReadOnly(VolCB->DiskVolume))
    {
        Status = STATUS_MEDIA_WRITE_PROTECTED;
        goto Done;
    }

    InputLength = IrpSp->Parameters.SetEa.Length;
    Input = (PUCHAR)GetBuffer(Irp);
    if (InputLength == 0)
    {
        Status = STATUS_EA_LIST_INCONSISTENT;
        goto Done;
    }
    if (!Input)
    {
        Status = STATUS_INVALID_USER_BUFFER;
        goto Done;
    }

    Status = IoCheckEaBufferValidity(
        (PFILE_FULL_EA_INFORMATION)Input,
        InputLength,
        &ErrorOffset);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Information = ErrorOffset;
        goto Done;
    }

    Offset = 0;
    for (;;)
    {
        Entry = (PFILE_FULL_EA_INFORMATION)
            (Input + Offset);
        EntryCount++;
        if (Entry->NextEntryOffset == 0)
            break;
        Offset += Entry->NextEntryOffset;
    }
    if (EntryCount >
        MAXULONG / sizeof(*Updates))
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    Updates = ExAllocatePoolWithTag(
        PagedPool,
        EntryCount * sizeof(*Updates),
        TAG_NTFS);
    if (!Updates)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Done;
    }

    Offset = 0;
    for (EntryIndex = 0;
         EntryIndex < EntryCount;
         EntryIndex++)
    {
        Entry = (PFILE_FULL_EA_INFORMATION)
            (Input + Offset);
        if (Entry->EaNameLength == 0 ||
            Entry->EaNameLength == 0xff)
        {
            Status = STATUS_INVALID_EA_NAME;
            Irp->IoStatus.Information = Offset;
            goto Done;
        }
        if (Entry->Flags != 0 &&
            Entry->Flags != FILE_NEED_EA)
        {
            Status = STATUS_INVALID_EA_NAME;
            Irp->IoStatus.Information = Offset;
            goto Done;
        }
        for (NameIndex = 0;
             NameIndex < Entry->EaNameLength;
             NameIndex++)
        {
            if (!NtfsIsValidEaNameCharacter(
                    (UCHAR)Entry->EaName[NameIndex]))
            {
                Status = STATUS_INVALID_EA_NAME;
                Irp->IoStatus.Information = Offset;
                goto Done;
            }
        }

        Updates[EntryIndex].Operation =
            Entry->EaValueLength == 0
                ? NTFS_EA_UPDATE_REMOVE
                : NTFS_EA_UPDATE_SET;
        Updates[EntryIndex].Flags = Entry->Flags;
        Updates[EntryIndex].NameLength =
            Entry->EaNameLength;
        Updates[EntryIndex].ValueLength =
            Entry->EaValueLength;
        Updates[EntryIndex].Name =
            (const UCHAR*)Entry->EaName;
        Updates[EntryIndex].Value =
            (const UCHAR*)Entry->EaName +
            Entry->EaNameLength + 1;

        if (Entry->NextEntryOffset != 0)
            Offset += Entry->NextEntryOffset;
    }

    KeEnterCriticalRegion();
    ExAcquireResourceExclusiveLite(
        &FileCB->MainResource,
        TRUE);
    ResourceAcquired = TRUE;
    Status = NtfsFileRecordUpdateExtendedAttributes(
        FileCB->FileRec,
        Updates,
        EntryCount);
    if (NT_SUCCESS(Status))
    {
        FileCB->EaIndex = 1;
        FileObject->Flags |= FO_FILE_MODIFIED;
    }

Done:
    if (ResourceAcquired)
    {
        ExReleaseResourceLite(&FileCB->MainResource);
        KeLeaveCriticalRegion();
    }
    if (Updates)
        ExFreePoolWithTag(Updates, TAG_NTFS);
    Irp->IoStatus.Status = Status;
    if (NT_SUCCESS(Status))
        Irp->IoStatus.Information = 0;
    IoCompleteRequest(Irp, IO_DISK_INCREMENT);
    return Status;
}
