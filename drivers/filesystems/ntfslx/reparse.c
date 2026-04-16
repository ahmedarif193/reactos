/*
 * Reparse point reading for the ntfslx ReactOS driver.
 *
 * Reads the $REPARSE_POINT attribute (type 0xC0) from an MFT record.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "ntfslx.h"

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
        DbgPrint("NTFSLX: Non-resident $REPARSE_POINT not yet supported\n");
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    ValueLength = RpAttr->Data.Resident.ValueLength;
    if (ValueLength < sizeof(NTFSLX_REPARSE_POINT))
    {
        Status = STATUS_FILE_CORRUPT_ERROR;
        goto Cleanup;
    }

    RpHeader = (PNTFSLX_REPARSE_POINT)(
        (PUCHAR)RpAttr + RpAttr->Data.Resident.ValueOffset);

    *ReparseTag = RpHeader->ReparseTag;
    DataLen = RpHeader->ReparseDataLength;

    if (ReparseData != NULL && DataLen > 0)
    {
        if ((ULONG)sizeof(NTFSLX_REPARSE_POINT) + DataLen > ValueLength)
        {
            Status = STATUS_FILE_CORRUPT_ERROR;
            goto Cleanup;
        }

        DataBuffer = ExAllocatePoolWithTag(NonPagedPool, DataLen, NTFSLX_TAG);
        if (DataBuffer == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Cleanup;
        }

        RtlCopyMemory(DataBuffer,
            (PUCHAR)RpHeader + sizeof(NTFSLX_REPARSE_POINT),
            DataLen);

        *ReparseData = DataBuffer;
        DataBuffer = NULL;
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
