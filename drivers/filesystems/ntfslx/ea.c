/*
 * Extended attribute (EA) reading for the ntfslx ReactOS driver.
 *
 * Reads $EA_INFORMATION (type 0xD0) and $EA (type 0xE0) attributes.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

static
NTSTATUS
NtfslxReadNonResidentAttributeBuffer(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_ATTR_RECORD Attribute,
    _Outptr_ PVOID *Buffer,
    _Out_ PULONG BufferLength)
{
    PNTFSLX_RUNLIST_ELEMENT Runlist = NULL;
    ULONG RunlistCount = 0;
    PVOID LocalBuffer = NULL;
    ULONG Length;
    NTSTATUS Status;

    *Buffer = NULL;
    *BufferLength = 0;

    if (Attribute == NULL || Attribute->NonResident == 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if ((Attribute->Flags &
         (NTFSLX_ATTR_IS_COMPRESSED |
          NTFSLX_ATTR_IS_SPARSE |
          NTFSLX_ATTR_IS_ENCRYPTED)) != 0 ||
        Attribute->Data.NonResident.CompressionUnit != 0)
    {
        return STATUS_NOT_SUPPORTED;
    }

    if (Attribute->Data.NonResident.DataSize > MAXULONG)
    {
        return STATUS_FILE_CORRUPT_ERROR;
    }

    Length = (ULONG)Attribute->Data.NonResident.DataSize;
    if (Length == 0)
    {
        return STATUS_SUCCESS;
    }

    Status = NtfslxMappingPairsDecompress(VolumeInfo,
                                          Attribute,
                                          &Runlist,
                                          &RunlistCount);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    if (RunlistCount == 0)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }

    LocalBuffer = ExAllocatePoolWithTag(NonPagedPool, Length, NTFSLX_TAG);
    if (LocalBuffer == NULL)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Cleanup;
    }

    Status = NtfslxReadMappedAttributeData(StorageDevice,
                                           VolumeInfo,
                                           Runlist,
                                           0,
                                           Length,
                                           (PUCHAR)LocalBuffer);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    *Buffer = LocalBuffer;
    *BufferLength = Length;
    LocalBuffer = NULL;
    Status = STATUS_SUCCESS;

Cleanup:
    if (LocalBuffer != NULL)
    {
        ExFreePoolWithTag(LocalBuffer, NTFSLX_TAG);
    }
    if (Runlist != NULL)
    {
        ExFreePoolWithTag(Runlist, NTFSLX_TAG);
    }

    return Status;
}

static
NTSTATUS
NtfslxValidateEaBuffer(
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length)
{
    ULONG Offset;
    ULONG EntryBytes;
    const NTFSLX_EA_ENTRY *Entry;

    if (Buffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (Offset = 0; Offset < Length; )
    {
        if (Length - Offset < sizeof(NTFSLX_EA_ENTRY))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        Entry = (const NTFSLX_EA_ENTRY *)(Buffer + Offset);
        EntryBytes = sizeof(NTFSLX_EA_ENTRY) + Entry->EaNameLength + 1 + Entry->EaValueLength;
        if (EntryBytes < sizeof(NTFSLX_EA_ENTRY) ||
            EntryBytes > (Length - Offset))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        if (Entry->NextEntryOffset != 0)
        {
            if (Entry->NextEntryOffset < EntryBytes ||
                Entry->NextEntryOffset > (Length - Offset))
            {
                return STATUS_FILE_CORRUPT_ERROR;
            }

            Offset += Entry->NextEntryOffset;
        }
        else
        {
            return STATUS_SUCCESS;
        }
    }

    return STATUS_FILE_CORRUPT_ERROR;
}

/*
 * NtfslxReadEaInformation - read $EA_INFORMATION from an MFT record
 *
 * Returns the EA metadata (packed size, need-EA count, query size).
 */
