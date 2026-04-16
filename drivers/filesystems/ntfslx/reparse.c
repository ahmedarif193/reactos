/*
 * Reparse point reading for the ntfslx ReactOS driver.
 *
 * Reads the $REPARSE_POINT attribute (type 0xC0) from an MFT record.
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
        return STATUS_FILE_CORRUPT_ERROR;
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

/*
 * NtfslxReadReparsePoint - read reparse point data from a file
 *
 * Reads the $REPARSE_POINT attribute and returns the tag and optional data.
 * Caller must free *ReparseData with ExFreePoolWithTag if non-NULL.
 *
 * Returns STATUS_SUCCESS if a reparse point was found.
 * Returns STATUS_NOT_A_REPARSE_POINT if the file has no reparse attribute.
 */
NTSTATUS
NtfslxReadReparsePoint(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG MftIndex,
    _Out_ PULONG ReparseTag,
    _Outptr_opt_ PVOID *ReparseData,
    _Out_opt_ PUSHORT ReparseDataLength)
{
    NTSTATUS Status;
    PNTFSLX_MFT_RECORD FileRecord = NULL;
    PNTFSLX_ATTR_RECORD RpAttr = NULL;
    PNTFSLX_REPARSE_POINT RpHeader;
    PVOID DataBuffer = NULL;
    ULONG ValueLength;
    USHORT DataLen;

    *ReparseTag = 0;
    if (ReparseData != NULL)
        *ReparseData = NULL;
    if (ReparseDataLength != NULL)
        *ReparseDataLength = 0;

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
        FileRecord, NTFSLX_ATTRIBUTE_REPARSE_POINT,
        NULL, 0, &RpAttr);
    if (!NT_SUCCESS(Status))
    {
        Status = STATUS_NOT_A_REPARSE_POINT;
        goto Cleanup;
    }

    if (RpAttr->NonResident != 0)
    {
        Status = NtfslxReadNonResidentAttributeBuffer(StorageDevice,
                                                      VolumeInfo,
                                                      RpAttr,
                                                      &DataBuffer,
                                                      &ValueLength);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }
    }
    else
    {
        ValueLength = RpAttr->Data.Resident.ValueLength;
        if (ValueLength < sizeof(NTFSLX_REPARSE_POINT))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }

        DataBuffer = ExAllocatePoolWithTag(NonPagedPool, ValueLength, NTFSLX_TAG);
        if (DataBuffer == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        RtlCopyMemory(DataBuffer,
            (PUCHAR)RpAttr + RpAttr->Data.Resident.ValueOffset,
            ValueLength);
    }

    RpHeader = (PNTFSLX_REPARSE_POINT)DataBuffer;
    if (ValueLength < sizeof(NTFSLX_REPARSE_POINT) ||
        RpHeader == NULL)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }

    *ReparseTag = RpHeader->ReparseTag;
    DataLen = RpHeader->ReparseDataLength;

    if ((ULONG)sizeof(NTFSLX_REPARSE_POINT) + DataLen > ValueLength)
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }

    if (ReparseData != NULL && DataLen > 0)
    {
        PVOID ReparseCopy;

        ReparseCopy = ExAllocatePoolWithTag(NonPagedPool, DataLen, NTFSLX_TAG);
        if (ReparseCopy == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        RtlCopyMemory(ReparseCopy,
            (PUCHAR)RpHeader + sizeof(NTFSLX_REPARSE_POINT),
            DataLen);

        *ReparseData = ReparseCopy;
    }

    if (ReparseDataLength != NULL)
        *ReparseDataLength = DataLen;

    Status = STATUS_SUCCESS;

Cleanup:
    if (DataBuffer != NULL)
        ExFreePoolWithTag(DataBuffer, NTFSLX_TAG);
    if (FileRecord != NULL)
        ExFreePoolWithTag(FileRecord, NTFSLX_TAG);

    return Status;
}

