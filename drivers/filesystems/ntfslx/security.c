/*
 * Security descriptor reading for the ntfslx ReactOS driver.
 *
 * Reads the $SECURITY_DESCRIPTOR attribute from an MFT record
 * and returns a self-relative security descriptor.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

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
        /* Non-resident security descriptors are unusual but legal */
        /* For now, we only handle resident */
        DbgPrint("NTFSLX: Non-resident $SECURITY_DESCRIPTOR not yet supported\n");
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

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