NTSTATUS
NtfslxReadEaInformation(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG MftIndex,
    _Out_ PNTFSLX_EA_INFORMATION EaInfo)
{
    NTSTATUS Status;
    PNTFSLX_MFT_RECORD FileRecord = NULL;
    PNTFSLX_ATTR_RECORD EaInfoAttr = NULL;
    PNTFSLX_EA_INFORMATION EaInfoValue;

    RtlZeroMemory(EaInfo, sizeof(*EaInfo));

    FileRecord = ExAllocatePoolWithTag(
        NonPagedPool, VolumeInfo->BytesPerFileRecord, NTFSLX_TAG);
    if (FileRecord == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = NtfslxReadMftRecord(
        StorageDevice, VolumeInfo, MftRunlist,
        MftIndex, FileRecord);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = NtfslxFindAttribute(
        FileRecord, NTFSLX_ATTRIBUTE_EA_INFORMATION,
        NULL, 0, &EaInfoAttr);
    if (!NT_SUCCESS(Status))
    {
        /* No EA_INFORMATION means no EAs */
        Status = STATUS_NOT_FOUND;
        goto Cleanup;
    }

    if (EaInfoAttr->NonResident != 0 ||
        EaInfoAttr->Data.Resident.ValueLength < sizeof(NTFSLX_EA_INFORMATION))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }

    EaInfoValue = (PNTFSLX_EA_INFORMATION)(
        (PUCHAR)EaInfoAttr + EaInfoAttr->Data.Resident.ValueOffset);

    *EaInfo = *EaInfoValue;
    Status = STATUS_SUCCESS;

Cleanup:
    if (FileRecord != NULL)
        ExFreePoolWithTag(FileRecord, NTFSLX_TAG);

    return Status;
}

/*
 * NtfslxReadEaData - read $EA attribute data
 *
 * Returns the raw packed EA entries buffer.
 * Caller must free *EaBuffer with ExFreePoolWithTag.
 */
