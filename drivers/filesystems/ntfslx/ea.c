/*
 * Extended attribute (EA) reading for the ntfslx ReactOS driver.
 *
 * Reads $EA_INFORMATION (type 0xD0) and $EA (type 0xE0) attributes.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

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
        /* Non-resident EA: need to read via runlist */
        DbgPrint("NTFSLX: Non-resident $EA not yet supported\n");
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

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
