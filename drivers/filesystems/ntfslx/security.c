/*
 * Security descriptor reading for the ntfslx ReactOS driver.
 *
 * Reads the $SECURITY_DESCRIPTOR attribute from an MFT record
 * and returns a self-relative security descriptor.
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
 * NtfslxReadSecurityDescriptor - read $SECURITY_DESCRIPTOR from an inode
 *
 * Allocates and returns the security descriptor buffer.
 * Caller must free with ExFreePoolWithTag.
 *
 * Returns STATUS_SUCCESS or appropriate error.
 * Returns STATUS_NOT_FOUND if the file has no security descriptor attribute.
 */
NTSTATUS
NtfslxReadSecurityDescriptor(
    _In_ PDEVICE_OBJECT StorageDevice,
    _In_ PNTFSLX_VOLUME_INFO VolumeInfo,
    _In_ PNTFSLX_RUNLIST_ELEMENT MftRunlist,
    _In_ ULONGLONG MftIndex,
    _Outptr_ PSECURITY_DESCRIPTOR *SecurityDescriptor,
    _Out_ PULONG SecurityDescriptorLength)
{
    NTSTATUS Status;
    PNTFSLX_MFT_RECORD FileRecord = NULL;
    PNTFSLX_ATTR_RECORD SdAttr = NULL;
    PVOID SdBuffer = NULL;
    ULONG SdLength;

    *SecurityDescriptor = NULL;
    *SecurityDescriptorLength = 0;

    /* Read the MFT record */
    FileRecord = ExAllocatePoolWithTag(
        NonPagedPool, VolumeInfo->BytesPerFileRecord, NTFSLX_TAG);
    if (FileRecord == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = NtfslxReadMftRecord(
        StorageDevice, VolumeInfo, MftRunlist,
        MftIndex, FileRecord);
    if (!NT_SUCCESS(Status))
        goto Cleanup;

    /* Find $SECURITY_DESCRIPTOR attribute */
    Status = NtfslxFindAttribute(
        FileRecord, NTFSLX_ATTRIBUTE_SECURITY_DESCRIPTOR,
        NULL, 0, &SdAttr);
    if (!NT_SUCCESS(Status))
    {
        /* No SD attribute - not an error per se, but caller should know */
        Status = STATUS_NOT_FOUND;
        goto Cleanup;
    }

    if (SdAttr->NonResident != 0)
    {
        Status = NtfslxReadNonResidentAttributeBuffer(StorageDevice,
                                                      VolumeInfo,
                                                      SdAttr,
                                                      &SdBuffer,
                                                      &SdLength);
        if (!NT_SUCCESS(Status))
        {
            goto Cleanup;
        }
    }
    else
    {
        SdLength = SdAttr->Data.Resident.ValueLength;
        if (SdLength < sizeof(NTFSLX_SECURITY_DESCRIPTOR_RELATIVE))
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }

        SdBuffer = ExAllocatePoolWithTag(NonPagedPool, SdLength, NTFSLX_TAG);
        if (SdBuffer == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        RtlCopyMemory(SdBuffer,
            (PUCHAR)SdAttr + SdAttr->Data.Resident.ValueOffset,
            SdLength);
    }

    if (SdLength < sizeof(NTFSLX_SECURITY_DESCRIPTOR_RELATIVE) ||
        !RtlValidRelativeSecurityDescriptor((PSECURITY_DESCRIPTOR)SdBuffer,
                                            SdLength,
                                            0))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }

    *SecurityDescriptor = SdBuffer;
    *SecurityDescriptorLength = SdLength;
    SdBuffer = NULL;
    Status = STATUS_SUCCESS;

Cleanup:
    if (SdBuffer != NULL)
        ExFreePoolWithTag(SdBuffer, NTFSLX_TAG);
    if (FileRecord != NULL)
        ExFreePoolWithTag(FileRecord, NTFSLX_TAG);

    return Status;
}