NTSTATUS
NtfslxReadEaData(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG MftIndex,
    _Outptr_ PVOID *EaBuffer,
    _Out_ PULONG EaBufferLength)
{
    NTSTATUS Status;
    PNTFSLX_MFT_RECORD FileRecord = NULL;
    PNTFSLX_ATTR_RECORD EaAttr = NULL;
    PVOID Buffer = NULL;
    ULONG ValueLength;

    *EaBuffer = NULL;
    *EaBufferLength = 0;

    FileRecord = ExAllocatePoolWithTag(
        NonPagedPool, VolumeInfo->BytesPerFileRecord, NTFSLX_TAG);
    if (FileRecord == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = NtfslxReadMftRecord(
        StorageDevice, VolumeInfo, MftRunlist,
        MftIndex, FileRecord);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    Status = NtfslxFindAttribute(
        FileRecord, NTFSLX_ATTRIBUTE_EA,
        NULL, 0, &EaAttr);
    if (!NT_SUCCESS(Status))
    {
        Status = STATUS_NOT_FOUND;
        goto Cleanup;
    }

    if (EaAttr->NonResident != 0)
    {
        Status = NtfslxReadNonResidentAttributeBuffer(StorageDevice,
                                                      VolumeInfo,
                                                      EaAttr,
                                                      &Buffer,
                                                      &ValueLength);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }
    }
    else
    {
        ValueLength = EaAttr->Data.Resident.ValueLength;
        if (ValueLength == 0)
        {
            Status = STATUS_NOT_FOUND;
            goto Cleanup;
        }

        Buffer = ExAllocatePoolWithTag(NonPagedPool, ValueLength, NTFSLX_TAG);
        if (Buffer == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        RtlCopyMemory(Buffer,
            (PUCHAR)EaAttr + EaAttr->Data.Resident.ValueOffset,
            ValueLength);
    }

    if (ValueLength == 0)
    {
        Status = STATUS_NOT_FOUND;
        goto Cleanup;
    }

    Status = NtfslxValidateEaBuffer((const UCHAR *)Buffer, ValueLength);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    *EaBuffer = Buffer;
    *EaBufferLength = ValueLength;
    Buffer = NULL;
    Status = STATUS_SUCCESS;

Cleanup:
    if (Buffer != NULL)
        ExFreePoolWithTag(Buffer, NTFSLX_TAG);
    if (FileRecord != NULL)
        ExFreePoolWithTag(FileRecord, NTFSLX_TAG);

    return Status;
}

static
NTSTATUS
NtfslxValidateEaFileObject(
    _In_ PFILE_OBJECT FileObject,
    _Outptr_ PNTFSLX_FILE_CONTEXT *FileContext)
{
    PNTFSLX_FILE_CONTEXT Context;

    if (FileContext != NULL)
    {
        *FileContext = NULL;
    }

    if (FileObject == NULL || FileObject->FsContext == NULL)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    Context = (PNTFSLX_FILE_CONTEXT)FileObject->FsContext;
    if (Context->Signature != NTFSLX_FILE_CONTEXT_SIGNATURE ||
        Context->DeviceExtension == NULL ||
        Context->DeviceExtension->Signature != NTFSLX_TAG ||
        Context->DeviceExtension->StorageDevice == NULL ||
        Context->DeviceExtension->MftRunlist == NULL ||
        Context->FileRecord == NULL)
    {
        return STATUS_INVALID_DEVICE_REQUEST;
    }

    if (FileContext != NULL)
    {
        *FileContext = Context;
    }

    return STATUS_SUCCESS;
}

static
PVOID
NtfslxGetEaIrpBuffer(
    _In_ PIRP Irp)
{
    if (Irp->AssociatedIrp.SystemBuffer != NULL)
    {
        return Irp->AssociatedIrp.SystemBuffer;
    }

    if (Irp->MdlAddress != NULL)
    {
        return MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    }

    return Irp->UserBuffer;
}

static
ULONG
NtfslxPackedEaQueryLength(
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length,
    _Out_ PUSHORT NeedEaCount)
{
    ULONG Offset;
    ULONG Total;

    Total = 0;
    *NeedEaCount = 0;

    for (Offset = 0; Offset < Length; )
    {
        const NTFSLX_EA_ENTRY *Entry;
        ULONG QueryBytes;

        Entry = (const NTFSLX_EA_ENTRY *)(Buffer + Offset);
        QueryBytes = FIELD_OFFSET(FILE_GET_EA_INFORMATION, EaName) + Entry->EaNameLength + 1;

        Total += QueryBytes;
        if ((Entry->Flags & FILE_NEED_EA) != 0)
        {
            ++(*NeedEaCount);
        }

        if (Entry->NextEntryOffset != 0)
        {
            Offset += Entry->NextEntryOffset;
        }
        else
        {
            break;
        }
    }

    return Total;
}

static
const NTFSLX_EA_ENTRY *
NtfslxFindEaEntryByName(
    _In_reads_bytes_(Length) const UCHAR *Buffer,
    _In_ ULONG Length,
    _In_reads_(NameLength) const CHAR *Name,
    _In_ UCHAR NameLength)
{
    ULONG Offset;

    for (Offset = 0; Offset < Length; )
    {
        const NTFSLX_EA_ENTRY *Entry;
        const CHAR *EntryName;

        Entry = (const NTFSLX_EA_ENTRY *)(Buffer + Offset);
        EntryName = (const CHAR *)(Entry + 1);

        if (Entry->EaNameLength == NameLength &&
            RtlEqualMemory(EntryName, Name, NameLength))
        {
            return Entry;
        }

        if (Entry->NextEntryOffset != 0)
        {
            Offset += Entry->NextEntryOffset;
        }
        else
        {
            break;
        }
    }

    return NULL;
}

static
NTSTATUS
NtfslxRemoveEaAttributes(
    _Inout_ PNTFSLX_MFT_RECORD Record)
{
    NTSTATUS Status;
    PNTFSLX_ATTR_RECORD Attr;

    Status = NtfslxFindAttribute(Record, NTFSLX_ATTRIBUTE_EA, NULL, 0, &Attr);
    if (NT_SUCCESS(Status))
    {
        if (Attr->NonResident != 0)
        {
            return STATUS_NOT_SUPPORTED;
        }

        Status = NtfslxRemoveAttributeRecord(Record, Attr);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }
    else if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        return Status;
    }

    Status = NtfslxFindAttribute(Record, NTFSLX_ATTRIBUTE_EA_INFORMATION, NULL, 0, &Attr);
    if (NT_SUCCESS(Status))
    {
        if (Attr->NonResident != 0)
        {
            return STATUS_NOT_SUPPORTED;
        }

        Status = NtfslxRemoveAttributeRecord(Record, Attr);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }
    else if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        return Status;
    }

    return STATUS_SUCCESS;
}