static
NTSTATUS
NtfslxValidateReparseFileObject(
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
NTSTATUS
NtfslxValidateUserReparseBuffer(
    _In_ ULONG BufferLength,
    _In_ PREPARSE_DATA_BUFFER ReparseBuffer)
{
    ULONG ExpectedLength;

    if (ReparseBuffer == NULL ||
        BufferLength < REPARSE_DATA_BUFFER_HEADER_SIZE ||
        BufferLength > MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ExpectedLength = REPARSE_DATA_BUFFER_HEADER_SIZE + ReparseBuffer->ReparseDataLength;
    if (ExpectedLength != BufferLength)
    {
        return STATUS_INVALID_PARAMETER;
    }

    switch (ReparseBuffer->ReparseTag)
    {
        case IO_REPARSE_TAG_MOUNT_POINT:
            if (BufferLength < FIELD_OFFSET(REPARSE_DATA_BUFFER,
                                            MountPointReparseBuffer.PathBuffer) +
                                  sizeof(WCHAR))
            {
                return STATUS_INVALID_PARAMETER;
            }
            break;

        case IO_REPARSE_TAG_SYMLINK:
            if (BufferLength < FIELD_OFFSET(REPARSE_DATA_BUFFER,
                                            SymbolicLinkReparseBuffer.PathBuffer) +
                                  sizeof(WCHAR))
            {
                return STATUS_INVALID_PARAMETER;
            }
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    return STATUS_SUCCESS;
}

static
VOID
NtfslxUpdateReparseAttributeFlags(
    _Inout_ PNTFSLX_MFT_RECORD Record,
    _In_ BOOLEAN SetReparse)
{
    PUCHAR Base;
    ULONG Offset;

    Base = (PUCHAR)Record;
    Offset = Record->AttributesOffset;

    while (Offset + sizeof(ULONG) * 2 <= Record->BytesInUse)
    {
        PNTFSLX_ATTR_RECORD Attr;

        Attr = (PNTFSLX_ATTR_RECORD)(Base + Offset);
        if (Attr->Type == NTFSLX_ATTRIBUTE_END)
        {
            break;
        }

        if (Attr->Length == 0 || Offset + Attr->Length > Record->BytesInUse)
        {
            break;
        }

        if (Attr->NonResident == 0)
        {
            if (Attr->Type == NTFSLX_ATTRIBUTE_STANDARD_INFORMATION &&
                Attr->Data.Resident.ValueLength >= sizeof(NTFSLX_STANDARD_INFORMATION))
            {
                PNTFSLX_STANDARD_INFORMATION StdInfo;

                StdInfo = (PNTFSLX_STANDARD_INFORMATION)
                    ((PUCHAR)Attr + Attr->Data.Resident.ValueOffset);
                if (SetReparse)
                {
                    StdInfo->FileAttributes |= FILE_ATTRIBUTE_REPARSE_POINT;
                }
                else
                {
                    StdInfo->FileAttributes &= ~FILE_ATTRIBUTE_REPARSE_POINT;
                }
            }
            else if (Attr->Type == NTFSLX_ATTRIBUTE_FILE_NAME &&
                     Attr->Data.Resident.ValueLength >= sizeof(NTFSLX_FILE_NAME_ATTRIBUTE))
            {
                PNTFSLX_FILE_NAME_ATTRIBUTE FileName;

                FileName = (PNTFSLX_FILE_NAME_ATTRIBUTE)
                    ((PUCHAR)Attr + Attr->Data.Resident.ValueOffset);
                if (SetReparse)
                {
                    FileName->FileAttributes |= FILE_ATTRIBUTE_REPARSE_POINT;
                }
                else
                {
                    FileName->FileAttributes &= ~FILE_ATTRIBUTE_REPARSE_POINT;
                }
            }
        }

        Offset += Attr->Length;
    }
}

NTSTATUS
NtfslxSetReparsePoint(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength)
{
    PNTFSLX_FILE_CONTEXT FileContext;
    PNTFSLX_DEVICE_EXTENSION DevExt;
    PREPARSE_DATA_BUFFER ReparseBuffer;
    PNTFSLX_ATTR_RECORD ReparseAttr;
    ULONG RecordSize;
    NTSTATUS Status;

    Status = NtfslxValidateReparseFileObject(FileObject, &FileContext);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (InputBuffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (InputLength < REPARSE_DATA_BUFFER_HEADER_SIZE ||
        InputLength > MAXIMUM_REPARSE_DATA_BUFFER_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    ReparseBuffer = (PREPARSE_DATA_BUFFER)InputBuffer;
    Status = NtfslxValidateUserReparseBuffer(InputLength, ReparseBuffer);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    DbgPrint("ntfslx: reparse set begin mft=%I64u tag=0x%08lx in=%lu path='%wZ'\n",
             FileContext->MftIndex,
             ReparseBuffer->ReparseTag,
             InputLength,
             &FileContext->FullPath);

    DevExt = FileContext->DeviceExtension;
    RecordSize = DevExt->VolumeInfo.BytesPerFileRecord;

    Status = NtfslxFindAttribute(FileContext->FileRecord,
                                 NTFSLX_ATTRIBUTE_REPARSE_POINT,
                                 NULL,
                                 0,
                                 &ReparseAttr);
    if (NT_SUCCESS(Status))
    {
        if (ReparseAttr->NonResident != 0)
        {
            return STATUS_NOT_SUPPORTED;
        }

        Status = NtfslxRemoveAttributeRecord(FileContext->FileRecord,
                                             ReparseAttr);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }
    }
    else if (Status != STATUS_OBJECT_NAME_NOT_FOUND)
    {
        return Status;
    }

    Status = NtfslxInsertAttributeRecord(FileContext->FileRecord,
                                         RecordSize,
                                         NTFSLX_ATTRIBUTE_REPARSE_POINT,
                                         NULL,
                                         0,
                                         InputBuffer,
                                         InputLength,
                                         &ReparseAttr);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    NtfslxUpdateReparseAttributeFlags(FileContext->FileRecord, TRUE);

    Status = NtfslxWriteMftRecord(DevExt->StorageDevice,
                                 &DevExt->VolumeInfo,
                                 DevExt->MftRunlist,
                                 FileContext->MftIndex,
                                 FileContext->FileRecord);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("ntfslx: reparse set write failed mft=%I64u status=0x%08lx\n",
                 FileContext->MftIndex,
                 Status);
        return Status;
    }

    DbgPrint("ntfslx: reparse set done mft=%I64u tag=0x%08lx bytes=%lu\n",
             FileContext->MftIndex,
             ReparseBuffer->ReparseTag,
             InputLength);
    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxGetReparsePoint(
    _In_ PFILE_OBJECT FileObject,
    _Out_writes_bytes_to_(OutputLength, *ReturnLength) PVOID OutputBuffer,
    _In_ ULONG OutputLength,
    _Out_ PULONG ReturnLength)
{
    PNTFSLX_FILE_CONTEXT FileContext;
    PVOID ReparseData = NULL;
    ULONG ReparseTag = 0;
    USHORT ReparseDataLength = 0;
    ULONG RequiredLength;
    PREPARSE_DATA_BUFFER OutBuffer;
    NTSTATUS Status;

    if (ReturnLength != NULL)
    {
        *ReturnLength = 0;
    }

    Status = NtfslxValidateReparseFileObject(FileObject, &FileContext);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (OutputBuffer == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Status = NtfslxReadReparsePoint(FileContext->DeviceExtension->StorageDevice,
                                    &FileContext->DeviceExtension->VolumeInfo,
                                    FileContext->DeviceExtension->MftRunlist,
                                    FileContext->MftIndex,
                                    &ReparseTag,
                                    &ReparseData,
                                    &ReparseDataLength);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    RequiredLength = REPARSE_DATA_BUFFER_HEADER_SIZE + ReparseDataLength;
    if (ReturnLength != NULL)
    {
        *ReturnLength = RequiredLength;
    }

    if (OutputLength < RequiredLength)
    {
        if (ReparseData != NULL)
        {
            ExFreePoolWithTag(ReparseData, NTFSLX_TAG);
        }
        return STATUS_BUFFER_OVERFLOW;
    }

    OutBuffer = (PREPARSE_DATA_BUFFER)OutputBuffer;
    RtlZeroMemory(OutBuffer, RequiredLength);
    OutBuffer->ReparseTag = ReparseTag;
    OutBuffer->ReparseDataLength = ReparseDataLength;
    OutBuffer->Reserved = 0;
    if (ReparseDataLength > 0 && ReparseData != NULL)
    {
        RtlCopyMemory(OutBuffer->GenericReparseBuffer.DataBuffer,
                      ReparseData,
                      ReparseDataLength);
    }

    if (ReparseData != NULL)
    {
        ExFreePoolWithTag(ReparseData, NTFSLX_TAG);
    }

    DbgPrint("ntfslx: reparse get done mft=%I64u tag=0x%08lx bytes=%lu\n",
             FileContext->MftIndex,
             ReparseTag,
             RequiredLength);
    return STATUS_SUCCESS;
}

NTSTATUS
NtfslxDeleteReparsePoint(
    _In_ PFILE_OBJECT FileObject,
    _In_reads_bytes_(InputLength) PVOID InputBuffer,
    _In_ ULONG InputLength)
{
    PNTFSLX_FILE_CONTEXT FileContext;
    PREPARSE_DATA_BUFFER DeleteBuffer;
    PNTFSLX_ATTR_RECORD ReparseAttr;
    PNTFSLX_REPARSE_POINT ExistingHeader;
    NTSTATUS Status;

    Status = NtfslxValidateReparseFileObject(FileObject, &FileContext);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    if (InputBuffer == NULL || InputLength < REPARSE_DATA_BUFFER_HEADER_SIZE)
    {
        return STATUS_INVALID_PARAMETER;
    }

    DeleteBuffer = (PREPARSE_DATA_BUFFER)InputBuffer;
    if (DeleteBuffer->ReparseDataLength != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (DeleteBuffer->ReparseTag != IO_REPARSE_TAG_MOUNT_POINT &&
        DeleteBuffer->ReparseTag != IO_REPARSE_TAG_SYMLINK)
    {
        return STATUS_NOT_SUPPORTED;
    }

    DbgPrint("ntfslx: reparse delete begin mft=%I64u tag=0x%08lx path='%wZ'\n",
             FileContext->MftIndex,
             DeleteBuffer->ReparseTag,
             &FileContext->FullPath);

    Status = NtfslxFindAttribute(FileContext->FileRecord,
                                 NTFSLX_ATTRIBUTE_REPARSE_POINT,
                                 NULL,
                                 0,
                                 &ReparseAttr);
    if (!NT_SUCCESS(Status))
    {
        return STATUS_NOT_A_REPARSE_POINT;
    }

    if (ReparseAttr->NonResident == 0)
    {
        if (ReparseAttr->Data.Resident.ValueLength < sizeof(NTFSLX_REPARSE_POINT))
        {
            return STATUS_FILE_CORRUPT_ERROR;
        }

        ExistingHeader = (PNTFSLX_REPARSE_POINT)
            ((PUCHAR)ReparseAttr + ReparseAttr->Data.Resident.ValueOffset);
        if (ExistingHeader->ReparseTag != DeleteBuffer->ReparseTag)
        {
            return STATUS_IO_REPARSE_TAG_MISMATCH;
        }
    }
    else
    {
        ULONG ExistingTag = 0;

        Status = NtfslxReadReparsePoint(FileContext->DeviceExtension->StorageDevice,
                                        &FileContext->DeviceExtension->VolumeInfo,
                                        FileContext->DeviceExtension->MftRunlist,
                                        FileContext->MftIndex,
                                        &ExistingTag,
                                        NULL,
                                        NULL);
        if (!NT_SUCCESS(Status))
        {
            return Status;
        }

        if (ExistingTag != DeleteBuffer->ReparseTag)
        {
            return STATUS_IO_REPARSE_TAG_MISMATCH;
        }

        return STATUS_NOT_SUPPORTED;
    }

    Status = NtfslxRemoveAttributeRecord(FileContext->FileRecord,
                                         ReparseAttr);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    NtfslxUpdateReparseAttributeFlags(FileContext->FileRecord, FALSE);

    Status = NtfslxWriteMftRecord(FileContext->DeviceExtension->StorageDevice,
                                  &FileContext->DeviceExtension->VolumeInfo,
                                  FileContext->DeviceExtension->MftRunlist,
                                  FileContext->MftIndex,
                                  FileContext->FileRecord);
    if (!NT_SUCCESS(Status))
    {
        DbgPrint("ntfslx: reparse delete write failed mft=%I64u status=0x%08lx\n",
                 FileContext->MftIndex,
                 Status);
        return Status;
    }

    DbgPrint("ntfslx: reparse delete done mft=%I64u tag=0x%08lx\n",
             FileContext->MftIndex,
             DeleteBuffer->ReparseTag);
    return STATUS_SUCCESS;
}