static
NTSTATUS
NtfslxSetEaFileObject(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_opt_(Length) PVOID Buffer,
    _In_ ULONG Length)
{
    PNTFSLX_FILE_CONTEXT FileContext;
    PNTFSLX_DEVICE_EXTENSION DevExt;
    PNTFSLX_MFT_RECORD WorkingRecord = NULL;
    ULONG RecordSize;
    NTSTATUS Status;

    Status = NtfslxValidateEaFileObject(FileObject, &FileContext);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (Length != 0 && Buffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Length != 0)
    {
        Status = NtfslxValidateEaBuffer((const UCHAR *)Buffer, Length);
        if (!NT_SUCCESS(Status))
        {
            return STATUS_EA_LIST_INCONSISTENT;
        }
    }

    DevExt = FileContext->DeviceExtension;
    RecordSize = DevExt->VolumeInfo.BytesPerFileRecord;
    WorkingRecord = ExAllocatePoolWithTag(NonPagedPool, RecordSize, NTFSLX_TAG);
    if (WorkingRecord == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlCopyMemory(WorkingRecord, FileContext->FileRecord, RecordSize);

    Status = NtfslxRemoveEaAttributes(WorkingRecord);
    if (!NT_SUCCESS(Status))
    {
        goto Cleanup;
    }

    if (Length != 0)
    {
        NTFSLX_EA_INFORMATION EaInfo;
        USHORT NeedEaCount;

        if (Length > MAXUSHORT)
        {
            Status = STATUS_EA_LIST_INCONSISTENT;
            goto Cleanup;
        }

        EaInfo.EaLength = (USHORT)Length;
        EaInfo.EaQueryLength = NtfslxPackedEaQueryLength((const UCHAR *)Buffer,
                                                         Length,
                                                         &NeedEaCount);
        EaInfo.NeedEaCount = NeedEaCount;

        Status = NtfslxInsertAttributeRecord(WorkingRecord,
                                             RecordSize,
                                             NTFSLX_ATTRIBUTE_EA_INFORMATION,
                                             NULL,
                                             0,
                                             &EaInfo,
                                             sizeof(EaInfo),
                                             NULL);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }

        Status = NtfslxInsertAttributeRecord(WorkingRecord,
                                             RecordSize,
                                             NTFSLX_ATTRIBUTE_EA,
                                             NULL,
                                             0,
                                             Buffer,
                                             Length,
                                             NULL);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }
    }

    Status = NtfslxWriteMftRecord(DevExt->StorageDevice,
                                  &DevExt->VolumeInfo,
                                  DevExt->MftRunlist,
                                  FileContext->MftIndex,
                                  WorkingRecord);
    if (NT_SUCCESS(Status))
    {
        RtlCopyMemory(FileContext->FileRecord, WorkingRecord, RecordSize);
    }

Cleanup:
    if (WorkingRecord != NULL)
    {
        ExFreePoolWithTag(WorkingRecord, NTFSLX_TAG);
    }

    return Status;
}

static
NTSTATUS
NtfslxAppendEaEntry(
    _Inout_updates_bytes_(OutputLength) PUCHAR OutputBuffer,
    _In_ ULONG OutputLength,
    _Inout_ PULONG BytesWritten,
    _In_ const NTFSLX_EA_ENTRY *Entry,
    _In_ BOOLEAN LastEntry)
{
    ULONG EntryBytes;
    PFILE_FULL_EA_INFORMATION Dest;

    EntryBytes = sizeof(NTFSLX_EA_ENTRY) + Entry->EaNameLength + 1 + Entry->EaValueLength;
    if (*BytesWritten + EntryBytes > OutputLength)
    {
        return (*BytesWritten == 0) ? STATUS_BUFFER_OVERFLOW : STATUS_BUFFER_TOO_SMALL;
    }

    Dest = (PFILE_FULL_EA_INFORMATION)(OutputBuffer + *BytesWritten);
    RtlCopyMemory(Dest, Entry, EntryBytes);
    Dest->NextEntryOffset = LastEntry ? 0 : EntryBytes;
    *BytesWritten += EntryBytes;
    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxQueryEa(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PNTFSLX_FILE_CONTEXT FileContext;
    PUCHAR OutputBuffer;
    PVOID EaBuffer = NULL;
    ULONG EaBufferLength = 0;
    ULONG BytesWritten = 0;
    NTSTATUS Status;
    ULONG Remaining;
    ULONG Offset;

    UNREFERENCED_PARAMETER(DeviceObject);

    Stack = IoGetCurrentIrpStackLocation(Irp);
    Status = NtfslxValidateEaFileObject(Stack->FileObject, &FileContext);
    if (!NT_SUCCESS(Status))
    {
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = Status;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return Status;
    }

    OutputBuffer = NtfslxGetEaIrpBuffer(Irp);
    if (Stack->Parameters.QueryEa.Length != 0 && OutputBuffer == NULL)
    {
        Status = STATUS_INVALID_USER_BUFFER;
        goto Complete;
    }

    Status = NtfslxReadEaData(FileContext->DeviceExtension->StorageDevice,
                              &FileContext->DeviceExtension->VolumeInfo,
                              FileContext->DeviceExtension->MftRunlist,
                              FileContext->MftIndex,
                              &EaBuffer,
                              &EaBufferLength);
    if (Status == STATUS_NOT_FOUND)
    {
        Status = STATUS_NO_EAS_ON_FILE;
        goto Complete;
    }
    if (!NT_SUCCESS(Status))
    {
        goto Complete;
    }

    if (Stack->Parameters.QueryEa.EaList != NULL &&
        Stack->Parameters.QueryEa.EaListLength != 0)
    {
        const UCHAR *QueryBuffer;

        QueryBuffer = (const UCHAR *)Stack->Parameters.QueryEa.EaList;
        Remaining = Stack->Parameters.QueryEa.EaListLength;
        while (Remaining != 0)
        {
            const FILE_GET_EA_INFORMATION *QueryEntry;
            const NTFSLX_EA_ENTRY *Match;
            ULONG QueryBytes;
            BOOLEAN LastQuery;

            if (Remaining < FIELD_OFFSET(FILE_GET_EA_INFORMATION, EaName) + 1)
            {
                Status = STATUS_EA_LIST_INCONSISTENT;
                goto Complete;
            }

            QueryEntry = (const FILE_GET_EA_INFORMATION *)QueryBuffer;
            QueryBytes = FIELD_OFFSET(FILE_GET_EA_INFORMATION, EaName) + QueryEntry->EaNameLength + 1;
            if (QueryBytes > Remaining)
            {
                Status = STATUS_EA_LIST_INCONSISTENT;
                goto Complete;
            }

            Match = NtfslxFindEaEntryByName((const UCHAR *)EaBuffer,
                                            EaBufferLength,
                                            QueryEntry->EaName,
                                            QueryEntry->EaNameLength);
            if (Match != NULL)
            {
                LastQuery = ((Stack->Flags & SL_RETURN_SINGLE_ENTRY) != 0) ||
                            QueryEntry->NextEntryOffset == 0;
                Status = NtfslxAppendEaEntry(OutputBuffer,
                                             Stack->Parameters.QueryEa.Length,
                                             &BytesWritten,
                                             Match,
                                             LastQuery);
                if (!NT_SUCCESS(Status))
                {
                    goto Complete;
                }

                if ((Stack->Flags & SL_RETURN_SINGLE_ENTRY) != 0)
                {
                    break;
                }
            }

            if (QueryEntry->NextEntryOffset == 0)
            {
                break;
            }

            if (QueryEntry->NextEntryOffset > Remaining)
            {
                Status = STATUS_EA_LIST_INCONSISTENT;
                goto Complete;
            }

            QueryBuffer += QueryEntry->NextEntryOffset;
            Remaining -= QueryEntry->NextEntryOffset;
        }

        if (BytesWritten == 0)
        {
            Status = STATUS_NONEXISTENT_EA_ENTRY;
            goto Complete;
        }
    }
    else
    {
        ULONG EntryIndex;

        Offset = 0;
        EntryIndex = 0;
        while (Offset < EaBufferLength)
        {
            const NTFSLX_EA_ENTRY *Entry;
            BOOLEAN LastEntry;

            Entry = (const NTFSLX_EA_ENTRY *)((const UCHAR *)EaBuffer + Offset);
            if ((Stack->Flags & SL_INDEX_SPECIFIED) != 0 &&
                EntryIndex < Stack->Parameters.QueryEa.EaIndex)
            {
                ++EntryIndex;
            }
            else
            {
                LastEntry = ((Stack->Flags & SL_RETURN_SINGLE_ENTRY) != 0) ||
                            Entry->NextEntryOffset == 0;
                Status = NtfslxAppendEaEntry(OutputBuffer,
                                             Stack->Parameters.QueryEa.Length,
                                             &BytesWritten,
                                             Entry,
                                             LastEntry);
                if (!NT_SUCCESS(Status))
                {
                    goto Complete;
                }

                if ((Stack->Flags & SL_RETURN_SINGLE_ENTRY) != 0)
                {
                    break;
                }
            }

            if (Entry->NextEntryOffset == 0)
            {
                break;
            }

            Offset += Entry->NextEntryOffset;
            ++EntryIndex;
        }

        if (BytesWritten == 0)
        {
            Status = STATUS_NO_EAS_ON_FILE;
            goto Complete;
        }
    }

    Status = STATUS_SUCCESS;

Complete:
    if (EaBuffer != NULL)
    {
        ExFreePoolWithTag(EaBuffer, NTFSLX_TAG);
    }

    Irp->IoStatus.Information = NT_SUCCESS(Status) ? BytesWritten : 0;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

NTSTATUS
NtfslxSetEa(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION Stack;
    PVOID Buffer;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    Stack = IoGetCurrentIrpStackLocation(Irp);
    Buffer = NtfslxGetEaIrpBuffer(Irp);
    Status = NtfslxSetEaFileObject(Stack->FileObject,
                                   Buffer,
                                   Stack->Parameters.SetEa.Length);
    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
